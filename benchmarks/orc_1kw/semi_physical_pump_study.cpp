#include "thermox/platform/semi_physical_positive_displacement_pump.hpp"
#include "thermox/physics/coolprop_heos_package.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <numbers>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Row = std::map<std::string, double, std::less<>>;
constexpr std::size_t calibration_case_count = 68;
constexpr double fixed_discharge_coefficient = 0.8;

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::istringstream input(line);
    for (std::string field; std::getline(input, field, ',');)
        fields.push_back(std::move(field));
    return fields;
}

std::vector<Row> read_csv(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open measurement CSV: " + path);
    std::string line;
    if (!std::getline(input, line))
        throw std::runtime_error("measurement CSV is empty");
    const auto headers = split_csv_line(line);
    std::vector<Row> rows;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        const auto fields = split_csv_line(line);
        if (fields.size() != headers.size())
            throw std::runtime_error("measurement CSV row width mismatch");
        Row row;
        for (std::size_t i = 0; i < headers.size(); ++i)
            row.emplace(headers.at(i), std::stod(fields.at(i)));
        rows.push_back(std::move(row));
    }
    if (rows.size() != 77U)
        throw std::runtime_error("expected exactly 77 complete source cases");
    return rows;
}

double value(const Row& row, std::string_view key) {
    const auto found = row.find(key);
    if (found == row.end())
        throw std::runtime_error("missing measurement column: " + std::string{key});
    return found->second;
}

struct Point {
    double inlet_pressure{};
    double inlet_enthalpy{};
    double outlet_pressure{};
    double angular_speed{};
    double density{};
    double isentropic_enthalpy_rise{};
    double measured_mass_flow{};
    double measured_outlet_enthalpy{};
    double measured_outlet_temperature{};
};

std::vector<Point> make_points(
    const std::vector<Row>& rows,
    const thermox::physics::PropertyPackage& fluid) {
    std::vector<Point> points;
    points.reserve(rows.size());
    for (const auto& row : rows) {
        const double inlet_pressure =
            value(row, "pump_inlet_pressure_kpa") * 1000.0;
        const double outlet_pressure =
            value(row, "pump_outlet_pressure_kpa") * 1000.0;
        const auto inlet = fluid.state_pt(
            inlet_pressure,
            value(row, "pump_inlet_temperature_c") + 273.15);
        const auto outlet = fluid.state_pt(
            outlet_pressure,
            value(row, "pump_outlet_temperature_c") + 273.15);
        if (!inlet.ok() || !outlet.ok())
            throw std::runtime_error("could not reconstruct measured pump state");
        const auto isentropic = fluid.state_ps(
            outlet_pressure, inlet.state.entropy_j_kg_k);
        if (!isentropic.ok())
            throw std::runtime_error("could not reconstruct isentropic pump state");
        points.push_back({
            inlet_pressure,
            inlet.state.enthalpy_j_kg,
            outlet_pressure,
            value(row, "pump_speed_rpm") *
                2.0 * std::numbers::pi / 60.0,
            inlet.state.density_kg_m3,
            isentropic.state.enthalpy_j_kg - inlet.state.enthalpy_j_kg,
            value(row, "working_fluid_mass_flow_g_s") / 1000.0,
            outlet.state.enthalpy_j_kg,
            outlet.state.temperature_k,
        });
    }
    return points;
}

struct Fit {
    double displacement{};
    double leakage_area{};
    double efficiency{};
    double capacity_condition_number{};
};

