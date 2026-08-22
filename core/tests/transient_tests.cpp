#include "thermox/dae_equation_system.hpp"
#include "thermox/dense_linear_solver.hpp"
#include "thermox/sparse_linear_solver.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(double actual, double expected, double tolerance, const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message + ": actual=" + std::to_string(actual) +
                                 " expected=" + std::to_string(expected));
    }
}

thermox::DaeProblem make_decay_problem() {
    thermox::DaeProblem problem;
    problem.variable_names = {"temperature_excess"};
    problem.residual_names = {"thermal_capacitance_balance"};
    problem.variable_kinds = {thermox::DaeVariableKind::differential};
    problem.initial_state = {1.0};
    problem.initial_derivative = {0.0};
    problem.variable_scales = {1.0};
    problem.derivative_scales = {1.0};
    problem.residual_scales = {1.0};
    problem.lower_bounds = {0.0};
    problem.upper_bounds = {2.0};
    problem.residual = [](double,
                          const std::vector<double>& state,
                          const std::vector<double>& derivative,
                          std::vector<double>& residual) {
        residual[0] = derivative[0] + state[0];
        return thermox::EvaluationStatus::success();
    };
    problem.jacobian = [](double,
                          const std::vector<double>&,
                          const std::vector<double>&,
                          double derivative_coefficient,
                          thermox::Matrix& jacobian) {
        jacobian[0][0] = 1.0 + derivative_coefficient;
        return thermox::EvaluationStatus::success();
    };
    return problem;
}

