#pragma once

#include "thermox/examples/schema.hpp"
#include "thermox/nonlinear_solver.hpp"

#include <string>
#include <vector>

namespace thermox::examples {

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
