#include "thermox/equation_system.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace thermox {

namespace {

bool is_finite(double value) {
    return std::isfinite(value);
}

}  // namespace

std::size_t EquationSystemBuilder::add_variable(std::string name,
                                                double initial_value,
                                                double scale) {
    return registry_.add_variable(std::move(name), initial_value, scale);
}

std::size_t EquationSystemBuilder::add_variable(std::string name,
                                                double initial_value,
                                                double scale,
                                                double lower_bound,
                                                double upper_bound) {
    return registry_.add_variable(std::move(name), initial_value, scale, lower_bound, upper_bound);
}

std::size_t EquationSystemBuilder::add_equation(std::string name,
                                                EquationCallback evaluate,
                                                double scale) {
    if (!evaluate) {
        throw std::invalid_argument("equation callback must not be empty");
    }
    const std::size_t index = registry_.add_residual(name, scale);
    equations_.push_back(
        Equation{index, std::move(name), scale, std::move(evaluate), {}, {}, {}});
    return index;
}

std::size_t EquationSystemBuilder::add_checked_equation(
    std::string name, CheckedEquationCallback evaluate, double scale) {
    if (!evaluate)
        throw std::invalid_argument("checked equation callback must not be empty");
    const std::size_t index = registry_.add_residual(name, scale);
    equations_.push_back(
        Equation{index, std::move(name), scale, {}, std::move(evaluate), {}, {}});
    return index;
}

std::size_t EquationSystemBuilder::add_sparse_equation(std::string name,
                                                       SparseEquationCallback assemble,
                                                       double scale) {
    if (!assemble) {
        throw std::invalid_argument("sparse equation callback must not be empty");
    }
    SparseEquationCallback sparse = std::move(assemble);
    EquationCallback evaluate = [sparse](const std::vector<double>& x) {
        std::vector<EquationPartial> ignored;
        return sparse(x, ignored);
    };
    const std::size_t index = registry_.add_residual(name, scale);
    equations_.push_back(Equation{index, std::move(name), scale, std::move(evaluate), {},
                                  std::move(sparse), {}});
    return index;
}

std::size_t EquationSystemBuilder::add_sparse_equation(
    std::string name,
    std::vector<std::size_t> sparsity_variables,
    SparseEquationCallback assemble,
    double scale) {
    if (!assemble) {
        throw std::invalid_argument("sparse equation callback must not be empty");
    }
    const std::size_t variable_count = registry_.variables().size();
    std::sort(sparsity_variables.begin(), sparsity_variables.end());
    sparsity_variables.erase(
        std::unique(sparsity_variables.begin(), sparsity_variables.end()),
        sparsity_variables.end());
    for (const std::size_t variable : sparsity_variables) {
        if (variable >= variable_count) {
            throw std::invalid_argument("sparse equation variable index out of range");
        }
    }
    if (sparsity_variables.empty()) {
        throw std::invalid_argument("sparse equation pattern must not be empty");
    }

    SparseEquationCallback sparse = std::move(assemble);
    EquationCallback evaluate = [sparse](const std::vector<double>& x) {
        std::vector<EquationPartial> ignored;
        return sparse(x, ignored);
    };
    const std::size_t index = registry_.add_residual(name, scale);
    equations_.push_back(Equation{index, std::move(name), scale, std::move(evaluate), {},
                                  std::move(sparse), std::move(sparsity_variables)});
    return index;
}

std::size_t EquationSystemBuilder::add_linear_equation(std::string name,
                                                       std::vector<LinearTerm> terms,
                                                       double rhs,
                                                       double scale) {
    if (!is_finite(rhs)) {
        throw std::invalid_argument("linear equation rhs must be finite");
    }
    if (terms.empty()) {
        throw std::invalid_argument("linear equation must contain at least one term");
    }
    const std::size_t variable_count = registry_.variables().size();
    for (const auto& term : terms) {
        if (term.variable >= variable_count) {
            throw std::invalid_argument("linear equation term variable index out of range");
        }
        if (!is_finite(term.coefficient)) {
            throw std::invalid_argument("linear equation coefficients must be finite");
        }
    }

    std::vector<std::size_t> sparsity_variables;
    sparsity_variables.reserve(terms.size());
    for (const auto& term : terms) {
        sparsity_variables.push_back(term.variable);
    }
    return add_sparse_equation(
        std::move(name),
        std::move(sparsity_variables),
        [terms = std::move(terms), rhs](const std::vector<double>& x,
                                        std::vector<EquationPartial>& jacobian_row) {
            double residual = -rhs;
            for (const auto& term : terms) {
                residual += term.coefficient * x.at(term.variable);
                jacobian_row.push_back(EquationPartial{term.variable, term.coefficient});
            }
            return residual;
        },
        scale);
}

