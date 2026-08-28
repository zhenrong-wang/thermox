#include "thermox/platform/component_registry.hpp"

#include "component_modules.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>
#include <set>
#include <stdexcept>
#include <utility>

namespace thermox::platform {

namespace {

std::vector<ConnectorDomainDescriptor>
standard_connector_domains() {
    return {
        {"fluid", "thermox.connector.fluid/v1", "fluid_link",
         {{"m_dot", 1.0, 100.0, "mass_flow", false},
          {"p", 101325.0, 100000.0, "pressure", false},
          {"h", 300000.0, 100000.0,
           "specific_enthalpy", false}}},
        {"material", "thermox.connector.material/v1",
         "material_link",
         {{"p", 101325.0, 100000.0, "pressure", false},
          {"h", 300000.0, 100000.0,
           "specific_enthalpy", false},
          {"m_dot[species]", 0.0, 100.0, "mass_flow",
           true}}},
        {"heat", "thermox.connector.heat/v1", "heat_link",
         {{"Q_dot", 0.0, 1000000.0, "power", false},
          {"T", 300.0, 100.0, "temperature", false}}},
        {"shaft", "thermox.connector.shaft/v1", "shaft_link",
         {{"W_dot", 0.0, 1000000.0, "power", false},
          {"omega", 314.1592653589793, 100.0,
           "angular_speed", false}}},
        {"electrical", "thermox.connector.electrical/v1",
         "electrical_link",
         {{"P", 0.0, 1000000.0, "power", false},
          {"frequency", 50.0, 50.0, "frequency", false}}},
        {"force", "thermox.connector.force/v1", "force_link",
         {{"F", 0.0, 100000.0, "force", false}}},
        {"inventory", "thermox.connector.inventory/v1",
         "inventory_link",
         {{"mass", 1.0, 10.0, "mass", false}}},
        {"signal", "thermox.connector.signal/v1",
         "signal_link",
         {{"value", 0.0, 1.0, "dimensionless", false}}},
        {"control", "thermox.connector.control/v1",
         "signal_link",
         {{"value", 0.0, 1.0, "dimensionless", false}}},
    };
}

std::vector<ConnectorVariableDescriptor>
canonical_variables_for_domain(
    const ComponentRegistry& registry,
    const std::string& domain,
    const std::vector<std::string>& species = {}) {
    std::vector<ConnectorVariableDescriptor> variables;
    for (const auto& descriptor :
         registry.require_connector_domain(domain).variables) {
        if (!descriptor.expand_species) {
            variables.push_back(descriptor);
            continue;
        }
        for (const auto& species_name : species) {
            auto expanded = descriptor;
            const auto marker =
                expanded.name.find("species");
            expanded.name.replace(
                marker,
                std::string_view{"species"}.size(),
                species_name);
            expanded.expand_species = false;
            variables.push_back(std::move(expanded));
        }
    }
    return variables;
}

std::string endpoint_component(const std::string& endpoint) {
    const std::size_t dot = endpoint.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= endpoint.size() ||
        endpoint.find('.', dot + 1) != std::string::npos) {
        throw std::invalid_argument("connection endpoint must use component.port: " + endpoint);
    }
    return endpoint.substr(0, dot);
}

std::string endpoint_port(const std::string& endpoint) {
    const std::size_t dot = endpoint.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= endpoint.size() ||
        endpoint.find('.', dot + 1) != std::string::npos) {
        throw std::invalid_argument("connection endpoint must use component.port: " + endpoint);
    }
    return endpoint.substr(dot + 1);
}

std::string variable_key(const std::string& component_id,
                         const std::string& port_name,
                         const std::string& variable_name) {
    return component_id + "." + port_name + "." + variable_name;
}

int explicit_boundary_sign(
    const ComponentModelDescriptor& descriptor) {
    if (descriptor.system_boundary_role.empty()) {
        return 0;
    }
    if (descriptor.system_boundary_role == "source") {
        return 1;
    }
    if (descriptor.system_boundary_role == "sink") {
        return -1;
    }
    throw std::invalid_argument(
        "component kind '" + descriptor.kind +
        "' has invalid system boundary role: " +
        descriptor.system_boundary_role);
}

void mark_unconnected_system_boundaries(
    std::vector<CompiledPortVariable>& variables,
    const std::map<std::string, std::size_t>&
        connection_counts) {
    for (auto& variable : variables) {
        if (variable.system_boundary_sign != 0) {
            continue;
        }
        const auto endpoint =
            variable.component_id + "." + variable.port_name;
        if (connection_counts.contains(endpoint)) {
            continue;
        }
        if (variable.direction == "in") {
            variable.system_boundary_sign = 1;
        } else if (variable.direction == "out") {
            variable.system_boundary_sign = -1;
        }
    }
}

const ComponentDefinition& find_component(const ModelDocument& document, const std::string& component_id) {
    for (const ComponentDefinition& component : document.components) {
        if (component.id == component_id) {
            return component;
        }
    }
    throw std::invalid_argument("unknown component during graph compilation: " + component_id);
}

PortModelDescriptor find_port(
    const ComponentDefinition& component,
    const ComponentModelDescriptor& descriptor,
    const std::string& port_name) {
    const auto it = std::find_if(
        descriptor.ports.begin(),
        descriptor.ports.end(),
        [&](const auto& port) {
            return port.name == port_name;
        });
    if (it == descriptor.ports.end()) {
        throw std::invalid_argument(
            "unknown port during graph compilation: " +
            component.id + "." + port_name);
    }
    return *it;
}

const std::string& require_medium_binding(
    const ComponentDefinition& component,
    const std::string& port_name) {
    const auto medium =
        component.medium_bindings.find(port_name);
    if (medium == component.medium_bindings.end()) {
        throw std::invalid_argument(
            "component '" + component.id +
            "' is missing medium binding for fluid port: " +
            port_name);
    }
    return medium->second;
}

const std::string& require_material_binding(
    const ComponentDefinition& component,
    const std::string& port_name) {
    const auto material =
        component.material_bindings.find(port_name);
    if (material == component.material_bindings.end()) {
        throw std::invalid_argument(
            "component '" + component.id +
            "' is missing material binding for material port: " +
            port_name);
    }
    return material->second;
}

const MaterialDefinition& find_material(
    const ModelDocument& document,
    const std::string& material_id) {
    const auto it = std::find_if(
        document.materials.begin(), document.materials.end(),
        [&](const auto& material) {
            return material.id == material_id;
        });
    if (it == document.materials.end()) {
        throw std::invalid_argument(
            "unknown material binding during graph compilation: " +
            material_id);
    }
    return *it;
}

using MediumPropertyMap =
    std::map<
        std::string,
        std::shared_ptr<const physics::PropertyPackage>>;

MediumPropertyMap create_medium_properties(
    const ModelDocument& document,
    const physics::PropertyPackageRegistry& property_registry) {
    MediumPropertyMap properties;
    for (const auto& medium : document.media) {
        auto package =
            property_registry.create(
                medium.backend, medium.substance);
        if (!medium.package_version.empty() &&
            medium.package_version != package->version()) {
            throw std::invalid_argument(
                "medium '" + medium.id +
                "' requests property package version '" +
                medium.package_version + "' but backend '" +
                medium.backend + "' provides version '" +
                std::string(package->version()) + "'");
        }
        properties.emplace(medium.id, std::move(package));
    }
    return properties;
}

struct ValidatedConnection {
    std::string from_component;
    std::string from_port;
    std::string to_component;
    std::string to_port;
    std::string domain;
    std::vector<std::string> species;
};

ValidatedConnection validate_connection(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const ConnectionDefinition& connection,
    std::map<std::string, std::size_t>& connection_counts) {
    ValidatedConnection result{
        endpoint_component(connection.from),
        endpoint_port(connection.from),
        endpoint_component(connection.to),
        endpoint_port(connection.to),
        {},
        {},
    };
    const auto& from_definition =
        find_component(document, result.from_component);
    const auto& to_definition =
        find_component(document, result.to_component);
    const auto& from_model =
        registry.require_model(from_definition.kind);
    const auto& to_model =
        registry.require_model(to_definition.kind);
    const auto from_descriptor =
        from_model.instance_descriptor(from_definition);
    const auto to_descriptor =
        to_model.instance_descriptor(to_definition);
    const auto from_port = find_port(
        from_definition, from_descriptor, result.from_port);
    const auto to_port = find_port(
        to_definition, to_descriptor, result.to_port);
    result.domain = from_port.domain;
    if (from_port.domain != to_port.domain) {
        throw std::invalid_argument(
            "connection '" + connection.id +
            "' links incompatible port domains");
    }
    if (from_port.direction == "in") {
        throw std::invalid_argument(
            "connection '" + connection.id +
            "' source port cannot have direction 'in'");
    }
    if (to_port.direction == "out") {
        throw std::invalid_argument(
            "connection '" + connection.id +
            "' target port cannot have direction 'out'");
    }
    const auto& connector =
        registry.require_connector_domain(from_port.domain);
    if (connection.kind !=
        connector.connection_kind) {
        throw std::invalid_argument(
            "connection '" + connection.id +
            "' kind '" + connection.kind +
            "' is incompatible with domain '" +
            from_port.domain + "'");
    }
    if (!connection.contract_version.empty() &&
        connection.contract_version !=
            connector.contract_version) {
        throw std::invalid_argument(
            "connection '" + connection.id +
            "' requests connector contract version '" +
            connection.contract_version + "' but domain '" +
            from_port.domain + "' provides version '" +
            connector.contract_version + "'");
    }
    if (from_port.domain == "fluid" &&
        require_medium_binding(
            from_definition, result.from_port) !=
            require_medium_binding(
                to_definition, result.to_port)) {
        throw std::invalid_argument(
            "connection '" + connection.id +
            "' links incompatible fluid media");
    }
    if (from_port.domain == "material" &&
        require_material_binding(
            from_definition, result.from_port) !=
            require_material_binding(
                to_definition, result.to_port)) {
        throw std::invalid_argument(
            "connection '" + connection.id +
            "' links incompatible material definitions");
    }
    if (from_port.domain == "material") {
        result.species = find_material(
            document,
            require_material_binding(
                from_definition, result.from_port)).species;
    }
    const auto count_connection =
        [&](const std::string& endpoint,
            const PortModelDescriptor& port) {
            const auto count = ++connection_counts[endpoint];
            if (count > port.maximum_connections) {
                throw std::invalid_argument(
                    "port '" + endpoint +
                    "' exceeds its maximum connection count of " +
                    std::to_string(port.maximum_connections));
            }
        };
    count_connection(connection.from, from_port);
    count_connection(connection.to, to_port);
    return result;
}

const CaseDefinition* select_case(const ModelDocument& document, const std::string& case_id) {
    if (case_id.empty()) {
        return document.cases.empty() ? nullptr : &document.cases.front();
    }
    for (const CaseDefinition& c : document.cases) {
        if (c.id == case_id) {
            return &c;
        }
    }
    throw std::invalid_argument("unknown case id during graph compilation: " + case_id);
}

std::optional<double> case_scalar_value(const CaseDefinition* active_case,
                                        const std::string& key,
                                        bool fixed_values) {
    if (active_case == nullptr) {
        return std::nullopt;
    }
    const auto& values = fixed_values ? active_case->fixed_values : active_case->initial_guesses;
    const auto it = values.find(key);
    if (it == values.end()) {
        return std::nullopt;
    }
    return it->second.value_si;
}

std::optional<double> case_schedule_initial_value(
    const CaseDefinition* active_case,
    const std::string& key) {
    if (active_case == nullptr) return std::nullopt;
    const auto schedule = active_case->input_schedules.find(key);
    if (schedule == active_case->input_schedules.end() ||
        schedule->second.points.empty()) {
        return std::nullopt;
    }
    return schedule->second.points.front().value.value_si;
}

