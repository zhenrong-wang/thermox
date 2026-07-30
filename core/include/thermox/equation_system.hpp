#pragma once

#include "thermox/nonlinear_solver.hpp"
#include "thermox/variable_registry.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace thermox {

using EquationCallback = std::function<double(const std::vector<double>& x)>;
using CheckedEquationCallback =
    std::function<EvaluationStatus(const std::vector<double>& x, double& residual)>;
using ContinuationCheckedEquationCallback =
    std::function<EvaluationStatus(
        const std::vector<double>& x,
        const std::vector<double>& anchor,
        double parameter,
        double& residual)>;

struct EquationPartial {
    std::size_t variable{0};
    double derivative{0.0};
};

using SparseEquationCallback =
    std::function<double(const std::vector<double>& x, std::vector<EquationPartial>& jacobian_row)>;
using ContinuationSparseEquationCallback =
    std::function<double(
        const std::vector<double>& x,
        const std::vector<double>& anchor,
        double parameter,
        std::vector<EquationPartial>& jacobian_row)>;

struct LinearTerm {
    std::size_t variable{0};
    double coefficient{0.0};
};

struct Equation {
    std::size_t index{0};
    std::string name;
    double scale{1.0};
    EquationCallback evaluate;
    CheckedEquationCallback evaluate_checked;
    SparseEquationCallback assemble_sparse;
    std::vector<std::size_t> sparsity_variables;
    ContinuationCheckedEquationCallback
        evaluate_continuation_checked;
    ContinuationSparseEquationCallback
        assemble_continuation_sparse;
};

enum class LinearEquationRelation {
    independent,
    redundant,
    inconsistent,
};

class EquationSystemBuilder {
public:
    std::size_t add_variable(std::string name, double initial_value, double scale = 1.0);
    std::size_t add_variable(std::string name,
                             double initial_value,
                             double scale,
                             double lower_bound,
                             double upper_bound);
    std::size_t add_equation(std::string name, EquationCallback evaluate, double scale = 1.0);
    std::size_t add_checked_equation(std::string name,
                                     CheckedEquationCallback evaluate,
                                     double scale = 1.0);
    std::size_t add_continuation_checked_equation(
        std::string name,
        ContinuationCheckedEquationCallback evaluate,
        double scale = 1.0);
    std::size_t add_checked_sparse_equation(
        std::string name,
        CheckedEquationCallback evaluate,
        SparseEquationCallback assemble,
        double scale = 1.0);
    std::size_t add_checked_sparse_equation(
        std::string name,
        CheckedEquationCallback evaluate,
        std::vector<std::size_t> sparsity_variables,
        SparseEquationCallback assemble,
        double scale = 1.0);
    std::size_t add_continuation_checked_sparse_equation(
        std::string name,
        ContinuationCheckedEquationCallback evaluate,
        std::vector<std::size_t> sparsity_variables,
        ContinuationSparseEquationCallback assemble,
        double scale = 1.0);
    std::size_t add_sparse_equation(std::string name,
                                    SparseEquationCallback assemble,
                                    double scale = 1.0);
    std::size_t add_sparse_equation(std::string name,
                                    std::vector<std::size_t> sparsity_variables,
                                    SparseEquationCallback assemble,
                                    double scale = 1.0);
    std::size_t add_continuation_sparse_equation(
        std::string name,
        ContinuationSparseEquationCallback assemble,
        double scale = 1.0);
    std::size_t add_continuation_sparse_equation(
        std::string name,
        std::vector<std::size_t> sparsity_variables,
        ContinuationSparseEquationCallback assemble,
        double scale = 1.0);
    std::size_t add_linear_equation(std::string name,
                                    std::vector<LinearTerm> terms,
                                    double rhs,
                                    double scale = 1.0);
    std::size_t add_continuation_linear_equation(
        std::string name,
        std::vector<LinearTerm> target_terms,
        double target_rhs,
        ContinuationSparseEquationCallback assemble,
        double scale = 1.0);
    [[nodiscard]] LinearEquationRelation classify_linear_equation(
        const std::vector<LinearTerm>& terms,
        double rhs,
        double tolerance = 1.0e-10) const;

    const std::vector<Variable>& variables() const { return registry_.variables(); }
    const std::vector<ResidualDescriptor>& residuals() const { return registry_.residuals(); }
    const std::vector<Equation>& equations() const { return equations_; }

    NonlinearProblem build() const;

private:
    struct LinearBasisRow {
        std::size_t pivot{0};
        std::map<std::size_t, double> coefficients;
        double rhs{0.0};
    };
    struct LinearReduction {
        LinearEquationRelation relation{
            LinearEquationRelation::independent};
        std::size_t pivot{0};
        std::map<std::size_t, double> coefficients;
        double rhs{0.0};
    };

    [[nodiscard]] LinearReduction reduce_linear_equation(
        const std::vector<LinearTerm>& terms,
        double rhs,
        double tolerance) const;
    void record_linear_equation_if_independent(
        const std::vector<LinearTerm>& terms,
        double rhs);

    VariableRegistry registry_;
    std::vector<Equation> equations_;
    std::vector<LinearBasisRow> linear_basis_;
};

}  // namespace thermox