void test_dae_equation_system_builder() {
    thermox::DaeEquationSystemBuilder system;
    const auto inventory = system.add_variable(
        "inventory", thermox::DaeVariableKind::differential,
        0.2, 0.0, 1.0, 1.0);
    const auto flow = system.add_variable(
        "flow", thermox::DaeVariableKind::algebraic,
        0.2, 0.0, 1.0, 1.0);
    system.add_linear_equation(
        "inventory_balance",
        {{inventory, 1.0, 1.0}, {flow, -1.0, 0.0}}, 0.0);
    system.add_linear_equation(
        "flow_constraint",
        {{inventory, 1.0, 0.0}, {flow, 1.0, 0.0}}, 1.0);

    const auto problem = system.build();
    require(problem.sparse_jacobian_pattern.has_value(),
            "DAE builder emits a fixed sparse Jacobian pattern");
    require(
        static_cast<bool>(problem.residual_subset) &&
            static_cast<bool>(
                problem.sparse_jacobian_values_subset),
        "DAE builder emits row-selective callbacks");
    std::vector<double> subset_residual(1, 0.0);
    auto subset_status = problem.residual_subset(
        0.0, problem.initial_state,
        problem.initial_derivative, {1}, subset_residual);
    require(
        subset_status.ok() &&
            std::abs(subset_residual[0] + 0.6) < 1.0e-12,
        "DAE subset residual evaluates the requested row");
    std::vector<double> subset_values(1, 0.0);
    subset_status = problem.sparse_jacobian_values_subset(
        0.0, problem.initial_state,
        problem.initial_derivative, 2.0,
        {problem.sparse_jacobian_pattern->row_offsets()[1]},
        subset_values);
    require(
        subset_status.ok() && subset_values[0] == 1.0,
        "DAE subset Jacobian evaluates the requested fixed-pattern value");
    const auto initialized =
        thermox::make_consistent_initial_conditions(problem, 0.0);
    require(initialized.diagnostics.converged, initialized.diagnostics.message);
    require_near(initialized.state[0], 0.2, 1.0e-9,
                 "builder retains differential initial state");
    require_near(initialized.state[1], 0.8, 1.0e-8,
                 "builder solves algebraic initial state");
    require_near(initialized.derivative[0], 0.6, 1.0e-8,
                 "builder solves differential initial derivative");

    thermox::TimeIntegrationOptions options;
    options.end_time = 0.2;
    options.initial_step = 0.1;
    options.max_step = 0.1;
    options.nonlinear_options.sparse_factorization =
        thermox::make_default_sparse_factorization();
    const bool umfpack =
        options.nonlinear_options.sparse_factorization
            ->backend_name() == "umfpack";
    const auto integrated =
        thermox::integrate_dae(problem, options);
    require(integrated.diagnostics.success,
            integrated.diagnostics.message);
    require(integrated.diagnostics.numeric_factorizations > 1,
            "DAE stages perform repeated numeric factorization");
    require(
        integrated.diagnostics.symbolic_factorizations ==
            (umfpack ? 1 : 0),
        "DAE stages reuse fixed-pattern symbolic analysis: actual=" +
            std::to_string(
                integrated.diagnostics.symbolic_factorizations));
    require(
        integrated.diagnostics.linear_solver_backend ==
            options.nonlinear_options.sparse_factorization
                ->backend_name(),
        "DAE diagnostics report selected sparse backend");
    require(
        integrated.diagnostics.maximum_linear_backward_error <=
            options.nonlinear_options.linear_residual_tolerance,
        "DAE diagnostics bound the worst implicit linear solve error");
    require(
        integrated.diagnostics.factorization_quality_observations ==
                integrated.diagnostics.numeric_factorizations &&
            integrated.diagnostics.minimum_reciprocal_pivot_ratio > 0.0 &&
            integrated.diagnostics
                    .accepted_pivot_count_at_minimum_ratio ==
                integrated.diagnostics
                    .factorization_size_at_minimum_ratio &&
            integrated.diagnostics
                    .factorization_size_at_minimum_ratio == 2,
        "DAE diagnostics aggregate backend pivot evidence across stages");

    thermox::TimeIntegrationOptions refined_options = options;
    refined_options.nonlinear_options.sparse_factorization.reset();
    refined_options.nonlinear_options.linear_solver = [](
        thermox::Matrix matrix,
        std::vector<double> rhs) {
        const auto exact = thermox::solve_dense_linear_system(
            std::move(matrix), std::move(rhs));
        if (!exact.success) return exact;
        auto inexact = exact;
        for (auto& value : inexact.x) {
            value *= 1.0 - 1.0e-4;
        }
        return inexact;
    };
    const auto refined = thermox::integrate_dae(
        problem, refined_options);
    require(
        refined.diagnostics.success &&
            refined.diagnostics.linear_refinement_attempts > 0 &&
            refined.diagnostics.linear_refinement_successes > 0 &&
            refined.diagnostics.linear_refinement_attempts >=
                refined.diagnostics.linear_refinement_successes &&
            refined.diagnostics.linear_refinement_attempts <=
                2 * refined.diagnostics.linear_refinement_successes,
        "DAE orchestration aggregates recovered linear solves across "
        "initialization and implicit stages: success=" +
            std::to_string(refined.diagnostics.success) +
            " attempts=" + std::to_string(
                refined.diagnostics.linear_refinement_attempts) +
            " recoveries=" + std::to_string(
                refined.diagnostics.linear_refinement_successes) +
            " message=" + refined.diagnostics.message);
    require_near(
        refined.trajectory.back().state[0],
        integrated.trajectory.back().state[0], 1.0e-10,
        "refined DAE integration preserves the direct-solve trajectory");

    thermox::TimeIntegrationOptions tearing_options = options;
    tearing_options.nonlinear_options.sparse_factorization.reset();
    tearing_options.nonlinear_options.structural_decomposition_policy =
        thermox::StructuralDecompositionPolicy::tearing;
    const auto torn = thermox::integrate_dae(
        problem, tearing_options);
    require(
        torn.diagnostics.success &&
            torn.diagnostics.linear_solver_backend.starts_with(
                "structural-schur/") &&
            torn.diagnostics.structural_tearing_attempts > 0 &&
            torn.diagnostics.structural_tearing_successes ==
                torn.diagnostics.structural_tearing_attempts &&
            torn.diagnostics.structural_tearing_fallbacks == 0 &&
            torn.diagnostics.maximum_linear_backward_error <=
                tearing_options.nonlinear_options
                    .linear_residual_tolerance,
        "DAE initialization and implicit stages support exact structural tearing");
    require_near(
        torn.trajectory.back().state[0],
        integrated.trajectory.back().state[0], 1.0e-12,
        "torn DAE integration preserves the ordinary trajectory");
}

