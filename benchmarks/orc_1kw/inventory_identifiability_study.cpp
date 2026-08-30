#include "thermox/least_squares_solver.hpp"
#include "thermox/physics/coolprop_heos_package.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Row = std::map<std::string, double, std::less<>>;
constexpr std::size_t training_case_count = 68U;
constexpr double cubic_metres_per_litre = 1.0e-3;

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
        for (std::size_t index = 0; index < headers.size(); ++index)
            row.emplace(headers.at(index), std::stod(fields.at(index)));
        rows.push_back(std::move(row));
    }
    if (rows.size() != 77U)
        throw std::runtime_error("expected exactly 77 source cases");
    return rows;
}

double value(const Row& row, std::string_view key) {
    const auto found = row.find(key);
    if (found == row.end())
        throw std::runtime_error("missing measurement column: " + std::string{key});
    return found->second;
}

double density(
    const thermox::physics::PropertyPackage& fluid,
    const Row& row,
    std::string_view pressure_column,
    std::string_view temperature_column) {
    const auto state = fluid.state_pt(
        value(row, pressure_column) * 1000.0,
        value(row, temperature_column) + 273.15);
    if (!state.ok()) throw std::runtime_error(state.message);
    return state.state.density_kg_m3;
}

struct InventoryRow {
    double evaporator_density{};
    double condenser_density{};
    double receiver_density{};
    double charge{};
};

struct ErrorMetrics {
    double squared{};
    double maximum_absolute{};
    double bias{};
};

ErrorMetrics errors(
    const thermox::Matrix& design,
    const std::vector<double>& measured,
    const std::vector<double>& parameters) {
    ErrorMetrics result;
    for (std::size_t row = 0; row < design.size(); ++row) {
        double predicted = 0.0;
        for (std::size_t column = 0; column < parameters.size(); ++column)
            predicted += design.at(row).at(column) * parameters.at(column);
        const double error = predicted - measured.at(row);
        result.squared += error * error;
        result.maximum_absolute = std::max(
            result.maximum_absolute, std::abs(error));
        result.bias += error;
    }
    return result;
}

std::vector<double> nonnegative_fit(
    const thermox::Matrix& design,
    const std::vector<double>& measured) {
    std::vector<double> best(3U, 0.0);
    double best_squared = errors(design, measured, best).squared;
    for (unsigned mask = 1U; mask < 8U; ++mask) {
        std::vector<std::size_t> active;
        for (std::size_t column = 0; column < 3U; ++column) {
            if ((mask & (1U << column)) != 0U) active.push_back(column);
        }
        thermox::Matrix reduced;
        reduced.reserve(design.size());
        for (const auto& row : design) {
            std::vector<double> values;
            values.reserve(active.size());
            for (const auto column : active) values.push_back(row.at(column));
            reduced.push_back(std::move(values));
        }
        const auto candidate =
            thermox::solve_dense_least_squares(reduced, measured);
        if (!candidate.success || candidate.rank != active.size() ||
            std::any_of(candidate.x.begin(), candidate.x.end(),
                        [](double number) { return number < 0.0; })) {
            continue;
        }
        std::vector<double> expanded(3U, 0.0);
        for (std::size_t index = 0; index < active.size(); ++index)
            expanded.at(active.at(index)) = candidate.x.at(index);
        const double squared = errors(design, measured, expanded).squared;
        if (squared < best_squared) {
            best_squared = squared;
            best = std::move(expanded);
        }
    }
    return best;
}

std::vector<InventoryRow> reconstruct(
    const std::vector<Row>& rows,
    const thermox::physics::PropertyPackage& fluid) {
    std::vector<InventoryRow> result;
    result.reserve(training_case_count);
    for (std::size_t index = 0; index < training_case_count; ++index) {
        const auto& row = rows.at(index);
        const double evaporator_in = density(
            fluid, row, "pump_outlet_pressure_kpa",
            "pump_outlet_temperature_c");
        const double evaporator_out = density(
            fluid, row, "evaporator_outlet_pressure_kpa",
            "evaporator_outlet_temperature_c");
        const double condenser_in = density(
            fluid, row, "condenser_inlet_pressure_kpa",
            "condenser_inlet_temperature_c");
        const double condenser_out = density(
            fluid, row, "condenser_outlet_pressure_kpa",
            "condenser_outlet_temperature_c");
        result.push_back({
            0.5 * (evaporator_in + evaporator_out),
            0.5 * (condenser_in + condenser_out),
            density(fluid, row, "pump_inlet_pressure_kpa",
                    "pump_inlet_temperature_c"),
            value(row, "charge_kg")});
    }
    return result;
}