Fit fit(const std::vector<Point>& points, const std::set<std::size_t>& training) {
    double aa = 0.0;
    double ab = 0.0;
    double bb = 0.0;
    double ay = 0.0;
    double by = 0.0;
    double isentropic_square = 0.0;
    double isentropic_measured = 0.0;
    for (const auto index : training) {
        const auto& point = points.at(index);
        const double a = point.density * point.angular_speed /
            (2.0 * std::numbers::pi);
        const double b = -fixed_discharge_coefficient * std::sqrt(
            2.0 * point.density *
            (point.outlet_pressure - point.inlet_pressure));
        aa += a * a;
        ab += a * b;
        bb += b * b;
        ay += a * point.measured_mass_flow;
        by += b * point.measured_mass_flow;
        const double measured_rise =
            point.measured_outlet_enthalpy - point.inlet_enthalpy;
        if (measured_rise > 0.0 && point.isentropic_enthalpy_rise > 0.0) {
            isentropic_square += point.isentropic_enthalpy_rise *
                point.isentropic_enthalpy_rise;
            isentropic_measured += point.isentropic_enthalpy_rise *
                measured_rise;
        }
    }
    const double determinant = aa * bb - ab * ab;
    if (!(determinant > 0.0) || !(isentropic_measured > 0.0))
        throw std::runtime_error("pump calibration is structurally singular");
    const double trace = aa + bb;
    const double discriminant = std::sqrt(
        std::max(0.0, trace * trace - 4.0 * determinant));
    const double largest = 0.5 * (trace + discriminant);
    const double smallest = 0.5 * (trace - discriminant);
    const double efficiency = isentropic_square / isentropic_measured;
    if (!(efficiency > 0.0 && efficiency <= 1.0))
        throw std::runtime_error(
            "measured pump PT states imply a nonphysical aggregate efficiency");
    return {
        (ay * bb - by * ab) / determinant,
        (by * aa - ay * ab) / determinant,
        efficiency,
        std::sqrt(largest / smallest),
    };
}

struct Metrics {
    std::size_t count{};
    double absolute_sum{};
    double absolute_max{};
    double relative_sum{};
    double relative_max{};
    double relative_bias{};
};

void accumulate(Metrics& metrics, double predicted, double measured) {
    const double absolute = std::abs(predicted - measured);
    const double relative = (predicted - measured) / measured;
    ++metrics.count;
    metrics.absolute_sum += absolute;
    metrics.absolute_max = std::max(metrics.absolute_max, absolute);
    metrics.relative_sum += std::abs(relative);
    metrics.relative_max = std::max(metrics.relative_max, std::abs(relative));
    metrics.relative_bias += relative;
}

void print_metrics(std::ostream& out, const Metrics& metrics) {
    out << "{\"count\":" << metrics.count
        << ",\"mean_absolute_error_si\":"
        << metrics.absolute_sum / metrics.count
        << ",\"maximum_absolute_error_si\":" << metrics.absolute_max
        << ",\"mape_percent\":"
        << 100.0 * metrics.relative_sum / metrics.count
        << ",\"maximum_absolute_relative_error_percent\":"
        << 100.0 * metrics.relative_max
        << ",\"mean_bias_percent\":"
        << 100.0 * metrics.relative_bias / metrics.count << '}';
}

void print_fit(std::ostream& out, const Fit& fitted) {
    out << "{\"displacement_volume_per_revolution\":"
        << fitted.displacement
        << ",\"effective_leakage_area_at_fixed_cd_0_8\":"
        << fitted.leakage_area
        << ",\"aggregate_isentropic_efficiency\":"
        << fitted.efficiency
        << ",\"capacity_design_matrix_condition_number\":"
        << fitted.capacity_condition_number << '}';
}

void evaluate(
    const std::vector<Point>& points,
    const std::set<std::size_t>& validation,
    const Fit& fitted,
    const thermox::physics::PropertyPackage& fluid,
    Metrics& mass,
    Metrics& temperature) {
    for (const auto index : validation) {
        const auto& point = points.at(index);
        thermox::platform::SemiPhysicalPositiveDisplacementPumpEvaluation result;
        const auto status = thermox::platform::
            evaluate_semi_physical_positive_displacement_pump(
                fluid, point.inlet_pressure, point.inlet_enthalpy,
                point.outlet_pressure, point.angular_speed,
                {fitted.displacement, fitted.leakage_area,
                 fixed_discharge_coefficient, fitted.efficiency},
                result);
        if (!status.ok()) throw std::runtime_error(status.message);
        const auto outlet = fluid.state_ph(
            point.outlet_pressure, result.outlet_enthalpy);
        if (!outlet.ok()) throw std::runtime_error(outlet.message);
        accumulate(mass, result.mass_flow, point.measured_mass_flow);
        accumulate(
            temperature, outlet.state.temperature_k,
            point.measured_outlet_temperature);
    }
}

