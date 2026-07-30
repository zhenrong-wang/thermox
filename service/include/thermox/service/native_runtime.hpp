#pragma once

#include "thermox/platform/component_registry.hpp"
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
    std::function<void(platform::PerformanceMapRegistry&)>
        register_performance_maps;
    std::function<void(
        physics::ThermochemistryPackageRegistry&)>
        register_thermochemistry;
};

void apply_native_extension(
    const NativeExtensionPackage& extension,
    platform::ComponentRegistry& components,
    physics::PropertyPackageRegistry& properties,
    platform::PerformanceMapRegistry& performance_maps,
    physics::ThermochemistryPackageRegistry& thermochemistry);

// Native application composition boundary. RPC and UI adapters should use
// simulation_runtime.hpp and service DTOs without including this header.
std::shared_ptr<const SimulationRuntime> make_simulation_runtime(
    platform::ComponentRegistry components,
    physics::PropertyPackageRegistry properties,
    platform::PerformanceMapRegistry performance_maps = {},
    physics::ThermochemistryPackageRegistry thermochemistry = {});

}  // namespace thermox::service
