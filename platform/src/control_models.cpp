#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <limits>
#include <memory>

namespace thermox::platform {

namespace {

using component_model_support::parameter_or;
using component_model_support::require_port_variable;
using component_model_support::required_parameter;

class NormalizedProportionalControllerModel final
    : public ComponentModel {
public:
    NormalizedProportionalControllerModel() {
        descriptor_.kind = "control.proportional.normalized";
        descriptor_.version = "1.0.0";
        descriptor_.ports = {
            {"measurement", "signal", "in"},
            {"command", "control", "out"},
        };
        descriptor_.parameters = {
            {"gain", "dimensionless", true, std::nullopt,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(), true, true},
            {"bias", "dimensionless", false, 0.0,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(), true, true},
        };
        descriptor_.supports_steady = true;
        descriptor_.supports_transient = true;
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const double gain =
            required_parameter(context.component, "gain");
        const double bias =
            parameter_or(context.component, "bias", 0.0);
        const auto measurement =
            require_port_variable(
                context, "measurement.value");
        const auto command =
            require_port_variable(context, "command.value");
        system.add_linear_equation(
            "component." + context.component.id +
                ".proportional_command",
            {{command, 1.0}, {measurement, -gain}},
            bias, 1.0);
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        const double gain =
            required_parameter(context.component, "gain");
        const double bias =
            parameter_or(context.component, "bias", 0.0);
        const auto measurement =
            require_port_variable(
                context, "measurement.value");
        const auto command =
            require_port_variable(context, "command.value");
        system.add_linear_equation(
            "component." + context.component.id +
                ".proportional_command",
            {{command, 1.0, 0.0},
             {measurement, -gain, 0.0}},
            bias, 1.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class NormalizedFirstOrderControlLagModel final
    : public ComponentModel {
public:
    NormalizedFirstOrderControlLagModel() {
        descriptor_.kind = "control.first_order_lag.normalized";
        descriptor_.version = "1.0.0";
        descriptor_.ports = {
            {"command", "control", "in"},
            {"response", "control", "out"},
        };
        descriptor_.parameters = {
            {"gain", "dimensionless", false, 1.0,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(), true, true},
            {"time_constant", "time", true, std::nullopt,
             0.0, std::numeric_limits<double>::infinity(),
             false, true},
        };
        descriptor_.supports_steady = true;
        descriptor_.supports_transient = true;
        descriptor_.transient_variables = {
            {"response", "value", DaeVariableKind::differential,
             1.0},
        };
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const double gain =
            parameter_or(context.component, "gain", 1.0);
        const auto command =
            require_port_variable(context, "command.value");
        const auto response =
            require_port_variable(context, "response.value");
        system.add_linear_equation(
            "component." + context.component.id +
                ".steady_response",
            {{response, 1.0}, {command, -gain}},
            0.0, 1.0);
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        const double gain =
            parameter_or(context.component, "gain", 1.0);
        const double time_constant =
            required_parameter(context.component, "time_constant");
        const auto command =
            require_port_variable(context, "command.value");
        const auto response =
            require_port_variable(context, "response.value");
        system.add_linear_equation(
            "component." + context.component.id +
                ".first_order_response",
            {{response, 1.0, time_constant},
             {command, -gain, 0.0}},
            0.0, 1.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

}  // namespace

void register_control_component_models(
    ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<
            NormalizedProportionalControllerModel>());
    registry.register_model(
        std::make_shared<
            NormalizedFirstOrderControlLagModel>());
}

}  // namespace thermox::platform
