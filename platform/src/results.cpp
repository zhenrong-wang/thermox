#include "thermox/platform/results.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>

namespace thermox::platform {

namespace {

std::string phase_name(physics::Phase phase) {
    switch (phase) {
        case physics::Phase::liquid: return "liquid";
        case physics::Phase::vapor: return "vapor";
        case physics::Phase::supercritical: return "supercritical";
        case physics::Phase::two_phase: return "two_phase";
        case physics::Phase::unknown: return "unknown";
    }
    return "unknown";
}

const ResultValue& require_primary(
    const PortResult& port,
    const std::string& name) {
    const auto value = std::find_if(
        port.primary_values.begin(),
        port.primary_values.end(),
        [&](const auto& candidate) {
            return candidate.name == name;
        });
    if (value == port.primary_values.end()) {
        throw std::logic_error(
            "fluid port is missing primary result variable '" +
            name + "'");
    }
    return *value;
}

std::vector<ResultValue> fluid_derived_values(
    const physics::ThermodynamicState& state) {
    return {
        {"T", "temperature", state.temperature_k},
        {"rho", "density", state.density_kg_m3},
        {"u", "specific_internal_energy",
         state.internal_energy_j_kg},
        {"s", "specific_entropy", state.entropy_j_kg_k},
        {"cp", "specific_heat_capacity", state.cp_j_kg_k},
        {"cv", "specific_heat_capacity", state.cv_j_kg_k},
        {"speed_of_sound", "speed", state.speed_of_sound_m_s},
        {"viscosity", "dynamic_viscosity",
         state.viscosity_pa_s},
        {"thermal_conductivity", "thermal_conductivity",
         state.thermal_conductivity_w_m_k},
        {"vapor_quality", "dimensionless", state.vapor_quality},
    };
}

struct MaterialResultBackend {
    std::shared_ptr<const physics::ThermochemistryPackage> package;
    std::vector<std::string> species;
};

}  // namespace

struct GraphResultEvaluator::Impl {
    void initialize(
        const ModelDocument& document,
        const std::vector<CompiledPortVariable>& compiled_ports,
        const std::vector<CompiledInternalVariable>& compiled_internal,
        std::size_t count,
        const physics::PropertyPackageRegistry& property_registry,
        const physics::ThermochemistryPackageRegistry*
            thermochemistry_registry) {
        variable_count = count;
        port_variables = compiled_ports;
        internal_variables = compiled_internal;
        for (const auto& component : document.components) {
            components.push_back({component.id, component.kind});
        }
        for (const auto& medium : document.media) {
            auto package = property_registry.create(
                medium.backend, medium.substance);
            if (!medium.package_version.empty() &&
                medium.package_version != package->version()) {
                throw std::invalid_argument(
                    "result evaluator property package version mismatch for medium: " +
                    medium.id);
            }
            properties.emplace(medium.id, std::move(package));
        }
        if (thermochemistry_registry != nullptr) {
            for (const auto& material : document.materials) {
                if (!thermochemistry_registry->contains(
                        material.backend)) {
                    continue;
                }
                auto package = thermochemistry_registry->create(
                    material.backend, material.mechanism,
                    material.phase);
                if (!material.package_version.empty() &&
                    material.package_version !=
                        package->version()) {
                    throw std::invalid_argument(
                        "result evaluator thermochemistry package "
                        "version mismatch for material: " +
                        material.id);
                }
                materials.emplace(
                    material.id,
                    MaterialResultBackend{
                        std::move(package), material.species});
            }
        }
    }