std::string selected_component_mode(
    const ComponentModelDescriptor& descriptor,
    const CaseDefinition* active_case,
    const std::string& component_id) {
    const auto override_mode = active_case == nullptr
        ? std::map<std::string, std::string>::const_iterator{}
        : active_case->component_modes.find(component_id);
    const bool has_override = active_case != nullptr &&
        override_mode != active_case->component_modes.end();
    if (descriptor.supported_modes.empty()) {
        if (has_override) {
            throw std::invalid_argument(
                "case '" + active_case->id + "' assigns mode '" +
                override_mode->second + "' to component '" +
                component_id + "', whose model does not declare modes");
        }
        return {};
    }
    const std::string selected = has_override
        ? override_mode->second
        : descriptor.default_mode;
    if (std::find(
            descriptor.supported_modes.begin(),
            descriptor.supported_modes.end(), selected) ==
        descriptor.supported_modes.end()) {
        throw std::invalid_argument(
            "component '" + component_id + "' requests unsupported "
            "mode '" + selected + "' for model '" +
            descriptor.kind + "'");
    }
    return selected;
}

double interpolate_input_schedule(
    const InputScheduleDefinition& schedule,
    double time) {
    if (time <= schedule.points.front().time.value_si) {
        return schedule.points.front().value.value_si;
    }
    if (time >= schedule.points.back().time.value_si) {
        return schedule.points.back().value.value_si;
    }
    const auto upper = std::upper_bound(
        schedule.points.begin(), schedule.points.end(), time,
        [](double candidate,
           const InputSchedulePointDefinition& point) {
            return candidate < point.time.value_si;
        });
    const auto lower = std::prev(upper);
    if (schedule.interpolation == "previous") {
        return lower->value.value_si;
    }
    const double fraction =
        (time - lower->time.value_si) /
        (upper->time.value_si - lower->time.value_si);
    return lower->value.value_si +
        fraction *
            (upper->value.value_si - lower->value.value_si);
}

std::map<
    std::string,
    std::map<std::string, ScalarValue>>
case_parameter_overrides(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const CaseDefinition* active_case) {
    std::map<
        std::string,
        std::map<std::string, ScalarValue>> overrides;
    if (active_case == nullptr) return overrides;
    constexpr std::string_view prefix{"components."};
    constexpr std::string_view marker{".parameters."};
    for (const auto& [target, scalar] :
         active_case->parameter_overrides) {
        if (!target.starts_with(prefix)) {
            throw std::invalid_argument(
                "case '" + active_case->id +
                "' parameter override must use "
                "components.<component-id>.parameters."
                "<parameter-name>: " + target);
        }
        const auto marker_position =
            target.find(marker, prefix.size());
        if (marker_position == std::string::npos) {
            throw std::invalid_argument(
                "case '" + active_case->id +
                "' parameter override must use "
                "components.<component-id>.parameters."
                "<parameter-name>: " + target);
        }
        const std::string component_id = target.substr(
            prefix.size(),
            marker_position - prefix.size());
        const std::string parameter_name = target.substr(
            marker_position + marker.size());
        if (component_id.empty() || parameter_name.empty()) {
            throw std::invalid_argument(
                "case '" + active_case->id +
                "' parameter override has an empty component or "
                "parameter name: " + target);
        }
        const auto component = std::find_if(
            document.components.begin(),
            document.components.end(),
            [&](const auto& candidate) {
                return candidate.id == component_id;
            });
        if (component == document.components.end()) {
            throw std::invalid_argument(
                "case '" + active_case->id +
                "' parameter override references unknown "
                "component: " + component_id);
        }
        const auto descriptor =
            registry.require_model(component->kind)
                .instance_descriptor(*component);
        const auto* parameter =
            find_component_parameter_descriptor(
                descriptor, parameter_name);
        if (parameter == nullptr) {
            throw std::invalid_argument(
                "case '" + active_case->id +
                "' parameter override references unknown "
                "parameter: " + target);
        }
        if (scalar.dimension != "dimensionless" &&
            scalar.dimension != parameter->dimension) {
            throw std::invalid_argument(
                "case '" + active_case->id +
                "' parameter override '" + target +
                "' requires dimension '" +
                parameter->dimension + "' but received '" +
                scalar.dimension + "'");
        }
        overrides[component_id][parameter_name] = scalar;
    }
    return overrides;
}

ComponentDefinition effective_component(
    const ComponentDefinition& component,
    const std::map<
        std::string,
        std::map<std::string, ScalarValue>>& overrides) {
    ComponentDefinition effective = component;
    const auto selected = overrides.find(component.id);
    if (selected == overrides.end()) return effective;
    for (const auto& [name, value] : selected->second) {
        effective.parameters[name] = value;
    }
    return effective;
}

void validate_component_bindings(
    const ComponentDefinition& component,
    const ComponentModelDescriptor& descriptor) {
    const auto& expected_ports = descriptor.ports;
    if (!component.version.empty() &&
        component.version != descriptor.version) {
        throw std::invalid_argument(
            "component '" + component.id + "' requests version '" +
            component.version + "' but registered kind '" +
            component.kind + "' provides version '" +
            descriptor.version + "'");
    }
    for (const auto& expected : expected_ports) {
        const auto medium_binding =
            component.medium_bindings.find(expected.name);
        const auto material_binding =
            component.material_bindings.find(expected.name);
        if (expected.domain == "fluid" &&
            medium_binding == component.medium_bindings.end()) {
            throw std::invalid_argument(
                "component '" + component.id +
                "' is missing medium binding for fluid port: " +
                expected.name);
        }
        if (expected.domain == "material" &&
            material_binding ==
                component.material_bindings.end()) {
            throw std::invalid_argument(
                "component '" + component.id +
                "' is missing material binding for material port: " +
                expected.name);
        }
        if (expected.domain != "fluid" &&
            medium_binding != component.medium_bindings.end()) {
            throw std::invalid_argument(
                "component '" + component.id +
                "' supplies a medium binding for non-fluid port: " +
                expected.name);
        }
        if (expected.domain != "material" &&
            material_binding !=
                component.material_bindings.end()) {
            throw std::invalid_argument(
                "component '" + component.id +
                "' supplies a material binding for non-material port: " +
                expected.name);
        }
    }
    for (const auto& [bound_port, _] :
         component.medium_bindings) {
        const auto expected = std::find_if(
            expected_ports.begin(), expected_ports.end(),
            [&](const auto& port) {
                return port.name == bound_port;
            });
        if (expected == expected_ports.end()) {
            throw std::invalid_argument(
                "component '" + component.id +
                "' binds a medium to unknown port for kind '" +
                component.kind + "': " + bound_port);
        }
    }
    for (const auto& [bound_port, _] :
         component.material_bindings) {
        const auto expected = std::find_if(
            expected_ports.begin(), expected_ports.end(),
            [&](const auto& port) {
                return port.name == bound_port;
            });
        if (expected == expected_ports.end()) {
            throw std::invalid_argument(
                "component '" + component.id +
                "' binds a material to unknown port for kind '" +
                component.kind + "': " + bound_port);
        }
    }

    std::map<std::string, const ArtifactModelDescriptor*>
        declared_artifacts;
    for (const auto& artifact : descriptor.artifacts) {
        declared_artifacts.emplace(artifact.role, &artifact);
        if (artifact.required &&
            component.artifact_bindings.find(artifact.role) ==
                component.artifact_bindings.end()) {
            throw std::invalid_argument(
                "component '" + component.id +
                "' is missing required artifact binding: " +
                artifact.role);
        }
    }
    for (const auto& [role, _] :
         component.artifact_bindings) {
        if (declared_artifacts.find(role) ==
            declared_artifacts.end()) {
            throw std::invalid_argument(
                "component '" + component.id +
                "' supplies unknown artifact role for kind '" +
                component.kind + "': " + role);
        }
    }
}

void resolve_component_artifacts(
    ComponentCompileContext& context,
    const ComponentModel& model,
    const EngineeringArtifactRegistry& artifact_registry) {
    for (const auto& artifact : model.descriptor().artifacts) {
        const auto binding =
            context.component.artifact_bindings.find(
                artifact.role);
        if (binding ==
            context.component.artifact_bindings.end()) {
            continue;
        }
        context.artifacts.emplace(
            artifact.role,
            artifact_registry.require_artifact(
                binding->second, artifact.artifact_type));
    }
}

std::string_view capability_name(physics::PropertyCapability capability) {
    switch (capability) {
        case physics::PropertyCapability::state_pt: return "state_pt";
        case physics::PropertyCapability::state_ph: return "state_ph";
        case physics::PropertyCapability::state_ph_derivatives:
            return "state_ph_derivatives";
        case physics::PropertyCapability::state_ps: return "state_ps";
        case physics::PropertyCapability::saturation_p:
            return "saturation_p";
        case physics::PropertyCapability::transport: return "transport";
        case physics::PropertyCapability::surface_tension:
            return "surface_tension";
    }
    return "unknown";
}

void validate_property_capabilities(const ComponentCompileContext& context,
                                    const ComponentModel& model) {
    for (const auto capability : model.descriptor().required_property_capabilities) {
        for (const auto& [port, package] : context.port_properties) {
            if (!model.requires_property_capability_on_port(
                    capability, port)) {
                continue;
            }
            if (!package->supports(capability)) {
                throw std::invalid_argument(
                    "component '" + context.component.id + "' port '" + port +
                    "' requires property capability '" +
                    std::string(capability_name(capability)) +
                    "' unsupported by backend '" + std::string(package->name()) + "'");
            }
        }
    }
}

template <typename Builder>
ProblemStructureReport structural_degree_of_freedom_report(
    const Builder& system) {
    const std::size_t variable_count =
        system.variables().size();
    std::vector<std::string> variable_names;
    variable_names.reserve(variable_count);
    for (const auto& variable : system.variables()) {
        variable_names.push_back(variable.name);
    }
    std::vector<std::string> equation_names;
    std::vector<std::vector<std::size_t>> incidence;
    equation_names.reserve(system.equations().size());
    incidence.reserve(system.equations().size());
    for (const auto& equation : system.equations()) {
        equation_names.push_back(equation.name);
        if (equation.sparsity_variables.empty()) {
            std::vector<std::size_t> dense(variable_count);
            std::iota(dense.begin(), dense.end(), 0);
            incidence.push_back(std::move(dense));
        } else {
            incidence.push_back(equation.sparsity_variables);
        }
    }
    return analyze_incidence_structure(
        variable_names, equation_names, incidence);
}

std::string summarize_candidates(
    const std::vector<std::string>& names) {
    constexpr std::size_t maximum_names = 8;
    std::string summary;
    const std::size_t shown =
        std::min(names.size(), maximum_names);
    for (std::size_t index = 0; index < shown; ++index) {
        if (!summary.empty()) summary += ", ";
        summary += names.at(index);
    }
    if (names.size() > shown) {
        summary += ", ... (" +
            std::to_string(names.size() - shown) +
            " more)";
    }
    return summary;
}

std::string summarize_structural_regions(
    const ProblemStructureReport& report,
    StructuralRegionKind kind) {
    constexpr std::size_t maximum_regions = 4;
    std::string summary;
    std::size_t shown = 0;
    for (const auto& region : report.structural_regions) {
        if (region.kind != kind) continue;
        if (shown == maximum_regions) {
            summary += "; ...";
            break;
        }
        if (!summary.empty()) summary += "; ";
        summary += "{variables: " +
            (region.variable_names.empty()
                 ? std::string{"none"}
                 : summarize_candidates(region.variable_names)) +
            "; equations: " +
            (region.residual_names.empty()
                 ? std::string{"none"}
                 : summarize_candidates(region.residual_names)) +
            "}";
        ++shown;
    }
    return summary;
}

