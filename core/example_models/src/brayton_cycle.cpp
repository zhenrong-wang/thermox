#include "thermox/examples/brayton_cycle.hpp"

#include "thermox/equation_system.hpp"
#include "thermox/examples/ideal_gas.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace thermox::examples {

namespace {

constexpr std::size_t kCompressorOutletTemperature = 0;
constexpr std::size_t kTurbineOutletTemperature = 1;

void validate_brayton_input(const BraytonCycleInput& input) {
    if (input.ambient_pressure_pa <= 0.0) {
        throw std::invalid_argument("ambient_pressure_pa must be positive");
    }
    if (input.ambient_temperature_k <= 0.0) {
        throw std::invalid_argument("ambient_temperature_k must be positive");
    }
    if (input.pressure_ratio <= 1.0) {
        throw std::invalid_argument("pressure_ratio must be greater than 1");
    }
    if (input.turbine_inlet_temperature_k <= input.ambient_temperature_k) {
        throw std::invalid_argument("turbine_inlet_temperature_k must exceed ambient_temperature_k");
    }
    if (input.compressor_efficiency <= 0.0 || input.compressor_efficiency > 1.0) {
        throw std::invalid_argument("compressor_efficiency must be in (0, 1]");
    }
    if (input.turbine_efficiency <= 0.0 || input.turbine_efficiency > 1.0) {
        throw std::invalid_argument("turbine_efficiency must be in (0, 1]");
    }
    if (input.mass_flow_kg_s <= 0.0) {
        throw std::invalid_argument("mass_flow_kg_s must be positive");
    }
    if (input.cp_j_kg_k <= 0.0) {
        throw std::invalid_argument("cp_j_kg_k must be positive");
    }
    if (input.gamma <= 1.0) {
        throw std::invalid_argument("gamma must be greater than 1");
    }
}

}  // namespace

NonlinearProblem build_brayton_cycle_problem(const BraytonCycleInput& input) {
    validate_brayton_input(input);

    const IdealGas gas{input.cp_j_kg_k, input.gamma, 0.0};
    const double t2s = gas.isentropic_temperature_out(input.ambient_temperature_k,
                                                      input.pressure_ratio);
    const double t2_guess = input.ambient_temperature_k +
                            (t2s - input.ambient_temperature_k) / input.compressor_efficiency;
    const double t4s = gas.isentropic_temperature_out(input.turbine_inlet_temperature_k,
                                                      1.0 / input.pressure_ratio);
    const double t4_guess = input.turbine_inlet_temperature_k -
                            input.turbine_efficiency *
                                (input.turbine_inlet_temperature_k - t4s);

    EquationSystemBuilder system;
    system.add_variable("compressor_outlet_temperature_k", t2_guess * 0.95, 100.0);
    system.add_variable("turbine_outlet_temperature_k", t4_guess * 1.05, 100.0);

    system.add_equation(
        "compressor_isentropic_efficiency",
        [input](const std::vector<double>& x) {
            if (x.size() != 2) {
                throw std::invalid_argument("Brayton compressor equation expects two variables");
            }
            const IdealGas gas{input.cp_j_kg_k, input.gamma, 0.0};
            const double t1 = input.ambient_temperature_k;
            const double t2 = x[kCompressorOutletTemperature];
            const double t2s = gas.isentropic_temperature_out(t1, input.pressure_ratio);
            const double expected_t2 = t1 + (t2s - t1) / input.compressor_efficiency;
            return t2 - expected_t2;
        },
        100.0);

    system.add_equation(
        "turbine_isentropic_efficiency",
        [input](const std::vector<double>& x) {
            if (x.size() != 2) {
                throw std::invalid_argument("Brayton turbine equation expects two variables");
            }
            const IdealGas gas{input.cp_j_kg_k, input.gamma, 0.0};
            const double t3 = input.turbine_inlet_temperature_k;
            const double t4 = x[kTurbineOutletTemperature];
            const double t4s = gas.isentropic_temperature_out(t3, 1.0 / input.pressure_ratio);
            const double expected_t4 = t3 - input.turbine_efficiency * (t3 - t4s);
            return t4 - expected_t4;
        },
        100.0);

    return system.build();
}