void test_transient_solver_executes_independent_structural_blocks() {
    thermox::DaeEquationSystemBuilder system;
    const auto first = system.add_variable(
        "first", thermox::DaeVariableKind::differential,
        1.0, -1.0, 1.0, 1.0);
    const auto second = system.add_variable(
        "second", thermox::DaeVariableKind::differential,
        2.0, -4.0, 1.0, 1.0);
    system.add_linear_equation(
        "first_decay", {{first, 1.0, 1.0}}, 0.0);
    system.add_linear_equation(
        "second_decay", {{second, 2.0, 1.0}}, 0.0);
    const auto problem = system.build();

    thermox::TimeIntegrationOptions options;
    options.end_time = 0.1;
    options.initial_step = 0.05;
    options.max_step = 0.05;
    const bool umfpack =
        thermox::make_default_sparse_factorization()
            ->backend_name() == "umfpack";
    const auto result = thermox::integrate_dae(problem, options);
    require(result.diagnostics.success,
            result.diagnostics.message);
    require(
        result.diagnostics.structural_block_solves > 0 &&
            result.diagnostics.largest_linear_system_size == 1,
        "transient initialization and stages execute independent scalar blocks");
    require(result.diagnostics.numeric_factorizations > 1,
            "transient block stages refresh numeric factorizations");
    require(
        result.diagnostics.symbolic_factorizations ==
            (umfpack ? 1 : 0),
        "equal transient block patterns share one symbolic analysis: actual=" +
            std::to_string(
                result.diagnostics.symbolic_factorizations));
}

void test_dae_equation_system_builder_rejects_non_square_system() {
    thermox::DaeEquationSystemBuilder system;
    system.add_variable("state", thermox::DaeVariableKind::differential,
                        1.0, 0.0);
    try {
        (void)system.build();
    } catch (const std::invalid_argument& ex) {
        require(std::string(ex.what()).find("must be square") != std::string::npos,
                "non-square DAE builder diagnostic is actionable");
        return;
    }
    throw std::runtime_error("DAE builder should reject a non-square system");
}

void test_consistent_initial_conditions_for_ode() {
    const auto problem = make_decay_problem();
    const auto initialized =
        thermox::make_consistent_initial_conditions(problem, 0.0);
    require(initialized.diagnostics.converged, initialized.diagnostics.message);
    require_near(initialized.state[0], 1.0, 0.0, "ODE initial state remains fixed");
    require_near(initialized.derivative[0], -1.0, 1.0e-7,
                 "ODE consistent initial derivative");
}

void test_singular_initialization_names_unresolved_unknown() {
    thermox::DaeProblem problem;
    problem.variable_names = {"pressure", "unresolved_flow"};
    problem.residual_names = {"constraint_a", "constraint_b"};
    problem.variable_kinds = {
        thermox::DaeVariableKind::algebraic,
        thermox::DaeVariableKind::algebraic};
    problem.initial_state = {1.0, 1.0};
    problem.initial_derivative = {0.0, 0.0};
    problem.residual = [](
        double, const std::vector<double>& state,
        const std::vector<double>&,
        std::vector<double>& residual) {
        residual[0] = state[0] + state[1] - 1.0;
        residual[1] = 2.0 * state[0] + 2.0 * state[1] - 3.0;
        return thermox::EvaluationStatus::success();
    };
    const auto initialized =
        thermox::make_consistent_initial_conditions(problem, 0.0);
    require(!initialized.diagnostics.converged,
            "dependent initialization equations must fail");
    require(
        initialized.diagnostics.message.find("unresolved_flow") !=
            std::string::npos,
        "singular initialization diagnostic must name the unresolved "
        "unknown: " + initialized.diagnostics.message);
}

void test_adaptive_dae_integration() {
    auto problem = make_decay_problem();
    problem.events.push_back(thermox::DaeEvent{
        "half_value",
        [](double, const std::vector<double>& state) { return state[0] - 0.5; },
        thermox::EventDirection::falling,
        false});

    thermox::TimeIntegrationOptions options;
    options.end_time = 1.0;
    options.initial_step = 0.2;
    options.max_step = 0.25;
    options.absolute_tolerance = 1.0e-6;
    options.relative_tolerance = 1.0e-4;
    const auto result = thermox::integrate_dae(problem, options);
    require(result.diagnostics.success, result.diagnostics.message);
    require(result.trajectory.size() > 2, "adaptive integration stores trajectory");
    require(result.diagnostics.accepted_steps > 0, "adaptive integration accepts steps");
    require(result.diagnostics.nonlinear_solves >= result.diagnostics.accepted_steps,
            "implicit integration records nonlinear solves");
    require_near(result.trajectory.back().time, 1.0, 1.0e-12,
                 "integration reaches requested end time");
    require_near(result.trajectory.back().state[0], std::exp(-1.0), 2.0e-3,
                 "implicit transient solution follows exponential decay");
    require(result.events.size() == 1, "falling event is detected once");
    require_near(result.events[0].time, std::log(2.0), 2.0e-2,
                 "event time is interpolated");
}

