#include "thermox/platform/results.hpp"

#include <map>
#include <memory>
#include <stdexcept>
#include <utility>

namespace thermox::platform {

namespace {

const MediumDefinition& require_medium(
    const ModelDocument& document,
    const std::string& medium_id) {
    for (const auto& medium : document.media) {
        if (medium.id == medium_id) {
            return medium;
        }
    }
    throw std::invalid_argument(
        "compiled result references unknown medium: " + medium_id);
}

struct FluidPortIndices {
    std::string component_id;
    std::string port_name;
    std::string medium_id;
    std::size_t mass_flow{0};
    std::size_t pressure{0};
    std::size_t enthalpy{0};
    bool has_mass_flow{false};
    bool has_pressure{false};
    bool has_enthalpy{false};
};

}  // namespace

std::vector<FluidPortResult> evaluate_fluid_port_results(
    const ModelDocument& document,
    const CompiledModelGraph& graph,
    const std::vector<double>& solution) {
    return evaluate_fluid_port_results(
        document, graph, solution,
        physics::make_default_property_package_registry());
}

std::vector<FluidPortResult> evaluate_fluid_port_results(
    const ModelDocument& document,
    const CompiledModelGraph& graph,
    const std::vector<double>& solution,
    const physics::PropertyPackageRegistry& property_registry) {
    if (solution.size() != graph.problem.variable_names.size()) {
        throw std::invalid_argument(
            "solution size does not match compiled model variables");
    }

    std::map<std::pair<std::string, std::string>, FluidPortIndices>
        grouped;
    for (const auto& variable : graph.port_variables) {
        if (variable.domain != "fluid") {
            continue;
        }
        auto& indices = grouped[
            {variable.component_id, variable.port_name}];
        indices.component_id = variable.component_id;
        indices.port_name = variable.port_name;
        indices.medium_id = variable.medium_id;
        if (variable.variable_name == "m_dot") {
            indices.mass_flow = variable.index;
            indices.has_mass_flow = true;
        } else if (variable.variable_name == "p") {
            indices.pressure = variable.index;
            indices.has_pressure = true;
        } else if (variable.variable_name == "h") {
            indices.enthalpy = variable.index;
            indices.has_enthalpy = true;
        }
    }

    std::map<std::string,
             std::shared_ptr<const physics::PropertyPackage>>
        properties;
    std::vector<FluidPortResult> results;
    results.reserve(grouped.size());
    for (const auto& entry : grouped) {
        const auto& indices = entry.second;
        if (!indices.has_mass_flow || !indices.has_pressure ||
            !indices.has_enthalpy) {
            throw std::logic_error(
                "fluid port is missing a primary result variable: " +
                indices.component_id + "." + indices.port_name);
        }
        auto package = properties.find(indices.medium_id);
        if (package == properties.end()) {
            const auto& medium =
                require_medium(document, indices.medium_id);
            package = properties
                          .emplace(
                              indices.medium_id,
                              property_registry.create(
                                  medium.backend, medium.substance))
                          .first;
        }
        const auto property_state = package->second->state_ph(
            solution.at(indices.pressure),
            solution.at(indices.enthalpy));
        if (!property_state.ok()) {
            throw std::runtime_error(
                "failed to evaluate fluid-port result '" +
                indices.component_id + "." + indices.port_name +
                "': " + property_state.message);
        }
        results.push_back(
            FluidPortResult{indices.component_id, indices.port_name,
                            indices.medium_id,
                            solution.at(indices.mass_flow),
                            property_state.state});
    }
    return results;
}

}  // namespace thermox::platform
