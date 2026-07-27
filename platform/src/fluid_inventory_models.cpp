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
using component_model_support::require_internal_variable;
using component_model_support::require_port_variable;
using component_model_support::require_property_package;
using component_model_support::required_parameter;

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

class RigidAdiabaticFluidVolumeModel final
    : public ComponentModel {
public:
    RigidAdiabaticFluidVolumeModel() {
        descriptor_.kind = "volume.fluid.rigid_adiabatic";
        descriptor_.version = "1.0.0";
        descriptor_.ports = {
            {"inlet", "fluid", "in"},
            {"outlet", "fluid", "out"}};
        descriptor_.parameters = {
            {"volume", "volume", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true}};
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

    void add_equations(
        const ComponentCompileContext&,
        EquationSystemBuilder&) const override {
        throw std::logic_error(
            "rigid fluid volume is a transient-only component");
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        const double volume =
            required_parameter(context.component, "volume");
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
                residual =
                    x.at(mass) -
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
                residual =
                    x.at(energy) -
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

}  // namespace

void register_fluid_inventory_component_models(
    ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<RigidAdiabaticFluidVolumeModel>());
}

}  // namespace thermox::platform
