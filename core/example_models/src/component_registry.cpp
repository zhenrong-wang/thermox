#include "thermox/examples/component_registry.hpp"

#include "thermox/examples/ideal_gas.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

namespace thermox::examples {

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
                {"h", 300000.0, 100000.0},
                {"T", 300.0, 100.0}};
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
        if (actual.direction != expected.direction && actual.direction != "bidirectional") {
            throw std::invalid_argument("component '" + component.id + "' port '" + expected.name +
                                        "' has incompatible direction for kind '" + component.kind + "'");
        }
    }
}

ComponentModelDescriptor make_descriptor(std::string kind,
                                    std::vector<PortModelDescriptor> ports,
                                    std::string version = "1.0.0") {
    ComponentModelDescriptor out;
    out.kind = std::move(kind);
    out.version = std::move(version);
    out.ports = std::move(ports);
    return out;
}

std::size_t require_port_variable(const ComponentCompileContext& context,
                                  const std::string& key) {
    const auto it = context.port_variables.find(key);
    if (it == context.port_variables.end()) {
        throw std::logic_error("compiled component variable missing: " + context.component.id + "." + key);
    }
    return it->second;
}

double required_parameter(const ComponentDefinition& component, const std::string& name) {
    const auto it = component.parameters.find(name);
    if (it == component.parameters.end()) {
        throw std::invalid_argument("component '" + component.id + "' is missing required parameter: " + name);
    }
    return it->second.value_si;
}

double optional_parameter(const ComponentDefinition& component,
                          const std::string& name,
                          double default_value) {
    const auto it = component.parameters.find(name);
    return it == component.parameters.end() ? default_value : it->second.value_si;
}

void validate_positive(const ComponentDefinition& component,
                       const std::string& name,
                       double value) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument("component '" + component.id + "' parameter '" + name +
                                    "' must be positive and finite");
    }
}

class IdealGasCompressorModel final : public ComponentModel {
public:
    IdealGasCompressorModel()
        : descriptor_(make_descriptor("compressor.gas.isentropic_efficiency",
                                 {{"inlet", "fluid", "in"},
                                  {"outlet", "fluid", "out"},
                                  {"shaft", "shaft", "in"}})) {}

    const ComponentModelDescriptor& descriptor() const override { return descriptor_; }

