#include "thermox/platform/component_registry.hpp"

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

std::size_t require_internal_variable(
    const ComponentCompileContext& context,
    const std::string& name) {
    const auto it = context.internal_variables.find(name);
    if (it == context.internal_variables.end()) {
        throw std::logic_error(
            "compiled component internal variable missing: " +
            context.component.id + "." + name);
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
    return it == component.parameters.end() ? default_value
                                            : it->second.value_si;
}

void validate_positive(const ComponentDefinition& component,
                       const std::string& name,
                       double value) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument("component '" + component.id + "' parameter '" + name +
                                    "' must be positive and finite");
    }
}

std::shared_ptr<const physics::PropertyPackage> require_property_package(
    const ComponentCompileContext& context, const std::string& port) {
    const auto it = context.port_properties.find(port);
    if (it == context.port_properties.end() || !it->second)
        throw std::logic_error("compiled property package missing: " +
                               context.component.id + "." + port);
    return it->second;
}

EvaluationStatus property_failure(const physics::PropertyResult& result) {
    if (result.status == physics::PropertyStatus::backend_error)
        return EvaluationStatus::fatal(result.message);
    return EvaluationStatus::recoverable(result.message);
}

EvaluationStatus property_partial(
    const physics::PropertyPackage& properties,
    double pressure,
    double enthalpy,
    double base_value,
    bool with_respect_to_pressure,
    double (*extract)(const physics::ThermodynamicState&),
    double& derivative) {
    const double coordinate =
        with_respect_to_pressure ? pressure : enthalpy;
    const double step =
        std::max(std::abs(coordinate) * 1.0e-6, 1.0e-3);
    const auto plus = properties.state_ph(
        with_respect_to_pressure ? pressure + step : pressure,
        with_respect_to_pressure ? enthalpy : enthalpy + step);
    if (plus.ok()) {
        derivative = (extract(plus.state) - base_value) / step;
        return EvaluationStatus::success();
    }
    const auto minus = properties.state_ph(
        with_respect_to_pressure ? pressure - step : pressure,
        with_respect_to_pressure ? enthalpy : enthalpy - step);
    if (minus.ok()) {
        derivative = (base_value - extract(minus.state)) / step;
        return EvaluationStatus::success();
    }
    return property_failure(plus);
}

EvaluationStatus log_mean_temperature_difference(
    double difference_a,
    double difference_b,
    double& value) {
    if (!std::isfinite(difference_a) ||
        !std::isfinite(difference_b) ||
        difference_a <= 0.0 || difference_b <= 0.0) {
        return EvaluationStatus::recoverable(
            "heat exchanger terminal temperature differences must be positive");
    }
    const double largest =
        std::max(std::abs(difference_a), std::abs(difference_b));
    if (std::abs(difference_a - difference_b) <=
        1.0e-8 * std::max(largest, 1.0)) {
        value = 0.5 * (difference_a + difference_b);
        return EvaluationStatus::success();
    }
    value = (difference_a - difference_b) /
            std::log(difference_a / difference_b);
    if (!std::isfinite(value) || value <= 0.0) {
        return EvaluationStatus::recoverable(
            "heat exchanger LMTD evaluation failed");
    }
    return EvaluationStatus::success();
}

