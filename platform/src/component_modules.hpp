#pragma once

#include "thermox/platform/component_registry.hpp"

namespace thermox::platform {

void register_boundary_component_models(ComponentRegistry& registry);
void register_storage_component_models(ComponentRegistry& registry);
void register_turbomachinery_component_models(
    ComponentRegistry& registry);
void register_material_turbomachinery_component_models(
    ComponentRegistry& registry);
void register_transport_component_models(ComponentRegistry& registry);
void register_heat_transfer_component_models(
    ComponentRegistry& registry);
void register_fluid_inventory_component_models(
    ComponentRegistry& registry);
void register_power_component_models(ComponentRegistry& registry);
void register_combustion_component_models(
    ComponentRegistry& registry);
void validate_component_descriptor(
    const ComponentModelDescriptor& descriptor);
void validate_component_parameters(
    const ComponentDefinition& component,
    const ComponentModelDescriptor& descriptor);
const ParameterModelDescriptor*
find_component_parameter_descriptor(
    const ComponentModelDescriptor& descriptor,
    const std::string& parameter_name);

}  // namespace thermox::platform
