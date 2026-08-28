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

using component_model_support::property_failure;
using component_model_support::require_internal_variable;
using component_model_support::require_port_species;
using component_model_support::require_port_variable;
using component_model_support::require_property_package;
using component_model_support::require_thermochemistry_package;
using component_model_support::required_parameter;

using NumericDaeResidual = std::function<EvaluationStatus(
    const std::vector<double>&,
    const std::vector<double>&,
    double&)>;

void add_numeric_sparse_dae_equation(
    DaeEquationSystemBuilder& system,
    std::string name,
    std::vector<std::size_t> state_variables,
    std::vector<DaeEquationPartial> exact_rate_partials,
    NumericDaeResidual evaluate,
    double scale) {
    std::sort(state_variables.begin(), state_variables.end());
    state_variables.erase(
        std::unique(state_variables.begin(), state_variables.end()),
        state_variables.end());
    std::vector<std::size_t> sparsity = state_variables;
    for (const auto& partial : exact_rate_partials) {
        sparsity.push_back(partial.variable);
    }
    system.add_sparse_equation(
        std::move(name), std::move(sparsity),
        [state_variables = std::move(state_variables),
         exact_rate_partials = std::move(exact_rate_partials),
         evaluate = std::move(evaluate)](
            double, const std::vector<double>& x,
            const std::vector<double>& x_dot, double& residual,
            std::vector<DaeEquationPartial>& jacobian) {
            auto status = evaluate(x, x_dot, residual);
            if (!status.ok()) return status;
            for (const auto variable : state_variables) {
                std::vector<double> perturbed = x;
                const double step = 1.0e-6 *
                    std::max(std::abs(x.at(variable)), 1.0);
                perturbed.at(variable) += step;
                double shifted = 0.0;
                status = evaluate(perturbed, x_dot, shifted);
                if (!status.ok()) return status;
                jacobian.push_back(
                    {variable, (shifted - residual) / step, 0.0});
            }
            jacobian.insert(
                jacobian.end(), exact_rate_partials.begin(),
                exact_rate_partials.end());
            return EvaluationStatus::success();
        },
        scale);
}

EvaluationStatus thermochemistry_failure(
    const physics::ThermochemicalResult& result) {
    if (result.status == physics::PropertyStatus::backend_error) {
        return EvaluationStatus::fatal(result.message);
    }
    return EvaluationStatus::recoverable(result.message);
}

