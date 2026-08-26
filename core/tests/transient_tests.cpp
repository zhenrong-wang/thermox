#include "thermox/dae_equation_system.hpp"
#include "thermox/dae_linearization.hpp"
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

void test_dae_jacobian_verification_checks_both_channels() {
    thermox::DaeEquationSystemBuilder system;
    const auto x = system.add_variable(
        "x", thermox::DaeVariableKind::differential,
        2.0, -4.0, 1.0, 1.0);
    system.add_sparse_equation(
        "decay",
        {x},
        [x](double,
            const std::vector<double>& state,
            const std::vector<double>& derivative,
            double& residual,
            std::vector<thermox::DaeEquationPartial>& partials) {
            residual = derivative[x] + state[x] * state[x];
            partials.push_back({x, 2.0 * state[x], 1.0});
            return thermox::EvaluationStatus::success();
        });

    const auto report = thermox::verify_dae_problem_jacobian(
        system.build(), 0.0);
    require(
        report.analytic_derivatives_available && report.passed &&
            report.state_jacobian.passed &&
            report.derivative_jacobian.passed,
        "DAE verification checks state and state-rate derivatives");
    require(
        report.state_jacobian.compared_entries == 1 &&
            report.derivative_jacobian.compared_entries == 1,
        "DAE verification reports both derivative channels");

    auto bad = system.build();
    bad.jacobian = [](double,
                      const std::vector<double>&,
                      const std::vector<double>&,
                      double coefficient,
                      thermox::Matrix& jacobian) {
        jacobian[0][0] = 3.0 + 2.0 * coefficient;
        return thermox::EvaluationStatus::success();
    };
    bad.sparse_jacobian_pattern.reset();
    bad.sparse_jacobian_values = {};
    bad.sparse_jacobian_values_subset = {};
    bad.sparse_jacobian = {};
    const auto bad_report = thermox::verify_dae_problem_jacobian(
        bad, 0.0);
    require(
        !bad_report.passed &&
            bad_report.state_jacobian.mismatch_count == 1 &&
            bad_report.derivative_jacobian.mismatch_count == 1,
        "DAE verification names incorrect state and state-rate derivatives");
    require(
        bad_report.derivative_jacobian.mismatches.at(0).variable_name ==
            "d(x)/dt",
        "DAE state-rate mismatch retains derivative identity");
}

void test_dae_jacobian_verification_respects_state_domain() {
    thermox::DaeProblem problem;
    problem.variable_names = {"nonnegative_x"};
    problem.residual_names = {"square_balance"};
    problem.variable_kinds = {
        thermox::DaeVariableKind::differential};
    problem.initial_state = {0.0};
    problem.initial_derivative = {0.0};
    problem.variable_scales = {1.0};
    problem.derivative_scales = {1.0};
    problem.residual_scales = {1.0};
    problem.lower_bounds = {0.0};
    problem.upper_bounds = {1.0};
    problem.residual = [](
        double,
        const std::vector<double>& state,
        const std::vector<double>& derivative,
        std::vector<double>& residual) {
        if (state[0] < 0.0) {
            return thermox::EvaluationStatus::recoverable(
                "negative state is outside the physical domain");
        }
        residual[0] = state[0] * state[0] + derivative[0];
        return thermox::EvaluationStatus::success();
    };
    problem.jacobian = [](
        double,
        const std::vector<double>& state,
        const std::vector<double>&,
        double coefficient,
        thermox::Matrix& jacobian) {
        jacobian[0][0] = 2.0 * state[0] + coefficient;
        return thermox::EvaluationStatus::success();
    };
    thermox::JacobianVerificationOptions options;
    options.finite_difference_epsilon = 1.0e-4;
    options.absolute_tolerance = 1.1e-4;
    options.relative_tolerance = 0.0;
    const auto report = thermox::verify_dae_problem_jacobian(
        problem, 0.0, {}, {}, options);
    require(
        report.passed,
        "DAE verification uses a valid one-sided state perturbation at a bound");
}

