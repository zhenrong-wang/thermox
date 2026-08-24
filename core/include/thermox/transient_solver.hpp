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
using DaeResidualSubsetFunction =
    std::function<EvaluationStatus(
        double time,
        const std::vector<double>& state,
        const std::vector<double>& derivative,
        const std::vector<std::size_t>& residual_indices,
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
using DaeSparseJacobianValuesSubsetFunction =
    std::function<EvaluationStatus(
        double time,
        const std::vector<double>& state,
        const std::vector<double>& derivative,
        double derivative_coefficient,
        const std::vector<std::size_t>& value_offsets,
        std::vector<double>& values)>;

enum class EventDirection {
    any,
    rising,
    falling,
};

struct DaeEvent {
    std::string name;
    // Checked event surfaces make domain/property failures explicit instead
    // of silently turning a failed evaluation into a missed crossing.
    std::function<EvaluationStatus(
        double time,
        const std::vector<double>& state,
        double& value)> evaluate;
    EventDirection direction{EventDirection::any};
    bool terminal{false};
    // Optional accepted-crossing transition. It may change discrete data
    // captured by the residual and may reset state values. The integrator
    // restores consistent algebraic states and derivatives afterward.
    std::function<EvaluationStatus(
        double time,
        std::vector<double>& state,
        std::vector<double>& derivative)> transition;
    // Simultaneous transitions execute in ascending priority order so the
    // highest-priority action has final authority over shared inputs.
    int priority{0};
    // Event-surface distance required on the inactive side before rearming.
    double hysteresis{0.0};
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
    bool automatic_structural_decomposition_safe{false};
    DaeResidualFunction residual;
    DaeResidualSubsetFunction residual_subset;
    DaeJacobianFunction jacobian;
    DaeSparseJacobianFunction sparse_jacobian;
    std::optional<SparsePattern> structural_jacobian_pattern;
    std::optional<SparsePattern> sparse_jacobian_pattern;
    DaeSparseJacobianValuesFunction sparse_jacobian_values;
    DaeSparseJacobianValuesSubsetFunction
        sparse_jacobian_values_subset;
    std::vector<DaeEvent> events;
    // Restores residual-owned discrete modes before each integration so a
    // compiled problem is deterministic across sequential executions.
    std::function<EvaluationStatus()> reset_discrete_state;
    // Problem-owned times where a time-dependent residual changes slope or
    // regime. The adaptive integrator lands exactly on each in-range value
    // and restarts multistep history before continuing.
    std::vector<double> time_breakpoints;
    // Right-continuous discontinuities. Each must also be a time breakpoint.
    // The integrator advances to its left limit, preserves differential
    // states, and performs a consistent algebraic/derivative reinitialization
    // at the exact declared time.
    std::vector<double> time_discontinuities;
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
    // Dimensionless multiplier for each differential state's declared
    // variable_scale. This avoids applying one dimensional absolute value
    // to heterogeneous thermal states.
    double absolute_tolerance{1.0e-7};
    double relative_tolerance{1.0e-5};
    int max_steps{100000};
    int max_consecutive_rejections{20};
    // Native variable-order BDF currently supports orders 1 and 2.
    int maximum_order{2};
    bool compute_consistent_initial_conditions{true};
    // Optional, strictly increasing times at which the adaptive integrator
    // must emit an accepted state. Values must lie in [start_time,
    // end_time]. This is intended for measurement-aligned simulation and
    // avoids treating a nearby adaptive step as an observation sample.
    std::vector<double> required_output_times;
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
    bool discontinuous_from_previous{false};
};

struct DetectedEvent {
    std::string name;
    double time{0.0};
    std::vector<double> state;
    bool terminal{false};
    bool transitioned{false};
    int priority{0};
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
    int factorization_quality_observations{0};
    double last_reciprocal_pivot_ratio{0.0};
    double minimum_reciprocal_pivot_ratio{0.0};
    double minimum_absolute_pivot_at_minimum_ratio{0.0};
    double maximum_absolute_pivot_at_minimum_ratio{0.0};
    std::size_t accepted_pivot_count_at_minimum_ratio{0};
    std::size_t factorization_size_at_minimum_ratio{0};
    std::string factorization_quality_method;
    double maximum_linear_backward_error{0.0};
    int linear_refinement_attempts{0};
    int linear_refinement_successes{0};
    int structural_block_solves{0};
    std::size_t largest_linear_system_size{0};
    int structural_tearing_attempts{0};
    int structural_tearing_successes{0};
    int structural_tearing_fallbacks{0};
    std::size_t largest_tearing_inner_system_size{0};
    std::size_t largest_tearing_outer_system_size{0};
    std::size_t largest_tearing_inner_nonzero_count{0};
    std::string last_structural_tearing_fallback;
    std::string linear_solver_backend{"not-used"};
    double final_time{0.0};
    double last_step{0.0};
    double last_error_norm{0.0};
    double maximum_accepted_error_norm{0.0};
    double maximum_error_ratio{0.0};
    std::string limiting_error_variable;
    double maximum_absolute_normalized_residual{0.0};
    std::string limiting_nonlinear_residual;
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
