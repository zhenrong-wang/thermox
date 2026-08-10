#include "thermox/transient_solver.hpp"
#include "thermox/sparse_linear_solver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace thermox {

namespace {

bool positive_finite(double value) {
    return std::isfinite(value) && value > 0.0;
}

std::vector<double> default_values(const std::vector<double>& supplied,
                                   std::size_t size,
                                   double value,
                                   const std::string& name) {
    if (!supplied.empty() && supplied.size() != size) {
        throw std::invalid_argument(name + " size does not match DAE state size");
    }
    return supplied.empty() ? std::vector<double>(size, value) : supplied;
}

void validate_dae_problem(const DaeProblem& problem) {
    const std::size_t size = problem.initial_state.size();
    if (size == 0) {
        throw std::invalid_argument("DAE problem must have at least one state variable");
    }
    if (problem.variable_names.size() != size || problem.residual_names.size() != size) {
        throw std::invalid_argument("DAE problem must be square with one name per state");
    }
    if (!problem.variable_kinds.empty() && problem.variable_kinds.size() != size) {
        throw std::invalid_argument("DAE variable_kinds size does not match state size");
    }
    if (!problem.initial_derivative.empty() && problem.initial_derivative.size() != size) {
        throw std::invalid_argument("DAE initial_derivative size does not match state size");
    }
    if (!problem.residual) {
        throw std::invalid_argument("DAE residual callback is empty");
    }
    for (const double value : problem.initial_state) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("DAE initial state must be finite");
        }
    }
    for (const double value : problem.initial_derivative) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("DAE initial derivative must be finite");
        }
    }
    const auto validate_positive_vector = [size](const std::vector<double>& values,
                                                 const std::string& name) {
        if (!values.empty() && values.size() != size) {
            throw std::invalid_argument(name + " size does not match DAE state size");
        }
        for (const double value : values) {
            if (!positive_finite(value)) {
                throw std::invalid_argument(name + " values must be positive and finite");
            }
        }
    };
    validate_positive_vector(problem.variable_scales, "variable_scales");
    validate_positive_vector(problem.derivative_scales, "derivative_scales");
    validate_positive_vector(problem.residual_scales, "residual_scales");
    if ((!problem.lower_bounds.empty() && problem.lower_bounds.size() != size) ||
        (!problem.upper_bounds.empty() && problem.upper_bounds.size() != size)) {
        throw std::invalid_argument("DAE bound vector size does not match state size");
    }
    for (std::size_t i = 0; i < size; ++i) {
        const double lower = problem.lower_bounds.empty()
                                 ? -std::numeric_limits<double>::infinity()
                                 : problem.lower_bounds[i];
        const double upper = problem.upper_bounds.empty()
                                 ? std::numeric_limits<double>::infinity()
                                 : problem.upper_bounds[i];
        if (std::isnan(lower) || std::isnan(upper) || lower > upper) {
            throw std::invalid_argument("DAE variable bounds are invalid");
        }
        if (problem.initial_state[i] < lower || problem.initial_state[i] > upper) {
            throw std::invalid_argument("DAE initial state is outside variable bounds");
        }
    }
    if (problem.sparse_jacobian_pattern.has_value()) {
        if (problem.sparse_jacobian_pattern->rows() != size ||
            problem.sparse_jacobian_pattern->columns() != size ||
            !problem.sparse_jacobian_values) {
            throw std::invalid_argument(
                "DAE fixed sparse Jacobian pattern/value contract is invalid");
        }
    } else if (problem.sparse_jacobian_values) {
        throw std::invalid_argument(
            "DAE sparse Jacobian values require a fixed sparse pattern");
    }
    for (const auto& event : problem.events) {
        if (event.name.empty() || !event.evaluate) {
            throw std::invalid_argument("DAE events require a name and evaluation callback");
        }
    }
}

