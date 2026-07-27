#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

namespace thermox::platform {

namespace {

using component_model_support::property_failure;
using component_model_support::require_port_variable;
using component_model_support::require_property_package;
using component_model_support::required_parameter;

ComponentModelDescriptor exchanger_descriptor(std::string kind) {
    ComponentModelDescriptor out;
    out.kind = std::move(kind);
    out.version = "1.0.0";
    out.ports = {
        {"hot_in", "fluid", "in"},
        {"hot_out", "fluid", "out"},
        {"cold_in", "fluid", "in"},
        {"cold_out", "fluid", "out"}};
    return out;
}

double parameter_value(
    const ComponentDefinition& component,
    const ComponentModelDescriptor& descriptor,
    const std::string& name) {
    const auto supplied = component.parameters.find(name);
    if (supplied != component.parameters.end()) {
        return supplied->second.value_si;
    }
    const auto declared = std::find_if(
        descriptor.parameters.begin(), descriptor.parameters.end(),
        [&](const auto& parameter) {
            return parameter.name == name;
        });
    if (declared == descriptor.parameters.end() ||
        !declared->default_value.has_value()) {
        throw std::logic_error(
            "component parameter default missing: " +
            descriptor.kind + "." + name);
    }
    return *declared->default_value;
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

class FixedDutyHeatExchangerModel final : public ComponentModel {
public:
    FixedDutyHeatExchangerModel()
        : descriptor_(exchanger_descriptor(
              "heat_exchanger.fluid.fixed_duty")) {
        descriptor_.parameters = {
            {"heat_duty", "power", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"hot_pressure_loss_fraction", "dimensionless", false,
             0.0, 0.0, 1.0, true, false},
            {"cold_pressure_loss_fraction", "dimensionless", false,
             0.0, 0.0, 1.0, true, false}};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const double duty =
            required_parameter(context.component, "heat_duty");
        const double hot_loss = parameter_value(
            context.component, descriptor_,
            "hot_pressure_loss_fraction");
        const double cold_loss = parameter_value(
            context.component, descriptor_,
            "cold_pressure_loss_fraction");
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
        : descriptor_(exchanger_descriptor(
              "heat_exchanger.fluid.counterflow_ua")) {
        descriptor_.parameters = {
            {"UA", "thermal_conductance", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"hot_pressure_loss_fraction", "dimensionless", false,
             0.0, 0.0, 1.0, true, false},
            {"cold_pressure_loss_fraction", "dimensionless", false,
             0.0, 0.0, 1.0, true, false}};
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const double conductance =
            required_parameter(context.component, "UA");
        const double hot_loss = parameter_value(
            context.component, descriptor_,
            "hot_pressure_loss_fraction");
        const double cold_loss = parameter_value(
            context.component, descriptor_,
            "cold_pressure_loss_fraction");
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
        std::string kind, bool evaporator) : evaporator_(evaporator) {
        descriptor_.kind = std::move(kind);
        descriptor_.version = "1.0.0";
        descriptor_.ports = {
            {"inlet", "fluid", "in"},
            {"outlet", "fluid", "out"},
            {"heat", "heat", evaporator ? "in" : "out"}};
        descriptor_.parameters = {
            {"outlet_quality", "dimensionless", true, std::nullopt,
             0.0, 1.0, false, false},
            {"pressure_loss_fraction", "dimensionless", false,
             0.0, 0.0, 1.0, true, false}};
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const double target_quality =
            required_parameter(context.component, "outlet_quality");
        const double pressure_loss = parameter_value(
            context.component, descriptor_, "pressure_loss_fraction");
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

}  // namespace

void register_heat_transfer_component_models(
    ComponentRegistry& registry) {
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
}

}  // namespace thermox::platform
