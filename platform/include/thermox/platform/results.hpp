#pragma once

#include "thermox/platform/component_registry.hpp"

#include <string>
#include <vector>

namespace thermox::platform {

struct FluidPortResult {
    std::string component_id;
    std::string port_name;
    std::string medium_id;
    double mass_flow_kg_s{0.0};
    physics::ThermodynamicState state;
};

std::vector<FluidPortResult> evaluate_fluid_port_results(
    const ModelDocument& document,
    const CompiledModelGraph& graph,
    const std::vector<double>& solution);
std::vector<FluidPortResult> evaluate_fluid_port_results(
    const ModelDocument& document,
    const CompiledModelGraph& graph,
    const std::vector<double>& solution,
    const physics::PropertyPackageRegistry& property_registry);

}  // namespace thermox::platform
