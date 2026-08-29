#include "thermox/nonlinear_solver.hpp"
#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/correlation.hpp"
#include "thermox/platform/model_document.hpp"
#include "thermox/physics/coolprop_heos_package.hpp"
#include "thermox/physics/property_registry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Row = std::map<std::string, double, std::less<>>;
using Features = std::array<double, 10>;
using Coefficients = std::array<double, 10>;

constexpr std::size_t calibration_case_count = 68;
constexpr double displacement_m3_per_revolution = 14.5e-6;

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
    return rows;
}

double value(const Row& row, std::string_view key) {
    const auto position = row.find(key);
    if (position == row.end())
        throw std::runtime_error("measurement column is missing: " + std::string{key});
    return position->second;
}

thermox::physics::ThermodynamicState require_state(
    const thermox::physics::PropertyResult& result) {
    if (!result.ok())
        throw std::runtime_error("property evaluation failed: " + result.message);
    return result.state;
}

struct Observation {
    int case_id{};
    double inlet_temperature_k{};
    double inlet_pressure_pa{};
    double outlet_pressure_pa{};
    double angular_speed_rad_s{};
    double measured_mass_flow_kg_s{};
    double measured_outlet_temperature_k{};
    double measured_shaft_power_w{};
    double effective_capacity_cm3_per_revolution{};
    double fluid_isentropic_efficiency{};
    double shaft_isentropic_efficiency{};
    Features features{};
};

Features make_features(
    double pressure_ratio, double inlet_pressure_mpa,
    double speed_krpm) {
    return {1.0, pressure_ratio, inlet_pressure_mpa, speed_krpm,
        pressure_ratio * pressure_ratio,
        inlet_pressure_mpa * inlet_pressure_mpa,
        speed_krpm * speed_krpm,
        pressure_ratio * inlet_pressure_mpa,
        pressure_ratio * speed_krpm,
        inlet_pressure_mpa * speed_krpm};
}

std::vector<Observation> make_observations(const std::vector<Row>& rows) {
    const thermox::physics::CoolPropHeosPropertyPackage fluid{"R245fa"};
    std::vector<Observation> observations;
    observations.reserve(rows.size());
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto& row = rows.at(index);
        Observation observation;
        observation.case_id = static_cast<int>(value(row, "case"));
        if (observation.case_id != static_cast<int>(index + 1U))
            throw std::runtime_error("case identifiers must be consecutive from 1 to 77");
        observation.inlet_temperature_k =
            value(row, "expander_inlet_temperature_c") + 273.15;
        observation.inlet_pressure_pa =
            value(row, "expander_inlet_pressure_kpa") * 1000.0;
        observation.outlet_pressure_pa =
            value(row, "expander_outlet_pressure_kpa") * 1000.0;
        const double speed_rpm = value(row, "expander_speed_rpm");
        observation.angular_speed_rad_s =
            speed_rpm * 2.0 * std::numbers::pi / 60.0;
        observation.measured_mass_flow_kg_s =
            value(row, "working_fluid_mass_flow_g_s") / 1000.0;
        observation.measured_outlet_temperature_k =
            value(row, "expander_outlet_temperature_c") + 273.15;
        observation.measured_shaft_power_w =
            value(row, "expander_torque_n_m") *
            observation.angular_speed_rad_s;

        const auto inlet = require_state(fluid.state_pt(
            observation.inlet_pressure_pa,
            observation.inlet_temperature_k));
        const auto outlet = require_state(fluid.state_pt(
            observation.outlet_pressure_pa,
            observation.measured_outlet_temperature_k));
        const auto isentropic = require_state(fluid.state_ps(
            observation.outlet_pressure_pa,
            inlet.entropy_j_kg_k));
        const double ideal_drop =
            inlet.enthalpy_j_kg - isentropic.enthalpy_j_kg;
        if (!(ideal_drop > 0.0))
            throw std::runtime_error("nonpositive isentropic drop in source case");
        observation.effective_capacity_cm3_per_revolution =
            observation.measured_mass_flow_kg_s /
            (inlet.density_kg_m3 * speed_rpm / 60.0) * 1.0e6;
        observation.fluid_isentropic_efficiency =
            (inlet.enthalpy_j_kg - outlet.enthalpy_j_kg) / ideal_drop;
        observation.shaft_isentropic_efficiency =
            observation.measured_shaft_power_w /
            (observation.measured_mass_flow_kg_s * ideal_drop);
        observation.features = make_features(
            observation.inlet_pressure_pa / observation.outlet_pressure_pa,
            observation.inlet_pressure_pa / 1.0e6,
            speed_rpm / 1000.0);
        observations.push_back(observation);
    }
    return observations;
}

