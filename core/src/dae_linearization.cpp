#include "thermox/dae_linearization.hpp"

#include "thermox/dense_linear_solver.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

namespace thermox {
namespace {

void validate_problem_vectors(
    const DaeProblem& problem,
    const std::vector<double>& state,
    const std::vector<double>& derivative) {
    const auto size = problem.variable_names.size();
    if (size == 0 || problem.residual_names.size() != size ||
        problem.variable_kinds.size() != size ||
        problem.variable_scales.size() != size ||
        problem.derivative_scales.size() != size ||
        problem.residual_scales.size() != size ||
        problem.lower_bounds.size() != size ||
        problem.upper_bounds.size() != size ||
        state.size() != size || derivative.size() != size ||
        !problem.residual) {
        throw std::invalid_argument(
            "DAE linearization requires a complete square initialized "
            "problem");
    }
}

double perturbation_for(
    double value,
    double scale,
    const DaeLinearizationOptions& options) {
    return std::max(
        options.minimum_perturbation,
        options.relative_perturbation *
            std::max(std::abs(value), std::abs(scale)));
}

}  // namespace

DaeLinearizationResult linearize_index1_dae(
    const DaeProblem& problem,
    double time,
    const std::vector<double>& operating_state,
    const std::vector<double>& operating_derivative,
    const std::vector<DaeLinearizationInput>& inputs,
    const DaeLinearizationOptions& options) {
    validate_problem_vectors(
        problem, operating_state, operating_derivative);
    if (!std::isfinite(time) ||
        !std::isfinite(options.relative_perturbation) ||
        options.relative_perturbation <= 0.0 ||
        !std::isfinite(options.minimum_perturbation) ||
        options.minimum_perturbation <= 0.0) {
        throw std::invalid_argument(
            "DAE linearization time must be finite and perturbations must "
            "be finite and positive");
    }

    const std::size_t variable_count = problem.variable_names.size();
    std::vector<std::size_t> differential;
    std::set<std::size_t> input_variables;
    std::set<std::size_t> removed_residuals;
    for (std::size_t index = 0; index < variable_count; ++index) {
        if (problem.variable_kinds[index] ==
            DaeVariableKind::differential) {
            differential.push_back(index);
        }
    }
    if (differential.empty()) {
        throw std::invalid_argument(
            "DAE linearization requires at least one differential state");
    }
    for (const auto& input : inputs) {
        if (input.variable >= variable_count ||
            input.fixed_residual >= variable_count) {
            throw std::invalid_argument(
                "DAE linearization input index is out of range");
        }
        if (problem.variable_kinds[input.variable] !=
            DaeVariableKind::algebraic) {
            throw std::invalid_argument(
                "DAE linearization inputs must be algebraic variables");
        }
        if (!input_variables.insert(input.variable).second ||
            !removed_residuals.insert(input.fixed_residual).second) {
            throw std::invalid_argument(
                "DAE linearization input variables and fixed residuals "
                "must be unique");
        }
    }

    std::vector<std::size_t> algebraic_unknowns;
    std::vector<std::size_t> active_residuals;
    for (std::size_t index = 0; index < variable_count; ++index) {
        if (problem.variable_kinds[index] == DaeVariableKind::algebraic &&
            !input_variables.contains(index)) {
            algebraic_unknowns.push_back(index);
        }
        if (!removed_residuals.contains(index)) {
            active_residuals.push_back(index);
        }
    }
    if (active_residuals.size() !=
        differential.size() + algebraic_unknowns.size()) {
        throw std::invalid_argument(
            "DAE linearization system is not square after releasing "
            "inputs");
    }

    DaeLinearizationResult result;
    result.differential_state_indices = differential;
    result.operating_state = operating_state;
    result.operating_derivative = operating_derivative;
    for (const auto index : differential) {
        result.differential_state_names.push_back(
            problem.variable_names[index]);
    }
    for (const auto& input : inputs) {
        result.input_indices.push_back(input.variable);
        result.input_names.push_back(
            input.name.empty()
                ? problem.variable_names[input.variable]
                : input.name);
    }

    auto evaluate = [&result, &problem, time](
        const std::vector<double>& state,
        const std::vector<double>& derivative,
        std::vector<double>& residual) {
        ++result.diagnostics.residual_evaluations;
        residual.assign(problem.residual_names.size(), 0.0);
        return problem.residual(time, state, derivative, residual);
    };
    std::vector<double> nominal;
    auto status = evaluate(
        operating_state, operating_derivative, nominal);
    if (!status.ok()) {
        result.diagnostics.message =
            "operating-point residual evaluation failed: " +
            status.message;
        return result;
    }
    for (std::size_t row = 0; row < variable_count; ++row) {
        result.diagnostics.maximum_operating_residual = std::max(
            result.diagnostics.maximum_operating_residual,
            std::abs(nominal[row]) / problem.residual_scales[row]);
    }

    auto state_partial = [&](std::size_t variable, Matrix& destination,
                             std::size_t column) {
        double step = perturbation_for(
            operating_state[variable], problem.variable_scales[variable],
            options);
        bool can_plus = false;
        bool can_minus = false;
        std::vector<double> plus;
        std::vector<double> minus;
        for (int attempt = 0; attempt < 9; ++attempt) {
            can_plus = operating_state[variable] + step <=
                problem.upper_bounds[variable];
            can_minus = operating_state[variable] - step >=
                problem.lower_bounds[variable];
            if (can_plus) {
                auto state = operating_state;
                state[variable] += step;
                status = evaluate(state, operating_derivative, plus);
                if (!status.ok()) can_plus = false;
            }
            if (can_minus) {
                auto state = operating_state;
                state[variable] -= step;
                status = evaluate(state, operating_derivative, minus);
                if (!status.ok()) can_minus = false;
            }
            if (can_plus || can_minus) break;
            step = std::max(
                options.minimum_perturbation, step * 0.1);
        }
        if (!can_plus && !can_minus) {
            result.diagnostics.message =
                "both state perturbation directions are outside the "
                "residual domain for " + problem.variable_names[variable];
            return false;
        }
        for (std::size_t row = 0; row < active_residuals.size(); ++row) {
            const auto source = active_residuals[row];
            if (can_plus && can_minus) {
                destination[row][column] =
                    (plus[source] - minus[source]) / (2.0 * step);
            } else if (can_plus) {
                destination[row][column] =
                    (plus[source] - nominal[source]) / step;
            } else {
                destination[row][column] =
                    (nominal[source] - minus[source]) / step;
            }
        }
        return true;
    };

    auto rate_partial = [&](std::size_t variable, Matrix& destination,
                            std::size_t column) {
        const double step = perturbation_for(
            operating_derivative[variable],
            problem.derivative_scales[variable], options);
        auto plus_rate = operating_derivative;
        auto minus_rate = operating_derivative;
        plus_rate[variable] += step;
        minus_rate[variable] -= step;
        std::vector<double> plus;
        std::vector<double> minus;
        status = evaluate(operating_state, plus_rate, plus);
        if (!status.ok()) {
            result.diagnostics.message =
                "positive rate perturbation failed for " +
                problem.variable_names[variable] + ": " + status.message;
            return false;
        }
        status = evaluate(operating_state, minus_rate, minus);
        if (!status.ok()) {
            result.diagnostics.message =
                "negative rate perturbation failed for " +
                problem.variable_names[variable] + ": " + status.message;
            return false;
        }
        for (std::size_t row = 0; row < active_residuals.size(); ++row) {
            const auto source = active_residuals[row];
            destination[row][column] =
                (plus[source] - minus[source]) / (2.0 * step);
        }
        return true;
    };

    const auto response_size = active_residuals.size();
    Matrix response_jacobian(
        response_size, std::vector<double>(response_size, 0.0));
    Matrix right_hand_sides;
    right_hand_sides.reserve(differential.size() + inputs.size());
    for (std::size_t column = 0; column < differential.size(); ++column) {
        if (!rate_partial(
                differential[column], response_jacobian, column)) {
            return result;
        }
    }
    for (std::size_t column = 0;
         column < algebraic_unknowns.size(); ++column) {
        if (!state_partial(
                algebraic_unknowns[column], response_jacobian,
                differential.size() + column)) {
            return result;
        }
    }
    auto append_rhs = [&](std::size_t variable) {
        Matrix partial(
            response_size, std::vector<double>(1, 0.0));
        if (!state_partial(variable, partial, 0)) return false;
        std::vector<double> rhs(response_size, 0.0);
        for (std::size_t row = 0; row < response_size; ++row) {
            rhs[row] = -partial[row][0];
        }
        right_hand_sides.push_back(std::move(rhs));
        return true;
    };
    for (const auto variable : differential) {
        if (!append_rhs(variable)) return result;
    }
    for (const auto& input : inputs) {
        if (!append_rhs(input.variable)) return result;
    }

    std::vector<double> response_variable_scales;
    response_variable_scales.reserve(response_size);
    for (const auto index : differential) {
        response_variable_scales.push_back(
            problem.derivative_scales[index]);
    }
    for (const auto index : algebraic_unknowns) {
        response_variable_scales.push_back(
            problem.variable_scales[index]);
    }
    for (std::size_t row = 0; row < response_size; ++row) {
        const double residual_scale =
            problem.residual_scales[active_residuals[row]];
        for (std::size_t column = 0; column < response_size; ++column) {
            response_jacobian[row][column] *=
                response_variable_scales[column] / residual_scale;
        }
        for (auto& rhs : right_hand_sides) {
            rhs[row] /= residual_scale;
        }
    }

    DenseLinearFactorization factorization;
    if (!factorization.factorize(std::move(response_jacobian))) {
        std::vector<std::string> response_variables;
        for (const auto index : differential) {
            response_variables.push_back(
                problem.variable_names[index] + ".rate");
        }
        for (const auto index : algebraic_unknowns) {
            response_variables.push_back(problem.variable_names[index]);
        }
        result.diagnostics.message =
            "index-1 DAE response Jacobian factorization failed: " +
            factorization.message();
        const auto marker = factorization.message().find("column ");
        if (marker != std::string::npos) {
            try {
                const auto column = static_cast<std::size_t>(std::stoul(
                    factorization.message().substr(marker + 7U)));
                if (column < response_variables.size()) {
                    result.diagnostics.message +=
                        " (response variable " +
                        response_variables[column] + ")";
                }
            } catch (const std::exception&) {
            }
        }
        return result;
    }
    const auto solved = factorization.solve_multiple(right_hand_sides);
    result.diagnostics.linear_right_hand_sides =
        static_cast<int>(solved.size());
    for (const auto& column : solved) {
        if (!column.success) {
            result.diagnostics.message =
                "index-1 DAE sensitivity solve failed: " +
                column.message;
            return result;
        }
    }
    result.A.assign(
        differential.size(),
        std::vector<double>(differential.size(), 0.0));
    result.B.assign(
        differential.size(),
        std::vector<double>(inputs.size(), 0.0));
    for (std::size_t column = 0; column < differential.size(); ++column) {
        for (std::size_t row = 0; row < differential.size(); ++row) {
            result.A[row][column] = solved[column].x[row] *
                problem.derivative_scales[differential[row]];
        }
    }
    for (std::size_t column = 0; column < inputs.size(); ++column) {
        for (std::size_t row = 0; row < differential.size(); ++row) {
            result.B[row][column] =
                solved[differential.size() + column].x[row] *
                problem.derivative_scales[differential[row]];
        }
    }
    result.diagnostics.success = true;
    result.diagnostics.message =
        "index-1 DAE tangent linearization completed";
    return result;
}

DaeLinearizationResponseProbeResult
validate_index1_dae_linearization_response(
    const DaeProblem& problem,
    double time,
    const DaeLinearizationResult& linearization,
    const std::vector<DaeLinearizationInput>& inputs,
    const std::vector<double>& state_perturbations,
    const std::vector<double>& input_perturbations,
    const DaeLinearizationResponseProbeOptions& options) {
    DaeLinearizationResponseProbeResult result;
    result.state_perturbations = state_perturbations;
    result.input_perturbations = input_perturbations;
    if (!linearization.diagnostics.success) {
        result.message =
            "nonlinear response probe requires a successful linearization";
        return result;
    }
    if (!std::isfinite(time) ||
        !std::isfinite(options.absolute_normalized_tolerance) ||
        options.absolute_normalized_tolerance < 0.0 ||
        !std::isfinite(options.relative_tolerance) ||
        options.relative_tolerance < 0.0) {
        throw std::invalid_argument(
            "nonlinear response probe tolerances must be finite and "
            "non-negative and time must be finite");
    }
    validate_problem_vectors(
        problem, linearization.operating_state,
        linearization.operating_derivative);
    const std::size_t state_count =
        linearization.differential_state_indices.size();
    const std::size_t input_count = inputs.size();
    if (state_perturbations.size() != state_count ||
        input_perturbations.size() != input_count ||
        linearization.A.size() != state_count ||
        linearization.B.size() != state_count ||
        linearization.input_indices.size() != input_count) {
        throw std::invalid_argument(
            "nonlinear response probe dimensions do not match the "
            "linearization");
    }
    bool nonzero_perturbation = false;
    for (const double value : state_perturbations) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument(
                "state perturbations must be finite");
        }
        nonzero_perturbation = nonzero_perturbation || value != 0.0;
    }
    for (std::size_t index = 0; index < input_count; ++index) {
        if (!std::isfinite(input_perturbations[index]) ||
            inputs[index].variable !=
                linearization.input_indices[index] ||
            inputs[index].fixed_residual >=
                problem.residual_names.size()) {
            throw std::invalid_argument(
                "input perturbations and declarations must match the "
                "linearization");
        }
        nonzero_perturbation = nonzero_perturbation ||
            input_perturbations[index] != 0.0;
    }
    if (!nonzero_perturbation) {
        throw std::invalid_argument(
            "nonlinear response probe requires a nonzero perturbation");
    }

    std::vector<double> predicted_rate_change(state_count, 0.0);
    for (std::size_t row = 0; row < state_count; ++row) {
        if (linearization.A[row].size() != state_count ||
            linearization.B[row].size() != input_count) {
            throw std::invalid_argument(
                "linearization matrix dimensions are inconsistent");
        }
        for (std::size_t column = 0; column < state_count; ++column) {
            predicted_rate_change[row] +=
                linearization.A[row][column] *
                state_perturbations[column];
        }
        for (std::size_t column = 0; column < input_count; ++column) {
            predicted_rate_change[row] +=
                linearization.B[row][column] *
                input_perturbations[column];
        }
    }

    DaeProblem probe = problem;
    probe.initial_state = linearization.operating_state;
    probe.initial_derivative = linearization.operating_derivative;
    for (std::size_t index = 0; index < state_count; ++index) {
        const auto variable =
            linearization.differential_state_indices[index];
        probe.initial_state[variable] += state_perturbations[index];
    }
    std::vector<std::pair<std::size_t, std::pair<std::size_t, double>>>
        released_inputs;
    released_inputs.reserve(input_count);
    for (std::size_t index = 0; index < input_count; ++index) {
        const auto variable = inputs[index].variable;
        probe.initial_state[variable] += input_perturbations[index];
        released_inputs.push_back({
            inputs[index].fixed_residual,
            {variable, probe.initial_state[variable]}});
    }
    for (std::size_t index = 0; index < state_count; ++index) {
        const auto variable =
            linearization.differential_state_indices[index];
        probe.initial_derivative[variable] +=
            predicted_rate_change[index];
    }
    for (std::size_t variable = 0;
         variable < probe.initial_state.size(); ++variable) {
        if (probe.initial_state[variable] < probe.lower_bounds[variable] ||
            probe.initial_state[variable] > probe.upper_bounds[variable]) {
            throw std::invalid_argument(
                "nonlinear response perturbation places variable '" +
                problem.variable_names[variable] +
                "' outside its bounds");
        }
    }

    const auto original_residual = problem.residual;
    probe.residual =
        [original_residual, released_inputs](
            double evaluation_time,
            const std::vector<double>& state,
            const std::vector<double>& derivative,
            std::vector<double>& residual) {
            auto status = original_residual(
                evaluation_time, state, derivative, residual);
            if (!status.ok()) return status;
            for (const auto& [row, target] : released_inputs) {
                residual[row] = state[target.first] - target.second;
            }
            return EvaluationStatus::success();
        };
    if (problem.residual_subset) {
        const auto original_subset = problem.residual_subset;
        probe.residual_subset =
            [original_subset, released_inputs](
                double evaluation_time,
                const std::vector<double>& state,
                const std::vector<double>& derivative,
                const std::vector<std::size_t>& rows,
                std::vector<double>& residual) {
                auto status = original_subset(
                    evaluation_time, state, derivative,
                    rows, residual);
                if (!status.ok()) return status;
                for (std::size_t output = 0;
                     output < rows.size(); ++output) {
                    for (const auto& [row, target] : released_inputs) {
                        if (rows[output] == row) {
                            residual[output] =
                                state[target.first] - target.second;
                            break;
                        }
                    }
                }
                return EvaluationStatus::success();
            };
    }

    const auto initialized = make_consistent_initial_conditions(
        probe, time, options.nonlinear_solver);
    result.nonlinear_diagnostics = initialized.diagnostics;
    if (!initialized.diagnostics.converged) {
        result.message =
            "nonlinear response probe failed to restore DAE consistency: " +
            initialized.diagnostics.message;
        return result;
    }
    result.success = true;
    result.passed = true;
    result.states.reserve(state_count);
    for (std::size_t index = 0; index < state_count; ++index) {
        const auto variable =
            linearization.differential_state_indices[index];
        const double nonlinear_rate_change =
            initialized.derivative[variable] -
            linearization.operating_derivative[variable];
        const double absolute_error = std::abs(
            nonlinear_rate_change - predicted_rate_change[index]);
        const double rate_scale = problem.derivative_scales[variable];
        const double normalized_absolute_error =
            absolute_error / rate_scale;
        const double reference = std::max(
            std::abs(nonlinear_rate_change),
            std::abs(predicted_rate_change[index]));
        const double relative_error = reference == 0.0
            ? 0.0
            : absolute_error / reference;
        result.maximum_normalized_absolute_error = std::max(
            result.maximum_normalized_absolute_error,
            normalized_absolute_error);
        result.maximum_relative_error = std::max(
            result.maximum_relative_error, relative_error);
        const bool state_passed =
            absolute_error <=
                options.absolute_normalized_tolerance * rate_scale +
                    options.relative_tolerance * reference;
        result.passed = result.passed && state_passed;
        result.states.push_back({
            linearization.differential_state_names[index],
            predicted_rate_change[index],
            nonlinear_rate_change,
            absolute_error,
            normalized_absolute_error,
            relative_error});
    }
    result.message = result.passed
        ? "linearized rate response agrees with the consistent nonlinear DAE response"
        : "linearized rate response differs from the consistent nonlinear DAE response";
    return result;
}

}  // namespace thermox
