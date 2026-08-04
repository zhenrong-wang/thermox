#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
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
    const std::vector<double>&, double&)>;

void add_numeric_sparse_dae_equation(
    DaeEquationSystemBuilder& system,
    std::string name,
    std::vector<std::size_t> variables,
    NumericDaeResidual evaluate,
    double scale) {
    std::sort(variables.begin(), variables.end());
    variables.erase(
        std::unique(variables.begin(), variables.end()),
        variables.end());
    const auto sparsity = variables;
    system.add_sparse_equation(
        std::move(name), sparsity,
        [variables = std::move(variables),
         evaluate = std::move(evaluate)](
            double, const std::vector<double>& x,
            const std::vector<double>&, double& residual,
            std::vector<DaeEquationPartial>& jacobian) {
            auto status = evaluate(x, residual);
            if (!status.ok()) return status;
            for (const auto variable : variables) {
                std::vector<double> perturbed = x;
                const double step = 1.0e-6 *
                    std::max(std::abs(x.at(variable)), 1.0);
                perturbed.at(variable) += step;
                double shifted = 0.0;
                status = evaluate(perturbed, shifted);
                if (status.ok()) {
                    jacobian.push_back(
                        {variable, (shifted - residual) / step, 0.0});
                    continue;
                }
                perturbed.at(variable) = x.at(variable) - step;
                status = evaluate(perturbed, shifted);
                if (!status.ok()) return status;
                jacobian.push_back(
                    {variable, (residual - shifted) / step, 0.0});
            }
            return EvaluationStatus::success();
        },
        scale);
}