template <typename Builder>
ProblemStructureReport validate_degree_of_freedom(
    const std::string& model_id,
    const Builder& system) {
    const std::size_t variables = system.variables().size();
    const std::size_t equations = system.residuals().size();
    const auto structure =
        structural_degree_of_freedom_report(system);
    const auto& unmatched_variables =
        structure.unmatched_variable_names;
    const auto& unmatched_equations =
        structure.unmatched_residual_names;
    const auto underdetermined_regions =
        summarize_structural_regions(
            structure,
            StructuralRegionKind::underdetermined);
    const auto overdetermined_regions =
        summarize_structural_regions(
            structure,
            StructuralRegionKind::overdetermined);
    if (variables == equations) {
        if (unmatched_variables.empty() &&
            unmatched_equations.empty()) {
            return structure;
        }
        throw std::invalid_argument(
            "model '" + model_id +
            "' is square but structurally singular: " +
            std::to_string(variables) + " variables and " +
            std::to_string(equations) + " equations" +
            (unmatched_variables.empty()
                 ? std::string{}
                 : "; unmatched variable candidate(s): " +
                       summarize_candidates(
                           unmatched_variables)) +
            (unmatched_equations.empty()
                 ? std::string{}
                 : "; unmatched equation candidate(s): " +
                       summarize_candidates(
                           unmatched_equations)) +
            (underdetermined_regions.empty()
                 ? std::string{}
                 : "; underdetermined structural region(s): " +
                       underdetermined_regions) +
            (overdetermined_regions.empty()
                 ? std::string{}
                 : "; overdetermined structural region(s): " +
                       overdetermined_regions));
    }
    const bool under_specified = variables > equations;
    const std::size_t difference = under_specified
                                       ? variables - equations
                                       : equations - variables;
    const auto& candidates = under_specified
        ? unmatched_variables
        : unmatched_equations;
    const std::string candidate_diagnostic =
        candidates.empty()
        ? std::string{}
        : std::string{
              under_specified
                  ? "; unmatched variable candidate(s): "
                  : "; unmatched equation candidate(s): "} +
              summarize_candidates(candidates);
    const auto& regions = under_specified
        ? underdetermined_regions
        : overdetermined_regions;
    const std::string region_diagnostic =
        regions.empty()
        ? std::string{}
        : std::string{
              under_specified
                  ? "; underdetermined structural region(s): "
                  : "; overdetermined structural region(s): "} +
              regions;
    throw std::invalid_argument(
        "model '" + model_id + "' is " +
        (under_specified ? "under-specified" : "over-specified") +
        ": " + std::to_string(variables) + " variables and " +
        std::to_string(equations) + " equations; " +
        std::to_string(difference) +
        (under_specified
             ? " additional independent equation(s) or specification(s) required"
             : " equation(s) or specification(s) must be removed") +
        candidate_diagnostic +
        region_diagnostic);
}

EvaluationStatus property_failure(const physics::PropertyResult& result) {
    if (result.status == physics::PropertyStatus::backend_error)
        return EvaluationStatus::fatal(result.message);
    return EvaluationStatus::recoverable(result.message);
}

bool add_transient_temperature_specification(
    const std::string& key,
    const ScalarValue& scalar,
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry_registry,
    const std::map<
        std::string,
        std::shared_ptr<const physics::PropertyPackage>>&
        medium_properties,
    const std::map<std::string, std::size_t>& variable_indices,
    const std::string& case_id,
    DaeEquationSystemBuilder& system,
    CompiledTransientModelGraph& graph) {
    const auto first_dot = key.find('.');
    const auto second_dot = first_dot == std::string::npos
        ? std::string::npos
        : key.find('.', first_dot + 1);
    if (first_dot == std::string::npos ||
        second_dot == std::string::npos ||
        key.find('.', second_dot + 1) != std::string::npos ||
        key.substr(second_dot + 1) != "T") {
        return false;
    }
    const auto component_id = key.substr(0, first_dot);
    const auto port_name = key.substr(
        first_dot + 1, second_dot - first_dot - 1);
    const auto component = std::find_if(
        document.components.begin(), document.components.end(),
        [&](const auto& candidate) {
            return candidate.id == component_id;
        });
    if (component == document.components.end()) return false;
    const auto descriptor = registry.require_model(
        component->kind).instance_descriptor(*component);
    const auto port = std::find_if(
        descriptor.ports.begin(), descriptor.ports.end(),
        [&](const auto& candidate) {
            return candidate.name == port_name;
        });
    if (port == descriptor.ports.end()) return false;

    const auto pressure = variable_indices.find(
        variable_key(component_id, port_name, "p"));
    const auto enthalpy = variable_indices.find(
        variable_key(component_id, port_name, "h"));
    if (pressure == variable_indices.end() ||
        enthalpy == variable_indices.end()) {
        throw std::logic_error(
            "temperature specification is missing primary variables: " +
            key);
    }
    const auto residual_name = "fixed." + case_id + "." + key;
    std::size_t residual_index = 0;
    if (port->domain == "fluid") {
        const auto medium_id = require_medium_binding(
            *component, port_name);
        const auto package = medium_properties.find(medium_id);
        if (package == medium_properties.end()) {
            throw std::logic_error(
                "compiled medium property package missing: " + medium_id);
        }
        if (!package->second->supports(
                physics::PropertyCapability::state_ph)) {
            throw std::invalid_argument(
                "temperature specification '" + key +
                "' requires property capability 'state_ph'");
        }
        residual_index = system.add_checked_equation(
            residual_name,
            [properties = package->second,
             pressure = pressure->second,
             enthalpy = enthalpy->second,
             target = scalar.value_si](
                double, const std::vector<double>& state,
                const std::vector<double>&, double& residual) {
                const auto result = properties->state_ph(
                    state.at(pressure), state.at(enthalpy));
                if (!result.ok()) return property_failure(result);
                residual = result.state.temperature_k - target;
                return EvaluationStatus::success();
            },
            std::max(std::abs(scalar.value_si), 1.0));
    } else if (port->domain == "material") {
        const auto material_id = require_material_binding(
            *component, port_name);
        const auto& material = find_material(document, material_id);
        const auto package = thermochemistry_registry.create(
            material.backend, material.mechanism, material.phase);
        if (!material.package_version.empty() &&
            material.package_version != package->version()) {
            throw std::invalid_argument(
                "material '" + material.id +
                "' requests thermochemistry package version '" +
                material.package_version + "' but backend '" +
                material.backend + "' provides version '" +
                std::string(package->version()) + "'");
        }
        if (!package->supports(
                physics::ThermochemistryCapability::state_ph)) {
            throw std::invalid_argument(
                "temperature specification '" + key +
                "' requires thermochemistry capability 'state_ph'");
        }
        std::vector<std::size_t> flows;
        flows.reserve(material.species.size());
        for (const auto& species : material.species) {
            const auto flow = variable_indices.find(variable_key(
                component_id, port_name, "m_dot[" + species + "]"));
            if (flow == variable_indices.end()) {
                throw std::logic_error(
                    "material temperature specification is missing "
                    "species flow: " + species);
            }
            flows.push_back(flow->second);
        }
        residual_index = system.add_checked_equation(
            residual_name,
            [properties = std::move(package),
             species = material.species, flows = std::move(flows),
             pressure = pressure->second,
             enthalpy = enthalpy->second,
             target = scalar.value_si](
                double, const std::vector<double>& state,
                const std::vector<double>&, double& residual) {
                std::vector<double> fractions(flows.size(), 0.0);
                double total_flow = 0.0;
                for (std::size_t index = 0;
                     index < flows.size(); ++index) {
                    const double flow = state.at(flows.at(index));
                    if (!std::isfinite(flow) || flow < 0.0) {
                        return EvaluationStatus::recoverable(
                            "material temperature specification requires "
                            "finite nonnegative species flows");
                    }
                    fractions.at(index) = flow;
                    total_flow += flow;
                }
                if (!std::isfinite(total_flow) || total_flow <= 0.0) {
                    return EvaluationStatus::recoverable(
                        "material temperature specification requires "
                        "positive total mass flow");
                }
                for (auto& fraction : fractions) {
                    fraction /= total_flow;
                }
                const auto result = properties->state_ph(
                    state.at(pressure), state.at(enthalpy),
                    physics::SpeciesComposition{
                        physics::CompositionBasis::mass_fraction,
                        species, std::move(fractions)});
                if (!result.ok()) {
                    return result.status ==
                            physics::PropertyStatus::backend_error
                        ? EvaluationStatus::fatal(result.message)
                        : EvaluationStatus::recoverable(result.message);
                }
                residual = result.state.thermodynamic.temperature_k -
                    target;
                return EvaluationStatus::success();
            },
            std::max(std::abs(scalar.value_si), 1.0));
    } else {
        return false;
    }
    graph.fixed_value_equations.push_back(
        system.residuals().at(residual_index).name);
    return true;
}

const TransientVariableDescriptor* find_transient_variable(
    const ComponentModelDescriptor& descriptor,
    const std::string& port_name,
    const std::string& variable_name) {
    const auto it = std::find_if(
        descriptor.transient_variables.begin(),
        descriptor.transient_variables.end(),
        [&](const auto& variable) {
            return variable.port_name == port_name &&
                   variable.variable_name == variable_name;
        });
    return it == descriptor.transient_variables.end() ? nullptr : &*it;
}

EvaluationStatus evaluate_steady_equation(
    const Equation& equation,
    const std::vector<double>& state,
    double& residual) {
    if (equation.evaluate_checked) {
        return equation.evaluate_checked(state, residual);
    }
    if (equation.evaluate) {
        residual = equation.evaluate(state);
        return EvaluationStatus::success();
    }
    return EvaluationStatus::fatal(
        "quasi-steady equation has no target evaluator: " +
        equation.name);
}

void add_quasi_steady_transient_equations(
    const ComponentModel& model,
    const ComponentCompileContext& context,
    DaeEquationSystemBuilder& dae_system) {
    EquationSystemBuilder steady_system;
    for (const auto& variable : dae_system.variables()) {
        steady_system.add_variable(
            variable.name, variable.initial_value, variable.scale,
            variable.lower_bound, variable.upper_bound);
    }
    model.add_equations(context, steady_system);

    for (const auto& equation : steady_system.equations()) {
        if (equation.assemble_sparse &&
            !equation.sparsity_variables.empty()) {
            dae_system.add_sparse_equation(
                equation.name, equation.sparsity_variables,
                [equation](
                    double, const std::vector<double>& state,
                    const std::vector<double>&, double& residual,
                    std::vector<DaeEquationPartial>& jacobian) {
                    const auto status = evaluate_steady_equation(
                        equation, state, residual);
                    if (!status.ok()) return status;
                    std::vector<EquationPartial> steady_jacobian;
                    residual = equation.assemble_sparse(
                        state, steady_jacobian);
                    jacobian.reserve(
                        jacobian.size() + steady_jacobian.size());
                    for (const auto& partial : steady_jacobian) {
                        jacobian.push_back({
                            partial.variable,
                            partial.derivative, 0.0});
                    }
                    return EvaluationStatus::success();
                },
                equation.scale);
            continue;
        }
        dae_system.add_checked_equation(
            equation.name,
            [equation](
                double, const std::vector<double>& state,
                const std::vector<double>&, double& residual) {
                return evaluate_steady_equation(
                    equation, state, residual);
            },
            equation.scale);
    }
}

}  // namespace

bool ComponentModel::requires_property_capability_on_port(
    physics::PropertyCapability capability,
    std::string_view) const {
    return std::find(
               descriptor().required_property_capabilities.begin(),
               descriptor().required_property_capabilities.end(),
               capability) !=
        descriptor().required_property_capabilities.end();
}

void ComponentModel::add_transient_equations(
    const ComponentCompileContext&,
    DaeEquationSystemBuilder&) const {
    throw std::logic_error(
        "component does not implement transient equations: " +
        descriptor().kind);
}

void ComponentModel::add_transient_events(
    const ComponentCompileContext&,
    std::vector<DaeEvent>&) const {}

MetadataComponentModel::MetadataComponentModel(ComponentModelDescriptor descriptor)
    : descriptor_(std::move(descriptor)) {}

void MetadataComponentModel::add_equations(const ComponentCompileContext&, EquationSystemBuilder&) const {
    // Metadata-only MVP component models validate port contracts and reserve extension points.
    // Physical component residuals are added by specialized ComponentModel implementations.
}

void MetadataComponentModel::add_transient_equations(
    const ComponentCompileContext&,
    DaeEquationSystemBuilder&) const {
    // Boundary components contribute variables and topology but no intrinsic equations.
}

ComponentRegistry::ComponentRegistry() {
    for (auto descriptor : standard_connector_domains()) {
        register_connector_domain(std::move(descriptor));
    }
}

