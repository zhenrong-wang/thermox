#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace thermox {

struct Variable {
    std::size_t index{0};
    std::string name;
    double initial_value{0.0};
    double scale{1.0};
    double lower_bound{-std::numeric_limits<double>::infinity()};
    double upper_bound{std::numeric_limits<double>::infinity()};
};

struct ResidualDescriptor {
    std::size_t index{0};
    std::string name;
    double scale{1.0};
};

class VariableRegistry {
public:
    std::size_t add_variable(std::string name, double initial_value, double scale = 1.0);
    std::size_t add_variable(std::string name,
                             double initial_value,
                             double scale,
                             double lower_bound,
                             double upper_bound);
    std::size_t add_residual(std::string name, double scale = 1.0);

    const std::vector<Variable>& variables() const { return variables_; }
    const std::vector<ResidualDescriptor>& residuals() const { return residuals_; }

    std::vector<std::string> variable_names() const;
    std::vector<std::string> residual_names() const;
    std::vector<double> initial_guess() const;
    std::vector<double> variable_scales() const;
    std::vector<double> residual_scales() const;
    std::vector<double> lower_bounds() const;
    std::vector<double> upper_bounds() const;

private:
    std::vector<Variable> variables_;
    std::vector<ResidualDescriptor> residuals_;
};

}  // namespace thermox
