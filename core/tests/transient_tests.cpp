#include "thermox/transient_solver.hpp"

#include <cmath>
#include <exception>
#include <iostream>
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

void test_consistent_initial_conditions_for_ode() {
    const auto problem = make_decay_problem();
    const auto initialized =
        thermox::make_consistent_initial_conditions(problem, 0.0);
    require(initialized.diagnostics.converged, initialized.diagnostics.message);
    require_near(initialized.state[0], 1.0, 0.0, "ODE initial state remains fixed");
    require_near(initialized.derivative[0], -1.0, 1.0e-7,
                 "ODE consistent initial derivative");
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

}  // namespace

int main() {
    try {
        test_consistent_initial_conditions_for_ode();
        test_adaptive_dae_integration();
        test_index_one_dae_consistent_initialization_and_integration();
        test_terminal_event_stops_integration();
    } catch (const std::exception& ex) {
        std::cerr << "test failure: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "thermox_transient_tests passed\n";
    return 0;
}