void ComponentRegistry::register_connector_domain(
    ConnectorDomainDescriptor descriptor) {
    if (descriptor.domain.empty() ||
        descriptor.contract_version.empty() ||
        descriptor.connection_kind.empty() ||
        descriptor.variables.empty()) {
        throw std::invalid_argument(
            "connector domain registration requires domain, "
            "contract version, connection kind, and variables");
    }
    std::set<std::string> variable_names;
    for (const auto& variable : descriptor.variables) {
        if (variable.name.empty() ||
            variable.dimension.empty() ||
            !std::isfinite(variable.initial_value) ||
            !std::isfinite(variable.scale) ||
            variable.scale <= 0.0) {
            throw std::invalid_argument(
                "connector variable descriptors require a "
                "name, dimension, finite initial value, and "
                "positive finite scale");
        }
        if (!variable_names.insert(variable.name).second) {
            throw std::invalid_argument(
                "duplicate connector variable for domain '" +
                descriptor.domain + "': " + variable.name);
        }
        if (variable.expand_species &&
            variable.name.find("species") ==
                std::string::npos) {
            throw std::invalid_argument(
                "species-expanded connector variable must "
                "contain the species placeholder");
        }
    }
    const auto domain = descriptor.domain;
    if (!connector_domains_
             .emplace(domain, std::move(descriptor))
             .second) {
        throw std::invalid_argument(
            "duplicate connector domain registration: " +
            domain);
    }
}

const ConnectorDomainDescriptor&
ComponentRegistry::require_connector_domain(
    const std::string& domain) const {
    const auto found = connector_domains_.find(domain);
    if (found == connector_domains_.end()) {
        throw std::invalid_argument(
            "unsupported port domain during graph "
            "compilation: " + domain);
    }
    return found->second;
}

std::vector<ConnectorDomainDescriptor>
ComponentRegistry::connector_domain_descriptors() const {
    std::vector<ConnectorDomainDescriptor> descriptors;
    descriptors.reserve(connector_domains_.size());
    for (const auto& [unused, descriptor] :
         connector_domains_) {
        (void)unused;
        descriptors.push_back(descriptor);
    }
    return descriptors;
}

void ComponentRegistry::register_runtime_extension(
    RuntimeExtensionDescriptor descriptor) {
    if (descriptor.package_id.empty() ||
        descriptor.package_version.empty()) {
        throw std::invalid_argument(
            "runtime extension registration requires a "
            "non-empty package ID and version");
    }
    const auto package_id = descriptor.package_id;
    if (!runtime_extensions_
             .emplace(package_id, std::move(descriptor))
             .second) {
        throw std::invalid_argument(
            "duplicate runtime extension package: " +
            package_id);
    }
}

std::vector<RuntimeExtensionDescriptor>
ComponentRegistry::runtime_extension_descriptors() const {
    std::vector<RuntimeExtensionDescriptor> descriptors;
    descriptors.reserve(runtime_extensions_.size());
    for (const auto& [unused, descriptor] :
         runtime_extensions_) {
        (void)unused;
        descriptors.push_back(descriptor);
    }
    return descriptors;
}

void ComponentRegistry::register_model(std::shared_ptr<const ComponentModel> model) {
    if (!model) {
        throw std::invalid_argument("component model registration must not be null");
    }
    const std::string& kind = model->descriptor().kind;
    if (kind.empty()) {
        throw std::invalid_argument("component model kind must not be empty");
    }
    validate_component_descriptor(model->descriptor());
    std::set<std::string> mode_names;
    for (const auto& mode : model->descriptor().supported_modes) {
        if (mode.empty() || !mode_names.insert(mode).second) {
            throw std::invalid_argument(
                "component model modes must be non-empty and unique: " +
                kind);
        }
    }
    if (model->descriptor().supported_modes.empty() !=
        model->descriptor().default_mode.empty() ||
        (!model->descriptor().default_mode.empty() &&
         !mode_names.contains(model->descriptor().default_mode))) {
        throw std::invalid_argument(
            "component model default mode must name one supported mode: " +
            kind);
    }
    for (const auto& port : model->descriptor().ports) {
        (void)require_connector_domain(port.domain);
    }
    for (const auto& group : model->descriptor().port_groups) {
        (void)require_connector_domain(group.domain);
    }
    std::set<std::string> transient_names;
    for (const auto& variable : model->descriptor().transient_variables) {
        if (variable.port_name.empty() || variable.variable_name.empty()) {
            throw std::invalid_argument(
                "transient variable descriptors require port and variable names");
        }
        const std::string variable_key =
            variable.port_name + "." + variable.variable_name;
        if (!transient_names.insert(variable_key).second) {
            throw std::invalid_argument(
                "duplicate transient port variable descriptor: " +
                kind + "." + variable_key);
        }
        if (!std::isfinite(variable.derivative_scale) ||
            variable.derivative_scale <= 0.0) {
            throw std::invalid_argument(
                "transient variable derivative scale must be positive and finite");
        }
        const auto port = std::find_if(
            model->descriptor().ports.begin(),
            model->descriptor().ports.end(),
            [&](const auto& candidate) {
                return candidate.name == variable.port_name;
            });
        if (port == model->descriptor().ports.end()) {
            throw std::invalid_argument(
                "transient variable references unknown descriptor port: " +
                kind + "." + variable.port_name);
        }
        const auto& canonical =
            require_connector_domain(port->domain).variables;
        if (std::none_of(
                canonical.begin(), canonical.end(),
                [&](const auto& candidate) {
                    return candidate.name == variable.variable_name;
                })) {
            throw std::invalid_argument(
                "transient variable is not canonical for port domain: " +
                kind + "." + variable.port_name + "." +
                variable.variable_name);
        }
    }
    const bool has_differential_internal = std::any_of(
        model->descriptor().internal_variables.begin(),
        model->descriptor().internal_variables.end(),
        [](const auto& variable) {
            return variable.kind == DaeVariableKind::differential;
        });
    if (!model->descriptor().supports_transient &&
        (!model->descriptor().transient_variables.empty() ||
         has_differential_internal)) {
        throw std::invalid_argument(
            "steady-only component declares transient variables: " + kind);
    }
    if (model->descriptor().uses_quasi_steady_transient_equations &&
        (!model->descriptor().supports_steady ||
         !model->descriptor().supports_transient ||
         !model->descriptor().transient_variables.empty() ||
         has_differential_internal)) {
        throw std::invalid_argument(
            "quasi-steady transient component must support steady and "
            "transient compilation without differential variables: " +
            kind);
    }
    std::set<std::string> internal_names;
    for (const auto& variable : model->descriptor().internal_variables) {
        if (!internal_names.insert(variable.name).second) {
            throw std::invalid_argument(
                "duplicate internal component variable descriptor: " +
                kind + "." + variable.name);
        }
        if (variable.name.empty()) {
            throw std::invalid_argument(
                "internal component variable name must not be empty");
        }
        if (!std::isfinite(variable.initial_value) ||
            !std::isfinite(variable.initial_derivative) ||
            !std::isfinite(variable.state_scale) ||
            variable.state_scale <= 0.0 ||
            !std::isfinite(variable.derivative_scale) ||
            variable.derivative_scale <= 0.0 ||
            std::isnan(variable.lower_bound) ||
            std::isnan(variable.upper_bound) ||
            variable.lower_bound > variable.upper_bound ||
            variable.initial_value < variable.lower_bound ||
            variable.initial_value > variable.upper_bound) {
            throw std::invalid_argument(
                "invalid internal component variable descriptor: " +
                kind + "." + variable.name);
        }
    }
    if (!models_.emplace(kind, std::move(model)).second) {
        throw std::invalid_argument("duplicate component model kind registration: " + kind);
    }
}

const ComponentModel& ComponentRegistry::require_model(const std::string& kind) const {
    const auto it = models_.find(kind);
    if (it == models_.end()) {
        throw std::invalid_argument("no component model registered for kind: " + kind);
    }
    return *it->second;
}

bool ComponentRegistry::contains(const std::string& kind) const {
    return models_.find(kind) != models_.end();
}

std::vector<std::string> ComponentRegistry::kinds() const {
    std::vector<std::string> out;
    out.reserve(models_.size());
    for (const auto& [kind, _] : models_) {
        out.push_back(kind);
    }
    return out;
}

std::vector<ComponentModelDescriptor> ComponentRegistry::descriptors() const {
    std::vector<ComponentModelDescriptor> out;
    out.reserve(models_.size());
    for (const auto& [_, model] : models_) {
        out.push_back(model->descriptor());
    }
    return out;
}

ComponentRegistry make_default_component_registry() {
    ComponentRegistry registry;
    register_boundary_component_models(registry);
    register_turbomachinery_component_models(registry);
    register_material_turbomachinery_component_models(registry);
    register_transport_component_models(registry);
    register_valve_component_models(registry);
    register_phase_separation_component_models(registry);
    register_heat_transfer_component_models(registry);
    register_dynamic_heat_transfer_component_models(registry);
    register_dynamic_material_heat_transfer_component_models(registry);
    register_dynamic_material_rigid_volume_component_models(registry);
    register_fluid_inventory_component_models(registry);
    register_drum_component_models(registry);
    register_storage_component_models(registry);
    register_power_component_models(registry);
    register_control_component_models(registry);
    register_combustion_component_models(registry);
    return registry;
}

CompiledModelGraph compile_model_graph(const ModelDocument& document,
                                       const ComponentRegistry& registry,
                                       const std::string& case_id) {
    return compile_model_graph(document, registry,
                               physics::make_default_property_package_registry(),
                               case_id);
}

CompiledModelGraph compile_model_graph(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::PropertyPackageRegistry& property_registry,
    const std::string& case_id) {
    return compile_model_graph(
        document, registry, property_registry,
        EngineeringArtifactRegistry{}, case_id);
}

CompiledModelGraph compile_model_graph(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::PropertyPackageRegistry& property_registry,
    const EngineeringArtifactRegistry& artifact_registry,
    const std::string& case_id) {
    return compile_model_graph(
        document, registry, property_registry,
        artifact_registry,
        physics::ThermochemistryPackageRegistry{}, case_id);
}

