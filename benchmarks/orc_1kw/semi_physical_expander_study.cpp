#include "thermox/bounded_least_squares_optimizer.hpp"
#include "thermox/platform/semi_physical_volumetric_expander.hpp"
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
        for (std::size_t index = 0; index < headers.size(); ++index) {
            std::size_t parsed = 0;
            const double item = std::stod(fields.at(index), &parsed);
            if (parsed != fields.at(index).size() || !std::isfinite(item))
                throw std::runtime_error("measurement CSV contains a non-finite value");
            row.emplace(headers.at(index), item);
        }
        rows.push_back(std::move(row));
    }
    if (rows.size() != 77U)
        throw std::runtime_error("expected exactly 77 complete source cases");
    return rows;
}

double value(const Row& row, std::string_view key) {
    const auto position = row.find(key);
    if (position == row.end())
        throw std::runtime_error("measurement column is missing: " + std::string{key});
    return position->second;
}

std::string case_id(std::size_t index) {
    return "case_" + std::to_string(index + 1U);
}

struct Metrics {
    std::size_t count{};
    double sum_absolute{};
    double maximum_absolute{};
    double sum_absolute_relative{};
    double maximum_absolute_relative{};
    double sum_error{};
};

void accumulate(Metrics& metrics, double predicted, double measured) {
    const double relative = (predicted - measured) / measured;
    const double absolute = std::abs(predicted - measured);
    ++metrics.count;
    metrics.sum_absolute += absolute;
    metrics.maximum_absolute =
        std::max(metrics.maximum_absolute, absolute);
    metrics.sum_absolute_relative += std::abs(relative);
    metrics.maximum_absolute_relative =
        std::max(metrics.maximum_absolute_relative, std::abs(relative));
    metrics.sum_error += relative;
}

void print_metrics(std::ostream& out, const Metrics& metrics) {
    out << "{\"count\":" << metrics.count
        << ",\"mean_absolute_error_si\":"
        << metrics.sum_absolute / metrics.count
        << ",\"maximum_absolute_error_si\":"
        << metrics.maximum_absolute
        << ",\"mape_percent\":"
        << 100.0 * metrics.sum_absolute_relative / metrics.count
        << ",\"maximum_absolute_relative_error_percent\":"
        << 100.0 * metrics.maximum_absolute_relative
        << ",\"mean_bias_percent\":"
        << 100.0 * metrics.sum_error / metrics.count << '}';
}

struct Fold {
    std::string id;
    std::set<std::size_t> validation;
};

std::set<std::size_t> complement(const std::set<std::size_t>& held_out);
Fold contiguous_fold(
    std::string id, std::size_t first, std::size_t last);

struct OperatingPoint {
    double inlet_pressure{};
    double inlet_enthalpy{};
    double outlet_pressure{};
    double angular_speed{};
    double ambient_temperature{};
    double measured_mass_flow{};
    double measured_outlet_temperature{};
    double measured_shaft_power{};
};

std::vector<OperatingPoint> make_operating_points(
    const std::vector<Row>& rows,
    const thermox::physics::PropertyPackage& fluid) {
    std::vector<OperatingPoint> points;
    points.reserve(rows.size());
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto& row = rows.at(index);
        const double inlet_pressure =
            value(row, "expander_inlet_pressure_kpa") * 1000.0;
        const auto inlet = fluid.state_pt(
            inlet_pressure,
            value(row, "expander_inlet_temperature_c") + 273.15);
        if (!inlet.ok())
            throw std::runtime_error(
                "could not reconstruct inlet state for " + case_id(index) +
                ": " + inlet.message);
        const double angular_speed = value(row, "expander_speed_rpm") *
            2.0 * std::numbers::pi / 60.0;
        points.push_back({
            inlet_pressure,
            inlet.state.enthalpy_j_kg,
            value(row, "expander_outlet_pressure_kpa") * 1000.0,
            angular_speed,
            value(row, "ambient_temperature_c") + 273.15,
            value(row, "working_fluid_mass_flow_g_s") / 1000.0,
            value(row, "expander_outlet_temperature_c") + 273.15,
            value(row, "expander_torque_n_m") * angular_speed,
        });
    }
    return points;
}

