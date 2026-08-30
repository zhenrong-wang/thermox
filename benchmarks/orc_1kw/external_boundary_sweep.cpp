#include "thermox/nonlinear_solver.hpp"
#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/model_document.hpp"
#include "thermox/physics/coolprop_heos_package.hpp"
#include "thermox/physics/property_registry.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Row = std::map<std::string, double, std::less<>>;

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
        for (std::size_t i = 0; i < fields.size(); ++i)
            row.emplace(headers.at(i), std::stod(fields.at(i)));
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

thermox::platform::ScalarValue scalar(
    double value_si, std::string unit, std::string dimension) {
    return {value_si, std::move(unit), std::move(dimension)};
}

void configure_case(
    thermox::platform::ModelDocument& document,
    const Row& row,
    const thermox::physics::PropertyPackage& water,
    const thermox::physics::PropertyPackage& working_fluid,
    std::size_t case_number) {
    auto& simulation_case = document.cases.front();
    simulation_case.id = "case_" + std::to_string(case_number);
    const double hot_pressure =
        value(row, "hot_water_inlet_pressure_kpa") * 1000.0;
    const double hot_temperature =
        value(row, "hot_water_inlet_temperature_c") + 273.15;
    const double cold_pressure =
        value(row, "cooling_water_inlet_pressure_kpa") * 1000.0;
    const double cold_temperature =
        value(row, "cooling_water_inlet_temperature_c") + 273.15;
    const auto hot = water.state_pt(hot_pressure, hot_temperature);
    const auto cold = water.state_pt(cold_pressure, cold_temperature);
    if (!hot.ok() || !cold.ok())
        throw std::runtime_error("could not reconstruct utility inlet density");
    auto& fixed = simulation_case.fixed_values;
    fixed["hot_source.outlet.m_dot"] = scalar(
        value(row, "hot_water_volume_flow_lpm") / 60000.0 *
            hot.state.density_kg_m3,
        "kg/s", "mass_flow");
    fixed["hot_source.outlet.p"] =
        scalar(hot_pressure, "Pa", "pressure");
    fixed["hot_source.outlet.T"] =
        scalar(hot_temperature, "K", "temperature");
    fixed["cold_source.outlet.m_dot"] = scalar(
        value(row, "cooling_water_volume_flow_lpm") / 60000.0 *
            cold.state.density_kg_m3,
        "kg/s", "mass_flow");
    fixed["cold_source.outlet.p"] =
        scalar(cold_pressure, "Pa", "pressure");
    fixed["cold_source.outlet.T"] =
        scalar(cold_temperature, "K", "temperature");
    fixed["pump.shaft.omega"] = scalar(
        value(row, "pump_speed_rpm") * 2.0 * std::numbers::pi / 60.0,
        "rad/s", "angular_speed");
    fixed["expander.shaft.omega"] = scalar(
        value(row, "expander_speed_rpm") * 2.0 * std::numbers::pi / 60.0,
        "rad/s", "angular_speed");
    simulation_case.parameter_overrides[
        "components.charge.parameters.total_charge"] = scalar(
            value(row, "charge_kg"), "kg", "mass");
    simulation_case.parameter_overrides[
        "components.expander.parameters.ambient_temperature"] = scalar(
            value(row, "ambient_temperature_c") + 273.15,
            "K", "temperature");

    const auto water_h = [&](std::string_view pressure_column,
                             std::string_view temperature_column) {
        const auto state = water.state_pt(
            value(row, pressure_column) * 1000.0,
            value(row, temperature_column) + 273.15);
        if (!state.ok()) throw std::runtime_error(state.message);
        return state.state.enthalpy_j_kg;
    };
    const auto fluid_h = [&](std::string_view pressure_column,
                             std::string_view temperature_column) {
        const auto state = working_fluid.state_pt(
            value(row, pressure_column) * 1000.0,
            value(row, temperature_column) + 273.15);
        if (!state.ok()) throw std::runtime_error(state.message);
        return state.state.enthalpy_j_kg;
    };
    const double flow =
        value(row, "working_fluid_mass_flow_g_s") / 1000.0;
    auto& guesses = simulation_case.initial_guesses;
    const auto guess = [&](std::string name, double value_si,
                           std::string unit, std::string dimension) {
        guesses[std::move(name)] = scalar(
            value_si, std::move(unit), std::move(dimension));
    };
    for (const std::string_view name : {
             "pump.inlet.m_dot", "pump.outlet.m_dot",
             "expander.inlet.m_dot", "expander.outlet.m_dot",
             "receiver.inlet.m_dot", "receiver.outlet.m_dot",
             "evaporator/cell_1.cold_in.m_dot",
             "evaporator/cell_1.cold_out.m_dot",
             "evaporator/cell_2.cold_in.m_dot",
             "evaporator/cell_2.cold_out.m_dot",
             "condenser/cell_1.hot_in.m_dot",
             "condenser/cell_1.hot_out.m_dot",
             "condenser/cell_2.hot_in.m_dot",
             "condenser/cell_2.hot_out.m_dot"}) {
        guess(std::string{name}, flow, "kg/s", "mass_flow");
    }

    const double pump_in_p = value(row, "pump_inlet_pressure_kpa") * 1000.0;
    const double pump_out_p = value(row, "pump_outlet_pressure_kpa") * 1000.0;
    const double expander_in_p =
        value(row, "expander_inlet_pressure_kpa") * 1000.0;
    const double expander_out_p =
        value(row, "expander_outlet_pressure_kpa") * 1000.0;
    const double pump_in_h = fluid_h(
        "pump_inlet_pressure_kpa", "pump_inlet_temperature_c");
    const double pump_out_h = fluid_h(
        "pump_outlet_pressure_kpa", "pump_outlet_temperature_c");
    const double expander_in_h = fluid_h(
        "expander_inlet_pressure_kpa", "expander_inlet_temperature_c");
    const double expander_out_h = fluid_h(
        "expander_outlet_pressure_kpa", "expander_outlet_temperature_c");
    for (const auto& [name, number] : std::vector<std::pair<std::string, double>>{
             {"pump.inlet.p", pump_in_p}, {"pump.outlet.p", pump_out_p},
             {"expander.inlet.p", expander_in_p},
             {"expander.outlet.p", expander_out_p}}) {
        guess(name, number, "Pa", "pressure");
    }
    for (const auto& [name, number] : std::vector<std::pair<std::string, double>>{
             {"pump.inlet.h", pump_in_h}, {"pump.outlet.h", pump_out_h},
             {"expander.inlet.h", expander_in_h},
             {"expander.outlet.h", expander_out_h}}) {
        guess(name, number, "J/kg", "specific_energy");
    }

    const double hot_out_h = water_h(
        "hot_water_outlet_pressure_kpa", "hot_water_outlet_temperature_c");
    const double cold_out_h = water_h(
        "cooling_water_outlet_pressure_kpa",
        "cooling_water_outlet_temperature_c");
    guess("hot_sink.inlet.h", hot_out_h, "J/kg", "specific_energy");
    guess("cold_sink.inlet.h", cold_out_h, "J/kg", "specific_energy");
}