CompiledModelGraph compile_flat_model_graph(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::PropertyPackageRegistry& property_registry,
    const EngineeringArtifactRegistry& artifact_registry,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry_registry,
    const std::string& case_id) {
    const CaseDefinition* active_case = select_case(document, case_id);
    if (active_case != nullptr &&
        (!active_case->input_schedules.empty() ||
         !active_case->state_events.empty())) {
        throw std::invalid_argument(
            "case '" + active_case->id +
            "' declares dynamic schedules/events and requires transient "
            "compilation");
    }

    EquationSystemBuilder system;
    CompiledModelGraph graph;
    graph.model_id = document.model_id;
    if (active_case != nullptr) {
        graph.case_id = active_case->id;
    }

    std::map<std::string, std::size_t> variable_indices;
    auto medium_properties =
        create_medium_properties(document, property_registry);
    std::set<std::string> seen_case_keys;
    const auto parameter_overrides =
        case_parameter_overrides(
            document, registry, active_case);
    std::set<std::string> seen_component_modes;

    for (const ComponentDefinition& declared_component :
         document.components) {
        const auto component = effective_component(
            declared_component, parameter_overrides);
        const ComponentModel& model = registry.require_model(component.kind);
        const auto descriptor = model.instance_descriptor(component);
        validate_component_descriptor(descriptor);
        validate_component_bindings(component, descriptor);
        validate_component_parameters(component, descriptor);
        if (!descriptor.supports_steady) {
            throw std::invalid_argument(
                "component '" + component.id + "' of kind '" +
                component.kind + "' does not support steady compilation");
        }

        ComponentCompileContext context{
            component, active_case, {}, {}, {}, {}, {}, {}, {}, {}};
        const auto selected_mode = selected_component_mode(
            descriptor, active_case, component.id);
        if (!selected_mode.empty()) {
            const auto mode =
                std::make_shared<std::string>(selected_mode);
            context.active_mode = [mode]() { return *mode; };
        }
        if (active_case != nullptr &&
            active_case->component_modes.contains(component.id)) {
            seen_component_modes.insert(component.id);
        }
        for (const auto& port : descriptor.ports) {
            std::string medium_id;
            if (port.domain == "fluid") {
                medium_id =
                    require_medium_binding(component, port.name);
                const auto package =
                    medium_properties.find(medium_id);
                if (package == medium_properties.end())
                    throw std::logic_error("compiled medium property package missing: " +
                                           medium_id);
                context.port_properties.emplace(
                    port.name, package->second);
            } else if (port.domain == "material") {
                medium_id =
                    require_material_binding(component, port.name);
                const auto& material =
                    find_material(document, medium_id);
                context.port_species.emplace(
                    port.name, material.species);
                if (!descriptor
                         .required_thermochemistry_capabilities
                         .empty()) {
                    auto package = thermochemistry_registry.create(
                        material.backend, material.mechanism,
                        material.phase);
                    if (!material.package_version.empty() &&
                        material.package_version !=
                            package->version()) {
                        throw std::invalid_argument(
                            "material '" + material.id +
                            "' requests thermochemistry package "
                            "version '" + material.package_version +
                            "' but backend '" + material.backend +
                            "' provides version '" +
                            std::string(package->version()) + "'");
                    }
                    for (const auto& species : material.species) {
                        if (std::find(
                                package->species_basis().begin(),
                                package->species_basis().end(),
                                species) ==
                            package->species_basis().end()) {
                            throw std::invalid_argument(
                                "material '" + material.id +
                                "' species is absent from backend "
                                "mechanism: " + species);
                        }
                    }
                    for (const auto capability :
                         descriptor
                             .required_thermochemistry_capabilities) {
                        if (!package->supports(capability)) {
                            throw std::invalid_argument(
                                "component '" + component.id +
                                "' requires an unsupported "
                                "thermochemistry capability");
                        }
                    }
                    context.port_thermochemistry.emplace(
                        port.name, std::move(package));
                }
            }
            const auto species = context.port_species.find(
                port.name);
            std::optional<double> temperature_initialized_enthalpy;
            if (port.domain == "fluid") {
                const std::string temperature_name = variable_key(
                    component.id, port.name, "T");
                const std::string enthalpy_name = variable_key(
                    component.id, port.name, "h");
                const auto target_temperature = case_scalar_value(
                    active_case, temperature_name, true);
                const bool has_enthalpy_initial =
                    case_scalar_value(
                        active_case, enthalpy_name, false).has_value() ||
                    case_scalar_value(
                        active_case, enthalpy_name, true).has_value();
                if (target_temperature && !has_enthalpy_initial) {
                    const std::string pressure_name = variable_key(
                        component.id, port.name, "p");
                    double pressure = 101325.0;
                    if (const auto initial_pressure = case_scalar_value(
                            active_case, pressure_name, false)) {
                        pressure = *initial_pressure;
                    } else if (const auto fixed_pressure =
                                   case_scalar_value(
                                       active_case, pressure_name, true)) {
                        pressure = *fixed_pressure;
                    }
                    const auto properties =
                        context.port_properties.find(port.name);
                    if (properties != context.port_properties.end() &&
                        properties->second &&
                        properties->second->supports(
                            physics::PropertyCapability::state_pt)) {
                        const auto state = properties->second->state_pt(
                            pressure, *target_temperature);
                        if (state.ok()) {
                            temperature_initialized_enthalpy =
                                state.state.enthalpy_j_kg;
                        }
                    }
                }
            }
            for (const auto& spec : canonical_variables_for_domain(
                     registry, port.domain,
                     species == context.port_species.end()
                         ? std::vector<std::string>{}
                         : species->second)) {
                const std::string full_name =
                    variable_key(component.id, port.name, spec.name);
                double initial = spec.initial_value;
                bool initialization_anchor = false;
                if (const auto value = case_scalar_value(active_case, full_name, false)) {
                    initial = *value;
                    initialization_anchor = true;
                    seen_case_keys.insert(full_name);
                } else if (const auto fixed = case_scalar_value(active_case, full_name, true)) {
                    initial = *fixed;
                    initialization_anchor = true;
                } else if (spec.name == "h" &&
                           temperature_initialized_enthalpy) {
                    initial = *temperature_initialized_enthalpy;
                    initialization_anchor = true;
                }
                const std::size_t index = system.add_variable(full_name, initial, spec.scale);
                if (initialization_anchor) {
                    system.mark_initialization_anchor(index);
                }
                variable_indices.emplace(full_name, index);
                context.port_variables.emplace(
                    port.name + "." + spec.name, index);
                graph.port_variables.push_back(
                    CompiledPortVariable{
                        component.id, port.name, spec.name,
                        full_name, port.domain, medium_id,
                        spec.dimension, port.direction,
                        explicit_boundary_sign(
                            descriptor),
                        index});
            }
        }
        for (const auto& variable : descriptor.internal_variables) {
            // Dual-mode dynamic components own their internal variables in
            // the DAE formulation. A steady-only model may instead expose
            // algebraic work variables required by its steady equations.
            if ((descriptor.supports_transient &&
                 !descriptor.uses_quasi_steady_transient_equations) ||
                variable.kind != DaeVariableKind::algebraic) {
                continue;
            }
            const std::string full_name =
                component.id + "." + variable.name;
            double initial = variable.initial_value;
            bool initialization_anchor = false;
            if (const auto value =
                    case_scalar_value(active_case, full_name, false)) {
                initial = *value;
                initialization_anchor = true;
                seen_case_keys.insert(full_name);
            } else if (const auto fixed =
                           case_scalar_value(active_case, full_name, true)) {
                initial = *fixed;
                initialization_anchor = true;
            }
            const auto index = system.add_variable(
                full_name, initial, variable.state_scale,
                variable.lower_bound, variable.upper_bound);
            if (initialization_anchor) {
                system.mark_initialization_anchor(index);
            }
            variable_indices.emplace(full_name, index);
            context.internal_variables.emplace(variable.name, index);
            graph.internal_variables.push_back({
                component.id, variable.name, full_name,
                variable.dimension, index});
        }
        resolve_component_artifacts(
            context, model, artifact_registry);
        validate_property_capabilities(context, model);
        model.add_equations(context, system);
    }
    if (active_case != nullptr &&
        seen_component_modes.size() !=
            active_case->component_modes.size()) {
        for (const auto& [component_id, _] :
             active_case->component_modes) {
            if (!seen_component_modes.contains(component_id)) {
                throw std::invalid_argument(
                    "case '" + active_case->id +
                    "' component_modes references unknown component: " +
                    component_id);
            }
        }
    }

    std::map<std::string, std::size_t> connection_counts;
    for (const ConnectionDefinition& connection : document.connections) {
        const auto endpoints = validate_connection(
            document, registry, connection, connection_counts);
        for (const auto& spec :
             canonical_variables_for_domain(
                 registry,
                 endpoints.domain, endpoints.species)) {
            const std::string from_key = variable_key(
                endpoints.from_component, endpoints.from_port, spec.name);
            const std::string to_key = variable_key(
                endpoints.to_component, endpoints.to_port, spec.name);
            const auto from_it = variable_indices.find(from_key);
            const auto to_it = variable_indices.find(to_key);
            if (from_it == variable_indices.end() || to_it == variable_indices.end()) {
                throw std::logic_error("compiled connection variable missing for: " + connection.id);
            }
            const std::string residual_name = "connection." + connection.id + "." + spec.name;
            const std::vector<LinearTerm> terms{
                {from_it->second, 1.0},
                {to_it->second, -1.0}};
            system.add_initialization_relation(terms, 0.0);
            if (system.classify_linear_equation(terms, 0.0) ==
                LinearEquationRelation::redundant) {
                graph.reduced_connection_equations.push_back(
                    residual_name);
                continue;
            }
            const std::size_t residual_index = system.add_linear_equation(
                residual_name,
                terms,
                0.0,
                spec.scale);
            graph.connection_equations.push_back(CompiledConnectionEquation{connection.id, spec.name,
                                                                            residual_name, residual_index});
        }
    }
    mark_unconnected_system_boundaries(
        graph.port_variables, connection_counts);

    if (active_case != nullptr) {
        const auto boundary_continuation_option =
            active_case->solver_options.find("boundary_continuation");
        if (boundary_continuation_option !=
                active_case->solver_options.end() &&
            boundary_continuation_option->second.dimension !=
                "dimensionless") {
            throw std::invalid_argument(
                "case '" + active_case->id +
                "' boundary_continuation solver option must be "
                "dimensionless");
        }
        const bool boundary_continuation =
            boundary_continuation_option !=
                active_case->solver_options.end() &&
            boundary_continuation_option->second.value_si != 0.0;
        for (const auto& [key, scalar] : active_case->fixed_values) {
            const auto variable_it = variable_indices.find(key);
            if (variable_it == variable_indices.end()) {
                const std::size_t first_dot = key.find('.');
                const std::size_t second_dot =
                    first_dot == std::string::npos
                        ? std::string::npos
                        : key.find('.', first_dot + 1);
                const bool is_temperature_specification =
                    first_dot != std::string::npos &&
                    second_dot != std::string::npos &&
                    key.find('.', second_dot + 1) ==
                        std::string::npos &&
                    key.substr(second_dot + 1) == "T";
                if (is_temperature_specification) {
                    const std::string component_id =
                        key.substr(0, first_dot);
                    const std::string port_name = key.substr(
                        first_dot + 1,
                        second_dot - first_dot - 1);
                    const ComponentDefinition* component = nullptr;
                    for (const auto& candidate :
                         document.components) {
                        if (candidate.id == component_id) {
                            component = &candidate;
                            break;
                        }
                    }
                    if (component != nullptr) {
                        const auto& component_model =
                            registry.require_model(component->kind);
                        const auto component_descriptor =
                            component_model.instance_descriptor(
                                *component);
                        const auto port = std::find_if(
                            component_descriptor.ports.begin(),
                            component_descriptor.ports.end(),
                            [&](const auto& candidate) {
                                return candidate.name == port_name;
                            });
                        if (port !=
                                component_descriptor.ports.end() &&
                            port->domain == "fluid") {
                            const std::string& medium_id =
                                require_medium_binding(
                                    *component, port_name);
                            const auto package =
                                medium_properties.find(
                                    medium_id);
                            if (package ==
                                medium_properties.end()) {
                                throw std::logic_error(
                                    "compiled medium property package missing: " +
                                    medium_id);
                            }
                            if (!package->second->supports(
                                    physics::PropertyCapability::
                                        state_ph)) {
                                throw std::invalid_argument(
                                    "temperature specification '" +
                                    key +
                                    "' requires property capability 'state_ph'");
                            }
                            const auto pressure =
                                variable_indices.find(
                                    variable_key(
                                        component_id, port_name,
                                        "p"));
                            const auto enthalpy =
                                variable_indices.find(
                                    variable_key(
                                        component_id, port_name,
                                        "h"));
                            if (pressure ==
                                    variable_indices.end() ||
                                enthalpy ==
                                    variable_indices.end()) {
                                throw std::logic_error(
                                    "fluid temperature specification is missing primary variables: " +
                                    key);
                            }
                            const std::string residual_name =
                                "fixed." + active_case->id +
                                "." + key;
                            const std::size_t residual_index =
                                system.add_continuation_checked_equation(
                                    residual_name,
                                    [properties =
                                         package->second,
                                     pressure =
                                         pressure->second,
                                     enthalpy =
                                         enthalpy->second,
                                     target =
                                         scalar.value_si,
                                     boundary_continuation](
                                        const std::vector<double>&
                                            x,
                                        const std::vector<double>&
                                            anchor,
                                        double continuation_parameter,
                                        double& residual) {
                                        const auto state =
                                            properties->state_ph(
                                                x.at(pressure),
                                                x.at(enthalpy));
                                        if (!state.ok()) {
                                            return property_failure(
                                                state);
                                        }
                                        double staged_target = target;
                                        if (boundary_continuation &&
                                            continuation_parameter < 1.0) {
                                            const auto anchor_state =
                                                properties->state_ph(
                                                    anchor.at(pressure),
                                                    anchor.at(enthalpy));
                                            if (!anchor_state.ok()) {
                                                return property_failure(
                                                    anchor_state);
                                            }
                                            staged_target =
                                                anchor_state.state
                                                    .temperature_k +
                                                continuation_parameter *
                                                    (target -
                                                     anchor_state.state
                                                         .temperature_k);
                                        }
                                        residual =
                                            state.state
                                                .temperature_k -
                                            staged_target;
                                        return EvaluationStatus::
                                            success();
                                    },
                                    std::max(
                                        std::abs(
                                            scalar.value_si),
                                        1.0));
                            graph.fixed_value_equations.push_back(
                                system.residuals()
                                    .at(residual_index)
                                    .name);
                            continue;
                        }
                        if (port !=
                                component_descriptor.ports.end() &&
                            port->domain == "material") {
                            const std::string& material_id =
                                require_material_binding(
                                    *component, port_name);
                            const auto& material =
                                find_material(
                                    document, material_id);
                            const auto package =
                                thermochemistry_registry.create(
                                    material.backend,
                                    material.mechanism,
                                    material.phase);
                            if (!material.package_version.empty() &&
                                material.package_version !=
                                    package->version()) {
                                throw std::invalid_argument(
                                    "material '" + material.id +
                                    "' requests thermochemistry package "
                                    "version '" +
                                    material.package_version +
                                    "' but backend '" +
                                    material.backend +
                                    "' provides version '" +
                                    std::string(package->version()) +
                                    "'");
                            }
                            if (!package->supports(
                                    physics::
                                        ThermochemistryCapability::
                                            state_ph)) {
                                throw std::invalid_argument(
                                    "temperature specification '" +
                                    key +
                                    "' requires thermochemistry "
                                    "capability 'state_ph'");
                            }
                            const auto pressure =
                                variable_indices.find(
                                    variable_key(
                                        component_id, port_name,
                                        "p"));
                            const auto enthalpy =
                                variable_indices.find(
                                    variable_key(
                                        component_id, port_name,
                                        "h"));
                            if (pressure ==
                                    variable_indices.end() ||
                                enthalpy ==
                                    variable_indices.end()) {
                                throw std::logic_error(
                                    "material temperature specification "
                                    "is missing primary variables: " +
                                    key);
                            }
                            std::vector<std::size_t> flows;
                            flows.reserve(
                                material.species.size());
                            for (const auto& species :
                                 material.species) {
                                const auto flow =
                                    variable_indices.find(
                                        variable_key(
                                            component_id,
                                            port_name,
                                            "m_dot[" + species +
                                                "]"));
                                if (flow ==
                                    variable_indices.end()) {
                                    throw std::logic_error(
                                        "material temperature "
                                        "specification is missing "
                                        "species flow: " +
                                        species);
                                }
                                flows.push_back(flow->second);
                            }
                            const std::string residual_name =
                                "fixed." + active_case->id +
                                "." + key;
                            const std::size_t residual_index =
                                system.add_continuation_checked_equation(
                                    residual_name,
                                    [properties = package,
                                     species = material.species,
                                     flows = std::move(flows),
                                     pressure =
                                         pressure->second,
                                     enthalpy =
                                         enthalpy->second,
                                     target =
                                         scalar.value_si,
                                     boundary_continuation](
                                        const std::vector<double>&
                                            x,
                                        const std::vector<double>&
                                            anchor,
                                        double continuation_parameter,
                                        double& residual) {
                                        const auto evaluate_state =
                                            [&](const auto& values) {
                                                std::vector<double>
                                                    mass_fractions(
                                                        flows.size(), 0.0);
                                                double total_flow = 0.0;
                                                for (std::size_t index = 0;
                                                     index < flows.size();
                                                     ++index) {
                                                    const double flow =
                                                        values.at(
                                                            flows.at(index));
                                                    if (!std::isfinite(flow) ||
                                                        flow < 0.0) {
                                                        return physics::
                                                            ThermochemicalResult{
                                                                {},
                                                                physics::
                                                                    PropertyStatus::
                                                                        invalid_input,
                                                                "material temperature specification requires finite nonnegative species flows"};
                                                    }
                                                    mass_fractions.at(index) =
                                                        flow;
                                                    total_flow += flow;
                                                }
                                                if (!std::isfinite(total_flow) ||
                                                    total_flow <= 0.0) {
                                                    return physics::
                                                        ThermochemicalResult{
                                                            {},
                                                            physics::
                                                                PropertyStatus::
                                                                    invalid_input,
                                                            "material temperature specification requires positive total mass flow"};
                                                }
                                                for (auto& fraction :
                                                     mass_fractions) {
                                                    fraction /= total_flow;
                                                }
                                                return properties->state_ph(
                                                    values.at(pressure),
                                                    values.at(enthalpy),
                                                    physics::SpeciesComposition{
                                                        physics::
                                                            CompositionBasis::
                                                                mass_fraction,
                                                        species,
                                                        std::move(
                                                            mass_fractions)});
                                            };
                                        const auto state = evaluate_state(x);
                                        if (!state.ok()) {
                                            return state.status ==
                                                physics::
                                                    PropertyStatus::
                                                        backend_error
                                                ? EvaluationStatus::
                                                      fatal(
                                                          state.message)
                                                : EvaluationStatus::
                                                      recoverable(
                                                          state.message);
                                        }
                                        double staged_target = target;
                                        if (boundary_continuation &&
                                            continuation_parameter < 1.0) {
                                            const auto anchor_state =
                                                evaluate_state(anchor);
                                            if (!anchor_state.ok()) {
                                                return anchor_state.status ==
                                                        physics::PropertyStatus::
                                                            backend_error
                                                    ? EvaluationStatus::fatal(
                                                          anchor_state.message)
                                                    : EvaluationStatus::
                                                          recoverable(
                                                              anchor_state.message);
                                            }
                                            const double anchor_temperature =
                                                anchor_state.state
                                                    .thermodynamic
                                                    .temperature_k;
                                            staged_target =
                                                anchor_temperature +
                                                continuation_parameter *
                                                    (target -
                                                     anchor_temperature);
                                        }
                                        residual =
                                            state.state
                                                .thermodynamic
                                                .temperature_k -
                                            staged_target;
                                        return EvaluationStatus::
                                            success();
                                    },
                                    std::max(
                                        std::abs(
                                            scalar.value_si),
                                        1.0));
                            graph.fixed_value_equations.push_back(
                                system.residuals()
                                    .at(residual_index)
                                    .name);
                            continue;
                        }
                    }
                }
                throw std::invalid_argument("case '" + active_case->id +
                                            "' fixed value references unknown variable: " + key);
            }
            const std::size_t variable = variable_it->second;
            const double target = scalar.value_si;
            const std::size_t residual_index =
                system.add_continuation_linear_equation(
                    "fixed." + active_case->id + "." + key,
                    {{variable, 1.0}}, target,
                    [variable, target, boundary_continuation](
                        const std::vector<double>& x,
                        const std::vector<double>& anchor,
                        double continuation_parameter,
                        std::vector<EquationPartial>& jacobian) {
                        const double staged_target = boundary_continuation
                            ? anchor.at(variable) +
                                continuation_parameter *
                                    (target - anchor.at(variable))
                            : target;
                        jacobian.push_back({variable, 1.0});
                        return x.at(variable) - staged_target;
                    },
                    std::max(std::abs(target), 1.0));
            graph.fixed_value_equations.push_back(system.residuals().at(residual_index).name);
        }
        for (const auto& [key, _] : active_case->initial_guesses) {
            if (variable_indices.find(key) == variable_indices.end()) {
                throw std::invalid_argument("case '" + active_case->id +
                                            "' initial guess references unknown variable: " + key);
            }
        }
    }

    graph.structure =
        validate_degree_of_freedom(document.model_id, system);
    graph.problem = system.build();
    graph.problem.continuation_path_is_complete =
        active_case != nullptr &&
        active_case->solver_options.contains("boundary_continuation") &&
        active_case->solver_options.at("boundary_continuation").value_si !=
            0.0;
    return graph;
}

