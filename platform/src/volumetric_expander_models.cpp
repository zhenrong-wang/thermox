#include "component_modules.hpp"
#include "component_model_support.hpp"

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
using component_model_support::require_port_variable;
using component_model_support::require_property_package;
using component_model_support::required_parameter;

struct ExpanderClosures {
    double filling_factor{0.0};
    double fluid_isentropic_efficiency{0.0};
    double shaft_isentropic_efficiency{0.0};
};

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

}  // namespace

void register_volumetric_expander_component_models(
    ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<CorrelatedVolumetricExpanderModel>());
}

}  // namespace thermox::platform