NonlinearProblem EquationSystemBuilder::build() const {
    if (registry_.variables().empty()) {
        throw std::invalid_argument("equation system must contain at least one variable");
    }
    if (equations_.empty()) {
        throw std::invalid_argument("equation system must contain at least one equation");
    }
    if (equations_.size() != registry_.residuals().size()) {
        throw std::logic_error("equation and residual descriptor counts differ");
    }

    NonlinearProblem problem;
    problem.variable_names = registry_.variable_names();
    problem.residual_names = registry_.residual_names();
    problem.initial_guess = registry_.initial_guess();
    problem.variable_scales = registry_.variable_scales();
    problem.residual_scales = registry_.residual_scales();
    problem.lower_bounds = registry_.lower_bounds();
    problem.upper_bounds = registry_.upper_bounds();

    const auto equations = equations_;
    const std::size_t variable_count = registry_.variables().size();
    problem.residual = [equations, variable_count](const std::vector<double>& x,
                                                   std::vector<double>& residual) {
        if (x.size() != variable_count) {
            throw std::invalid_argument("variable vector size does not match equation system");
        }
        if (residual.size() != equations.size()) {
            throw std::invalid_argument("residual vector size does not match equation count");
        }
        for (const auto& equation : equations) {
            if (equation.evaluate) {
                residual.at(equation.index) = equation.evaluate(x);
            } else {
                const auto status =
                    equation.evaluate_checked(x, residual.at(equation.index));
                if (!status.ok())
                    throw std::runtime_error("checked equation evaluation failed: " +
                                             equation.name + ": " + status.message);
            }
        }
    };

    const bool has_checked_equations =
        std::any_of(equations.begin(), equations.end(), [](const auto& equation) {
            return static_cast<bool>(equation.evaluate_checked);
        });
    if (has_checked_equations) {
        problem.checked_residual =
            [equations, variable_count](const std::vector<double>& x,
                                        std::vector<double>& residual) {
                if (x.size() != variable_count)
                    return EvaluationStatus::fatal(
                        "variable vector size does not match equation system");
                if (residual.size() != equations.size())
                    return EvaluationStatus::fatal(
                        "residual vector size does not match equation count");
                for (const auto& equation : equations) {
                    if (equation.evaluate_checked) {
                        auto status =
                            equation.evaluate_checked(x, residual.at(equation.index));
                        if (!status.ok()) {
                            status.message = equation.name + ": " + status.message;
                            return status;
                        }
                    } else {
                        residual.at(equation.index) = equation.evaluate(x);
                    }
                }
                return EvaluationStatus::success();
            };
    }

    const bool can_assemble_sparse = std::all_of(equations.begin(), equations.end(), [](const auto& equation) {
        return static_cast<bool>(equation.assemble_sparse);
    });
    const bool has_fixed_sparse_pattern =
        can_assemble_sparse &&
        std::all_of(equations.begin(), equations.end(), [](const auto& equation) {
            return !equation.sparsity_variables.empty();
        });
    if (has_fixed_sparse_pattern) {
        std::vector<std::size_t> row_offsets;
        std::vector<std::size_t> column_indices;
        row_offsets.reserve(equations.size() + 1);
        row_offsets.push_back(0);
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
            [equations, pattern](const std::vector<double>& x, std::vector<double>& values) {
                std::fill(values.begin(), values.end(), 0.0);
                for (const auto& equation : equations) {
                    std::vector<EquationPartial> row_partials;
                    (void)equation.assemble_sparse(x, row_partials);
                    for (const auto& partial : row_partials) {
                        const auto begin = pattern.column_indices().begin() +
                                           static_cast<std::ptrdiff_t>(
                                               pattern.row_offsets()[equation.index]);
                        const auto end = pattern.column_indices().begin() +
                                         static_cast<std::ptrdiff_t>(
                                             pattern.row_offsets()[equation.index + 1]);
                        const auto it = std::lower_bound(begin, end, partial.variable);
                        if (it == end || *it != partial.variable) {
                            throw std::runtime_error(
                                "equation emitted derivative outside declared sparse pattern");
                        }
                        values[static_cast<std::size_t>(
                            std::distance(pattern.column_indices().begin(), it))] +=
                            partial.derivative;
                    }
                }
            };
    } else if (can_assemble_sparse) {
        problem.sparse_jacobian = [equations, variable_count](const std::vector<double>& x,
                                                              std::vector<SparseTriplet>& jacobian) {
            if (x.size() != variable_count) {
                throw std::invalid_argument("variable vector size does not match equation system");
            }
            for (const auto& equation : equations) {
                std::vector<EquationPartial> row_partials;
                (void)equation.assemble_sparse(x, row_partials);
                for (const auto& partial : row_partials) {
                    jacobian.push_back(SparseTriplet{equation.index, partial.variable, partial.derivative});
                }
            }
        };
    } else {
        const bool has_any_analytic =
            std::any_of(equations.begin(), equations.end(), [](const auto& equation) {
                return static_cast<bool>(equation.assemble_sparse);
            });
        if (has_any_analytic) {
            problem.analytic_jacobian_rows.reserve(equations.size());
            for (const auto& equation : equations) {
                problem.analytic_jacobian_rows.push_back(
                    static_cast<bool>(equation.assemble_sparse));
            }
            problem.partial_sparse_jacobian =
                [equations, variable_count](const std::vector<double>& x,
                                            std::vector<SparseTriplet>& jacobian) {
                    if (x.size() != variable_count) {
                        throw std::invalid_argument(
                            "variable vector size does not match equation system");
                    }
                    for (const auto& equation : equations) {
                        if (!equation.assemble_sparse) {
                            continue;
                        }
                        std::vector<EquationPartial> row_partials;
                        (void)equation.assemble_sparse(x, row_partials);
                        for (const auto& partial : row_partials) {
                            jacobian.push_back(SparseTriplet{
                                equation.index, partial.variable, partial.derivative});
                        }
                    }
                };
        }
    }

    return problem;
}

}  // namespace thermox
