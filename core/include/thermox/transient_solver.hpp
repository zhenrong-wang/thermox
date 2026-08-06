#pragma once

#include "thermox/nonlinear_solver.hpp"

#include <functional>
#include <string>
#include <vector>

namespace thermox {

enum class DaeVariableKind {
    differential,
    algebraic,
};

using DaeResidualFunction =
    std::function<EvaluationStatus(double time,
                                   const std::vector<double>& state,
                                   const std::vector<double>& derivative,
                                   std::vector<double>& residual)>;

// Assemble dF/dy + derivative_coefficient * dF/d(ydot).
using DaeJacobianFunction =
    std::function<EvaluationStatus(double time,
                                   const std::vector<double>& state,
                                   const std::vector<double>& derivative,
                                   double derivative_coefficient,
                                   Matrix& jacobian)>;
using DaeSparseJacobianFunction =
    std::function<EvaluationStatus(double time,
                                   const std::vector<double>& state,
                                   const std::vector<double>& derivative,
                                   double derivative_coefficient,
                                   std::vector<SparseTriplet>& jacobian)>;
using DaeSparseJacobianValuesFunction =
    std::function<EvaluationStatus(double time,
                                   const std::vector<double>& state,
                                   const std::vector<double>& derivative,
                                   double derivative_coefficient,
                                   std::vector<double>& values)>;

enum class EventDirection {
    any,
    rising,
    falling,
};

struct DaeEvent {
    std::string name;
    std::function<double(double time, const std::vector<double>& state)> evaluate;
    EventDirection direction{EventDirection::any};
    bool terminal{false};
};

struct DaeProblem {
    std::vector<std::string> variable_names;
    std::vector<std::string> residual_names;
    std::vector<DaeVariableKind> variable_kinds;
    std::vector<double> initial_state;
    std::vector<double> initial_derivative;
    std::vector<double> variable_scales;
    std::vector<double> derivative_scales;
    std::vector<double> residual_scales;
    std::vector<double> lower_bounds;
    std::vector<double> upper_bounds;
    DaeResidualFunction residual;
    DaeJacobianFunction jacobian;
    DaeSparseJacobianFunction sparse_jacobian;
    std::optional<SparsePattern> sparse_jacobian_pattern;
    DaeSparseJacobianValuesFunction sparse_jacobian_values;
    std::vector<DaeEvent> events;
};

struct DaeInitializationResult {
    std::vector<double> state;
    std::vector<double> derivative;
    SolverDiagnostics diagnostics;
};

struct TimeIntegrationOptions {
    double start_time{0.0};
    double end_time{1.0};
    double initial_step{1.0e-3};
    double min_step{1.0e-9};
    double max_step{0.1};
    double absolute_tolerance{1.0e-7};
    double relative_tolerance{1.0e-5};
    int max_steps{100000};
    int max_consecutive_rejections{20};
    // Native variable-order BDF currently supports orders 1 and 2.
    int maximum_order{2};
    bool compute_consistent_initial_conditions{true};
    SolverOptions nonlinear_options = [] {
        SolverOptions options;
        options.residual_tolerance = 1.0e-8;
        return options;
    }();
};

struct DaeState {
    double time{0.0};
    std::vector<double> state;
    std::vector<double> derivative;
};

struct DetectedEvent {
    std::string name;
    double time{0.0};
    std::vector<double> state;
    bool terminal{false};
};

struct TimeIntegrationDiagnostics {
    bool success{false};
    int accepted_steps{0};
    int rejected_steps{0};
    int maximum_order_used{0};
    int nonlinear_solves{0};
    int nonlinear_iterations{0};
    int symbolic_factorizations{0};
    int numeric_factorizations{0};
    std::string linear_solver_backend{"not-used"};
    double final_time{0.0};
    double last_step{0.0};
    std::string message;
};

struct DaeSolveResult {
    std::vector<DaeState> trajectory;
    std::vector<DetectedEvent> events;
    TimeIntegrationDiagnostics diagnostics;
};

DaeInitializationResult make_consistent_initial_conditions(
    const DaeProblem& problem,
    double initial_time,
    const SolverOptions& options = {});

DaeSolveResult integrate_dae(const DaeProblem& problem,
                             const TimeIntegrationOptions& options = {});

}  // namespace thermox
