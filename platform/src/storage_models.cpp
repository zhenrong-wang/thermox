#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

namespace thermox::platform {

namespace {

using component_model_support::require_internal_variable;
using component_model_support::require_port_variable;
using component_model_support::required_parameter;

class LumpedThermalStorageModel final : public ComponentModel {
public:
    LumpedThermalStorageModel() {
        descriptor_.kind = "storage.thermal.lumped";
        descriptor_.version = "1.0.0";
        descriptor_.ports = {
            {"thermal", "heat", "bidirectional"}};
        descriptor_.parameters = {
            {"thermal_capacity", "thermal_capacity", true,
             std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true}};
        descriptor_.supports_steady = false;
        descriptor_.supports_transient = true;
        descriptor_.internal_variables.push_back(
            {"temperature", DaeVariableKind::differential,
             300.0, 100.0, 0.0, 1.0, 0.0,
             std::numeric_limits<double>::infinity(),
             "temperature"});
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext&,
        EquationSystemBuilder&) const override {
        throw std::logic_error(
            "lumped thermal storage is a transient-only component");
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        const double capacity =
            required_parameter(context.component, "thermal_capacity");
        if (!std::isfinite(capacity) || capacity <= 0.0) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' parameter 'thermal_capacity' must be positive and finite");
        }
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
            "component." + context.component.id +
                ".energy_accumulation",
            {{temperature, 0.0, capacity},
             {heat_flow, -1.0, 0.0}},
            0.0, std::max(capacity, 1.0));
    }

private:
    ComponentModelDescriptor descriptor_;
};

class TwoSidedWallThermalStorageModel final
    : public ComponentModel {
public:
    TwoSidedWallThermalStorageModel() {
        descriptor_.kind = "storage.thermal.wall_two_sided";
        descriptor_.version = "1.0.0";
        descriptor_.ports = {
            {"hot_side", "heat", "in"},
            {"cold_side", "heat", "out"},
        };
        descriptor_.parameters = {
            {"thermal_capacity", "thermal_capacity", true,
             std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true}};
        descriptor_.supports_steady = true;
        descriptor_.supports_transient = true;
        descriptor_.internal_variables.push_back(
            {"temperature", DaeVariableKind::differential,
             300.0, 100.0, 0.0, 1.0, 0.0,
             std::numeric_limits<double>::infinity(),
             "temperature"});
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto hot_flow =
            require_port_variable(context, "hot_side.Q_dot");
        const auto hot_temperature =
            require_port_variable(context, "hot_side.T");
        const auto cold_flow =
            require_port_variable(context, "cold_side.Q_dot");
        const auto cold_temperature =
            require_port_variable(context, "cold_side.T");
        const std::string prefix =
            "component." + context.component.id + ".";
        system.add_linear_equation(
            prefix + "surface_temperature",
            {{hot_temperature, 1.0},
             {cold_temperature, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "steady_energy_balance",
            {{hot_flow, 1.0}, {cold_flow, -1.0}},
            0.0, 1.0e6);
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        const double capacity =
            required_parameter(context.component, "thermal_capacity");
        const auto hot_flow =
            require_port_variable(context, "hot_side.Q_dot");
        const auto hot_temperature =
            require_port_variable(context, "hot_side.T");
        const auto cold_flow =
            require_port_variable(context, "cold_side.Q_dot");
        const auto cold_temperature =
            require_port_variable(context, "cold_side.T");
        const auto temperature =
            require_internal_variable(context, "temperature");
        const std::string prefix =
            "component." + context.component.id + ".";
        system.add_linear_equation(
            prefix + "hot_surface_temperature",
            {{hot_temperature, 1.0, 0.0},
             {temperature, -1.0, 0.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "cold_surface_temperature",
            {{cold_temperature, 1.0, 0.0},
             {temperature, -1.0, 0.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "energy_accumulation",
            {{temperature, 0.0, capacity},
             {hot_flow, -1.0, 0.0},
             {cold_flow, 1.0, 0.0}},
            0.0, std::max(capacity, 1.0));
    }

private:
    ComponentModelDescriptor descriptor_;
};

}  // namespace

void register_storage_component_models(ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<LumpedThermalStorageModel>());
    registry.register_model(
        std::make_shared<TwoSidedWallThermalStorageModel>());
}

}  // namespace thermox::platform