    void add_equations(const ComponentCompileContext& context,
                       EquationSystemBuilder& system) const override {
        const IdealGas defaults;
        const IdealGas gas{optional_parameter(context.component, "cp", defaults.cp),
                           optional_parameter(context.component, "gamma", defaults.gamma),
                           optional_parameter(context.component, "gas_constant", defaults.gas_constant)};
        const double pressure_ratio = required_parameter(context.component, "pressure_ratio");
        const double eta_is = required_parameter(context.component, "eta_is");
        validate_positive(context.component, "cp", gas.cp);
        validate_positive(context.component, "gamma", gas.gamma);
        validate_positive(context.component, "pressure_ratio", pressure_ratio);
        validate_positive(context.component, "eta_is", eta_is);
        if (gas.gamma <= 1.0) {
            throw std::invalid_argument("component '" + context.component.id +
                                        "' parameter 'gamma' must be greater than 1");
        }
        if (pressure_ratio <= 1.0) {
            throw std::invalid_argument("component '" + context.component.id +
                                        "' parameter 'pressure_ratio' must be greater than 1");
        }
        if (eta_is > 1.0) {
            throw std::invalid_argument("component '" + context.component.id +
                                        "' parameter 'eta_is' must be <= 1");
        }

        const std::size_t inlet_m = require_port_variable(context, "inlet.m_dot");
        const std::size_t inlet_p = require_port_variable(context, "inlet.p");
        const std::size_t inlet_h = require_port_variable(context, "inlet.h");
        const std::size_t inlet_t = require_port_variable(context, "inlet.T");
        const std::size_t outlet_m = require_port_variable(context, "outlet.m_dot");
        const std::size_t outlet_p = require_port_variable(context, "outlet.p");
        const std::size_t outlet_h = require_port_variable(context, "outlet.h");
        const std::size_t outlet_t = require_port_variable(context, "outlet.T");
        const std::size_t shaft_w = require_port_variable(context, "shaft.W_dot");

        const std::string prefix = "component." + context.component.id + ".";
        system.add_linear_equation(prefix + "mass_continuity",
                                   {{outlet_m, 1.0}, {inlet_m, -1.0}},
                                   0.0,
                                   100.0);
        system.add_linear_equation(prefix + "pressure_ratio",
                                   {{outlet_p, 1.0}, {inlet_p, -pressure_ratio}},
                                   0.0,
                                   100000.0 * pressure_ratio);
        system.add_linear_equation(prefix + "inlet_ideal_gas_h",
                                   {{inlet_h, 1.0}, {inlet_t, -gas.cp}},
                                   0.0,
                                   gas.cp * 300.0);
        system.add_linear_equation(prefix + "outlet_ideal_gas_h",
                                   {{outlet_h, 1.0}, {outlet_t, -gas.cp}},
                                   0.0,
                                   gas.cp * 600.0);

        const double isentropic_temperature_factor =
            std::pow(pressure_ratio, (gas.gamma - 1.0) / gas.gamma);
        const double actual_temperature_factor =
            1.0 + (isentropic_temperature_factor - 1.0) / eta_is;
        system.add_linear_equation(prefix + "isentropic_efficiency_temperature",
                                   {{outlet_t, 1.0}, {inlet_t, -actual_temperature_factor}},
                                   0.0,
                                   100.0 * actual_temperature_factor);
        system.add_sparse_equation(
            prefix + "shaft_power",
            [inlet_m, inlet_h, outlet_h, shaft_w](const std::vector<double>& x,
                                                  std::vector<EquationPartial>& jacobian_row) {
                const double mass_flow = x.at(inlet_m);
                const double enthalpy_rise = x.at(outlet_h) - x.at(inlet_h);
                jacobian_row.push_back(EquationPartial{shaft_w, 1.0});
                jacobian_row.push_back(EquationPartial{inlet_m, -enthalpy_rise});
                jacobian_row.push_back(EquationPartial{inlet_h, mass_flow});
                jacobian_row.push_back(EquationPartial{outlet_h, -mass_flow});
                return x.at(shaft_w) - mass_flow * enthalpy_rise;
            },
            1000000.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class IdealGasTurbineModel final : public ComponentModel {
public:
    IdealGasTurbineModel()
        : descriptor_(make_descriptor("turbine.gas.isentropic_efficiency",
                                      {{"inlet", "fluid", "in"},
                                       {"outlet", "fluid", "out"},
                                       {"shaft", "shaft", "out"}})) {}

    const ComponentModelDescriptor& descriptor() const override { return descriptor_; }

    void add_equations(const ComponentCompileContext& context,
                       EquationSystemBuilder& system) const override {
        const IdealGas defaults;
        const IdealGas gas{optional_parameter(context.component, "cp", defaults.cp),
                           optional_parameter(context.component, "gamma", defaults.gamma),
                           optional_parameter(context.component, "gas_constant", defaults.gas_constant)};
        const double pressure_ratio = required_parameter(context.component, "pressure_ratio");
        const double eta_is = required_parameter(context.component, "eta_is");
        validate_positive(context.component, "cp", gas.cp);
        validate_positive(context.component, "gamma", gas.gamma);
        validate_positive(context.component, "pressure_ratio", pressure_ratio);
        validate_positive(context.component, "eta_is", eta_is);
        if (gas.gamma <= 1.0) {
            throw std::invalid_argument("component '" + context.component.id +
                                        "' parameter 'gamma' must be greater than 1");
        }
        if (pressure_ratio <= 1.0) {
            throw std::invalid_argument("component '" + context.component.id +
                                        "' parameter 'pressure_ratio' must be greater than 1");
        }
        if (eta_is > 1.0) {
            throw std::invalid_argument("component '" + context.component.id +
                                        "' parameter 'eta_is' must be <= 1");
        }

        const std::size_t inlet_m = require_port_variable(context, "inlet.m_dot");
        const std::size_t inlet_p = require_port_variable(context, "inlet.p");
        const std::size_t inlet_h = require_port_variable(context, "inlet.h");
        const std::size_t inlet_t = require_port_variable(context, "inlet.T");
        const std::size_t outlet_m = require_port_variable(context, "outlet.m_dot");
        const std::size_t outlet_p = require_port_variable(context, "outlet.p");
        const std::size_t outlet_h = require_port_variable(context, "outlet.h");
        const std::size_t outlet_t = require_port_variable(context, "outlet.T");
        const std::size_t shaft_w = require_port_variable(context, "shaft.W_dot");

        const std::string prefix = "component." + context.component.id + ".";
        system.add_linear_equation(prefix + "mass_continuity",
                                   {{outlet_m, 1.0}, {inlet_m, -1.0}},
                                   0.0,
                                   100.0);
        system.add_linear_equation(prefix + "pressure_ratio",
                                   {{inlet_p, 1.0}, {outlet_p, -pressure_ratio}},
                                   0.0,
                                   100000.0 * pressure_ratio);
        system.add_linear_equation(prefix + "inlet_ideal_gas_h",
                                   {{inlet_h, 1.0}, {inlet_t, -gas.cp}},
                                   0.0,
                                   gas.cp * 1200.0);
        system.add_linear_equation(prefix + "outlet_ideal_gas_h",
                                   {{outlet_h, 1.0}, {outlet_t, -gas.cp}},
                                   0.0,
                                   gas.cp * 800.0);

        const double isentropic_temperature_factor =
            std::pow(1.0 / pressure_ratio, (gas.gamma - 1.0) / gas.gamma);
        const double actual_temperature_factor =
            1.0 - eta_is * (1.0 - isentropic_temperature_factor);
        system.add_linear_equation(prefix + "isentropic_efficiency_temperature",
                                   {{outlet_t, 1.0}, {inlet_t, -actual_temperature_factor}},
                                   0.0,
                                   100.0 * actual_temperature_factor);
        system.add_sparse_equation(
            prefix + "shaft_power",
            [inlet_m, inlet_h, outlet_h, shaft_w](const std::vector<double>& x,
                                                  std::vector<EquationPartial>& jacobian_row) {
                const double mass_flow = x.at(inlet_m);
                const double enthalpy_drop = x.at(inlet_h) - x.at(outlet_h);
                jacobian_row.push_back(EquationPartial{shaft_w, 1.0});
                jacobian_row.push_back(EquationPartial{inlet_m, -enthalpy_drop});
                jacobian_row.push_back(EquationPartial{inlet_h, -mass_flow});
                jacobian_row.push_back(EquationPartial{outlet_h, mass_flow});
                return x.at(shaft_w) - mass_flow * enthalpy_drop;
            },
            1000000.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

}  // namespace
MetadataComponentModel::MetadataComponentModel(ComponentModelDescriptor descriptor)
    : descriptor_(std::move(descriptor)) {}

void MetadataComponentModel::add_equations(const ComponentCompileContext&, EquationSystemBuilder&) const {
    // Metadata-only MVP component models validate port contracts and reserve extension points.
    // Physical component residuals are added by specialized ComponentModel implementations.
}

void ComponentRegistry::register_model(std::shared_ptr<const ComponentModel> model) {
    if (!model) {
        throw std::invalid_argument("component model registration must not be null");
    }
    const std::string& kind = model->descriptor().kind;
    if (kind.empty()) {
        throw std::invalid_argument("component model kind must not be empty");
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

ComponentRegistry make_default_component_registry() {
    ComponentRegistry registry;
    registry.register_model(std::make_shared<MetadataComponentModel>(make_descriptor(
        "source.fluid.boundary", {{"outlet", "fluid", "out"}})));
    registry.register_model(std::make_shared<MetadataComponentModel>(make_descriptor(
        "sink.fluid.boundary", {{"inlet", "fluid", "in"}})));
    registry.register_model(std::make_shared<IdealGasCompressorModel>());
    registry.register_model(std::make_shared<IdealGasTurbineModel>());
    registry.register_model(std::make_shared<MetadataComponentModel>(make_descriptor(
        "pump.fluid.isentropic_efficiency",
        {{"inlet", "fluid", "in"}, {"outlet", "fluid", "out"}, {"shaft", "shaft", "in"}})));
    registry.register_model(std::make_shared<MetadataComponentModel>(make_descriptor(
        "heat_exchanger.simple",
        {{"hot_in", "fluid", "in"},
         {"hot_out", "fluid", "out"},
         {"cold_in", "fluid", "in"},
         {"cold_out", "fluid", "out"}})));
    return registry;
}

CompiledModelGraph compile_model_graph(const ModelDocument& document,
                                       const ComponentRegistry& registry,
                                       const std::string& case_id) {
    const CaseDefinition* active_case = select_case(document, case_id);

    EquationSystemBuilder system;
    CompiledModelGraph graph;
    graph.model_id = document.model_id;
    if (active_case != nullptr) {
        graph.case_id = active_case->id;
    }

    std::map<std::string, std::size_t> variable_indices;
    std::set<std::string> seen_case_keys;

    for (const ComponentDefinition& component : document.components) {
        const ComponentModel& model = registry.require_model(component.kind);
        validate_declared_ports(component, model);

        ComponentCompileContext context{component, active_case, {}};
        for (const auto& [port_name, port] : component.ports) {
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

    graph.problem = system.build();
    return graph;
}

}  // namespace thermox::examples