void test_adaptive_error_control_uses_physical_variable_scales() {
    auto problem = make_decay_problem();
    problem.variable_names = {"trace_inventory"};
    problem.initial_state = {1.0e-9};
    problem.variable_scales = {1.0e-9};
    problem.derivative_scales = {1.0e-8};
    problem.residual_scales = {1.0e-8};
    problem.lower_bounds = {0.0};
    problem.upper_bounds = {2.0e-9};
    problem.residual = [](
        double, const std::vector<double>& state,
        const std::vector<double>& derivative,
        std::vector<double>& residual) {
        residual[0] = derivative[0] + 10.0 * state[0];
        return thermox::EvaluationStatus::success();
    };
    problem.jacobian = [](
        double, const std::vector<double>&,
        const std::vector<double>&, double derivative_coefficient,
        thermox::Matrix& jacobian) {
        jacobian[0][0] = derivative_coefficient + 10.0;
        return thermox::EvaluationStatus::success();
    };
    thermox::TimeIntegrationOptions options;
    options.end_time = 1.0;
    options.initial_step = 1.0;
    options.max_step = 1.0;
    options.absolute_tolerance = 1.0e-4;
    options.relative_tolerance = 1.0e-6;
    const auto result = thermox::integrate_dae(problem, options);
    require(result.diagnostics.success, result.diagnostics.message);
    require(
        result.diagnostics.accepted_steps > 1,
        "a small physical state must not disappear under a scalar "
        "absolute tolerance");
    require_near(
        result.trajectory.back().state[0], 1.0e-9 * std::exp(-10.0),
        5.0e-13,
        "scale-aware error control resolves trace-inventory decay");
    require(
        result.diagnostics.maximum_accepted_error_norm <= 1.0 &&
            result.diagnostics.last_error_norm <= 1.0 &&
            result.diagnostics.maximum_error_ratio > 0.0 &&
            result.diagnostics.limiting_error_variable ==
                "trace_inventory" &&
            result.diagnostics
                    .maximum_absolute_normalized_residual <=
                options.nonlinear_options.residual_tolerance &&
            !result.diagnostics
                 .limiting_nonlinear_residual.empty(),
        "integration diagnostics identify the scale-limiting physical "
        "state, accepted normalized error, and limiting implicit "
        "constraint");
}

void test_variable_order_bdf2_improves_smooth_accuracy() {
    const auto problem = make_decay_problem();
    thermox::TimeIntegrationOptions first_order;
    first_order.end_time = 1.0;
    first_order.initial_step = 0.1;
    first_order.max_step = 0.1;
    first_order.absolute_tolerance = 1.0;
    first_order.relative_tolerance = 1.0;
    first_order.maximum_order = 1;
    const auto bdf1 = thermox::integrate_dae(problem, first_order);
    require(bdf1.diagnostics.success, bdf1.diagnostics.message);

    auto second_order = first_order;
    second_order.maximum_order = 2;
    const auto bdf2 = thermox::integrate_dae(problem, second_order);
    require(bdf2.diagnostics.success, bdf2.diagnostics.message);
    require(bdf1.diagnostics.maximum_order_used == 1,
            "BDF1-only execution reports order one");
    require(bdf2.diagnostics.maximum_order_used == 2,
            "variable-order execution advances to BDF2 after startup");
    const double exact = std::exp(-1.0);
    const double first_error = std::abs(
        bdf1.trajectory.back().state[0] - exact);
    const double second_error = std::abs(
        bdf2.trajectory.back().state[0] - exact);
    require(second_error < 0.5 * first_error,
            "BDF2 materially improves smooth transient accuracy: bdf1=" +
                std::to_string(first_error) + " bdf2=" +
                std::to_string(second_error));
}

void test_native_bdf_rejects_unsupported_order() {
    auto options = thermox::TimeIntegrationOptions{};
    options.maximum_order = 3;
    try {
        (void)thermox::integrate_dae(make_decay_problem(), options);
    } catch (const std::invalid_argument& error) {
        require(std::string(error.what()).find("maximum_order") !=
                    std::string::npos,
                "unsupported BDF order diagnostic names maximum_order");
        return;
    }
    throw std::runtime_error("native BDF must reject orders above two");
}