class RigidFluidVolumeModel final
    : public ComponentModel {
public:
    explicit RigidFluidVolumeModel(bool heat_transfer)
        : heat_transfer_(heat_transfer) {
        descriptor_.kind = heat_transfer_
            ? "volume.fluid.rigid_heat_transfer"
            : "volume.fluid.rigid_adiabatic";
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "volume.fluid.rigid";
        descriptor_.display_name = heat_transfer_
            ? "Rigid fluid volume with heat transfer"
            : "Adiabatic rigid fluid volume";
        descriptor_.category = "Fluid inventory";
        descriptor_.model_name =
            "Regime-spanning pressure-enthalpy inventory";
        descriptor_.ports = {
            {"inlet", "fluid", "in"},
            {"outlet", "fluid", "out"}};
        if (heat_transfer_) {
            descriptor_.ports.push_back(
                {"heat", "heat", "in"});
        }
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
             std::numeric_limits<double>::infinity(), "mass"},
            {"total_energy", DaeVariableKind::differential,
             200000.0, 1.0e6, 0.0, 1.0e5,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(), "energy"},
            {"pressure", DaeVariableKind::algebraic,
             101325.0, 100000.0, 0.0, 100000.0, 1.0,
             std::numeric_limits<double>::infinity(), "pressure"},
            {"enthalpy", DaeVariableKind::algebraic,
             300000.0, 100000.0, 0.0, 100000.0,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(),
             "specific_enthalpy"}};
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
        const auto heat_flow = heat_transfer_
            ? std::optional<std::size_t>(require_port_variable(
                  context, "heat.Q_dot"))
            : std::nullopt;
        const auto heat_temperature = heat_transfer_
            ? std::optional<std::size_t>(require_port_variable(
                  context, "heat.T"))
            : std::nullopt;
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_accumulation",
            {{mass, 0.0, 1.0}, {inlet_m, -1.0, 0.0},
             {outlet_m, 1.0, 0.0}},
            0.0, 100.0);
        auto energy_pattern = std::vector<std::size_t>{
            energy, inlet_m, inlet_h, outlet_m, outlet_h};
        if (heat_flow) energy_pattern.push_back(*heat_flow);
        system.add_sparse_equation(
            prefix + "energy_accumulation",
            energy_pattern,
            [energy, inlet_m, inlet_h, outlet_m, outlet_h,
             heat_flow](
                double, const std::vector<double>& x,
                const std::vector<double>& x_dot, double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                residual = x_dot.at(energy) -
                           x.at(inlet_m) * x.at(inlet_h) +
                           x.at(outlet_m) * x.at(outlet_h);
                if (heat_flow) residual -= x.at(*heat_flow);
                jacobian.push_back({energy, 0.0, 1.0});
                jacobian.push_back(
                    {inlet_m, -x.at(inlet_h), 0.0});
                jacobian.push_back(
                    {inlet_h, -x.at(inlet_m), 0.0});
                jacobian.push_back(
                    {outlet_m, x.at(outlet_h), 0.0});
                jacobian.push_back(
                    {outlet_h, x.at(outlet_m), 0.0});
                if (heat_flow) {
                    jacobian.push_back(
                        {*heat_flow, -1.0, 0.0});
                }
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
        if (heat_temperature) {
            system.add_sparse_equation(
                prefix + "heat_surface_temperature",
                {*heat_temperature, pressure, enthalpy},
                [properties, heat_temperature = *heat_temperature,
                 pressure, enthalpy](
                    double, const std::vector<double>& x,
                    const std::vector<double>&, double& residual,
                    std::vector<DaeEquationPartial>& jacobian) {
                    const auto state =
                        physics::state_ph_derivatives_with_fallback(
                            *properties, x.at(pressure),
                            x.at(enthalpy));
                    if (!state.ok()) return property_failure(state);
                    residual = x.at(heat_temperature) -
                        state.state.temperature_k;
                    jacobian.push_back(
                        {heat_temperature, 1.0, 0.0});
                    jacobian.push_back({
                        pressure,
                        -state.derivatives
                            .temperature_wrt_pressure_at_enthalpy,
                        0.0});
                    jacobian.push_back({
                        enthalpy,
                        -state.derivatives
                            .temperature_wrt_enthalpy_at_pressure,
                        0.0});
                    return EvaluationStatus::success();
                },
                1000.0);
        }
        system.add_sparse_equation(
            prefix + "volume_closure",
            {mass, pressure, enthalpy},
            [properties, volume, mass, pressure, enthalpy](
                double, const std::vector<double>& x,
                const std::vector<double>&, double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                const auto state =
                    physics::state_ph_derivatives_with_fallback(
                        *properties, x.at(pressure),
                        x.at(enthalpy));
                if (!state.ok()) return property_failure(state);
                residual =
                    x.at(mass) -
                    volume * state.state.density_kg_m3;
                jacobian.push_back({mass, 1.0, 0.0});
                jacobian.push_back(
                    {pressure,
                     -volume *
                         state.derivatives
                             .density_wrt_pressure_at_enthalpy,
                     0.0});
                jacobian.push_back(
                    {enthalpy,
                     -volume *
                         state.derivatives
                             .density_wrt_enthalpy_at_pressure,
                     0.0});
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
                const auto state =
                    physics::state_ph_derivatives_with_fallback(
                        *properties, x.at(pressure),
                        x.at(enthalpy));
                if (!state.ok()) return property_failure(state);
                residual =
                    x.at(energy) -
                    x.at(mass) *
                        state.state.internal_energy_j_kg;
                jacobian.push_back({energy, 1.0, 0.0});
                jacobian.push_back(
                    {mass, -state.state.internal_energy_j_kg, 0.0});
                jacobian.push_back(
                    {pressure,
                     -x.at(mass) *
                         state.derivatives
                             .internal_energy_wrt_pressure_at_enthalpy,
                     0.0});
                jacobian.push_back(
                    {enthalpy,
                     -x.at(mass) *
                         state.derivatives
                             .internal_energy_wrt_enthalpy_at_pressure,
                     0.0});
                return EvaluationStatus::success();
            },
            1.0e6);
    }

private:
    ComponentModelDescriptor descriptor_;
    bool heat_transfer_{false};
};

