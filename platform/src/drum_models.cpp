#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace thermox::platform {

namespace {

using component_model_support::property_failure;
using component_model_support::require_internal_variable;
using component_model_support::require_port_variable;
using component_model_support::require_property_package;
using component_model_support::required_parameter;

struct SaturationPressureDerivatives {
    physics::SaturationResult state;
    double liquid_density{0.0};
    double vapor_density{0.0};
    double liquid_internal_energy{0.0};
    double vapor_internal_energy{0.0};
    double liquid_enthalpy{0.0};
    double vapor_enthalpy{0.0};
    double temperature{0.0};
};

EvaluationStatus saturation_with_pressure_derivatives(
    const physics::PropertyPackage& properties,
    double pressure_pa,
    SaturationPressureDerivatives& output) {
    output.state = properties.saturation_p(pressure_pa);
    if (!output.state.ok()) return property_failure(output.state);
    const double step =
        std::max(10.0, std::abs(pressure_pa) * 1.0e-6);
    const auto plus = properties.saturation_p(pressure_pa + step);
    const auto minus = properties.saturation_p(pressure_pa - step);
    const physics::SaturationResult* first = nullptr;
    const physics::SaturationResult* second = nullptr;
    double denominator = 0.0;
    if (plus.ok() && minus.ok()) {
        first = &plus;
        second = &minus;
        denominator = 2.0 * step;
    } else if (plus.ok()) {
        first = &plus;
        second = &output.state;
        denominator = step;
    } else if (minus.ok()) {
        first = &output.state;
        second = &minus;
        denominator = step;
    } else {
        return EvaluationStatus::recoverable(
            "could not evaluate saturation pressure derivatives for "
            "equilibrium drum");
    }
    const auto derivative = [first, second, denominator](
        auto member, bool vapor) {
        const auto& first_phase =
            vapor ? first->vapor : first->liquid;
        const auto& second_phase =
            vapor ? second->vapor : second->liquid;
        return (first_phase.*member - second_phase.*member) /
            denominator;
    };
    output.liquid_density = derivative(
        &physics::ThermodynamicState::density_kg_m3, false);
    output.vapor_density = derivative(
        &physics::ThermodynamicState::density_kg_m3, true);
    output.liquid_internal_energy = derivative(
        &physics::ThermodynamicState::internal_energy_j_kg, false);
    output.vapor_internal_energy = derivative(
        &physics::ThermodynamicState::internal_energy_j_kg, true);
    output.liquid_enthalpy = derivative(
        &physics::ThermodynamicState::enthalpy_j_kg, false);
    output.vapor_enthalpy = derivative(
        &physics::ThermodynamicState::enthalpy_j_kg, true);
    output.temperature = derivative(
        &physics::ThermodynamicState::temperature_k, false);
    return EvaluationStatus::success();
}

class EquilibriumTwoPhaseDrumModel final : public ComponentModel {
public:
    EquilibriumTwoPhaseDrumModel() {
        descriptor_.kind = "drum.fluid.equilibrium_two_phase";
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "drum.fluid";
        descriptor_.display_name = "Two-phase equilibrium drum";
        descriptor_.category = "Fluid inventory";
        descriptor_.model_name = "Rigid equilibrium inventory";
        descriptor_.ports = {
            {"inlet", "fluid", "in"},
            {"vapor_outlet", "fluid", "out"},
            {"liquid_outlet", "fluid", "out"},
            {"heat", "heat", "in"},
        };
        descriptor_.parameters = {
            {"volume", "volume", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"vessel_height", "length", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
        };
        descriptor_.supports_steady = false;
        descriptor_.supports_transient = true;
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::saturation_p};
        descriptor_.internal_variables = {
            {"total_mass", DaeVariableKind::differential,
             10.0, 10.0, 0.0, 1.0, 1.0e-12,
             std::numeric_limits<double>::infinity(), "mass"},
            {"total_internal_energy", DaeVariableKind::differential,
             1.0e7, 1.0e7, 0.0, 1.0e6, 0.0,
             std::numeric_limits<double>::infinity(), "energy"},
            {"pressure", DaeVariableKind::algebraic,
             2.0e5, 1.0e5, 0.0, 1.0e5, 1.0,
             std::numeric_limits<double>::infinity(), "pressure"},
            {"vapor_quality", DaeVariableKind::algebraic,
             0.1, 1.0, 0.0, 1.0, 0.0, 1.0, "dimensionless"},
            {"liquid_level", DaeVariableKind::algebraic,
             0.1, 1.0, 0.0, 1.0, 0.0,
             std::numeric_limits<double>::infinity(), "length"},
        };
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext&,
        EquationSystemBuilder&) const override {
        throw std::logic_error(
            "two-phase equilibrium drum is a transient-only component");
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        const double volume =
            required_parameter(context.component, "volume");
        const double vessel_height =
            required_parameter(context.component, "vessel_height");
        const auto properties =
            require_property_package(context, "inlet");
        if (properties !=
                require_property_package(context, "vapor_outlet") ||
            properties !=
                require_property_package(context, "liquid_outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' drum fluid ports must use the same medium");
        }
        const auto inlet_m =
            require_port_variable(context, "inlet.m_dot");
        const auto inlet_h =
            require_port_variable(context, "inlet.h");
        const auto vapor_m =
            require_port_variable(context, "vapor_outlet.m_dot");
        const auto vapor_p =
            require_port_variable(context, "vapor_outlet.p");
        const auto vapor_h =
            require_port_variable(context, "vapor_outlet.h");
        const auto liquid_m =
            require_port_variable(context, "liquid_outlet.m_dot");
        const auto liquid_p =
            require_port_variable(context, "liquid_outlet.p");
        const auto liquid_h =
            require_port_variable(context, "liquid_outlet.h");
        const auto heat_flow =
            require_port_variable(context, "heat.Q_dot");
        const auto heat_temperature =
            require_port_variable(context, "heat.T");
        const auto mass =
            require_internal_variable(context, "total_mass");
        const auto energy = require_internal_variable(
            context, "total_internal_energy");
        const auto pressure =
            require_internal_variable(context, "pressure");
        const auto quality =
            require_internal_variable(context, "vapor_quality");
        const auto level =
            require_internal_variable(context, "liquid_level");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_accumulation",
            {{mass, 0.0, 1.0},
             {inlet_m, -1.0, 0.0},
             {vapor_m, 1.0, 0.0},
             {liquid_m, 1.0, 0.0}},
            0.0, 100.0);
        system.add_sparse_equation(
            prefix + "energy_accumulation",
            {energy, inlet_m, inlet_h, vapor_m, vapor_h,
             liquid_m, liquid_h, heat_flow},
            [energy, inlet_m, inlet_h, vapor_m, vapor_h,
             liquid_m, liquid_h, heat_flow](
                double, const std::vector<double>& x,
                const std::vector<double>& x_dot,
                double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                residual = x_dot.at(energy) -
                    x.at(inlet_m) * x.at(inlet_h) +
                    x.at(vapor_m) * x.at(vapor_h) +
                    x.at(liquid_m) * x.at(liquid_h) -
                    x.at(heat_flow);
                jacobian.push_back({energy, 0.0, 1.0});
                jacobian.push_back(
                    {inlet_m, -x.at(inlet_h), 0.0});
                jacobian.push_back(
                    {inlet_h, -x.at(inlet_m), 0.0});
                jacobian.push_back(
                    {vapor_m, x.at(vapor_h), 0.0});
                jacobian.push_back(
                    {vapor_h, x.at(vapor_m), 0.0});
                jacobian.push_back(
                    {liquid_m, x.at(liquid_h), 0.0});
                jacobian.push_back(
                    {liquid_h, x.at(liquid_m), 0.0});
                jacobian.push_back({heat_flow, -1.0, 0.0});
                return EvaluationStatus::success();
            },
            1.0e7);
        system.add_linear_equation(
            prefix + "vapor_pressure",
            {{vapor_p, 1.0, 0.0}, {pressure, -1.0, 0.0}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "liquid_pressure",
            {{liquid_p, 1.0, 0.0}, {pressure, -1.0, 0.0}},
            0.0, 100000.0);

        const auto add_saturation_output =
            [&](const std::string& name,
                std::size_t output_variable,
                bool vapor,
                bool temperature) {
                system.add_sparse_equation(
                    prefix + name,
                    {pressure, output_variable},
                    [properties, pressure, output_variable,
                     vapor, temperature](
                        double, const std::vector<double>& x,
                        const std::vector<double>&,
                        double& residual,
                        std::vector<DaeEquationPartial>& jacobian) {
                        SaturationPressureDerivatives saturation;
                        const auto status =
                            saturation_with_pressure_derivatives(
                                *properties, x.at(pressure), saturation);
                        if (!status.ok()) return status;
                        const double target = temperature
                            ? saturation.state.liquid.temperature_k
                            : (vapor
                                ? saturation.state.vapor.enthalpy_j_kg
                                : saturation.state.liquid.enthalpy_j_kg);
                        const double derivative = temperature
                            ? saturation.temperature
                            : (vapor
                                ? saturation.vapor_enthalpy
                                : saturation.liquid_enthalpy);
                        residual = x.at(output_variable) - target;
                        jacobian.push_back(
                            {pressure, -derivative, 0.0});
                        jacobian.push_back(
                            {output_variable, 1.0, 0.0});
                        return EvaluationStatus::success();
                    },
                    temperature ? 100.0 : 100000.0);
            };
        add_saturation_output(
            "saturated_vapor_enthalpy", vapor_h, true, false);
        add_saturation_output(
            "saturated_liquid_enthalpy", liquid_h, false, false);
        add_saturation_output(
            "heat_boundary_temperature", heat_temperature, false, true);

        system.add_sparse_equation(
            prefix + "rigid_volume_closure",
            {mass, pressure, quality},
            [properties, volume, mass, pressure, quality](
                double, const std::vector<double>& x,
                const std::vector<double>&,
                double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                SaturationPressureDerivatives saturation;
                const auto status =
                    saturation_with_pressure_derivatives(
                        *properties, x.at(pressure), saturation);
                if (!status.ok()) return status;
                const double rho_l =
                    saturation.state.liquid.density_kg_m3;
                const double rho_v =
                    saturation.state.vapor.density_kg_m3;
                if (rho_l <= 0.0 || rho_v <= 0.0) {
                    return EvaluationStatus::recoverable(
                        "equilibrium drum requires positive saturated "
                        "phase densities");
                }
                const double v_l = 1.0 / rho_l;
                const double v_v = 1.0 / rho_v;
                const double x_vapor = x.at(quality);
                const double mixture_volume =
                    (1.0 - x_vapor) * v_l + x_vapor * v_v;
                const double dv_l_dp =
                    -saturation.liquid_density / (rho_l * rho_l);
                const double dv_v_dp =
                    -saturation.vapor_density / (rho_v * rho_v);
                residual =
                    x.at(mass) * mixture_volume - volume;
                jacobian.push_back({mass, mixture_volume, 0.0});
                jacobian.push_back({
                    pressure,
                    x.at(mass) *
                        ((1.0 - x_vapor) * dv_l_dp +
                         x_vapor * dv_v_dp),
                    0.0});
                jacobian.push_back(
                    {quality, x.at(mass) * (v_v - v_l), 0.0});
                return EvaluationStatus::success();
            },
            volume);
        system.add_sparse_equation(
            prefix + "internal_energy_closure",
            {energy, mass, pressure, quality},
            [properties, energy, mass, pressure, quality](
                double, const std::vector<double>& x,
                const std::vector<double>&,
                double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                SaturationPressureDerivatives saturation;
                const auto status =
                    saturation_with_pressure_derivatives(
                        *properties, x.at(pressure), saturation);
                if (!status.ok()) return status;
                const double u_l = saturation.state.liquid
                    .internal_energy_j_kg;
                const double u_v = saturation.state.vapor
                    .internal_energy_j_kg;
                const double x_vapor = x.at(quality);
                const double mixture_energy =
                    (1.0 - x_vapor) * u_l + x_vapor * u_v;
                residual = x.at(energy) -
                    x.at(mass) * mixture_energy;
                jacobian.push_back({energy, 1.0, 0.0});
                jacobian.push_back({mass, -mixture_energy, 0.0});
                jacobian.push_back({
                    pressure,
                    -x.at(mass) *
                        ((1.0 - x_vapor) *
                             saturation.liquid_internal_energy +
                         x_vapor *
                             saturation.vapor_internal_energy),
                    0.0});
                jacobian.push_back(
                    {quality, -x.at(mass) * (u_v - u_l), 0.0});
                return EvaluationStatus::success();
            },
            1.0e7);
        system.add_sparse_equation(
            prefix + "liquid_level",
            {mass, pressure, quality, level},
            [properties, volume, vessel_height, mass, pressure,
             quality, level](
                double, const std::vector<double>& x,
                const std::vector<double>&,
                double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                SaturationPressureDerivatives saturation;
                const auto status =
                    saturation_with_pressure_derivatives(
                        *properties, x.at(pressure), saturation);
                if (!status.ok()) return status;
                const double rho_l =
                    saturation.state.liquid.density_kg_m3;
                const double v_l = 1.0 / rho_l;
                const double dv_l_dp =
                    -saturation.liquid_density / (rho_l * rho_l);
                const double liquid_fraction = 1.0 - x.at(quality);
                const double level_scale = vessel_height / volume;
                residual = x.at(level) - level_scale *
                    x.at(mass) * liquid_fraction * v_l;
                jacobian.push_back({level, 1.0, 0.0});
                jacobian.push_back({
                    mass, -level_scale * liquid_fraction * v_l, 0.0});
                jacobian.push_back({
                    pressure,
                    -level_scale * x.at(mass) * liquid_fraction *
                        dv_l_dp,
                    0.0});
                jacobian.push_back({
                    quality,
                    level_scale * x.at(mass) * v_l,
                    0.0});
                return EvaluationStatus::success();
            },
            vessel_height);
    }

private:
    ComponentModelDescriptor descriptor_;
};

}  // namespace

void register_drum_component_models(ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<EquilibriumTwoPhaseDrumModel>());
}

}  // namespace thermox::platform