CompiledModelGraph compile_model_graph(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::PropertyPackageRegistry& property_registry,
    const EngineeringArtifactRegistry& artifact_registry,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry_registry,
    const std::string& case_id) {
    return compile_flat_model_graph(
        flatten_model_document(document), registry,
        property_registry, artifact_registry,
        thermochemistry_registry, case_id);
}

CompiledTransientModelGraph compile_transient_model_graph(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const std::string& case_id) {
    return compile_transient_model_graph(
        document, registry,
        physics::make_default_property_package_registry(), case_id);
}

CompiledTransientModelGraph compile_transient_model_graph(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::PropertyPackageRegistry& property_registry,
    const std::string& case_id) {
    return compile_transient_model_graph(
        document, registry, property_registry,
        EngineeringArtifactRegistry{}, case_id);
}

CompiledTransientModelGraph compile_transient_model_graph(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::PropertyPackageRegistry& property_registry,
    const EngineeringArtifactRegistry& artifact_registry,
    const std::string& case_id) {
    return compile_transient_model_graph(
        document, registry, property_registry, artifact_registry,
        physics::ThermochemistryPackageRegistry{}, case_id);
}

CompiledTransientModelGraph compile_flat_transient_model_graph(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::PropertyPackageRegistry& property_registry,
    const EngineeringArtifactRegistry& artifact_registry,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry_registry,
    const std::string& case_id) {
    const CaseDefinition* active_case = select_case(document, case_id);
    if (active_case != nullptr &&
        active_case->mode != "dynamic_initialization" &&
        active_case->mode != "dynamic_transient") {
        throw std::invalid_argument(
            "case '" + active_case->id +
            "' must use dynamic_initialization or dynamic_transient mode");
    }

    DaeEquationSystemBuilder system;
    CompiledTransientModelGraph graph;
    graph.model_id = document.model_id;
    if (active_case != nullptr) {
        graph.case_id = active_case->id;
    }

    std::map<std::string, std::size_t> variable_indices;
    std::map<std::size_t, DaeVariableKind> variable_kinds;
    std::map<std::string, std::string> variable_dimensions;
    std::map<std::string, double> variable_scales;
    auto medium_properties =
        create_medium_properties(document, property_registry);
    const auto parameter_overrides =
        case_parameter_overrides(
            document, registry, active_case);
    std::map<std::string, std::shared_ptr<std::string>>
        component_mode_states;
    std::map<std::string, std::string> initial_component_modes;
    std::set<std::string> seen_component_modes;
    std::vector<DaeEvent> component_events;

    for (const ComponentDefinition& declared_component :
         document.components) {
        const auto component = effective_component(
            declared_component, parameter_overrides);
        const ComponentModel& model =
            registry.require_model(component.kind);
        const auto descriptor = model.instance_descriptor(component);
        validate_component_descriptor(descriptor);
        validate_component_bindings(component, descriptor);
        validate_component_parameters(component, descriptor);
        if (!descriptor.supports_transient) {
            throw std::invalid_argument(
                "component '" + component.id + "' of kind '" +
                component.kind + "' does not support transient compilation");
        }

        ComponentCompileContext context{
            component, active_case, {}, {}, {}, {}, {}, {}, {}, {}};
        const auto selected_mode = selected_component_mode(
            descriptor, active_case, component.id);
        if (!selected_mode.empty()) {
            const auto mode =
                std::make_shared<std::string>(selected_mode);
            component_mode_states.emplace(component.id, mode);
            initial_component_modes.emplace(
                component.id, selected_mode);
            context.active_mode = [mode]() { return *mode; };
            context.set_active_mode =
                [mode](std::string next) {
                    *mode = std::move(next);
                };
        }
        if (active_case != nullptr &&
            active_case->component_modes.contains(component.id)) {
            seen_component_modes.insert(component.id);
        }
        for (const auto& port : descriptor.ports) {
            std::string medium_id;
            if (port.domain == "fluid") {
                medium_id =
                    require_medium_binding(component, port.name);
                const auto package =
                    medium_properties.find(medium_id);
                if (package == medium_properties.end()) {
                    throw std::logic_error(
                        "compiled medium property package missing: " +
                        medium_id);
                }
                context.port_properties.emplace(
                    port.name, package->second);
            } else if (port.domain == "material") {
                medium_id =
                    require_material_binding(component, port.name);
                const auto& material =
                    find_material(document, medium_id);
                context.port_species.emplace(
                    port.name, material.species);
                if (!descriptor
                         .required_thermochemistry_capabilities
                         .empty()) {
                    auto package = thermochemistry_registry.create(
                        material.backend, material.mechanism,
                        material.phase);
                    if (!material.package_version.empty() &&
                        material.package_version != package->version()) {
                        throw std::invalid_argument(
                            "material '" + material.id +
                            "' requests thermochemistry package version '" +
                            material.package_version + "' but backend '" +
                            material.backend + "' provides version '" +
                            std::string(package->version()) + "'");
                    }
                    for (const auto& species : material.species) {
                        if (std::find(
                                package->species_basis().begin(),
                                package->species_basis().end(),
                                species) == package->species_basis().end()) {
                            throw std::invalid_argument(
                                "material '" + material.id +
                                "' species is absent from backend mechanism: " +
                                species);
                        }
                    }
                    for (const auto capability :
                         descriptor
                             .required_thermochemistry_capabilities) {
                        if (!package->supports(capability)) {
                            throw std::invalid_argument(
                                "component '" + component.id +
                                "' requires an unsupported thermochemistry capability");
                        }
                    }
                    context.port_thermochemistry.emplace(
                        port.name, std::move(package));
                }
            }
            const auto species = context.port_species.find(
                port.name);
            for (const auto& spec :
                 canonical_variables_for_domain(
                     registry, port.domain,
                     species == context.port_species.end()
                         ? std::vector<std::string>{}
                         : species->second)) {
                const std::string full_name =
                    variable_key(
                        component.id, port.name, spec.name);
                double initial = spec.initial_value;
                if (const auto value =
                        case_scalar_value(active_case, full_name, false)) {
                    initial = *value;
                } else if (const auto scheduled =
                               case_schedule_initial_value(
                                   active_case, full_name)) {
                    initial = *scheduled;
                } else if (const auto fixed =
                               case_scalar_value(active_case, full_name, true)) {
                    initial = *fixed;
                }
                const auto* transient = find_transient_variable(
                    descriptor, port.name, spec.name);
                const DaeVariableKind kind =
                    transient == nullptr
                        ? DaeVariableKind::algebraic
                        : transient->kind;
                const double derivative_scale =
                    transient == nullptr
                        ? spec.scale
                        : transient->derivative_scale;
                const std::size_t index = system.add_variable(
                    full_name, kind, initial, 0.0,
                    spec.scale, derivative_scale);
                variable_indices.emplace(full_name, index);
                variable_kinds.emplace(index, kind);
                variable_dimensions.emplace(
                    full_name, spec.dimension);
                variable_scales.emplace(full_name, spec.scale);
                context.port_variables.emplace(
                    port.name + "." + spec.name, index);
                graph.port_variables.push_back(
                    CompiledPortVariable{
                        component.id, port.name, spec.name,
                        full_name, port.domain, medium_id,
                        spec.dimension, port.direction,
                        explicit_boundary_sign(
                            descriptor),
                        index});
            }
        }
        for (const auto& variable :
             descriptor.internal_variables) {
            const std::string full_name =
                component.id + "." + variable.name;
            double initial = variable.initial_value;
            if (const auto value =
                    case_scalar_value(active_case, full_name, false)) {
                initial = *value;
            } else if (const auto scheduled =
                           case_schedule_initial_value(
                               active_case, full_name)) {
                initial = *scheduled;
            } else if (const auto fixed =
                           case_scalar_value(active_case, full_name, true)) {
                initial = *fixed;
            }
            const std::size_t index = system.add_variable(
                full_name, variable.kind, initial,
                variable.initial_derivative, variable.state_scale,
                variable.derivative_scale, variable.lower_bound,
                variable.upper_bound);
            variable_indices.emplace(full_name, index);
            variable_kinds.emplace(index, variable.kind);
            variable_dimensions.emplace(
                full_name, variable.dimension);
            variable_scales.emplace(
                full_name, variable.state_scale);
            context.internal_variables.emplace(variable.name, index);
            graph.internal_variables.push_back(
                CompiledInternalVariable{component.id, variable.name,
                                         full_name,
                                         variable.dimension, index});
        }
        resolve_component_artifacts(
            context, model, artifact_registry);
        validate_property_capabilities(context, model);
        if (descriptor.uses_quasi_steady_transient_equations) {
            add_quasi_steady_transient_equations(
                model, context, system);
        } else {
            model.add_transient_equations(context, system);
        }
        model.add_transient_events(context, component_events);
    }
    if (active_case != nullptr &&
        seen_component_modes.size() !=
            active_case->component_modes.size()) {
        for (const auto& [component_id, _] :
             active_case->component_modes) {
            if (!seen_component_modes.contains(component_id)) {
                throw std::invalid_argument(
                    "case '" + active_case->id +
                    "' component_modes references unknown component: " +
                    component_id);
            }
        }
    }

    std::map<std::string, std::size_t> connection_counts;
    for (const ConnectionDefinition& connection :
         document.connections) {
        const auto endpoints = validate_connection(
            document, registry, connection, connection_counts);
        for (const auto& spec :
             canonical_variables_for_domain(
                 registry,
                 endpoints.domain, endpoints.species)) {
            const auto from_it = variable_indices.find(variable_key(
                endpoints.from_component, endpoints.from_port,
                spec.name));
            const auto to_it = variable_indices.find(variable_key(
                endpoints.to_component, endpoints.to_port,
                spec.name));
            if (from_it == variable_indices.end() ||
                to_it == variable_indices.end()) {
                throw std::logic_error(
                    "compiled connection variable missing for: " +
                    connection.id);
            }
            const std::string residual_name =
                "connection." + connection.id + "." + spec.name;
            const std::size_t residual_index =
                system.add_linear_equation(
                    residual_name,
                    {{from_it->second, 1.0, 0.0},
                     {to_it->second, -1.0, 0.0}},
                    0.0, spec.scale);
            graph.connection_equations.push_back(
                CompiledConnectionEquation{
                    connection.id, spec.name, residual_name,
                    residual_index});
        }
    }
    mark_unconnected_system_boundaries(
        graph.port_variables, connection_counts);

    const auto transitioned_input_values =
        std::make_shared<std::map<std::string, double>>();
    std::set<std::string> transitioned_input_targets;
    if (active_case != nullptr) {
        for (const auto& event : active_case->state_events) {
            for (const auto& action : event.actions) {
                if (action.type != "set_input" &&
                    action.type != "set_mode" &&
                    action.type != "set_state") {
                    throw std::invalid_argument(
                        "case '" + active_case->id +
                        "' state event '" + event.id +
                        "' declares unsupported action type: " +
                        action.type);
                }
                if (action.type == "set_mode") {
                    if (!action.source.empty() ||
                        action.value.has_value()) {
                        throw std::invalid_argument(
                            "case '" + active_case->id +
                            "' state event '" + event.id +
                            "' set_mode action cannot declare source or "
                            "value");
                    }
                    const auto mode_state =
                        component_mode_states.find(action.target);
                    if (mode_state == component_mode_states.end()) {
                        throw std::invalid_argument(
                            "case '" + active_case->id +
                            "' state event '" + event.id +
                            "' set_mode target does not declare component "
                            "modes: " + action.target);
                    }
                    const auto& component =
                        find_component(document, action.target);
                    const auto& modes = registry.require_model(
                        component.kind).descriptor().supported_modes;
                    if (std::find(
                            modes.begin(), modes.end(), action.mode) ==
                        modes.end()) {
                        throw std::invalid_argument(
                            "case '" + active_case->id +
                            "' state event '" + event.id +
                            "' requests unsupported mode '" +
                            action.mode + "' for component '" +
                            action.target + "'");
                    }
                    continue;
                }
                if (action.source.empty() ==
                    !action.value.has_value()) {
                    throw std::invalid_argument(
                        "case '" + active_case->id +
                        "' state event '" + event.id + "' " +
                        action.type +
                        " action must declare exactly one source or "
                        "value");
                }
                if (action.type == "set_state") {
                    const auto variable =
                        variable_indices.find(action.target);
                    if (variable == variable_indices.end()) {
                        throw std::invalid_argument(
                            "case '" + active_case->id +
                            "' state event '" + event.id +
                            "' set_state action references unknown graph "
                            "variable: " + action.target);
                    }
                    if (variable_kinds.at(variable->second) !=
                        DaeVariableKind::differential) {
                        throw std::invalid_argument(
                            "case '" + active_case->id +
                            "' state event '" + event.id +
                            "' set_state action target must be a "
                            "differential variable: " + action.target);
                    }
                    const auto& descriptor =
                        system.variables().at(variable->second);
                    if (!action.source.empty()) {
                        const auto source =
                            variable_indices.find(action.source);
                        if (source == variable_indices.end()) {
                            throw std::invalid_argument(
                                "case '" + active_case->id +
                                "' state event '" + event.id +
                                "' set_state source references unknown "
                                "graph variable: " + action.source);
                        }
                        if (variable_dimensions.at(action.source) !=
                            variable_dimensions.at(action.target)) {
                            throw std::invalid_argument(
                                "case '" + active_case->id +
                                "' state event '" + event.id +
                                "' set_state source dimension does not "
                                "match target dimension '" +
                                variable_dimensions.at(action.target) +
                                "'");
                        }
                    } else {
                        if (action.value->dimension !=
                            variable_dimensions.at(action.target)) {
                            throw std::invalid_argument(
                                "case '" + active_case->id +
                                "' state event '" + event.id +
                                "' set_state value dimension does not "
                                "match graph variable dimension '" +
                                variable_dimensions.at(action.target) +
                                "'");
                        }
                        if (!std::isfinite(action.value->value_si) ||
                            action.value->value_si <
                                descriptor.lower_bound ||
                            action.value->value_si >
                                descriptor.upper_bound) {
                            throw std::invalid_argument(
                                "case '" + active_case->id +
                                "' state event '" + event.id +
                                "' set_state value is outside bounds for "
                                "graph variable: " + action.target);
                        }
                    }
                    continue;
                }
                if (!active_case->fixed_values.contains(action.target) &&
                    !active_case->input_schedules.contains(action.target)) {
                    throw std::invalid_argument(
                        "case '" + active_case->id +
                        "' state event '" + event.id +
                        "' set_input action target must be declared in "
                        "fixed_values or input_schedules: " +
                        action.target);
                }
                const auto variable = variable_indices.find(action.target);
                if (variable == variable_indices.end()) {
                    throw std::invalid_argument(
                        "case '" + active_case->id +
                        "' state event '" + event.id +
                        "' action references unknown graph variable: " +
                        action.target);
                }
                if (variable_kinds.at(variable->second) ==
                    DaeVariableKind::differential) {
                    throw std::invalid_argument(
                        "case '" + active_case->id +
                        "' state event '" + event.id +
                        "' cannot set differential variable: " +
                        action.target);
                }
                const auto& descriptor =
                    system.variables().at(variable->second);
                if (!action.source.empty()) {
                    const auto source =
                        variable_indices.find(action.source);
                    if (source == variable_indices.end()) {
                        throw std::invalid_argument(
                            "case '" + active_case->id +
                            "' state event '" + event.id +
                            "' set_input source references unknown graph "
                            "variable: " + action.source);
                    }
                    if (variable_dimensions.at(action.source) !=
                        variable_dimensions.at(action.target)) {
                        throw std::invalid_argument(
                            "case '" + active_case->id +
                            "' state event '" + event.id +
                            "' set_input source dimension does not match "
                            "target dimension '" +
                            variable_dimensions.at(action.target) + "'");
                    }
                } else {
                    if (action.value->dimension !=
                        variable_dimensions.at(action.target)) {
                        throw std::invalid_argument(
                            "case '" + active_case->id +
                            "' state event '" + event.id +
                            "' action value dimension does not match "
                            "graph variable dimension '" +
                            variable_dimensions.at(action.target) + "'");
                    }
                    if (!std::isfinite(action.value->value_si) ||
                        action.value->value_si < descriptor.lower_bound ||
                        action.value->value_si > descriptor.upper_bound) {
                        throw std::invalid_argument(
                            "case '" + active_case->id +
                            "' state event '" + event.id +
                            "' set_input value is outside bounds for "
                            "graph variable: " + action.target);
                    }
                }
                transitioned_input_targets.insert(action.target);
            }
        }
    }

    if (active_case != nullptr) {
        for (const auto& [key, scalar] :
             active_case->fixed_values) {
            if (active_case->input_schedules.contains(key)) {
                throw std::invalid_argument(
                    "case '" + active_case->id +
                    "' cannot both fix and schedule variable: " + key);
            }
            const auto variable_it = variable_indices.find(key);
            if (variable_it == variable_indices.end()) {
                if (add_transient_temperature_specification(
                        key, scalar, document, registry,
                        thermochemistry_registry, medium_properties,
                        variable_indices, active_case->id, system,
                        graph)) {
                    continue;
                }
                throw std::invalid_argument(
                    "case '" + active_case->id +
                    "' fixed value references unknown variable: " +
                    key);
            }
            if (variable_kinds.at(variable_it->second) ==
                DaeVariableKind::differential) {
                throw std::invalid_argument(
                    "case '" + active_case->id +
                    "' cannot fix differential variable '" + key +
                    "'; use initial_guesses for its initial state");
            }
            std::size_t residual_index = 0;
            if (transitioned_input_targets.contains(key)) {
                residual_index = system.add_sparse_equation(
                    "fixed." + active_case->id + "." + key,
                    {variable_it->second},
                    [index = variable_it->second, key,
                     initial_value = scalar.value_si,
                     transitioned_input_values](
                        double, const std::vector<double>& state,
                        const std::vector<double>&, double& residual,
                        std::vector<DaeEquationPartial>& jacobian) {
                        const auto override_value =
                            transitioned_input_values->find(key);
                        const double target =
                            override_value ==
                                    transitioned_input_values->end()
                                ? initial_value
                                : override_value->second;
                        residual = state.at(index) - target;
                        jacobian.push_back({index, 1.0, 0.0});
                        return EvaluationStatus::success();
                    },
                    std::max(std::abs(scalar.value_si), 1.0));
            } else {
                residual_index = system.add_linear_equation(
                    "fixed." + active_case->id + "." + key,
                    {{variable_it->second, 1.0, 0.0}},
                    scalar.value_si,
                    std::max(std::abs(scalar.value_si), 1.0));
            }
            graph.fixed_value_equations.push_back(
                system.residuals().at(residual_index).name);
        }
        for (const auto& [key, schedule] :
             active_case->input_schedules) {
            const auto variable_it = variable_indices.find(key);
            if (variable_it == variable_indices.end()) {
                throw std::invalid_argument(
                    "case '" + active_case->id +
                    "' input schedule references unknown variable: " +
                    key);
            }
            if (variable_kinds.at(variable_it->second) ==
                DaeVariableKind::differential) {
                throw std::invalid_argument(
                    "case '" + active_case->id +
                    "' cannot schedule differential variable '" + key +
                    "'; schedule its algebraic boundary or control input");
            }
            for (const auto& point : schedule.points) {
                if (point.value.dimension !=
                    variable_dimensions.at(key)) {
                    throw std::invalid_argument(
                        "case '" + active_case->id +
                        "' input schedule dimension for '" + key +
                        "' does not match graph variable dimension '" +
                        variable_dimensions.at(key) + "'");
                }
            }
            const std::string residual_name =
                "scheduled." + active_case->id + "." + key;
            const std::size_t residual_index =
                system.add_sparse_equation(
                    residual_name, {variable_it->second},
                    [index = variable_it->second, key, schedule,
                     transitioned_input_values](
                        double time,
                        const std::vector<double>& state,
                        const std::vector<double>&,
                        double& residual,
                        std::vector<DaeEquationPartial>& jacobian) {
                        const auto override_value =
                            transitioned_input_values->find(key);
                        const double target =
                            override_value ==
                                    transitioned_input_values->end()
                                ? interpolate_input_schedule(schedule, time)
                                : override_value->second;
                        residual = state.at(index) - target;
                        jacobian.push_back({index, 1.0, 0.0});
                        return EvaluationStatus::success();
                    },
                    variable_scales.at(key));
            graph.scheduled_value_equations.push_back(
                system.residuals().at(residual_index).name);
        }
        for (const auto& [key, _] :
             active_case->initial_guesses) {
            if (variable_indices.find(key) ==
                variable_indices.end()) {
                throw std::invalid_argument(
                    "case '" + active_case->id +
                    "' initial guess references unknown variable: " +
                    key);
            }
        }
    }

    graph.structure =
        validate_degree_of_freedom(document.model_id, system);
    graph.problem = system.build();
    graph.problem.events = std::move(component_events);
    if (!transitioned_input_targets.empty() ||
        !component_mode_states.empty()) {
        graph.problem.reset_discrete_state =
            [transitioned_input_values,
             component_mode_states,
             initial_component_modes]() {
                transitioned_input_values->clear();
                for (const auto& [component_id, initial_mode] :
                     initial_component_modes) {
                    *component_mode_states.at(component_id) =
                        initial_mode;
                }
                return EvaluationStatus::success();
            };
    }
    if (active_case != nullptr) {
        for (const auto& event : active_case->state_events) {
            const auto variable = variable_indices.find(event.target);
            if (variable == variable_indices.end()) {
                throw std::invalid_argument(
                    "case '" + active_case->id + "' state event '" +
                    event.id +
                    "' references unknown graph variable: " +
                    event.target);
            }
            if (event.threshold.dimension !=
                variable_dimensions.at(event.target)) {
                throw std::invalid_argument(
                    "case '" + active_case->id + "' state event '" +
                    event.id + "' threshold dimension does not match "
                    "graph variable dimension '" +
                    variable_dimensions.at(event.target) + "'");
            }
            if (event.hysteresis.has_value() &&
                event.hysteresis->dimension !=
                    variable_dimensions.at(event.target)) {
                throw std::invalid_argument(
                    "case '" + active_case->id + "' state event '" +
                    event.id + "' hysteresis dimension does not match "
                    "graph variable dimension '" +
                    variable_dimensions.at(event.target) + "'");
            }
            const auto direction = event.direction == "rising"
                ? EventDirection::rising
                : event.direction == "falling"
                    ? EventDirection::falling
                    : EventDirection::any;
            std::function<EvaluationStatus(
                double, std::vector<double>&,
                std::vector<double>&)> transition;
            if (!event.actions.empty()) {
                struct CompiledTransitionAction {
                    std::string type;
                    std::string target;
                    std::size_t target_index{0};
                    std::optional<std::size_t> source_index;
                    double constant_value{0.0};
                    double lower_bound{
                        -std::numeric_limits<double>::infinity()};
                    double upper_bound{
                        std::numeric_limits<double>::infinity()};
                    std::shared_ptr<std::string> mode_state;
                    std::string mode;
                };
                std::vector<CompiledTransitionAction> actions;
                actions.reserve(event.actions.size());
                for (const auto& action : event.actions) {
                    if (action.type == "set_input") {
                        const auto index =
                            variable_indices.at(action.target);
                        const auto& descriptor =
                            system.variables().at(index);
                        actions.push_back({
                            action.type,
                            action.target,
                            index,
                            action.source.empty()
                                ? std::optional<std::size_t>{}
                                : std::optional<std::size_t>{
                                      variable_indices.at(action.source)},
                            action.value.has_value()
                                ? action.value->value_si : 0.0,
                            descriptor.lower_bound,
                            descriptor.upper_bound,
                            {},
                            {},
                        });
                    } else if (action.type == "set_mode") {
                        actions.push_back({
                            action.type,
                            action.target,
                            0,
                            {},
                            0.0,
                            -std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::infinity(),
                            component_mode_states.at(action.target),
                            action.mode,
                        });
                    } else {
                        const auto index =
                            variable_indices.at(action.target);
                        const auto& descriptor =
                            system.variables().at(index);
                        actions.push_back({
                            action.type,
                            action.target,
                            index,
                            action.source.empty()
                                ? std::optional<std::size_t>{}
                                : std::optional<std::size_t>{
                                      variable_indices.at(action.source)},
                            action.value.has_value()
                                ? action.value->value_si : 0.0,
                            descriptor.lower_bound,
                            descriptor.upper_bound,
                            {},
                            {},
                        });
                    }
                }
                transition =
                    [actions = std::move(actions),
                     transitioned_input_values,
                     event_name = event.id](
                        double, std::vector<double>& state,
                        std::vector<double>& derivative) {
                        struct PendingValue {
                            double value{0.0};
                        };
                        std::vector<PendingValue> pending_values;
                        pending_values.reserve(actions.size());
                        // Every source reads the same accepted pre-event
                        // graph state. No action is committed until all
                        // dynamic values and bounds have been checked.
                        for (std::size_t index = 0;
                             index < actions.size(); ++index) {
                            const auto& action = actions[index];
                            if (action.type == "set_mode") continue;
                            const double value = action.source_index
                                ? state.at(*action.source_index)
                                : action.constant_value;
                            if (!std::isfinite(value) ||
                                value < action.lower_bound ||
                                value > action.upper_bound) {
                                return EvaluationStatus::fatal(
                                    "system event '" + event_name +
                                    "' produced an invalid or out-of-bounds "
                                    "value for action target '" +
                                    action.target + "'");
                            }
                            pending_values.push_back({value});
                        }
                        std::size_t pending_index = 0;
                        for (const auto& action : actions) {
                            if (action.type == "set_mode") {
                                *action.mode_state = action.mode;
                                continue;
                            }
                            const double value =
                                pending_values.at(pending_index++).value;
                            if (action.type == "set_input") {
                                (*transitioned_input_values)[action.target] =
                                    value;
                            } else {
                                state.at(action.target_index) = value;
                                derivative.at(action.target_index) = 0.0;
                            }
                        }
                        return EvaluationStatus::success();
                    };
            }
            graph.problem.events.push_back(DaeEvent{
                event.id,
                [index = variable->second,
                 threshold = event.threshold.value_si](
                    double, const std::vector<double>& state,
                    double& value) {
                    value = state.at(index) - threshold;
                    return EvaluationStatus::success();
                },
                direction,
                event.terminal,
                std::move(transition),
                event.priority,
                event.hysteresis.has_value()
                    ? event.hysteresis->value_si
                    : 0.0,
            });
        }
        for (const auto& [_, schedule] :
             active_case->input_schedules) {
            for (const auto& point : schedule.points) {
                graph.problem.time_breakpoints.push_back(
                    point.time.value_si);
            }
            if (schedule.interpolation == "previous") {
                for (std::size_t index = 1;
                     index < schedule.points.size(); ++index) {
                    graph.problem.time_discontinuities.push_back(
                        schedule.points[index].time.value_si);
                }
            }
        }
        std::sort(
            graph.problem.time_breakpoints.begin(),
            graph.problem.time_breakpoints.end());
        graph.problem.time_breakpoints.erase(
            std::unique(
                graph.problem.time_breakpoints.begin(),
                graph.problem.time_breakpoints.end()),
            graph.problem.time_breakpoints.end());
        std::sort(
            graph.problem.time_discontinuities.begin(),
            graph.problem.time_discontinuities.end());
        graph.problem.time_discontinuities.erase(
            std::unique(
                graph.problem.time_discontinuities.begin(),
                graph.problem.time_discontinuities.end()),
            graph.problem.time_discontinuities.end());
    }
    return graph;
}

CompiledTransientModelGraph compile_transient_model_graph(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::PropertyPackageRegistry& property_registry,
    const EngineeringArtifactRegistry& artifact_registry,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry_registry,
    const std::string& case_id) {
    return compile_flat_transient_model_graph(
        flatten_model_document(document), registry,
        property_registry, artifact_registry,
        thermochemistry_registry, case_id);
}

}  // namespace thermox::platform
