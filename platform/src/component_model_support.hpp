#pragma once

#include "thermox/platform/component_registry.hpp"

#include <memory>
#include <stdexcept>

namespace thermox::platform::component_model_support {

inline double required_parameter(
    const ComponentDefinition& component,
    const std::string& name) {
    const auto it = component.parameters.find(name);
    if (it == component.parameters.end()) {
        throw std::invalid_argument(
            "component '" + component.id +
            "' is missing required parameter: " + name);
    }
    return it->second.value_si;
}

inline std::size_t require_port_variable(
    const ComponentCompileContext& context,
    const std::string& key) {
    const auto it = context.port_variables.find(key);
    if (it == context.port_variables.end()) {
        throw std::logic_error(
            "compiled component variable missing: " +
            context.component.id + "." + key);
    }
    return it->second;
}

inline std::size_t require_internal_variable(
    const ComponentCompileContext& context,
    const std::string& name) {
    const auto it = context.internal_variables.find(name);
    if (it == context.internal_variables.end()) {
        throw std::logic_error(
            "compiled component internal variable missing: " +
            context.component.id + "." + name);
    }
    return it->second;
}

inline std::shared_ptr<const physics::PropertyPackage>
require_property_package(
    const ComponentCompileContext& context,
    const std::string& port) {
    const auto it = context.port_properties.find(port);
    if (it == context.port_properties.end() || !it->second) {
        throw std::logic_error(
            "compiled property package missing: " +
            context.component.id + "." + port);
    }
    return it->second;
}

inline EvaluationStatus property_failure(
    const physics::PropertyResult& result) {
    if (result.status == physics::PropertyStatus::backend_error) {
        return EvaluationStatus::fatal(result.message);
    }
    return EvaluationStatus::recoverable(result.message);
}

inline EvaluationStatus property_failure(
    const physics::SaturationResult& result) {
    if (result.status == physics::PropertyStatus::backend_error) {
        return EvaluationStatus::fatal(result.message);
    }
    return EvaluationStatus::recoverable(result.message);
}

}  // namespace thermox::platform::component_model_support
