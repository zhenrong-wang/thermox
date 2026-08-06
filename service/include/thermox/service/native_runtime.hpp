#pragma once

#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/correlation.hpp"
#include "thermox/platform/unit_registry.hpp"
#include "thermox/physics/property_registry.hpp"
#include "thermox/physics/thermochemistry.hpp"
#include "thermox/service/simulation_runtime.hpp"

#include <functional>
#include <memory>
#include <string>

namespace thermox::service {

struct NativeExtensionPackage {
    std::string package_id;
    std::string package_version;
    std::function<void(platform::ComponentRegistry&)>
        register_components;
    std::function<void(physics::PropertyPackageRegistry&)>
        register_properties;
    std::function<void(platform::EngineeringArtifactRegistry&)>
        register_engineering_artifacts;
    std::function<void(platform::CorrelationTemplateRegistry&)>
        register_correlation_templates;
    std::function<void(
        physics::ThermochemistryPackageRegistry&)>
        register_thermochemistry;
    std::function<void(platform::UnitRegistry&)>
        register_units;
};

void apply_native_extension(
    const NativeExtensionPackage& extension,
    platform::ComponentRegistry& components,
    physics::PropertyPackageRegistry& properties,
    platform::EngineeringArtifactRegistry& engineering_artifacts,
    platform::CorrelationTemplateRegistry& correlation_templates,
    physics::ThermochemistryPackageRegistry& thermochemistry,
    platform::UnitRegistry& units);

// Native application composition boundary. RPC and UI adapters should use
// simulation_runtime.hpp and service DTOs without including this header.
std::shared_ptr<const SimulationRuntime> make_simulation_runtime(
    platform::ComponentRegistry components,
    physics::PropertyPackageRegistry properties,
    platform::EngineeringArtifactRegistry engineering_artifacts = {},
    platform::CorrelationTemplateRegistry correlation_templates =
        platform::make_default_correlation_template_registry(),
    physics::ThermochemistryPackageRegistry thermochemistry = {},
    platform::UnitRegistry units =
        platform::make_default_unit_registry());

}  // namespace thermox::service
