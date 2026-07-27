#include "thermox/dae_equation_system.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace thermox {

std::size_t DaeEquationSystemBuilder::add_variable(
    std::string name,
    DaeVariableKind kind,
    double initial_state,
    double initial_derivative,
    double state_scale,
    double derivative_scale) {
    return add_variable(std::move(name), kind, initial_state, initial_derivative,
                        state_scale, derivative_scale,
                        -std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::infinity());
}

std::size_t DaeEquationSystemBuilder::add_variable(
    std::string name,
    DaeVariableKind kind,
    double initial_state,
    double initial_derivative,
    double state_scale,
    double derivative_scale,
    double lower_bound,
    double upper_bound) {
    if (!std::isfinite(initial_derivative)) {
        throw std::invalid_argument("DAE variable initial derivative must be finite");
    }
    if (!std::isfinite(derivative_scale) || derivative_scale <= 0.0) {
        throw std::invalid_argument("DAE variable derivative scale must be positive and finite");
    }
    const std::size_t index = registry_.add_variable(
        std::move(name), initial_state, state_scale, lower_bound, upper_bound);
    variable_kinds_.push_back(kind);
    initial_derivatives_.push_back(initial_derivative);
    derivative_scales_.push_back(derivative_scale);
    return index;
}

std::size_t DaeEquationSystemBuilder::add_equation(
    std::string name,
    DaeEquationCallback evaluate,
    double scale) {
    if (!evaluate) {
        throw std::invalid_argument("DAE equation callback must not be empty");
    }
    return add_checked_equation(
        std::move(name),
        [evaluate = std::move(evaluate)](
            double time,
            const std::vector<double>& state,
            const std::vector<double>& derivative,
            double& residual) {
            residual = evaluate(time, state, derivative);
            return EvaluationStatus::success();
        },
        scale);
}

std::size_t DaeEquationSystemBuilder::add_checked_equation(
    std::string name,
    CheckedDaeEquationCallback evaluate,
    double scale) {
    if (!evaluate) {
        throw std::invalid_argument("checked DAE equation callback must not be empty");
    }
    const std::size_t index = registry_.add_residual(name, scale);
    equations_.push_back(
        DaeEquation{index, std::move(name), scale, std::move(evaluate), {}, {}});
    return index;
}

std::size_t DaeEquationSystemBuilder::add_sparse_equation(
    std::string name,
    std::vector<std::size_t> sparsity_variables,
    SparseDaeEquationCallback assemble,
    double scale) {
    if (!assemble) {
        throw std::invalid_argument("sparse DAE equation callback must not be empty");
    }
    std::sort(sparsity_variables.begin(), sparsity_variables.end());
    sparsity_variables.erase(
        std::unique(sparsity_variables.begin(), sparsity_variables.end()),
        sparsity_variables.end());
    if (sparsity_variables.empty()) {
        throw std::invalid_argument("sparse DAE equation pattern must not be empty");
    }
    for (const std::size_t variable : sparsity_variables) {
        if (variable >= registry_.variables().size()) {
            throw std::invalid_argument("sparse DAE equation variable index out of range");
        }
    }

    SparseDaeEquationCallback sparse = std::move(assemble);
    CheckedDaeEquationCallback evaluate =
        [sparse](double time,
                 const std::vector<double>& state,
                 const std::vector<double>& derivative,
                 double& residual) {
            std::vector<DaeEquationPartial> ignored;
            return sparse(time, state, derivative, residual, ignored);
        };
    const std::size_t index = registry_.add_residual(name, scale);
    equations_.push_back(DaeEquation{index, std::move(name), scale, std::move(evaluate),
                                     std::move(sparse), std::move(sparsity_variables)});
    return index;
}

std::size_t DaeEquationSystemBuilder::add_linear_equation(
    std::string name,
    std::vector<DaeLinearTerm> terms,
    double rhs,
    double scale) {
    if (!std::isfinite(rhs)) {
        throw std::invalid_argument("linear DAE equation rhs must be finite");
    }
    if (terms.empty()) {
        throw std::invalid_argument("linear DAE equation must contain at least one term");
    }
    std::vector<std::size_t> sparsity;
    sparsity.reserve(terms.size());
    for (const auto& term : terms) {
        if (term.variable >= registry_.variables().size()) {
            throw std::invalid_argument("linear DAE equation variable index out of range");
        }
        if (!std::isfinite(term.state_coefficient) ||
            !std::isfinite(term.derivative_coefficient)) {
            throw std::invalid_argument("linear DAE equation coefficients must be finite");
        }
        sparsity.push_back(term.variable);
    }
    return add_sparse_equation(
        std::move(name), std::move(sparsity),
        [terms = std::move(terms), rhs](
            double,
            const std::vector<double>& state,
            const std::vector<double>& derivative,
            double& residual,
            std::vector<DaeEquationPartial>& jacobian_row) {
            residual = -rhs;
            for (const auto& term : terms) {
                residual += term.state_coefficient * state.at(term.variable) +
                            term.derivative_coefficient * derivative.at(term.variable);
                jacobian_row.push_back(DaeEquationPartial{
                    term.variable, term.state_coefficient, term.derivative_coefficient});
            }
            return EvaluationStatus::success();
        },
        scale);
}