    std::size_t variable_count{0};
    std::vector<std::pair<std::string, std::string>> components;
    std::vector<CompiledPortVariable> port_variables;
    std::vector<CompiledInternalVariable> internal_variables;
    std::map<
        std::string,
        std::shared_ptr<const physics::PropertyPackage>>
        properties;
    std::map<std::string, MaterialResultBackend> materials;
};

GraphResultEvaluator::GraphResultEvaluator(
    const ModelDocument& document,
    const CompiledModelGraph& graph,
    const physics::PropertyPackageRegistry& property_registry)
    : impl_(std::make_unique<Impl>()) {
    impl_->initialize(
        document, graph.port_variables, {},
        graph.problem.variable_names.size(), property_registry,
        nullptr);
}

GraphResultEvaluator::GraphResultEvaluator(
    const ModelDocument& document,
    const CompiledModelGraph& graph,
    const physics::PropertyPackageRegistry& property_registry,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry_registry)
    : impl_(std::make_unique<Impl>()) {
    impl_->initialize(
        document, graph.port_variables, {},
        graph.problem.variable_names.size(), property_registry,
        &thermochemistry_registry);
}

GraphResultEvaluator::GraphResultEvaluator(
    const ModelDocument& document,
    const CompiledTransientModelGraph& graph,
    const physics::PropertyPackageRegistry& property_registry)
    : impl_(std::make_unique<Impl>()) {
    impl_->initialize(
        document, graph.port_variables, graph.internal_variables,
        graph.problem.variable_names.size(), property_registry,
        nullptr);
}

GraphResultEvaluator::~GraphResultEvaluator() = default;
GraphResultEvaluator::GraphResultEvaluator(
    GraphResultEvaluator&&) noexcept = default;
GraphResultEvaluator& GraphResultEvaluator::operator=(
    GraphResultEvaluator&&) noexcept = default;

GraphResult GraphResultEvaluator::evaluate(
    const std::vector<double>& state,
    const std::vector<double>& derivative) const {
    if (state.size() != impl_->variable_count) {
        throw std::invalid_argument(
            "state size does not match compiled model variables");
    }
    if (!derivative.empty() &&
        derivative.size() != impl_->variable_count) {
        throw std::invalid_argument(
            "derivative size does not match compiled model variables");
    }

    GraphResult result;
    std::map<std::string, std::size_t> component_indices;
    for (const auto& [component_id, kind] : impl_->components) {
        component_indices.emplace(
            component_id, result.components.size());
        ComponentResult component;
        component.component_id = component_id;
        component.kind = kind;
        result.components.push_back(std::move(component));
    }

    std::map<
        std::pair<std::string, std::string>,
        std::size_t>
        port_indices;
    for (const auto& variable : impl_->port_variables) {
        const auto component =
            component_indices.find(variable.component_id);
        if (component == component_indices.end()) {
            throw std::logic_error(
                "compiled port references unknown component: " +
                variable.component_id);
        }
        auto& component_result =
            result.components.at(component->second);
        const auto key =
            std::make_pair(
                variable.component_id, variable.port_name);
        auto port = port_indices.find(key);
        if (port == port_indices.end()) {
            port = port_indices
                       .emplace(
                           key, component_result.ports.size())
                       .first;
            PortResult port_result;
            port_result.port_name = variable.port_name;
            port_result.domain = variable.domain;
            port_result.medium_id = variable.medium_id;
            component_result.ports.push_back(
                std::move(port_result));
        }
        auto& port_result =
            component_result.ports.at(port->second);
        port_result.primary_values.push_back({
            variable.variable_name,
            variable.dimension,
            state.at(variable.index),
            !derivative.empty(),
            derivative.empty()
                ? 0.0
                : derivative.at(variable.index),
        });
    }

    for (const auto& variable : impl_->internal_variables) {
        const auto component =
            component_indices.find(variable.component_id);
        if (component == component_indices.end()) {
            throw std::logic_error(
                "compiled internal state references unknown component: " +
                variable.component_id);
        }
        result.components.at(component->second)
            .internal_values.push_back({
                variable.variable_name,
                variable.dimension,
                state.at(variable.index),
                !derivative.empty(),
                derivative.empty()
                    ? 0.0
                    : derivative.at(variable.index),
            });
    }

    for (auto& component : result.components) {
        for (auto& port : component.ports) {
            if (port.domain != "fluid") {
                continue;
            }
            const auto package =
                impl_->properties.find(port.medium_id);
            if (package == impl_->properties.end()) {
                throw std::logic_error(
                    "fluid result references unresolved medium: " +
                    port.medium_id);
            }
            const auto properties = package->second->state_ph(
                require_primary(port, "p").value_si,
                require_primary(port, "h").value_si);
            if (!properties.ok()) {
                throw std::runtime_error(
                    "failed to evaluate fluid-port result '" +
                    component.component_id + "." + port.port_name +
                    "': " + properties.message);
            }
            port.phase = phase_name(properties.state.phase);
            port.derived_values =
                fluid_derived_values(properties.state);
        }
    }
    for (auto& component : result.components) {
        for (auto& port : component.ports) {
            if (port.domain != "material") {
                continue;
            }
            const auto backend =
                impl_->materials.find(port.medium_id);
            if (backend == impl_->materials.end()) {
                if (impl_->materials.empty()) {
                    continue;
                }
                throw std::logic_error(
                    "material result references unresolved "
                    "thermochemistry package: " +
                    port.medium_id);
            }
            std::vector<double> fractions;
            fractions.reserve(backend->second.species.size());
            double total_mass_flow = 0.0;
            for (const auto& species :
                 backend->second.species) {
                const double mass_flow = require_primary(
                    port, "m_dot[" + species + "]").value_si;
                if (mass_flow < 0.0) {
                    throw std::runtime_error(
                        "failed to evaluate material-port result '" +
                        component.component_id + "." +
                        port.port_name +
                        "': species mass flow is negative");
                }
                total_mass_flow += mass_flow;
                fractions.push_back(mass_flow);
            }
            if (total_mass_flow <= 0.0) {
                throw std::runtime_error(
                    "failed to evaluate material-port result '" +
                    component.component_id + "." +
                    port.port_name +
                    "': total species mass flow is not positive");
            }
            for (auto& fraction : fractions) {
                fraction /= total_mass_flow;
            }
            const auto properties =
                backend->second.package->state_ph(
                    require_primary(port, "p").value_si,
                    require_primary(port, "h").value_si,
                    physics::SpeciesComposition{
                        physics::CompositionBasis::mass_fraction,
                        backend->second.species,
                        std::move(fractions)});
            if (!properties.ok()) {
                throw std::runtime_error(
                    "failed to evaluate material-port result '" +
                    component.component_id + "." +
                    port.port_name + "': " +
                    properties.message);
            }
            port.derived_values = fluid_derived_values(
                properties.state.thermodynamic);
            port.derived_values.push_back({
                "mean_molecular_weight", "molar_mass",
                properties.state.mean_molecular_weight_kg_mol,
            });
        }
    }
    return result;
}

}  // namespace thermox::platform
