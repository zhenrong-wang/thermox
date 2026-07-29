#include "thermox/platform/component_registry.hpp"

#include "component_modules.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

namespace thermox::platform {

namespace {

struct CanonicalVariableSpec {
    std::string name;
    double initial_value{0.0};
    double scale{1.0};
    std::string dimension{"dimensionless"};
};

std::vector<CanonicalVariableSpec> canonical_variables_for_domain(
    const std::string& domain,
    const std::vector<std::string>& species = {}) {
    if (domain == "fluid") {
        return {{"m_dot", 1.0, 100.0, "mass_flow"},
                {"p", 101325.0, 100000.0, "pressure"},
                {"h", 300000.0, 100000.0,
                 "specific_enthalpy"}};
    }
    if (domain == "heat") {
        return {{"Q_dot", 0.0, 1000000.0, "power"},
                {"T", 300.0, 100.0, "temperature"}};
    }
    if (domain == "shaft") {
        return {{"W_dot", 0.0, 1000000.0, "power"},
                {"omega", 314.1592653589793, 100.0,
                 "angular_speed"}};
    }
    if (domain == "electrical") {
        return {{"P", 0.0, 1000000.0, "power"},
                {"frequency", 50.0, 50.0, "frequency"}};
    }
    if (domain == "material") {
        std::vector<CanonicalVariableSpec> variables{
            {"p", 101325.0, 100000.0, "pressure"},
            {"h", 300000.0, 100000.0,
             "specific_enthalpy"}};
        for (const auto& name : species) {
            variables.push_back(
                {"m_dot[" + name + "]", 0.01, 100.0,
                 "mass_flow"});
        }
        return variables;
    }
    if (domain == "signal" || domain == "control") {
        return {{"value", 0.0, 1.0, "dimensionless"}};
    }
    throw std::invalid_argument("unsupported port domain during graph compilation: " + domain);
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

const PortModelDescriptor& find_port(
    const ComponentDefinition& component,
    const ComponentModel& model,
    const std::string& port_name) {
    const auto it = std::find_if(
        model.descriptor().ports.begin(),
        model.descriptor().ports.end(),
        [&](const auto& port) {
            return port.name == port_name;
        });
    if (it == model.descriptor().ports.end()) {
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

std::string required_connection_kind(const std::string& domain) {
    if (domain == "fluid") return "fluid_link";
    if (domain == "material") return "material_link";
    if (domain == "heat") return "heat_link";
    if (domain == "shaft") return "shaft_link";
    if (domain == "electrical") return "electrical_link";
    if (domain == "signal" || domain == "control") {
        return "signal_link";
    }
    throw std::invalid_argument(
        "unsupported port domain during connection validation: " +
        domain);
}

std::string required_connection_contract(
    const std::string& domain) {
    if (domain == "fluid") return "thermox.connector.fluid/v1";
    if (domain == "material") {
        return "thermox.connector.material/v1";
    }
    if (domain == "heat") return "thermox.connector.heat/v1";
    if (domain == "shaft") return "thermox.connector.shaft/v1";
    if (domain == "electrical") {
        return "thermox.connector.electrical/v1";
    }
    if (domain == "signal") return "thermox.connector.signal/v1";
    if (domain == "control") return "thermox.connector.control/v1";
    throw std::invalid_argument(
        "unsupported port domain during connection validation: " +
        domain);
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
    const auto& from_port = find_port(
        from_definition, from_model, result.from_port);
    const auto& to_port = find_port(
        to_definition, to_model, result.to_port);
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
    if (connection.kind !=
        required_connection_kind(from_port.domain)) {
        throw std::invalid_argument(
            "connection '" + connection.id +
            "' kind '" + connection.kind +
            "' is incompatible with domain '" +
            from_port.domain + "'");
    }
    const auto resolved_contract =
        required_connection_contract(from_port.domain);
    if (!connection.contract_version.empty() &&
        connection.contract_version != resolved_contract) {
        throw std::invalid_argument(
            "connection '" + connection.id +
            "' requests connector contract version '" +
            connection.contract_version + "' but domain '" +
            from_port.domain + "' provides version '" +
            resolved_contract + "'");
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
        const auto& descriptor =
            registry.require_model(component->kind).descriptor();
        const auto parameter = std::find_if(
            descriptor.parameters.begin(),
            descriptor.parameters.end(),
            [&](const auto& candidate) {
                return candidate.name == parameter_name;
            });
        if (parameter == descriptor.parameters.end()) {
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
    const ComponentModel& model) {
    const auto& expected_ports = model.descriptor().ports;
    if (!component.version.empty() &&
        component.version != model.descriptor().version) {
        throw std::invalid_argument(
            "component '" + component.id + "' requests version '" +
            component.version + "' but registered kind '" +
            component.kind + "' provides version '" +
            model.descriptor().version + "'");
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
    for (const auto& artifact : model.descriptor().artifacts) {
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
    const PerformanceMapRegistry& performance_map_registry) {
    for (const auto& artifact : model.descriptor().artifacts) {
        const auto binding =
            context.component.artifact_bindings.find(
                artifact.role);
        if (binding ==
            context.component.artifact_bindings.end()) {
            continue;
        }
        if (artifact.artifact_type ==
            performance_map_artifact_type) {
            context.performance_maps.emplace(
                artifact.role,
                performance_map_registry.require_artifact(
                    binding->second));
            continue;
        }
        throw std::logic_error(
            "unsupported resolved component artifact type: " +
            artifact.artifact_type);
    }
}

std::string_view capability_name(physics::PropertyCapability capability) {
    switch (capability) {
        case physics::PropertyCapability::state_pt: return "state_pt";
        case physics::PropertyCapability::state_ph: return "state_ph";
        case physics::PropertyCapability::state_ps: return "state_ps";
        case physics::PropertyCapability::saturation_p:
            return "saturation_p";
        case physics::PropertyCapability::transport: return "transport";
    }
    return "unknown";
}

void validate_property_capabilities(const ComponentCompileContext& context,
                                    const ComponentModel& model) {
    for (const auto capability : model.descriptor().required_property_capabilities) {
        for (const auto& [port, package] : context.port_properties) {
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
void validate_degree_of_freedom(
    const std::string& model_id,
    const Builder& system) {
    const std::size_t variables = system.variables().size();
    const std::size_t equations = system.residuals().size();
    if (variables == equations) {
        return;
    }
    const bool under_specified = variables > equations;
    const std::size_t difference = under_specified
                                       ? variables - equations
                                       : equations - variables;
    throw std::invalid_argument(
        "model '" + model_id + "' is " +
        (under_specified ? "under-specified" : "over-specified") +
        ": " + std::to_string(variables) + " variables and " +
        std::to_string(equations) + " equations; " +
        std::to_string(difference) +
        (under_specified
             ? " additional independent equation(s) or specification(s) required"
             : " equation(s) or specification(s) must be removed"));
}

EvaluationStatus property_failure(const physics::PropertyResult& result) {
    if (result.status == physics::PropertyStatus::backend_error)
        return EvaluationStatus::fatal(result.message);
    return EvaluationStatus::recoverable(result.message);
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

}  // namespace

void ComponentModel::add_transient_equations(
    const ComponentCompileContext&,
    DaeEquationSystemBuilder&) const {
    throw std::logic_error(
        "component does not implement transient equations: " +
        descriptor().kind);
}

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

void ComponentRegistry::register_model(std::shared_ptr<const ComponentModel> model) {
    if (!model) {
        throw std::invalid_argument("component model registration must not be null");
    }
    const std::string& kind = model->descriptor().kind;
    if (kind.empty()) {
        throw std::invalid_argument("component model kind must not be empty");
    }
    validate_component_descriptor(model->descriptor());
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
        const auto canonical =
            canonical_variables_for_domain(port->domain);
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
    if (!model->descriptor().supports_transient &&
        (!model->descriptor().transient_variables.empty() ||
         !model->descriptor().internal_variables.empty())) {
        throw std::invalid_argument(
            "steady-only component declares transient variables: " + kind);
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
    register_heat_transfer_component_models(registry);
    register_fluid_inventory_component_models(registry);
    register_storage_component_models(registry);
    register_power_component_models(registry);
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
        PerformanceMapRegistry{}, case_id);
}

CompiledModelGraph compile_model_graph(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::PropertyPackageRegistry& property_registry,
    const PerformanceMapRegistry& performance_map_registry,
    const std::string& case_id) {
    return compile_model_graph(
        document, registry, property_registry,
        performance_map_registry,
        physics::ThermochemistryPackageRegistry{}, case_id);
}

CompiledModelGraph compile_model_graph(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::PropertyPackageRegistry& property_registry,
    const PerformanceMapRegistry& performance_map_registry,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry_registry,
    const std::string& case_id) {
    const CaseDefinition* active_case = select_case(document, case_id);

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

    for (const ComponentDefinition& declared_component :
         document.components) {
        const auto component = effective_component(
            declared_component, parameter_overrides);
        const ComponentModel& model = registry.require_model(component.kind);
        validate_component_bindings(component, model);
        validate_component_parameters(component, model.descriptor());
        if (!model.descriptor().supports_steady) {
            throw std::invalid_argument(
                "component '" + component.id + "' of kind '" +
                component.kind + "' does not support steady compilation");
        }

        ComponentCompileContext context{
            component, active_case, {}, {}, {}, {}, {}, {}};
        for (const auto& port : model.descriptor().ports) {
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
                if (!model.descriptor()
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
                         model.descriptor()
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
            for (const auto& spec : canonical_variables_for_domain(
                     port.domain,
                     species == context.port_species.end()
                         ? std::vector<std::string>{}
                         : species->second)) {
                const std::string full_name =
                    variable_key(component.id, port.name, spec.name);
                double initial = spec.initial_value;
                if (const auto value = case_scalar_value(active_case, full_name, false)) {
                    initial = *value;
                    seen_case_keys.insert(full_name);
                } else if (const auto fixed = case_scalar_value(active_case, full_name, true)) {
                    initial = *fixed;
                }
                const std::size_t index = system.add_variable(full_name, initial, spec.scale);
                variable_indices.emplace(full_name, index);
                context.port_variables.emplace(
                    port.name + "." + spec.name, index);
                graph.port_variables.push_back(
                    CompiledPortVariable{
                        component.id, port.name, spec.name,
                        full_name, port.domain, medium_id,
                        spec.dimension, port.direction,
                        explicit_boundary_sign(
                            model.descriptor()),
                        index});
            }
        }
        resolve_component_artifacts(
            context, model, performance_map_registry);
        validate_property_capabilities(context, model);
        model.add_equations(context, system);
    }

    std::map<std::string, std::size_t> connection_counts;
    for (const ConnectionDefinition& connection : document.connections) {
        const auto endpoints = validate_connection(
            document, registry, connection, connection_counts);
        for (const auto& spec :
             canonical_variables_for_domain(
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
                        const auto port = std::find_if(
                            component_model.descriptor().ports.begin(),
                            component_model.descriptor().ports.end(),
                            [&](const auto& candidate) {
                                return candidate.name == port_name;
                            });
                        if (port !=
                                component_model.descriptor().ports.end() &&
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
                                system.add_checked_equation(
                                    residual_name,
                                    [properties =
                                         package->second,
                                     pressure =
                                         pressure->second,
                                     enthalpy =
                                         enthalpy->second,
                                     target =
                                         scalar.value_si](
                                        const std::vector<double>&
                                            x,
                                        double& residual) {
                                        const auto state =
                                            properties->state_ph(
                                                x.at(pressure),
                                                x.at(enthalpy));
                                        if (!state.ok()) {
                                            return property_failure(
                                                state);
                                        }
                                        residual =
                                            state.state
                                                .temperature_k -
                                            target;
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
            const std::size_t residual_index = system.add_linear_equation(
                "fixed." + active_case->id + "." + key,
                {{variable_it->second, 1.0}},
                scalar.value_si,
                std::max(std::abs(scalar.value_si), 1.0));
            graph.fixed_value_equations.push_back(system.residuals().at(residual_index).name);
        }
        for (const auto& [key, _] : active_case->initial_guesses) {
            if (variable_indices.find(key) == variable_indices.end()) {
                throw std::invalid_argument("case '" + active_case->id +
                                            "' initial guess references unknown variable: " + key);
            }
        }
    }

    validate_degree_of_freedom(document.model_id, system);
    graph.problem = system.build();
    return graph;
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
        PerformanceMapRegistry{}, case_id);
}

CompiledTransientModelGraph compile_transient_model_graph(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::PropertyPackageRegistry& property_registry,
    const PerformanceMapRegistry& performance_map_registry,
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
    auto medium_properties =
        create_medium_properties(document, property_registry);
    const auto parameter_overrides =
        case_parameter_overrides(
            document, registry, active_case);

    for (const ComponentDefinition& declared_component :
         document.components) {
        const auto component = effective_component(
            declared_component, parameter_overrides);
        const ComponentModel& model =
            registry.require_model(component.kind);
        validate_component_bindings(component, model);
        validate_component_parameters(component, model.descriptor());
        if (!model.descriptor().supports_transient) {
            throw std::invalid_argument(
                "component '" + component.id + "' of kind '" +
                component.kind + "' does not support transient compilation");
        }

        ComponentCompileContext context{
            component, active_case, {}, {}, {}, {}, {}, {}};
        for (const auto& port : model.descriptor().ports) {
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
                context.port_species.emplace(
                    port.name,
                    find_material(document, medium_id).species);
            }
            const auto species = context.port_species.find(
                port.name);
            for (const auto& spec :
                 canonical_variables_for_domain(
                     port.domain,
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
                } else if (const auto fixed =
                               case_scalar_value(active_case, full_name, true)) {
                    initial = *fixed;
                }
                const auto* transient = find_transient_variable(
                    model.descriptor(), port.name, spec.name);
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
                context.port_variables.emplace(
                    port.name + "." + spec.name, index);
                graph.port_variables.push_back(
                    CompiledPortVariable{
                        component.id, port.name, spec.name,
                        full_name, port.domain, medium_id,
                        spec.dimension, port.direction,
                        explicit_boundary_sign(
                            model.descriptor()),
                        index});
            }
        }
        for (const auto& variable :
             model.descriptor().internal_variables) {
            const std::string full_name =
                component.id + "." + variable.name;
            double initial = variable.initial_value;
            if (const auto value =
                    case_scalar_value(active_case, full_name, false)) {
                initial = *value;
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
            context.internal_variables.emplace(variable.name, index);
            graph.internal_variables.push_back(
                CompiledInternalVariable{component.id, variable.name,
                                         full_name,
                                         variable.dimension, index});
        }
        resolve_component_artifacts(
            context, model, performance_map_registry);
        validate_property_capabilities(context, model);
        model.add_transient_equations(context, system);
    }

    std::map<std::string, std::size_t> connection_counts;
    for (const ConnectionDefinition& connection :
         document.connections) {
        const auto endpoints = validate_connection(
            document, registry, connection, connection_counts);
        for (const auto& spec :
             canonical_variables_for_domain(
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

    if (active_case != nullptr) {
        for (const auto& [key, scalar] :
             active_case->fixed_values) {
            const auto variable_it = variable_indices.find(key);
            if (variable_it == variable_indices.end()) {
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
            const std::size_t residual_index =
                system.add_linear_equation(
                    "fixed." + active_case->id + "." + key,
                    {{variable_it->second, 1.0, 0.0}},
                    scalar.value_si,
                    std::max(std::abs(scalar.value_si), 1.0));
            graph.fixed_value_equations.push_back(
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

    validate_degree_of_freedom(document.model_id, system);
    graph.problem = system.build();
    return graph;
}

}  // namespace thermox::platform