void run(const std::string& path) {
    const thermox::physics::CoolPropHeosPropertyPackage fluid{"R245fa"};
    const auto points = reconstruct(read_csv(path), fluid);
    thermox::Matrix design;
    std::vector<double> charge;
    design.reserve(points.size());
    charge.reserve(points.size());
    for (const auto& point : points) {
        design.push_back({
            cubic_metres_per_litre * point.evaporator_density,
            cubic_metres_per_litre * point.condenser_density,
            cubic_metres_per_litre * point.receiver_density});
        charge.push_back(point.charge);
    }
    const auto fitted = thermox::solve_dense_least_squares(design, charge);
    if (!fitted.success)
        throw std::runtime_error("inventory fit failed: " + fitted.message);

    const auto unconstrained_errors = errors(design, charge, fitted.x);
    const auto nonnegative = nonnegative_fit(design, charge);
    const auto nonnegative_errors = errors(design, charge, nonnegative);
    const bool physically_admissible = std::all_of(
        fitted.x.begin(), fitted.x.end(), [](double volume_l) {
            return std::isfinite(volume_l) && volume_l >= 0.0;
        });
    const auto& quality = fitted.factorization_quality;
    std::cout
        << "{\"schema_version\":\"thermox.orc_inventory_identifiability/v1\","
        << "\"classification\":\"training_only_structural_screen_not_calibration\","
        << "\"training_cases\":68,"
        << "\"proxy_model\":{"
        << "\"equation\":\"charge=sum(lumped_volume*mean_measured_boundary_density)\","
        << "\"evaporator_density\":\"arithmetic mean of measured inlet/outlet density\","
        << "\"condenser_density\":\"arithmetic mean of measured inlet/outlet density\","
        << "\"receiver_density\":\"measured pump-inlet density\"},"
        << "\"fit\":{\"rank\":" << fitted.rank
        << ",\"parameter_count\":3,"
        << "\"evaporator_volume_l\":" << fitted.x.at(0)
        << ",\"condenser_volume_l\":" << fitted.x.at(1)
        << ",\"receiver_volume_l\":" << fitted.x.at(2)
        << ",\"all_volumes_nonnegative\":"
        << (physically_admissible ? "true" : "false")
        << ",\"charge_rmse_kg\":"
        << std::sqrt(unconstrained_errors.squared / points.size())
        << ",\"charge_maximum_absolute_error_kg\":"
        << unconstrained_errors.maximum_absolute
        << ",\"charge_mean_bias_kg\":"
        << unconstrained_errors.bias / points.size()
        << ",\"qr_reciprocal_pivot_ratio\":"
        << quality.reciprocal_pivot_ratio
        << ",\"qr_quality_method\":\"" << quality.method << "\"},"
        << "\"best_nonnegative_fit\":{"
        << "\"evaporator_volume_l\":" << nonnegative.at(0)
        << ",\"condenser_volume_l\":" << nonnegative.at(1)
        << ",\"receiver_volume_l\":" << nonnegative.at(2)
        << ",\"charge_rmse_kg\":"
        << std::sqrt(nonnegative_errors.squared / points.size())
        << ",\"charge_maximum_absolute_error_kg\":"
        << nonnegative_errors.maximum_absolute
        << ",\"charge_mean_bias_kg\":"
        << nonnegative_errors.bias / points.size() << "},"
        << "\"interpretation\":\"A full numerical rank does not establish physical identifiability; negative volume or large charge residual rejects this boundary-density proxy and requires internal holdup or void-fraction information.\"}\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::invalid_argument(
                "usage: thermox_orc_1kw_inventory_identifiability_study <measurements.csv>");
        run(argv[1]);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "ORC inventory identifiability study failed: "
                  << ex.what() << '\n';
        return 1;
    }
}
