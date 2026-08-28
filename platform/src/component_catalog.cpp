#include "component_modules.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace thermox::platform {

namespace {

bool is_parameter_template(const std::string& name) {
    const auto open = name.find('{');
    const auto close =
        open == std::string::npos
            ? std::string::npos
            : name.find('}', open + 1);
    return open != std::string::npos &&
        close != std::string::npos &&
        close > open + 1 &&
        name.find('{', open + 1) == std::string::npos &&
        name.find('}', close + 1) == std::string::npos;
}

bool matches_parameter_template(
    const std::string& pattern,
    const std::string& candidate) {
    if (!is_parameter_template(pattern)) return false;
    const auto open = pattern.find('{');
    const auto close = pattern.find('}', open + 1);
    const auto prefix = pattern.substr(0, open);
    const auto suffix = pattern.substr(close + 1);
    return candidate.size() >
            prefix.size() + suffix.size() &&
        candidate.starts_with(prefix) &&
        candidate.ends_with(suffix);
}

}  // namespace

const ParameterModelDescriptor*
find_component_parameter_descriptor(
    const ComponentModelDescriptor& descriptor,
    const std::string& parameter_name) {
    const auto exact = std::find_if(
        descriptor.parameters.begin(),
        descriptor.parameters.end(),
        [&](const auto& candidate) {
            return candidate.name == parameter_name;
        });
    if (exact != descriptor.parameters.end()) return &*exact;
    const auto templated = std::find_if(
        descriptor.parameters.begin(),
        descriptor.parameters.end(),
        [&](const auto& candidate) {
            return matches_parameter_template(
                candidate.name, parameter_name);
        });
    return templated == descriptor.parameters.end()
        ? nullptr
        : &*templated;
}

void validate_component_descriptor(
    const ComponentModelDescriptor& descriptor) {
    if (descriptor.version.empty()) {
        throw std::invalid_argument(
            "component model '" + descriptor.kind +
            "' must declare a version");
    }
    if (!descriptor.system_boundary_role.empty() &&
        descriptor.system_boundary_role != "source" &&
        descriptor.system_boundary_role != "sink") {
        throw std::invalid_argument(
            "component model '" + descriptor.kind +
            "' has invalid system boundary role: " +
            descriptor.system_boundary_role);
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
        if (port.direction != "in" &&
            port.direction != "out" &&
            port.direction != "bidirectional") {
            throw std::invalid_argument(
                "component model '" + descriptor.kind +
                "' has invalid port direction: " +
                port.direction);
        }
        if (!ports.emplace(port.name, true).second) {
            throw std::invalid_argument(
                "component model '" + descriptor.kind +
                "' declares duplicate port: " + port.name);
        }
    }
    for (const auto& port : descriptor.ports) {
        if (port.medium_source_port.empty()) continue;
        if (port.domain != "inventory") {
            throw std::invalid_argument(
                "component model '" + descriptor.kind +
                "' declares a medium source on non-inventory port: " +
                port.name);
        }
        const auto source = std::find_if(
            descriptor.ports.begin(), descriptor.ports.end(),
            [&](const auto& candidate) {
                return candidate.name == port.medium_source_port;
            });
        if (source == descriptor.ports.end() ||
            source->domain != "fluid") {
            throw std::invalid_argument(
                "component model '" + descriptor.kind +
                "' inventory port '" + port.name +
                "' references a non-fluid medium source: " +
                port.medium_source_port);
        }
    }
    std::map<std::string, bool> port_groups;
    for (const auto& group : descriptor.port_groups) {
        if (group.name.empty() || group.port_name_prefix.empty() ||
            group.domain.empty() || group.direction.empty() ||
            group.minimum_count > group.maximum_count ||
            group.maximum_connections == 0U) {
            throw std::invalid_argument(
                "component model '" + descriptor.kind +
                "' has an incomplete port-group descriptor");
        }
        if (group.direction != "in" && group.direction != "out" &&
            group.direction != "bidirectional") {
            throw std::invalid_argument(
                "component model '" + descriptor.kind +
                "' has invalid port-group direction: " +
                group.direction);
        }
        if (!port_groups.emplace(group.name, true).second) {
            throw std::invalid_argument(
                "component model '" + descriptor.kind +
                "' declares duplicate port group: " + group.name);
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
        const bool contains_brace =
            parameter.name.find('{') != std::string::npos ||
            parameter.name.find('}') != std::string::npos;
        if (contains_brace &&
            !is_parameter_template(parameter.name)) {
            throw std::logic_error(
                "component model '" + descriptor.kind +
                "' declares an invalid parameter template: " +
                parameter.name);
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
        if (is_parameter_template(parameter.name)) continue;
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

    for (const auto& [name, scalar] : component.parameters) {
        const auto* parameter =
            find_component_parameter_descriptor(
                descriptor, name);
        if (parameter == nullptr) {
            throw std::invalid_argument(
                "component '" + component.id +
                "' supplies unknown parameter for kind '" +
                descriptor.kind + "': " + name);
        }
        if (declared.contains(name)) continue;
        if (scalar.dimension != "dimensionless" &&
            scalar.dimension != parameter->dimension) {
            throw std::invalid_argument(
                "component '" + component.id +
                "' parameter '" + name +
                "' requires dimension '" +
                parameter->dimension + "' but received '" +
                scalar.dimension + "'");
        }
        const double value = scalar.value_si;
        if (!std::isfinite(value)) {
            throw std::invalid_argument(
                "component '" + component.id +
                "' parameter '" + name + "' must be finite");
        }
        const bool below =
            parameter->lower_inclusive
                ? value < parameter->lower_bound
                : value <= parameter->lower_bound;
        const bool above =
            parameter->upper_inclusive
                ? value > parameter->upper_bound
                : value >= parameter->upper_bound;
        if (below || above) {
            throw std::invalid_argument(
                "component '" + component.id +
                "' parameter '" + name +
                "' is outside its declared bounds");
        }
    }
}

}  // namespace thermox::platform
