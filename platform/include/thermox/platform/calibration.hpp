#pragma once

#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/model_document.hpp"

namespace thermox::platform {

// Validates calibration observations against the result fields exposed by
// the registered component instances. Parameter targets are validated while
// parsing the model document because component/connection owners and explicit
// case boundary values carry their dimensions in the document itself.
void validate_calibration_observation_contracts(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry_registry);

ScalarValue& require_calibration_parameter_target(
    ModelDocument& document,
    const std::string& target);
const ScalarValue& require_calibration_parameter_target(
    const ModelDocument& document,
    const std::string& target);

}  // namespace thermox::platform
