#include "component_modules.hpp"

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

namespace thermox::platform {

namespace {

double required_parameter(
    const ComponentDefinition& component,
    const std::string& name) {
    const auto it = component.parameters.find(name);
    if (it == component.parameters.end()) {
        throw std::invalid_argument(
            "component '" + component.id +
            "' is missing required parameter: " + name);
    }
    return it->second.value_si;
}

std::size_t require_port_variable(
    const ComponentCompileContext& context,
    const std::string& key) {
    const auto it = context.port_variables.find(key);
    if (it == context.port_variables.end()) {
        throw std::logic_error(
            "compiled component variable missing: " +
            context.component.id + "." + key);
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
             std::numeric_limits<double>::infinity()});
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

}  // namespace

void register_storage_component_models(ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<LumpedThermalStorageModel>());
}

}  // namespace thermox::platform
