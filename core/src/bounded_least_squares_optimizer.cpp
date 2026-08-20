#include "thermox/bounded_least_squares_optimizer.hpp"

#include "thermox/least_squares_solver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace thermox {

namespace {

double squared_norm(const std::vector<double>& values) {
    double result = 0.0;
    for (const double value : values) result += value * value;
    return result;
}

bool finite_vector(const std::vector<double>& values) {
    return std::all_of(
        values.begin(), values.end(),
        [](const double value) { return std::isfinite(value); });
}

}  // namespace

BoundedLeastSquaresResult solve_bounded_nonlinear_least_squares(
    const BoundedResidualFunction& evaluate,
    std::vector<double> values,
    std::vector<double> lower,
    std::vector<double> upper,
    const BoundedLeastSquaresSettings& settings) {
    BoundedLeastSquaresResult result;
    const std::size_t parameter_count = values.size();
    const bool settings_valid =
        settings.max_iterations > 0 &&
        std::isfinite(settings.finite_difference_fraction) &&
        settings.finite_difference_fraction > 0.0 &&
        settings.finite_difference_fraction < 1.0 &&
        std::isfinite(settings.initial_trust_region_radius) &&
        settings.initial_trust_region_radius > 0.0 &&
        std::isfinite(settings.minimum_trust_region_radius) &&
        settings.minimum_trust_region_radius > 0.0 &&
        settings.minimum_trust_region_radius <
            settings.initial_trust_region_radius &&
        std::isfinite(settings.maximum_trust_region_radius) &&
        settings.maximum_trust_region_radius >=
            settings.initial_trust_region_radius &&
        std::isfinite(settings.acceptance_ratio) &&
        settings.acceptance_ratio >= 0.0 &&
        settings.acceptance_ratio < 1.0 &&
        std::isfinite(settings.gradient_tolerance) &&
        settings.gradient_tolerance > 0.0 &&
        std::isfinite(settings.step_tolerance) &&
        settings.step_tolerance > 0.0 &&
        std::isfinite(settings.objective_relative_tolerance) &&
        settings.objective_relative_tolerance > 0.0;
    if (!evaluate || parameter_count == 0U ||
        lower.size() != parameter_count ||
        upper.size() != parameter_count ||
        !finite_vector(values) || !finite_vector(lower) ||
        !finite_vector(upper) || !settings_valid) {
        result.message =
            "invalid bounded nonlinear least-squares contract";
        return result;
    }
    std::vector<double> ranges(parameter_count, 0.0);
    for (std::size_t index = 0; index < parameter_count; ++index) {
        if (!(lower[index] < upper[index]) ||
            values[index] < lower[index] ||
            values[index] > upper[index]) {
            result.message =
                "initial point and finite bounds are inconsistent";
            return result;
        }
        ranges[index] = upper[index] - lower[index];
    }

    const auto checked_evaluate =
        [&](const std::vector<double>& candidate,
            const std::vector<double>* reference,
            bool sensitivity) {
        ++result.diagnostics.residual_evaluations;
        if (sensitivity) {
            ++result.diagnostics.sensitivity_evaluations;
        }
        auto evaluation = evaluate(candidate, reference);
        if (evaluation.success &&
            (evaluation.residuals.empty() ||
             !finite_vector(evaluation.residuals))) {
            evaluation.success = false;
            evaluation.message =
                "residual callback returned an empty or non-finite vector";
        }
        return evaluation;
    };

    auto current = checked_evaluate(values, nullptr, false);
    if (!current.success) {
        result.message = "initial residual evaluation failed: " +
            current.message;
        return result;
    }
    result.initial_objective = squared_norm(current.residuals);
    double trust_radius = settings.initial_trust_region_radius;

    for (int iteration = 0;
         iteration < settings.max_iterations; ++iteration) {
        Matrix jacobian(
            current.residuals.size(),
            std::vector<double>(parameter_count, 0.0));
        for (std::size_t column = 0;
             column < parameter_count; ++column) {
            const double scaled_value =
                (values[column] - lower[column]) / ranges[column];
            const double delta = settings.finite_difference_fraction;
            const bool can_subtract = scaled_value - delta >= 0.0;
            const bool can_add = scaled_value + delta <= 1.0;
            BoundedResidualEvaluation negative;
            BoundedResidualEvaluation positive;
            if (can_subtract) {
                auto candidate = values;
                candidate[column] -= delta * ranges[column];
                negative = checked_evaluate(
                    candidate, &values, true);
            }
            if (can_add) {
                auto candidate = values;
                candidate[column] += delta * ranges[column];
                positive = checked_evaluate(
                    candidate, &values, true);
            }
            const bool negative_valid =
                can_subtract && negative.success &&
                negative.residuals.size() == current.residuals.size();
            const bool positive_valid =
                can_add && positive.success &&
                positive.residuals.size() == current.residuals.size();
            if (!negative_valid && !positive_valid) {
                result.message =
                    "could not evaluate sensitivity for parameter " +
                    std::to_string(column);
                result.x = values;
                result.residuals = current.residuals;
                result.final_objective = squared_norm(current.residuals);
                return result;
            }
            for (std::size_t row = 0; row < jacobian.size(); ++row) {
                if (negative_valid && positive_valid) {
                    jacobian[row][column] =
                        (positive.residuals[row] -
                         negative.residuals[row]) /
                        (2.0 * delta);
                } else if (positive_valid) {
                    jacobian[row][column] =
                        (positive.residuals[row] -
                         current.residuals[row]) / delta;
                } else {
                    jacobian[row][column] =
                        (current.residuals[row] -
                         negative.residuals[row]) / delta;
                }
            }
        }

        std::vector<double> gradient(parameter_count, 0.0);
        std::vector<std::size_t> free_indices;
        double projected_gradient_norm = 0.0;
        for (std::size_t column = 0;
             column < parameter_count; ++column) {
            for (std::size_t row = 0; row < jacobian.size(); ++row) {
                gradient[column] +=
                    jacobian[row][column] * current.residuals[row];
            }
            const double scaled_value =
                (values[column] - lower[column]) / ranges[column];
            const bool outward =
                (scaled_value <= 1.0e-12 && gradient[column] > 0.0) ||
                (scaled_value >= 1.0 - 1.0e-12 &&
                 gradient[column] < 0.0);
            if (!outward) {
                free_indices.push_back(column);
                projected_gradient_norm = std::max(
                    projected_gradient_norm,
                    std::abs(gradient[column]));
            }
        }
        result.diagnostics.final_projected_gradient_norm =
            projected_gradient_norm;
        if (projected_gradient_norm <= settings.gradient_tolerance) {
            result.diagnostics.converged = true;
            result.diagnostics.message =
                "projected gradient tolerance reached";
            result.diagnostics.iterations = iteration;
            break;
        }
        if (free_indices.empty()) {
            result.diagnostics.converged = true;
            result.diagnostics.message =
                "all parameters satisfy active-bound optimality";
            result.diagnostics.iterations = iteration;
            break;
        }

        Matrix free_jacobian(
            jacobian.size(),
            std::vector<double>(free_indices.size(), 0.0));
        for (std::size_t row = 0; row < jacobian.size(); ++row) {
            for (std::size_t column = 0;
                 column < free_indices.size(); ++column) {
                free_jacobian[row][column] =
                    jacobian[row][free_indices[column]];
            }
        }
        std::vector<double> rhs(current.residuals.size(), 0.0);
        for (std::size_t row = 0; row < rhs.size(); ++row) {
            rhs[row] = -current.residuals[row];
        }
        const auto linearized = solve_dense_least_squares(
            free_jacobian, std::move(rhs));
        result.diagnostics.sensitivity_rank = linearized.rank;
        result.diagnostics.factorization_quality =
            linearized.factorization_quality;
        if (!linearized.success) {
            result.message =
                "free-parameter sensitivity is not identifiable: " +
                linearized.message;
            result.x = values;
            result.residuals = current.residuals;
            result.final_objective = squared_norm(current.residuals);
            return result;
        }

        std::vector<double> scaled_step(parameter_count, 0.0);
        double step_norm = 0.0;
        for (std::size_t index = 0;
             index < free_indices.size(); ++index) {
            scaled_step[free_indices[index]] = linearized.x[index];
            step_norm = std::hypot(step_norm, linearized.x[index]);
        }
        if (step_norm > trust_radius) {
            const double scale = trust_radius / step_norm;
            for (auto& value : scaled_step) value *= scale;
            step_norm = trust_radius;
        }
        if (step_norm <= settings.step_tolerance) {
            result.diagnostics.converged = true;
            result.diagnostics.message =
                "scaled step tolerance reached";
            result.diagnostics.iterations = iteration;
            break;
        }

        auto candidate = values;
        double bounded_step_norm = 0.0;
        for (std::size_t index = 0;
             index < parameter_count; ++index) {
            candidate[index] = std::clamp(
                values[index] + scaled_step[index] * ranges[index],
                lower[index], upper[index]);
            scaled_step[index] =
                (candidate[index] - values[index]) / ranges[index];
            bounded_step_norm = std::hypot(
                bounded_step_norm, scaled_step[index]);
        }
        if (bounded_step_norm <= settings.step_tolerance) {
            result.diagnostics.converged = true;
            result.diagnostics.message =
                "bounded scaled step tolerance reached";
            result.diagnostics.iterations = iteration;
            break;
        }
        std::vector<double> predicted_residual = current.residuals;
        for (std::size_t row = 0; row < jacobian.size(); ++row) {
            for (std::size_t column = 0;
                 column < parameter_count; ++column) {
                predicted_residual[row] +=
                    jacobian[row][column] * scaled_step[column];
            }
        }
        const double objective = squared_norm(current.residuals);
        const double predicted_reduction =
            objective - squared_norm(predicted_residual);
        bool accepted = false;
        double ratio = -std::numeric_limits<double>::infinity();
        if (predicted_reduction > 0.0) {
            auto trial = checked_evaluate(candidate, &values, false);
            if (trial.success &&
                trial.residuals.size() == current.residuals.size()) {
                const double actual_reduction =
                    objective - squared_norm(trial.residuals);
                ratio = actual_reduction / predicted_reduction;
                if (actual_reduction > 0.0 &&
                    ratio >= settings.acceptance_ratio) {
                    values = std::move(candidate);
                    current = std::move(trial);
                    accepted = true;
                    ++result.diagnostics.accepted_steps;
                    if (actual_reduction /
                            std::max(objective, 1.0) <=
                        settings.objective_relative_tolerance) {
                        result.diagnostics.converged = true;
                        result.diagnostics.message =
                            "objective relative tolerance reached";
                    }
                }
            }
        }
        if (!accepted) ++result.diagnostics.rejected_steps;
        if (ratio < 0.25) {
            trust_radius *= 0.25;
        } else if (ratio > 0.75 &&
                   bounded_step_norm >= 0.9 * trust_radius) {
            trust_radius = std::min(
                settings.maximum_trust_region_radius,
                2.0 * trust_radius);
        }
        result.diagnostics.iterations = iteration + 1;
        if (result.diagnostics.converged) break;
        if (trust_radius < settings.minimum_trust_region_radius) {
            result.diagnostics.message =
                "trust-region radius fell below its minimum";
            break;
        }
    }

    if (!result.diagnostics.converged &&
        result.diagnostics.message.empty()) {
        result.diagnostics.message =
            "trust-region least squares reached iteration limit";
    }
    result.diagnostics.final_trust_region_radius = trust_radius;
    result.x = std::move(values);
    result.residuals = std::move(current.residuals);
    result.final_objective = squared_norm(result.residuals);
    result.success = true;
    result.message = "ok";
    return result;
}

}  // namespace thermox