void test_mixed_dae_derivatives_are_preserved_and_solved() {
    thermox::DaeEquationSystemBuilder system;
    const auto x = system.add_variable(
        "x", thermox::DaeVariableKind::differential,
        1.0, 0.0, 1.0, 1.0);
    const auto z = system.add_variable(
        "z", thermox::DaeVariableKind::algebraic,
        0.0, 0.0, 1.0, 1.0);
    system.add_linear_equation(
        "analytic_dynamics",
        {{x, 1.0, 1.0}, {z, -1.0, 0.0}}, 0.0);
    system.add_checked_equation(
        "numeric_constraint",
        [x, z](double,
               const std::vector<double>& state,
               const std::vector<double>&,
               double& residual) {
            residual = state[z] - 2.0 * state[x];
            return thermox::EvaluationStatus::success();
        });
    const auto problem = system.build();
    require(
        static_cast<bool>(problem.partial_sparse_jacobian) &&
            problem.analytic_jacobian_rows ==
                std::vector<bool>({true, false}) &&
            !problem.sparse_jacobian_pattern.has_value(),
        "mixed DAE retains provider-owned rows without claiming a "
        "complete analytic Jacobian");

    const auto initialized =
        thermox::make_consistent_initial_conditions(problem, 0.0);
    require(
        initialized.diagnostics.converged,
        initialized.diagnostics.message);
    require_near(
        initialized.state[z], 2.0, 1.0e-9,
        "mixed DAE initialization solves its numerical row");
    require_near(
        initialized.derivative[x], 1.0, 1.0e-9,
        "mixed DAE initialization uses its analytic dynamic row");
    const auto report = thermox::verify_dae_problem_jacobian(
        problem, 0.0, initialized.state, initialized.derivative);
    require(
        report.passed && report.state_jacobian.compared_rows == 1 &&
            report.derivative_jacobian.compared_rows == 1 &&
            report.state_jacobian.compared_entries == 2,
        "mixed DAE verification attributes only provider-owned rows");
    thermox::TimeIntegrationOptions options;
    options.end_time = 0.1;
    options.initial_step = 0.1;
    options.max_step = 0.1;
    const auto integrated = thermox::integrate_dae(problem, options);
    require(
        integrated.diagnostics.success &&
            integrated.trajectory.back().state[x] > 1.0 &&
            integrated.trajectory.back().state[z] > 2.0,
        "mixed DAE implicit integration combines analytic and numerical rows");
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
        [](double, const std::vector<double>& state, double& value) {
            value = state[0] - 0.5;
            return thermox::EvaluationStatus::success();
        },
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
        [](double, const std::vector<double>& state, double& value) {
            value = state[1] - 0.5;
            return thermox::EvaluationStatus::success();
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
        knot != result.trajectory.end() &&
            knot->state_before_discontinuity.size() == 2U &&
            knot->derivative_before_discontinuity.size() == 2U,
        "discontinuous DAE input must emit both limits at its exact knot");
    require_near(
        knot->state_before_discontinuity[1], 0.0, 1.0e-9,
        "discontinuity evidence must retain the left-limit algebraic "
        "state");
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
        [](double, const std::vector<double>& state, double& value) {
            value = state[0] - 0.75;
            return thermox::EvaluationStatus::success();
        },
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
        [](double, const std::vector<double>& state, double& value) {
            value = state[0] - 0.5;
            return thermox::EvaluationStatus::success();
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
    const auto transition_sample = std::find_if(
        result.trajectory.begin(), result.trajectory.end(),
        [&](const auto& sample) {
            return sample.time == result.events.front().time;
        });
    require(
        transition_sample != result.trajectory.end() &&
            transition_sample->state_before_discontinuity.size() == 2U,
        "event transition samples must retain their left-limit state");
    require_near(
        transition_sample->state_before_discontinuity[1], 1.0, 1.0e-9,
        "event transition evidence must retain the pre-transition "
        "algebraic mode");
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
        double, const std::vector<double>& state, double& value) {
        value = state[0] - 0.5;
        return thermox::EvaluationStatus::success();
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

void test_checked_event_surface_failure_is_reported() {
    auto problem = make_decay_problem();
    problem.events.push_back(thermox::DaeEvent{
        "invalid_surface",
        [](double, const std::vector<double>&, double&) {
            return thermox::EvaluationStatus::recoverable(
                "property state is outside its valid range");
        },
        thermox::EventDirection::any,
        false});
    thermox::TimeIntegrationOptions options;
    options.end_time = 0.1;
    const auto result = thermox::integrate_dae(problem, options);
    require(
        !result.diagnostics.success &&
            result.diagnostics.message.find(
                "initial event evaluation failed") !=
                std::string::npos &&
            result.diagnostics.message.find(
                "outside its valid range") != std::string::npos,
        "checked event-surface failures must be explicit diagnostics");
}

void test_index_one_dae_small_signal_linearization() {
    thermox::DaeEquationSystemBuilder builder;
    const auto x = builder.add_variable(
        "x", thermox::DaeVariableKind::differential,
        1.0, 9.0, 1.0, 1.0);
    const auto z = builder.add_variable(
        "z", thermox::DaeVariableKind::algebraic,
        11.0, 0.0, 1.0, 1.0);
    const auto u = builder.add_variable(
        "u", thermox::DaeVariableKind::algebraic,
        2.0, 0.0, 1.0, 1.0);
    builder.add_linear_equation(
        "dynamics",
        {{x, 2.0, 1.0}, {z, -1.0, 0.0}}, 0.0);
    builder.add_linear_equation(
        "algebraic",
        {{z, 1.0, 0.0}, {x, -3.0, 0.0},
         {u, -4.0, 0.0}},
        0.0);
    const auto fixed_input = builder.add_linear_equation(
        "fixed.u", {{u, 1.0, 0.0}}, 2.0);
    const auto problem = builder.build();
    const auto initialized =
        thermox::make_consistent_initial_conditions(problem, 0.0);
    require(
        initialized.diagnostics.converged,
        initialized.diagnostics.message);

    thermox::DaeLinearizationOptions options;
    options.relative_perturbation = 1.0e-4;
    const std::vector<thermox::DaeLinearizationInput> inputs{
        {u, fixed_input, "command"}};
    const std::vector<thermox::DaeLinearizationOutput> outputs{
        {x, "state"}, {z, "algebraic"}, {u, "command_echo"}};
    const auto result = thermox::linearize_index1_dae(
        problem, 0.0, initialized.state, initialized.derivative,
        inputs, outputs, options);
    require(result.diagnostics.success, result.diagnostics.message);
    require(
        result.differential_state_names ==
            std::vector<std::string>{"x"} &&
            result.input_names ==
                std::vector<std::string>{"command"} &&
            result.output_names ==
                std::vector<std::string>{
                    "state", "algebraic", "command_echo"},
        "DAE linearization preserves state, input, and output identity");
    require_near(
        result.operating_derivative[x], 9.0, 1.0e-10,
        "nominal DAE response rate");
    require_near(
        result.A.at(0).at(0), 1.0, 1.0e-8,
        "DAE state matrix");
    require_near(
        result.B.at(0).at(0), 4.0, 1.0e-8,
        "DAE input matrix");
    require_near(result.C.at(0).at(0), 1.0, 1.0e-12,
                 "differential-state output C row");
    require_near(result.D.at(0).at(0), 0.0, 1.0e-12,
                 "differential-state output has no direct feedthrough");
    require_near(result.C.at(1).at(0), 3.0, 1.0e-8,
                 "algebraic output state sensitivity");
    require_near(result.D.at(1).at(0), 4.0, 1.0e-8,
                 "algebraic output input sensitivity");
    require_near(result.C.at(2).at(0), 0.0, 1.0e-12,
                 "released-input output has no state sensitivity");
    require_near(result.D.at(2).at(0), 1.0, 1.0e-12,
                 "released-input output direct feedthrough");
    require(
        result.diagnostics.residual_evaluations == 9 &&
            result.diagnostics.linear_right_hand_sides == 2,
        "tangent linearization reports its residual evaluations and "
        "sensitivity right-hand sides");
    const auto probe =
        thermox::validate_index1_dae_linearization_response(
            problem, 0.0, result, inputs, {0.01}, {0.02});
    require(
        probe.success && probe.passed && probe.states.size() == 1 &&
            probe.outputs.size() == 3,
        probe.message);
    require_near(
        probe.states[0].predicted_rate_change, 0.09, 1.0e-10,
        "linear response probe prediction");
    require_near(
        probe.states[0].nonlinear_rate_change, 0.09, 1.0e-9,
        "consistent nonlinear DAE response");
    require_near(probe.outputs[0].predicted_change, 0.01, 1.0e-12,
                 "state-output linear response");
    require_near(probe.outputs[1].predicted_change, 0.11, 1.0e-10,
                 "algebraic-output linear response");
    require_near(probe.outputs[1].nonlinear_change, 0.11, 1.0e-9,
                 "algebraic-output nonlinear response");
    require_near(probe.outputs[2].predicted_change, 0.02, 1.0e-12,
                 "direct-feedthrough linear response");
    auto incorrect = result;
    incorrect.A[0][0] = 2.0;
    const auto rejected_probe =
        thermox::validate_index1_dae_linearization_response(
            problem, 0.0, incorrect, inputs, {0.01}, {0.0});
    require(
        rejected_probe.success && !rejected_probe.passed &&
            rejected_probe.states[0].relative_error > 0.4,
        "nonlinear response probe rejects an incorrect A column");
    thermox::DaeLinearizationTrajectoryProbeOptions trajectory_options;
    trajectory_options.duration = 0.1;
    trajectory_options.sample_count = 2;
    trajectory_options.nonlinear_integration.initial_step = 0.01;
    trajectory_options.nonlinear_integration.max_step = 0.05;
    const auto trajectory =
        thermox::validate_index1_dae_linearization_trajectory(
            problem, 0.0, result, inputs,
            {0.01}, {0.02}, trajectory_options);
    require(
        trajectory.success && trajectory.passed &&
            trajectory.samples.size() == 2 &&
            trajectory.samples[0].outputs.size() == 3,
        trajectory.message + " max_normalized_error=" +
            std::to_string(
                trajectory.maximum_normalized_absolute_error) +
            " max_relative_error=" +
            std::to_string(trajectory.maximum_relative_error) +
            (trajectory.samples.empty()
                 ? std::string{}
                 : " linear=" + std::to_string(
                       trajectory.samples[0].states[0].linear_change) +
                       " nonlinear=" + std::to_string(
                       trajectory.samples[0].states[0].nonlinear_change)));
    require(
        trajectory.maximum_normalized_absolute_error < 3.0e-6,
        "integrated linear state and amplified algebraic-output "
        "trajectories agree for a linear DAE: maximum normalized error=" +
            std::to_string(
                trajectory.maximum_normalized_absolute_error));
    const auto rejected_trajectory =
        thermox::validate_index1_dae_linearization_trajectory(
            problem, 0.0, incorrect, inputs,
            {0.01}, {0.0}, trajectory_options);
    require(
        rejected_trajectory.success && !rejected_trajectory.passed,
        "trajectory probe rejects an incorrect A matrix");
}

}  // namespace

int main() {
    try {
        test_dae_equation_system_builder();
        test_dae_jacobian_verification_checks_both_channels();
        test_dae_jacobian_verification_respects_state_domain();
        test_mixed_dae_derivatives_are_preserved_and_solved();
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
        test_checked_event_surface_failure_is_reported();
        test_index_one_dae_small_signal_linearization();
    } catch (const std::exception& ex) {
        std::cerr << "test failure: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "thermox_transient_tests passed\n";
    return 0;
}