Coefficients fit_least_squares(
    const std::vector<Observation>& observations,
    const std::function<double(const Observation&)>& target) {
    std::array<std::array<double, 11>, 10> augmented{};
    for (std::size_t row = 0; row < calibration_case_count; ++row) {
        const auto& observation = observations.at(row);
        const double response = target(observation);
        for (std::size_t i = 0; i < 10; ++i) {
            augmented.at(i).at(10) += observation.features.at(i) * response;
            for (std::size_t j = 0; j < 10; ++j)
                augmented.at(i).at(j) +=
                    observation.features.at(i) * observation.features.at(j);
        }
    }
    for (std::size_t pivot = 0; pivot < 10; ++pivot) {
        std::size_t best = pivot;
        for (std::size_t row = pivot + 1; row < 10; ++row) {
            if (std::abs(augmented.at(row).at(pivot)) >
                std::abs(augmented.at(best).at(pivot))) best = row;
        }
        if (std::abs(augmented.at(best).at(pivot)) < 1.0e-14)
            throw std::runtime_error("singular least-squares normal matrix");
        std::swap(augmented.at(pivot), augmented.at(best));
        const double diagonal = augmented.at(pivot).at(pivot);
        for (std::size_t column = pivot; column <= 10; ++column)
            augmented.at(pivot).at(column) /= diagonal;
        for (std::size_t row = 0; row < 10; ++row) {
            if (row == pivot) continue;
            const double scale = augmented.at(row).at(pivot);
            for (std::size_t column = pivot; column <= 10; ++column)
                augmented.at(row).at(column) -=
                    scale * augmented.at(pivot).at(column);
        }
    }
    Coefficients result{};
    for (std::size_t index = 0; index < 10; ++index)
        result.at(index) = augmented.at(index).at(10);
    return result;
}

std::string polynomial_expression(double divisor = 1.0) {
    std::ostringstream expression;
    expression << std::setprecision(17)
        << "(c0 + c1 * pressure_ratio + c2 * (inlet_pressure / 1000000)"
        << " + c3 * (angular_speed * 60 / (2 * 3.14159265358979323846) / 1000)"
        << " + c4 * pressure_ratio * pressure_ratio"
        << " + c5 * (inlet_pressure / 1000000) * (inlet_pressure / 1000000)"
        << " + c6 * (angular_speed * 60 / (2 * 3.14159265358979323846) / 1000) * (angular_speed * 60 / (2 * 3.14159265358979323846) / 1000)"
        << " + c7 * pressure_ratio * (inlet_pressure / 1000000)"
        << " + c8 * pressure_ratio * (angular_speed * 60 / (2 * 3.14159265358979323846) / 1000)"
        << " + c9 * (inlet_pressure / 1000000) * (angular_speed * 60 / (2 * 3.14159265358979323846) / 1000)) / "
        << divisor;
    return expression.str();
}

thermox::platform::CorrelationArtifact make_correlation(
    std::string id, std::string output, const Coefficients& coefficients,
    double divisor, char checksum) {
    std::map<std::string, double> named;
    for (std::size_t index = 0; index < coefficients.size(); ++index)
        named.emplace("c" + std::to_string(index), coefficients.at(index));
    return thermox::platform::CorrelationArtifact{
        std::move(id), thermox::platform::correlation_artifact_schema_v2,
        "frozen-quadratic-v1", std::string(64, checksum),
        {{"pressure_ratio", "dimensionless"},
         {"inlet_pressure", "pressure"},
         {"angular_speed", "angular_speed"}},
        {std::move(output), "dimensionless"},
        {{"calibration_cases_1_68", "general", 0, std::move(named),
          polynomial_expression(divisor), {}, {}, false}}};
}

