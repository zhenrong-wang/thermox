#pragma once

#include "thermox/platform/component_registry.hpp"

namespace thermox::platform {

void register_boundary_component_models(ComponentRegistry& registry);
void register_storage_component_models(ComponentRegistry& registry);
void validate_component_descriptor(
    const ComponentModelDescriptor& descriptor);
void validate_component_parameters(
    const ComponentDefinition& component,
    const ComponentModelDescriptor& descriptor);

}  // namespace thermox::platform
