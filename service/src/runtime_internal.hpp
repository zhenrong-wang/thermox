#pragma once

#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/correlation.hpp"
#include "thermox/platform/expression_component.hpp"
#include "thermox/platform/unit_registry.hpp"
#include "thermox/physics/property_registry.hpp"
#include "thermox/physics/thermochemistry.hpp"
#include "thermox/service/simulation_runtime.hpp"

#include <string>
#include <vector>

namespace thermox::service {

struct SimulationRuntime::Impl {
    platform::ComponentRegistry components;
    physics::PropertyPackageRegistry properties;
    platform::EngineeringArtifactRegistry engineering_artifacts;
    platform::CorrelationTemplateRegistry correlation_templates;
    physics::ThermochemistryPackageRegistry thermochemistry;
    platform::UnitRegistry units;
    std::string fingerprint;
};

namespace detail {

struct NativeRuntimeFactory {
    static std::shared_ptr<const SimulationRuntime> create(
        platform::ComponentRegistry components,
        physics::PropertyPackageRegistry properties,
        platform::EngineeringArtifactRegistry engineering_artifacts,
        platform::CorrelationTemplateRegistry correlation_templates,
        physics::ThermochemistryPackageRegistry thermochemistry,
        platform::UnitRegistry units);
    static std::shared_ptr<const SimulationRuntime> overlay(
        const std::shared_ptr<const SimulationRuntime>& base,
        std::vector<platform::ExpressionComponentDefinition>
            expression_components);
};

}  // namespace detail

}  // namespace thermox::service