struct Prediction {
    int case_id{};
    double mass_flow_kg_s{};
    double outlet_temperature_k{};
    double shaft_power_w{};
    double fluid_efficiency{};
    double shaft_efficiency{};
    double maximum_absolute_normalized_residual{};
};

std::size_t variable_index(
    const std::vector<std::string>& names, const std::string& name) {
    const auto position = std::find(names.begin(), names.end(), name);
    if (position == names.end())
        throw std::runtime_error("compiled variable is missing: " + name);
    return static_cast<std::size_t>(position - names.begin());
}

Prediction solve_case(
    const Observation& observation,
    const thermox::platform::EngineeringArtifactRegistry& artifacts,
    const thermox::platform::ComponentRegistry& components,
    const thermox::physics::PropertyPackageRegistry& properties,
    const thermox::physics::PropertyPackage& fluid) {
    std::ostringstream json;
    json << std::setprecision(17) << R"json({
  "schema_version": "thermox.model/v2",
  "model": {"id": "orc_expander_holdout", "media": [{"id": "wf", "backend": "coolprop_heos", "substance": "R245fa"}],
    "components": [{"id": "expander", "kind": "expander.fluid.volumetric_correlations",
      "artifacts": {"filling_factor_correlation": "orc-filling", "fluid_efficiency_correlation": "orc-fluid-efficiency", "shaft_efficiency_correlation": "orc-shaft-efficiency"},
      "parameters": {"displacement_per_revolution": )json"
         << displacement_m3_per_revolution << R"json(, "rejected_heat_temperature": 300.0},
      "media": {"inlet": "wf", "outlet": "wf"}}], "connections": []},
  "cases": [{"id": "point", "mode": "steady_state_off_design", "fixed_values": {
    "expander.inlet.p": )json" << observation.inlet_pressure_pa
         << R"json(, "expander.inlet.T": )json" << observation.inlet_temperature_k
         << R"json(, "expander.outlet.p": )json" << observation.outlet_pressure_pa
         << R"json(, "expander.shaft.omega": )json" << observation.angular_speed_rad_s
         << R"json(}, "initial_guesses": {"expander.inlet.m_dot": 0.03, "expander.outlet.m_dot": 0.03, "expander.outlet.h": 450000.0, "expander.shaft.W_dot": 500.0, "expander.rejected_heat.Q_dot": 200.0, "expander.rejected_heat.T": 300.0}}]})json";
    const auto document = thermox::platform::parse_model_document_text(json.str());
    const auto graph = thermox::platform::compile_model_graph(
        document, components, properties, artifacts, "point");
    const auto result = thermox::solve_newton(graph.problem);
    if (!result.diagnostics.converged)
        throw std::runtime_error(
            "case " + std::to_string(observation.case_id) +
            " did not converge: " + result.diagnostics.message);
    const auto get = [&](std::string name) {
        return result.x.at(variable_index(graph.problem.variable_names, name));
    };
    const double inlet_h = get("expander.inlet.h");
    const auto inlet = require_state(fluid.state_ph(
        observation.inlet_pressure_pa, inlet_h));
    const auto isentropic = require_state(fluid.state_ps(
        observation.outlet_pressure_pa, inlet.entropy_j_kg_k));
    const double outlet_h = get("expander.outlet.h");
    const double ideal_drop = inlet_h - isentropic.enthalpy_j_kg;
    Prediction prediction;
    prediction.case_id = observation.case_id;
    prediction.mass_flow_kg_s = get("expander.inlet.m_dot");
    prediction.outlet_temperature_k = require_state(fluid.state_ph(
        observation.outlet_pressure_pa, outlet_h)).temperature_k;
    prediction.shaft_power_w = get("expander.shaft.W_dot");
    prediction.fluid_efficiency = (inlet_h - outlet_h) / ideal_drop;
    prediction.shaft_efficiency = prediction.shaft_power_w /
        (prediction.mass_flow_kg_s * ideal_drop);
    prediction.maximum_absolute_normalized_residual =
        result.diagnostics.final_maximum_absolute_normalized_residual;
    return prediction;
}

struct ErrorStatistics {
    double mean_absolute_percent{};
    double maximum_absolute_percent{};
    double mean_signed_percent{};
};

ErrorStatistics relative_error_statistics(
    const std::vector<double>& predicted,
    const std::vector<double>& measured) {
    if (predicted.size() != measured.size() || predicted.empty())
        throw std::runtime_error("invalid metric series");
    ErrorStatistics statistics;
    for (std::size_t index = 0; index < predicted.size(); ++index) {
        const double error =
            100.0 * (predicted.at(index) - measured.at(index)) /
            measured.at(index);
        statistics.mean_signed_percent += error;
        statistics.mean_absolute_percent += std::abs(error);
        statistics.maximum_absolute_percent =
            std::max(statistics.maximum_absolute_percent, std::abs(error));
    }
    statistics.mean_signed_percent /= predicted.size();
    statistics.mean_absolute_percent /= predicted.size();
    return statistics;
}

struct AbsoluteStatistics {
    double mean_absolute{};
    double maximum_absolute{};
    double mean_signed{};
};

AbsoluteStatistics absolute_error_statistics(
    const std::vector<double>& predicted,
    const std::vector<double>& measured) {
    AbsoluteStatistics statistics;
    for (std::size_t index = 0; index < predicted.size(); ++index) {
        const double error = predicted.at(index) - measured.at(index);
        statistics.mean_signed += error;
        statistics.mean_absolute += std::abs(error);
        statistics.maximum_absolute =
            std::max(statistics.maximum_absolute, std::abs(error));
    }
    statistics.mean_signed /= predicted.size();
    statistics.mean_absolute /= predicted.size();
    return statistics;
}

void write_relative_statistics(
    std::ostream& output, std::string_view name,
    const ErrorStatistics& statistics, bool comma = true) {
    output << "      \"" << name << "\": {\"mean_absolute_percent\": "
           << statistics.mean_absolute_percent
           << ", \"maximum_absolute_percent\": "
           << statistics.maximum_absolute_percent
           << ", \"mean_signed_percent\": "
           << statistics.mean_signed_percent << "}"
           << (comma ? "," : "") << "\n";
}

void write_coefficients(
    std::ostream& output, std::string_view name,
    const Coefficients& coefficients, bool comma = true) {
    output << "    \"" << name << "\": [";
    for (std::size_t index = 0; index < coefficients.size(); ++index) {
        if (index) output << ", ";
        output << coefficients.at(index);
    }
    output << "]" << (comma ? "," : "") << "\n";
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc != 2) {
        std::cerr << "usage: thermox_orc_1kw_expander_holdout_validation <measurements.csv>\n";
        return 2;
    }
    const auto rows = read_csv(argv[1]);
    if (rows.size() != 77U)
        throw std::runtime_error("the audited source must contain exactly 77 cases");
    const auto observations = make_observations(rows);
    const auto capacity_coefficients = fit_least_squares(
        observations, [](const Observation& item) {
            return item.effective_capacity_cm3_per_revolution;
        });
    const auto fluid_efficiency_coefficients = fit_least_squares(
        observations, [](const Observation& item) {
            return item.fluid_isentropic_efficiency;
        });
    const auto shaft_efficiency_coefficients = fit_least_squares(
        observations, [](const Observation& item) {
            return item.shaft_isentropic_efficiency;
        });

    thermox::platform::EngineeringArtifactRegistry artifacts;
    artifacts.register_artifact(make_correlation(
        "orc-filling", "filling_factor", capacity_coefficients,
        displacement_m3_per_revolution * 1.0e6, '1'));
    artifacts.register_artifact(make_correlation(
        "orc-fluid-efficiency", "fluid_isentropic_efficiency",
        fluid_efficiency_coefficients, 1.0, '2'));
    artifacts.register_artifact(make_correlation(
        "orc-shaft-efficiency", "shaft_isentropic_efficiency",
        shaft_efficiency_coefficients, 1.0, '3'));
    const auto components = thermox::platform::make_default_component_registry();
    const auto properties =
        thermox::physics::make_default_property_package_registry();
    const auto fluid = properties.create("coolprop_heos", "R245fa");

    std::vector<Prediction> predictions;
    predictions.reserve(observations.size());
    for (const auto& observation : observations)
        predictions.push_back(solve_case(
            observation, artifacts, components, properties, *fluid));

    const auto summarize_relative = [&](
        std::size_t begin, std::size_t end,
        const std::function<double(const Prediction&)>& predicted,
        const std::function<double(const Observation&)>& measured) {
        std::vector<double> predicted_values;
        std::vector<double> measured_values;
        for (std::size_t index = begin; index < end; ++index) {
            predicted_values.push_back(predicted(predictions.at(index)));
            measured_values.push_back(measured(observations.at(index)));
        }
        return relative_error_statistics(predicted_values, measured_values);
    };
    const auto summarize_temperature = [&](std::size_t begin, std::size_t end) {
        std::vector<double> predicted_values;
        std::vector<double> measured_values;
        for (std::size_t index = begin; index < end; ++index) {
            predicted_values.push_back(
                predictions.at(index).outlet_temperature_k);
            measured_values.push_back(
                observations.at(index).measured_outlet_temperature_k);
        }
        return absolute_error_statistics(predicted_values, measured_values);
    };
    const auto metric = [&](std::size_t begin, std::size_t end, auto p, auto m) {
        return summarize_relative(begin, end, p, m);
    };
    const auto mass = [](const Prediction& item) { return item.mass_flow_kg_s; };
    const auto measured_mass = [](const Observation& item) {
        return item.measured_mass_flow_kg_s;
    };
    const auto fluid_eta = [](const Prediction& item) {
        return item.fluid_efficiency;
    };
    const auto measured_fluid_eta = [](const Observation& item) {
        return item.fluid_isentropic_efficiency;
    };
    const auto shaft_eta = [](const Prediction& item) {
        return item.shaft_efficiency;
    };
    const auto measured_shaft_eta = [](const Observation& item) {
        return item.shaft_isentropic_efficiency;
    };
    const auto shaft_power = [](const Prediction& item) {
        return item.shaft_power_w;
    };
    const auto measured_shaft_power = [](const Observation& item) {
        return item.measured_shaft_power_w;
    };
    const auto train_mass = metric(0, calibration_case_count, mass, measured_mass);
    const auto train_fluid_eta = metric(
        0, calibration_case_count, fluid_eta, measured_fluid_eta);
    const auto train_shaft_eta = metric(
        0, calibration_case_count, shaft_eta, measured_shaft_eta);
    const auto train_power = metric(
        0, calibration_case_count, shaft_power, measured_shaft_power);
    const auto holdout_mass = metric(
        calibration_case_count, observations.size(), mass, measured_mass);
    const auto holdout_fluid_eta = metric(
        calibration_case_count, observations.size(),
        fluid_eta, measured_fluid_eta);
    const auto holdout_shaft_eta = metric(
        calibration_case_count, observations.size(),
        shaft_eta, measured_shaft_eta);
    const auto holdout_power = metric(
        calibration_case_count, observations.size(),
        shaft_power, measured_shaft_power);
    const auto train_temperature = summarize_temperature(
        0, calibration_case_count);
    const auto holdout_temperature = summarize_temperature(
        calibration_case_count, observations.size());
    const bool passed =
        holdout_mass.mean_absolute_percent <= 8.0 &&
        holdout_mass.maximum_absolute_percent <= 15.0 &&
        holdout_power.mean_absolute_percent <= 8.0 &&
        holdout_power.maximum_absolute_percent <= 15.0;
    double maximum_solver_residual = 0.0;
    for (const auto& prediction : predictions) {
        maximum_solver_residual = std::max(
            maximum_solver_residual,
            prediction.maximum_absolute_normalized_residual);
    }

    std::cout << std::setprecision(10)
              << "{\n"
              << "  \"schema_version\": \"thermox.component_blocked_validation/v1\",\n"
              << "  \"benchmark_id\": \"orc_1kw_r245fa\",\n"
              << "  \"component_kind\": \"expander.fluid.volumetric_correlations\",\n"
              << "  \"classification\": \"calibrated_component_blocked_extrapolative_holdout\",\n"
              << "  \"split\": {\"calibration_cases\": \"1-68\", \"holdout_cases\": \"69-77\", \"holdout_inspected_once\": true},\n"
              << "  \"model\": {\"family\": \"full_quadratic_response_surface\", \"inputs\": [\"pressure_ratio\", \"inlet_pressure_MPa\", \"speed_krpm\"], \"feature_order\": [\"1\", \"pr\", \"pin\", \"speed\", \"pr2\", \"pin2\", \"speed2\", \"pr_pin\", \"pr_speed\", \"pin_speed\"], \"displacement_normalization_m3_per_revolution\": "
              << displacement_m3_per_revolution << "},\n"
              << "  \"coefficients\": {\n";
    write_coefficients(std::cout, "effective_capacity_cm3_per_revolution",
        capacity_coefficients);
    write_coefficients(std::cout, "fluid_isentropic_efficiency",
        fluid_efficiency_coefficients);
    write_coefficients(std::cout, "shaft_isentropic_efficiency",
        shaft_efficiency_coefficients, false);
    std::cout << "  },\n  \"metrics\": {\n    \"calibration\": {\n";
    write_relative_statistics(std::cout, "mass_flow", train_mass);
    write_relative_statistics(std::cout, "fluid_isentropic_efficiency", train_fluid_eta);
    write_relative_statistics(std::cout, "shaft_isentropic_efficiency", train_shaft_eta);
    write_relative_statistics(std::cout, "shaft_power", train_power);
    std::cout << "      \"outlet_temperature_error_k\": {\"mean_absolute\": "
              << train_temperature.mean_absolute
              << ", \"maximum_absolute\": " << train_temperature.maximum_absolute
              << ", \"mean_signed\": " << train_temperature.mean_signed << "}\n"
              << "    },\n    \"holdout\": {\n";
    write_relative_statistics(std::cout, "mass_flow", holdout_mass);
    write_relative_statistics(std::cout, "fluid_isentropic_efficiency", holdout_fluid_eta);
    write_relative_statistics(std::cout, "shaft_isentropic_efficiency", holdout_shaft_eta);
    write_relative_statistics(std::cout, "shaft_power", holdout_power);
    std::cout << "      \"outlet_temperature_error_k\": {\"mean_absolute\": "
              << holdout_temperature.mean_absolute
              << ", \"maximum_absolute\": " << holdout_temperature.maximum_absolute
              << ", \"mean_signed\": " << holdout_temperature.mean_signed << "}\n"
              << "    }\n  },\n"
              << "  \"holdout_cases\": [\n";
    for (std::size_t index = calibration_case_count;
         index < observations.size(); ++index) {
        const auto& observation = observations.at(index);
        const auto& prediction = predictions.at(index);
        const auto relative_error = [](double predicted, double measured) {
            return 100.0 * (predicted - measured) / measured;
        };
        std::cout << "    {\"case\": " << observation.case_id
                  << ", \"mass_flow_error_percent\": "
                  << relative_error(
                         prediction.mass_flow_kg_s,
                         observation.measured_mass_flow_kg_s)
                  << ", \"outlet_temperature_error_k\": "
                  << prediction.outlet_temperature_k -
                         observation.measured_outlet_temperature_k
                  << ", \"shaft_power_error_percent\": "
                  << relative_error(
                         prediction.shaft_power_w,
                         observation.measured_shaft_power_w)
                  << "}";
        if (index + 1U != observations.size()) std::cout << ',';
        std::cout << '\n';
    }
    std::cout << "  ],\n"
              << "  \"numerical\": {\"converged_cases\": 77, \"maximum_absolute_normalized_residual\": "
              << maximum_solver_residual << "},\n"
              << "  \"acceptance\": {\"primary_outputs\": [\"mass_flow\", \"shaft_power\"], \"mape_limit_percent\": 8.0, \"maximum_absolute_relative_error_limit_percent\": 15.0, \"passed\": "
              << (passed ? "true" : "false") << "},\n"
              << "  \"assumptions\": [\"the 14.5 cm3/rev displacement is a normalization assumption and is not independently identified by this dataset; mass-flow predictions are invariant because effective capacity is fitted\", \"measured expander inlet PT and outlet pressure are fixed component boundaries\", \"correlations are fitted only on cases 1-68\"],\n"
              << "  \"claim_limit\": \"This is a component-blocked holdout test, not external-boundary whole-ORC prediction. The frozen quadratic model fails the provisional primary-output gate and must remain negative evidence.\"\n"
              << "}\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
}
