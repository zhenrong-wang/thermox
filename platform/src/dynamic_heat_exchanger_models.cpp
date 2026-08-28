#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
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

class DynamicHeatExchangerCellModel final : public ComponentModel {
public:
    DynamicHeatExchangerCellModel() {
        descriptor_.kind = "heat_exchanger.fluid.dynamic_cell";
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "heat_exchanger.fluid.cell";
        descriptor_.display_name = "Dynamic heat-exchanger cell";
        descriptor_.category = "Heat transfer";
        descriptor_.model_name =
            "Well-mixed constant-holdup fluids with wall capacitance";
        descriptor_.ports = {
            {"hot_in", "fluid", "in"},
            {"hot_out", "fluid", "out"},
            {"cold_in", "fluid", "in"},
            {"cold_out", "fluid", "out"},
            {"hot_inventory", "inventory", "out"},
            {"cold_inventory", "inventory", "out"}};
        descriptor_.parameters = {
            {"hot_fluid_mass", "mass", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"cold_fluid_mass", "mass", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"wall_thermal_capacity", "thermal_capacity", true,
             std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"hot_side_UA", "thermal_conductance", true,
             std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"cold_side_UA", "thermal_conductance", true,
             std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"hot_flow_diameter", "length", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"cold_flow_diameter", "length", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"hot_loss_coefficient", "dimensionless", true,
             std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"cold_loss_coefficient", "dimensionless", true,
             std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true}};
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph};
        descriptor_.supports_steady = true;
        descriptor_.supports_transient = true;
        descriptor_.internal_variables = {
            {"hot_total_energy", DaeVariableKind::differential,
             300000.0, 1.0e6, 0.0, 1.0e5,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(), "energy"},
            {"hot_enthalpy", DaeVariableKind::algebraic,
             500000.0, 100000.0, 0.0, 100000.0,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(),
             "specific_enthalpy"},
            {"cold_total_energy", DaeVariableKind::differential,
             200000.0, 1.0e6, 0.0, 1.0e5,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(), "energy"},
            {"cold_enthalpy", DaeVariableKind::algebraic,
             300000.0, 100000.0, 0.0, 100000.0,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(),
             "specific_enthalpy"},
            {"wall_temperature", DaeVariableKind::differential,
             350.0, 100.0, 0.0, 10.0, 0.0,
             std::numeric_limits<double>::infinity(), "temperature"}};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto hot_properties = validate_media(context);
        const auto cold_properties =
            require_property_package(context, "cold_in");
        const double hot_ua =
            required_parameter(context.component, "hot_side_UA");
        const double cold_ua =
            required_parameter(context.component, "cold_side_UA");
        const double effective_ua =
            hot_ua * cold_ua / (hot_ua + cold_ua);
        const double hot_fluid_mass = required_parameter(
            context.component, "hot_fluid_mass");
        const double cold_fluid_mass = required_parameter(
            context.component, "cold_fluid_mass");
        const double hot_diameter = required_parameter(
            context.component, "hot_flow_diameter");
        const double cold_diameter = required_parameter(
            context.component, "cold_flow_diameter");
        const double hot_area = std::numbers::pi *
            hot_diameter * hot_diameter / 4.0;
        const double cold_area = std::numbers::pi *
            cold_diameter * cold_diameter / 4.0;
        const double hot_loss = required_parameter(
            context.component, "hot_loss_coefficient");
        const double cold_loss = required_parameter(
            context.component, "cold_loss_coefficient");
        const auto hot_in_m = require_port_variable(context, "hot_in.m_dot");
        const auto hot_in_p = require_port_variable(context, "hot_in.p");
        const auto hot_in_h = require_port_variable(context, "hot_in.h");
        const auto hot_out_m = require_port_variable(context, "hot_out.m_dot");
        const auto hot_out_p = require_port_variable(context, "hot_out.p");
        const auto hot_out_h = require_port_variable(context, "hot_out.h");
        const auto cold_in_m = require_port_variable(context, "cold_in.m_dot");
        const auto cold_in_p = require_port_variable(context, "cold_in.p");
        const auto cold_in_h = require_port_variable(context, "cold_in.h");
        const auto cold_out_m = require_port_variable(context, "cold_out.m_dot");
        const auto cold_out_p = require_port_variable(context, "cold_out.p");
        const auto cold_out_h = require_port_variable(context, "cold_out.h");
        const auto hot_inventory = require_port_variable(
            context, "hot_inventory.mass");
        const auto cold_inventory = require_port_variable(
            context, "cold_inventory.mass");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "hot_mass_continuity",
            {{hot_out_m, 1.0}, {hot_in_m, -1.0}}, 0.0, 100.0);
        system.add_linear_equation(
            prefix + "cold_mass_continuity",
            {{cold_out_m, 1.0}, {cold_in_m, -1.0}}, 0.0, 100.0);
        system.add_linear_equation(
            prefix + "hot_inventory",
            {{hot_inventory, 1.0}}, hot_fluid_mass, 10.0);
        system.add_linear_equation(
            prefix + "cold_inventory",
            {{cold_inventory, 1.0}}, cold_fluid_mass, 10.0);
        add_steady_pressure_loss(
            system, prefix + "hot_pressure_loss", hot_properties,
            hot_in_m, hot_in_p, hot_out_p, hot_out_h,
            hot_loss / (2.0 * hot_area * hot_area));
        add_steady_pressure_loss(
            system, prefix + "cold_pressure_loss", cold_properties,
            cold_in_m, cold_in_p, cold_out_p, cold_out_h,
            cold_loss / (2.0 * cold_area * cold_area));
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
            1.0e6);
        system.add_checked_equation(
            prefix + "mixed_cell_heat_transfer",
            [hot_properties, cold_properties, effective_ua,
             hot_in_m, hot_in_p, hot_in_h, hot_out_h,
             cold_in_p, cold_out_h](
                const std::vector<double>& x, double& residual) {
                const auto hot = hot_properties->state_ph(
                    x.at(hot_in_p), x.at(hot_out_h));
                if (!hot.ok()) return property_failure(hot);
                const auto cold = cold_properties->state_ph(
                    x.at(cold_in_p), x.at(cold_out_h));
                if (!cold.ok()) return property_failure(cold);
                const double difference =
                    hot.state.temperature_k - cold.state.temperature_k;
                if (!std::isfinite(difference) || difference <= 0.0) {
                    return EvaluationStatus::recoverable(
                        "dynamic heat-exchanger cell requires hot bulk "
                        "temperature above cold bulk temperature");
                }
                residual =
                    x.at(hot_in_m) *
                        (x.at(hot_in_h) - x.at(hot_out_h)) -
                    effective_ua * difference;
                return EvaluationStatus::success();
            },
            1.0e6);
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        const auto hot_properties = validate_media(context);
        const auto cold_properties =
            require_property_package(context, "cold_in");
        const double hot_fluid_mass = required_parameter(
            context.component, "hot_fluid_mass");
        const double cold_fluid_mass = required_parameter(
            context.component, "cold_fluid_mass");
        const double wall_capacity = required_parameter(
            context.component, "wall_thermal_capacity");
        const double hot_ua =
            required_parameter(context.component, "hot_side_UA");
        const double cold_ua =
            required_parameter(context.component, "cold_side_UA");
        const double hot_diameter = required_parameter(
            context.component, "hot_flow_diameter");
        const double cold_diameter = required_parameter(
            context.component, "cold_flow_diameter");
        const double hot_area = std::numbers::pi *
            hot_diameter * hot_diameter / 4.0;
        const double cold_area = std::numbers::pi *
            cold_diameter * cold_diameter / 4.0;
        const double hot_loss = required_parameter(
            context.component, "hot_loss_coefficient");
        const double cold_loss = required_parameter(
            context.component, "cold_loss_coefficient");
        const auto hot_in_m = require_port_variable(context, "hot_in.m_dot");
        const auto hot_in_p = require_port_variable(context, "hot_in.p");
        const auto hot_in_h = require_port_variable(context, "hot_in.h");
        const auto hot_out_m = require_port_variable(context, "hot_out.m_dot");
        const auto hot_out_p = require_port_variable(context, "hot_out.p");
        const auto hot_out_h = require_port_variable(context, "hot_out.h");
        const auto cold_in_m = require_port_variable(context, "cold_in.m_dot");
        const auto cold_in_p = require_port_variable(context, "cold_in.p");
        const auto cold_in_h = require_port_variable(context, "cold_in.h");
        const auto cold_out_m = require_port_variable(context, "cold_out.m_dot");
        const auto cold_out_p = require_port_variable(context, "cold_out.p");
        const auto cold_out_h = require_port_variable(context, "cold_out.h");
        const auto hot_inventory = require_port_variable(
            context, "hot_inventory.mass");
        const auto cold_inventory = require_port_variable(
            context, "cold_inventory.mass");
        const auto hot_energy =
            require_internal_variable(context, "hot_total_energy");
        const auto hot_enthalpy =
            require_internal_variable(context, "hot_enthalpy");
        const auto cold_energy =
            require_internal_variable(context, "cold_total_energy");
        const auto cold_enthalpy =
            require_internal_variable(context, "cold_enthalpy");
        const auto wall_temperature =
            require_internal_variable(context, "wall_temperature");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "hot_mass_continuity",
            {{hot_out_m, 1.0, 0.0}, {hot_in_m, -1.0, 0.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "cold_mass_continuity",
            {{cold_out_m, 1.0, 0.0}, {cold_in_m, -1.0, 0.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "hot_inventory",
            {{hot_inventory, 1.0, 0.0}}, hot_fluid_mass, 10.0);
        system.add_linear_equation(
            prefix + "cold_inventory",
            {{cold_inventory, 1.0, 0.0}}, cold_fluid_mass, 10.0);
        add_fluid_energy_equation(
            system, prefix + "hot_energy_accumulation",
            hot_properties, hot_energy, hot_in_m, hot_in_h,
            hot_out_m, hot_in_p, hot_enthalpy,
            wall_temperature, hot_ua);
        add_fluid_energy_equation(
            system, prefix + "cold_energy_accumulation",
            cold_properties, cold_energy, cold_in_m, cold_in_h,
            cold_out_m, cold_in_p, cold_enthalpy,
            wall_temperature, cold_ua);
        add_wall_energy_equation(
            system, prefix + "wall_energy_accumulation",
            hot_properties, cold_properties, hot_in_p,
            hot_enthalpy, cold_in_p, cold_enthalpy,
            wall_temperature, hot_ua, cold_ua, wall_capacity);

        system.add_linear_equation(
            prefix + "hot_outlet_enthalpy",
            {{hot_out_h, 1.0, 0.0}, {hot_enthalpy, -1.0, 0.0}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "cold_outlet_enthalpy",
            {{cold_out_h, 1.0, 0.0}, {cold_enthalpy, -1.0, 0.0}},
            0.0, 100000.0);
        add_transient_pressure_loss(
            system, prefix + "hot_pressure_loss", hot_properties,
            hot_out_m, hot_in_p, hot_enthalpy, hot_out_p,
            hot_loss / (2.0 * hot_area * hot_area));
        add_transient_pressure_loss(
            system, prefix + "cold_pressure_loss", cold_properties,
            cold_out_m, cold_in_p, cold_enthalpy, cold_out_p,
            cold_loss / (2.0 * cold_area * cold_area));
        add_fluid_energy_closure(
            system, prefix + "hot_energy_closure", hot_properties,
            hot_fluid_mass, hot_energy, hot_in_p, hot_enthalpy);
        add_fluid_energy_closure(
            system, prefix + "cold_energy_closure", cold_properties,
            cold_fluid_mass, cold_energy, cold_in_p, cold_enthalpy);
    }

private:
    static void add_steady_pressure_loss(
        EquationSystemBuilder& system,
        const std::string& name,
        std::shared_ptr<const physics::PropertyPackage> properties,
        std::size_t mass_flow,
        std::size_t inlet_pressure,
        std::size_t outlet_pressure,
        std::size_t outlet_enthalpy,
        double loss_scale) {
        system.add_checked_sparse_equation(
            name,
            [properties, mass_flow, inlet_pressure, outlet_pressure,
             outlet_enthalpy, loss_scale](
                const std::vector<double>& x, double& residual) {
                const auto state = properties->state_ph(
                    x.at(inlet_pressure), x.at(outlet_enthalpy));
                if (!state.ok()) return property_failure(state);
                const double density = state.state.density_kg_m3;
                if (!std::isfinite(density) || density <= 0.0) {
                    return EvaluationStatus::recoverable(
                        "heat-exchanger cell pressure loss requires "
                        "positive finite density");
                }
                const double flow = x.at(mass_flow);
                residual = x.at(inlet_pressure) -
                           x.at(outlet_pressure) -
                           loss_scale * flow * std::abs(flow) / density;
                return EvaluationStatus::success();
            },
            {mass_flow, inlet_pressure, outlet_pressure,
             outlet_enthalpy},
            [properties, mass_flow,
             inlet_pressure, outlet_pressure, outlet_enthalpy,
             loss_scale](const std::vector<double>& x,
                         std::vector<EquationPartial>& jacobian) {
                const auto state =
                    physics::state_ph_derivatives_with_fallback(
                        *properties, x.at(inlet_pressure),
                        x.at(outlet_enthalpy));
                if (!state.ok()) throw std::runtime_error(state.message);
                const double density = state.state.density_kg_m3;
                const double flow = x.at(mass_flow);
                const double signed_square = flow * std::abs(flow);
                const double density_factor =
                    loss_scale * signed_square / (density * density);
                jacobian.push_back(
                    {mass_flow,
                     -2.0 * loss_scale * std::abs(flow) / density});
                jacobian.push_back(
                    {inlet_pressure,
                     1.0 + density_factor * state.derivatives
                         .density_wrt_pressure_at_enthalpy});
                jacobian.push_back({outlet_pressure, -1.0});
                jacobian.push_back(
                    {outlet_enthalpy,
                     density_factor * state.derivatives
                         .density_wrt_enthalpy_at_pressure});
                return x.at(inlet_pressure) -
                       x.at(outlet_pressure) -
                       loss_scale * signed_square / density;
            },
            100000.0);
    }

    static void add_transient_pressure_loss(
        DaeEquationSystemBuilder& system,
        const std::string& name,
        std::shared_ptr<const physics::PropertyPackage> properties,
        std::size_t mass_flow,
        std::size_t internal_pressure,
        std::size_t internal_enthalpy,
        std::size_t outlet_pressure,
        double loss_scale) {
        system.add_sparse_equation(
            name,
            {mass_flow, internal_pressure, internal_enthalpy,
             outlet_pressure},
            [properties = std::move(properties), mass_flow,
             internal_pressure, internal_enthalpy, outlet_pressure,
             loss_scale](
                double, const std::vector<double>& x,
                const std::vector<double>&, double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                const auto state =
                    physics::state_ph_derivatives_with_fallback(
                        *properties, x.at(internal_pressure),
                        x.at(internal_enthalpy));
                if (!state.ok()) return property_failure(state);
                const double density = state.state.density_kg_m3;
                if (!std::isfinite(density) || density <= 0.0) {
                    return EvaluationStatus::recoverable(
                        "heat-exchanger cell pressure loss requires "
                        "positive finite density");
                }
                const double flow = x.at(mass_flow);
                const double signed_square = flow * std::abs(flow);
                const double density_factor =
                    loss_scale * signed_square / (density * density);
                residual = x.at(internal_pressure) -
                           x.at(outlet_pressure) -
                           loss_scale * signed_square / density;
                jacobian.push_back(
                    {mass_flow,
                     -2.0 * loss_scale * std::abs(flow) / density,
                     0.0});
                jacobian.push_back(
                    {internal_pressure,
                     1.0 + density_factor * state.derivatives
                         .density_wrt_pressure_at_enthalpy,
                     0.0});
                jacobian.push_back(
                    {internal_enthalpy,
                     density_factor * state.derivatives
                         .density_wrt_enthalpy_at_pressure,
                     0.0});
                jacobian.push_back(
                    {outlet_pressure, -1.0, 0.0});
                return EvaluationStatus::success();
            },
            100000.0);
    }

    std::shared_ptr<const physics::PropertyPackage> validate_media(
        const ComponentCompileContext& context) const {
        const auto hot = require_property_package(context, "hot_in");
        if (hot != require_property_package(context, "hot_out")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' hot-side ports must use the same medium");
        }
        const auto cold = require_property_package(context, "cold_in");
        if (cold != require_property_package(context, "cold_out")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' cold-side ports must use the same medium");
        }
        return hot;
    }

    static void add_fluid_energy_equation(
        DaeEquationSystemBuilder& system,
        const std::string& name,
        std::shared_ptr<const physics::PropertyPackage> properties,
        std::size_t energy,
        std::size_t inlet_m,
        std::size_t inlet_h,
        std::size_t outlet_m,
        std::size_t pressure,
        std::size_t enthalpy,
        std::size_t wall_temperature,
        double conductance) {
        system.add_sparse_equation(
            name,
            {energy, inlet_m, inlet_h, outlet_m, pressure,
             enthalpy, wall_temperature},
            [properties = std::move(properties), energy, inlet_m,
             inlet_h, outlet_m, pressure, enthalpy,
             wall_temperature, conductance](
                double, const std::vector<double>& x,
                const std::vector<double>& x_dot, double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                const auto state =
                    physics::state_ph_derivatives_with_fallback(
                        *properties, x.at(pressure), x.at(enthalpy));
                if (!state.ok()) return property_failure(state);
                const double heat = conductance *
                    (state.state.temperature_k -
                     x.at(wall_temperature));
                residual = x_dot.at(energy) -
                           x.at(inlet_m) * x.at(inlet_h) +
                           x.at(outlet_m) * x.at(enthalpy) +
                           heat;
                jacobian.push_back({energy, 0.0, 1.0});
                jacobian.push_back(
                    {inlet_m, -x.at(inlet_h), 0.0});
                jacobian.push_back(
                    {inlet_h, -x.at(inlet_m), 0.0});
                jacobian.push_back(
                    {outlet_m, x.at(enthalpy), 0.0});
                jacobian.push_back(
                    {pressure,
                     conductance *
                         state.derivatives
                             .temperature_wrt_pressure_at_enthalpy,
                     0.0});
                jacobian.push_back(
                    {enthalpy,
                     x.at(outlet_m) +
                         conductance *
                             state.derivatives
                                 .temperature_wrt_enthalpy_at_pressure,
                     0.0});
                jacobian.push_back(
                    {wall_temperature,
                     -conductance, 0.0});
                return EvaluationStatus::success();
            },
            1.0e6);
    }

    static void add_wall_energy_equation(
        DaeEquationSystemBuilder& system,
        const std::string& name,
        std::shared_ptr<const physics::PropertyPackage> hot_properties,
        std::shared_ptr<const physics::PropertyPackage> cold_properties,
        std::size_t hot_pressure,
        std::size_t hot_enthalpy,
        std::size_t cold_pressure,
        std::size_t cold_enthalpy,
        std::size_t wall_temperature,
        double hot_ua,
        double cold_ua,
        double wall_capacity) {
        system.add_sparse_equation(
            name,
            {hot_pressure, hot_enthalpy, cold_pressure,
             cold_enthalpy, wall_temperature},
            [hot_properties = std::move(hot_properties),
             cold_properties = std::move(cold_properties),
             hot_pressure, hot_enthalpy, cold_pressure,
             cold_enthalpy, wall_temperature, hot_ua, cold_ua,
             wall_capacity](
                double, const std::vector<double>& x,
                const std::vector<double>& x_dot, double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                const auto hot =
                    physics::state_ph_derivatives_with_fallback(
                        *hot_properties, x.at(hot_pressure),
                        x.at(hot_enthalpy));
                if (!hot.ok()) return property_failure(hot);
                const auto cold =
                    physics::state_ph_derivatives_with_fallback(
                        *cold_properties, x.at(cold_pressure),
                        x.at(cold_enthalpy));
                if (!cold.ok()) return property_failure(cold);
                const double hot_heat = hot_ua *
                    (hot.state.temperature_k -
                     x.at(wall_temperature));
                const double cold_heat = cold_ua *
                    (x.at(wall_temperature) -
                     cold.state.temperature_k);
                residual = wall_capacity *
                               x_dot.at(wall_temperature) -
                           hot_heat + cold_heat;
                jacobian.push_back(
                    {hot_pressure,
                     -hot_ua * hot.derivatives
                         .temperature_wrt_pressure_at_enthalpy,
                     0.0});
                jacobian.push_back(
                    {hot_enthalpy,
                     -hot_ua * hot.derivatives
                         .temperature_wrt_enthalpy_at_pressure,
                     0.0});
                jacobian.push_back(
                    {cold_pressure,
                     -cold_ua * cold.derivatives
                         .temperature_wrt_pressure_at_enthalpy,
                     0.0});
                jacobian.push_back(
                    {cold_enthalpy,
                     -cold_ua * cold.derivatives
                         .temperature_wrt_enthalpy_at_pressure,
                     0.0});
                jacobian.push_back(
                    {wall_temperature, hot_ua + cold_ua,
                     wall_capacity});
                return EvaluationStatus::success();
            },
            std::max(wall_capacity, 1.0));
    }

    static void add_fluid_energy_closure(
        DaeEquationSystemBuilder& system,
        const std::string& name,
        std::shared_ptr<const physics::PropertyPackage> properties,
        double fluid_mass,
        std::size_t energy,
        std::size_t pressure,
        std::size_t enthalpy) {
        system.add_sparse_equation(
            name, {energy, pressure, enthalpy},
            [properties = std::move(properties), fluid_mass, energy,
             pressure, enthalpy](
                double, const std::vector<double>& x,
                const std::vector<double>&, double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                const auto state =
                    physics::state_ph_derivatives_with_fallback(
                        *properties, x.at(pressure), x.at(enthalpy));
                if (!state.ok()) return property_failure(state);
                residual = x.at(energy) -
                           fluid_mass * state.state.internal_energy_j_kg;
                jacobian.push_back({energy, 1.0, 0.0});
                jacobian.push_back(
                    {pressure,
                     -fluid_mass * state.derivatives
                         .internal_energy_wrt_pressure_at_enthalpy,
                     0.0});
                jacobian.push_back(
                    {enthalpy,
                     -fluid_mass * state.derivatives
                         .internal_energy_wrt_enthalpy_at_pressure,
                     0.0});
                return EvaluationStatus::success();
            },
            1.0e6);
    }

    ComponentModelDescriptor descriptor_;
};

}  // namespace

void register_dynamic_heat_transfer_component_models(
    ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<DynamicHeatExchangerCellModel>());
}

}  // namespace thermox::platform