void test_adaptive_integration_honors_required_output_times() {
    auto options = thermox::TimeIntegrationOptions{};
    options.end_time = 1.0;
    options.initial_step = 0.23;
    options.max_step = 0.4;
    options.required_output_times = {0.17, 0.51, 0.93};

    const auto result = thermox::integrate_dae(
        make_decay_problem(), options);
    require(result.diagnostics.success, result.diagnostics.message);
    for (const double required_time : options.required_output_times) {
        const auto sample = std::find_if(
            result.trajectory.begin(), result.trajectory.end(),
            [&](const auto& candidate) {
                return candidate.time == required_time;
            });
        require(
            sample != result.trajectory.end(),
            "adaptive integration must emit the exact required output time " +
                std::to_string(required_time));
    }

    options.required_output_times = {0.5, 0.4};
    try {
        (void)thermox::integrate_dae(make_decay_problem(), options);
    } catch (const std::invalid_argument& error) {
        require(
            std::string(error.what()).find("strictly increasing") !=
                std::string::npos,
            "invalid output schedule diagnostic must explain ordering");
        return;
    }
    throw std::runtime_error(
        "adaptive integration must reject an unordered output schedule");
}

void test_adaptive_integration_honors_problem_time_breakpoints() {
    auto problem = make_decay_problem();
    problem.time_breakpoints = {0.37, 0.71};
    thermox::TimeIntegrationOptions options;
    options.end_time = 1.0;
    options.initial_step = 0.29;
    options.max_step = 0.4;

    const auto result = thermox::integrate_dae(problem, options);
    require(result.diagnostics.success, result.diagnostics.message);
    for (const double breakpoint : problem.time_breakpoints) {
        require(
            std::any_of(
                result.trajectory.begin(), result.trajectory.end(),
                [&](const auto& sample) {
                    return sample.time == breakpoint;
                }),
            "adaptive integration must land exactly on a problem-owned "
            "time breakpoint");
    }

    problem.time_breakpoints = {0.5, 0.4};
    try {
        (void)thermox::integrate_dae(problem, options);
    } catch (const std::invalid_argument& error) {
        require(
            std::string(error.what()).find("strictly increasing") !=
                std::string::npos,
            "invalid problem breakpoint diagnostic must explain ordering");
        return;
    }
    throw std::runtime_error(
        "adaptive integration must reject unordered problem breakpoints");
}

void test_discontinuity_preserves_state_and_reinitializes_algebraics() {
    thermox::DaeProblem problem;
    problem.variable_names = {"inventory", "command"};
    problem.residual_names = {"inventory_balance", "command_schedule"};
    problem.variable_kinds = {
        thermox::DaeVariableKind::differential,
        thermox::DaeVariableKind::algebraic};
    problem.initial_state = {0.0, 0.0};
    problem.initial_derivative = {0.0, 0.0};
    problem.variable_scales = {1.0, 1.0};
    problem.derivative_scales = {1.0, 1.0};
    problem.residual_scales = {1.0, 1.0};
    problem.residual = [](
        double time,
        const std::vector<double>& state,
        const std::vector<double>& derivative,
        std::vector<double>& residual) {
        const double command = time < 0.5 ? 0.0 : 1.0;
        residual[0] = derivative[0] - state[1];
        residual[1] = state[1] - command;
        return thermox::EvaluationStatus::success();
    };
    problem.jacobian = [](
        double,
        const std::vector<double>&,
        const std::vector<double>&,
        double derivative_coefficient,
        thermox::Matrix& jacobian) {
        jacobian[0][0] = derivative_coefficient;
        jacobian[0][1] = -1.0;
        jacobian[1][1] = 1.0;
        return thermox::EvaluationStatus::success();
    };
    problem.time_breakpoints = {0.5};
    problem.time_discontinuities = {0.5};
    problem.events.push_back(thermox::DaeEvent{
        "command_enabled",
        [](double, const std::vector<double>& state) {
            return state[1] - 0.5;
        },
        thermox::EventDirection::rising,
        false});

    thermox::TimeIntegrationOptions options;
    options.end_time = 1.0;
    options.initial_step = 0.3;
    options.max_step = 0.3;
    const auto result = thermox::integrate_dae(problem, options);
    require(result.diagnostics.success, result.diagnostics.message);
    const auto knot = std::find_if(
        result.trajectory.begin(), result.trajectory.end(),
        [](const auto& sample) {
            return sample.time == 0.5;
        });
    require(
        knot != result.trajectory.end(),
        "discontinuous DAE input must emit its exact knot");
    require_near(
        knot->state[0], 0.0, 1.0e-9,
        "differential state must not integrate the new command before "
        "its right-continuous knot");
    require_near(
        knot->state[1], 1.0, 1.0e-9,
        "algebraic state must be consistently reinitialized to the new "
        "command at the knot");
    require_near(
        result.trajectory.back().state[0], 0.5, 1.0e-6,
        "differential state must integrate the post-knot command only "
        "after the discontinuity");
    require(
        result.events.size() == 1,
        "an algebraic jump must produce exactly one state event");
    require_near(
        result.events.front().time, 0.5, 0.0,
        "an event caused by an algebraic jump must occur at the exact "
        "discontinuity time");
    require_near(
        result.events.front().state[1], 1.0, 1.0e-9,
        "a jump event must report the right-continuous post-jump state");

    problem.time_breakpoints.clear();
    try {
        (void)thermox::integrate_dae(problem, options);
    } catch (const std::invalid_argument& error) {
        require(
            std::string(error.what()).find("must also be a time breakpoint") !=
                std::string::npos,
            "discontinuity validation must require exact breakpoint "
            "ownership");
        return;
    }
    throw std::runtime_error(
        "DAE discontinuities without breakpoints must be rejected");
}

