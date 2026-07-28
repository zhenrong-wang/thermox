#pragma once

#include "thermox/platform/component_registry.hpp"
#include "thermox/physics/property_registry.hpp"
#include "thermox/service/simulation_runtime.hpp"

#include <memory>

namespace thermox::service {

// Native application composition boundary. RPC and UI adapters should use
// simulation_runtime.hpp and service DTOs without including this header.
std::shared_ptr<const SimulationRuntime> make_simulation_runtime(
    platform::ComponentRegistry components,
    physics::PropertyPackageRegistry properties,
    platform::PerformanceMapRegistry performance_maps = {});

}  // namespace thermox::service
