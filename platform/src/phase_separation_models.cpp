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

using component_model_support::parameter_or;
using component_model_support::property_failure;
using component_model_support::require_port_variable;
using component_model_support::require_property_package;

struct SaturationEnthalpyEvaluation {
    EvaluationStatus status;
    double enthalpy_j_kg{0.0};
};

SaturationEnthalpyEvaluation evaluate_saturation_enthalpy(
    const physics::PropertyPackage& properties,
    double pressure_pa,
    bool vapor) {
    const auto saturation = properties.saturation_p(pressure_pa);
    if (!saturation.ok()) {
        return {property_failure(saturation), 0.0};
    }
    return {
        EvaluationStatus::success(),
        vapor ? saturation.vapor.enthalpy_j_kg
              : saturation.liquid.enthalpy_j_kg};
}

class EquilibriumFlashSeparatorModel final : public ComponentModel {
public:
    EquilibriumFlashSeparatorModel() {
        descriptor_.kind = "separator.fluid.equilibrium_flash";
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "separator.fluid";
        descriptor_.display_name = "Equilibrium flash separator";
        descriptor_.category = "Phase separation";
        descriptor_.model_name = "Saturated liquid-vapor equilibrium";
        descriptor_.ports = {
            {"inlet", "fluid", "in"},
            {"vapor_outlet", "fluid", "out"},
            {"liquid_outlet", "fluid", "out"},
        };
        descriptor_.parameters = {
            {"pressure_loss_fraction", "dimensionless", false, 0.0,
             0.0, 1.0, true, false},
        };
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::saturation_p};
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
                require_property_package(context, "vapor_outlet") ||
            properties !=
                require_property_package(context, "liquid_outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' separator ports must use the same medium");
        }
        const double pressure_ratio = 1.0 - parameter_or(
            context.component, "pressure_loss_fraction", 0.0);
        const auto inlet_m =
            require_port_variable(context, "inlet.m_dot");
        const auto inlet_p =
            require_port_variable(context, "inlet.p");
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
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "vapor_pressure",
            {{vapor_p, 1.0}, {inlet_p, -pressure_ratio}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "liquid_pressure",
            {{liquid_p, 1.0}, {inlet_p, -pressure_ratio}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "mass_balance",
            {{vapor_m, 1.0}, {liquid_m, 1.0}, {inlet_m, -1.0}},
            0.0, 100.0);

        const auto add_saturation_equation =
            [&](const std::string& name,
                std::size_t pressure,
                std::size_t enthalpy,
                bool vapor) {
                system.add_checked_sparse_equation(
                    prefix + name,
                    [properties, pressure, enthalpy, vapor](
                        const std::vector<double>& x,
                        double& residual) {
                        const auto saturation =
                            evaluate_saturation_enthalpy(
                                *properties, x.at(pressure), vapor);
                        if (!saturation.status.ok()) {
                            return saturation.status;
                        }
                        residual = x.at(enthalpy) -
                            saturation.enthalpy_j_kg;
                        return EvaluationStatus::success();
                    },
                    {pressure, enthalpy},
                    [properties, pressure, enthalpy, vapor](
                        const std::vector<double>& x,
                        std::vector<EquationPartial>& jacobian) {
                        const auto base = evaluate_saturation_enthalpy(
                            *properties, x.at(pressure), vapor);
                        if (!base.status.ok()) {
                            throw std::runtime_error(base.status.message);
                        }
                        const double step = std::max(
                            10.0, std::abs(x.at(pressure)) * 1.0e-6);
                        const auto plus = evaluate_saturation_enthalpy(
                            *properties, x.at(pressure) + step, vapor);
                        const auto minus = evaluate_saturation_enthalpy(
                            *properties, x.at(pressure) - step, vapor);
                        double derivative = 0.0;
                        if (plus.status.ok() && minus.status.ok()) {
                            derivative =
                                (plus.enthalpy_j_kg -
                                 minus.enthalpy_j_kg) /
                                (2.0 * step);
                        } else if (plus.status.ok()) {
                            derivative =
                                (plus.enthalpy_j_kg -
                                 base.enthalpy_j_kg) /
                                step;
                        } else if (minus.status.ok()) {
                            derivative =
                                (base.enthalpy_j_kg -
                                 minus.enthalpy_j_kg) /
                                step;
                        } else {
                            throw std::runtime_error(
                                "could not evaluate saturation-enthalpy "
                                "derivative for flash separator");
                        }
                        jacobian.push_back({pressure, -derivative});
                        jacobian.push_back({enthalpy, 1.0});
                        return x.at(enthalpy) - base.enthalpy_j_kg;
                    },
                    100000.0);
            };
        add_saturation_equation(
            "saturated_vapor", vapor_p, vapor_h, true);
        add_saturation_equation(
            "saturated_liquid", liquid_p, liquid_h, false);

        system.add_checked_sparse_equation(
            prefix + "equilibrium_vapor_fraction",
            [properties, pressure_ratio, inlet_m, inlet_p, inlet_h,
             vapor_m](
                const std::vector<double>& x, double& residual) {
                const auto saturation = properties->saturation_p(
                    pressure_ratio * x.at(inlet_p));
                if (!saturation.ok()) {
                    return property_failure(saturation);
                }
                const double tolerance = 1.0e-8 * std::max(
                    std::abs(saturation.vapor.enthalpy_j_kg), 1.0);
                if (x.at(inlet_h) <
                        saturation.liquid.enthalpy_j_kg - tolerance ||
                    x.at(inlet_h) >
                        saturation.vapor.enthalpy_j_kg + tolerance) {
                    return EvaluationStatus::recoverable(
                        "equilibrium flash separator inlet enthalpy is "
                        "outside the liquid-vapor saturation dome at "
                        "separator pressure");
                }
                if (!std::isfinite(x.at(inlet_m)) ||
                    x.at(inlet_m) < 0.0) {
                    return EvaluationStatus::recoverable(
                        "equilibrium flash separator requires "
                        "non-negative finite inlet mass flow");
                }
                const double latent_enthalpy =
                    saturation.vapor.enthalpy_j_kg -
                    saturation.liquid.enthalpy_j_kg;
                if (!std::isfinite(latent_enthalpy) ||
                    latent_enthalpy <= 0.0) {
                    return EvaluationStatus::recoverable(
                        "equilibrium flash separator requires positive "
                        "finite latent enthalpy");
                }
                const double vapor_quality =
                    (x.at(inlet_h) -
                     saturation.liquid.enthalpy_j_kg) /
                    latent_enthalpy;
                residual = x.at(vapor_m) -
                    x.at(inlet_m) * vapor_quality;
                return EvaluationStatus::success();
            },
            {inlet_m, inlet_p, inlet_h, vapor_m},
            [properties, pressure_ratio, inlet_m, inlet_p, inlet_h,
             vapor_m](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                const double separator_pressure =
                    pressure_ratio * x.at(inlet_p);
                const auto saturation =
                    properties->saturation_p(separator_pressure);
                if (!saturation.ok()) {
                    throw std::runtime_error(saturation.message);
                }
                const double tolerance = 1.0e-8 * std::max(
                    std::abs(saturation.vapor.enthalpy_j_kg), 1.0);
                if (x.at(inlet_h) <
                        saturation.liquid.enthalpy_j_kg - tolerance ||
                    x.at(inlet_h) >
                        saturation.vapor.enthalpy_j_kg + tolerance) {
                    throw std::runtime_error(
                        "equilibrium flash separator inlet enthalpy is "
                        "outside the liquid-vapor saturation dome at "
                        "separator pressure");
                }
                if (!std::isfinite(x.at(inlet_m)) ||
                    x.at(inlet_m) < 0.0) {
                    throw std::runtime_error(
                        "equilibrium flash separator requires "
                        "non-negative finite inlet mass flow");
                }
                const double latent_enthalpy =
                    saturation.vapor.enthalpy_j_kg -
                    saturation.liquid.enthalpy_j_kg;
                const double vapor_quality =
                    (x.at(inlet_h) -
                     saturation.liquid.enthalpy_j_kg) /
                    latent_enthalpy;
                const double pressure_step = std::max(
                    10.0, std::abs(separator_pressure) * 1.0e-6);
                const auto quality_at_pressure =
                    [&](double pressure) {
                        const auto candidate =
                            properties->saturation_p(pressure);
                        if (!candidate.ok()) {
                            throw std::runtime_error(candidate.message);
                        }
                        return (x.at(inlet_h) -
                            candidate.liquid.enthalpy_j_kg) /
                            (candidate.vapor.enthalpy_j_kg -
                             candidate.liquid.enthalpy_j_kg);
                    };
                const double quality_pressure_derivative =
                    (quality_at_pressure(
                         separator_pressure + pressure_step) -
                     quality_at_pressure(
                         separator_pressure - pressure_step)) /
                    (2.0 * pressure_step);
                jacobian.push_back({vapor_m, 1.0});
                jacobian.push_back({inlet_m, -vapor_quality});
                jacobian.push_back(
                    {inlet_h, -x.at(inlet_m) / latent_enthalpy});
                jacobian.push_back({
                    inlet_p,
                    -x.at(inlet_m) * quality_pressure_derivative *
                        pressure_ratio});
                return x.at(vapor_m) -
                    x.at(inlet_m) * vapor_quality;
            },
            100.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

}  // namespace

void register_phase_separation_component_models(
    ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<EquilibriumFlashSeparatorModel>());
}

}  // namespace thermox::platform