thermox::platform::SemiPhysicalVolumetricExpanderParameters parameters_from(
    const std::vector<double>& candidate, double ambient_temperature) {
    return {
        candidate.at(0), candidate.at(1), candidate.at(2), 0.8,
        candidate.at(3), 300.0, 0.0, candidate.at(4),
        ambient_temperature,
    };
}

struct PointPrediction {
    double mass_flow{};
    double outlet_temperature{};
    double shaft_power{};
};

bool predict(
    const thermox::physics::PropertyPackage& fluid,
    const OperatingPoint& point,
    const std::vector<double>& candidate,
    PointPrediction& prediction,
    std::string& message) {
    thermox::platform::SemiPhysicalVolumetricExpanderEvaluation evaluation;
    const auto status =
        thermox::platform::evaluate_semi_physical_volumetric_expander(
            fluid, point.inlet_pressure, point.inlet_enthalpy,
            point.outlet_pressure, point.angular_speed,
            parameters_from(candidate, point.ambient_temperature),
            evaluation);
    if (!status.ok()) {
        message = status.message;
        return false;
    }
    const auto outlet = fluid.state_ph(
        point.outlet_pressure, evaluation.outlet_enthalpy);
    if (!outlet.ok()) {
        message = outlet.message;
        return false;
    }
    prediction = {
        evaluation.mass_flow,
        outlet.state.temperature_k,
        evaluation.shaft_power,
    };
    return true;
}

thermox::BoundedLeastSquaresResult fit_direct(
    const std::vector<OperatingPoint>& points,
    const std::set<std::size_t>& training,
    const thermox::physics::PropertyPackage& fluid) {
    const auto residual = [&](const std::vector<double>& candidate,
                              const std::vector<double>*) {
        std::vector<double> residuals;
        residuals.reserve(3U * training.size());
        for (const auto index : training) {
            PointPrediction prediction;
            std::string message;
            if (!predict(fluid, points.at(index), candidate,
                         prediction, message)) {
                return thermox::BoundedResidualEvaluation{
                    false, {}, case_id(index) + ": " + message};
            }
            const auto& point = points.at(index);
            residuals.push_back(
                (prediction.mass_flow - point.measured_mass_flow) / 0.001);
            residuals.push_back(
                prediction.outlet_temperature -
                point.measured_outlet_temperature);
            residuals.push_back(
                (prediction.shaft_power - point.measured_shaft_power) / 20.0);
        }
        return thermox::BoundedResidualEvaluation{
            true, std::move(residuals), "ok"};
    };
    thermox::BoundedLeastSquaresSettings settings;
    settings.max_iterations = 24;
    settings.finite_difference_fraction = 2.0e-4;
    settings.gradient_tolerance = 1.0e-5;
    settings.step_tolerance = 1.0e-5;
    settings.objective_relative_tolerance = 1.0e-7;
    return thermox::solve_bounded_nonlinear_least_squares(
        residual,
        {5.0e-5, 3.0, 5.0e-7, 50.0, 2.0},
        {2.0e-5, 1.2, 0.0, 0.0, 0.0},
        {1.5e-4, 6.0, 1.0e-5, 1000.0, 50.0},
        settings);
}

const std::vector<std::string>& parameter_names() {
    static const std::vector<std::string> names{
        "maximum_chamber_volume", "built_in_volume_ratio",
        "leakage_area", "reference_mechanical_loss",
        "ambient_conductance",
    };
    return names;
}

