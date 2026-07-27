#pragma once

#include "thermox/transient_solver.hpp"
#include "thermox/variable_registry.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace thermox {

using DaeEquationCallback =
    std::function<double(double time,
                         const std::vector<double>& state,
                         const std::vector<double>& derivative)>;
using CheckedDaeEquationCallback =
    std::function<EvaluationStatus(double time,
                                   const std::vector<double>& state,
                                   const std::vector<double>& derivative,
                                   double& residual)>;

struct DaeEquationPartial {
    std::size_t variable{0};
    double state_derivative{0.0};
    double state_rate_derivative{0.0};
};

using SparseDaeEquationCallback =
    std::function<EvaluationStatus(double time,
                                   const std::vector<double>& state,
                                   const std::vector<double>& derivative,
                                   double& residual,
                                   std::vector<DaeEquationPartial>& jacobian_row)>;

struct DaeLinearTerm {
    std::size_t variable{0};
    double state_coefficient{0.0};
    double derivative_coefficient{0.0};
};

struct DaeEquation {
    std::size_t index{0};
    std::string name;
    double scale{1.0};
    CheckedDaeEquationCallback evaluate;
    SparseDaeEquationCallback assemble_sparse;
    std::vector<std::size_t> sparsity_variables;
};

class DaeEquationSystemBuilder {
public:
    std::size_t add_variable(std::string name,
                             DaeVariableKind kind,
                             double initial_state,
                             double initial_derivative,
                             double state_scale = 1.0,
                             double derivative_scale = 1.0);
    std::size_t add_variable(std::string name,
                             DaeVariableKind kind,
                             double initial_state,
                             double initial_derivative,
                             double state_scale,
                             double derivative_scale,
                             double lower_bound,
                             double upper_bound);

    std::size_t add_equation(std::string name,
                             DaeEquationCallback evaluate,
                             double scale = 1.0);
    std::size_t add_checked_equation(std::string name,
                                     CheckedDaeEquationCallback evaluate,
                                     double scale = 1.0);
    std::size_t add_sparse_equation(std::string name,
                                    std::vector<std::size_t> sparsity_variables,
                                    SparseDaeEquationCallback assemble,
                                    double scale = 1.0);
    std::size_t add_linear_equation(std::string name,
                                    std::vector<DaeLinearTerm> terms,
                                    double rhs,
                                    double scale = 1.0);

    const std::vector<Variable>& variables() const { return registry_.variables(); }
    const std::vector<ResidualDescriptor>& residuals() const { return registry_.residuals(); }
    const std::vector<DaeEquation>& equations() const { return equations_; }

    DaeProblem build() const;

private:
    VariableRegistry registry_;
    std::vector<DaeVariableKind> variable_kinds_;
    std::vector<double> initial_derivatives_;
    std::vector<double> derivative_scales_;
    std::vector<DaeEquation> equations_;
};

}  // namespace thermox
