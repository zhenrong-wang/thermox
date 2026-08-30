#include "component_modules.hpp"
#include "component_model_support.hpp"
#include "thermox/platform/semi_physical_volumetric_expander.hpp"

#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>

namespace thermox::platform {

namespace {

using component_model_support::property_failure;
using component_model_support::require_correlation;
using component_model_support::require_internal_variable;
using component_model_support::require_port_variable;
using component_model_support::require_property_package;
using component_model_support::required_parameter;

struct ExpanderClosures {
    double filling_factor{0.0};
    double fluid_isentropic_efficiency{0.0};
    double shaft_isentropic_efficiency{0.0};
};

using SemiPhysicalExpanderResult =
    SemiPhysicalVolumetricExpanderEvaluation;

EvaluationStatus find_isentropic_density_state(
    const physics::PropertyPackage& properties,
    const physics::ThermodynamicState& inlet,
    double target_density,
    physics::ThermodynamicState& state) {
    if (!std::isfinite(target_density) || target_density <= 0.0 ||
        target_density >= inlet.density_kg_m3) {
        return EvaluationStatus::recoverable(
            "volumetric expander built-in expansion requires a target "
            "density below the inlet density");
    }
    const double minimum_pressure = std::max(
        properties.limits().minimum_pressure_pa,
        inlet.pressure_pa * 1.0e-6);
    double upper_pressure = inlet.pressure_pa;
    physics::ThermodynamicState upper_state = inlet;
    double lower_pressure = upper_pressure;
    physics::ThermodynamicState lower_state;
    bool bracketed = false;
    for (int step = 1; step <= 60; ++step) {
        const double fraction = static_cast<double>(step) / 60.0;
        const double pressure = std::exp(
            std::log(inlet.pressure_pa) + fraction *
                (std::log(minimum_pressure) -
                 std::log(inlet.pressure_pa)));
        const auto candidate = properties.state_ps(
            pressure, inlet.entropy_j_kg_k);
        if (!candidate.ok()) continue;
        if (candidate.state.density_kg_m3 <= target_density) {
            lower_pressure = pressure;
            lower_state = candidate.state;
            bracketed = true;
            break;
        }
        upper_pressure = pressure;
        upper_state = candidate.state;
    }
    if (!bracketed) {
        return EvaluationStatus::recoverable(
            "volumetric expander could not bracket its built-in "
            "isentropic density state");
    }
    for (int iteration = 0; iteration < 48; ++iteration) {
        const double pressure = std::sqrt(
            lower_pressure * upper_pressure);
        const auto candidate = properties.state_ps(
            pressure, inlet.entropy_j_kg_k);
        if (!candidate.ok()) return property_failure(candidate);
        if (candidate.state.density_kg_m3 <= target_density) {
            lower_pressure = pressure;
            lower_state = candidate.state;
        } else {
            upper_pressure = pressure;
            upper_state = candidate.state;
        }
    }
    state = std::abs(lower_state.density_kg_m3 - target_density) <
            std::abs(upper_state.density_kg_m3 - target_density)
        ? lower_state
        : upper_state;
    return EvaluationStatus::success();
}

EvaluationStatus evaluate_semi_physical_expander(
    const physics::PropertyPackage& properties,
    double inlet_pressure,
    double inlet_enthalpy,
    double outlet_pressure,
    double angular_speed,
    double maximum_chamber_volume,
    double built_in_volume_ratio,
    double leakage_area,
    double leakage_discharge_coefficient,
    double mechanical_loss_at_reference_speed,
    double mechanical_loss_reference_angular_speed,
    double proportional_mechanical_loss,
    double ambient_heat_transfer_conductance,
    double ambient_temperature,
    SemiPhysicalExpanderResult& result) {
    if (!std::isfinite(outlet_pressure) || outlet_pressure <= 0.0 ||
        !std::isfinite(inlet_pressure) ||
        inlet_pressure <= outlet_pressure ||
        !std::isfinite(angular_speed) || angular_speed <= 0.0) {
        return EvaluationStatus::recoverable(
            "semi-physical volumetric expander requires positive speed "
            "and inlet pressure above positive outlet pressure");
    }
    const auto inlet = properties.state_ph(
        inlet_pressure, inlet_enthalpy);
    if (!inlet.ok()) return property_failure(inlet);
    physics::ThermodynamicState chamber;
    auto status = find_isentropic_density_state(
        properties, inlet.state,
        inlet.state.density_kg_m3 / built_in_volume_ratio,
        chamber);
    if (!status.ok()) return status;

    const double revolutions_per_second =
        angular_speed / (2.0 * std::numbers::pi);
    const double internal_mass_flow =
        inlet.state.density_kg_m3 *
        maximum_chamber_volume / built_in_volume_ratio *
        revolutions_per_second;
    double leakage_mass_flow = 0.0;
    if (leakage_area > 0.0 &&
        leakage_discharge_coefficient > 0.0) {
        const double gamma = inlet.state.cp_j_kg_k /
            inlet.state.cv_j_kg_k;
        if (!std::isfinite(gamma) || gamma <= 1.0) {
            return EvaluationStatus::recoverable(
                "semi-physical volumetric expander leakage model "
                "requires inlet cp/cv above one");
        }
        const double critical_pressure = inlet_pressure * std::pow(
            2.0 / (gamma + 1.0), gamma / (gamma - 1.0));
        const double leakage_outlet_pressure = std::max(
            outlet_pressure, critical_pressure);
        const auto leakage_outlet = properties.state_ps(
            leakage_outlet_pressure,
            inlet.state.entropy_j_kg_k);
        if (!leakage_outlet.ok()) {
            return property_failure(leakage_outlet);
        }
        const double leakage_enthalpy_drop =
            inlet_enthalpy -
            leakage_outlet.state.enthalpy_j_kg;
        if (!std::isfinite(leakage_enthalpy_drop) ||
            leakage_enthalpy_drop < 0.0) {
            return EvaluationStatus::recoverable(
                "semi-physical volumetric expander leakage nozzle "
                "has a negative isentropic enthalpy drop");
        }
        leakage_mass_flow = leakage_discharge_coefficient *
            leakage_area * leakage_outlet.state.density_kg_m3 *
            std::sqrt(2.0 * leakage_enthalpy_drop);
    }
    result.mass_flow = internal_mass_flow + leakage_mass_flow;
    result.built_in_pressure = chamber.pressure_pa;
    result.internal_mass_flow = internal_mass_flow;
    result.leakage_mass_flow = leakage_mass_flow;
    // Zero-clearance scroll reduction of the volumetric machine cycle:
    // trapped isentropic work plus constant-volume pressure equalization.
    const double indicated_specific_work =
        inlet_enthalpy - chamber.enthalpy_j_kg +
        (chamber.pressure_pa - outlet_pressure) /
            chamber.density_kg_m3;
    const double indicated_power =
        internal_mass_flow * indicated_specific_work;
    const double speed_ratio = angular_speed /
        mechanical_loss_reference_angular_speed;
    const double mechanical_loss =
        mechanical_loss_at_reference_speed * speed_ratio * speed_ratio +
        proportional_mechanical_loss * indicated_power;
    result.shaft_power = indicated_power - mechanical_loss;
    result.indicated_power = indicated_power;
    result.mechanical_loss_power = mechanical_loss;
    if (!std::isfinite(result.mass_flow) || result.mass_flow <= 0.0 ||
        !std::isfinite(indicated_power) || indicated_power <= 0.0 ||
        !std::isfinite(result.shaft_power) ||
        result.shaft_power <= 0.0) {
        return EvaluationStatus::recoverable(
            "semi-physical volumetric expander predicts nonpositive "
            "mass flow, indicated power, or shaft power");
    }
    const double representative_wall_temperature =
        0.5 * (inlet.state.temperature_k + chamber.temperature_k);
    const double ambient_heat_loss =
        ambient_heat_transfer_conductance *
        (representative_wall_temperature - ambient_temperature);
    result.rejected_heat = mechanical_loss + ambient_heat_loss;
    result.ambient_heat_loss = ambient_heat_loss;
    result.outlet_enthalpy = inlet_enthalpy -
        (indicated_power + ambient_heat_loss) / result.mass_flow;
    if (!std::isfinite(result.outlet_enthalpy) ||
        !std::isfinite(result.rejected_heat) ||
        result.rejected_heat < 0.0) {
        return EvaluationStatus::recoverable(
            "semi-physical volumetric expander produced an invalid "
            "outlet energy state or net heat uptake");
    }
    const auto outlet = properties.state_ph(
        outlet_pressure, result.outlet_enthalpy);
    if (!outlet.ok()) return property_failure(outlet);
    return EvaluationStatus::success();
}

const std::map<std::string, std::string>& closure_input_contract() {
    static const std::map<std::string, std::string> contract{
        {"pressure_ratio", "dimensionless"},
        {"pressure_drop", "pressure"},
        {"inlet_pressure", "pressure"},
        {"inlet_temperature", "temperature"},
        {"inlet_density", "density"},
        {"angular_speed", "angular_speed"},
    };
    return contract;
}

void validate_closure_correlation(
    const CorrelationArtifact& correlation,
    const std::string& role,
    const std::string& output_name) {
    if (correlation.output().name != output_name ||
        correlation.output().dimension != "dimensionless") {
        throw std::invalid_argument(
            role + " correlation output must be named '" +
            output_name + "' with dimensionless dimension");
    }
    for (const auto& input : correlation.inputs()) {
        const auto supported = closure_input_contract().find(input.name);
        if (supported == closure_input_contract().end() ||
            supported->second != input.dimension) {
            throw std::invalid_argument(
                role + " correlation input '" + input.name +
                "' has unsupported name or dimension");
        }
    }
}

EvaluationStatus evaluate_correlation(
    const CorrelationArtifact& correlation,
    const physics::ThermodynamicState& inlet,
    double inlet_pressure,
    double outlet_pressure,
    double angular_speed,
    double& value) {
    if (!std::isfinite(outlet_pressure) || outlet_pressure <= 0.0 ||
        !std::isfinite(inlet_pressure) ||
        inlet_pressure <= outlet_pressure ||
        !std::isfinite(angular_speed) || angular_speed <= 0.0) {
        return EvaluationStatus::recoverable(
            "volumetric expander requires positive speed and inlet "
            "pressure above positive outlet pressure");
    }
    const double pressure_ratio = inlet_pressure / outlet_pressure;
    std::map<std::string, double> inputs;
    for (const auto& input : correlation.inputs()) {
        if (input.name == "pressure_ratio") {
            inputs.emplace(input.name, pressure_ratio);
        } else if (input.name == "pressure_drop") {
            inputs.emplace(
                input.name, inlet_pressure - outlet_pressure);
        } else if (input.name == "inlet_pressure") {
            inputs.emplace(input.name, inlet_pressure);
        } else if (input.name == "inlet_temperature") {
            inputs.emplace(input.name, inlet.temperature_k);
        } else if (input.name == "inlet_density") {
            inputs.emplace(input.name, inlet.density_kg_m3);
        } else if (input.name == "angular_speed") {
            inputs.emplace(input.name, angular_speed);
        }
    }
    const auto evaluated = correlation.evaluate(inputs);
    if (!evaluated.error.empty()) {
        return EvaluationStatus::recoverable(evaluated.error);
    }
    value = evaluated.value;
    return EvaluationStatus::success();
}

EvaluationStatus evaluate_closures(
    const CorrelationArtifact& filling_factor,
    const CorrelationArtifact& fluid_efficiency,
    const CorrelationArtifact& shaft_efficiency,
    const physics::PropertyPackage& properties,
    double inlet_pressure,
    double inlet_enthalpy,
    double outlet_pressure,
    double angular_speed,
    ExpanderClosures& closures) {
    const auto inlet = properties.state_ph(
        inlet_pressure, inlet_enthalpy);
    if (!inlet.ok()) return property_failure(inlet);
    auto status = evaluate_correlation(
        filling_factor, inlet.state, inlet_pressure,
        outlet_pressure, angular_speed, closures.filling_factor);
    if (!status.ok()) return status;
    status = evaluate_correlation(
        fluid_efficiency, inlet.state, inlet_pressure,
        outlet_pressure, angular_speed,
        closures.fluid_isentropic_efficiency);
    if (!status.ok()) return status;
    status = evaluate_correlation(
        shaft_efficiency, inlet.state, inlet_pressure,
        outlet_pressure, angular_speed,
        closures.shaft_isentropic_efficiency);
    if (!status.ok()) return status;
    if (!std::isfinite(closures.filling_factor) ||
        closures.filling_factor <= 0.0) {
        return EvaluationStatus::recoverable(
            "volumetric expander filling factor must be finite and "
            "positive");
    }
    if (!std::isfinite(closures.fluid_isentropic_efficiency) ||
        closures.fluid_isentropic_efficiency <= 0.0 ||
        closures.fluid_isentropic_efficiency > 1.0) {
        return EvaluationStatus::recoverable(
            "volumetric expander fluid isentropic efficiency must be "
            "in (0, 1]");
    }
    if (!std::isfinite(closures.shaft_isentropic_efficiency) ||
        closures.shaft_isentropic_efficiency <= 0.0 ||
        closures.shaft_isentropic_efficiency >
            closures.fluid_isentropic_efficiency) {
        return EvaluationStatus::recoverable(
            "volumetric expander shaft isentropic efficiency must be "
            "in (0, fluid isentropic efficiency]");
    }
    return EvaluationStatus::success();
}

class CorrelatedVolumetricExpanderModel final : public ComponentModel {
public:
    CorrelatedVolumetricExpanderModel() {
        descriptor_.kind = "expander.fluid.volumetric_correlations";
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "expander";
        descriptor_.display_name = "Volumetric expander";
        descriptor_.category = "Turbomachinery";
        descriptor_.model_name =
            "Correlation-driven filling and efficiency";
        descriptor_.ports = {
            {"inlet", "fluid", "in"},
            {"outlet", "fluid", "out"},
            {"shaft", "shaft", "out"},
            {"rejected_heat", "heat", "out"},
        };
        descriptor_.parameters = {
            {"displacement_per_revolution", "volume", true,
             std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"rejected_heat_temperature", "temperature", false,
             300.0, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
        };
        descriptor_.artifacts = {
            {"filling_factor_correlation",
             correlation_artifact_type, true},
            {"fluid_efficiency_correlation",
             correlation_artifact_type, true},
            {"shaft_efficiency_correlation",
             correlation_artifact_type, true},
        };
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph,
            physics::PropertyCapability::state_ps,
        };
        descriptor_.supports_transient = true;
        descriptor_.uses_quasi_steady_transient_equations = true;
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto properties =
            require_property_package(context, "inlet");
        if (properties !=
            require_property_package(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' inlet and outlet must use the same medium");
        }
        const auto filling_factor = require_correlation(
            context, "filling_factor_correlation");
        const auto fluid_efficiency = require_correlation(
            context, "fluid_efficiency_correlation");
        const auto shaft_efficiency = require_correlation(
            context, "shaft_efficiency_correlation");
        validate_closure_correlation(
            *filling_factor, "filling-factor", "filling_factor");
        validate_closure_correlation(
            *fluid_efficiency, "fluid-efficiency",
            "fluid_isentropic_efficiency");
        validate_closure_correlation(
            *shaft_efficiency, "shaft-efficiency",
            "shaft_isentropic_efficiency");
        const double displacement = required_parameter(
            context.component, "displacement_per_revolution");
        const double rejected_heat_temperature = required_parameter(
            context.component, "rejected_heat_temperature");

        const auto inlet_m =
            require_port_variable(context, "inlet.m_dot");
        const auto inlet_p =
            require_port_variable(context, "inlet.p");
        const auto inlet_h =
            require_port_variable(context, "inlet.h");
        const auto outlet_m =
            require_port_variable(context, "outlet.m_dot");
        const auto outlet_p =
            require_port_variable(context, "outlet.p");
        const auto outlet_h =
            require_port_variable(context, "outlet.h");
        const auto shaft_w =
            require_port_variable(context, "shaft.W_dot");
        const auto shaft_omega =
            require_port_variable(context, "shaft.omega");
        const auto rejected_heat =
            require_port_variable(context, "rejected_heat.Q_dot");
        const auto rejected_temperature =
            require_port_variable(context, "rejected_heat.T");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_continuity",
            {{outlet_m, 1.0}, {inlet_m, -1.0}},
            0.0, 0.1);
        system.add_checked_equation(
            prefix + "volumetric_capacity",
            [properties, filling_factor, fluid_efficiency,
             shaft_efficiency, displacement, inlet_m, inlet_p,
             inlet_h, outlet_p, shaft_omega](
                const std::vector<double>& x, double& residual) {
                ExpanderClosures closures;
                const auto status = evaluate_closures(
                    *filling_factor, *fluid_efficiency,
                    *shaft_efficiency, *properties,
                    x.at(inlet_p), x.at(inlet_h),
                    x.at(outlet_p), x.at(shaft_omega), closures);
                if (!status.ok()) return status;
                const auto inlet = properties->state_ph(
                    x.at(inlet_p), x.at(inlet_h));
                if (!inlet.ok()) return property_failure(inlet);
                residual = x.at(inlet_m) -
                    closures.filling_factor *
                        inlet.state.density_kg_m3 * displacement *
                        x.at(shaft_omega) /
                        (2.0 * std::numbers::pi);
                return EvaluationStatus::success();
            },
            0.1);
        system.add_checked_equation(
            prefix + "fluid_isentropic_efficiency",
            [properties, filling_factor, fluid_efficiency,
             shaft_efficiency, inlet_p, inlet_h, outlet_p,
             outlet_h, shaft_omega](
                const std::vector<double>& x, double& residual) {
                ExpanderClosures closures;
                auto status = evaluate_closures(
                    *filling_factor, *fluid_efficiency,
                    *shaft_efficiency, *properties,
                    x.at(inlet_p), x.at(inlet_h),
                    x.at(outlet_p), x.at(shaft_omega), closures);
                if (!status.ok()) return status;
                const auto inlet = properties->state_ph(
                    x.at(inlet_p), x.at(inlet_h));
                if (!inlet.ok()) return property_failure(inlet);
                const auto isentropic = properties->state_ps(
                    x.at(outlet_p), inlet.state.entropy_j_kg_k);
                if (!isentropic.ok()) {
                    return property_failure(isentropic);
                }
                residual = x.at(outlet_h) - x.at(inlet_h) -
                    closures.fluid_isentropic_efficiency *
                        (isentropic.state.enthalpy_j_kg -
                         x.at(inlet_h));
                return EvaluationStatus::success();
            },
            1.0e5);
        system.add_checked_equation(
            prefix + "shaft_power",
            [properties, filling_factor, fluid_efficiency,
             shaft_efficiency, inlet_m, inlet_p, inlet_h,
             outlet_p, shaft_w, shaft_omega](
                const std::vector<double>& x, double& residual) {
                ExpanderClosures closures;
                auto status = evaluate_closures(
                    *filling_factor, *fluid_efficiency,
                    *shaft_efficiency, *properties,
                    x.at(inlet_p), x.at(inlet_h),
                    x.at(outlet_p), x.at(shaft_omega), closures);
                if (!status.ok()) return status;
                const auto inlet = properties->state_ph(
                    x.at(inlet_p), x.at(inlet_h));
                if (!inlet.ok()) return property_failure(inlet);
                const auto isentropic = properties->state_ps(
                    x.at(outlet_p), inlet.state.entropy_j_kg_k);
                if (!isentropic.ok()) {
                    return property_failure(isentropic);
                }
                residual = x.at(shaft_w) - x.at(inlet_m) *
                    closures.shaft_isentropic_efficiency *
                    (x.at(inlet_h) -
                     isentropic.state.enthalpy_j_kg);
                return EvaluationStatus::success();
            },
            1.0e3);
        system.add_sparse_equation(
            prefix + "loss_energy_balance",
            {inlet_m, inlet_h, outlet_h, shaft_w, rejected_heat},
            [inlet_m, inlet_h, outlet_h, shaft_w, rejected_heat](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                const double fluid_power =
                    x.at(inlet_h) - x.at(outlet_h);
                jacobian.push_back({inlet_m, fluid_power});
                jacobian.push_back({inlet_h, x.at(inlet_m)});
                jacobian.push_back({outlet_h, -x.at(inlet_m)});
                jacobian.push_back({shaft_w, -1.0});
                jacobian.push_back({rejected_heat, -1.0});
                return x.at(inlet_m) * fluid_power -
                    x.at(shaft_w) - x.at(rejected_heat);
            },
            1.0e3);
        system.add_linear_equation(
            prefix + "rejected_heat_temperature",
            {{rejected_temperature, 1.0}},
            rejected_heat_temperature, 100.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class SemiPhysicalVolumetricExpanderModel final : public ComponentModel {
public:
    SemiPhysicalVolumetricExpanderModel() {
        descriptor_.kind = "expander.fluid.semi_physical_volumetric";
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "expander";
        descriptor_.display_name = "Semi-physical volumetric expander";
        descriptor_.category = "Turbomachinery";
        descriptor_.model_name =
            "Built-in expansion with leakage and thermal/mechanical losses";
        descriptor_.ports = {
            {"inlet", "fluid", "in"},
            {"outlet", "fluid", "out"},
            {"shaft", "shaft", "out"},
            {"rejected_heat", "heat", "out"},
        };
        descriptor_.parameters = {
            {"maximum_chamber_volume_per_revolution", "volume", true,
             std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"built_in_volume_ratio", "dimensionless", true,
             std::nullopt, 1.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"leakage_area", "area", false, 0.0, 0.0,
             std::numeric_limits<double>::infinity(), true, true},
            {"leakage_discharge_coefficient", "dimensionless", false,
             1.0, 0.0, 1.0, true, true},
            {"mechanical_loss_at_reference_speed", "power", false,
             0.0, 0.0,
             std::numeric_limits<double>::infinity(), true, true},
            {"mechanical_loss_reference_angular_speed", "angular_speed",
             true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"proportional_mechanical_loss", "dimensionless", false,
             0.0, 0.0, 1.0, true, false},
            {"ambient_heat_transfer_conductance", "thermal_conductance",
             false, 0.0, 0.0,
             std::numeric_limits<double>::infinity(), true, true},
            {"ambient_temperature", "temperature", false, 300.0, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
        };
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph,
            physics::PropertyCapability::state_ps,
        };
        descriptor_.internal_variables = {
            {"built_in_pressure", DaeVariableKind::algebraic,
             2.0e5, 1.0e6, 0.0, 1.0, 0.0,
             std::numeric_limits<double>::infinity(), "pressure"},
            {"internal_mass_flow", DaeVariableKind::algebraic,
             0.01, 0.1, 0.0, 1.0, 0.0,
             std::numeric_limits<double>::infinity(), "mass_flow"},
            {"leakage_mass_flow", DaeVariableKind::algebraic,
             0.001, 0.1, 0.0, 1.0, 0.0,
             std::numeric_limits<double>::infinity(), "mass_flow"},
            {"indicated_power", DaeVariableKind::algebraic,
             500.0, 1.0e3, 0.0, 1.0, 0.0,
             std::numeric_limits<double>::infinity(), "power"},
            {"mechanical_loss_power", DaeVariableKind::algebraic,
             50.0, 1.0e3, 0.0, 1.0, 0.0,
             std::numeric_limits<double>::infinity(), "power"},
            {"ambient_heat_loss", DaeVariableKind::algebraic,
             50.0, 1.0e3, 0.0, 1.0,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(), "power"},
        };
        descriptor_.supports_transient = true;
        descriptor_.uses_quasi_steady_transient_equations = true;
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto properties =
            require_property_package(context, "inlet");
        if (properties !=
            require_property_package(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' inlet and outlet must use the same medium");
        }
        const double maximum_chamber_volume = required_parameter(
            context.component,
            "maximum_chamber_volume_per_revolution");
        const double built_in_volume_ratio = required_parameter(
            context.component, "built_in_volume_ratio");
        const double leakage_area = required_parameter(
            context.component, "leakage_area");
        const double leakage_discharge_coefficient = required_parameter(
            context.component, "leakage_discharge_coefficient");
        const double mechanical_loss_at_reference_speed =
            required_parameter(
                context.component,
                "mechanical_loss_at_reference_speed");
        const double mechanical_loss_reference_angular_speed =
            required_parameter(
                context.component,
                "mechanical_loss_reference_angular_speed");
        const double proportional_mechanical_loss = required_parameter(
            context.component, "proportional_mechanical_loss");
        const double ambient_heat_transfer_conductance =
            required_parameter(
                context.component,
                "ambient_heat_transfer_conductance");
        const double ambient_temperature = required_parameter(
            context.component, "ambient_temperature");
        const auto inlet_m =
            require_port_variable(context, "inlet.m_dot");
        const auto inlet_p =
            require_port_variable(context, "inlet.p");
        const auto inlet_h =
            require_port_variable(context, "inlet.h");
        const auto outlet_m =
            require_port_variable(context, "outlet.m_dot");
        const auto outlet_p =
            require_port_variable(context, "outlet.p");
        const auto outlet_h =
            require_port_variable(context, "outlet.h");
        const auto shaft_w =
            require_port_variable(context, "shaft.W_dot");
        const auto shaft_omega =
            require_port_variable(context, "shaft.omega");
        const auto rejected_heat =
            require_port_variable(context, "rejected_heat.Q_dot");
        const auto rejected_temperature =
            require_port_variable(context, "rejected_heat.T");
        const auto built_in_pressure = require_internal_variable(
            context, "built_in_pressure");
        const auto internal_mass_flow = require_internal_variable(
            context, "internal_mass_flow");
        const auto leakage_mass_flow = require_internal_variable(
            context, "leakage_mass_flow");
        const auto indicated_power = require_internal_variable(
            context, "indicated_power");
        const auto mechanical_loss_power = require_internal_variable(
            context, "mechanical_loss_power");
        const auto ambient_heat_loss = require_internal_variable(
            context, "ambient_heat_loss");
        const auto raw_evaluate =
            [properties, inlet_p, inlet_h, outlet_p, shaft_omega,
             parameters = SemiPhysicalVolumetricExpanderParameters{
                 maximum_chamber_volume, built_in_volume_ratio,
                 leakage_area, leakage_discharge_coefficient,
                 mechanical_loss_at_reference_speed,
                 mechanical_loss_reference_angular_speed,
                 proportional_mechanical_loss,
                 ambient_heat_transfer_conductance,
                 ambient_temperature}](
                const std::vector<double>& x,
                SemiPhysicalExpanderResult& result) {
                return evaluate_semi_physical_volumetric_expander(
                    *properties, x.at(inlet_p), x.at(inlet_h),
                    x.at(outlet_p), x.at(shaft_omega),
                    parameters, result);
            };
        const std::vector<std::size_t> evaluator_dependencies{
            inlet_p, inlet_h, outlet_p, shaft_omega};
        const auto evaluate = component_model_support::
            memoize_component_evaluation<SemiPhysicalExpanderResult>(
                raw_evaluate, evaluator_dependencies);
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_continuity",
            {{outlet_m, 1.0}, {inlet_m, -1.0}},
            0.0, 0.1);
        component_model_support::add_numeric_checked_sparse_equation(
            system,
            prefix + "semi_physical_mass_capacity",
            [evaluate, inlet_m](
                const std::vector<double>& x, double& residual) {
                SemiPhysicalExpanderResult result;
                const auto status = evaluate(x, result);
                if (!status.ok()) return status;
                residual = x.at(inlet_m) - result.mass_flow;
                return EvaluationStatus::success();
            }, {inlet_m, inlet_p, inlet_h, outlet_p, shaft_omega},
            0.1);
        component_model_support::add_numeric_checked_sparse_equation(
            system,
            prefix + "semi_physical_outlet_energy",
            [evaluate, outlet_h](
                const std::vector<double>& x, double& residual) {
                SemiPhysicalExpanderResult result;
                const auto status = evaluate(x, result);
                if (!status.ok()) return status;
                residual = x.at(outlet_h) - result.outlet_enthalpy;
                return EvaluationStatus::success();
            }, {outlet_h, inlet_p, inlet_h, outlet_p, shaft_omega},
            1.0e5);
        component_model_support::add_numeric_checked_sparse_equation(
            system,
            prefix + "semi_physical_shaft_power",
            [evaluate, shaft_w](
                const std::vector<double>& x, double& residual) {
                SemiPhysicalExpanderResult result;
                const auto status = evaluate(x, result);
                if (!status.ok()) return status;
                residual = x.at(shaft_w) - result.shaft_power;
                return EvaluationStatus::success();
            }, {shaft_w, inlet_p, inlet_h, outlet_p, shaft_omega},
            1.0e3);
        component_model_support::add_numeric_checked_sparse_equation(
            system,
            prefix + "semi_physical_rejected_heat",
            [evaluate, rejected_heat](
                const std::vector<double>& x, double& residual) {
                SemiPhysicalExpanderResult result;
                const auto status = evaluate(x, result);
                if (!status.ok()) return status;
                residual = x.at(rejected_heat) - result.rejected_heat;
                return EvaluationStatus::success();
            }, {rejected_heat, inlet_p, inlet_h, outlet_p, shaft_omega},
            1.0e3);
        const auto add_diagnostic_equation = [
            &system, &prefix, evaluate, evaluator_dependencies](
            const std::string& name,
            std::size_t variable,
            double SemiPhysicalExpanderResult::*member,
            double scale) {
            auto dependencies = evaluator_dependencies;
            dependencies.push_back(variable);
            component_model_support::add_numeric_checked_sparse_equation(
                system,
                prefix + name,
                [evaluate, variable, member](
                    const std::vector<double>& x, double& residual) {
                    SemiPhysicalExpanderResult result;
                    const auto status = evaluate(x, result);
                    if (!status.ok()) return status;
                    residual = x.at(variable) - result.*member;
                    return EvaluationStatus::success();
                }, std::move(dependencies),
                scale);
        };
        add_diagnostic_equation(
            "built_in_pressure_diagnostic", built_in_pressure,
            &SemiPhysicalExpanderResult::built_in_pressure, 1.0e6);
        add_diagnostic_equation(
            "internal_mass_flow_diagnostic", internal_mass_flow,
            &SemiPhysicalExpanderResult::internal_mass_flow, 0.1);
        add_diagnostic_equation(
            "leakage_mass_flow_diagnostic", leakage_mass_flow,
            &SemiPhysicalExpanderResult::leakage_mass_flow, 0.1);
        add_diagnostic_equation(
            "indicated_power_diagnostic", indicated_power,
            &SemiPhysicalExpanderResult::indicated_power, 1.0e3);
        add_diagnostic_equation(
            "mechanical_loss_power_diagnostic", mechanical_loss_power,
            &SemiPhysicalExpanderResult::mechanical_loss_power, 1.0e3);
        add_diagnostic_equation(
            "ambient_heat_loss_diagnostic", ambient_heat_loss,
            &SemiPhysicalExpanderResult::ambient_heat_loss, 1.0e3);
        system.add_linear_equation(
            prefix + "ambient_heat_sink_temperature",
            {{rejected_temperature, 1.0}},
            ambient_temperature, 100.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

}  // namespace

EvaluationStatus evaluate_semi_physical_volumetric_expander(
    const physics::PropertyPackage& properties,
    double inlet_pressure,
    double inlet_enthalpy,
    double outlet_pressure,
    double angular_speed,
    const SemiPhysicalVolumetricExpanderParameters& parameters,
    SemiPhysicalVolumetricExpanderEvaluation& result) {
    return evaluate_semi_physical_expander(
        properties, inlet_pressure, inlet_enthalpy, outlet_pressure,
        angular_speed,
        parameters.maximum_chamber_volume_per_revolution,
        parameters.built_in_volume_ratio,
        parameters.leakage_area,
        parameters.leakage_discharge_coefficient,
        parameters.mechanical_loss_at_reference_speed,
        parameters.mechanical_loss_reference_angular_speed,
        parameters.proportional_mechanical_loss,
        parameters.ambient_heat_transfer_conductance,
        parameters.ambient_temperature,
        result);
}

void register_volumetric_expander_component_models(
    ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<CorrelatedVolumetricExpanderModel>());
    registry.register_model(
        std::make_shared<SemiPhysicalVolumetricExpanderModel>());
}

}  // namespace thermox::platform
