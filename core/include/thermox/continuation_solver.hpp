#pragma once

#include "thermox/nonlinear_solver.hpp"

#include <string>
#include <vector>

namespace thermox {

struct ContinuationOptions {
    double initial_step{0.25};
    double minimum_step{1.0 / 64.0};
    double step_growth{1.5};
    double step_reduction{0.5};
    int maximum_stages{100};
};

struct ContinuationStageDiagnostic {
    double start_parameter{0.0};
    double target_parameter{0.0};
    bool accepted{false};
    SolverDiagnostics nonlinear;
};

struct ContinuationDiagnostics {
    bool converged{false};
    bool used_informed_path{false};
    double reached_parameter{0.0};
    int accepted_stages{0};
    int rejected_stages{0};
    std::string message;
    std::vector<ContinuationStageDiagnostic> stages;
};

struct ContinuationSolveResult {
    std::vector<double> x;
    SolverDiagnostics diagnostics;
    ContinuationDiagnostics continuation;
};

[[nodiscard]] ContinuationSolveResult solve_continuation(
    const NonlinearProblem& problem,
    const SolverOptions& solver_options = {},
    const ContinuationOptions& continuation_options = {});

}  // namespace thermox
