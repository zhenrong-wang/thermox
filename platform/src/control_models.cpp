#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>

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

class BoundedNormalizedPiControllerModel final
    : public ComponentModel {
public:
    BoundedNormalizedPiControllerModel() {
        descriptor_.kind = "control.pi_bounded.normalized";
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "control.pi";
        descriptor_.display_name = "Bounded PI controller";
        descriptor_.category = "Control";
        descriptor_.model_name = "Back-calculation anti-windup";
        descriptor_.ports = {
            {"setpoint", "signal", "in"},
            {"measurement", "signal", "in"},
            {"command", "control", "out"},
        };
        descriptor_.parameters = {
            {"proportional_gain", "dimensionless", true,
             std::nullopt,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(), true, true},
            {"integral_time", "time", true, std::nullopt,
             0.0, std::numeric_limits<double>::infinity(), false, true},
            {"tracking_time", "time", true, std::nullopt,
             0.0, std::numeric_limits<double>::infinity(), false, true},
            {"bias", "dimensionless", false, 0.0,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(), true, true},
            {"lower_limit", "dimensionless", false, 0.0,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(), true, true},
            {"upper_limit", "dimensionless", false, 1.0,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(), true, true},
        };
        descriptor_.supports_steady = false;
        descriptor_.supports_transient = true;
        descriptor_.internal_variables = {
            {"integral_state", DaeVariableKind::differential,
             0.0, 1.0, 0.0, 1.0,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(), "dimensionless"},
        };
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext&,
        EquationSystemBuilder&) const override {
        throw std::logic_error(
            "bounded PI controller is a transient-only component");
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        const double gain = required_parameter(
            context.component, "proportional_gain");
        const double integral_time = required_parameter(
            context.component, "integral_time");
        const double tracking_time = required_parameter(
            context.component, "tracking_time");
        const double bias =
            parameter_or(context.component, "bias", 0.0);
        const double lower =
            parameter_or(context.component, "lower_limit", 0.0);
        const double upper =
            parameter_or(context.component, "upper_limit", 1.0);
        if (lower >= upper) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' lower_limit must be smaller than upper_limit");
        }
        const auto setpoint =
            require_port_variable(context, "setpoint.value");
        const auto measurement =
            require_port_variable(context, "measurement.value");
        const auto command =
            require_port_variable(context, "command.value");
        const auto integral =
            component_model_support::require_internal_variable(
                context, "integral_state");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_sparse_equation(
            prefix + "bounded_command",
            {setpoint, measurement, command, integral},
            [gain, bias, lower, upper, setpoint, measurement,
             command, integral](
                double, const std::vector<double>& x,
                const std::vector<double>&,
                double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                const double error =
                    x.at(setpoint) - x.at(measurement);
                const double unbounded =
                    bias + gain * error + x.at(integral);
                const double bounded =
                    std::clamp(unbounded, lower, upper);
                const double active_derivative =
                    unbounded > lower && unbounded < upper ? 1.0 : 0.0;
                residual = x.at(command) - bounded;
                jacobian.push_back({command, 1.0, 0.0});
                jacobian.push_back(
                    {setpoint, -active_derivative * gain, 0.0});
                jacobian.push_back(
                    {measurement, active_derivative * gain, 0.0});
                jacobian.push_back(
                    {integral, -active_derivative, 0.0});
                return EvaluationStatus::success();
            },
            1.0);
        system.add_sparse_equation(
            prefix + "integral_anti_windup",
            {setpoint, measurement, command, integral},
            [gain, integral_time, tracking_time, bias,
             setpoint, measurement, command, integral](
                double, const std::vector<double>& x,
                const std::vector<double>& x_dot,
                double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                const double error =
                    x.at(setpoint) - x.at(measurement);
                const double unbounded =
                    bias + gain * error + x.at(integral);
                residual = x_dot.at(integral) -
                    gain / integral_time * error -
                    (x.at(command) - unbounded) / tracking_time;
                const double error_coefficient =
                    -gain / integral_time + gain / tracking_time;
                jacobian.push_back(
                    {setpoint, error_coefficient, 0.0});
                jacobian.push_back(
                    {measurement, -error_coefficient, 0.0});
                jacobian.push_back(
                    {command, -1.0 / tracking_time, 0.0});
                jacobian.push_back(
                    {integral, 1.0 / tracking_time, 1.0});
                return EvaluationStatus::success();
            },
            1.0);
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
    registry.register_model(
        std::make_shared<BoundedNormalizedPiControllerModel>());
}

}  // namespace thermox::platform