std::set<std::size_t> range(std::size_t first, std::size_t last) {
    std::set<std::size_t> result;
    for (std::size_t index = first; index <= last; ++index)
        result.insert(index);
    return result;
}

std::set<std::size_t> complement(const std::set<std::size_t>& held_out) {
    std::set<std::size_t> result;
    for (std::size_t index = 0; index < calibration_case_count; ++index)
        if (!held_out.contains(index)) result.insert(index);
    return result;
}

void run(
    const std::vector<Point>& points,
    const thermox::physics::PropertyPackage& fluid) {
    struct Fold { std::string id; std::set<std::size_t> validation; };
    const std::vector<Fold> folds{
        {"charge_sweeps", range(0, 28)},
        {"sink_temperature_sweeps", range(29, 48)},
        {"speed_sweeps", range(49, 67)},
    };
    std::cout << "{\"schema_version\":\"thermox.orc_semi_physical_pump_study/v1\","
        << "\"execution_path\":\"canonical_platform_component_evaluator\","
        << "\"capacity_model\":\"displacement_minus_incompressible_orifice_leakage\","
        << "\"calibration_scope\":\"cases_1_68\","
        << "\"consumed_primary_holdout_cases\":\"69_77_diagnostic_only\","
        << "\"assumptions\":{\"leakage_discharge_coefficient\":0.8,"
        << "\"leakage_identifiability\":\"only_cd_times_area_is_identified\","
        << "\"efficiency_interpretation\":\"aggregate_from_measured_inlet_and_outlet_PT_not_an_OEM_map\"},"
        << "\"blocked_internal_cross_validation\":[";
    for (std::size_t i = 0; i < folds.size(); ++i) {
        if (i != 0U) std::cout << ',';
        const auto fitted = fit(points, complement(folds.at(i).validation));
        Metrics mass;
        Metrics temperature;
        evaluate(points, folds.at(i).validation, fitted, fluid, mass, temperature);
        std::cout << "{\"id\":\"" << folds.at(i).id << "\",\"fit\":";
        print_fit(std::cout, fitted);
        std::cout << ",\"mass_flow\":";
        print_metrics(std::cout, mass);
        std::cout << ",\"outlet_temperature\":";
        print_metrics(std::cout, temperature);
        std::cout << '}';
    }
    const auto training = range(0, calibration_case_count - 1U);
    const auto fitted = fit(points, training);
    const auto diagnostic = range(calibration_case_count, points.size() - 1U);
    Metrics mass;
    Metrics temperature;
    evaluate(points, diagnostic, fitted, fluid, mass, temperature);
    std::cout << "],\"full_training_fit\":";
    print_fit(std::cout, fitted);
    std::cout << ",\"consumed_holdout_diagnostic\":{\"independent_validation\":false,"
        << "\"mass_flow\":";
    print_metrics(std::cout, mass);
    std::cout << ",\"outlet_temperature\":";
    print_metrics(std::cout, temperature);
    std::cout << "}}\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::invalid_argument(
                "usage: thermox_orc_1kw_semi_physical_pump_study "
                "<measurements.csv>");
        const auto rows = read_csv(argv[1]);
        const thermox::physics::CoolPropHeosPropertyPackage fluid{"R245fa"};
        run(make_points(rows, fluid), fluid);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "ORC semi-physical pump study failed: "
                  << ex.what() << '\n';
        return 1;
    }
}