void validate_integration_options(const TimeIntegrationOptions& options) {
    if (!std::isfinite(options.start_time) || !std::isfinite(options.end_time) ||
        options.end_time <= options.start_time) {
        throw std::invalid_argument("end_time must be finite and greater than start_time");
    }
    if (!positive_finite(options.initial_step) || !positive_finite(options.min_step) ||
        !positive_finite(options.max_step) || options.min_step > options.max_step) {
        throw std::invalid_argument("time steps must be positive with min_step <= max_step");
    }
    if (!positive_finite(options.absolute_tolerance) ||
        !positive_finite(options.relative_tolerance)) {
        throw std::invalid_argument("time integration tolerances must be positive and finite");
    }
    if (options.max_steps <= 0 || options.max_consecutive_rejections <= 0) {
        throw std::invalid_argument("time integration step limits must be positive");
    }
    if (options.maximum_order < 1 || options.maximum_order > 2) {
        throw std::invalid_argument(
            "native BDF maximum_order must be 1 or 2");
    }
}

struct ImplicitStepResult {
    bool success{false};
    std::vector<double> state;
    std::vector<double> derivative;
    SolverDiagnostics diagnostics;
};

ImplicitStepResult solve_implicit_bdf_step(
    const DaeProblem& problem,
    double next_time,
    const std::vector<double>& predicted_state,
    double derivative_coefficient,
    const std::vector<double>& derivative_offset,
    const std::vector<double>& variable_scales,
    const std::vector<double>& residual_scales,
    const std::vector<double>& lower_bounds,
    const std::vector<double>& upper_bounds,
    const SolverOptions& options) {
    const std::size_t size = predicted_state.size();
    NonlinearProblem nonlinear;
    nonlinear.variable_names = problem.variable_names;
    nonlinear.residual_names = problem.residual_names;
    nonlinear.initial_guess.resize(size);
    for (std::size_t i = 0; i < size; ++i) {
        nonlinear.initial_guess[i] = std::clamp(
            predicted_state[i], lower_bounds[i], upper_bounds[i]);
    }
    nonlinear.variable_scales = variable_scales;
    nonlinear.residual_scales = residual_scales;
    nonlinear.lower_bounds = lower_bounds;
    nonlinear.upper_bounds = upper_bounds;
    nonlinear.checked_residual =
        [&problem, next_time, derivative_coefficient,
         derivative_offset](const std::vector<double>& state,
                            std::vector<double>& residual) {
            std::vector<double> derivative(state.size(), 0.0);
            for (std::size_t i = 0; i < state.size(); ++i) {
                derivative[i] = derivative_coefficient * state[i] +
                                derivative_offset[i];
            }
            return problem.residual(next_time, state, derivative, residual);
        };

    if (problem.sparse_jacobian_pattern.has_value()) {
        nonlinear.sparse_jacobian_pattern = problem.sparse_jacobian_pattern;
        nonlinear.sparse_jacobian_values =
            [&problem, next_time, derivative_coefficient,
             derivative_offset](
                const std::vector<double>& state,
                std::vector<double>& values) {
                std::vector<double> derivative(state.size(), 0.0);
                for (std::size_t i = 0; i < state.size(); ++i) {
                    derivative[i] = derivative_coefficient * state[i] +
                                    derivative_offset[i];
                }
                const EvaluationStatus status =
                    problem.sparse_jacobian_values(next_time, state, derivative,
                                                   derivative_coefficient,
                                                   values);
                if (!status.ok()) {
                    throw std::runtime_error(
                        status.message.empty()
                            ? "DAE sparse Jacobian value evaluation failed"
                            : status.message);
                }
            };
    } else if (problem.sparse_jacobian) {
        nonlinear.sparse_jacobian =
            [&problem, next_time, derivative_coefficient,
             derivative_offset](
                const std::vector<double>& state,
                std::vector<SparseTriplet>& jacobian) {
                std::vector<double> derivative(state.size(), 0.0);
                for (std::size_t i = 0; i < state.size(); ++i) {
                    derivative[i] = derivative_coefficient * state[i] +
                                    derivative_offset[i];
                }
                const EvaluationStatus status = problem.sparse_jacobian(
                    next_time, state, derivative,
                    derivative_coefficient, jacobian);
                if (!status.ok()) {
                    throw std::runtime_error(status.message.empty()
                                                 ? "DAE sparse Jacobian evaluation failed"
                                                 : status.message);
                }
            };
    } else if (problem.jacobian) {
        nonlinear.jacobian =
            [&problem, next_time, derivative_coefficient,
             derivative_offset](const std::vector<double>& state,
                                Matrix& jacobian) {
                std::vector<double> derivative(state.size(), 0.0);
                for (std::size_t i = 0; i < state.size(); ++i) {
                    derivative[i] = derivative_coefficient * state[i] +
                                    derivative_offset[i];
                }
                const EvaluationStatus status =
                    problem.jacobian(next_time, state, derivative,
                                     derivative_coefficient, jacobian);
                if (!status.ok()) {
                    throw std::runtime_error(status.message.empty()
                                                 ? "DAE Jacobian evaluation failed"
                                                 : status.message);
                }
            };
    }

    NonlinearSolveResult solve = solve_newton(nonlinear, options);
    ImplicitStepResult result;
    result.success = solve.diagnostics.converged;
    result.state = std::move(solve.x);
    result.diagnostics = std::move(solve.diagnostics);
    result.derivative.resize(size, 0.0);
    for (std::size_t i = 0; i < size; ++i) {
        result.derivative[i] =
            derivative_coefficient * result.state[i] +
            derivative_offset[i];
    }
    return result;
}

