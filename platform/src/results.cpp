#include "thermox/platform/results.hpp"

#include <algorithm>
#include <iterator>
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
            thermochemistry_registry,
        bool include_boundary_audit) {
        const auto executable = flatten_model_document(document);
        variable_count = count;
        boundary_audit = include_boundary_audit;
        port_variables = compiled_ports;
        internal_variables = compiled_internal;
        for (const auto& variable : compiled_ports) {
            port_directions.emplace(
                std::make_pair(
                    variable.component_id,
                    variable.port_name),
                variable.direction);
            if (variable.system_boundary_sign != 0) {
                boundary_ports.emplace(
                    std::make_pair(
                        variable.component_id,
                        variable.port_name),
                    variable.system_boundary_sign);
            }
        }
        for (const auto& component : executable.components) {
            components.push_back({component.id, component.kind});
        }
        for (const auto& medium : executable.media) {
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
            for (const auto& material : executable.materials) {
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
    std::map<std::pair<std::string, std::string>, int>
        boundary_ports;
    std::map<std::pair<std::string, std::string>, std::string>
        port_directions;
    bool boundary_audit{false};
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
        nullptr, true);
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
        &thermochemistry_registry, true);
}

GraphResultEvaluator::GraphResultEvaluator(
    const ModelDocument& document,
    const CompiledTransientModelGraph& graph,
    const physics::PropertyPackageRegistry& property_registry)
    : impl_(std::make_unique<Impl>()) {
    impl_->initialize(
        document, graph.port_variables, graph.internal_variables,
        graph.problem.variable_names.size(), property_registry,
        nullptr, true);
}

GraphResultEvaluator::GraphResultEvaluator(
    const ModelDocument& document,
    const CompiledTransientModelGraph& graph,
    const physics::PropertyPackageRegistry& property_registry,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry_registry)
    : impl_(std::make_unique<Impl>()) {
    impl_->initialize(
        document, graph.port_variables, graph.internal_variables,
        graph.problem.variable_names.size(), property_registry,
        &thermochemistry_registry, true);
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
            port.derived_values.push_back({
                "m_dot_total", "mass_flow", total_mass_flow,
            });
            for (std::size_t index = 0;
                 index < backend->second.species.size(); ++index) {
                port.derived_values.push_back({
                    "mass_fraction[" +
                        backend->second.species[index] + "]",
                    "dimensionless", fractions[index],
                });
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
            auto thermodynamic_values = fluid_derived_values(
                properties.state.thermodynamic);
            port.derived_values.insert(
                port.derived_values.end(),
                std::make_move_iterator(
                    thermodynamic_values.begin()),
                std::make_move_iterator(
                    thermodynamic_values.end()));
            port.derived_values.push_back({
                "mean_molecular_weight", "molar_mass",
                properties.state.mean_molecular_weight_kg_mol,
            });
        }
    }
    if (impl_->boundary_audit) {
        double net_mass_flow = 0.0;
        double net_energy_flow = 0.0;
        bool has_mass_flow = false;
        bool has_energy_flow = false;
        for (const auto& [key, sign] :
             impl_->boundary_ports) {
            const auto component = component_indices.find(
                key.first);
            if (component == component_indices.end()) {
                throw std::logic_error(
                    "system boundary references unknown component: " +
                    key.first);
            }
            const auto& component_result =
                result.components.at(component->second);
            const auto port = std::find_if(
                component_result.ports.begin(),
                component_result.ports.end(),
                [&](const auto& candidate) {
                    return candidate.port_name == key.second;
                });
            if (port == component_result.ports.end()) {
                throw std::logic_error(
                    "system boundary references unknown port: " +
                    key.first + "." + key.second);
            }
            const double orientation =
                static_cast<double>(sign);
            if (port->domain == "fluid") {
                const double mass_flow =
                    require_primary(*port, "m_dot").value_si;
                net_mass_flow += orientation * mass_flow;
                net_energy_flow += orientation * mass_flow *
                    require_primary(*port, "h").value_si;
                has_mass_flow = true;
                has_energy_flow = true;
                continue;
            }
            if (port->domain == "material") {
                double mass_flow = 0.0;
                for (const auto& value :
                     port->primary_values) {
                    if (value.name.starts_with("m_dot[")) {
                        mass_flow += value.value_si;
                    }
                }
                net_mass_flow += orientation * mass_flow;
                net_energy_flow += orientation * mass_flow *
                    require_primary(*port, "h").value_si;
                has_mass_flow = true;
                has_energy_flow = true;
                continue;
            }
            if (port->domain == "heat") {
                net_energy_flow += orientation *
                    require_primary(*port, "Q_dot").value_si;
                has_energy_flow = true;
                continue;
            }
            if (port->domain == "shaft") {
                net_energy_flow += orientation *
                    require_primary(*port, "W_dot").value_si;
                has_energy_flow = true;
                continue;
            }
            if (port->domain == "electrical") {
                net_energy_flow += orientation *
                    require_primary(*port, "P").value_si;
                has_energy_flow = true;
            }
        }
        if (has_mass_flow) {
            result.system_balances.push_back({
                "net_boundary_mass_flow",
                "mass_flow",
                net_mass_flow,
            });
        }
        if (has_energy_flow) {
            result.system_balances.push_back({
                "net_boundary_energy_flow",
                "power",
                net_energy_flow,
            });
        }

        for (auto& component : result.components) {
            if (component.ports.size() < 2) {
                continue;
            }
            double net_mass_flow = 0.0;
            double net_energy_flow = 0.0;
            bool has_mass_flow = false;
            bool has_energy_flow = false;
            for (const auto& port : component.ports) {
                const auto direction =
                    impl_->port_directions.find({
                        component.component_id,
                        port.port_name});
                if (direction ==
                        impl_->port_directions.end() ||
                    direction->second == "bidirectional") {
                    continue;
                }
                const double orientation =
                    direction->second == "in" ? 1.0 : -1.0;
                if (port.domain == "fluid") {
                    const double mass_flow =
                        require_primary(port, "m_dot").value_si;
                    net_mass_flow += orientation * mass_flow;
                    net_energy_flow += orientation * mass_flow *
                        require_primary(port, "h").value_si;
                    has_mass_flow = true;
                    has_energy_flow = true;
                    continue;
                }
                if (port.domain == "material") {
                    double mass_flow = 0.0;
                    for (const auto& value :
                         port.primary_values) {
                        if (value.name.starts_with("m_dot[")) {
                            mass_flow += value.value_si;
                        }
                    }
                    net_mass_flow += orientation * mass_flow;
                    net_energy_flow += orientation * mass_flow *
                        require_primary(port, "h").value_si;
                    has_mass_flow = true;
                    has_energy_flow = true;
                    continue;
                }
                if (port.domain == "heat") {
                    net_energy_flow += orientation *
                        require_primary(port, "Q_dot").value_si;
                    has_energy_flow = true;
                    continue;
                }
                if (port.domain == "shaft") {
                    net_energy_flow += orientation *
                        require_primary(port, "W_dot").value_si;
                    has_energy_flow = true;
                    continue;
                }
                if (port.domain == "electrical") {
                    net_energy_flow += orientation *
                        require_primary(port, "P").value_si;
                    has_energy_flow = true;
                }
            }
            if (has_mass_flow) {
                component.metrics.push_back({
                    "net_mass_flow",
                    "mass_flow",
                    net_mass_flow,
                });
            }
            if (has_energy_flow) {
                component.metrics.push_back({
                    "net_energy_flow",
                    "power",
                    net_energy_flow,
                });
            }
        }
    }
    return result;
}

}  // namespace thermox::platform