DaeProblem DaeEquationSystemBuilder::build() const {
    const std::size_t variable_count = registry_.variables().size();
    if (variable_count == 0) {
        throw std::invalid_argument("DAE equation system must contain at least one variable");
    }
    if (equations_.size() != variable_count) {
        throw std::invalid_argument(
            "DAE equation system must be square: variables=" +
            std::to_string(variable_count) + " equations=" +
            std::to_string(equations_.size()));
    }

    DaeProblem problem;
    problem.variable_names = registry_.variable_names();
    problem.residual_names = registry_.residual_names();
    problem.variable_kinds = variable_kinds_;
    problem.initial_state = registry_.initial_guess();
    problem.initial_derivative = initial_derivatives_;
    problem.variable_scales = registry_.variable_scales();
    problem.derivative_scales = derivative_scales_;
    problem.residual_scales = registry_.residual_scales();
    problem.lower_bounds = registry_.lower_bounds();
    problem.upper_bounds = registry_.upper_bounds();

    const auto equations = equations_;
    problem.residual =
        [equations, variable_count](
            double time,
            const std::vector<double>& state,
            const std::vector<double>& derivative,
            std::vector<double>& residual) {
            if (state.size() != variable_count ||
                derivative.size() != variable_count ||
                residual.size() != equations.size()) {
                return EvaluationStatus::fatal(
                    "vector size does not match DAE equation system");
            }
            for (const auto& equation : equations) {
                auto status = equation.evaluate(
                    time, state, derivative, residual.at(equation.index));
                if (!status.ok()) {
                    status.message = equation.name + ": " + status.message;
                    return status;
                }
            }
            return EvaluationStatus::success();
        };

    const bool fixed_sparse =
        std::all_of(equations.begin(), equations.end(), [](const auto& equation) {
            return static_cast<bool>(equation.assemble_sparse) &&
                   !equation.sparsity_variables.empty();
        });
    if (!fixed_sparse) {
        return problem;
    }

    std::vector<std::size_t> row_offsets{0};
    std::vector<std::size_t> column_indices;
    for (const auto& equation : equations) {
        column_indices.insert(column_indices.end(),
                              equation.sparsity_variables.begin(),
                              equation.sparsity_variables.end());
        row_offsets.push_back(column_indices.size());
    }
    problem.sparse_jacobian_pattern =
        SparsePattern(equations.size(), variable_count, std::move(row_offsets),
                      std::move(column_indices));
    const SparsePattern pattern = *problem.sparse_jacobian_pattern;
    problem.sparse_jacobian_values =
        [equations, pattern](
            double time,
            const std::vector<double>& state,
            const std::vector<double>& derivative,
            double derivative_coefficient,
            std::vector<double>& values) {
            std::fill(values.begin(), values.end(), 0.0);
            for (const auto& equation : equations) {
                double ignored_residual = 0.0;
                std::vector<DaeEquationPartial> partials;
                auto status = equation.assemble_sparse(
                    time, state, derivative, ignored_residual, partials);
                if (!status.ok()) {
                    status.message = equation.name + ": " + status.message;
                    return status;
                }
                for (const auto& partial : partials) {
                    const auto begin =
                        pattern.column_indices().begin() +
                        static_cast<std::ptrdiff_t>(
                            pattern.row_offsets().at(equation.index));
                    const auto end =
                        pattern.column_indices().begin() +
                        static_cast<std::ptrdiff_t>(
                            pattern.row_offsets().at(equation.index + 1));
                    const auto position =
                        std::lower_bound(begin, end, partial.variable);
                    if (position == end || *position != partial.variable) {
                        return EvaluationStatus::fatal(
                            equation.name +
                            ": derivative emitted outside declared sparse pattern");
                    }
                    values.at(static_cast<std::size_t>(
                        std::distance(pattern.column_indices().begin(), position))) +=
                        partial.state_derivative +
                        derivative_coefficient *
                            partial.state_rate_derivative;
                }
            }
            return EvaluationStatus::success();
        };
    return problem;
}

}  // namespace thermox
