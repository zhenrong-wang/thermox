#include "thermox/variable_registry.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace thermox {

namespace {

void validate_variable_inputs(double initial_value,
                              double scale,
                              double lower_bound,
                              double upper_bound) {
    if (!std::isfinite(initial_value)) {
        throw std::invalid_argument("variable initial value must be finite");
    }
    if (!std::isfinite(scale) || scale <= 0.0) {
        throw std::invalid_argument("variable scale must be positive and finite");
    }
    if (std::isnan(lower_bound) || std::isnan(upper_bound)) {
        throw std::invalid_argument("variable bounds must not be NaN");
    }
    if (lower_bound > upper_bound) {
        throw std::invalid_argument("variable lower bound exceeds upper bound");
    }
    if (initial_value < lower_bound || initial_value > upper_bound) {
        throw std::invalid_argument("variable initial value is outside bounds");
    }
}

}  // namespace

std::size_t VariableRegistry::add_variable(std::string name, double initial_value, double scale) {
    return add_variable(std::move(name), initial_value, scale,
                        -std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::infinity());
}

std::size_t VariableRegistry::add_variable(std::string name,
                                           double initial_value,
                                           double scale,
                                           double lower_bound,
                                           double upper_bound) {
    if (name.empty()) {
        throw std::invalid_argument("variable name must not be empty");
    }
    if (std::any_of(variables_.begin(), variables_.end(), [&](const Variable& variable) {
            return variable.name == name;
        })) {
        throw std::invalid_argument("duplicate variable name: " + name);
    }
    validate_variable_inputs(initial_value, scale, lower_bound, upper_bound);
    const std::size_t index = variables_.size();
    variables_.push_back(Variable{index, std::move(name), initial_value, scale, lower_bound,
                                  upper_bound});
    return index;
}

std::size_t VariableRegistry::add_residual(std::string name, double scale) {
    if (name.empty()) {
        throw std::invalid_argument("residual name must not be empty");
    }
    if (std::any_of(residuals_.begin(), residuals_.end(),
                    [&](const ResidualDescriptor& residual) {
                        return residual.name == name;
                    })) {
        throw std::invalid_argument("duplicate residual name: " + name);
    }
    if (!std::isfinite(scale) || scale <= 0.0) {
        throw std::invalid_argument("residual scale must be positive and finite");
    }
    const std::size_t index = residuals_.size();
    residuals_.push_back(ResidualDescriptor{index, std::move(name), scale});
    return index;
}

std::vector<std::string> VariableRegistry::variable_names() const {
    std::vector<std::string> names;
    names.reserve(variables_.size());
    for (const auto& variable : variables_) {
        names.push_back(variable.name);
    }
    return names;
}

std::vector<std::string> VariableRegistry::residual_names() const {
    std::vector<std::string> names;
    names.reserve(residuals_.size());
    for (const auto& residual : residuals_) {
        names.push_back(residual.name);
    }
    return names;
}

std::vector<double> VariableRegistry::initial_guess() const {
    std::vector<double> guess;
    guess.reserve(variables_.size());
    for (const auto& variable : variables_) {
        guess.push_back(variable.initial_value);
    }
    return guess;
}

std::vector<double> VariableRegistry::variable_scales() const {
    std::vector<double> scales;
    scales.reserve(variables_.size());
    for (const auto& variable : variables_) {
        scales.push_back(variable.scale);
    }
    return scales;
}

std::vector<double> VariableRegistry::residual_scales() const {
    std::vector<double> scales;
    scales.reserve(residuals_.size());
    for (const auto& residual : residuals_) {
        scales.push_back(residual.scale);
    }
    return scales;
}

std::vector<double> VariableRegistry::lower_bounds() const {
    std::vector<double> bounds;
    bounds.reserve(variables_.size());
    for (const auto& variable : variables_) {
        bounds.push_back(variable.lower_bound);
    }
    return bounds;
}

std::vector<double> VariableRegistry::upper_bounds() const {
    std::vector<double> bounds;
    bounds.reserve(variables_.size());
    for (const auto& variable : variables_) {
        bounds.push_back(variable.upper_bound);
    }
    return bounds;
}

}  // namespace thermox
