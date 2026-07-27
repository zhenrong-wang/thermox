#pragma once

#include "thermox/linear_solver.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace thermox {

using ResidualFunction = std::function<void(const std::vector<double>& x, std::vector<double>& residual)>;
using JacobianFunction = std::function<void(const std::vector<double>& x, Matrix& jacobian)>;
using SparseJacobianFunction =
    std::function<void(const std::vector<double>& x, std::vector<SparseTriplet>& jacobian)>;
using SparseJacobianValuesFunction =
    std::function<void(const std::vector<double>& x, std::vector<double>& values)>;

enum class EvaluationStatusCode {
    success,
    recoverable_failure,
    fatal_failure,
};

struct EvaluationStatus {
    EvaluationStatusCode code{EvaluationStatusCode::success};
    std::string message;

    [[nodiscard]] bool ok() const { return code == EvaluationStatusCode::success; }
    static EvaluationStatus success() { return {}; }
    static EvaluationStatus recoverable(std::string message);
    static EvaluationStatus fatal(std::string message);
};

using CheckedResidualFunction =
    std::function<EvaluationStatus(const std::vector<double>& x, std::vector<double>& residual)>;

struct SolverOptions {
    int max_iterations{50};
    double residual_tolerance{1.0e-9};
    double step_tolerance{1.0e-10};
    double finite_difference_epsilon{1.0e-6};
    double min_damping{1.0e-6};
    double damping_reduction{0.5};
    double sufficient_decrease{1.0e-4};
    int max_line_search_steps{50};
    // Custom backends receive the dimensionless scaled Newton system and must return
    // a step in scaled variable coordinates.
    LinearSolverFunction linear_solver;
    SparseLinearSolverFunction sparse_linear_solver;
};

struct IterationDiagnostic {
    int iteration{0};
    double residual_norm{0.0};
    double accepted_residual_norm{0.0};
    double step_norm{0.0};
    double damping{1.0};
};

struct SolverDiagnostics {
    bool converged{false};
    int iterations{0};
    double final_residual_norm{0.0};
    double final_step_norm{0.0};
    int function_evaluations{0};
    int jacobian_evaluations{0};
    int linear_solver_evaluations{0};
    std::string message;
    std::vector<IterationDiagnostic> history;
};

struct NonlinearProblem {
    std::vector<std::string> variable_names;
    std::vector<std::string> residual_names;
    std::vector<double> initial_guess;
    std::vector<double> variable_scales;
    std::vector<double> residual_scales;
    std::vector<double> lower_bounds;
    std::vector<double> upper_bounds;
    ResidualFunction residual;
    CheckedResidualFunction checked_residual;
    JacobianFunction jacobian;
    SparseJacobianFunction sparse_jacobian;
    std::optional<SparsePattern> sparse_jacobian_pattern;
    SparseJacobianValuesFunction sparse_jacobian_values;
    SparseJacobianFunction partial_sparse_jacobian;
    std::vector<bool> analytic_jacobian_rows;
};

struct NonlinearSolveResult {
    std::vector<double> x;
    SolverDiagnostics diagnostics;
};

struct ProblemStructureReport {
    std::size_t variable_count{0};
    std::size_t residual_count{0};
    bool square{false};
    bool has_duplicate_variable_names{false};
    bool has_duplicate_residual_names{false};
    bool has_complete_sparse_pattern{false};
    bool structurally_nonsingular{false};
    std::vector<std::string> duplicate_variable_names;
    std::vector<std::string> duplicate_residual_names;
    std::vector<std::string> unmatched_variable_names;
    std::vector<std::string> unmatched_residual_names;
    std::vector<std::string> messages;

    [[nodiscard]] bool valid_for_newton() const;
};

ProblemStructureReport analyze_problem_structure(const NonlinearProblem& problem);
NonlinearSolveResult solve_newton(const NonlinearProblem& problem, const SolverOptions& options = {});

double l2_norm(const std::vector<double>& values);

}  // namespace thermox