void print_direct_fit(
    std::ostream& out, const thermox::BoundedLeastSquaresResult& fit) {
    out << "\"calibration\":{\"converged\":"
        << (fit.diagnostics.converged ? "true" : "false")
        << ",\"iterations\":" << fit.diagnostics.iterations
        << ",\"residual_evaluations\":"
        << fit.diagnostics.residual_evaluations
        << ",\"initial_objective\":" << fit.initial_objective
        << ",\"final_objective\":" << fit.final_objective
        << ",\"sensitivity_rank\":"
        << fit.diagnostics.sensitivity_rank
        << ",\"parameter_count\":" << fit.x.size()
        << ",\"parameters\":{";
    for (std::size_t index = 0; index < fit.x.size(); ++index) {
        if (index != 0U) out << ',';
        out << '\"' << parameter_names().at(index) << "\":"
            << fit.x.at(index);
    }
    out << "}}";
}

void run_direct_full_fit(
    const std::vector<OperatingPoint>& points,
    const thermox::physics::PropertyPackage& fluid) {
    std::set<std::size_t> training;
    for (std::size_t index = 0; index < calibration_case_count; ++index)
        training.insert(index);
    const auto fit = fit_direct(points, training, fluid);
    if (!fit.success)
        throw std::runtime_error("direct full calibration failed: " + fit.message);
    std::cout << "{\"schema_version\":\"thermox.orc_semi_physical_study/v2\","
        << "\"mode\":\"full_training_fit\",\"case_scope\":\"1-68\","
        << "\"model_configuration\":\"reduced_five_parameter_zero_proportional_loss\","
        << "\"execution_path\":\"canonical_platform_component_evaluator\","
        << "\"objective_scale_semantics\":\"engineering_model_discrimination_not_measurement_uncertainty\",";
    print_direct_fit(std::cout, fit);
    std::cout << "}\n";
}

void run_direct_cross_validation(
    const std::vector<OperatingPoint>& points,
    const thermox::physics::PropertyPackage& fluid) {
    const std::vector<Fold> folds{
        contiguous_fold("charge_sweeps", 0, 28),
        contiguous_fold("sink_temperature_sweeps", 29, 48),
        contiguous_fold("speed_sweeps", 49, 67),
    };
    std::cout << "{\"schema_version\":\"thermox.orc_semi_physical_study/v2\","
        << "\"mode\":\"blocked_internal_cross_validation\","
        << "\"model_configuration\":\"reduced_five_parameter_zero_proportional_loss\","
        << "\"execution_path\":\"canonical_platform_component_evaluator\","
        << "\"case_scope\":\"1-68\",\"consumed_primary_holdout_cases\":\"69-77_excluded\","
        << "\"objective_scale_semantics\":\"engineering_model_discrimination_not_measurement_uncertainty\",\"folds\":[";
    for (std::size_t fold_index = 0; fold_index < folds.size(); ++fold_index) {
        if (fold_index != 0U) std::cout << ',';
        const auto& fold = folds.at(fold_index);
        const auto fit = fit_direct(points, complement(fold.validation), fluid);
        if (!fit.success)
            throw std::runtime_error("fold '" + fold.id + "' failed: " + fit.message);
        Metrics mass;
        Metrics temperature;
        Metrics power;
        for (const auto index : fold.validation) {
            PointPrediction prediction;
            std::string message;
            if (!predict(fluid, points.at(index), fit.x,
                         prediction, message))
                throw std::runtime_error(
                    "fold '" + fold.id + "' prediction failed: " + message);
            const auto& point = points.at(index);
            accumulate(mass, prediction.mass_flow, point.measured_mass_flow);
            accumulate(temperature, prediction.outlet_temperature,
                       point.measured_outlet_temperature);
            accumulate(power, prediction.shaft_power,
                       point.measured_shaft_power);
        }
        std::cout << "{\"id\":\"" << fold.id
            << "\",\"training_case_count\":"
            << calibration_case_count - fold.validation.size()
            << ",\"validation_case_count\":" << fold.validation.size()
            << ',';
        print_direct_fit(std::cout, fit);
        std::cout << ",\"validation_metrics\":{\"mass_flow\":";
        print_metrics(std::cout, mass);
        std::cout << ",\"outlet_temperature\":";
        print_metrics(std::cout, temperature);
        std::cout << ",\"shaft_power\":";
        print_metrics(std::cout, power);
        std::cout << "}}";
    }
    std::cout << "]}\n";
}

