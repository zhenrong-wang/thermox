#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace thermox::platform {

namespace {

using component_model_support::property_failure;
using component_model_support::require_correlation;
using component_model_support::require_internal_variable;
using component_model_support::require_port_variable;
using component_model_support::require_property_package;
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

class DynamicHeatExchangerCellModel final : public ComponentModel {
public:
    explicit DynamicHeatExchangerCellModel(
        bool finite_volume,
        bool correlated_heat_transfer = false)
        : finite_volume_(finite_volume),
          correlated_heat_transfer_(correlated_heat_transfer) {
        if (correlated_heat_transfer_ && !finite_volume_) {
            throw std::logic_error(
                "correlated exchanger heat transfer requires finite volume");
        }
        descriptor_.kind = correlated_heat_transfer_
            ? "heat_exchanger.fluid.finite_volume_correlated_cell"
            : (finite_volume_
                ? "heat_exchanger.fluid.finite_volume_cell"
                : "heat_exchanger.fluid.dynamic_cell");
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "heat_exchanger.fluid.cell";
        descriptor_.display_name = correlated_heat_transfer_
            ? "Correlation-driven finite-volume heat-exchanger cell"
            : (finite_volume_
                ? "Finite-volume heat-exchanger cell"
                : "Dynamic heat-exchanger cell");
        descriptor_.category = "Heat transfer";
        descriptor_.model_name = correlated_heat_transfer_
            ? "Artifact-driven conductance with geometric fluid holdup"
            : (finite_volume_
                ? "Property-backed geometric fluid holdup"
                : "Well-mixed constant-holdup fluids with wall capacitance");
        descriptor_.ports = {
            {"hot_in", "fluid", "in"},
            {"hot_out", "fluid", "out"},
            {"cold_in", "fluid", "in"},
            {"cold_out", "fluid", "out"},
            {"hot_inventory", "inventory", "out", 1U, "hot_out"},
            {"cold_inventory", "inventory", "out", 1U, "cold_out"}};
        descriptor_.parameters = {
            {"wall_thermal_capacity", "thermal_capacity", true,
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
        if (correlated_heat_transfer_) {
            descriptor_.artifacts = {
                {"hot_side_conductance_correlation",
                 correlation_artifact_type, true},
                {"cold_side_conductance_correlation",
                 correlation_artifact_type, true}};
        } else {
            descriptor_.parameters.insert(
                descriptor_.parameters.begin() + 1,
                {"hot_side_UA", "thermal_conductance", true,
                 std::nullopt, 0.0,
                 std::numeric_limits<double>::infinity(), false, true});
            descriptor_.parameters.insert(
                descriptor_.parameters.begin() + 2,
                {"cold_side_UA", "thermal_conductance", true,
                 std::nullopt, 0.0,
                 std::numeric_limits<double>::infinity(), false, true});
        }
        if (finite_volume_) {
            descriptor_.parameters.insert(
                descriptor_.parameters.begin(), {
                    {"hot_fluid_volume", "volume", true,
                     std::nullopt, 0.0,
                     std::numeric_limits<double>::infinity(),
                     false, true},
                    {"cold_fluid_volume", "volume", true,
                     std::nullopt, 0.0,
                     std::numeric_limits<double>::infinity(),
                     false, true}});
        } else {
            descriptor_.parameters.insert(
                descriptor_.parameters.begin(), {
                    {"hot_fluid_mass", "mass", true,
                     std::nullopt, 0.0,
                     std::numeric_limits<double>::infinity(),
                     false, true},
                    {"cold_fluid_mass", "mass", true,
                     std::nullopt, 0.0,
                     std::numeric_limits<double>::infinity(),
                     false, true}});
        }
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph};
        descriptor_.supports_steady = true;
        descriptor_.supports_transient = true;
        if (finite_volume_) {
            descriptor_.internal_variables = {
                {"hot_mass", DaeVariableKind::differential,
                 1.0, 10.0, 0.0, 1.0, 1.0e-12,
                 std::numeric_limits<double>::infinity(), "mass"},
                {"hot_total_energy", DaeVariableKind::differential,
                 300000.0, 1.0e6, 0.0, 1.0e5,
                 -std::numeric_limits<double>::infinity(),
                 std::numeric_limits<double>::infinity(), "energy"},
                {"hot_pressure", DaeVariableKind::algebraic,
                 200000.0, 100000.0, 0.0, 100000.0, 1.0,
                 std::numeric_limits<double>::infinity(), "pressure"},
                {"hot_enthalpy", DaeVariableKind::algebraic,
                 500000.0, 100000.0, 0.0, 100000.0,
                 -std::numeric_limits<double>::infinity(),
                 std::numeric_limits<double>::infinity(),
                 "specific_enthalpy"},
                {"cold_mass", DaeVariableKind::differential,
                 1.0, 10.0, 0.0, 1.0, 1.0e-12,
                 std::numeric_limits<double>::infinity(), "mass"},
                {"cold_total_energy", DaeVariableKind::differential,
                 200000.0, 1.0e6, 0.0, 1.0e5,
                 -std::numeric_limits<double>::infinity(),
                 std::numeric_limits<double>::infinity(), "energy"},
                {"cold_pressure", DaeVariableKind::algebraic,
                 200000.0, 100000.0, 0.0, 100000.0, 1.0,
                 std::numeric_limits<double>::infinity(), "pressure"},
                {"cold_enthalpy", DaeVariableKind::algebraic,
                 300000.0, 100000.0, 0.0, 100000.0,
                 -std::numeric_limits<double>::infinity(),
                 std::numeric_limits<double>::infinity(),
                 "specific_enthalpy"},
                {"wall_temperature", DaeVariableKind::differential,
                 350.0, 100.0, 0.0, 10.0, 0.0,
                 std::numeric_limits<double>::infinity(), "temperature"}};
        } else {
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
        const double hot_holdup_parameter = required_parameter(
            context.component, finite_volume_
                ? "hot_fluid_volume" : "hot_fluid_mass");
        const double cold_holdup_parameter = required_parameter(
            context.component, finite_volume_
                ? "cold_fluid_volume" : "cold_fluid_mass");
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
        if (finite_volume_) {
            add_steady_inventory_closure(
                system, prefix + "hot_inventory", hot_properties,
                hot_holdup_parameter, hot_inventory,
                hot_in_p, hot_out_h);
            add_steady_inventory_closure(
                system, prefix + "cold_inventory", cold_properties,
                cold_holdup_parameter, cold_inventory,
                cold_in_p, cold_out_h);
        } else {
            system.add_linear_equation(
                prefix + "hot_inventory",
                {{hot_inventory, 1.0}}, hot_holdup_parameter, 10.0);
            system.add_linear_equation(
                prefix + "cold_inventory",
                {{cold_inventory, 1.0}}, cold_holdup_parameter, 10.0);
        }
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
        if (correlated_heat_transfer_) {
            const auto hot_correlation = require_correlation(
                context, "hot_side_conductance_correlation");
            const auto cold_correlation = require_correlation(
                context, "cold_side_conductance_correlation");
            validate_conductance_correlation(
                *hot_correlation, *hot_properties, "hot-side");
            validate_conductance_correlation(
                *cold_correlation, *cold_properties, "cold-side");
            add_steady_correlated_heat_transfer(
                system, prefix + "mixed_cell_heat_transfer",
                {hot_correlation, hot_properties, hot_in_m, hot_in_m,
                 hot_in_p, hot_out_h, hot_diameter, hot_area},
                {cold_correlation, cold_properties, cold_in_m, cold_in_m,
                 cold_in_p, cold_out_h, cold_diameter, cold_area},
                hot_in_h);
        } else {
            const double hot_ua = required_parameter(
                context.component, "hot_side_UA");
            const double cold_ua = required_parameter(
                context.component, "cold_side_UA");
            add_steady_fixed_heat_transfer(
                system, prefix + "mixed_cell_heat_transfer",
                hot_properties, cold_properties,
                hot_ua * cold_ua / (hot_ua + cold_ua),
                hot_in_m, hot_in_p, hot_in_h, hot_out_h,
                cold_in_p, cold_out_h);
        }
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        const auto hot_properties = validate_media(context);
        const auto cold_properties =
            require_property_package(context, "cold_in");
        if (finite_volume_) {
            add_finite_volume_transient_equations(
                context, system, hot_properties, cold_properties,
                correlated_heat_transfer_);
            return;
        }
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
    struct ConductanceClosure {
        std::shared_ptr<const CorrelationArtifact> correlation;
        std::shared_ptr<const physics::PropertyPackage> properties;
        std::size_t inlet_mass_flow{};
        std::size_t outlet_mass_flow{};
        std::size_t pressure{};
        std::size_t enthalpy{};
        double diameter{};
        double area{};
    };

    static const std::map<std::string, std::string>&
    conductance_input_contract() {
        static const std::map<std::string, std::string> contract{
            {"mass_flow", "mass_flow"},
            {"mass_flux", "mass_flux"},
            {"pressure", "pressure"},
            {"enthalpy", "specific_enthalpy"},
            {"temperature", "temperature"},
            {"density", "density"},
            {"vapor_quality", "dimensionless"},
            {"specific_heat_capacity", "specific_heat_capacity"},
            {"dynamic_viscosity", "dynamic_viscosity"},
            {"thermal_conductivity", "thermal_conductivity"},
            {"reynolds_number", "dimensionless"},
            {"prandtl_number", "dimensionless"},
            {"diameter", "length"},
            {"area", "area"},
            {"liquid_density", "density"},
            {"vapor_density", "density"},
            {"latent_heat", "specific_enthalpy"}};
        return contract;
    }

    static void validate_conductance_correlation(
        const CorrelationArtifact& correlation,
        const physics::PropertyPackage& properties,
        const std::string& side) {
        if (correlation.output().name != "thermal_conductance" ||
            correlation.output().dimension != "thermal_conductance") {
            throw std::invalid_argument(
                side + " heat-transfer correlation output must be named "
                "'thermal_conductance' with thermal_conductance dimension");
        }
        bool needs_transport = false;
        bool needs_saturation = false;
        for (const auto& input : correlation.inputs()) {
            const auto supported = conductance_input_contract().find(
                input.name);
            if (supported == conductance_input_contract().end() ||
                input.dimension != supported->second) {
                throw std::invalid_argument(
                    side + " heat-transfer correlation input '" +
                    input.name + "' has unsupported name or dimension");
            }
            needs_transport = needs_transport ||
                input.name == "dynamic_viscosity" ||
                input.name == "thermal_conductivity" ||
                input.name == "reynolds_number" ||
                input.name == "prandtl_number";
            needs_saturation = needs_saturation ||
                input.name == "liquid_density" ||
                input.name == "vapor_density" ||
                input.name == "latent_heat";
        }
        if (needs_transport &&
            !properties.supports(
                physics::PropertyCapability::transport)) {
            throw std::invalid_argument(
                side + " heat-transfer correlation requires transport "
                "properties unsupported by its medium");
        }
        if (needs_saturation &&
            !properties.supports(
                physics::PropertyCapability::saturation_p)) {
            throw std::invalid_argument(
                side + " heat-transfer correlation requires saturation "
                "properties unsupported by its medium");
        }
    }

    static EvaluationStatus evaluate_conductance(
        const ConductanceClosure& closure,
        const std::vector<double>& x,
        double& conductance) {
        const auto state = closure.properties->state_ph(
            x.at(closure.pressure), x.at(closure.enthalpy));
        if (!state.ok()) return property_failure(state);
        std::map<std::string, double> inputs;
        const double flow = 0.5 *
            (x.at(closure.inlet_mass_flow) +
             x.at(closure.outlet_mass_flow));
        const double mass_flux = std::abs(flow) / closure.area;
        bool saturation_requested = false;
        for (const auto& input : closure.correlation->inputs()) {
            if (input.name == "mass_flow") {
                inputs.emplace(input.name, flow);
            } else if (input.name == "mass_flux") {
                inputs.emplace(input.name, mass_flux);
            } else if (input.name == "pressure") {
                inputs.emplace(input.name, x.at(closure.pressure));
            } else if (input.name == "enthalpy") {
                inputs.emplace(input.name, x.at(closure.enthalpy));
            } else if (input.name == "temperature") {
                inputs.emplace(input.name, state.state.temperature_k);
            } else if (input.name == "density") {
                inputs.emplace(input.name, state.state.density_kg_m3);
            } else if (input.name == "vapor_quality") {
                inputs.emplace(input.name, state.state.vapor_quality);
            } else if (input.name == "specific_heat_capacity") {
                inputs.emplace(input.name, state.state.cp_j_kg_k);
            } else if (input.name == "dynamic_viscosity") {
                inputs.emplace(input.name, state.state.viscosity_pa_s);
            } else if (input.name == "thermal_conductivity") {
                inputs.emplace(
                    input.name,
                    state.state.thermal_conductivity_w_m_k);
            } else if (input.name == "reynolds_number") {
                if (!(state.state.viscosity_pa_s > 0.0)) {
                    return EvaluationStatus::recoverable(
                        "heat-transfer correlation requires positive "
                        "dynamic viscosity for Reynolds number");
                }
                inputs.emplace(
                    input.name,
                    mass_flux * closure.diameter /
                        state.state.viscosity_pa_s);
            } else if (input.name == "prandtl_number") {
                if (!(state.state.thermal_conductivity_w_m_k > 0.0)) {
                    return EvaluationStatus::recoverable(
                        "heat-transfer correlation requires positive "
                        "thermal conductivity for Prandtl number");
                }
                inputs.emplace(
                    input.name,
                    state.state.cp_j_kg_k *
                        state.state.viscosity_pa_s /
                        state.state.thermal_conductivity_w_m_k);
            } else if (input.name == "diameter") {
                inputs.emplace(input.name, closure.diameter);
            } else if (input.name == "area") {
                inputs.emplace(input.name, closure.area);
            } else {
                saturation_requested = true;
            }
        }
        if (saturation_requested) {
            const auto saturation = closure.properties->saturation_p(
                x.at(closure.pressure));
            if (!saturation.ok()) return property_failure(saturation);
            for (const auto& input : closure.correlation->inputs()) {
                if (input.name == "liquid_density") {
                    inputs.emplace(
                        input.name,
                        saturation.liquid.density_kg_m3);
                } else if (input.name == "vapor_density") {
                    inputs.emplace(
                        input.name,
                        saturation.vapor.density_kg_m3);
                } else if (input.name == "latent_heat") {
                    inputs.emplace(
                        input.name,
                        saturation.vapor.enthalpy_j_kg -
                            saturation.liquid.enthalpy_j_kg);
                }
            }
        }
        const auto evaluated = closure.correlation->evaluate(inputs);
        if (!evaluated.error.empty()) {
            return EvaluationStatus::recoverable(evaluated.error);
        }
        if (!std::isfinite(evaluated.value) ||
            evaluated.value <= 0.0) {
            return EvaluationStatus::recoverable(
                "heat-transfer correlation must produce positive finite "
                "thermal conductance");
        }
        conductance = evaluated.value;
        return EvaluationStatus::success();
    }

    static void add_steady_fixed_heat_transfer(
        EquationSystemBuilder& system,
        const std::string& name,
        std::shared_ptr<const physics::PropertyPackage> hot_properties,
        std::shared_ptr<const physics::PropertyPackage> cold_properties,
        double effective_ua,
        std::size_t hot_in_m,
        std::size_t hot_in_p,
        std::size_t hot_in_h,
        std::size_t hot_out_h,
        std::size_t cold_in_p,
        std::size_t cold_out_h) {
        system.add_checked_equation(
            name,
            [hot_properties = std::move(hot_properties),
             cold_properties = std::move(cold_properties), effective_ua,
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
                residual = x.at(hot_in_m) *
                        (x.at(hot_in_h) - x.at(hot_out_h)) -
                    effective_ua * difference;
                return EvaluationStatus::success();
            },
            1.0e6);
    }

    static void add_steady_correlated_heat_transfer(
        EquationSystemBuilder& system,
        const std::string& name,
        ConductanceClosure hot,
        ConductanceClosure cold,
        std::size_t hot_in_h) {
        system.add_checked_equation(
            name,
            [hot = std::move(hot), cold = std::move(cold), hot_in_h](
                const std::vector<double>& x, double& residual) {
                const auto hot_state = hot.properties->state_ph(
                    x.at(hot.pressure), x.at(hot.enthalpy));
                if (!hot_state.ok()) return property_failure(hot_state);
                const auto cold_state = cold.properties->state_ph(
                    x.at(cold.pressure), x.at(cold.enthalpy));
                if (!cold_state.ok()) return property_failure(cold_state);
                double hot_ua = 0.0;
                auto status = evaluate_conductance(hot, x, hot_ua);
                if (!status.ok()) return status;
                double cold_ua = 0.0;
                status = evaluate_conductance(cold, x, cold_ua);
                if (!status.ok()) return status;
                const double difference = hot_state.state.temperature_k -
                    cold_state.state.temperature_k;
                if (!std::isfinite(difference) || difference <= 0.0) {
                    return EvaluationStatus::recoverable(
                        "correlated heat-exchanger cell requires hot bulk "
                        "temperature above cold bulk temperature");
                }
                const double effective_ua =
                    hot_ua * cold_ua / (hot_ua + cold_ua);
                residual = x.at(hot.inlet_mass_flow) *
                        (x.at(hot_in_h) - x.at(hot.enthalpy)) -
                    effective_ua * difference;
                return EvaluationStatus::success();
            },
            1.0e6);
    }

    static void add_correlated_fluid_energy_equation(
        DaeEquationSystemBuilder& system,
        const std::string& name,
        ConductanceClosure closure,
        std::size_t energy,
        std::size_t inlet_m,
        std::size_t inlet_h,
        std::size_t outlet_m,
        std::size_t wall_temperature) {
        add_numeric_sparse_dae_equation(
            system, name,
            {inlet_m, inlet_h, outlet_m,
             closure.inlet_mass_flow, closure.outlet_mass_flow,
             closure.pressure, closure.enthalpy, wall_temperature},
            {{energy, 0.0, 1.0}},
            [closure = std::move(closure), energy, inlet_m, inlet_h,
             outlet_m, wall_temperature](
                const std::vector<double>& x,
                const std::vector<double>& x_dot,
                double& residual) {
                const auto state = closure.properties->state_ph(
                    x.at(closure.pressure), x.at(closure.enthalpy));
                if (!state.ok()) return property_failure(state);
                double conductance = 0.0;
                const auto status = evaluate_conductance(
                    closure, x, conductance);
                if (!status.ok()) return status;
                const double heat = conductance *
                    (state.state.temperature_k -
                     x.at(wall_temperature));
                residual = x_dot.at(energy) -
                    x.at(inlet_m) * x.at(inlet_h) +
                    x.at(outlet_m) * x.at(closure.enthalpy) + heat;
                return EvaluationStatus::success();
            },
            1.0e6);
    }

    static void add_correlated_wall_energy_equation(
        DaeEquationSystemBuilder& system,
        const std::string& name,
        ConductanceClosure hot,
        ConductanceClosure cold,
        std::size_t wall_temperature,
        double wall_capacity) {
        add_numeric_sparse_dae_equation(
            system, name,
            {hot.inlet_mass_flow, hot.outlet_mass_flow,
             hot.pressure, hot.enthalpy,
             cold.inlet_mass_flow, cold.outlet_mass_flow,
             cold.pressure, cold.enthalpy, wall_temperature},
            {{wall_temperature, 0.0, wall_capacity}},
            [hot = std::move(hot), cold = std::move(cold),
             wall_temperature, wall_capacity](
                const std::vector<double>& x,
                const std::vector<double>& x_dot,
                double& residual) {
                const auto hot_state = hot.properties->state_ph(
                    x.at(hot.pressure), x.at(hot.enthalpy));
                if (!hot_state.ok()) return property_failure(hot_state);
                const auto cold_state = cold.properties->state_ph(
                    x.at(cold.pressure), x.at(cold.enthalpy));
                if (!cold_state.ok()) return property_failure(cold_state);
                double hot_ua = 0.0;
                auto status = evaluate_conductance(hot, x, hot_ua);
                if (!status.ok()) return status;
                double cold_ua = 0.0;
                status = evaluate_conductance(cold, x, cold_ua);
                if (!status.ok()) return status;
                const double hot_heat = hot_ua *
                    (hot_state.state.temperature_k -
                     x.at(wall_temperature));
                const double cold_heat = cold_ua *
                    (x.at(wall_temperature) -
                     cold_state.state.temperature_k);
                residual = wall_capacity * x_dot.at(wall_temperature) -
                    hot_heat + cold_heat;
                return EvaluationStatus::success();
            },
            std::max(wall_capacity, 1.0));
    }

    static void add_finite_volume_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system,
        const std::shared_ptr<const physics::PropertyPackage>&
            hot_properties,
        const std::shared_ptr<const physics::PropertyPackage>&
            cold_properties,
        bool correlated_heat_transfer) {
        const double hot_volume = required_parameter(
            context.component, "hot_fluid_volume");
        const double cold_volume = required_parameter(
            context.component, "cold_fluid_volume");
        const double wall_capacity = required_parameter(
            context.component, "wall_thermal_capacity");
        const double hot_diameter = required_parameter(
            context.component, "hot_flow_diameter");
        const double cold_diameter = required_parameter(
            context.component, "cold_flow_diameter");
        const double hot_area = std::numbers::pi * hot_diameter *
            hot_diameter / 4.0;
        const double cold_area = std::numbers::pi * cold_diameter *
            cold_diameter / 4.0;
        const double hot_loss_scale = required_parameter(
            context.component, "hot_loss_coefficient") /
            (2.0 * hot_area * hot_area);
        const double cold_loss_scale = required_parameter(
            context.component, "cold_loss_coefficient") /
            (2.0 * cold_area * cold_area);

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
        const auto hot_mass = require_internal_variable(context, "hot_mass");
        const auto hot_energy = require_internal_variable(
            context, "hot_total_energy");
        const auto hot_pressure = require_internal_variable(
            context, "hot_pressure");
        const auto hot_enthalpy = require_internal_variable(
            context, "hot_enthalpy");
        const auto cold_mass = require_internal_variable(context, "cold_mass");
        const auto cold_energy = require_internal_variable(
            context, "cold_total_energy");
        const auto cold_pressure = require_internal_variable(
            context, "cold_pressure");
        const auto cold_enthalpy = require_internal_variable(
            context, "cold_enthalpy");
        const auto wall_temperature = require_internal_variable(
            context, "wall_temperature");
        const std::string prefix = "component." + context.component.id + ".";

        add_mass_accumulation(
            system, prefix + "hot_mass_accumulation",
            hot_mass, hot_in_m, hot_out_m);
        add_mass_accumulation(
            system, prefix + "cold_mass_accumulation",
            cold_mass, cold_in_m, cold_out_m);
        system.add_linear_equation(
            prefix + "hot_inventory_port",
            {{hot_inventory, 1.0, 0.0}, {hot_mass, -1.0, 0.0}},
            0.0, 10.0);
        system.add_linear_equation(
            prefix + "cold_inventory_port",
            {{cold_inventory, 1.0, 0.0}, {cold_mass, -1.0, 0.0}},
            0.0, 10.0);
        system.add_linear_equation(
            prefix + "hot_bulk_pressure",
            {{hot_in_p, 1.0, 0.0}, {hot_pressure, -1.0, 0.0}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "cold_bulk_pressure",
            {{cold_in_p, 1.0, 0.0}, {cold_pressure, -1.0, 0.0}},
            0.0, 100000.0);
        if (correlated_heat_transfer) {
            const auto hot_correlation = require_correlation(
                context, "hot_side_conductance_correlation");
            const auto cold_correlation = require_correlation(
                context, "cold_side_conductance_correlation");
            validate_conductance_correlation(
                *hot_correlation, *hot_properties, "hot-side");
            validate_conductance_correlation(
                *cold_correlation, *cold_properties, "cold-side");
            const ConductanceClosure hot_closure{
                hot_correlation, hot_properties, hot_in_m, hot_out_m,
                hot_pressure, hot_enthalpy, hot_diameter, hot_area};
            const ConductanceClosure cold_closure{
                cold_correlation, cold_properties,
                cold_in_m, cold_out_m, cold_pressure, cold_enthalpy,
                cold_diameter, cold_area};
            add_correlated_fluid_energy_equation(
                system, prefix + "hot_energy_accumulation",
                hot_closure, hot_energy, hot_in_m, hot_in_h,
                hot_out_m, wall_temperature);
            add_correlated_fluid_energy_equation(
                system, prefix + "cold_energy_accumulation",
                cold_closure, cold_energy, cold_in_m, cold_in_h,
                cold_out_m, wall_temperature);
            add_correlated_wall_energy_equation(
                system, prefix + "wall_energy_accumulation",
                hot_closure, cold_closure, wall_temperature,
                wall_capacity);
        } else {
            const double hot_ua = required_parameter(
                context.component, "hot_side_UA");
            const double cold_ua = required_parameter(
                context.component, "cold_side_UA");
            add_fluid_energy_equation(
                system, prefix + "hot_energy_accumulation",
                hot_properties, hot_energy, hot_in_m, hot_in_h,
                hot_out_m, hot_pressure, hot_enthalpy,
                wall_temperature, hot_ua);
            add_fluid_energy_equation(
                system, prefix + "cold_energy_accumulation",
                cold_properties, cold_energy, cold_in_m, cold_in_h,
                cold_out_m, cold_pressure, cold_enthalpy,
                wall_temperature, cold_ua);
            add_wall_energy_equation(
                system, prefix + "wall_energy_accumulation",
                hot_properties, cold_properties, hot_pressure,
                hot_enthalpy, cold_pressure, cold_enthalpy,
                wall_temperature, hot_ua, cold_ua, wall_capacity);
        }
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
            hot_out_m, hot_pressure, hot_enthalpy, hot_out_p,
            hot_loss_scale);
        add_transient_pressure_loss(
            system, prefix + "cold_pressure_loss", cold_properties,
            cold_out_m, cold_pressure, cold_enthalpy, cold_out_p,
            cold_loss_scale);
        add_transient_volume_closure(
            system, prefix + "hot_volume_closure", hot_properties,
            hot_volume, hot_mass, hot_pressure, hot_enthalpy);
        add_transient_volume_closure(
            system, prefix + "cold_volume_closure", cold_properties,
            cold_volume, cold_mass, cold_pressure, cold_enthalpy);
        add_variable_mass_energy_closure(
            system, prefix + "hot_energy_closure", hot_properties,
            hot_energy, hot_mass, hot_pressure, hot_enthalpy);
        add_variable_mass_energy_closure(
            system, prefix + "cold_energy_closure", cold_properties,
            cold_energy, cold_mass, cold_pressure, cold_enthalpy);
    }

    static void add_mass_accumulation(
        DaeEquationSystemBuilder& system,
        const std::string& name,
        std::size_t mass,
        std::size_t inlet_m,
        std::size_t outlet_m) {
        system.add_linear_equation(
            name,
            {{mass, 0.0, 1.0}, {inlet_m, -1.0, 0.0},
             {outlet_m, 1.0, 0.0}},
            0.0, 100.0);
    }

    static void add_transient_volume_closure(
        DaeEquationSystemBuilder& system,
        const std::string& name,
        std::shared_ptr<const physics::PropertyPackage> properties,
        double volume,
        std::size_t mass,
        std::size_t pressure,
        std::size_t enthalpy) {
        system.add_sparse_equation(
            name, {mass, pressure, enthalpy},
            [properties = std::move(properties), volume, mass,
             pressure, enthalpy](
                double, const std::vector<double>& x,
                const std::vector<double>&, double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                const auto state =
                    physics::state_ph_derivatives_with_fallback(
                        *properties, x.at(pressure), x.at(enthalpy));
                if (!state.ok()) return property_failure(state);
                residual = x.at(mass) -
                    volume * state.state.density_kg_m3;
                jacobian.push_back({mass, 1.0, 0.0});
                jacobian.push_back({
                    pressure,
                    -volume * state.derivatives
                        .density_wrt_pressure_at_enthalpy,
                    0.0});
                jacobian.push_back({
                    enthalpy,
                    -volume * state.derivatives
                        .density_wrt_enthalpy_at_pressure,
                    0.0});
                return EvaluationStatus::success();
            },
            10.0);
    }

    static void add_variable_mass_energy_closure(
        DaeEquationSystemBuilder& system,
        const std::string& name,
        std::shared_ptr<const physics::PropertyPackage> properties,
        std::size_t energy,
        std::size_t mass,
        std::size_t pressure,
        std::size_t enthalpy) {
        system.add_sparse_equation(
            name, {energy, mass, pressure, enthalpy},
            [properties = std::move(properties), energy, mass,
             pressure, enthalpy](
                double, const std::vector<double>& x,
                const std::vector<double>&, double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                const auto state =
                    physics::state_ph_derivatives_with_fallback(
                        *properties, x.at(pressure), x.at(enthalpy));
                if (!state.ok()) return property_failure(state);
                residual = x.at(energy) - x.at(mass) *
                    state.state.internal_energy_j_kg;
                jacobian.push_back({energy, 1.0, 0.0});
                jacobian.push_back({
                    mass, -state.state.internal_energy_j_kg, 0.0});
                jacobian.push_back({
                    pressure,
                    -x.at(mass) * state.derivatives
                        .internal_energy_wrt_pressure_at_enthalpy,
                    0.0});
                jacobian.push_back({
                    enthalpy,
                    -x.at(mass) * state.derivatives
                        .internal_energy_wrt_enthalpy_at_pressure,
                    0.0});
                return EvaluationStatus::success();
            },
            1.0e6);
    }

    static void add_steady_inventory_closure(
        EquationSystemBuilder& system,
        const std::string& name,
        std::shared_ptr<const physics::PropertyPackage> properties,
        double volume,
        std::size_t inventory_mass,
        std::size_t pressure,
        std::size_t enthalpy) {
        system.add_checked_sparse_equation(
            name,
            [properties, volume, inventory_mass, pressure, enthalpy](
                const std::vector<double>& x, double& residual) {
                const auto state = properties->state_ph(
                    x.at(pressure), x.at(enthalpy));
                if (!state.ok()) return property_failure(state);
                residual = x.at(inventory_mass) -
                    volume * state.state.density_kg_m3;
                return EvaluationStatus::success();
            },
            {inventory_mass, pressure, enthalpy},
            [properties, volume, inventory_mass, pressure, enthalpy](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                const auto state =
                    physics::state_ph_derivatives_with_fallback(
                        *properties, x.at(pressure), x.at(enthalpy));
                if (!state.ok()) {
                    throw std::runtime_error(state.message);
                }
                jacobian.push_back({inventory_mass, 1.0});
                jacobian.push_back({
                    pressure,
                    -volume * state.derivatives
                        .density_wrt_pressure_at_enthalpy});
                jacobian.push_back({
                    enthalpy,
                    -volume * state.derivatives
                        .density_wrt_enthalpy_at_pressure});
                return x.at(inventory_mass) -
                    volume * state.state.density_kg_m3;
            },
            10.0);
    }

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
    bool finite_volume_{false};
    bool correlated_heat_transfer_{false};
};

}  // namespace

void register_dynamic_heat_transfer_component_models(
    ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<DynamicHeatExchangerCellModel>(false));
    registry.register_model(
        std::make_shared<DynamicHeatExchangerCellModel>(true));
    registry.register_model(
        std::make_shared<DynamicHeatExchangerCellModel>(true, true));
}

}  // namespace thermox::platform