std::size_t variable_index(
    const std::vector<std::string>& names, const std::string& name) {
    const auto found = std::find(names.begin(), names.end(), name);
    if (found == names.end())
        throw std::runtime_error("compiled variable is missing: " + name);
    return static_cast<std::size_t>(std::distance(names.begin(), found));
}

struct Metric {
    std::size_t count{};
    double absolute_relative_sum{};
    double maximum_absolute_relative{};
    double bias_sum{};
};

void accumulate(Metric& metric, double predicted, double measured) {
    const double relative = (predicted - measured) / measured;
    ++metric.count;
    metric.absolute_relative_sum += std::abs(relative);
    metric.maximum_absolute_relative = std::max(
        metric.maximum_absolute_relative, std::abs(relative));
    metric.bias_sum += relative;
}

void print_metric(std::ostream& output, const Metric& metric) {
    output << "{\"count\":" << metric.count
           << ",\"mape_percent\":"
           << 100.0 * metric.absolute_relative_sum / metric.count
           << ",\"maximum_absolute_relative_error_percent\":"
           << 100.0 * metric.maximum_absolute_relative
           << ",\"mean_bias_percent\":"
           << 100.0 * metric.bias_sum / metric.count << '}';
}

std::string json_string(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 2U);
    escaped.push_back('"');
    for (const char character : value) {
        switch (character) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(character); break;
        }
    }
    escaped.push_back('"');
    return escaped;
}

