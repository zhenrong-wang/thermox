#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace thermox::platform {

namespace {

using component_model_support::parameter_or;
using component_model_support::property_failure;
using component_model_support::require_port_variable;
using component_model_support::require_property_package;
using component_model_support::required_parameter;

struct ValveFlowEvaluation {
    EvaluationStatus status;
    double residual{0.0};
};

ValveFlowEvaluation evaluate_actuated_liquid_valve(
    const physics::PropertyPackage& properties,
    double maximum_effective_area,
    double minimum_opening,
    double mass_flow,
    double inlet_pressure,
    double inlet_enthalpy,
    double outlet_pressure,
    double command) {
    if (!std::isfinite(command) || command < 0.0 || command > 1.0) {
        return {
            EvaluationStatus::recoverable(
                "actuated valve command must be between zero and one")};
    }
    const double pressure_drop = inlet_pressure - outlet_pressure;
    if (!std::isfinite(pressure_drop) || pressure_drop <= 0.0) {
        return {
            EvaluationStatus::recoverable(
                "actuated liquid valve requires inlet pressure above "
                "outlet pressure")};
    }
    const auto inlet =
        properties.state_ph(inlet_pressure, inlet_enthalpy);
    if (!inlet.ok()) return {property_failure(inlet)};
    if (inlet.state.phase != physics::Phase::liquid) {
        return {
            EvaluationStatus::recoverable(
                "actuated non-flashing valve requires a liquid inlet")};
    }
    const auto saturation = properties.saturation_p(outlet_pressure);
    if (!saturation.ok()) return {property_failure(saturation)};
    const double tolerance = 1.0e-8 * std::max(
        std::abs(saturation.liquid.enthalpy_j_kg), 1.0);
    if (inlet_enthalpy >=
        saturation.liquid.enthalpy_j_kg - tolerance) {
        return {
            EvaluationStatus::recoverable(
                "actuated non-flashing valve reaches the outlet "
                "saturation boundary")};
    }
    const double density = inlet.state.density_kg_m3;
    if (!std::isfinite(density) || density <= 0.0) {
        return {
            EvaluationStatus::recoverable(
                "actuated liquid valve requires positive finite density")};
    }
    const double opening =
        minimum_opening + (1.0 - minimum_opening) * command;
    const double capacity = maximum_effective_area * opening *
        std::sqrt(2.0 * density * pressure_drop);
    return {
        EvaluationStatus::success(), mass_flow - capacity};
}

class ActuatedNonflashingLiquidValveModel final
    : public ComponentModel {
public:
    ActuatedNonflashingLiquidValveModel() {
        descriptor_.kind =
            "valve.fluid.actuated_nonflashing_liquid";
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "valve.fluid";
        descriptor_.display_name = "Actuated control valve";
        descriptor_.category = "Fluid control";
        descriptor_.model_name = "Normalized non-flashing liquid";
        descriptor_.ports = {
            {"inlet", "fluid", "in"},
            {"outlet", "fluid", "out"},
            {"command", "control", "in"},
        };
        descriptor_.parameters = {
            {"full_open_diameter", "length", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"discharge_coefficient", "dimensionless", true,
             std::nullopt, 0.0, 1.0, false, true},
            {"minimum_opening", "dimensionless", false, 0.0,
             0.0, 1.0, true, true},
        };
        descriptor_.supports_steady = true;
        descriptor_.supports_transient = true;
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph,
            physics::PropertyCapability::saturation_p};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto data = compile_data(context);
        const std::string prefix =
            "component." + context.component.id + ".";
        system.add_linear_equation(
            prefix + "mass_continuity",
            {{data.outlet_m, 1.0}, {data.inlet_m, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "isenthalpic_throttling",
            {{data.outlet_h, 1.0}, {data.inlet_h, -1.0}},
            0.0, 100000.0);
        const auto evaluate = make_evaluator(data);
        const auto derivatives = derivative_variables(data);
        std::vector<std::size_t> pattern;
        for (const auto& item : derivatives) pattern.push_back(item.first);
        system.add_checked_sparse_equation(
            prefix + "commanded_mass_flow",
            [evaluate](const std::vector<double>& x, double& residual) {
                const auto value = evaluate(x);
                residual = value.residual;
                return value.status;
            },
            std::move(pattern),
            [evaluate, derivatives](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                const auto base = evaluate(x);
                if (!base.status.ok()) {
                    throw std::runtime_error(base.status.message);
                }
                for (const auto& [variable, minimum_step] : derivatives) {
                    const double step = std::max(
                        minimum_step,
                        std::abs(x.at(variable)) * 1.0e-6);
                    auto plus = x;
                    auto minus = x;
                    plus.at(variable) += step;
                    minus.at(variable) -= step;
                    const auto high = evaluate(plus);
                    const auto low = evaluate(minus);
                    double derivative = 0.0;
                    if (high.status.ok() && low.status.ok()) {
                        derivative =
                            (high.residual - low.residual) /
                            (2.0 * step);
                    } else if (high.status.ok()) {
                        derivative =
                            (high.residual - base.residual) / step;
                    } else if (low.status.ok()) {
                        derivative =
                            (base.residual - low.residual) / step;
                    } else {
                        throw std::runtime_error(
                            "could not evaluate actuated-valve derivative");
                    }
                    jacobian.push_back({variable, derivative});
                }
                return base.residual;
            },
            100.0);
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        const auto data = compile_data(context);
        const std::string prefix =
            "component." + context.component.id + ".";
        system.add_linear_equation(
            prefix + "mass_continuity",
            {{data.outlet_m, 1.0, 0.0},
             {data.inlet_m, -1.0, 0.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "isenthalpic_throttling",
            {{data.outlet_h, 1.0, 0.0},
             {data.inlet_h, -1.0, 0.0}},
            0.0, 100000.0);
        const auto evaluate = make_evaluator(data);
        const auto derivatives = derivative_variables(data);
        std::vector<std::size_t> pattern;
        for (const auto& item : derivatives) pattern.push_back(item.first);
        system.add_sparse_equation(
            prefix + "commanded_mass_flow", std::move(pattern),
            [evaluate, derivatives](
                double, const std::vector<double>& x,
                const std::vector<double>&,
                double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                const auto base = evaluate(x);
                if (!base.status.ok()) return base.status;
                residual = base.residual;
                for (const auto& [variable, minimum_step] : derivatives) {
                    const double step = std::max(
                        minimum_step,
                        std::abs(x.at(variable)) * 1.0e-6);
                    auto plus = x;
                    auto minus = x;
                    plus.at(variable) += step;
                    minus.at(variable) -= step;
                    const auto high = evaluate(plus);
                    const auto low = evaluate(minus);
                    double derivative = 0.0;
                    if (high.status.ok() && low.status.ok()) {
                        derivative =
                            (high.residual - low.residual) /
                            (2.0 * step);
                    } else if (high.status.ok()) {
                        derivative =
                            (high.residual - base.residual) / step;
                    } else if (low.status.ok()) {
                        derivative =
                            (base.residual - low.residual) / step;
                    } else {
                        return EvaluationStatus::recoverable(
                            "could not evaluate actuated-valve derivative");
                    }
                    jacobian.push_back({variable, derivative, 0.0});
                }
                return EvaluationStatus::success();
            },
            100.0);
    }

private:
    struct CompileData {
        std::shared_ptr<const physics::PropertyPackage> properties;
        double maximum_effective_area{0.0};
        double minimum_opening{0.0};
        std::size_t inlet_m{0};
        std::size_t inlet_p{0};
        std::size_t inlet_h{0};
        std::size_t outlet_m{0};
        std::size_t outlet_p{0};
        std::size_t outlet_h{0};
        std::size_t command{0};
    };

    CompileData compile_data(
        const ComponentCompileContext& context) const {
        auto properties = require_property_package(context, "inlet");
        if (properties != require_property_package(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' valve fluid ports must use the same medium");
        }
        const double diameter = required_parameter(
            context.component, "full_open_diameter");
        const double coefficient = required_parameter(
            context.component, "discharge_coefficient");
        return {
            std::move(properties),
            coefficient * std::numbers::pi * diameter * diameter / 4.0,
            parameter_or(context.component, "minimum_opening", 0.0),
            require_port_variable(context, "inlet.m_dot"),
            require_port_variable(context, "inlet.p"),
            require_port_variable(context, "inlet.h"),
            require_port_variable(context, "outlet.m_dot"),
            require_port_variable(context, "outlet.p"),
            require_port_variable(context, "outlet.h"),
            require_port_variable(context, "command.value")};
    }

    static std::function<ValveFlowEvaluation(
        const std::vector<double>&)>
    make_evaluator(const CompileData& data) {
        return [data](const std::vector<double>& x) {
            return evaluate_actuated_liquid_valve(
                *data.properties, data.maximum_effective_area,
                data.minimum_opening, x.at(data.inlet_m),
                x.at(data.inlet_p), x.at(data.inlet_h),
                x.at(data.outlet_p), x.at(data.command));
        };
    }

    static std::vector<std::pair<std::size_t, double>>
    derivative_variables(const CompileData& data) {
        return {
            {data.inlet_m, 1.0e-7},
            {data.inlet_p, 0.1},
            {data.inlet_h, 0.1},
            {data.outlet_p, 0.1},
            {data.command, 1.0e-7},
        };
    }

    ComponentModelDescriptor descriptor_;
};

}  // namespace

void register_valve_component_models(ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<ActuatedNonflashingLiquidValveModel>());
}

}  // namespace thermox::platform
