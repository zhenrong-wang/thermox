#include "component_modules.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace thermox::platform {

void validate_component_descriptor(
    const ComponentModelDescriptor& descriptor) {
    if (descriptor.version.empty()) {
        throw std::invalid_argument(
            "component model '" + descriptor.kind +
            "' must declare a version");
    }
    std::map<std::string, bool> ports;
    for (const auto& port : descriptor.ports) {
        if (port.name.empty() || port.domain.empty() ||
            port.direction.empty() ||
            port.maximum_connections == 0) {
            throw std::invalid_argument(
                "component model '" + descriptor.kind +
                "' has an incomplete port descriptor");
        }
        if (!ports.emplace(port.name, true).second) {
            throw std::invalid_argument(
                "component model '" + descriptor.kind +
                "' declares duplicate port: " + port.name);
        }
    }
    const auto has_domain = [&](const std::string& domain) {
        return std::any_of(
            descriptor.ports.begin(), descriptor.ports.end(),
            [&](const auto& port) {
                return port.domain == domain;
            });
    };
    if (!descriptor.required_property_capabilities.empty() &&
        !has_domain("fluid")) {
        throw std::logic_error(
            "component model '" + descriptor.kind +
            "' requests fluid property capabilities without a "
            "fluid port");
    }
    if (!descriptor.required_thermochemistry_capabilities.empty() &&
        !has_domain("material")) {
        throw std::logic_error(
            "component model '" + descriptor.kind +
            "' requests thermochemistry capabilities without a "
            "material port");
    }
    std::map<std::string, const ParameterModelDescriptor*> declared;
    for (const auto& parameter : descriptor.parameters) {
        if (parameter.name.empty()) {
            throw std::logic_error(
                "component model '" + descriptor.kind +
                "' declares an empty parameter name");
        }
        if (!declared.emplace(parameter.name, &parameter).second) {
            throw std::logic_error(
                "component model '" + descriptor.kind +
                "' declares duplicate parameter: " + parameter.name);
        }
        if (!parameter.required &&
            !parameter.default_value.has_value()) {
            throw std::logic_error(
                "optional component parameter requires a default: " +
                descriptor.kind + "." + parameter.name);
        }
        if (parameter.lower_bound > parameter.upper_bound) {
            throw std::logic_error(
                "component parameter has invalid bounds: " +
                descriptor.kind + "." + parameter.name);
        }
    }
    std::map<std::string, bool> artifacts;
    for (const auto& artifact : descriptor.artifacts) {
        if (artifact.role.empty() ||
            artifact.artifact_type.empty()) {
            throw std::logic_error(
                "component model '" + descriptor.kind +
                "' has an incomplete artifact descriptor");
        }
        if (!artifacts.emplace(artifact.role, true).second) {
            throw std::logic_error(
                "component model '" + descriptor.kind +
                "' declares duplicate artifact role: " +
                artifact.role);
        }
        if (artifact.artifact_type !=
            performance_map_artifact_type) {
            throw std::logic_error(
                "component model '" + descriptor.kind +
                "' declares unsupported artifact type: " +
                artifact.artifact_type);
        }
    }
    std::map<std::string, bool> internal_variables;
    for (const auto& variable : descriptor.internal_variables) {
        if (variable.name.empty() || variable.dimension.empty()) {
            throw std::logic_error(
                "component model '" + descriptor.kind +
                "' has an incomplete internal variable descriptor");
        }
        if (!internal_variables.emplace(
                variable.name, true).second) {
            throw std::logic_error(
                "component model '" + descriptor.kind +
                "' declares duplicate internal variable: " +
                variable.name);
        }
    }
}

void validate_component_parameters(
    const ComponentDefinition& component,
    const ComponentModelDescriptor& descriptor) {
    std::map<std::string, const ParameterModelDescriptor*> declared;
    for (const auto& parameter : descriptor.parameters) {
        declared.emplace(parameter.name, &parameter);
    }
    for (const auto& parameter : descriptor.parameters) {
        const auto supplied =
            component.parameters.find(parameter.name);
        if (supplied == component.parameters.end()) {
            if (parameter.required) {
                throw std::invalid_argument(
                    "component '" + component.id +
                    "' is missing required parameter: " +
                    parameter.name);
            }
            continue;
        }
        const ScalarValue& scalar = supplied->second;
        if (scalar.dimension != "dimensionless" &&
            scalar.dimension != parameter.dimension) {
            throw std::invalid_argument(
                "component '" + component.id + "' parameter '" +
                parameter.name + "' requires dimension '" +
                parameter.dimension + "' but received '" +
                scalar.dimension + "'");
        }
        const double value = scalar.value_si;
        if (!std::isfinite(value)) {
            throw std::invalid_argument(
                "component '" + component.id + "' parameter '" +
                parameter.name + "' must be finite");
        }
        const bool below =
            parameter.lower_inclusive
                ? value < parameter.lower_bound
                : value <= parameter.lower_bound;
        const bool above =
            parameter.upper_inclusive
                ? value > parameter.upper_bound
                : value >= parameter.upper_bound;
        if (below || above) {
            throw std::invalid_argument(
                "component '" + component.id + "' parameter '" +
                parameter.name +
                "' is outside its declared bounds");
        }
    }

    for (const auto& [name, _] : component.parameters) {
        if (declared.find(name) == declared.end()) {
            throw std::invalid_argument(
                "component '" + component.id +
                "' supplies unknown parameter for kind '" +
                descriptor.kind + "': " + name);
        }
    }
}

}  // namespace thermox::platform