struct SaturationDerivatives {
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
    double pressure,
    SaturationDerivatives& output) {
    output.state = properties.saturation_p(pressure);
    if (!output.state.ok()) return property_failure(output.state);
    const double step = std::max(10.0, std::abs(pressure) * 1.0e-6);
    const auto plus = properties.saturation_p(pressure + step);
    const auto minus = properties.saturation_p(pressure - step);
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
            "could not evaluate saturation derivatives for two-phase cell");
    }
    const auto derivative = [first, second, denominator](
        auto member, bool vapor) {
        const auto& first_phase = vapor ? first->vapor : first->liquid;
        const auto& second_phase = vapor ? second->vapor : second->liquid;
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

class DynamicMaterialRigidVolumeCellModel final
    : public ComponentModel {
public:
    DynamicMaterialRigidVolumeCellModel() {
        descriptor_.kind =
            "heat_exchanger.material_fluid.equilibrium_two_phase_cell";
        descriptor_.version = "1.0.0";
        descriptor_.template_kind =
            "heat_exchanger.material_fluid.cell";
        descriptor_.display_name =
            "Material-to-fluid equilibrium two-phase cell";
        descriptor_.category = "Heat transfer";
        descriptor_.model_name =
            "Rigid saturated-mixture inventory with wall storage";
        descriptor_.ports = {
            {"hot_in", "material", "in"},
            {"hot_out", "material", "out"},
            {"cold_in", "fluid", "in"},
            {"cold_out", "fluid", "out"},
            {"inventory", "inventory", "out"}};
        descriptor_.parameters = {
            {"fluid_volume", "volume", true, std::nullopt, 0.0,
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
            {"hot_loss_coefficient", "dimensionless", true,
             std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true}};
        descriptor_.required_thermochemistry_capabilities = {
            physics::ThermochemistryCapability::state_ph};
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::saturation_p};
        descriptor_.supports_steady = false;
        descriptor_.supports_transient = true;
        descriptor_.internal_variables = {
            {"fluid_mass", DaeVariableKind::differential,
             2.5, 10.0, 0.0, 1.0, 1.0e-12,
             std::numeric_limits<double>::infinity(), "mass"},
            {"fluid_total_energy", DaeVariableKind::differential,
             3.5e6, 1.0e7, 0.0, 1.0e6,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(), "energy"},
            {"fluid_pressure", DaeVariableKind::algebraic,
             2.0e5, 1.0e5, 0.0, 1.0e5, 1.0,
             std::numeric_limits<double>::infinity(), "pressure"},
            {"vapor_quality", DaeVariableKind::algebraic,
             0.5, 1.0, 0.0, 1.0, 0.0, 1.0,
             "dimensionless"},
            {"wall_temperature", DaeVariableKind::differential,
             600.0, 100.0, 0.0, 10.0, 0.0,
             std::numeric_limits<double>::infinity(), "temperature"}};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext&,
        EquationSystemBuilder&) const override {
        throw std::logic_error(
            "material-to-fluid rigid-volume cell is transient-only");
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        const auto data = compile_data(context);
        const std::string prefix =
            "component." + context.component.id + ".";
        for (std::size_t i = 0; i < data.species.size(); ++i) {
            system.add_linear_equation(
                prefix + "hot_species_continuity." + data.species[i],
                {{data.hot_out_flows[i], 1.0, 0.0},
                 {data.hot_in_flows[i], -1.0, 0.0}},
                0.0, 100.0);
        }
        system.add_linear_equation(
            prefix + "fluid_mass_accumulation",
            {{data.fluid_mass, 0.0, 1.0},
             {data.cold_in_m, -1.0, 0.0},
             {data.cold_out_m, 1.0, 0.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "inventory_port",
            {{data.inventory_mass, 1.0, 0.0},
             {data.fluid_mass, -1.0, 0.0}},
            0.0, 10.0);
        system.add_linear_equation(
            prefix + "fluid_inlet_pressure",
            {{data.cold_in_p, 1.0, 0.0},
             {data.fluid_pressure, -1.0, 0.0}},
            0.0, 100000.0);
        add_saturated_outlet_enthalpy(
            system, prefix + "saturated_outlet_enthalpy", data);
        add_hot_pressure_loss(
            system, prefix + "hot_pressure_loss", data);
        system.add_linear_equation(
            prefix + "fluid_outlet_pressure",
            {{data.cold_out_p, 1.0, 0.0},
             {data.fluid_pressure, -1.0, 0.0}},
            0.0, 100000.0);
        add_hot_energy(system, prefix + "hot_energy", data);
        add_fluid_energy(
            system, prefix + "fluid_energy_accumulation", data);
        add_wall_energy(
            system, prefix + "wall_energy_accumulation", data);
        add_volume_closure(
            system, prefix + "rigid_volume_closure", data);
        add_energy_closure(
            system, prefix + "fluid_energy_closure", data);
    }

private:
    struct Data {
        std::vector<std::string> species;
        std::vector<std::size_t> hot_in_flows;
        std::vector<std::size_t> hot_out_flows;
        std::shared_ptr<const physics::ThermochemistryPackage> hot;
        std::shared_ptr<const physics::PropertyPackage> cold;
        std::size_t hot_in_p{}, hot_in_h{}, hot_out_p{}, hot_out_h{};
        std::size_t cold_in_m{}, cold_in_p{}, cold_in_h{};
        std::size_t cold_out_m{}, cold_out_p{}, cold_out_h{};
        std::size_t inventory_mass{};
        std::size_t fluid_mass{}, fluid_energy{};
        std::size_t fluid_pressure{}, vapor_quality{};
        std::size_t wall_temperature{};
        double volume{}, wall_capacity{}, hot_ua{}, cold_ua{};
        double hot_loss_scale{};
    };

    static Data compile_data(const ComponentCompileContext& context) {
        Data data;
        data.species = require_port_species(context, "hot_in");
        if (data.species != require_port_species(context, "hot_out")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' material ports must use the same species basis");
        }
        data.hot = require_thermochemistry_package(context, "hot_in");
        const auto hot_out =
            require_thermochemistry_package(context, "hot_out");
        if (data.hot->name() != hot_out->name() ||
            data.hot->version() != hot_out->version() ||
            data.hot->mechanism() != hot_out->mechanism() ||
            data.hot->phase() != hot_out->phase()) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' material ports must resolve the same thermochemistry package");
        }
        data.cold = require_property_package(context, "cold_in");
        if (data.cold != require_property_package(context, "cold_out")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' fluid ports must use the same medium");
        }
        for (const auto& species : data.species) {
            const std::string variable = "m_dot[" + species + "]";
            data.hot_in_flows.push_back(require_port_variable(
                context, "hot_in." + variable));
            data.hot_out_flows.push_back(require_port_variable(
                context, "hot_out." + variable));
        }
        data.hot_in_p = require_port_variable(context, "hot_in.p");
        data.hot_in_h = require_port_variable(context, "hot_in.h");
        data.hot_out_p = require_port_variable(context, "hot_out.p");
        data.hot_out_h = require_port_variable(context, "hot_out.h");
        data.cold_in_m = require_port_variable(context, "cold_in.m_dot");
        data.cold_in_p = require_port_variable(context, "cold_in.p");
        data.cold_in_h = require_port_variable(context, "cold_in.h");
        data.cold_out_m = require_port_variable(context, "cold_out.m_dot");
        data.cold_out_p = require_port_variable(context, "cold_out.p");
        data.cold_out_h = require_port_variable(context, "cold_out.h");
        data.inventory_mass = require_port_variable(
            context, "inventory.mass");
        data.fluid_mass = require_internal_variable(context, "fluid_mass");
        data.fluid_energy = require_internal_variable(
            context, "fluid_total_energy");
        data.fluid_pressure = require_internal_variable(
            context, "fluid_pressure");
        data.vapor_quality = require_internal_variable(
            context, "vapor_quality");
        data.wall_temperature = require_internal_variable(
            context, "wall_temperature");
        data.volume = required_parameter(context.component, "fluid_volume");
        data.wall_capacity = required_parameter(
            context.component, "wall_thermal_capacity");
        data.hot_ua = required_parameter(context.component, "hot_side_UA");
        data.cold_ua = required_parameter(
            context.component, "cold_side_UA");
        const double hot_diameter = required_parameter(
            context.component, "hot_flow_diameter");
        const double hot_area = std::numbers::pi * hot_diameter *
            hot_diameter / 4.0;
        data.hot_loss_scale = required_parameter(
            context.component, "hot_loss_coefficient") /
            (2.0 * hot_area * hot_area);
        return data;
    }

    static double hot_total_flow(
        const Data& data, const std::vector<double>& x) {
        double total = 0.0;
        for (const auto flow : data.hot_in_flows) {
            const double value = x.at(flow);
            if (!std::isfinite(value) || value < 0.0) {
                throw std::domain_error(
                    "rigid-volume cell hot species flows must be finite and nonnegative");
            }
            total += value;
        }
        if (!std::isfinite(total) || total <= 0.0) {
            throw std::domain_error(
                "rigid-volume cell total hot flow must be positive");
        }
        return total;
    }

    static EvaluationStatus hot_state(
        const Data& data, const std::vector<double>& x,
        physics::ThermochemicalResult& result) {
        try {
            const double total = hot_total_flow(data, x);
            std::vector<double> fractions;
            fractions.reserve(data.hot_in_flows.size());
            for (const auto flow : data.hot_in_flows) {
                fractions.push_back(x.at(flow) / total);
            }
            result = data.hot->state_ph(
                x.at(data.hot_out_p), x.at(data.hot_out_h),
                {physics::CompositionBasis::mass_fraction,
                 data.species, std::move(fractions)});
        } catch (const std::exception& ex) {
            return EvaluationStatus::recoverable(ex.what());
        }
        return result.ok() ? EvaluationStatus::success()
                           : thermochemistry_failure(result);
    }

    static std::vector<std::size_t> hot_variables(const Data& data) {
        std::vector<std::size_t> variables = data.hot_in_flows;
        variables.insert(
            variables.end(),
            {data.hot_in_p, data.hot_in_h,
             data.hot_out_p, data.hot_out_h});
        return variables;
    }

    static void add_hot_pressure_loss(
        DaeEquationSystemBuilder& system, const std::string& name,
        const Data& data) {
        add_numeric_sparse_dae_equation(
            system, name, hot_variables(data), {},
            [data](const std::vector<double>& x,
                   const std::vector<double>&, double& residual) {
                physics::ThermochemicalResult hot;
                const auto status = hot_state(data, x, hot);
                if (!status.ok()) return status;
                const double density =
                    hot.state.thermodynamic.density_kg_m3;
                if (!std::isfinite(density) || density <= 0.0) {
                    return EvaluationStatus::recoverable(
                        "rigid-volume cell hot pressure loss requires positive density");
                }
                const double flow = hot_total_flow(data, x);
                residual = x.at(data.hot_in_p) -
                    x.at(data.hot_out_p) - data.hot_loss_scale *
                    flow * flow / density;
                return EvaluationStatus::success();
            },
            100000.0);
    }

    static void add_saturated_outlet_enthalpy(
        DaeEquationSystemBuilder& system, const std::string& name,
        const Data& data) {
        system.add_sparse_equation(
            name,
            {data.fluid_pressure, data.vapor_quality,
             data.cold_out_h},
            [data](double, const std::vector<double>& x,
                   const std::vector<double>&, double& residual,
                   std::vector<DaeEquationPartial>& jacobian) {
                SaturationDerivatives saturation;
                const auto status = saturation_with_pressure_derivatives(
                    *data.cold, x.at(data.fluid_pressure), saturation);
                if (!status.ok()) return status;
                const double quality = x.at(data.vapor_quality);
                const double liquid_h =
                    saturation.state.liquid.enthalpy_j_kg;
                const double vapor_h =
                    saturation.state.vapor.enthalpy_j_kg;
                residual = x.at(data.cold_out_h) -
                    ((1.0 - quality) * liquid_h + quality * vapor_h);
                jacobian.push_back({
                    data.fluid_pressure,
                    -((1.0 - quality) * saturation.liquid_enthalpy +
                      quality * saturation.vapor_enthalpy),
                    0.0});
                jacobian.push_back(
                    {data.vapor_quality, -(vapor_h - liquid_h), 0.0});
                jacobian.push_back({data.cold_out_h, 1.0, 0.0});
                return EvaluationStatus::success();
            },
            1.0e6);
    }

    static void add_hot_energy(
        DaeEquationSystemBuilder& system, const std::string& name,
        const Data& data) {
        auto variables = hot_variables(data);
        variables.push_back(data.wall_temperature);
        add_numeric_sparse_dae_equation(
            system, name, std::move(variables), {},
            [data](const std::vector<double>& x,
                   const std::vector<double>&, double& residual) {
                physics::ThermochemicalResult hot;
                const auto status = hot_state(data, x, hot);
                if (!status.ok()) return status;
                const double heat = data.hot_ua *
                    (hot.state.thermodynamic.temperature_k -
                     x.at(data.wall_temperature));
                residual = hot_total_flow(data, x) *
                    (x.at(data.hot_in_h) - x.at(data.hot_out_h)) -
                    heat;
                return EvaluationStatus::success();
            },
            1.0e6);
    }

    static void add_fluid_energy(
        DaeEquationSystemBuilder& system, const std::string& name,
        const Data& data) {
        system.add_sparse_equation(
            name,
            {data.fluid_energy, data.cold_in_m, data.cold_in_h,
             data.cold_out_m, data.fluid_pressure,
             data.cold_out_h, data.wall_temperature},
            [data](double, const std::vector<double>& x,
                   const std::vector<double>& x_dot, double& residual,
                   std::vector<DaeEquationPartial>& jacobian) {
                SaturationDerivatives saturation;
                const auto status = saturation_with_pressure_derivatives(
                    *data.cold, x.at(data.fluid_pressure), saturation);
                if (!status.ok()) return status;
                residual = x_dot.at(data.fluid_energy) -
                    x.at(data.cold_in_m) * x.at(data.cold_in_h) +
                    x.at(data.cold_out_m) * x.at(data.cold_out_h) -
                    data.cold_ua *
                        (x.at(data.wall_temperature) -
                         saturation.state.liquid.temperature_k);
                jacobian.push_back({data.fluid_energy, 0.0, 1.0});
                jacobian.push_back(
                    {data.cold_in_m, -x.at(data.cold_in_h), 0.0});
                jacobian.push_back(
                    {data.cold_in_h, -x.at(data.cold_in_m), 0.0});
                jacobian.push_back({
                    data.cold_out_m, x.at(data.cold_out_h), 0.0});
                jacobian.push_back({
                    data.fluid_pressure,
                    data.cold_ua * saturation.temperature,
                    0.0});
                jacobian.push_back({
                    data.cold_out_h, x.at(data.cold_out_m), 0.0});
                jacobian.push_back(
                    {data.wall_temperature, -data.cold_ua, 0.0});
                return EvaluationStatus::success();
            },
            1.0e6);
    }

    static void add_wall_energy(
        DaeEquationSystemBuilder& system, const std::string& name,
        const Data& data) {
        auto variables = hot_variables(data);
        variables.insert(
            variables.end(),
            {data.fluid_pressure, data.vapor_quality,
             data.wall_temperature});
        add_numeric_sparse_dae_equation(
            system, name, std::move(variables),
            {{data.wall_temperature, 0.0, data.wall_capacity}},
            [data](const std::vector<double>& x,
                   const std::vector<double>& x_dot,
                   double& residual) {
                physics::ThermochemicalResult hot;
                const auto status = hot_state(data, x, hot);
                if (!status.ok()) return status;
                const auto saturation = data.cold->saturation_p(
                    x.at(data.fluid_pressure));
                if (!saturation.ok()) {
                    return property_failure(saturation);
                }
                const double hot_heat = data.hot_ua *
                    (hot.state.thermodynamic.temperature_k -
                     x.at(data.wall_temperature));
                const double cold_heat = data.cold_ua *
                    (x.at(data.wall_temperature) -
                     saturation.liquid.temperature_k);
                residual = data.wall_capacity *
                    x_dot.at(data.wall_temperature) -
                    hot_heat + cold_heat;
                return EvaluationStatus::success();
            },
            std::max(data.wall_capacity, 1.0));
    }

    static void add_volume_closure(
        DaeEquationSystemBuilder& system, const std::string& name,
        const Data& data) {
        system.add_sparse_equation(
            name,
            {data.fluid_mass, data.fluid_pressure,
             data.vapor_quality},
            [data](double, const std::vector<double>& x,
                   const std::vector<double>&, double& residual,
                   std::vector<DaeEquationPartial>& jacobian) {
                SaturationDerivatives saturation;
                const auto status = saturation_with_pressure_derivatives(
                    *data.cold, x.at(data.fluid_pressure), saturation);
                if (!status.ok()) return status;
                const double rho_l =
                    saturation.state.liquid.density_kg_m3;
                const double rho_v =
                    saturation.state.vapor.density_kg_m3;
                const double quality = x.at(data.vapor_quality);
                const double v_l = 1.0 / rho_l;
                const double v_v = 1.0 / rho_v;
                const double specific_volume =
                    (1.0 - quality) * v_l + quality * v_v;
                const double mass = x.at(data.fluid_mass);
                const double dv_l_dp = -saturation.liquid_density /
                    (rho_l * rho_l);
                const double dv_v_dp = -saturation.vapor_density /
                    (rho_v * rho_v);
                residual = mass * specific_volume - data.volume;
                jacobian.push_back(
                    {data.fluid_mass, specific_volume, 0.0});
                jacobian.push_back({
                    data.fluid_pressure,
                    mass * ((1.0 - quality) * dv_l_dp +
                            quality * dv_v_dp),
                    0.0});
                jacobian.push_back({
                    data.vapor_quality, mass * (v_v - v_l), 0.0});
                return EvaluationStatus::success();
            },
            std::max(data.volume, 1.0));
    }

    static void add_energy_closure(
        DaeEquationSystemBuilder& system, const std::string& name,
        const Data& data) {
        system.add_sparse_equation(
            name,
            {data.fluid_energy, data.fluid_mass,
             data.fluid_pressure, data.vapor_quality},
            [data](double, const std::vector<double>& x,
                   const std::vector<double>&, double& residual,
                   std::vector<DaeEquationPartial>& jacobian) {
                SaturationDerivatives saturation;
                const auto status = saturation_with_pressure_derivatives(
                    *data.cold, x.at(data.fluid_pressure), saturation);
                if (!status.ok()) return status;
                const double mass = x.at(data.fluid_mass);
                const double quality = x.at(data.vapor_quality);
                const double liquid_u = saturation.state.liquid
                    .internal_energy_j_kg;
                const double vapor_u = saturation.state.vapor
                    .internal_energy_j_kg;
                const double internal_energy =
                    (1.0 - quality) * liquid_u + quality * vapor_u;
                residual = x.at(data.fluid_energy) -
                    mass * internal_energy;
                jacobian.push_back({data.fluid_energy, 1.0, 0.0});
                jacobian.push_back(
                    {data.fluid_mass, -internal_energy, 0.0});
                jacobian.push_back({
                    data.fluid_pressure,
                    -mass * ((1.0 - quality) *
                        saturation.liquid_internal_energy +
                        quality * saturation.vapor_internal_energy),
                    0.0});
                jacobian.push_back({
                    data.vapor_quality,
                    -mass * (vapor_u - liquid_u), 0.0});
                return EvaluationStatus::success();
            },
            1.0e7);
    }

    ComponentModelDescriptor descriptor_;
};

}  // namespace

void register_dynamic_material_rigid_volume_component_models(
    ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<DynamicMaterialRigidVolumeCellModel>());
}

}  // namespace thermox::platform
