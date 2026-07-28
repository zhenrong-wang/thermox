#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <cmath>
#include <limits>
#include <memory>

namespace thermox::platform {

namespace {

using component_model_support::parameter_or;
using component_model_support::require_port_variable;
using component_model_support::required_parameter;

class TwoLoadShaftTrainModel final : public ComponentModel {
public:
    TwoLoadShaftTrainModel() {
        descriptor_.kind = "shaft.train.two_load";
        descriptor_.version = "1.0.0";
        descriptor_.ports = {
            {"driver", "shaft", "in"},
            {"load_1", "shaft", "out"},
            {"load_2", "shaft", "out"},
        };
        descriptor_.parameters = {
            {"mechanical_efficiency", "dimensionless", true,
             std::nullopt, 0.0, 1.0, false, true},
            {"fixed_loss", "power", false, 0.0, 0.0,
             std::numeric_limits<double>::infinity(), true, true},
        };
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const double efficiency = required_parameter(
            context.component, "mechanical_efficiency");
        const double fixed_loss = parameter_or(
            context.component, "fixed_loss", 0.0);
        const auto driver_power =
            require_port_variable(context, "driver.W_dot");
        const auto driver_speed =
            require_port_variable(context, "driver.omega");
        const auto load_1_power =
            require_port_variable(context, "load_1.W_dot");
        const auto load_1_speed =
            require_port_variable(context, "load_1.omega");
        const auto load_2_power =
            require_port_variable(context, "load_2.W_dot");
        const auto load_2_speed =
            require_port_variable(context, "load_2.omega");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "load_1_speed",
            {{driver_speed, 1.0}, {load_1_speed, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "load_2_speed",
            {{driver_speed, 1.0}, {load_2_speed, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "power_balance",
            {
                {driver_power, efficiency},
                {load_1_power, -1.0},
                {load_2_power, -1.0},
            },
            fixed_loss, 1.0e6);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class GeneratorModel final : public ComponentModel {
public:
    GeneratorModel() {
        descriptor_.kind = "generator.electrical.efficiency";
        descriptor_.version = "1.0.0";
        descriptor_.ports = {
            {"shaft", "shaft", "in"},
            {"electrical", "electrical", "out"},
        };
        descriptor_.parameters = {
            {"generator_efficiency", "dimensionless", true,
             std::nullopt, 0.0, 1.0, false, true},
            {"pole_pairs", "dimensionless", true, std::nullopt,
             0.0, std::numeric_limits<double>::infinity(), false,
             true},
        };
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const double efficiency = required_parameter(
            context.component, "generator_efficiency");
        const double pole_pairs = required_parameter(
            context.component, "pole_pairs");
        const auto shaft_power =
            require_port_variable(context, "shaft.W_dot");
        const auto shaft_speed =
            require_port_variable(context, "shaft.omega");
        const auto electrical_power =
            require_port_variable(context, "electrical.P");
        const auto frequency =
            require_port_variable(
                context, "electrical.frequency");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "electrical_power",
            {
                {electrical_power, 1.0},
                {shaft_power, -efficiency},
            },
            0.0, 1.0e6);
        system.add_linear_equation(
            prefix + "electrical_frequency",
            {
                {frequency, 2.0 * std::acos(-1.0)},
                {shaft_speed, -pole_pairs},
            },
            0.0, 50.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

}  // namespace

void register_power_component_models(ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<TwoLoadShaftTrainModel>());
    registry.register_model(std::make_shared<GeneratorModel>());
}

}  // namespace thermox::platform