class CorrelatedTwoPhaseFluidVolumeModel final
    : public ComponentModel {
public:
    CorrelatedTwoPhaseFluidVolumeModel() {
        descriptor_.kind =
            "volume.fluid.equilibrium_two_phase_correlated_outlet";
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "volume.fluid.rigid";
        descriptor_.display_name =
            "Two-phase volume with correlated outlet slip";
        descriptor_.category = "Fluid inventory";
        descriptor_.model_name =
            "Equilibrium holdup with correlation-driven flow quality";
        descriptor_.ports = {
            {"inlet", "fluid", "in"},
            {"outlet", "fluid", "out"},
            {"heat", "heat", "in"}};
        descriptor_.parameters = {
            {"volume", "volume", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"flow_diameter", "length", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true}};
        descriptor_.artifacts = {
            {"void_fraction_correlation",
             correlation_artifact_type, true}};
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::saturation_p};
        descriptor_.supports_steady = false;
        descriptor_.supports_transient = true;
        descriptor_.internal_variables = {
            {"mass", DaeVariableKind::differential,
             1.0, 10.0, 0.0, 1.0, 1.0e-12,
             std::numeric_limits<double>::infinity(), "mass"},
            {"total_energy", DaeVariableKind::differential,
             1.0e6, 1.0e7, 0.0, 1.0e6,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(), "energy"},
            {"pressure", DaeVariableKind::algebraic,
             1.0e6, 1.0e5, 0.0, 1.0e5, 1.0,
             std::numeric_limits<double>::infinity(), "pressure"},
            {"holdup_quality", DaeVariableKind::algebraic,
             0.5, 1.0, 0.0, 1.0, 1.0e-9, 1.0 - 1.0e-9,
             "dimensionless"},
            {"void_fraction", DaeVariableKind::algebraic,
             0.5, 1.0, 0.0, 1.0, 1.0e-9, 1.0 - 1.0e-9,
             "dimensionless"},
            {"outlet_quality", DaeVariableKind::algebraic,
             0.5, 1.0, 0.0, 1.0, 1.0e-9, 1.0 - 1.0e-9,
             "dimensionless"}};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext&,
        EquationSystemBuilder&) const override {
        throw std::logic_error(
            "correlated two-phase fluid volume is transient-only");
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        const auto data = compile_data(context);
        const std::string prefix =
            "component." + context.component.id + ".";
        system.add_linear_equation(
            prefix + "mass_accumulation",
            {{data.mass, 0.0, 1.0},
             {data.inlet_m, -1.0, 0.0},
             {data.outlet_m, 1.0, 0.0}},
            0.0, 100.0);
        system.add_sparse_equation(
            prefix + "energy_accumulation",
            {data.energy, data.inlet_m, data.inlet_h,
             data.outlet_m, data.outlet_h, data.heat_flow},
            [data](double, const std::vector<double>& x,
                   const std::vector<double>& x_dot,
                   double& residual,
                   std::vector<DaeEquationPartial>& jacobian) {
                residual = x_dot.at(data.energy) -
                    x.at(data.inlet_m) * x.at(data.inlet_h) +
                    x.at(data.outlet_m) * x.at(data.outlet_h) -
                    x.at(data.heat_flow);
                jacobian.push_back({data.energy, 0.0, 1.0});
                jacobian.push_back(
                    {data.inlet_m, -x.at(data.inlet_h), 0.0});
                jacobian.push_back(
                    {data.inlet_h, -x.at(data.inlet_m), 0.0});
                jacobian.push_back(
                    {data.outlet_m, x.at(data.outlet_h), 0.0});
                jacobian.push_back(
                    {data.outlet_h, x.at(data.outlet_m), 0.0});
                jacobian.push_back(
                    {data.heat_flow, -1.0, 0.0});
                return EvaluationStatus::success();
            },
            1.0e6);
        system.add_linear_equation(
            prefix + "outlet_pressure",
            {{data.outlet_p, 1.0, 0.0},
             {data.pressure, -1.0, 0.0}},
            0.0, 100000.0);
        add_numeric_sparse_dae_equation(
            system, prefix + "outlet_flow_enthalpy",
            {data.pressure, data.outlet_quality, data.outlet_h},
            [data](const std::vector<double>& x, double& residual) {
                const auto saturation = data.properties->saturation_p(
                    x.at(data.pressure));
                if (!saturation.ok()) return property_failure(saturation);
                const double quality = x.at(data.outlet_quality);
                residual = x.at(data.outlet_h) -
                    ((1.0 - quality) *
                         saturation.liquid.enthalpy_j_kg +
                     quality * saturation.vapor.enthalpy_j_kg);
                return EvaluationStatus::success();
            },
            1.0e5);
        add_numeric_sparse_dae_equation(
            system, prefix + "heat_surface_temperature",
            {data.pressure, data.heat_temperature},
            [data](const std::vector<double>& x, double& residual) {
                const auto saturation = data.properties->saturation_p(
                    x.at(data.pressure));
                if (!saturation.ok()) return property_failure(saturation);
                residual = x.at(data.heat_temperature) -
                    saturation.liquid.temperature_k;
                return EvaluationStatus::success();
            },
            1000.0);
        add_numeric_sparse_dae_equation(
            system, prefix + "rigid_volume_closure",
            {data.mass, data.pressure, data.holdup_quality},
            [data](const std::vector<double>& x, double& residual) {
                const auto saturation = data.properties->saturation_p(
                    x.at(data.pressure));
                if (!saturation.ok()) return property_failure(saturation);
                const double quality = x.at(data.holdup_quality);
                const double specific_volume =
                    (1.0 - quality) /
                        saturation.liquid.density_kg_m3 +
                    quality / saturation.vapor.density_kg_m3;
                residual = x.at(data.mass) * specific_volume -
                    data.volume;
                return EvaluationStatus::success();
            },
            1.0);
        add_numeric_sparse_dae_equation(
            system, prefix + "inventory_energy_closure",
            {data.energy, data.mass, data.pressure,
             data.holdup_quality},
            [data](const std::vector<double>& x, double& residual) {
                const auto saturation = data.properties->saturation_p(
                    x.at(data.pressure));
                if (!saturation.ok()) return property_failure(saturation);
                const double quality = x.at(data.holdup_quality);
                const double internal_energy =
                    (1.0 - quality) *
                        saturation.liquid.internal_energy_j_kg +
                    quality *
                        saturation.vapor.internal_energy_j_kg;
                residual = x.at(data.energy) -
                    x.at(data.mass) * internal_energy;
                return EvaluationStatus::success();
            },
            1.0e6);
        add_numeric_sparse_dae_equation(
            system, prefix + "holdup_void_fraction",
            {data.pressure, data.holdup_quality,
             data.void_fraction},
            [data](const std::vector<double>& x, double& residual) {
                const auto saturation = data.properties->saturation_p(
                    x.at(data.pressure));
                if (!saturation.ok()) return property_failure(saturation);
                const double rho_l = saturation.liquid.density_kg_m3;
                const double rho_v = saturation.vapor.density_kg_m3;
                const double quality = x.at(data.holdup_quality);
                const double specific_volume =
                    (1.0 - quality) / rho_l + quality / rho_v;
                residual = x.at(data.void_fraction) -
                    (quality / rho_v) / specific_volume;
                return EvaluationStatus::success();
            },
            1.0);
        add_numeric_sparse_dae_equation(
            system, prefix + "correlated_outlet_slip",
            {data.pressure, data.void_fraction,
             data.outlet_quality, data.outlet_m},
            [data](const std::vector<double>& x, double& residual) {
                const auto saturation = data.properties->saturation_p(
                    x.at(data.pressure));
                if (!saturation.ok()) return property_failure(saturation);
                const double rho_l =
                    saturation.liquid.density_kg_m3;
                const double rho_v =
                    saturation.vapor.density_kg_m3;
                std::map<std::string, double> inputs;
                for (const auto& input : data.correlation->inputs()) {
                    if (input.name == "vapor_quality") {
                        inputs.emplace(
                            input.name, x.at(data.outlet_quality));
                    } else if (input.name == "liquid_density") {
                        inputs.emplace(input.name, rho_l);
                    } else if (input.name == "vapor_density") {
                        inputs.emplace(input.name, rho_v);
                    } else if (input.name == "mass_flow") {
                        inputs.emplace(input.name, x.at(data.outlet_m));
                    } else if (input.name == "area") {
                        inputs.emplace(input.name, data.area);
                    } else if (input.name == "diameter") {
                        inputs.emplace(input.name, data.diameter);
                    } else if (input.name == "pressure") {
                        inputs.emplace(input.name, x.at(data.pressure));
                    }
                }
                const auto evaluated = data.correlation->evaluate(inputs);
                if (!evaluated.error.empty()) {
                    return EvaluationStatus::recoverable(
                        evaluated.error);
                }
                if (!std::isfinite(evaluated.value) ||
                    evaluated.value <= 0.0 ||
                    evaluated.value >= 1.0) {
                    return EvaluationStatus::recoverable(
                        "two-phase inventory void-fraction correlation "
                        "must produce 0 < alpha < 1");
                }
                residual = x.at(data.void_fraction) - evaluated.value;
                return EvaluationStatus::success();
            },
            1.0);
    }