ImplicitStepResult solve_backward_euler_step(
    const DaeProblem& problem,
    double next_time,
    double step,
    const std::vector<double>& previous_state,
    const std::vector<double>& previous_derivative,
    const std::vector<double>& variable_scales,
    const std::vector<double>& residual_scales,
    const std::vector<double>& lower_bounds,
    const std::vector<double>& upper_bounds,
    const SolverOptions& options) {
    std::vector<double> predicted(previous_state.size());
    std::vector<double> offset(previous_state.size());
    for (std::size_t i = 0; i < previous_state.size(); ++i) {
        predicted[i] = previous_state[i] + step * previous_derivative[i];
        offset[i] = -previous_state[i] / step;
    }
    return solve_implicit_bdf_step(
        problem, next_time, predicted, 1.0 / step, offset,
        variable_scales, residual_scales, lower_bounds,
        upper_bounds, options);
}

ImplicitStepResult solve_bdf2_step(
    const DaeProblem& problem,
    double next_time,
    double step,
    const std::vector<double>& current_state,
    const std::vector<double>& older_state,
    double previous_step,
    const std::vector<double>& variable_scales,
    const std::vector<double>& residual_scales,
    const std::vector<double>& lower_bounds,
    const std::vector<double>& upper_bounds,
    const SolverOptions& options) {
    const double ratio = step / previous_step;
    const double derivative_coefficient =
        (1.0 + 2.0 * ratio) / (step * (1.0 + ratio));
    const double current_coefficient = -(1.0 + ratio) / step;
    const double older_coefficient =
        ratio * ratio / (step * (1.0 + ratio));
    std::vector<double> predicted(current_state.size());
    std::vector<double> offset(current_state.size());
    for (std::size_t i = 0; i < current_state.size(); ++i) {
        predicted[i] = current_state[i] +
            ratio * (current_state[i] - older_state[i]);
        offset[i] = current_coefficient * current_state[i] +
                    older_coefficient * older_state[i];
    }
    return solve_implicit_bdf_step(
        problem, next_time, predicted, derivative_coefficient,
        offset, variable_scales, residual_scales, lower_bounds,
        upper_bounds, options);
}

struct IntegrationErrorEstimate {
    double norm{0.0};
    double maximum_ratio{0.0};
    std::size_t limiting_variable{0U};
    bool has_controlled_variable{false};
};

IntegrationErrorEstimate integration_error_estimate(
    const std::vector<double>& coarse,
    const std::vector<double>& refined,
    const std::vector<double>& previous,
    const std::vector<double>& variable_scales,
    const std::vector<DaeVariableKind>& kinds,
    double absolute_tolerance,
    double relative_tolerance) {
    long double sum = 0.0;
    std::size_t controlled_variables = 0;
    IntegrationErrorEstimate estimate;
    for (std::size_t i = 0; i < coarse.size(); ++i) {
        if (!kinds.empty() &&
            kinds[i] != DaeVariableKind::differential) {
            continue;
        }
        const double scale =
            absolute_tolerance * variable_scales[i] +
            relative_tolerance *
                std::max({std::abs(previous[i]), std::abs(coarse[i]), std::abs(refined[i])});
        const long double error = static_cast<long double>(
            refined[i] - coarse[i]) / static_cast<long double>(scale);
        sum += error * error;
        const double ratio = std::abs(static_cast<double>(error));
        if (!estimate.has_controlled_variable ||
            ratio > estimate.maximum_ratio) {
            estimate.maximum_ratio = ratio;
            estimate.limiting_variable = i;
        }
        estimate.has_controlled_variable = true;
        ++controlled_variables;
    }
    if (controlled_variables != 0U) {
        estimate.norm = std::sqrt(static_cast<double>(
            sum / static_cast<long double>(controlled_variables)));
    }
    return estimate;
}

