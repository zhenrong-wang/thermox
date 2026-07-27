#pragma once

#include "thermox/nonlinear_solver.hpp"
#include "thermox/variable_registry.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace thermox {

using EquationCallback = std::function<double(const std::vector<double>& x)>;

struct EquationPartial {
    std::size_t variable{0};
    double derivative{0.0};
};

using SparseEquationCallback =
    std::function<double(const std::vector<double>& x, std::vector<EquationPartial>& jacobian_row)>;

struct LinearTerm {
    std::size_t variable{0};
    double coefficient{0.0};
};

struct Equation {
    std::size_t index{0};
    std::string name;
    double scale{1.0};
    EquationCallback evaluate;
    SparseEquationCallback assemble_sparse;
    std::vector<std::size_t> sparsity_variables;
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
    std::size_t add_sparse_equation(std::string name,
                                    SparseEquationCallback assemble,
                                    double scale = 1.0);
    std::size_t add_sparse_equation(std::string name,
                                    std::vector<std::size_t> sparsity_variables,
                                    SparseEquationCallback assemble,
                                    double scale = 1.0);
    std::size_t add_linear_equation(std::string name,
                                    std::vector<LinearTerm> terms,
                                    double rhs,
                                    double scale = 1.0);

    const std::vector<Variable>& variables() const { return registry_.variables(); }
    const std::vector<ResidualDescriptor>& residuals() const { return registry_.residuals(); }
    const std::vector<Equation>& equations() const { return equations_; }

    NonlinearProblem build() const;

private:
    VariableRegistry registry_;
    std::vector<Equation> equations_;
};

}  // namespace thermox