void test_index_one_dae_consistent_initialization_and_integration() {
    thermox::DaeProblem problem;
    problem.variable_names = {"inventory", "algebraic_flow"};
    problem.residual_names = {"inventory_balance", "flow_constraint"};
    problem.variable_kinds = {
        thermox::DaeVariableKind::differential,
        thermox::DaeVariableKind::algebraic};
    problem.initial_state = {0.2, 0.2};
    problem.initial_derivative = {0.0, 0.0};
    problem.variable_scales = {1.0, 1.0};
    problem.derivative_scales = {1.0, 1.0};
    problem.residual_scales = {1.0, 1.0};
    problem.residual = [](double,
                          const std::vector<double>& state,
                          const std::vector<double>& derivative,
                          std::vector<double>& residual) {
        residual[0] = derivative[0] + state[0] - state[1];
        residual[1] = state[0] + state[1] - 1.0;
        return thermox::EvaluationStatus::success();
    };
    problem.jacobian = [](double,
                          const std::vector<double>&,
                          const std::vector<double>&,
                          double derivative_coefficient,
                          thermox::Matrix& jacobian) {
        jacobian[0][0] = derivative_coefficient + 1.0;
        jacobian[0][1] = -1.0;
        jacobian[1][0] = 1.0;
        jacobian[1][1] = 1.0;
        return thermox::EvaluationStatus::success();
    };

    const auto initialized =
        thermox::make_consistent_initial_conditions(problem, 0.0);
    require(initialized.diagnostics.converged, initialized.diagnostics.message);
    require_near(initialized.state[0], 0.2, 1.0e-8, "differential initial state is retained");
    require_near(initialized.state[1], 0.8, 1.0e-7, "algebraic initial state is solved");
    require_near(initialized.derivative[0], 0.6, 1.0e-7,
                 "differential initial derivative is solved");

    thermox::TimeIntegrationOptions options;
    options.end_time = 0.5;
    options.initial_step = 0.1;
    options.absolute_tolerance = 1.0e-6;
    options.relative_tolerance = 1.0e-4;
    const auto result = thermox::integrate_dae(problem, options);
    require(result.diagnostics.success, result.diagnostics.message);
    const auto& final = result.trajectory.back().state;
    require_near(final[0] + final[1], 1.0, 1.0e-7,
                 "algebraic constraint remains satisfied during integration");
}

