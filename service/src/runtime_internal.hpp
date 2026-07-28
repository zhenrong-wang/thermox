#pragma once

#include "thermox/platform/component_registry.hpp"
#include "thermox/physics/property_registry.hpp"
#include "thermox/service/simulation_runtime.hpp"

#include <string>

namespace thermox::service {

struct SimulationRuntime::Impl {
    platform::ComponentRegistry components;
    physics::PropertyPackageRegistry properties;
    std::string fingerprint;
};

namespace detail {

struct NativeRuntimeFactory {
    static std::shared_ptr<const SimulationRuntime> create(
        platform::ComponentRegistry components,
        physics::PropertyPackageRegistry properties);
};

}  // namespace detail

}  // namespace thermox::service