void add_turbomachinery_equations(const ComponentCompileContext& context,
                                  EquationSystemBuilder& system,
                                  bool compressor) {
    const double pressure_ratio = required_parameter(context.component, "pressure_ratio");
    const double eta_is = required_parameter(context.component, "eta_is");
    validate_positive(context.component, "pressure_ratio", pressure_ratio);
    validate_positive(context.component, "eta_is", eta_is);
    if (pressure_ratio <= 1.0)
        throw std::invalid_argument("component '" + context.component.id +
                                    "' parameter 'pressure_ratio' must be greater than 1");
    if (eta_is > 1.0)
        throw std::invalid_argument("component '" + context.component.id +
                                    "' parameter 'eta_is' must be <= 1");

    const auto properties = require_property_package(context, "inlet");
    if (properties != require_property_package(context, "outlet"))
        throw std::invalid_argument("component '" + context.component.id +
                                    "' inlet and outlet must use the same medium");

    const auto inlet_m = require_port_variable(context, "inlet.m_dot");
    const auto inlet_p = require_port_variable(context, "inlet.p");
    const auto inlet_h = require_port_variable(context, "inlet.h");
    const auto outlet_m = require_port_variable(context, "outlet.m_dot");
    const auto outlet_p = require_port_variable(context, "outlet.p");
    const auto outlet_h = require_port_variable(context, "outlet.h");
    const auto shaft_w = require_port_variable(context, "shaft.W_dot");
    const std::string prefix = "component." + context.component.id + ".";

    system.add_linear_equation(prefix + "mass_continuity",
                               {{outlet_m, 1.0}, {inlet_m, -1.0}}, 0.0, 100.0);
    system.add_linear_equation(
        prefix + "pressure_ratio",
        compressor
            ? std::vector<LinearTerm>{{outlet_p, 1.0}, {inlet_p, -pressure_ratio}}
            : std::vector<LinearTerm>{{inlet_p, 1.0}, {outlet_p, -pressure_ratio}},
        0.0, 100000.0 * pressure_ratio);

    system.add_checked_equation(
        prefix + "isentropic_efficiency",
        [properties, compressor, eta_is, inlet_p, inlet_h, outlet_p,
         outlet_h](const std::vector<double>& x, double& residual) {
            const auto inlet = properties->state_ph(x.at(inlet_p), x.at(inlet_h));
            if (!inlet.ok()) return property_failure(inlet);
            const auto isentropic =
                properties->state_ps(x.at(outlet_p), inlet.state.entropy_j_kg_k);
            if (!isentropic.ok()) return property_failure(isentropic);
            const double ideal_change =
                isentropic.state.enthalpy_j_kg - x.at(inlet_h);
            residual = x.at(outlet_h) - x.at(inlet_h) -
                       (compressor ? ideal_change / eta_is : eta_is * ideal_change);
            return EvaluationStatus::success();
        },
        1e6);

    system.add_sparse_equation(
        prefix + "shaft_power",
        [compressor, inlet_m, inlet_h, outlet_h, shaft_w](
            const std::vector<double>& x, std::vector<EquationPartial>& jacobian) {
            const double direction = compressor ? 1.0 : -1.0;
            const double enthalpy_change = x.at(outlet_h) - x.at(inlet_h);
            jacobian.push_back({shaft_w, 1.0});
            jacobian.push_back({inlet_m, -direction * enthalpy_change});
            jacobian.push_back({inlet_h, direction * x.at(inlet_m)});
            jacobian.push_back({outlet_h, -direction * x.at(inlet_m)});
            return x.at(shaft_w) - direction * x.at(inlet_m) * enthalpy_change;
        },
        1e6);
}