void test_adaptive_error_control_uses_differential_states_only() {
    thermox::DaeProblem problem;
    problem.variable_names = {"inventory", "oscillatory_readout"};
    problem.residual_names = {"inventory_decay", "readout_constraint"};
    problem.variable_kinds = {
        thermox::DaeVariableKind::differential,
        thermox::DaeVariableKind::algebraic};
    problem.initial_state = {1.0, std::sin(1000.0)};
    problem.initial_derivative = {-1.0, 0.0};
    problem.variable_scales = {1.0, 1.0};
    problem.derivative_scales = {1.0, 1.0};
    problem.residual_scales = {1.0, 1.0};
    problem.residual = [](
        double, const std::vector<double>& state,
        const std::vector<double>& derivative,
        std::vector<double>& residual) {
        residual[0] = derivative[0] + state[0];
        residual[1] = state[1] - std::sin(1000.0 * state[0]);
        return thermox::EvaluationStatus::success();
    };
    problem.jacobian = [](
        double, const std::vector<double>& state,
        const std::vector<double>&, double derivative_coefficient,
        thermox::Matrix& jacobian) {
        jacobian[0][0] = derivative_coefficient + 1.0;
        jacobian[1][0] = -1000.0 * std::cos(1000.0 * state[0]);
        jacobian[1][1] = 1.0;
        return thermox::EvaluationStatus::success();
    };

    thermox::TimeIntegrationOptions options;
    options.end_time = 0.5;
    options.initial_step = 0.1;
    options.max_step = 0.1;
    options.absolute_tolerance = 1.0e-6;
    options.relative_tolerance = 1.0e-4;
    const auto result = thermox::integrate_dae(problem, options);
    require(result.diagnostics.success, result.diagnostics.message);
    require(result.diagnostics.accepted_steps < 100,
            "algebraic readout must not drive adaptive step size");
    require(
        result.diagnostics.limiting_error_variable == "inventory",
        "local-error diagnostics must identify a differential state, "
        "not an algebraic readout");
    const auto& final = result.trajectory.back().state;
    require_near(final[0], std::exp(-0.5), 2.0e-3,
                 "differential state controls integration accuracy");
    require_near(final[1], std::sin(1000.0 * final[0]), 1.0e-7,
                 "algebraic constraint remains solved");
}

void test_terminal_event_stops_integration() {
    auto problem = make_decay_problem();
    problem.events.push_back(thermox::DaeEvent{
        "terminal_threshold",
        [](double, const std::vector<double>& state) { return state[0] - 0.75; },
        thermox::EventDirection::falling,
        true});
    thermox::TimeIntegrationOptions options;
    options.end_time = 2.0;
    options.initial_step = 0.1;
    const auto result = thermox::integrate_dae(problem, options);
    require(result.diagnostics.success, result.diagnostics.message);
    require(result.events.size() == 1 && result.events[0].terminal,
            "terminal event is reported");
    require(result.diagnostics.final_time < options.end_time,
            "terminal event stops before end_time");
}

void test_event_transition_reinitializes_and_restarts_integration() {
    const auto command = std::make_shared<double>(1.0);
    thermox::DaeProblem problem;
    problem.variable_names = {"inventory", "command"};
    problem.residual_names = {"inventory_balance", "command_mode"};
    problem.variable_kinds = {
        thermox::DaeVariableKind::differential,
        thermox::DaeVariableKind::algebraic};
    problem.initial_state = {0.0, 1.0};
    problem.initial_derivative = {1.0, 0.0};
    problem.variable_scales = {1.0, 1.0};
    problem.derivative_scales = {1.0, 1.0};
    problem.residual_scales = {1.0, 1.0};
    problem.residual = [command](
        double, const std::vector<double>& state,
        const std::vector<double>& derivative,
        std::vector<double>& residual) {
        residual[0] = derivative[0] - state[1];
        residual[1] = state[1] - *command;
        return thermox::EvaluationStatus::success();
    };
    problem.jacobian = [](
        double, const std::vector<double>&,
        const std::vector<double>&, double derivative_coefficient,
        thermox::Matrix& jacobian) {
        jacobian[0][0] = derivative_coefficient;
        jacobian[0][1] = -1.0;
        jacobian[1][1] = 1.0;
        return thermox::EvaluationStatus::success();
    };
    problem.reset_discrete_state = [command]() {
        *command = 1.0;
        return thermox::EvaluationStatus::success();
    };
    problem.events.push_back(thermox::DaeEvent{
        "reverse_command",
        [](double, const std::vector<double>& state) {
            return state[0] - 0.5;
        },
        thermox::EventDirection::rising,
        false,
        [command](double, std::vector<double>&,
                  std::vector<double>&) {
            *command = -1.0;
            return thermox::EvaluationStatus::success();
        }});

    thermox::TimeIntegrationOptions options;
    options.end_time = 1.0;
    options.initial_step = 0.12;
    options.max_step = 0.12;
    options.required_output_times = {0.6};
    const auto result = thermox::integrate_dae(problem, options);
    require(result.diagnostics.success, result.diagnostics.message);
    require(
        result.events.size() == 1U &&
            result.events.front().transitioned &&
            !result.events.front().terminal,
        "a nonterminal event transition must be reported exactly once");
    require_near(
        result.events.front().time, 0.5, 3.0e-2,
        "the transition must occur at the state threshold");
    require_near(
        result.events.front().state[1], -1.0, 1.0e-9,
        "event evidence must contain the consistently reinitialized "
        "post-transition algebraic state");
    require_near(
        result.trajectory.back().state[0], 0.0, 4.0e-2,
        "integration must continue under the transitioned residual mode");
    require_near(
        result.trajectory.back().state[1], -1.0, 1.0e-9,
        "the transitioned algebraic mode must remain active");
    require(
        std::any_of(
            result.trajectory.begin(), result.trajectory.end(),
            [](const auto& sample) { return sample.time == 0.6; }),
        "event-time rollback must preserve later required output times");
    const auto repeated = thermox::integrate_dae(problem, options);
    require(
        repeated.diagnostics.success &&
            repeated.events.size() == 1U,
        "a hybrid DAE problem must reset its discrete mode before a "
        "sequential repeat execution");
}