void run_consumed_holdout_diagnostic(
    const std::vector<OperatingPoint>& points,
    const thermox::physics::PropertyPackage& fluid) {
    std::set<std::size_t> training;
    for (std::size_t index = 0; index < calibration_case_count; ++index)
        training.insert(index);
    const auto fit = fit_direct(points, training, fluid);
    if (!fit.success)
        throw std::runtime_error(
            "consumed-holdout diagnostic fit failed: " + fit.message);
    Metrics mass;
    Metrics temperature;
    Metrics power;
    for (std::size_t index = calibration_case_count;
         index < points.size(); ++index) {
        PointPrediction prediction;
        std::string message;
        if (!predict(fluid, points.at(index), fit.x,
                     prediction, message)) {
            throw std::runtime_error(
                "consumed-holdout prediction failed for " +
                case_id(index) + ": " + message);
        }
        const auto& point = points.at(index);
        accumulate(mass, prediction.mass_flow, point.measured_mass_flow);
        accumulate(temperature, prediction.outlet_temperature,
                   point.measured_outlet_temperature);
        accumulate(power, prediction.shaft_power,
                   point.measured_shaft_power);
    }
    std::cout << "{\"schema_version\":\"thermox.orc_semi_physical_study/v2\","
        << "\"mode\":\"consumed_holdout_diagnostic\","
        << "\"model_configuration\":\"reduced_five_parameter_zero_proportional_loss\","
        << "\"calibration_cases\":\"1-68\",\"diagnostic_cases\":\"69-77\","
        << "\"independent_validation\":false,"
        << "\"claim_limit\":\"diagnostic_only_holdout_previously_consumed\","
        << "\"execution_path\":\"canonical_platform_component_evaluator\",";
    print_direct_fit(std::cout, fit);
    std::cout << ",\"diagnostic_metrics\":{\"mass_flow\":";
    print_metrics(std::cout, mass);
    std::cout << ",\"outlet_temperature\":";
    print_metrics(std::cout, temperature);
    std::cout << ",\"shaft_power\":";
    print_metrics(std::cout, power);
    std::cout << "}}\n";
}

std::set<std::size_t> complement(const std::set<std::size_t>& held_out) {
    std::set<std::size_t> result;
    for (std::size_t index = 0; index < calibration_case_count; ++index)
        if (!held_out.contains(index)) result.insert(index);
    return result;
}

Fold contiguous_fold(std::string id, std::size_t first, std::size_t last) {
    Fold fold{std::move(id), {}};
    for (std::size_t index = first; index <= last; ++index)
        fold.validation.insert(index);
    return fold;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::invalid_argument(
                "usage: thermox_orc_1kw_semi_physical_expander_study "
                "<measurements.csv> "
                "<full-fit|cross-validation|consumed-holdout-diagnostic>");
        const auto rows = read_csv(argv[1]);
        const thermox::physics::CoolPropHeosPropertyPackage fluid{"R245fa"};
        const auto points = make_operating_points(rows, fluid);
        const std::string_view mode{argv[2]};
        if (mode == "full-fit") run_direct_full_fit(points, fluid);
        else if (mode == "cross-validation")
            run_direct_cross_validation(points, fluid);
        else if (mode == "consumed-holdout-diagnostic")
            run_consumed_holdout_diagnostic(points, fluid);
        else throw std::invalid_argument("unknown study mode: " + std::string{mode});
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "ORC semi-physical expander study failed: " << ex.what() << '\n';
        return 1;
    }
}