class PropertyCompressorModel final : public ComponentModel {
public:
    explicit PropertyCompressorModel(std::string kind)
        : descriptor_(make_descriptor(std::move(kind),
                                 {{"inlet", "fluid", "in"},
                                  {"outlet", "fluid", "out"},
                                  {"shaft", "shaft", "in"}})) {
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph,
            physics::PropertyCapability::state_ps};
    }

    const ComponentModelDescriptor& descriptor() const override { return descriptor_; }

    void add_equations(const ComponentCompileContext& context,
                       EquationSystemBuilder& system) const override {
        add_turbomachinery_equations(context, system, true);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class PropertyTurbineModel final : public ComponentModel {
public:
    explicit PropertyTurbineModel(std::string kind)
        : descriptor_(make_descriptor(std::move(kind),
                                      {{"inlet", "fluid", "in"},
                                       {"outlet", "fluid", "out"},
                                       {"shaft", "shaft", "out"}})) {
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph,
            physics::PropertyCapability::state_ps};
    }

    const ComponentModelDescriptor& descriptor() const override { return descriptor_; }

    void add_equations(const ComponentCompileContext& context,
                       EquationSystemBuilder& system) const override {
        add_turbomachinery_equations(context, system, false);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class TwoInletFluidMixerModel final : public ComponentModel {
public:
    TwoInletFluidMixerModel()
        : descriptor_(make_descriptor(
              "junction.fluid.mixer.two_inlet",
              {{"inlet_a", "fluid", "in"},
               {"inlet_b", "fluid", "in"},
               {"outlet", "fluid", "out"}})) {}

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(const ComponentCompileContext& context,
                       EquationSystemBuilder& system) const override {
        const auto properties =
            require_property_package(context, "inlet_a");
        if (properties !=
                require_property_package(context, "inlet_b") ||
            properties !=
                require_property_package(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' mixer ports must use the same medium");
        }

        const auto mass_a =
            require_port_variable(context, "inlet_a.m_dot");
        const auto pressure_a =
            require_port_variable(context, "inlet_a.p");
        const auto enthalpy_a =
            require_port_variable(context, "inlet_a.h");
        const auto mass_b =
            require_port_variable(context, "inlet_b.m_dot");
        const auto pressure_b =
            require_port_variable(context, "inlet_b.p");
        const auto enthalpy_b =
            require_port_variable(context, "inlet_b.h");
        const auto mass_out =
            require_port_variable(context, "outlet.m_dot");
        const auto pressure_out =
            require_port_variable(context, "outlet.p");
        const auto enthalpy_out =
            require_port_variable(context, "outlet.h");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_balance",
            {{mass_out, 1.0}, {mass_a, -1.0}, {mass_b, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "pressure_inlet_a",
            {{pressure_out, 1.0}, {pressure_a, -1.0}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "pressure_inlet_b",
            {{pressure_out, 1.0}, {pressure_b, -1.0}},
            0.0, 100000.0);
        system.add_sparse_equation(
            prefix + "energy_balance",
            {mass_a, enthalpy_a, mass_b, enthalpy_b,
             mass_out, enthalpy_out},
            [mass_a, enthalpy_a, mass_b, enthalpy_b,
             mass_out, enthalpy_out](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                jacobian.push_back(
                    {mass_out, x.at(enthalpy_out)});
                jacobian.push_back(
                    {enthalpy_out, x.at(mass_out)});
                jacobian.push_back(
                    {mass_a, -x.at(enthalpy_a)});
                jacobian.push_back(
                    {enthalpy_a, -x.at(mass_a)});
                jacobian.push_back(
                    {mass_b, -x.at(enthalpy_b)});
                jacobian.push_back(
                    {enthalpy_b, -x.at(mass_b)});
                return x.at(mass_out) * x.at(enthalpy_out) -
                       x.at(mass_a) * x.at(enthalpy_a) -
                       x.at(mass_b) * x.at(enthalpy_b);
            },
            1.0e8);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class TwoOutletFluidSplitterModel final : public ComponentModel {
public:
    TwoOutletFluidSplitterModel()
        : descriptor_(make_descriptor(
              "junction.fluid.splitter.two_outlet",
              {{"inlet", "fluid", "in"},
               {"outlet_a", "fluid", "out"},
               {"outlet_b", "fluid", "out"}})) {}

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(const ComponentCompileContext& context,
                       EquationSystemBuilder& system) const override {
        const auto properties =
            require_property_package(context, "inlet");
        if (properties !=
                require_property_package(context, "outlet_a") ||
            properties !=
                require_property_package(context, "outlet_b")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' splitter ports must use the same medium");
        }

        const auto mass_in =
            require_port_variable(context, "inlet.m_dot");
        const auto pressure_in =
            require_port_variable(context, "inlet.p");
        const auto enthalpy_in =
            require_port_variable(context, "inlet.h");
        const auto mass_a =
            require_port_variable(context, "outlet_a.m_dot");
        const auto pressure_a =
            require_port_variable(context, "outlet_a.p");
        const auto enthalpy_a =
            require_port_variable(context, "outlet_a.h");
        const auto mass_b =
            require_port_variable(context, "outlet_b.m_dot");
        const auto pressure_b =
            require_port_variable(context, "outlet_b.p");
        const auto enthalpy_b =
            require_port_variable(context, "outlet_b.h");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_balance",
            {{mass_in, 1.0}, {mass_a, -1.0}, {mass_b, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "pressure_outlet_a",
            {{pressure_a, 1.0}, {pressure_in, -1.0}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "pressure_outlet_b",
            {{pressure_b, 1.0}, {pressure_in, -1.0}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "enthalpy_outlet_a",
            {{enthalpy_a, 1.0}, {enthalpy_in, -1.0}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "enthalpy_outlet_b",
            {{enthalpy_b, 1.0}, {enthalpy_in, -1.0}},
            0.0, 100000.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class IsenthalpicPressureRatioValveModel final : public ComponentModel {
public:
    IsenthalpicPressureRatioValveModel()
        : descriptor_(make_descriptor(
              "valve.fluid.isenthalpic_pressure_ratio",
              {{"inlet", "fluid", "in"},
               {"outlet", "fluid", "out"}})) {}

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(const ComponentCompileContext& context,
                       EquationSystemBuilder& system) const override {
        const double pressure_ratio =
            required_parameter(context.component, "pressure_ratio");
        validate_positive(context.component, "pressure_ratio",
                          pressure_ratio);
        if (pressure_ratio <= 1.0) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' parameter 'pressure_ratio' must be greater than 1");
        }
        if (require_property_package(context, "inlet") !=
            require_property_package(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' inlet and outlet must use the same medium");
        }

        const auto inlet_m =
            require_port_variable(context, "inlet.m_dot");
        const auto inlet_p =
            require_port_variable(context, "inlet.p");
        const auto inlet_h =
            require_port_variable(context, "inlet.h");
        const auto outlet_m =
            require_port_variable(context, "outlet.m_dot");
        const auto outlet_p =
            require_port_variable(context, "outlet.p");
        const auto outlet_h =
            require_port_variable(context, "outlet.h");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_continuity",
            {{outlet_m, 1.0}, {inlet_m, -1.0}}, 0.0, 100.0);
        system.add_linear_equation(
            prefix + "pressure_ratio",
            {{inlet_p, 1.0}, {outlet_p, -pressure_ratio}},
            0.0, 100000.0 * pressure_ratio);
        system.add_linear_equation(
            prefix + "isenthalpic",
            {{outlet_h, 1.0}, {inlet_h, -1.0}},
            0.0, 100000.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class FixedDutyHeatExchangerModel final : public ComponentModel {
public:
    FixedDutyHeatExchangerModel()
        : descriptor_(make_descriptor(
              "heat_exchanger.fluid.fixed_duty",
              {{"hot_in", "fluid", "in"},
               {"hot_out", "fluid", "out"},
               {"cold_in", "fluid", "in"},
               {"cold_out", "fluid", "out"}})) {}

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(const ComponentCompileContext& context,
                       EquationSystemBuilder& system) const override {
        const double duty =
            required_parameter(context.component, "heat_duty");
        validate_positive(context.component, "heat_duty", duty);
        const double hot_loss = optional_parameter(
            context.component, "hot_pressure_loss_fraction", 0.0);
        const double cold_loss = optional_parameter(
            context.component, "cold_pressure_loss_fraction", 0.0);
        const auto validate_loss = [&](const std::string& name,
                                       double loss) {
            if (!std::isfinite(loss) || loss < 0.0 || loss >= 1.0) {
                throw std::invalid_argument(
                    "component '" + context.component.id +
                    "' parameter '" + name +
                    "' must be finite and in [0, 1)");
            }
        };
        validate_loss("hot_pressure_loss_fraction", hot_loss);
        validate_loss("cold_pressure_loss_fraction", cold_loss);
        if (require_property_package(context, "hot_in") !=
            require_property_package(context, "hot_out")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' hot-side ports must use the same medium");
        }
        if (require_property_package(context, "cold_in") !=
            require_property_package(context, "cold_out")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' cold-side ports must use the same medium");
        }

        const auto hot_in_m =
            require_port_variable(context, "hot_in.m_dot");
        const auto hot_in_p =
            require_port_variable(context, "hot_in.p");
        const auto hot_in_h =
            require_port_variable(context, "hot_in.h");
        const auto hot_out_m =
            require_port_variable(context, "hot_out.m_dot");
        const auto hot_out_p =
            require_port_variable(context, "hot_out.p");
        const auto hot_out_h =
            require_port_variable(context, "hot_out.h");
        const auto cold_in_m =
            require_port_variable(context, "cold_in.m_dot");
        const auto cold_in_p =
            require_port_variable(context, "cold_in.p");
        const auto cold_in_h =
            require_port_variable(context, "cold_in.h");
        const auto cold_out_m =
            require_port_variable(context, "cold_out.m_dot");
        const auto cold_out_p =
            require_port_variable(context, "cold_out.p");
        const auto cold_out_h =
            require_port_variable(context, "cold_out.h");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "hot_mass_continuity",
            {{hot_out_m, 1.0}, {hot_in_m, -1.0}}, 0.0, 100.0);
        system.add_linear_equation(
            prefix + "cold_mass_continuity",
            {{cold_out_m, 1.0}, {cold_in_m, -1.0}}, 0.0, 100.0);
        system.add_linear_equation(
            prefix + "hot_pressure_loss",
            {{hot_out_p, 1.0}, {hot_in_p, -(1.0 - hot_loss)}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "cold_pressure_loss",
            {{cold_out_p, 1.0}, {cold_in_p, -(1.0 - cold_loss)}},
            0.0, 100000.0);
        system.add_sparse_equation(
            prefix + "hot_energy",
            {hot_in_m, hot_in_h, hot_out_h},
            [hot_in_m, hot_in_h, hot_out_h, duty](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                const double delta_h =
                    x.at(hot_in_h) - x.at(hot_out_h);
                jacobian.push_back({hot_in_m, delta_h});
                jacobian.push_back({hot_in_h, x.at(hot_in_m)});
                jacobian.push_back({hot_out_h, -x.at(hot_in_m)});
                return x.at(hot_in_m) * delta_h - duty;
            },
            std::max(duty, 1.0));
        system.add_sparse_equation(
            prefix + "cold_energy",
            {cold_in_m, cold_in_h, cold_out_h},
            [cold_in_m, cold_in_h, cold_out_h, duty](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                const double delta_h =
                    x.at(cold_out_h) - x.at(cold_in_h);
                jacobian.push_back({cold_in_m, delta_h});
                jacobian.push_back({cold_in_h, -x.at(cold_in_m)});
                jacobian.push_back({cold_out_h, x.at(cold_in_m)});
                return x.at(cold_in_m) * delta_h - duty;
            },
            std::max(duty, 1.0));
    }

private:
    ComponentModelDescriptor descriptor_;
};

class CounterflowUaHeatExchangerModel final : public ComponentModel {
public:
    CounterflowUaHeatExchangerModel()
        : descriptor_(make_descriptor(
              "heat_exchanger.fluid.counterflow_ua",
              {{"hot_in", "fluid", "in"},
               {"hot_out", "fluid", "out"},
               {"cold_in", "fluid", "in"},
               {"cold_out", "fluid", "out"}})) {
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(const ComponentCompileContext& context,
                       EquationSystemBuilder& system) const override {
        const double conductance =
            required_parameter(context.component, "UA");
        validate_positive(context.component, "UA", conductance);
        const double hot_loss = optional_parameter(
            context.component, "hot_pressure_loss_fraction", 0.0);
        const double cold_loss = optional_parameter(
            context.component, "cold_pressure_loss_fraction", 0.0);
        const auto validate_loss = [&](const std::string& name,
                                       double loss) {
            if (!std::isfinite(loss) || loss < 0.0 || loss >= 1.0) {
                throw std::invalid_argument(
                    "component '" + context.component.id +
                    "' parameter '" + name +
                    "' must be finite and in [0, 1)");
            }
        };
        validate_loss("hot_pressure_loss_fraction", hot_loss);
        validate_loss("cold_pressure_loss_fraction", cold_loss);
        const auto hot_properties =
            require_property_package(context, "hot_in");
        const auto cold_properties =
            require_property_package(context, "cold_in");
        if (hot_properties !=
            require_property_package(context, "hot_out")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' hot-side ports must use the same medium");
        }
        if (cold_properties !=
            require_property_package(context, "cold_out")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' cold-side ports must use the same medium");
        }

        const auto hot_in_m =
            require_port_variable(context, "hot_in.m_dot");
        const auto hot_in_p =
            require_port_variable(context, "hot_in.p");
        const auto hot_in_h =
            require_port_variable(context, "hot_in.h");
        const auto hot_out_m =
            require_port_variable(context, "hot_out.m_dot");
        const auto hot_out_p =
            require_port_variable(context, "hot_out.p");
        const auto hot_out_h =
            require_port_variable(context, "hot_out.h");
        const auto cold_in_m =
            require_port_variable(context, "cold_in.m_dot");
        const auto cold_in_p =
            require_port_variable(context, "cold_in.p");
        const auto cold_in_h =
            require_port_variable(context, "cold_in.h");
        const auto cold_out_m =
            require_port_variable(context, "cold_out.m_dot");
        const auto cold_out_p =
            require_port_variable(context, "cold_out.p");
        const auto cold_out_h =
            require_port_variable(context, "cold_out.h");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "hot_mass_continuity",
            {{hot_out_m, 1.0}, {hot_in_m, -1.0}}, 0.0, 100.0);
        system.add_linear_equation(
            prefix + "cold_mass_continuity",
            {{cold_out_m, 1.0}, {cold_in_m, -1.0}}, 0.0, 100.0);
        system.add_linear_equation(
            prefix + "hot_pressure_loss",
            {{hot_out_p, 1.0}, {hot_in_p, -(1.0 - hot_loss)}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "cold_pressure_loss",
            {{cold_out_p, 1.0}, {cold_in_p, -(1.0 - cold_loss)}},
            0.0, 100000.0);
        system.add_sparse_equation(
            prefix + "energy_balance",
            {hot_in_m, hot_in_h, hot_out_h,
             cold_in_m, cold_in_h, cold_out_h},
            [hot_in_m, hot_in_h, hot_out_h,
             cold_in_m, cold_in_h, cold_out_h](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                const double hot_delta =
                    x.at(hot_in_h) - x.at(hot_out_h);
                const double cold_delta =
                    x.at(cold_out_h) - x.at(cold_in_h);
                jacobian.push_back({hot_in_m, hot_delta});
                jacobian.push_back({hot_in_h, x.at(hot_in_m)});
                jacobian.push_back({hot_out_h, -x.at(hot_in_m)});
                jacobian.push_back({cold_in_m, -cold_delta});
                jacobian.push_back({cold_in_h, x.at(cold_in_m)});
                jacobian.push_back({cold_out_h, -x.at(cold_in_m)});
                return x.at(hot_in_m) * hot_delta -
                       x.at(cold_in_m) * cold_delta;
            },
            1.0e7);
        system.add_checked_equation(
            prefix + "counterflow_heat_transfer",
            [hot_properties, cold_properties, conductance,
             hot_in_m, hot_in_p, hot_in_h, hot_out_p, hot_out_h,
             cold_in_p, cold_in_h, cold_out_p, cold_out_h](
                const std::vector<double>& x, double& residual) {
                const auto hot_in = hot_properties->state_ph(
                    x.at(hot_in_p), x.at(hot_in_h));
                if (!hot_in.ok()) return property_failure(hot_in);
                const auto hot_out = hot_properties->state_ph(
                    x.at(hot_out_p), x.at(hot_out_h));
                if (!hot_out.ok()) return property_failure(hot_out);
                const auto cold_in = cold_properties->state_ph(
                    x.at(cold_in_p), x.at(cold_in_h));
                if (!cold_in.ok()) return property_failure(cold_in);
                const auto cold_out = cold_properties->state_ph(
                    x.at(cold_out_p), x.at(cold_out_h));
                if (!cold_out.ok()) return property_failure(cold_out);
                double lmtd = 0.0;
                const auto status = log_mean_temperature_difference(
                    hot_in.state.temperature_k -
                        cold_out.state.temperature_k,
                    hot_out.state.temperature_k -
                        cold_in.state.temperature_k,
                    lmtd);
                if (!status.ok()) return status;
                residual =
                    x.at(hot_in_m) *
                        (x.at(hot_in_h) - x.at(hot_out_h)) -
                    conductance * lmtd;
                return EvaluationStatus::success();
            },
            1.0e7);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class FixedOutletQualityPhaseChangeModel final : public ComponentModel {
public:
    FixedOutletQualityPhaseChangeModel(
        std::string kind, bool evaporator)
        : descriptor_(make_descriptor(
              std::move(kind),
              {{"inlet", "fluid", "in"},
               {"outlet", "fluid", "out"},
               {"heat", "heat", evaporator ? "in" : "out"}})),
          evaporator_(evaporator) {
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(const ComponentCompileContext& context,
                       EquationSystemBuilder& system) const override {
        const double target_quality =
            required_parameter(context.component, "outlet_quality");
        if (!std::isfinite(target_quality) ||
            target_quality <= 0.0 || target_quality >= 1.0) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' parameter 'outlet_quality' must be finite and strictly between 0 and 1");
        }
        const double pressure_loss = optional_parameter(
            context.component, "pressure_loss_fraction", 0.0);
        if (!std::isfinite(pressure_loss) ||
            pressure_loss < 0.0 || pressure_loss >= 1.0) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' parameter 'pressure_loss_fraction' must be finite and in [0, 1)");
        }
        const auto properties =
            require_property_package(context, "inlet");
        if (properties != require_property_package(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' inlet and outlet must use the same medium");
        }

        const auto inlet_m =
            require_port_variable(context, "inlet.m_dot");
        const auto inlet_p =
            require_port_variable(context, "inlet.p");
        const auto inlet_h =
            require_port_variable(context, "inlet.h");
        const auto outlet_m =
            require_port_variable(context, "outlet.m_dot");
        const auto outlet_p =
            require_port_variable(context, "outlet.p");
        const auto outlet_h =
            require_port_variable(context, "outlet.h");
        const auto heat_flow =
            require_port_variable(context, "heat.Q_dot");
        const auto heat_temperature =
            require_port_variable(context, "heat.T");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_continuity",
            {{outlet_m, 1.0}, {inlet_m, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "pressure_loss",
            {{outlet_p, 1.0},
             {inlet_p, -(1.0 - pressure_loss)}},
            0.0, 100000.0);
        system.add_checked_equation(
            prefix + "outlet_quality",
            [properties, outlet_p, outlet_h, target_quality](
                const std::vector<double>& x, double& residual) {
                const auto outlet = properties->state_ph(
                    x.at(outlet_p), x.at(outlet_h));
                if (!outlet.ok()) return property_failure(outlet);
                if (outlet.state.phase != physics::Phase::two_phase) {
                    return EvaluationStatus::recoverable(
                        "phase-change outlet must remain in the two-phase region");
                }
                residual =
                    outlet.state.vapor_quality - target_quality;
                return EvaluationStatus::success();
            },
            1.0);
        system.add_sparse_equation(
            prefix + "heat_balance",
            {heat_flow, inlet_m, inlet_h, outlet_h},
            [evaporator = evaporator_, heat_flow, inlet_m,
             inlet_h, outlet_h](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                const double direction = evaporator ? 1.0 : -1.0;
                const double delta_h =
                    x.at(outlet_h) - x.at(inlet_h);
                jacobian.push_back({heat_flow, 1.0});
                jacobian.push_back(
                    {inlet_m, -direction * delta_h});
                jacobian.push_back(
                    {inlet_h, direction * x.at(inlet_m)});
                jacobian.push_back(
                    {outlet_h, -direction * x.at(inlet_m)});
                return x.at(heat_flow) -
                       direction * x.at(inlet_m) * delta_h;
            },
            1.0e7);
        system.add_checked_equation(
            prefix + "heat_temperature",
            [properties, outlet_p, outlet_h, heat_temperature](
                const std::vector<double>& x, double& residual) {
                const auto outlet = properties->state_ph(
                    x.at(outlet_p), x.at(outlet_h));
                if (!outlet.ok()) return property_failure(outlet);
                residual = x.at(heat_temperature) -
                           outlet.state.temperature_k;
                return EvaluationStatus::success();
            },
            100.0);
    }

private:
    ComponentModelDescriptor descriptor_;
    bool evaporator_{false};
};

class RigidAdiabaticFluidVolumeModel final : public ComponentModel {
public:
    RigidAdiabaticFluidVolumeModel()
        : descriptor_(make_descriptor(
              "volume.fluid.rigid_adiabatic",
              {{"inlet", "fluid", "in"},
               {"outlet", "fluid", "out"}})) {
        descriptor_.supports_steady = false;
        descriptor_.supports_transient = true;
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph};
        descriptor_.internal_variables = {
            {"mass", DaeVariableKind::differential,
             1.0, 10.0, 0.0, 1.0, 1.0e-12,
             std::numeric_limits<double>::infinity()},
            {"total_energy", DaeVariableKind::differential,
             200000.0, 1.0e6, 0.0, 1.0e5,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity()},
            {"pressure", DaeVariableKind::algebraic,
             101325.0, 100000.0, 0.0, 100000.0, 1.0,
             std::numeric_limits<double>::infinity()},
            {"enthalpy", DaeVariableKind::algebraic,
             300000.0, 100000.0, 0.0, 100000.0,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity()}};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(const ComponentCompileContext&,
                       EquationSystemBuilder&) const override {
        throw std::logic_error(
            "rigid fluid volume is a transient-only component");
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        const double volume =
            required_parameter(context.component, "volume");
        validate_positive(context.component, "volume", volume);
        const auto properties =
            require_property_package(context, "inlet");
        if (properties != require_property_package(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' inlet and outlet must use the same medium");
        }

        const auto inlet_m =
            require_port_variable(context, "inlet.m_dot");
        const auto inlet_h =
            require_port_variable(context, "inlet.h");
        const auto outlet_m =
            require_port_variable(context, "outlet.m_dot");
        const auto outlet_p =
            require_port_variable(context, "outlet.p");
        const auto outlet_h =
            require_port_variable(context, "outlet.h");
        const auto mass =
            require_internal_variable(context, "mass");
        const auto energy =
            require_internal_variable(context, "total_energy");
        const auto pressure =
            require_internal_variable(context, "pressure");
        const auto enthalpy =
            require_internal_variable(context, "enthalpy");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_accumulation",
            {{mass, 0.0, 1.0}, {inlet_m, -1.0, 0.0},
             {outlet_m, 1.0, 0.0}},
            0.0, 100.0);
        system.add_sparse_equation(
            prefix + "energy_accumulation",
            {energy, inlet_m, inlet_h, outlet_m, outlet_h},
            [energy, inlet_m, inlet_h, outlet_m, outlet_h](
                double, const std::vector<double>& x,
                const std::vector<double>& x_dot, double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                residual = x_dot.at(energy) -
                           x.at(inlet_m) * x.at(inlet_h) +
                           x.at(outlet_m) * x.at(outlet_h);
                jacobian.push_back({energy, 0.0, 1.0});
                jacobian.push_back(
                    {inlet_m, -x.at(inlet_h), 0.0});
                jacobian.push_back(
                    {inlet_h, -x.at(inlet_m), 0.0});
                jacobian.push_back(
                    {outlet_m, x.at(outlet_h), 0.0});
                jacobian.push_back(
                    {outlet_h, x.at(outlet_m), 0.0});
                return EvaluationStatus::success();
            },
            1.0e6);
        system.add_linear_equation(
            prefix + "outlet_pressure",
            {{outlet_p, 1.0, 0.0}, {pressure, -1.0, 0.0}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "outlet_enthalpy",
            {{outlet_h, 1.0, 0.0}, {enthalpy, -1.0, 0.0}},
            0.0, 100000.0);

        system.add_sparse_equation(
            prefix + "volume_closure",
            {mass, pressure, enthalpy},
            [properties, volume, mass, pressure, enthalpy](
                double, const std::vector<double>& x,
                const std::vector<double>&, double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                const auto state = properties->state_ph(
                    x.at(pressure), x.at(enthalpy));
                if (!state.ok()) return property_failure(state);
                double drho_dp = 0.0;
                double drho_dh = 0.0;
                auto status = property_partial(
                    *properties, x.at(pressure), x.at(enthalpy),
                    state.state.density_kg_m3, true,
                    [](const physics::ThermodynamicState& value) {
                        return value.density_kg_m3;
                    },
                    drho_dp);
                if (!status.ok()) return status;
                status = property_partial(
                    *properties, x.at(pressure), x.at(enthalpy),
                    state.state.density_kg_m3, false,
                    [](const physics::ThermodynamicState& value) {
                        return value.density_kg_m3;
                    },
                    drho_dh);
                if (!status.ok()) return status;
                residual = x.at(mass) -
                           volume * state.state.density_kg_m3;
                jacobian.push_back({mass, 1.0, 0.0});
                jacobian.push_back(
                    {pressure, -volume * drho_dp, 0.0});
                jacobian.push_back(
                    {enthalpy, -volume * drho_dh, 0.0});
                return EvaluationStatus::success();
            },
            10.0);
        system.add_sparse_equation(
            prefix + "energy_closure",
            {energy, mass, pressure, enthalpy},
            [properties, energy, mass, pressure, enthalpy](
                double, const std::vector<double>& x,
                const std::vector<double>&, double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                const auto state = properties->state_ph(
                    x.at(pressure), x.at(enthalpy));
                if (!state.ok()) return property_failure(state);
                double du_dp = 0.0;
                double du_dh = 0.0;
                auto status = property_partial(
                    *properties, x.at(pressure), x.at(enthalpy),
                    state.state.internal_energy_j_kg, true,
                    [](const physics::ThermodynamicState& value) {
                        return value.internal_energy_j_kg;
                    },
                    du_dp);
                if (!status.ok()) return status;
                status = property_partial(
                    *properties, x.at(pressure), x.at(enthalpy),
                    state.state.internal_energy_j_kg, false,
                    [](const physics::ThermodynamicState& value) {
                        return value.internal_energy_j_kg;
                    },
                    du_dh);
                if (!status.ok()) return status;
                residual = x.at(energy) -
                           x.at(mass) *
                               state.state.internal_energy_j_kg;
                jacobian.push_back({energy, 1.0, 0.0});
                jacobian.push_back(
                    {mass, -state.state.internal_energy_j_kg, 0.0});
                jacobian.push_back(
                    {pressure, -x.at(mass) * du_dp, 0.0});
                jacobian.push_back(
                    {enthalpy, -x.at(mass) * du_dh, 0.0});
                return EvaluationStatus::success();
            },
            1.0e6);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class LumpedThermalStorageModel final : public ComponentModel {
public:
    LumpedThermalStorageModel()
        : descriptor_(make_descriptor(
              "storage.thermal.lumped",
              {{"thermal", "heat", "bidirectional"}})) {
        descriptor_.supports_steady = false;
        descriptor_.supports_transient = true;
        descriptor_.internal_variables.push_back(
            {"temperature", DaeVariableKind::differential,
             300.0, 100.0, 0.0, 1.0, 0.0,
             std::numeric_limits<double>::infinity()});
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(const ComponentCompileContext&,
                       EquationSystemBuilder&) const override {
        throw std::logic_error(
            "lumped thermal storage is a transient-only component");
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        const double capacity =
            required_parameter(context.component, "thermal_capacity");
        validate_positive(context.component, "thermal_capacity", capacity);
        const auto heat_flow =
            require_port_variable(context, "thermal.Q_dot");
        const auto port_temperature =
            require_port_variable(context, "thermal.T");
        const auto temperature =
            require_internal_variable(context, "temperature");
        system.add_linear_equation(
            "component." + context.component.id +
                ".surface_temperature",
            {{port_temperature, 1.0, 0.0},
             {temperature, -1.0, 0.0}},
            0.0, 100.0);
        system.add_linear_equation(
            "component." + context.component.id + ".energy_accumulation",
            {{temperature, 0.0, capacity},
             {heat_flow, -1.0, 0.0}},
            0.0,
            std::max(capacity, 1.0));
    }

private:
    ComponentModelDescriptor descriptor_;
};

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

ComponentRegistry make_default_component_registry() {
    ComponentRegistry registry;
    auto fluid_source = make_descriptor(
        "source.fluid.boundary", {{"outlet", "fluid", "out"}});
    fluid_source.supports_transient = true;
    registry.register_model(std::make_shared<MetadataComponentModel>(
        std::move(fluid_source)));
    auto fluid_sink = make_descriptor(
        "sink.fluid.boundary", {{"inlet", "fluid", "in"}});
    fluid_sink.supports_transient = true;
    registry.register_model(std::make_shared<MetadataComponentModel>(
        std::move(fluid_sink)));
    auto heat_source = make_descriptor(
        "source.heat.boundary", {{"outlet", "heat", "out"}});
    heat_source.supports_transient = true;
    registry.register_model(
        std::make_shared<MetadataComponentModel>(
            std::move(heat_source)));
    auto heat_sink = make_descriptor(
        "sink.heat.boundary", {{"inlet", "heat", "in"}});
    heat_sink.supports_transient = true;
    registry.register_model(
        std::make_shared<MetadataComponentModel>(
            std::move(heat_sink)));
    registry.register_model(std::make_shared<PropertyCompressorModel>(
        "compressor.gas.isentropic_efficiency"));
    registry.register_model(std::make_shared<PropertyCompressorModel>(
        "compressor.fluid.isentropic_efficiency"));
    registry.register_model(std::make_shared<PropertyTurbineModel>(
        "turbine.gas.isentropic_efficiency"));
    registry.register_model(std::make_shared<PropertyTurbineModel>(
        "turbine.fluid.isentropic_efficiency"));
    registry.register_model(std::make_shared<PropertyCompressorModel>(
        "pump.fluid.isentropic_efficiency"));
    registry.register_model(
        std::make_shared<TwoInletFluidMixerModel>());
    registry.register_model(
        std::make_shared<TwoOutletFluidSplitterModel>());
    registry.register_model(
        std::make_shared<IsenthalpicPressureRatioValveModel>());
    registry.register_model(
        std::make_shared<FixedDutyHeatExchangerModel>());
    registry.register_model(
        std::make_shared<CounterflowUaHeatExchangerModel>());
    registry.register_model(
        std::make_shared<FixedOutletQualityPhaseChangeModel>(
            "evaporator.fluid.fixed_outlet_quality", true));
    registry.register_model(
        std::make_shared<FixedOutletQualityPhaseChangeModel>(
            "condenser.fluid.fixed_outlet_quality", false));
    registry.register_model(
        std::make_shared<RigidAdiabaticFluidVolumeModel>());
    registry.register_model(std::make_shared<LumpedThermalStorageModel>());
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