bool event_crossed(double before, double after, EventDirection direction) {
    if (!std::isfinite(before) || !std::isfinite(after)) {
        return false;
    }
    if (before == 0.0 && after == 0.0) {
        return false;
    }
    switch (direction) {
        case EventDirection::rising:
            return before < 0.0 && after >= 0.0;
        case EventDirection::falling:
            return before > 0.0 && after <= 0.0;
        case EventDirection::any:
            return (before < 0.0 && after >= 0.0) ||
                   (before > 0.0 && after <= 0.0);
    }
    return false;
}

}  // namespace

DaeInitializationResult make_consistent_initial_conditions(
    const DaeProblem& problem,
    double initial_time,
    const SolverOptions& options) {
    validate_dae_problem(problem);
    if (!std::isfinite(initial_time)) {
        throw std::invalid_argument("DAE initial time must be finite");
    }

    const std::size_t size = problem.initial_state.size();
    const auto kinds = problem.variable_kinds.empty()
                           ? std::vector<DaeVariableKind>(size, DaeVariableKind::differential)
                           : problem.variable_kinds;
    const auto state_scales = default_values(problem.variable_scales, size, 1.0,
                                             "variable_scales");
    const auto derivative_scales = problem.derivative_scales.empty()
                                       ? state_scales
                                       : default_values(problem.derivative_scales, size, 1.0,
                                                        "derivative_scales");
    const auto residual_scales = default_values(problem.residual_scales, size, 1.0,
                                                "residual_scales");
    const auto lower_bounds =
        default_values(problem.lower_bounds, size,
                       -std::numeric_limits<double>::infinity(), "lower_bounds");
    const auto upper_bounds =
        default_values(problem.upper_bounds, size,
                       std::numeric_limits<double>::infinity(), "upper_bounds");
    const auto initial_derivative =
        default_values(problem.initial_derivative, size, 0.0, "initial_derivative");

    NonlinearProblem initialization;
    initialization.variable_names.reserve(size);
    initialization.residual_names = problem.residual_names;
    initialization.initial_guess.resize(size);
    initialization.variable_scales.resize(size);
    initialization.lower_bounds.resize(size);
    initialization.upper_bounds.resize(size);
    initialization.residual_scales = residual_scales;
    for (std::size_t i = 0; i < size; ++i) {
        if (kinds[i] == DaeVariableKind::differential) {
            initialization.variable_names.push_back("derivative(" + problem.variable_names[i] + ")");
            initialization.initial_guess[i] = initial_derivative[i];
            initialization.variable_scales[i] = derivative_scales[i];
            initialization.lower_bounds[i] = -std::numeric_limits<double>::infinity();
            initialization.upper_bounds[i] = std::numeric_limits<double>::infinity();
        } else {
            initialization.variable_names.push_back(problem.variable_names[i]);
            initialization.initial_guess[i] = problem.initial_state[i];
            initialization.variable_scales[i] = state_scales[i];
            initialization.lower_bounds[i] = lower_bounds[i];
            initialization.upper_bounds[i] = upper_bounds[i];
        }
    }
    initialization.checked_residual =
        [&problem, &kinds, initial_time, initial_derivative](
            const std::vector<double>& unknowns,
            std::vector<double>& residual) {
            std::vector<double> state = problem.initial_state;
            std::vector<double> derivative = initial_derivative;
            for (std::size_t i = 0; i < unknowns.size(); ++i) {
                if (kinds[i] == DaeVariableKind::differential) {
                    derivative[i] = unknowns[i];
                } else {
                    state[i] = unknowns[i];
                }
            }
            return problem.residual(initial_time, state, derivative, residual);
        };
    if (problem.sparse_jacobian_pattern.has_value()) {
        initialization.sparse_jacobian_pattern =
            problem.sparse_jacobian_pattern;
        initialization.sparse_jacobian_values =
            [&problem, &kinds, initial_time, initial_derivative](
                const std::vector<double>& unknowns,
                std::vector<double>& values) {
                std::vector<double> state = problem.initial_state;
                std::vector<double> derivative =
                    initial_derivative;
                for (std::size_t i = 0;
                     i < unknowns.size(); ++i) {
                    if (kinds[i] ==
                        DaeVariableKind::differential) {
                        derivative[i] = unknowns[i];
                    } else {
                        state[i] = unknowns[i];
                    }
                }
                std::vector<double> state_partials(
                    values.size(), 0.0);
                std::vector<double> combined_partials(
                    values.size(), 0.0);
                auto status = problem.sparse_jacobian_values(
                    initial_time, state, derivative, 0.0,
                    state_partials);
                if (!status.ok()) {
                    throw std::runtime_error(
                        status.message.empty()
                        ? "DAE initialization state Jacobian "
                          "evaluation failed"
                        : status.message);
                }
                status = problem.sparse_jacobian_values(
                    initial_time, state, derivative, 1.0,
                    combined_partials);
                if (!status.ok()) {
                    throw std::runtime_error(
                        status.message.empty()
                        ? "DAE initialization derivative Jacobian "
                          "evaluation failed"
                        : status.message);
                }
                const auto& pattern =
                    *problem.sparse_jacobian_pattern;
                for (std::size_t row = 0;
                     row < pattern.rows(); ++row) {
                    for (std::size_t offset =
                             pattern.row_offsets()[row];
                         offset <
                             pattern.row_offsets()[row + 1];
                         ++offset) {
                        const std::size_t column =
                            pattern.column_indices()[offset];
                        values[offset] =
                            kinds[column] ==
                                DaeVariableKind::differential
                            ? combined_partials[offset] -
                                  state_partials[offset]
                            : state_partials[offset];
                    }
                }
            };
    } else if (problem.jacobian) {
        initialization.jacobian =
            [&problem, &kinds, initial_time, initial_derivative](
                const std::vector<double>& unknowns,
                Matrix& jacobian) {
                std::vector<double> state = problem.initial_state;
                std::vector<double> derivative =
                    initial_derivative;
                for (std::size_t i = 0;
                     i < unknowns.size(); ++i) {
                    if (kinds[i] ==
                        DaeVariableKind::differential) {
                        derivative[i] = unknowns[i];
                    } else {
                        state[i] = unknowns[i];
                    }
                }
                Matrix state_partials(
                    jacobian.size(),
                    std::vector<double>(unknowns.size(), 0.0));
                Matrix combined_partials(
                    jacobian.size(),
                    std::vector<double>(unknowns.size(), 0.0));
                auto status = problem.jacobian(
                    initial_time, state, derivative, 0.0,
                    state_partials);
                if (!status.ok()) {
                    throw std::runtime_error(
                        status.message.empty()
                        ? "DAE initialization state Jacobian "
                          "evaluation failed"
                        : status.message);
                }
                status = problem.jacobian(
                    initial_time, state, derivative, 1.0,
                    combined_partials);
                if (!status.ok()) {
                    throw std::runtime_error(
                        status.message.empty()
                        ? "DAE initialization derivative Jacobian "
                          "evaluation failed"
                        : status.message);
                }
                for (std::size_t row = 0;
                     row < jacobian.size(); ++row) {
                    for (std::size_t column = 0;
                         column < unknowns.size(); ++column) {
                        jacobian[row][column] =
                            kinds[column] ==
                                DaeVariableKind::differential
                            ? combined_partials[row][column] -
                                  state_partials[row][column]
                            : state_partials[row][column];
                    }
                }
            };
    }

    NonlinearSolveResult solve = solve_newton(initialization, options);
    DaeInitializationResult result;
    result.state = problem.initial_state;
    result.derivative = initial_derivative;
    result.diagnostics = std::move(solve.diagnostics);
    if (!result.diagnostics.converged) {
        constexpr std::string_view marker = "near column ";
        const auto marker_position =
            result.diagnostics.message.find(marker);
        if (marker_position != std::string::npos) {
            const auto number_position =
                marker_position + marker.size();
            try {
                const auto column = static_cast<std::size_t>(
                    std::stoull(result.diagnostics.message.substr(
                        number_position)));
                if (column < initialization.variable_names.size()) {
                    result.diagnostics.message +=
                        " ('" + initialization.variable_names[column] +
                        "')";
                }
            } catch (const std::exception&) {
                // Preserve the linear-solver diagnostic when its backend
                // does not report a parseable numeric column.
            }
        }
    }
    for (std::size_t i = 0; i < size; ++i) {
        if (kinds[i] == DaeVariableKind::differential) {
            result.derivative[i] = solve.x[i];
        } else {
            result.state[i] = solve.x[i];
        }
    }
    return result;
}