void run(
    const std::string& model_path,
    const std::vector<Row>& rows,
    std::size_t first,
    std::size_t last) {
    const auto base = thermox::platform::load_model_document(model_path);
    const auto components =
        thermox::platform::make_default_component_registry();
    const auto properties =
        thermox::physics::make_default_property_package_registry();
    const thermox::physics::CoolPropHeosPropertyPackage water{"Water"};
    const thermox::physics::CoolPropHeosPropertyPackage r245fa{"R245fa"};
    std::vector<double> previous_solution;
    std::vector<std::string> previous_names;
    Metric mass_flow;
    Metric expander_power;
    std::size_t converged = 0;
    std::size_t total_solver_iterations = 0;
    const auto started = std::chrono::steady_clock::now();
    std::cout << "{\"schema_version\":\"thermox.orc_external_boundary_sweep/v1\","
              << "\"classification\":\"preliminary_parameter_diagnostic_not_validation\","
              << "\"case_range\":\"" << first << '-' << last
              << "\",\"cases\":[";
    for (std::size_t case_number = first;
         case_number <= last; ++case_number) {
        auto document = base;
        const auto& row = rows.at(case_number - 1U);
        configure_case(document, row, water, r245fa, case_number);
        auto graph = thermox::platform::compile_model_graph(
            document, components, properties,
            document.cases.front().id);
        const auto measured_endpoint_guess = graph.problem.initial_guess;
        bool used_warm_start = false;
        if (!previous_solution.empty()) {
            if (graph.problem.variable_names != previous_names)
                throw std::runtime_error(
                    "case graph variable ordering changed during sweep");
            graph.problem.initial_guess = previous_solution;
            used_warm_start = true;
        }
        thermox::SolverOptions options;
        options.max_iterations = 35;
        options.residual_tolerance = 1.0e-9;
        auto solved = thermox::solve_newton(graph.problem, options);
        total_solver_iterations += solved.diagnostics.iterations;
        std::size_t solve_attempts = 1U;
        if (!solved.diagnostics.converged && used_warm_start) {
            graph.problem.initial_guess = measured_endpoint_guess;
            auto retry = thermox::solve_newton(graph.problem, options);
            total_solver_iterations += retry.diagnostics.iterations;
            ++solve_attempts;
            if (retry.diagnostics.converged ||
                retry.diagnostics
                        .final_maximum_absolute_normalized_residual <
                    solved.diagnostics
                        .final_maximum_absolute_normalized_residual) {
                solved = std::move(retry);
                used_warm_start = false;
            }
        }
        if (case_number != first) std::cout << ',';
        std::cout << "{\"case\":" << case_number
                  << ",\"converged\":"
                  << (solved.diagnostics.converged ? "true" : "false")
                  << ",\"iterations\":" << solved.diagnostics.iterations
                  << ",\"solve_attempts\":" << solve_attempts
                  << ",\"selected_initialization\":"
                  << json_string(used_warm_start
                         ? "previous_converged_solution"
                         : "measured_component_endpoints")
                  << ",\"maximum_scaled_residual\":"
                  << solved.diagnostics
                         .final_maximum_absolute_normalized_residual;
        if (!solved.diagnostics.converged) {
            std::cout << ",\"message\":"
                      << json_string(solved.diagnostics.message) << '}';
            continue;
        }
        ++converged;
        previous_solution = solved.x;
        previous_names = graph.problem.variable_names;
        const auto solved_value = [&](const std::string& name) {
            return solved.x.at(variable_index(
                graph.problem.variable_names, name));
        };
        const double predicted_mass = solved_value("pump.inlet.m_dot");
        const double predicted_power = solved_value("expander.shaft.W_dot");
        const double measured_mass =
            value(row, "working_fluid_mass_flow_g_s") / 1000.0;
        const double angular_speed =
            value(row, "expander_speed_rpm") *
            2.0 * std::numbers::pi / 60.0;
        const double measured_power =
            value(row, "expander_torque_n_m") * angular_speed;
        accumulate(mass_flow, predicted_mass, measured_mass);
        accumulate(expander_power, predicted_power, measured_power);
        const auto expander_outlet = r245fa.state_ph(
            solved_value("expander.outlet.p"),
            solved_value("expander.outlet.h"));
        if (!expander_outlet.ok())
            throw std::runtime_error(expander_outlet.message);
        std::cout << ",\"predicted_mass_flow\":" << predicted_mass
                  << ",\"predicted_expander_power\":" << predicted_power
                  << ",\"predicted_expander_outlet_temperature\":"
                  << expander_outlet.state.temperature_k << '}';
    }
    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    std::cout << "],\"summary\":{\"requested_cases\":"
              << last - first + 1U << ",\"converged_cases\":"
              << converged << ",\"total_solver_iterations\":"
              << total_solver_iterations << ",\"elapsed_seconds\":"
              << elapsed << ",\"mass_flow\":";
    if (mass_flow.count > 0U) print_metric(std::cout, mass_flow);
    else std::cout << "null";
    std::cout << ",\"expander_power\":";
    if (expander_power.count > 0U)
        print_metric(std::cout, expander_power);
    else std::cout << "null";
    std::cout << "}}\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 5)
            throw std::invalid_argument(
                "usage: thermox_orc_1kw_external_boundary_sweep "
                "<model.json> <measurements.csv> <first-case> <last-case>");
        const auto rows = read_csv(argv[2]);
        const auto first = static_cast<std::size_t>(std::stoul(argv[3]));
        const auto last = static_cast<std::size_t>(std::stoul(argv[4]));
        if (first == 0U || last < first || last > rows.size())
            throw std::invalid_argument("case range is outside source data");
        run(argv[1], rows, first, last);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "ORC external-boundary sweep failed: "
                  << ex.what() << '\n';
        return 1;
    }
}