void test_event_priority_and_hysteresis_prevent_chatter() {
    thermox::DaeProblem problem;
    problem.variable_names = {"position"};
    problem.residual_names = {"constant_rate"};
    problem.variable_kinds = {
        thermox::DaeVariableKind::differential};
    problem.initial_state = {0.0};
    problem.initial_derivative = {1.0};
    problem.variable_scales = {1.0};
    problem.derivative_scales = {1.0};
    problem.residual_scales = {1.0};
    problem.residual = [](
        double, const std::vector<double>&,
        const std::vector<double>& derivative,
        std::vector<double>& residual) {
        residual[0] = derivative[0] - 1.0;
        return thermox::EvaluationStatus::success();
    };
    problem.jacobian = [](
        double, const std::vector<double>&,
        const std::vector<double>&, double derivative_coefficient,
        thermox::Matrix& jacobian) {
        jacobian[0][0] = derivative_coefficient;
        return thermox::EvaluationStatus::success();
    };
    const auto surface = [](
        double, const std::vector<double>& state) {
        return state[0] - 0.5;
    };
    problem.events.push_back(thermox::DaeEvent{
        "low_priority_reset", surface,
        thermox::EventDirection::rising, false,
        [](double, std::vector<double>& state,
           std::vector<double>&) {
            state[0] = 0.45;
            return thermox::EvaluationStatus::success();
        },
        1,
        0.02});
    problem.events.push_back(thermox::DaeEvent{
        "high_priority_reset", surface,
        thermox::EventDirection::rising, false,
        [](double, std::vector<double>& state,
           std::vector<double>&) {
            state[0] = 0.49;
            return thermox::EvaluationStatus::success();
        },
        10,
        0.02});

    thermox::TimeIntegrationOptions options;
    options.end_time = 0.7;
    options.initial_step = 0.05;
    options.max_step = 0.05;
    const auto result = thermox::integrate_dae(problem, options);
    require(result.diagnostics.success, result.diagnostics.message);
    require(
        result.events.size() == 2U,
        "simultaneous hysteretic events must fire once each without "
        "immediate reset chatter");
    require(
        result.events[0].priority == 1 &&
            result.events[1].priority == 10,
        "detected events must preserve their declared priorities");
    require_near(
        result.events[0].state[0], 0.49, 1.0e-9,
        "the highest-priority simultaneous transition must have final "
        "authority over the reinitialized state");
    require_near(
        result.trajectory.back().state[0], 0.69, 2.0e-3,
        "integration must continue once after the prioritized reset");
}

}  // namespace

int main() {
    try {
        test_dae_equation_system_builder();
        test_transient_solver_executes_independent_structural_blocks();
        test_dae_equation_system_builder_rejects_non_square_system();
        test_consistent_initial_conditions_for_ode();
        test_singular_initialization_names_unresolved_unknown();
        test_adaptive_dae_integration();
        test_adaptive_error_control_uses_physical_variable_scales();
        test_variable_order_bdf2_improves_smooth_accuracy();
        test_native_bdf_rejects_unsupported_order();
        test_adaptive_integration_honors_required_output_times();
        test_adaptive_integration_honors_problem_time_breakpoints();
        test_discontinuity_preserves_state_and_reinitializes_algebraics();
        test_index_one_dae_consistent_initialization_and_integration();
        test_adaptive_error_control_uses_differential_states_only();
        test_terminal_event_stops_integration();
        test_event_transition_reinitializes_and_restarts_integration();
        test_event_priority_and_hysteresis_prevent_chatter();
    } catch (const std::exception& ex) {
        std::cerr << "test failure: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "thermox_transient_tests passed\n";
    return 0;
}
