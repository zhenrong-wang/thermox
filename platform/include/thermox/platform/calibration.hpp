#pragma once

#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/model_document.hpp"

namespace thermox::platform {

// Validates calibration observations against the result fields exposed by
// the registered component instances. Parameter targets are validated while
// parsing the model document because their owners and dimensions are explicit
// in the document itself.
void validate_calibration_observation_contracts(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry_registry);

}  // namespace thermox::platform
