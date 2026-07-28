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

std::map<std::size_t, double> aggregate_terms(
    const std::vector<LinearTerm>& terms,
    std::size_t variable_count) {
    if (terms.empty()) {
        throw std::invalid_argument(
            "linear equation must contain at least one term");
    }
    std::map<std::size_t, double> coefficients;
    for (const auto& term : terms) {
        if (term.variable >= variable_count) {
            throw std::invalid_argument(
                "linear equation term variable index out of range");
        }
        if (!is_finite(term.coefficient)) {
            throw std::invalid_argument(
                "linear equation coefficients must be finite");
        }
        coefficients[term.variable] += term.coefficient;
    }
    for (auto it = coefficients.begin();
         it != coefficients.end();) {
        if (it->second == 0.0) {
            it = coefficients.erase(it);
        } else {
            ++it;
        }
    }
    return coefficients;
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

std::size_t EquationSystemBuilder::add_checked_sparse_equation(
    std::string name,
    CheckedEquationCallback evaluate,
    SparseEquationCallback assemble,
    double scale) {
    if (!evaluate || !assemble) {
        throw std::invalid_argument(
            "checked sparse equation callbacks must not be empty");
    }
    const std::size_t index =
        registry_.add_residual(name, scale);
    equations_.push_back(Equation{
        index, std::move(name), scale, {}, std::move(evaluate),
        std::move(assemble), {}});
    return index;
}

std::size_t EquationSystemBuilder::add_checked_sparse_equation(
    std::string name,
    CheckedEquationCallback evaluate,
    std::vector<std::size_t> sparsity_variables,
    SparseEquationCallback assemble,
    double scale) {
    if (!evaluate || !assemble) {
        throw std::invalid_argument(
            "checked sparse equation callbacks must not be empty");
    }
    const std::size_t variable_count =
        registry_.variables().size();
    std::sort(
        sparsity_variables.begin(), sparsity_variables.end());
    sparsity_variables.erase(
        std::unique(
            sparsity_variables.begin(),
            sparsity_variables.end()),
        sparsity_variables.end());
    for (const std::size_t variable : sparsity_variables) {
        if (variable >= variable_count) {
            throw std::invalid_argument(
                "checked sparse equation variable index out of range");
        }
    }
    if (sparsity_variables.empty()) {
        throw std::invalid_argument(
            "checked sparse equation pattern must not be empty");
    }
    const std::size_t index =
        registry_.add_residual(name, scale);
    equations_.push_back(Equation{
        index, std::move(name), scale, {}, std::move(evaluate),
        std::move(assemble), std::move(sparsity_variables)});
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
    record_linear_equation_if_independent(terms, rhs);

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

LinearEquationRelation EquationSystemBuilder::classify_linear_equation(
    const std::vector<LinearTerm>& terms,
    double rhs,
    double tolerance) const {
    if (!is_finite(rhs)) {
        throw std::invalid_argument(
            "linear equation rhs must be finite");
    }
    if (!is_finite(tolerance) || tolerance <= 0.0) {
        throw std::invalid_argument(
            "linear equation relation tolerance must be positive and finite");
    }
    return reduce_linear_equation(terms, rhs, tolerance).relation;
}

EquationSystemBuilder::LinearReduction
EquationSystemBuilder::reduce_linear_equation(
    const std::vector<LinearTerm>& terms,
    double rhs,
    double tolerance) const {
    auto coefficients =
        aggregate_terms(terms, registry_.variables().size());
    double coefficient_scale = 0.0;
    for (const auto& [_, coefficient] : coefficients) {
        coefficient_scale =
            std::max(coefficient_scale, std::abs(coefficient));
    }
    if (coefficient_scale == 0.0) {
        return {
            std::abs(rhs) <= tolerance
                ? LinearEquationRelation::redundant
                : LinearEquationRelation::inconsistent,
            0, {}, rhs};
    }
    for (auto& [_, coefficient] : coefficients) {
        coefficient /= coefficient_scale;
    }
    const double normalized_rhs = rhs / coefficient_scale;
    double reduced_rhs = normalized_rhs;

    for (const auto& basis : linear_basis_) {
        const auto pivot = coefficients.find(basis.pivot);
        if (pivot == coefficients.end()) {
            continue;
        }
        const double factor = pivot->second;
        coefficients.erase(pivot);
        for (const auto& [variable, coefficient] :
             basis.coefficients) {
            if (variable == basis.pivot) {
                continue;
            }
            coefficients[variable] -= factor * coefficient;
            if (std::abs(coefficients[variable]) <= tolerance) {
                coefficients.erase(variable);
            }
        }
        reduced_rhs -= factor * basis.rhs;
    }

    double remaining_scale = 0.0;
    for (const auto& [_, coefficient] : coefficients) {
        remaining_scale =
            std::max(remaining_scale, std::abs(coefficient));
    }
    if (remaining_scale > tolerance) {
        const auto pivot = std::max_element(
            coefficients.begin(), coefficients.end(),
            [](const auto& left, const auto& right) {
                return std::abs(left.second) <
                       std::abs(right.second);
            });
        if (pivot == coefficients.end()) {
            throw std::logic_error(
                "independent linear equation produced no basis pivot");
        }
        const std::size_t pivot_variable = pivot->first;
        const double pivot_coefficient = pivot->second;
        for (auto& [_, coefficient] : coefficients) {
            coefficient /= pivot_coefficient;
        }
        reduced_rhs /= pivot_coefficient;
        return {LinearEquationRelation::independent,
                pivot_variable, std::move(coefficients),
                reduced_rhs};
    }
    return {
        std::abs(reduced_rhs) <=
                tolerance * (1.0 + std::abs(normalized_rhs))
            ? LinearEquationRelation::redundant
            : LinearEquationRelation::inconsistent,
        0, {}, reduced_rhs};
}

void EquationSystemBuilder::record_linear_equation_if_independent(
    const std::vector<LinearTerm>& terms,
    double rhs) {
    constexpr double tolerance = 1.0e-10;
    auto reduced =
        reduce_linear_equation(terms, rhs, tolerance);
    if (reduced.relation !=
        LinearEquationRelation::independent) {
        return;
    }
    linear_basis_.push_back(
        LinearBasisRow{
            reduced.pivot, std::move(reduced.coefficients),
            reduced.rhs});
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
