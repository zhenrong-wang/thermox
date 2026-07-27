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
};

std::vector<CanonicalVariableSpec> canonical_variables_for_domain(const std::string& domain) {
    if (domain == "fluid") {
        return {{"m_dot", 1.0, 100.0},
                {"p", 101325.0, 100000.0},
                {"h", 300000.0, 100000.0}};
    }
    if (domain == "heat") {
        return {{"Q_dot", 0.0, 1000000.0}, {"T", 300.0, 100.0}};
    }
    if (domain == "shaft") {
        return {{"W_dot", 0.0, 1000000.0}, {"omega", 314.1592653589793, 100.0}};
    }
    if (domain == "signal" || domain == "control") {
        return {{"value", 0.0, 1.0}};
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

const ComponentDefinition& find_component(const ModelDocument& document, const std::string& component_id) {
    for (const ComponentDefinition& component : document.components) {
        if (component.id == component_id) {
            return component;
        }
    }
    throw std::invalid_argument("unknown component during graph compilation: " + component_id);
}

const PortDefinition& find_port(const ComponentDefinition& component, const std::string& port_name) {
    const auto it = component.ports.find(port_name);
    if (it == component.ports.end()) {
        throw std::invalid_argument("unknown port during graph compilation: " + component.id + "." + port_name);
    }
    return it->second;
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

void validate_declared_ports(const ComponentDefinition& component, const ComponentModel& model) {
    const auto& expected_ports = model.descriptor().ports;
    if (expected_ports.empty()) {
        return;
    }

    std::set<std::string> expected_names;
    for (const auto& expected : expected_ports) {
        expected_names.insert(expected.name);
        const auto port_it = component.ports.find(expected.name);
        if (port_it == component.ports.end()) {
            throw std::invalid_argument("component '" + component.id + "' of kind '" + component.kind +
                                        "' is missing required port: " + expected.name);
        }
        const PortDefinition& actual = port_it->second;
        if (actual.domain != expected.domain) {
            throw std::invalid_argument("component '" + component.id + "' port '" + expected.name +
                                        "' has incompatible domain for kind '" + component.kind + "'");
        }
        if (expected.direction != "bidirectional" &&
            actual.direction != expected.direction &&
            actual.direction != "bidirectional") {
            throw std::invalid_argument("component '" + component.id + "' port '" + expected.name +
                                        "' has incompatible direction for kind '" + component.kind + "'");
        }
    }
}

std::string_view capability_name(physics::PropertyCapability capability) {
    switch (capability) {
        case physics::PropertyCapability::state_pt: return "state_pt";
        case physics::PropertyCapability::state_ph: return "state_ph";
        case physics::PropertyCapability::state_ps: return "state_ps";
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
    register_transport_component_models(registry);
    register_heat_transfer_component_models(registry);
    register_fluid_inventory_component_models(registry);
    register_storage_component_models(registry);
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
    const CaseDefinition* active_case = select_case(document, case_id);

    EquationSystemBuilder system;
    CompiledModelGraph graph;
    graph.model_id = document.model_id;
    if (active_case != nullptr) {
        graph.case_id = active_case->id;
    }

    std::map<std::string, std::size_t> variable_indices;
    std::map<std::string, std::shared_ptr<const physics::PropertyPackage>>
        medium_properties;
    std::set<std::string> seen_case_keys;
    for (const auto& medium : document.media) {
        medium_properties.emplace(
            medium.id, property_registry.create(medium.backend, medium.substance));
    }

    for (const ComponentDefinition& component : document.components) {
        const ComponentModel& model = registry.require_model(component.kind);
        validate_declared_ports(component, model);
        validate_component_parameters(component, model.descriptor());
        if (!model.descriptor().supports_steady) {
            throw std::invalid_argument(
                "component '" + component.id + "' of kind '" +
                component.kind + "' does not support steady compilation");
        }

        ComponentCompileContext context{component, active_case, {}, {}, {}};
        for (const auto& [port_name, port] : component.ports) {
            if (port.domain == "fluid") {
                const auto package = medium_properties.find(port.medium);
                if (package == medium_properties.end())
                    throw std::logic_error("compiled medium property package missing: " +
                                           port.medium);
                context.port_properties.emplace(port_name, package->second);
            }
            for (const auto& spec : canonical_variables_for_domain(port.domain)) {
                const std::string full_name = variable_key(component.id, port_name, spec.name);
                double initial = spec.initial_value;
                if (const auto value = case_scalar_value(active_case, full_name, false)) {
                    initial = *value;
                    seen_case_keys.insert(full_name);
                } else if (const auto fixed = case_scalar_value(active_case, full_name, true)) {
                    initial = *fixed;
                }
                const std::size_t index = system.add_variable(full_name, initial, spec.scale);
                variable_indices.emplace(full_name, index);
                context.port_variables.emplace(port_name + "." + spec.name, index);
                graph.port_variables.push_back(CompiledPortVariable{component.id, port_name, spec.name,
                                                                     full_name, index});
            }
        }
        validate_property_capabilities(context, model);
        model.add_equations(context, system);
    }

    for (const ConnectionDefinition& connection : document.connections) {
        const std::string from_component = endpoint_component(connection.from);
        const std::string from_port_name = endpoint_port(connection.from);
        const std::string to_component = endpoint_component(connection.to);
        const std::string to_port_name = endpoint_port(connection.to);
        const PortDefinition& from_port = find_port(find_component(document, from_component), from_port_name);
        const PortDefinition& to_port = find_port(find_component(document, to_component), to_port_name);
        if (from_port.domain != to_port.domain) {
            throw std::invalid_argument("connection '" + connection.id + "' links incompatible port domains");
        }

        for (const auto& spec : canonical_variables_for_domain(from_port.domain)) {
            const std::string from_key = variable_key(from_component, from_port_name, spec.name);
            const std::string to_key = variable_key(to_component, to_port_name, spec.name);
            const auto from_it = variable_indices.find(from_key);
            const auto to_it = variable_indices.find(to_key);
            if (from_it == variable_indices.end() || to_it == variable_indices.end()) {
                throw std::logic_error("compiled connection variable missing for: " + connection.id);
            }
            const std::string residual_name = "connection." + connection.id + "." + spec.name;
            const std::size_t residual_index = system.add_linear_equation(
                residual_name,
                {{from_it->second, 1.0}, {to_it->second, -1.0}},
                0.0,
                spec.scale);
            graph.connection_equations.push_back(CompiledConnectionEquation{connection.id, spec.name,
                                                                            residual_name, residual_index});
        }
    }

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
                        const auto port =
                            component->ports.find(port_name);
                        if (port != component->ports.end() &&
                            port->second.domain == "fluid") {
                            const auto package =
                                medium_properties.find(
                                    port->second.medium);
                            if (package ==
                                medium_properties.end()) {
                                throw std::logic_error(
                                    "compiled medium property package missing: " +
                                    port->second.medium);
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
    std::map<std::string, std::shared_ptr<const physics::PropertyPackage>>
        medium_properties;
    for (const auto& medium : document.media) {
        medium_properties.emplace(
            medium.id,
            property_registry.create(medium.backend, medium.substance));
    }

    for (const ComponentDefinition& component : document.components) {
        const ComponentModel& model =
            registry.require_model(component.kind);
        validate_declared_ports(component, model);
        validate_component_parameters(component, model.descriptor());
        if (!model.descriptor().supports_transient) {
            throw std::invalid_argument(
                "component '" + component.id + "' of kind '" +
                component.kind + "' does not support transient compilation");
        }

        ComponentCompileContext context{component, active_case, {}, {}, {}};
        for (const auto& [port_name, port] : component.ports) {
            if (port.domain == "fluid") {
                const auto package = medium_properties.find(port.medium);
                if (package == medium_properties.end()) {
                    throw std::logic_error(
                        "compiled medium property package missing: " +
                        port.medium);
                }
                context.port_properties.emplace(port_name, package->second);
            }
            for (const auto& spec :
                 canonical_variables_for_domain(port.domain)) {
                const std::string full_name =
                    variable_key(component.id, port_name, spec.name);
                double initial = spec.initial_value;
                if (const auto value =
                        case_scalar_value(active_case, full_name, false)) {
                    initial = *value;
                } else if (const auto fixed =
                               case_scalar_value(active_case, full_name, true)) {
                    initial = *fixed;
                }
                const auto* transient = find_transient_variable(
                    model.descriptor(), port_name, spec.name);
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
                    port_name + "." + spec.name, index);
                graph.port_variables.push_back(
                    CompiledPortVariable{component.id, port_name,
                                         spec.name, full_name, index});
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
                                         full_name, index});
        }
        validate_property_capabilities(context, model);
        model.add_transient_equations(context, system);
    }

    for (const ConnectionDefinition& connection :
         document.connections) {
        const std::string from_component =
            endpoint_component(connection.from);
        const std::string from_port_name =
            endpoint_port(connection.from);
        const std::string to_component =
            endpoint_component(connection.to);
        const std::string to_port_name =
            endpoint_port(connection.to);
        const PortDefinition& from_port = find_port(
            find_component(document, from_component), from_port_name);
        const PortDefinition& to_port = find_port(
            find_component(document, to_component), to_port_name);
        if (from_port.domain != to_port.domain) {
            throw std::invalid_argument(
                "connection '" + connection.id +
                "' links incompatible port domains");
        }
        for (const auto& spec :
             canonical_variables_for_domain(from_port.domain)) {
            const auto from_it = variable_indices.find(variable_key(
                from_component, from_port_name, spec.name));
            const auto to_it = variable_indices.find(variable_key(
                to_component, to_port_name, spec.name));
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