private:
    struct Data {
        std::shared_ptr<const physics::PropertyPackage> properties;
        std::shared_ptr<const CorrelationArtifact> correlation;
        std::size_t inlet_m{}, inlet_h{};
        std::size_t outlet_m{}, outlet_p{}, outlet_h{};
        std::size_t heat_flow{}, heat_temperature{};
        std::size_t mass{}, energy{}, pressure{};
        std::size_t holdup_quality{}, void_fraction{}, outlet_quality{};
        double volume{}, diameter{}, area{};
    };

    static Data compile_data(
        const ComponentCompileContext& context) {
        Data data;
        data.properties = require_property_package(context, "inlet");
        if (data.properties !=
            require_property_package(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' inlet and outlet must use the same medium");
        }
        data.correlation = require_correlation(
            context, "void_fraction_correlation");
        if (data.correlation->output().name != "void_fraction" ||
            data.correlation->output().dimension != "dimensionless") {
            throw std::invalid_argument(
                "two-phase inventory correlation output must be named "
                "'void_fraction' with dimensionless dimension");
        }
        const std::map<std::string, std::string> supported_inputs{
            {"vapor_quality", "dimensionless"},
            {"liquid_density", "density"},
            {"vapor_density", "density"},
            {"mass_flow", "mass_flow"},
            {"area", "area"},
            {"diameter", "length"},
            {"pressure", "pressure"}};
        for (const auto& input : data.correlation->inputs()) {
            const auto supported = supported_inputs.find(input.name);
            if (supported == supported_inputs.end() ||
                input.dimension != supported->second) {
                throw std::invalid_argument(
                    "two-phase inventory correlation input '" +
                    input.name + "' has unsupported name or dimension");
            }
        }
        data.inlet_m = require_port_variable(context, "inlet.m_dot");
        data.inlet_h = require_port_variable(context, "inlet.h");
        data.outlet_m = require_port_variable(context, "outlet.m_dot");
        data.outlet_p = require_port_variable(context, "outlet.p");
        data.outlet_h = require_port_variable(context, "outlet.h");
        data.heat_flow = require_port_variable(context, "heat.Q_dot");
        data.heat_temperature = require_port_variable(context, "heat.T");
        data.mass = require_internal_variable(context, "mass");
        data.energy = require_internal_variable(context, "total_energy");
        data.pressure = require_internal_variable(context, "pressure");
        data.holdup_quality = require_internal_variable(
            context, "holdup_quality");
        data.void_fraction = require_internal_variable(
            context, "void_fraction");
        data.outlet_quality = require_internal_variable(
            context, "outlet_quality");
        data.volume = required_parameter(context.component, "volume");
        data.diameter = required_parameter(
            context.component, "flow_diameter");
        data.area = std::numbers::pi * data.diameter *
            data.diameter / 4.0;
        return data;
    }

    ComponentModelDescriptor descriptor_;
};

}  // namespace

void register_fluid_inventory_component_models(
    ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<RigidFluidVolumeModel>(false));
    registry.register_model(
        std::make_shared<RigidFluidVolumeModel>(true));
    registry.register_model(
        std::make_shared<CorrelatedTwoPhaseFluidVolumeModel>());
}

}  // namespace thermox::platform
