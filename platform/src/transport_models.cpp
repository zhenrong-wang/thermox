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

using component_model_support::require_port_variable;
using component_model_support::require_port_species;
using component_model_support::require_property_package;
using component_model_support::require_correlation;
using component_model_support::parameter_or;
using component_model_support::required_parameter;
using component_model_support::property_failure;

void require_same_material(
    const ComponentCompileContext& context,
    const std::vector<std::string>& ports) {
    if (ports.empty()) return;
    const auto first =
        context.component.material_bindings.find(ports.front());
    if (first == context.component.material_bindings.end()) {
        throw std::logic_error(
            "component '" + context.component.id +
            "' material binding is missing for port '" +
            ports.front() + "'");
    }
    for (const auto& port : ports) {
        const auto binding =
            context.component.material_bindings.find(port);
        if (binding == context.component.material_bindings.end() ||
            binding->second != first->second) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' material ports must use the same material");
        }
    }
}

ComponentModelDescriptor make_descriptor(
    std::string kind,
    std::vector<PortModelDescriptor> ports) {
    ComponentModelDescriptor out;
    out.kind = std::move(kind);
    out.version = "1.0.0";
    out.ports = std::move(ports);
    return out;
}

class TwoInletFluidMixerModel final : public ComponentModel {
public:
    TwoInletFluidMixerModel()
        : descriptor_(make_descriptor(
              "junction.fluid.mixer.two_inlet",
              {{"inlet_a", "fluid", "in"},
               {"inlet_b", "fluid", "in"},
               {"outlet", "fluid", "out"}})) {}

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto properties =
            require_property_package(context, "inlet_a");
        if (properties !=
                require_property_package(context, "inlet_b") ||
            properties !=
                require_property_package(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' mixer ports must use the same medium");
        }

        const auto mass_a =
            require_port_variable(context, "inlet_a.m_dot");
        const auto pressure_a =
            require_port_variable(context, "inlet_a.p");
        const auto enthalpy_a =
            require_port_variable(context, "inlet_a.h");
        const auto mass_b =
            require_port_variable(context, "inlet_b.m_dot");
        const auto pressure_b =
            require_port_variable(context, "inlet_b.p");
        const auto enthalpy_b =
            require_port_variable(context, "inlet_b.h");
        const auto mass_out =
            require_port_variable(context, "outlet.m_dot");
        const auto pressure_out =
            require_port_variable(context, "outlet.p");
        const auto enthalpy_out =
            require_port_variable(context, "outlet.h");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_balance",
            {{mass_out, 1.0}, {mass_a, -1.0}, {mass_b, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "pressure_inlet_a",
            {{pressure_out, 1.0}, {pressure_a, -1.0}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "pressure_inlet_b",
            {{pressure_out, 1.0}, {pressure_b, -1.0}},
            0.0, 100000.0);
        system.add_sparse_equation(
            prefix + "energy_balance",
            {mass_a, enthalpy_a, mass_b, enthalpy_b,
             mass_out, enthalpy_out},
            [mass_a, enthalpy_a, mass_b, enthalpy_b,
             mass_out, enthalpy_out](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                jacobian.push_back(
                    {mass_out, x.at(enthalpy_out)});
                jacobian.push_back(
                    {enthalpy_out, x.at(mass_out)});
                jacobian.push_back(
                    {mass_a, -x.at(enthalpy_a)});
                jacobian.push_back(
                    {enthalpy_a, -x.at(mass_a)});
                jacobian.push_back(
                    {mass_b, -x.at(enthalpy_b)});
                jacobian.push_back(
                    {enthalpy_b, -x.at(mass_b)});
                return x.at(mass_out) * x.at(enthalpy_out) -
                       x.at(mass_a) * x.at(enthalpy_a) -
                       x.at(mass_b) * x.at(enthalpy_b);
            },
            1.0e8);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class TwoOutletFluidSplitterModel final : public ComponentModel {
public:
    TwoOutletFluidSplitterModel()
        : descriptor_(make_descriptor(
              "junction.fluid.splitter.two_outlet",
              {{"inlet", "fluid", "in"},
               {"outlet_a", "fluid", "out"},
               {"outlet_b", "fluid", "out"}})) {}

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto properties =
            require_property_package(context, "inlet");
        if (properties !=
                require_property_package(context, "outlet_a") ||
            properties !=
                require_property_package(context, "outlet_b")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' splitter ports must use the same medium");
        }

        const auto mass_in =
            require_port_variable(context, "inlet.m_dot");
        const auto pressure_in =
            require_port_variable(context, "inlet.p");
        const auto enthalpy_in =
            require_port_variable(context, "inlet.h");
        const auto mass_a =
            require_port_variable(context, "outlet_a.m_dot");
        const auto pressure_a =
            require_port_variable(context, "outlet_a.p");
        const auto enthalpy_a =
            require_port_variable(context, "outlet_a.h");
        const auto mass_b =
            require_port_variable(context, "outlet_b.m_dot");
        const auto pressure_b =
            require_port_variable(context, "outlet_b.p");
        const auto enthalpy_b =
            require_port_variable(context, "outlet_b.h");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_balance",
            {{mass_in, 1.0}, {mass_a, -1.0}, {mass_b, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "pressure_outlet_a",
            {{pressure_a, 1.0}, {pressure_in, -1.0}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "pressure_outlet_b",
            {{pressure_b, 1.0}, {pressure_in, -1.0}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "enthalpy_outlet_a",
            {{enthalpy_a, 1.0}, {enthalpy_in, -1.0}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "enthalpy_outlet_b",
            {{enthalpy_b, 1.0}, {enthalpy_in, -1.0}},
            0.0, 100000.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class IsenthalpicPressureRatioValveModel final
    : public ComponentModel {
public:
    IsenthalpicPressureRatioValveModel()
        : descriptor_(make_descriptor(
              "valve.fluid.isenthalpic_pressure_ratio",
              {{"inlet", "fluid", "in"},
               {"outlet", "fluid", "out"}})) {
        descriptor_.parameters = {
            {"pressure_ratio", "dimensionless", true, std::nullopt,
             1.0, std::numeric_limits<double>::infinity(),
             false, true}};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const double pressure_ratio =
            required_parameter(
                context.component, "pressure_ratio");
        if (require_property_package(context, "inlet") !=
            require_property_package(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' inlet and outlet must use the same medium");
        }

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
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_continuity",
            {{outlet_m, 1.0}, {inlet_m, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "pressure_ratio",
            {{inlet_p, 1.0},
             {outlet_p, -pressure_ratio}},
            0.0, 100000.0 * pressure_ratio);
        system.add_linear_equation(
            prefix + "isenthalpic",
            {{outlet_h, 1.0}, {inlet_h, -1.0}},
            0.0, 100000.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

struct RestrictionEvaluation {
    EvaluationStatus status;
    double residual{0.0};
};

using RestrictionEvaluator = std::function<RestrictionEvaluation(
    const std::vector<double>&)>;

double assemble_restriction_numerical_row(
    const RestrictionEvaluator& evaluate,
    const std::vector<std::pair<std::size_t, double>>& variables,
    const std::vector<double>& x,
    std::vector<EquationPartial>& jacobian) {
    const auto base = evaluate(x);
    if (!base.status.ok()) {
        throw std::runtime_error(base.status.message);
    }
    for (const auto& [variable, minimum_step] : variables) {
        const double step = std::max(
            minimum_step, std::abs(x.at(variable)) * 1.0e-6);
        auto plus = x;
        auto minus = x;
        plus.at(variable) += step;
        minus.at(variable) -= step;
        const auto plus_value = evaluate(plus);
        const auto minus_value = evaluate(minus);
        double derivative = 0.0;
        if (plus_value.status.ok() && minus_value.status.ok()) {
            derivative =
                (plus_value.residual - minus_value.residual) /
                (2.0 * step);
        } else if (plus_value.status.ok()) {
            derivative =
                (plus_value.residual - base.residual) / step;
        } else if (minus_value.status.ok()) {
            derivative =
                (base.residual - minus_value.residual) / step;
        } else {
            throw std::runtime_error(
                "could not evaluate a local restriction-model "
                "derivative");
        }
        jacobian.push_back({variable, derivative});
    }
    return base.residual;
}

class FlowAreaRestrictionModel final : public ComponentModel {
public:
    explicit FlowAreaRestrictionModel(bool compressible_gas)
        : compressible_gas_(compressible_gas) {
        descriptor_ = make_descriptor(
            compressible_gas
                ? "restriction.fluid.orifice.perfect_gas"
                : "restriction.fluid.orifice.nonflashing_liquid",
            {{"inlet", "fluid", "in"},
             {"outlet", "fluid", "out"}});
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "restriction.fluid.orifice";
        descriptor_.display_name = "Flow restriction / orifice";
        descriptor_.category = "Fluid control";
        descriptor_.model_name = compressible_gas
            ? "Perfect-gas with choking"
            : "Non-flashing liquid";
        descriptor_.parameters = {
            {"flow_diameter", "length", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"discharge_coefficient", "dimensionless", true,
             std::nullopt, 0.0, 1.0, false, true},
        };
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph};
        if (!compressible_gas) {
            descriptor_.required_property_capabilities.push_back(
                physics::PropertyCapability::saturation_p);
        }
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto properties =
            require_property_package(context, "inlet");
        if (properties != require_property_package(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' inlet and outlet must use the same medium");
        }
        const double diameter = required_parameter(
            context.component, "flow_diameter");
        const double discharge_coefficient = required_parameter(
            context.component, "discharge_coefficient");
        const double effective_area = discharge_coefficient *
            std::numbers::pi * diameter * diameter / 4.0;
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
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_continuity",
            {{outlet_m, 1.0}, {inlet_m, -1.0}}, 0.0, 100.0);
        system.add_linear_equation(
            prefix + "isenthalpic_throttling",
            {{outlet_h, 1.0}, {inlet_h, -1.0}},
            0.0, 100000.0);

        RestrictionEvaluator evaluate;
        if (!compressible_gas_) {
            evaluate = [properties, effective_area, inlet_m, inlet_p,
                        inlet_h, outlet_p](
                           const std::vector<double>& x) {
                const double pressure_drop =
                    x.at(inlet_p) - x.at(outlet_p);
                if (!std::isfinite(pressure_drop) ||
                    pressure_drop <= 0.0) {
                    return RestrictionEvaluation{
                        EvaluationStatus::recoverable(
                            "non-flashing liquid restriction requires "
                            "inlet pressure above outlet pressure")};
                }
                const auto inlet = properties->state_ph(
                    x.at(inlet_p), x.at(inlet_h));
                if (!inlet.ok()) {
                    return RestrictionEvaluation{
                        property_failure(inlet)};
                }
                if (inlet.state.phase != physics::Phase::liquid) {
                    return RestrictionEvaluation{
                        EvaluationStatus::recoverable(
                            "non-flashing liquid restriction requires "
                            "a liquid inlet state")};
                }
                const auto saturation =
                    properties->saturation_p(x.at(outlet_p));
                if (!saturation.ok()) {
                    return RestrictionEvaluation{
                        property_failure(saturation)};
                }
                const double flashing_margin =
                    saturation.liquid.enthalpy_j_kg - x.at(inlet_h);
                const double enthalpy_tolerance = 1.0e-8 * std::max(
                    std::abs(saturation.liquid.enthalpy_j_kg), 1.0);
                if (!std::isfinite(flashing_margin) ||
                    flashing_margin <= enthalpy_tolerance) {
                    return RestrictionEvaluation{
                        EvaluationStatus::recoverable(
                            "non-flashing liquid restriction reaches "
                            "the outlet saturation boundary; select a "
                            "validated flashing-flow model")};
                }
                const double density = inlet.state.density_kg_m3;
                if (!std::isfinite(density) || density <= 0.0) {
                    return RestrictionEvaluation{
                        EvaluationStatus::recoverable(
                            "liquid restriction requires positive "
                            "finite inlet density")};
                }
                return RestrictionEvaluation{
                    EvaluationStatus::success(),
                    x.at(inlet_m) - effective_area *
                        std::sqrt(2.0 * density * pressure_drop)};
            };
        } else {
            evaluate = [properties, effective_area, inlet_m, inlet_p,
                        inlet_h, outlet_p](
                           const std::vector<double>& x) {
                const double upstream_pressure = x.at(inlet_p);
                const double downstream_pressure = x.at(outlet_p);
                if (!std::isfinite(upstream_pressure) ||
                    !std::isfinite(downstream_pressure) ||
                    downstream_pressure <= 0.0 ||
                    downstream_pressure >= upstream_pressure) {
                    return RestrictionEvaluation{
                        EvaluationStatus::recoverable(
                            "compressible restriction requires 0 < "
                            "outlet pressure < inlet pressure")};
                }
                const auto inlet = properties->state_ph(
                    upstream_pressure, x.at(inlet_h));
                if (!inlet.ok()) {
                    return RestrictionEvaluation{
                        property_failure(inlet)};
                }
                if (inlet.state.phase != physics::Phase::vapor) {
                    return RestrictionEvaluation{
                        EvaluationStatus::recoverable(
                            "compressible-gas restriction requires a "
                            "vapor inlet state")};
                }
                const double gamma =
                    inlet.state.cp_j_kg_k / inlet.state.cv_j_kg_k;
                const double density = inlet.state.density_kg_m3;
                if (!std::isfinite(gamma) || gamma <= 1.0 ||
                    !std::isfinite(density) || density <= 0.0) {
                    return RestrictionEvaluation{
                        EvaluationStatus::recoverable(
                            "compressible restriction requires positive "
                            "density and a finite heat-capacity ratio "
                            "greater than one")};
                }
                const double critical_ratio = std::pow(
                    2.0 / (gamma + 1.0),
                    gamma / (gamma - 1.0));
                const double pressure_ratio = std::max(
                    downstream_pressure / upstream_pressure,
                    critical_ratio);
                const double flux_squared =
                    2.0 * gamma / (gamma - 1.0) * density *
                    upstream_pressure *
                    (std::pow(pressure_ratio, 2.0 / gamma) -
                     std::pow(
                         pressure_ratio,
                         (gamma + 1.0) / gamma));
                if (!std::isfinite(flux_squared) ||
                    flux_squared <= 0.0) {
                    return RestrictionEvaluation{
                        EvaluationStatus::recoverable(
                            "compressible restriction produced an "
                            "invalid mass-flux state")};
                }
                return RestrictionEvaluation{
                    EvaluationStatus::success(),
                    x.at(inlet_m) -
                        effective_area * std::sqrt(flux_squared)};
            };
        }

        const std::vector<std::pair<std::size_t, double>> derivatives{
            {inlet_m, 1.0e-7},
            {inlet_p, 0.1},
            {inlet_h, 0.1},
            {outlet_p, 0.1},
        };
        system.add_checked_sparse_equation(
            prefix + (compressible_gas_
                ? "compressible_mass_flow"
                : "nonflashing_liquid_mass_flow"),
            [evaluate](const std::vector<double>& x, double& residual) {
                const auto value = evaluate(x);
                residual = value.residual;
                return value.status;
            },
            {inlet_m, inlet_p, inlet_h, outlet_p},
            [evaluate, derivatives](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                return assemble_restriction_numerical_row(
                    evaluate, derivatives, x, jacobian);
            },
            100.0);
        system.add_initialization_relation(
            {{outlet_p, 1.0}, {inlet_p, -0.9}}, 0.0);
    }

private:
    bool compressible_gas_{false};
    ComponentModelDescriptor descriptor_;
};

class ReturnBendFixedLossModel final : public ComponentModel {
public:
    ReturnBendFixedLossModel()
        : descriptor_(make_descriptor(
              "fitting.fluid.return_bend.fixed_loss_coefficient",
              {{"inlet", "fluid", "in"},
               {"outlet", "fluid", "out"}})) {
        descriptor_.template_kind = "fitting.fluid.return_bend";
        descriptor_.display_name = "Return bend (180 deg)";
        descriptor_.category = "Fluid fittings";
        descriptor_.model_name = "Fixed loss coefficient";
        descriptor_.parameters = {
            {"inner_diameter", "length", true, std::nullopt,
             0.0, std::numeric_limits<double>::infinity(), false,
             true},
            {"loss_coefficient", "dimensionless", true,
             std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), true, true},
        };
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph,
        };
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
        const double diameter = required_parameter(
            context.component, "inner_diameter");
        const double loss_coefficient = required_parameter(
            context.component, "loss_coefficient");
        const double area =
            std::numbers::pi * diameter * diameter / 4.0;
        const double loss_scale =
            loss_coefficient / (2.0 * area * area);

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
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_continuity",
            {{outlet_m, 1.0}, {inlet_m, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "adiabatic_enthalpy",
            {{outlet_h, 1.0}, {inlet_h, -1.0}},
            0.0, 100000.0);
        system.add_checked_sparse_equation(
            prefix + "pressure_loss",
            [properties, inlet_m, inlet_p, inlet_h, outlet_p,
             loss_scale](const std::vector<double>& x,
                         double& residual) {
                const auto state = properties->state_ph(
                    x.at(inlet_p), x.at(inlet_h));
                if (!state.ok()) return property_failure(state);
                const double density = state.state.density_kg_m3;
                if (!std::isfinite(density) || density <= 0.0) {
                    return EvaluationStatus::recoverable(
                        "return-bend pressure loss requires positive "
                        "finite inlet density");
                }
                const double mass_flow = x.at(inlet_m);
                residual = x.at(inlet_p) - x.at(outlet_p) -
                    loss_scale * mass_flow * std::abs(mass_flow) /
                        density;
                return EvaluationStatus::success();
            },
            {inlet_m, inlet_p, inlet_h, outlet_p},
            [properties, inlet_m, inlet_p, inlet_h, outlet_p,
             loss_scale](const std::vector<double>& x,
                         std::vector<EquationPartial>& jacobian) {
                const auto state =
                    physics::state_ph_derivatives_with_fallback(
                        *properties, x.at(inlet_p),
                        x.at(inlet_h));
                if (!state.ok()) {
                    throw std::runtime_error(state.message);
                }
                const double density =
                    state.state.density_kg_m3;
                if (!std::isfinite(density) || density <= 0.0) {
                    throw std::runtime_error(
                        "return-bend pressure loss requires positive "
                        "finite inlet density");
                }
                const double mass_flow = x.at(inlet_m);
                const double signed_square =
                    mass_flow * std::abs(mass_flow);
                const double density_factor =
                    loss_scale * signed_square /
                    (density * density);
                jacobian.push_back({
                    inlet_m,
                    -2.0 * loss_scale * std::abs(mass_flow) /
                        density});
                jacobian.push_back({
                    inlet_p,
                    1.0 + density_factor *
                        state.derivatives
                            .density_wrt_pressure_at_enthalpy});
                jacobian.push_back({
                    inlet_h,
                    density_factor *
                        state.derivatives
                            .density_wrt_enthalpy_at_pressure});
                jacobian.push_back({outlet_p, -1.0});
                return x.at(inlet_p) - x.at(outlet_p) -
                    loss_scale * signed_square / density;
            },
            100000.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class ReturnBendCorrelationModel final : public ComponentModel {
public:
    ReturnBendCorrelationModel()
        : descriptor_(make_descriptor(
              "fitting.fluid.return_bend.correlation",
              {{"inlet", "fluid", "in"},
               {"outlet", "fluid", "out"}})) {
        descriptor_.template_kind = "fitting.fluid.return_bend";
        descriptor_.display_name = "Return bend (180 deg)";
        descriptor_.category = "Fluid fittings";
        descriptor_.model_name = "Engineering correlation";
        descriptor_.parameters = {
            {"inner_diameter", "length", true, std::nullopt,
             0.0, std::numeric_limits<double>::infinity(), false,
             true},
        };
        descriptor_.artifacts = {
            {"pressure_loss_correlation",
             correlation_artifact_type, true},
        };
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph,
        };
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
        const auto correlation = require_correlation(
            context, "pressure_loss_correlation");
        if (correlation->output().dimension != "pressure") {
            throw std::invalid_argument(
                "return-bend correlation output must have pressure "
                "dimension");
        }
        const std::map<std::string, std::string> supported_inputs{
            {"mass_flow", "mass_flow"},
            {"density", "density"},
            {"area", "area"},
            {"diameter", "length"},
        };
        for (const auto& input : correlation->inputs()) {
            const auto supported =
                supported_inputs.find(input.name);
            if (supported == supported_inputs.end()) {
                throw std::invalid_argument(
                    "return-bend correlation has unsupported input: " +
                    input.name);
            }
            if (input.dimension != supported->second) {
                throw std::invalid_argument(
                    "return-bend correlation input '" + input.name +
                    "' must have dimension '" + supported->second +
                    "'");
            }
        }

        const double diameter = required_parameter(
            context.component, "inner_diameter");
        const double area =
            std::numbers::pi * diameter * diameter / 4.0;
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
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_continuity",
            {{outlet_m, 1.0}, {inlet_m, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "adiabatic_enthalpy",
            {{outlet_h, 1.0}, {inlet_h, -1.0}},
            0.0, 100000.0);

        const auto correlation_inputs =
            [correlation, area, diameter](
                double mass_flow, double density) {
                std::map<std::string, double> values;
                for (const auto& input : correlation->inputs()) {
                    if (input.name == "mass_flow") {
                        values.emplace(input.name, mass_flow);
                    } else if (input.name == "density") {
                        values.emplace(input.name, density);
                    } else if (input.name == "area") {
                        values.emplace(input.name, area);
                    } else if (input.name == "diameter") {
                        values.emplace(input.name, diameter);
                    }
                }
                return values;
            };
        system.add_checked_sparse_equation(
            prefix + "pressure_loss_correlation",
            [properties, correlation, correlation_inputs,
             inlet_m, inlet_p, inlet_h, outlet_p](
                const std::vector<double>& x,
                double& residual) {
                const auto state = properties->state_ph(
                    x.at(inlet_p), x.at(inlet_h));
                if (!state.ok()) return property_failure(state);
                const auto loss = correlation->evaluate(
                    correlation_inputs(
                        x.at(inlet_m),
                        state.state.density_kg_m3));
                if (!loss.error.empty()) {
                    return EvaluationStatus::recoverable(loss.error);
                }
                residual = x.at(inlet_p) - x.at(outlet_p) -
                    loss.value;
                return EvaluationStatus::success();
            },
            {inlet_m, inlet_p, inlet_h, outlet_p},
            [properties, correlation, correlation_inputs,
             inlet_m, inlet_p, inlet_h, outlet_p](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                const auto state =
                    physics::state_ph_derivatives_with_fallback(
                        *properties, x.at(inlet_p),
                        x.at(inlet_h));
                if (!state.ok()) {
                    throw std::runtime_error(state.message);
                }
                const auto loss = correlation->evaluate(
                    correlation_inputs(
                        x.at(inlet_m),
                        state.state.density_kg_m3));
                if (!loss.error.empty()) {
                    throw std::runtime_error(loss.error);
                }
                const auto derivative =
                    [&](const std::string& input) {
                        const auto found =
                            loss.input_derivatives.find(input);
                        return found ==
                                loss.input_derivatives.end()
                            ? 0.0
                            : found->second;
                    };
                const double density_derivative =
                    derivative("density");
                jacobian.push_back(
                    {inlet_m, -derivative("mass_flow")});
                jacobian.push_back({
                    inlet_p,
                    1.0 - density_derivative *
                        state.derivatives
                            .density_wrt_pressure_at_enthalpy});
                jacobian.push_back({
                    inlet_h,
                    -density_derivative *
                        state.derivatives
                            .density_wrt_enthalpy_at_pressure});
                jacobian.push_back({outlet_p, -1.0});
                return x.at(inlet_p) - x.at(outlet_p) -
                    loss.value;
            },
            100000.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

struct PipeHydraulicParameters {
    double length_m{0.0};
    double diameter_m{0.0};
    double roughness_m{0.0};
    double elevation_change_m{0.0};
    double local_loss_coefficient{0.0};
};

EvaluationStatus evaluate_pipe_pressure_balance(
    const physics::PropertyPackage& properties,
    const PipeHydraulicParameters& parameters,
    double mass_flow_kg_s,
    double inlet_pressure_pa,
    double inlet_enthalpy_j_kg,
    double outlet_pressure_pa,
    double outlet_enthalpy_j_kg,
    double& residual) {
    constexpr double gravity_m_s2 = 9.80665;
    const double mean_pressure =
        0.5 * (inlet_pressure_pa + outlet_pressure_pa);
    const double mean_enthalpy =
        0.5 * (inlet_enthalpy_j_kg + outlet_enthalpy_j_kg);
    const auto state =
        properties.state_ph(mean_pressure, mean_enthalpy);
    if (!state.ok()) return property_failure(state);
    if (state.state.phase == physics::Phase::two_phase) {
        return EvaluationStatus::recoverable(
            "Darcy-Weisbach pipe model is restricted to single-phase "
            "states");
    }
    const double density = state.state.density_kg_m3;
    const double viscosity = state.state.viscosity_pa_s;
    if (!std::isfinite(density) || density <= 0.0 ||
        !std::isfinite(viscosity) || viscosity <= 0.0) {
        return EvaluationStatus::recoverable(
            "Darcy-Weisbach pipe model requires positive finite "
            "density and dynamic viscosity");
    }

    const double area = std::numbers::pi *
        parameters.diameter_m * parameters.diameter_m / 4.0;
    const double absolute_flow = std::abs(mass_flow_kg_s);
    double friction_pressure_loss = 0.0;
    if (absolute_flow > 1.0e-14) {
        const double reynolds = absolute_flow *
            parameters.diameter_m / (area * viscosity);
        const double laminar_factor = 64.0 / reynolds;
        const double haaland_argument = std::pow(
            parameters.roughness_m /
                (3.7 * parameters.diameter_m),
            1.11) + 6.9 / reynolds;
        const double turbulent_factor = 1.0 /
            std::pow(-1.8 * std::log10(haaland_argument), 2.0);
        double friction_factor = laminar_factor;
        if (reynolds >= 4000.0) {
            friction_factor = turbulent_factor;
        } else if (reynolds > 2300.0) {
            const double fraction =
                (reynolds - 2300.0) / (4000.0 - 2300.0);
            const double blend =
                fraction * fraction * (3.0 - 2.0 * fraction);
            friction_factor =
                (1.0 - blend) * laminar_factor +
                blend * turbulent_factor;
        }
        const double total_loss_coefficient =
            friction_factor * parameters.length_m /
                parameters.diameter_m +
            parameters.local_loss_coefficient;
        friction_pressure_loss = total_loss_coefficient *
            mass_flow_kg_s * absolute_flow /
            (2.0 * density * area * area);
    }
    const double elevation_pressure_change =
        density * gravity_m_s2 * parameters.elevation_change_m;
    residual = inlet_pressure_pa - outlet_pressure_pa -
        friction_pressure_loss - elevation_pressure_change;
    return EvaluationStatus::success();
}

class DarcyWeisbachPipeModel final : public ComponentModel {
public:
    explicit DarcyWeisbachPipeModel(bool heat_transfer)
        : heat_transfer_(heat_transfer) {
        if (heat_transfer) {
            descriptor_ = make_descriptor(
                "pipe.fluid.darcy_weisbach_heat_transfer",
                {{"inlet", "fluid", "in"},
                 {"outlet", "fluid", "out"},
                 {"ambient", "heat", "out"}});
        } else {
            descriptor_ = make_descriptor(
                "pipe.fluid.darcy_weisbach",
                {{"inlet", "fluid", "in"},
                 {"outlet", "fluid", "out"}});
        }
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "pipe.fluid";
        descriptor_.display_name = "Single-phase pipe";
        descriptor_.category = "Fluid transport";
        descriptor_.model_name = heat_transfer
            ? "Darcy-Weisbach with ambient heat transfer"
            : "Adiabatic Darcy-Weisbach";
        descriptor_.parameters = {
            {"length", "length", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"inner_diameter", "length", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"roughness", "length", false, 0.0, 0.0,
             std::numeric_limits<double>::infinity(), true, true},
            {"elevation_change", "length", false, 0.0,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(), true, true},
            {"local_loss_coefficient", "dimensionless", false, 0.0,
             0.0, std::numeric_limits<double>::infinity(), true, true},
        };
        if (heat_transfer) {
            descriptor_.parameters.push_back({
                "overall_thermal_conductance", "thermal_conductance",
                true, std::nullopt, 0.0,
                std::numeric_limits<double>::infinity(), false, true});
        }
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph,
            physics::PropertyCapability::transport};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto properties =
            require_property_package(context, "inlet");
        if (properties != require_property_package(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' inlet and outlet must use the same medium");
        }
        const PipeHydraulicParameters parameters{
            required_parameter(context.component, "length"),
            required_parameter(context.component, "inner_diameter"),
            parameter_or(context.component, "roughness", 0.0),
            parameter_or(context.component, "elevation_change", 0.0),
            parameter_or(
                context.component, "local_loss_coefficient", 0.0)};
        if (parameters.roughness_m >= parameters.diameter_m) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' roughness must be smaller than inner_diameter");
        }

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
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_continuity",
            {{outlet_m, 1.0}, {inlet_m, -1.0}}, 0.0, 100.0);
        if (!heat_transfer_) {
            system.add_linear_equation(
                prefix + "adiabatic_enthalpy",
                {{outlet_h, 1.0}, {inlet_h, -1.0}},
                0.0, 100000.0);
        } else {
            const auto heat_flow =
                require_port_variable(context, "ambient.Q_dot");
            system.add_sparse_equation(
                prefix + "fluid_energy_balance",
                {inlet_m, inlet_h, outlet_h, heat_flow},
                [inlet_m, inlet_h, outlet_h, heat_flow](
                    const std::vector<double>& x,
                    std::vector<EquationPartial>& jacobian) {
                    const double delta_h =
                        x.at(outlet_h) - x.at(inlet_h);
                    jacobian.push_back({inlet_m, delta_h});
                    jacobian.push_back({inlet_h, -x.at(inlet_m)});
                    jacobian.push_back({outlet_h, x.at(inlet_m)});
                    jacobian.push_back({heat_flow, 1.0});
                    return x.at(inlet_m) * delta_h +
                        x.at(heat_flow);
                },
                1.0e6);

            const double conductance = required_parameter(
                context.component, "overall_thermal_conductance");
            const auto ambient_temperature =
                require_port_variable(context, "ambient.T");
            system.add_checked_sparse_equation(
                prefix + "ambient_heat_transfer",
                [properties, inlet_p, inlet_h, outlet_p, outlet_h,
                 heat_flow, ambient_temperature, conductance](
                    const std::vector<double>& x, double& residual) {
                    const auto inlet = properties->state_ph(
                        x.at(inlet_p), x.at(inlet_h));
                    if (!inlet.ok()) return property_failure(inlet);
                    const auto outlet = properties->state_ph(
                        x.at(outlet_p), x.at(outlet_h));
                    if (!outlet.ok()) return property_failure(outlet);
                    residual = x.at(heat_flow) - conductance *
                        (0.5 * (inlet.state.temperature_k +
                                outlet.state.temperature_k) -
                         x.at(ambient_temperature));
                    return EvaluationStatus::success();
                },
                {inlet_p, inlet_h, outlet_p, outlet_h, heat_flow,
                 ambient_temperature},
                [properties, inlet_p, inlet_h, outlet_p, outlet_h,
                 heat_flow, ambient_temperature, conductance](
                    const std::vector<double>& x,
                    std::vector<EquationPartial>& jacobian) {
                    const auto inlet =
                        physics::state_ph_derivatives_with_fallback(
                            *properties, x.at(inlet_p), x.at(inlet_h));
                    const auto outlet =
                        physics::state_ph_derivatives_with_fallback(
                            *properties, x.at(outlet_p), x.at(outlet_h));
                    if (!inlet.ok() || !outlet.ok()) {
                        throw std::runtime_error(
                            inlet.ok() ? outlet.message : inlet.message);
                    }
                    jacobian.push_back({heat_flow, 1.0});
                    jacobian.push_back({ambient_temperature, conductance});
                    jacobian.push_back({inlet_p, -0.5 * conductance *
                        inlet.derivatives
                            .temperature_wrt_pressure_at_enthalpy});
                    jacobian.push_back({inlet_h, -0.5 * conductance *
                        inlet.derivatives
                            .temperature_wrt_enthalpy_at_pressure});
                    jacobian.push_back({outlet_p, -0.5 * conductance *
                        outlet.derivatives
                            .temperature_wrt_pressure_at_enthalpy});
                    jacobian.push_back({outlet_h, -0.5 * conductance *
                        outlet.derivatives
                            .temperature_wrt_enthalpy_at_pressure});
                    return x.at(heat_flow) - conductance *
                        (0.5 * (inlet.state.temperature_k +
                                outlet.state.temperature_k) -
                         x.at(ambient_temperature));
                },
                1.0e5);
        }

        const auto evaluate_pressure =
            [properties, parameters, inlet_m, inlet_p, inlet_h,
             outlet_p, outlet_h](const std::vector<double>& x,
                                 double& residual) {
                return evaluate_pipe_pressure_balance(
                    *properties, parameters, x.at(inlet_m),
                    x.at(inlet_p), x.at(inlet_h), x.at(outlet_p),
                    x.at(outlet_h), residual);
            };
        const std::vector<std::size_t> pressure_variables{
            inlet_m, inlet_p, inlet_h, outlet_p, outlet_h};
        system.add_checked_sparse_equation(
            prefix + "darcy_weisbach_pressure_balance",
            evaluate_pressure, pressure_variables,
            [evaluate_pressure, pressure_variables](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                double base = 0.0;
                const auto base_status = evaluate_pressure(x, base);
                if (!base_status.ok()) {
                    throw std::runtime_error(base_status.message);
                }
                for (const auto variable : pressure_variables) {
                    const double magnitude = std::abs(x.at(variable));
                    const double step = std::max(
                        variable == pressure_variables.front()
                            ? 1.0e-7
                            : 0.1,
                        magnitude * 1.0e-6);
                    auto plus = x;
                    auto minus = x;
                    plus.at(variable) += step;
                    minus.at(variable) -= step;
                    double plus_value = 0.0;
                    double minus_value = 0.0;
                    const auto plus_status =
                        evaluate_pressure(plus, plus_value);
                    const auto minus_status =
                        evaluate_pressure(minus, minus_value);
                    double derivative = 0.0;
                    if (plus_status.ok() && minus_status.ok()) {
                        derivative =
                            (plus_value - minus_value) / (2.0 * step);
                    } else if (plus_status.ok()) {
                        derivative = (plus_value - base) / step;
                    } else if (minus_status.ok()) {
                        derivative = (base - minus_value) / step;
                    } else {
                        throw std::runtime_error(
                            "could not evaluate a local derivative for "
                            "Darcy-Weisbach pipe pressure balance");
                    }
                    jacobian.push_back({variable, derivative});
                }
                return base;
            },
            100000.0);
        system.add_initialization_relation(
            {{outlet_p, 1.0}, {inlet_p, -1.0}}, 0.0);
    }

private:
    bool heat_transfer_{false};
    ComponentModelDescriptor descriptor_;
};

class FrozenMaterialPressureRatioModel final
    : public ComponentModel {
public:
    FrozenMaterialPressureRatioModel()
        : descriptor_(make_descriptor(
              "transport.material.frozen_pressure_ratio",
              {{"inlet", "material", "in"},
               {"outlet", "material", "out"}})) {
        descriptor_.parameters = {
            {"pressure_ratio", "dimensionless", true,
             std::nullopt, 0.0, 1.0, false, true}};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto inlet_species =
            require_port_species(context, "inlet");
        const auto outlet_species =
            require_port_species(context, "outlet");
        if (inlet_species != outlet_species) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' material ports must use the same species basis");
        }
        const double ratio = required_parameter(
            context.component, "pressure_ratio");
        const std::string prefix =
            "component." + context.component.id + ".";
        system.add_linear_equation(
            prefix + "pressure_ratio",
            {{require_port_variable(context, "outlet.p"), 1.0},
             {require_port_variable(context, "inlet.p"), -ratio}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "frozen_enthalpy",
            {{require_port_variable(context, "outlet.h"), 1.0},
             {require_port_variable(context, "inlet.h"), -1.0}},
            0.0, 100000.0);
        for (const auto& species : inlet_species) {
            const auto variable = "m_dot[" + species + "]";
            system.add_linear_equation(
                prefix + "species." + species,
                {{require_port_variable(
                      context, "outlet." + variable), 1.0},
                 {require_port_variable(
                      context, "inlet." + variable), -1.0}},
                0.0, 100.0);
        }
    }

private:
    ComponentModelDescriptor descriptor_;
};

class TwoInletMaterialMixerModel final : public ComponentModel {
public:
    TwoInletMaterialMixerModel()
        : descriptor_(make_descriptor(
              "junction.material.mixer.two_inlet",
              {{"inlet_a", "material", "in"},
               {"inlet_b", "material", "in"},
               {"outlet", "material", "out"}})) {}

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        require_same_material(
            context, {"inlet_a", "inlet_b", "outlet"});
        const auto species =
            require_port_species(context, "inlet_a");
        if (species != require_port_species(context, "inlet_b") ||
            species != require_port_species(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' material ports must use the same species basis");
        }

        std::vector<std::size_t> inlet_a_flows;
        std::vector<std::size_t> inlet_b_flows;
        std::vector<std::size_t> outlet_flows;
        const std::string prefix =
            "component." + context.component.id + ".";
        for (const auto& name : species) {
            const std::string variable =
                "m_dot[" + name + "]";
            const auto inlet_a = require_port_variable(
                context, "inlet_a." + variable);
            const auto inlet_b = require_port_variable(
                context, "inlet_b." + variable);
            const auto outlet = require_port_variable(
                context, "outlet." + variable);
            inlet_a_flows.push_back(inlet_a);
            inlet_b_flows.push_back(inlet_b);
            outlet_flows.push_back(outlet);
            system.add_linear_equation(
                prefix + "species_balance." + name,
                {{outlet, 1.0},
                 {inlet_a, -1.0},
                 {inlet_b, -1.0}},
                0.0, 100.0);
        }

        const auto pressure_a =
            require_port_variable(context, "inlet_a.p");
        const auto pressure_b =
            require_port_variable(context, "inlet_b.p");
        const auto pressure_out =
            require_port_variable(context, "outlet.p");
        system.add_linear_equation(
            prefix + "pressure_inlet_a",
            {{pressure_out, 1.0}, {pressure_a, -1.0}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "pressure_inlet_b",
            {{pressure_out, 1.0}, {pressure_b, -1.0}},
            0.0, 100000.0);

        const auto enthalpy_a =
            require_port_variable(context, "inlet_a.h");
        const auto enthalpy_b =
            require_port_variable(context, "inlet_b.h");
        const auto enthalpy_out =
            require_port_variable(context, "outlet.h");
        std::vector<std::size_t> energy_variables;
        energy_variables.reserve(
            inlet_a_flows.size() + inlet_b_flows.size() + 3);
        energy_variables.insert(
            energy_variables.end(),
            inlet_a_flows.begin(), inlet_a_flows.end());
        energy_variables.insert(
            energy_variables.end(),
            inlet_b_flows.begin(), inlet_b_flows.end());
        energy_variables.push_back(enthalpy_a);
        energy_variables.push_back(enthalpy_b);
        energy_variables.push_back(enthalpy_out);
        system.add_checked_sparse_equation(
            prefix + "energy_balance",
            [inlet_a_flows, inlet_b_flows, enthalpy_a,
             enthalpy_b, enthalpy_out](
                const std::vector<double>& x,
                double& residual) {
                double mass_a = 0.0;
                double mass_b = 0.0;
                for (const auto variable : inlet_a_flows) {
                    mass_a += x.at(variable);
                }
                for (const auto variable : inlet_b_flows) {
                    mass_b += x.at(variable);
                }
                const double total_mass = mass_a + mass_b;
                if (!std::isfinite(total_mass) ||
                    total_mass <= 0.0) {
                    return EvaluationStatus::recoverable(
                        "material mixer requires positive finite "
                        "inlet mass flow");
                }
                const double mixed_enthalpy =
                    (mass_a * x.at(enthalpy_a) +
                     mass_b * x.at(enthalpy_b)) /
                    total_mass;
                residual =
                    x.at(enthalpy_out) - mixed_enthalpy;
                return EvaluationStatus::success();
            },
            std::move(energy_variables),
            [inlet_a_flows, inlet_b_flows, enthalpy_a,
             enthalpy_b, enthalpy_out](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                double mass_a = 0.0;
                double mass_b = 0.0;
                for (const auto variable : inlet_a_flows) {
                    mass_a += x.at(variable);
                }
                for (const auto variable : inlet_b_flows) {
                    mass_b += x.at(variable);
                }
                const double total_mass = mass_a + mass_b;
                const double mixed_enthalpy =
                    (mass_a * x.at(enthalpy_a) +
                     mass_b * x.at(enthalpy_b)) /
                    total_mass;
                for (const auto variable : inlet_a_flows) {
                    jacobian.push_back(
                        {variable,
                         -(x.at(enthalpy_a) -
                           mixed_enthalpy) /
                             total_mass});
                }
                for (const auto variable : inlet_b_flows) {
                    jacobian.push_back(
                        {variable,
                         -(x.at(enthalpy_b) -
                           mixed_enthalpy) /
                             total_mass});
                }
                jacobian.push_back(
                    {enthalpy_a, -mass_a / total_mass});
                jacobian.push_back(
                    {enthalpy_b, -mass_b / total_mass});
                jacobian.push_back(
                    {enthalpy_out, 1.0});
                return x.at(enthalpy_out) -
                    mixed_enthalpy;
            },
            100000.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class FixedFractionMaterialSplitterModel final
    : public ComponentModel {
public:
    FixedFractionMaterialSplitterModel()
        : descriptor_(make_descriptor(
              "junction.material.splitter.fixed_fraction",
              {{"inlet", "material", "in"},
               {"outlet_a", "material", "out"},
               {"outlet_b", "material", "out"}})) {
        descriptor_.parameters = {
            {"outlet_a_fraction", "dimensionless", true,
             std::nullopt, 0.0, 1.0, true, true}};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        require_same_material(
            context, {"inlet", "outlet_a", "outlet_b"});
        const auto species =
            require_port_species(context, "inlet");
        if (species !=
                require_port_species(context, "outlet_a") ||
            species !=
                require_port_species(context, "outlet_b")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' material ports must use the same species basis");
        }
        const double fraction = required_parameter(
            context.component, "outlet_a_fraction");
        const std::string prefix =
            "component." + context.component.id + ".";
        for (const auto& name : species) {
            const std::string variable =
                "m_dot[" + name + "]";
            const auto inlet = require_port_variable(
                context, "inlet." + variable);
            system.add_linear_equation(
                prefix + "outlet_a_species." + name,
                {{require_port_variable(
                      context, "outlet_a." + variable), 1.0},
                 {inlet, -fraction}},
                0.0, 100.0);
            system.add_linear_equation(
                prefix + "outlet_b_species." + name,
                {{require_port_variable(
                      context, "outlet_b." + variable), 1.0},
                 {inlet, -(1.0 - fraction)}},
                0.0, 100.0);
        }

        const auto inlet_p =
            require_port_variable(context, "inlet.p");
        const auto inlet_h =
            require_port_variable(context, "inlet.h");
        for (const auto& outlet :
             std::vector<std::string>{"outlet_a", "outlet_b"}) {
            system.add_linear_equation(
                prefix + outlet + "_pressure",
                {{require_port_variable(
                      context, outlet + ".p"), 1.0},
                 {inlet_p, -1.0}},
                0.0, 100000.0);
            system.add_linear_equation(
                prefix + outlet + "_enthalpy",
                {{require_port_variable(
                      context, outlet + ".h"), 1.0},
                 {inlet_h, -1.0}},
                0.0, 100000.0);
        }
    }

private:
    ComponentModelDescriptor descriptor_;
};

}  // namespace

void register_transport_component_models(
    ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<TwoInletFluidMixerModel>());
    registry.register_model(
        std::make_shared<TwoOutletFluidSplitterModel>());
    registry.register_model(
        std::make_shared<
            IsenthalpicPressureRatioValveModel>());
    registry.register_model(
        std::make_shared<FlowAreaRestrictionModel>(false));
    registry.register_model(
        std::make_shared<FlowAreaRestrictionModel>(true));
    registry.register_model(
        std::make_shared<ReturnBendFixedLossModel>());
    registry.register_model(
        std::make_shared<ReturnBendCorrelationModel>());
    registry.register_model(
        std::make_shared<DarcyWeisbachPipeModel>(false));
    registry.register_model(
        std::make_shared<DarcyWeisbachPipeModel>(true));
    registry.register_model(
        std::make_shared<FrozenMaterialPressureRatioModel>());
    registry.register_model(
        std::make_shared<TwoInletMaterialMixerModel>());
    registry.register_model(
        std::make_shared<
            FixedFractionMaterialSplitterModel>());
}

}  // namespace thermox::platform