DaeSolveResult integrate_dae(const DaeProblem& problem,
                             const TimeIntegrationOptions& options) {
    validate_dae_problem(problem);
    validate_integration_options(options);

    const std::size_t size = problem.initial_state.size();
    const auto variable_scales =
        default_values(problem.variable_scales, size, 1.0, "variable_scales");
    const auto residual_scales =
        default_values(problem.residual_scales, size, 1.0, "residual_scales");
    const auto lower_bounds =
        default_values(problem.lower_bounds, size,
                       -std::numeric_limits<double>::infinity(), "lower_bounds");
    const auto upper_bounds =
        default_values(problem.upper_bounds, size,
                       std::numeric_limits<double>::infinity(), "upper_bounds");

    DaeSolveResult result;
    SolverOptions nonlinear_options = options.nonlinear_options;
    if ((problem.sparse_jacobian_pattern.has_value() ||
         problem.sparse_jacobian) &&
        !nonlinear_options.sparse_factorization &&
        !nonlinear_options.sparse_linear_solver &&
        !nonlinear_options.linear_solver) {
        nonlinear_options.sparse_factorization =
            make_default_sparse_factorization();
    }
    const auto accumulate_nonlinear_diagnostics =
        [&result](const SolverDiagnostics& diagnostics) {
            ++result.diagnostics.nonlinear_solves;
            result.diagnostics.nonlinear_iterations +=
                diagnostics.iterations;
            result.diagnostics.symbolic_factorizations +=
                diagnostics.symbolic_factorizations;
            result.diagnostics.numeric_factorizations +=
                diagnostics.numeric_factorizations;
            if (diagnostics.linear_solver_backend !=
                "not-used") {
                result.diagnostics.linear_solver_backend =
                    diagnostics.linear_solver_backend;
            }
        };
    std::vector<double> state = problem.initial_state;
    std::vector<double> derivative =
        default_values(problem.initial_derivative, size, 0.0, "initial_derivative");
    if (options.compute_consistent_initial_conditions) {
        auto initialized =
            make_consistent_initial_conditions(problem, options.start_time,
                                               nonlinear_options);
        accumulate_nonlinear_diagnostics(initialized.diagnostics);
        if (!initialized.diagnostics.converged) {
            result.diagnostics.message =
                "consistent initial-condition solve failed: " +
                initialized.diagnostics.message;
            result.diagnostics.final_time = options.start_time;
            return result;
        }
        state = std::move(initialized.state);
        derivative = std::move(initialized.derivative);
    }

    double time = options.start_time;
    double step = std::clamp(options.initial_step, options.min_step, options.max_step);
    result.trajectory.push_back(DaeState{time, state, derivative});
    std::vector<double> previous_event_values;
    previous_event_values.reserve(problem.events.size());
    for (const auto& event : problem.events) {
        previous_event_values.push_back(event.evaluate(time, state));
    }
    std::vector<double> older_state;
    double previous_accepted_step = 0.0;
    bool has_bdf2_history = false;

    int consecutive_rejections = 0;
    std::string last_rejection_message;
    while (time < options.end_time &&
           result.diagnostics.accepted_steps < options.max_steps) {
        const double time_roundoff =
            16.0 * std::numeric_limits<double>::epsilon() *
            std::max({1.0, std::abs(time), std::abs(options.end_time)});
        if (options.end_time - time <= time_roundoff) {
            time = options.end_time;
            if (!result.trajectory.empty()) {
                result.trajectory.back().time = time;
            }
            break;
        }
        step = std::min(step, options.end_time - time);
        if (step < options.min_step && options.end_time - time > options.min_step) {
            result.diagnostics.message = "time step fell below min_step";
            result.diagnostics.final_time = time;
            return result;
        }

        const int trial_order =
            options.maximum_order >= 2 && has_bdf2_history ? 2 : 1;
        auto full = trial_order == 2
            ? solve_bdf2_step(
                  problem, time + step, step, state, older_state,
                  previous_accepted_step, variable_scales,
                  residual_scales, lower_bounds, upper_bounds,
                  nonlinear_options)
            : solve_backward_euler_step(
                  problem, time + step, step, state, derivative,
                  variable_scales, residual_scales, lower_bounds,
                  upper_bounds, nonlinear_options);
        accumulate_nonlinear_diagnostics(full.diagnostics);

        ImplicitStepResult first_half;
        ImplicitStepResult second_half;
        if (full.success) {
            first_half = trial_order == 2
                ? solve_bdf2_step(
                      problem, time + 0.5 * step, 0.5 * step,
                      state, older_state, previous_accepted_step,
                      variable_scales, residual_scales, lower_bounds,
                      upper_bounds, nonlinear_options)
                : solve_backward_euler_step(
                      problem, time + 0.5 * step, 0.5 * step,
                      state, derivative, variable_scales,
                      residual_scales, lower_bounds, upper_bounds,
                      nonlinear_options);
            accumulate_nonlinear_diagnostics(
                first_half.diagnostics);
        }
        if (full.success && first_half.success) {
            second_half = trial_order == 2
                ? solve_bdf2_step(
                      problem, time + step, 0.5 * step,
                      first_half.state, state, 0.5 * step,
                      variable_scales, residual_scales, lower_bounds,
                      upper_bounds, nonlinear_options)
                : solve_backward_euler_step(
                      problem, time + step, 0.5 * step,
                      first_half.state, first_half.derivative,
                      variable_scales, residual_scales, lower_bounds,
                      upper_bounds, nonlinear_options);
            accumulate_nonlinear_diagnostics(
                second_half.diagnostics);
        }

        double error = std::numeric_limits<double>::infinity();
        IntegrationErrorEstimate error_estimate;
        if (full.success && first_half.success && second_half.success) {
            error_estimate = integration_error_estimate(
                full.state, second_half.state, state, variable_scales,
                problem.variable_kinds,
                options.absolute_tolerance,
                options.relative_tolerance);
            const double richardson =
                std::pow(2.0, trial_order) - 1.0;
            error_estimate.norm /= richardson;
            error_estimate.maximum_ratio /= richardson;
            error = error_estimate.norm;
            result.diagnostics.last_error_norm = error;
            if (result.diagnostics.limiting_error_variable.empty() ||
                error_estimate.maximum_ratio >
                    result.diagnostics.maximum_error_ratio) {
                result.diagnostics.maximum_error_ratio =
                    error_estimate.maximum_ratio;
                if (error_estimate.has_controlled_variable) {
                    result.diagnostics.limiting_error_variable =
                        problem.variable_names.at(
                            error_estimate.limiting_variable);
                }
            }
        }

        if (!std::isfinite(error) || error > 1.0) {
            if (!full.success) {
                last_rejection_message =
                    "full implicit step failed: " +
                    full.diagnostics.message;
            } else if (!first_half.success) {
                last_rejection_message =
                    "first half step failed: " +
                    first_half.diagnostics.message;
            } else if (!second_half.success) {
                last_rejection_message =
                    "second half step failed: " +
                    second_half.diagnostics.message;
            } else {
                last_rejection_message =
                    "estimated local error exceeded tolerance";
            }
            ++result.diagnostics.rejected_steps;
            ++consecutive_rejections;
            if (trial_order == 2) {
                // A failed or inaccurate multistep trial must not poison
                // subsequent retries. Restart safely at BDF1, then rebuild
                // order-two history from the next accepted state.
                has_bdf2_history = false;
            }
            if (consecutive_rejections > options.max_consecutive_rejections) {
                result.diagnostics.message =
                    "maximum consecutive rejected time steps reached: " +
                    last_rejection_message;
                result.diagnostics.final_time = time;
                return result;
            }
            const double factor =
                std::isfinite(error)
                    ? std::clamp(
                          0.9 / std::pow(
                              std::max(error, 1.0e-12),
                              1.0 / static_cast<double>(trial_order + 1)),
                          0.1, 0.5)
                    : 0.5;
            step *= factor;
            if (step < options.min_step) {
                result.diagnostics.message =
                    "time step fell below min_step after a rejected step: " +
                    last_rejection_message;
                result.diagnostics.final_time = time;
                return result;
            }
            continue;
        }

        consecutive_rejections = 0;
        const double previous_time = time;
        const std::vector<double> previous_state = state;
        time += step;
        if (std::abs(options.end_time - time) <= time_roundoff) {
            time = options.end_time;
        }
        older_state = previous_state;
        previous_accepted_step = step;
        has_bdf2_history = true;
        state = std::move(second_half.state);
        derivative = std::move(second_half.derivative);
        ++result.diagnostics.accepted_steps;
        result.diagnostics.maximum_order_used = std::max(
            result.diagnostics.maximum_order_used, trial_order);
        result.diagnostics.last_step = step;
        result.diagnostics.maximum_accepted_error_norm = std::max(
            result.diagnostics.maximum_accepted_error_norm, error);
        result.trajectory.push_back(DaeState{time, state, derivative});

        bool terminal_event = false;
        std::size_t earliest_terminal_index = std::numeric_limits<std::size_t>::max();
        for (std::size_t i = 0; i < problem.events.size(); ++i) {
            const auto& event = problem.events[i];
            const double current_value = event.evaluate(time, state);
            if (event_crossed(previous_event_values[i], current_value, event.direction)) {
                const double denominator =
                    std::abs(previous_event_values[i]) + std::abs(current_value);
                const double fraction =
                    denominator == 0.0 ? 1.0 : std::abs(previous_event_values[i]) / denominator;
                DetectedEvent detected;
                detected.name = event.name;
                detected.time = previous_time + fraction * step;
                detected.state.resize(size);
                for (std::size_t j = 0; j < size; ++j) {
                    detected.state[j] =
                        previous_state[j] + fraction * (state[j] - previous_state[j]);
                }
                detected.terminal = event.terminal;
                result.events.push_back(std::move(detected));
                terminal_event = terminal_event || event.terminal;
                if (event.terminal &&
                    (earliest_terminal_index == std::numeric_limits<std::size_t>::max() ||
                     result.events.back().time <
                         result.events[earliest_terminal_index].time)) {
                    earliest_terminal_index = result.events.size() - 1;
                }
            }
            previous_event_values[i] = current_value;
        }
        if (terminal_event) {
            const DetectedEvent& earliest_terminal =
                result.events[earliest_terminal_index];
            const double event_step = earliest_terminal.time - previous_time;
            state = earliest_terminal.state;
            derivative.assign(size, 0.0);
            if (event_step > 0.0) {
                for (std::size_t i = 0; i < size; ++i) {
                    derivative[i] = (state[i] - previous_state[i]) / event_step;
                }
            }
            time = earliest_terminal.time;
            result.trajectory.back() = DaeState{time, state, derivative};
            result.diagnostics.success = true;
            result.diagnostics.final_time = time;
            result.diagnostics.message = "terminal event detected";
            return result;
        }

        const double factor =
            error <= 1.0e-12 ? 2.0
                             : std::clamp(
                                   0.9 / std::pow(
                                       error,
                                       1.0 / static_cast<double>(
                                           trial_order + 1)),
                                   0.5, 2.0);
        step = std::clamp(step * factor, options.min_step, options.max_step);
    }

    result.diagnostics.final_time = time;
    if (time >= options.end_time) {
        result.diagnostics.success = true;
        result.diagnostics.message = "integration reached end_time";
    } else {
        result.diagnostics.message = "maximum accepted time steps reached";
    }
    return result;
}

}  // namespace thermox