BraytonCycleResult solve_brayton_cycle(const BraytonCycleInput& input,
                                       const SolverOptions& options) {
    auto problem = build_brayton_cycle_problem(input);
    auto solve = solve_newton(problem, options);

    BraytonCycleResult result;
    result.input = input;
    result.variable_names = problem.variable_names;
    result.solution = solve.x;
    result.diagnostics = solve.diagnostics;

    if (solve.x.size() == 2) {
        result.compressor_outlet_temperature_k = solve.x[kCompressorOutletTemperature];
        result.turbine_outlet_temperature_k = solve.x[kTurbineOutletTemperature];
        result.compressor_power_w = input.mass_flow_kg_s * input.cp_j_kg_k *
                                    (result.compressor_outlet_temperature_k -
                                     input.ambient_temperature_k);
        result.turbine_power_w = input.mass_flow_kg_s * input.cp_j_kg_k *
                                 (input.turbine_inlet_temperature_k -
                                  result.turbine_outlet_temperature_k);
        result.net_power_w = result.turbine_power_w - result.compressor_power_w;
        result.heat_input_w = input.mass_flow_kg_s * input.cp_j_kg_k *
                              (input.turbine_inlet_temperature_k -
                               result.compressor_outlet_temperature_k);
        result.thermal_efficiency = result.net_power_w / result.heat_input_w;
    }

    return result;
}

std::string format_brayton_result_json(const BraytonCycleResult& result) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(8);
    out << "{\n";
    out << "  \"model_id\": \"" << result.input.model_id << "\",\n";
    out << "  \"case_id\": \"" << result.input.case_id << "\",\n";
    out << "  \"converged\": " << (result.diagnostics.converged ? "true" : "false") << ",\n";
    out << "  \"iterations\": " << result.diagnostics.iterations << ",\n";
    out << "  \"final_residual_norm\": " << result.diagnostics.final_residual_norm << ",\n";
    out << "  \"final_maximum_absolute_normalized_residual\": "
        << result.diagnostics
               .final_maximum_absolute_normalized_residual
        << ",\n";
    out << "  \"limiting_residual\": \""
        << result.diagnostics.limiting_residual << "\",\n";
    out << "  \"message\": \"" << result.diagnostics.message << "\",\n";
    out << "  \"variables\": {\n";
    for (std::size_t i = 0; i < result.variable_names.size(); ++i) {
        out << "    \"" << result.variable_names[i] << "\": " << result.solution[i]
            << (i + 1 == result.variable_names.size() ? "\n" : ",\n");
    }
    out << "  },\n";
    out << "  \"performance\": {\n";
    out << "    \"compressor_power_w\": " << result.compressor_power_w << ",\n";
    out << "    \"turbine_power_w\": " << result.turbine_power_w << ",\n";
    out << "    \"net_power_w\": " << result.net_power_w << ",\n";
    out << "    \"heat_input_w\": " << result.heat_input_w << ",\n";
    out << "    \"thermal_efficiency\": " << result.thermal_efficiency << "\n";
    out << "  }\n";
    out << "}\n";
    return out.str();
}

std::string format_brayton_result_text(const BraytonCycleResult& result) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(4);
    out << "Thermox Brayton cycle solve\n";
    out << "model: " << result.input.model_id << " case: " << result.input.case_id << "\n";
    out << "converged: " << (result.diagnostics.converged ? "yes" : "no")
        << " iterations: " << result.diagnostics.iterations
        << " residual_norm: " << std::scientific << result.diagnostics.final_residual_norm
        << " worst_residual: "
        << result.diagnostics
               .final_maximum_absolute_normalized_residual
        << " limiting_equation: "
        << result.diagnostics.limiting_residual
        << std::fixed << "\n";
    out << "message: " << result.diagnostics.message << "\n";
    out << "\nVariables\n";
    for (std::size_t i = 0; i < result.variable_names.size(); ++i) {
        out << "  " << result.variable_names[i] << " = " << result.solution[i] << "\n";
    }
    out << "\nPerformance\n";
    out << "  compressor_power_w = " << result.compressor_power_w << "\n";
    out << "  turbine_power_w = " << result.turbine_power_w << "\n";
    out << "  net_power_w = " << result.net_power_w << "\n";
    out << "  heat_input_w = " << result.heat_input_w << "\n";
    out << "  thermal_efficiency = " << result.thermal_efficiency << "\n";
    return out.str();
}

}  // namespace thermox::examples
