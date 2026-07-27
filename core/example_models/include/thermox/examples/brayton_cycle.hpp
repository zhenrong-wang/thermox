#pragma once

#include "thermox/nonlinear_solver.hpp"

#include <string>
#include <vector>

namespace thermox::examples {

struct BraytonCycleInput {
    std::string model_id{"brayton_simple"};
    std::string case_id{"design"};
    double ambient_pressure_pa{101325.0};
    double ambient_temperature_k{288.15};
    double pressure_ratio{12.0};
    double turbine_inlet_temperature_k{1400.0};
    double compressor_efficiency{0.86};
    double turbine_efficiency{0.89};
    double mass_flow_kg_s{100.0};
    double cp_j_kg_k{1004.5};
    double gamma{1.4};
};

struct BraytonCycleResult {
    BraytonCycleInput input;
    std::vector<std::string> variable_names;
    std::vector<double> solution;
    SolverDiagnostics diagnostics;

    double compressor_outlet_temperature_k{0.0};
    double turbine_outlet_temperature_k{0.0};
    double compressor_power_w{0.0};
    double turbine_power_w{0.0};
    double net_power_w{0.0};
    double heat_input_w{0.0};
    double thermal_efficiency{0.0};
};

NonlinearProblem build_brayton_cycle_problem(const BraytonCycleInput& input);
BraytonCycleResult solve_brayton_cycle(const BraytonCycleInput& input,
                                       const SolverOptions& options = {});
std::string format_brayton_result_json(const BraytonCycleResult& result);
std::string format_brayton_result_text(const BraytonCycleResult& result);

}  // namespace thermox::examples
