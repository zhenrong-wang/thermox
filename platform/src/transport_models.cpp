#include "component_modules.hpp"
#include "component_model_support.hpp"

#include "thermox/platform/two_phase_flow_groups.hpp"

#include <algorithm>
#include <array>
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
using component_model_support::require_thermochemistry_package;
using component_model_support::require_internal_variable;
using component_model_support::require_correlation;
using component_model_support::require_performance_map;
using component_model_support::optional_regime_map;
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
               {"outlet", "fluid", "out"}})) {
        descriptor_.supports_transient = true;
    }

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

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
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
            {{mass_out, 1.0, 0.0}, {mass_a, -1.0, 0.0},
             {mass_b, -1.0, 0.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "pressure_inlet_a",
            {{pressure_out, 1.0, 0.0},
             {pressure_a, -1.0, 0.0}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "pressure_inlet_b",
            {{pressure_out, 1.0, 0.0},
             {pressure_b, -1.0, 0.0}},
            0.0, 100000.0);
        system.add_sparse_equation(
            prefix + "energy_balance",
            {mass_a, enthalpy_a, mass_b, enthalpy_b,
             mass_out, enthalpy_out},
            [mass_a, enthalpy_a, mass_b, enthalpy_b,
             mass_out, enthalpy_out](
                double, const std::vector<double>& x,
                const std::vector<double>&, double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                residual =
                    x.at(mass_out) * x.at(enthalpy_out) -
                    x.at(mass_a) * x.at(enthalpy_a) -
                    x.at(mass_b) * x.at(enthalpy_b);
                jacobian.push_back(
                    {mass_out, x.at(enthalpy_out), 0.0});
                jacobian.push_back(
                    {enthalpy_out, x.at(mass_out), 0.0});
                jacobian.push_back(
                    {mass_a, -x.at(enthalpy_a), 0.0});
                jacobian.push_back(
                    {enthalpy_a, -x.at(mass_a), 0.0});
                jacobian.push_back(
                    {mass_b, -x.at(enthalpy_b), 0.0});
                jacobian.push_back(
                    {enthalpy_b, -x.at(mass_b), 0.0});
                return EvaluationStatus::success();
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
               {"outlet_b", "fluid", "out"}})) {
        descriptor_.supports_transient = true;
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

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
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
            {{mass_in, 1.0, 0.0}, {mass_a, -1.0, 0.0},
             {mass_b, -1.0, 0.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "pressure_outlet_a",
            {{pressure_a, 1.0, 0.0},
             {pressure_in, -1.0, 0.0}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "pressure_outlet_b",
            {{pressure_b, 1.0, 0.0},
             {pressure_in, -1.0, 0.0}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "enthalpy_outlet_a",
            {{enthalpy_a, 1.0, 0.0},
             {enthalpy_in, -1.0, 0.0}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "enthalpy_outlet_b",
            {{enthalpy_b, 1.0, 0.0},
             {enthalpy_in, -1.0, 0.0}},
            0.0, 100000.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class FluidHydraulicInertanceModel final : public ComponentModel {
public:
    FluidHydraulicInertanceModel()
        : descriptor_(make_descriptor(
              "pipe.fluid.hydraulic_inertance",
              {{"inlet", "fluid", "in"},
               {"outlet", "fluid", "out"}})) {
        descriptor_.template_kind = "pipe.fluid";
        descriptor_.display_name = "Fluid hydraulic inertance";
        descriptor_.category = "Fluid transport";
        descriptor_.model_name = "Lumped one-dimensional momentum storage";
        descriptor_.parameters = {
            {"length", "length", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"flow_diameter", "length", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
        };
        descriptor_.supports_steady = false;
        descriptor_.supports_transient = true;
        descriptor_.transient_variables = {
            {"outlet", "m_dot", DaeVariableKind::differential, 1.0},
        };
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext&,
        EquationSystemBuilder&) const override {
        throw std::logic_error(
            "fluid hydraulic inertance is a transient-only component");
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        const auto properties =
            require_property_package(context, "inlet");
        if (properties != require_property_package(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' inlet and outlet must use the same medium");
        }
        const double length =
            required_parameter(context.component, "length");
        const double diameter =
            required_parameter(context.component, "flow_diameter");
        const double area = std::numbers::pi * diameter * diameter /
            4.0;
        const double inertance = length / area;
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
            {{outlet_m, 1.0, 0.0}, {inlet_m, -1.0, 0.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "isenthalpic_transport",
            {{outlet_h, 1.0, 0.0}, {inlet_h, -1.0, 0.0}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "momentum_accumulation",
            {{inlet_p, 1.0, 0.0}, {outlet_p, -1.0, 0.0},
             {outlet_m, 0.0, -inertance}},
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

EvaluationStatus assemble_restriction_numerical_dae_row(
    const RestrictionEvaluator& evaluate,
    const std::vector<std::pair<std::size_t, double>>& variables,
    const std::vector<double>& x,
    double& residual,
    std::vector<DaeEquationPartial>& jacobian) {
    const auto base = evaluate(x);
    if (!base.status.ok()) return base.status;
    residual = base.residual;
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
            return EvaluationStatus::recoverable(
                "could not evaluate a local restriction-model "
                "derivative");
        }
        jacobian.push_back({variable, derivative, 0.0});
    }
    return EvaluationStatus::success();
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

class HomogeneousTwoPhaseLocalLossModel final
    : public ComponentModel {
public:
    HomogeneousTwoPhaseLocalLossModel()
        : descriptor_(make_descriptor(
              "restriction.fluid.local_loss.homogeneous_two_phase",
              {{"inlet", "fluid", "in"},
               {"outlet", "fluid", "out"}})) {
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "restriction.fluid.local_loss";
        descriptor_.display_name =
            "Homogeneous two-phase local loss";
        descriptor_.category = "Fluid control";
        descriptor_.model_name =
            "Homogeneous-equilibrium mixture-density loss";
        descriptor_.parameters = {
            {"flow_diameter", "length", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"loss_coefficient", "dimensionless", true,
             std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
        };
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph};
        descriptor_.supports_steady = true;
        descriptor_.supports_transient = true;
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto data = compile_data(context);
        const std::string prefix =
            "component." + context.component.id + ".";
        system.add_linear_equation(
            prefix + "mass_continuity",
            {{data.outlet_m, 1.0}, {data.inlet_m, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "isenthalpic_transport",
            {{data.outlet_h, 1.0}, {data.inlet_h, -1.0}},
            0.0, 100000.0);
        const auto evaluate = evaluator(data);
        const auto derivatives = derivative_variables(data);
        system.add_checked_sparse_equation(
            prefix + "homogeneous_two_phase_pressure_loss",
            [evaluate](const std::vector<double>& x,
                       double& residual) {
                const auto result = evaluate(x);
                residual = result.residual;
                return result.status;
            },
            variable_pattern(derivatives),
            [evaluate, derivatives](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                return assemble_restriction_numerical_row(
                    evaluate, derivatives, x, jacobian);
            },
            100000.0);
        system.add_initialization_relation(
            {{data.outlet_p, 1.0}, {data.inlet_p, -0.99}},
            0.0);
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        const auto data = compile_data(context);
        const std::string prefix =
            "component." + context.component.id + ".";
        system.add_linear_equation(
            prefix + "mass_continuity",
            {{data.outlet_m, 1.0, 0.0},
             {data.inlet_m, -1.0, 0.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "isenthalpic_transport",
            {{data.outlet_h, 1.0, 0.0},
             {data.inlet_h, -1.0, 0.0}},
            0.0, 100000.0);
        const auto evaluate = evaluator(data);
        const auto derivatives = derivative_variables(data);
        system.add_sparse_equation(
            prefix + "homogeneous_two_phase_pressure_loss",
            variable_pattern(derivatives),
            [evaluate, derivatives](
                double, const std::vector<double>& x,
                const std::vector<double>&, double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                return assemble_restriction_numerical_dae_row(
                    evaluate, derivatives, x, residual, jacobian);
            },
            100000.0);
    }

private:
    struct Data {
        std::shared_ptr<const physics::PropertyPackage> properties;
        std::size_t inlet_m{}, inlet_p{}, inlet_h{};
        std::size_t outlet_m{}, outlet_p{}, outlet_h{};
        double loss_scale{};
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
        data.inlet_m =
            require_port_variable(context, "inlet.m_dot");
        data.inlet_p = require_port_variable(context, "inlet.p");
        data.inlet_h = require_port_variable(context, "inlet.h");
        data.outlet_m =
            require_port_variable(context, "outlet.m_dot");
        data.outlet_p = require_port_variable(context, "outlet.p");
        data.outlet_h = require_port_variable(context, "outlet.h");
        const double diameter =
            required_parameter(context.component, "flow_diameter");
        const double area = std::numbers::pi * diameter * diameter /
            4.0;
        data.loss_scale = required_parameter(
            context.component, "loss_coefficient") /
            (2.0 * area * area);
        return data;
    }

    static RestrictionEvaluator evaluator(const Data& data) {
        return [data](const std::vector<double>& x) {
            const double mass_flow = x.at(data.inlet_m);
            const double inlet_pressure = x.at(data.inlet_p);
            const double outlet_pressure = x.at(data.outlet_p);
            if (!std::isfinite(mass_flow) || mass_flow <= 0.0 ||
                !std::isfinite(inlet_pressure) ||
                !std::isfinite(outlet_pressure) ||
                outlet_pressure <= 0.0 ||
                outlet_pressure >= inlet_pressure) {
                return RestrictionEvaluation{
                    EvaluationStatus::recoverable(
                        "homogeneous two-phase local loss requires "
                        "positive forward flow and 0 < outlet "
                        "pressure < inlet pressure")};
            }
            const auto state = data.properties->state_ph(
                0.5 * (inlet_pressure + outlet_pressure),
                x.at(data.inlet_h));
            if (!state.ok()) {
                return RestrictionEvaluation{property_failure(state)};
            }
            if (state.state.phase != physics::Phase::two_phase) {
                return RestrictionEvaluation{
                    EvaluationStatus::recoverable(
                        "homogeneous two-phase local loss requires a "
                        "two-phase mean state")};
            }
            const double density = state.state.density_kg_m3;
            if (!std::isfinite(density) || density <= 0.0) {
                return RestrictionEvaluation{
                    EvaluationStatus::recoverable(
                        "homogeneous two-phase local loss requires "
                        "positive finite mixture density")};
            }
            return RestrictionEvaluation{
                EvaluationStatus::success(),
                inlet_pressure - outlet_pressure -
                    data.loss_scale * mass_flow *
                        std::abs(mass_flow) / density};
        };
    }

    static std::vector<std::pair<std::size_t, double>>
    derivative_variables(const Data& data) {
        return {
            {data.inlet_m, 1.0e-7},
            {data.inlet_p, 0.1},
            {data.inlet_h, 0.1},
            {data.outlet_p, 0.1},
        };
    }

    static std::vector<std::size_t> variable_pattern(
        const std::vector<std::pair<std::size_t, double>>& variables) {
        std::vector<std::size_t> pattern;
        pattern.reserve(variables.size());
        for (const auto& [variable, _] : variables) {
            pattern.push_back(variable);
        }
        return pattern;
    }

    ComponentModelDescriptor descriptor_;
};

class HomogeneousEquilibriumGravityPipeModel final
    : public ComponentModel {
public:
    HomogeneousEquilibriumGravityPipeModel()
        : descriptor_(make_descriptor(
              "pipe.fluid.homogeneous_equilibrium_local_loss",
              {{"inlet", "fluid", "in"},
               {"outlet", "fluid", "out"}})) {
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "pipe.fluid";
        descriptor_.display_name =
            "Homogeneous-equilibrium gravity pipe";
        descriptor_.category = "Fluid transport";
        descriptor_.model_name =
            "Density-based local loss with elevation head";
        descriptor_.parameters = {
            {"flow_diameter", "length", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"loss_coefficient", "dimensionless", true,
             std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), true, true},
            {"elevation_change", "length", false, 0.0,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(), true, true},
        };
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph};
        descriptor_.supports_steady = true;
        descriptor_.supports_transient = true;
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto data = compile_data(context);
        const std::string prefix =
            "component." + context.component.id + ".";
        system.add_linear_equation(
            prefix + "mass_continuity",
            {{data.outlet_m, 1.0}, {data.inlet_m, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "isenthalpic_transport",
            {{data.outlet_h, 1.0}, {data.inlet_h, -1.0}},
            0.0, 100000.0);
        const auto evaluate = evaluator(data);
        const auto derivatives = derivative_variables(data);
        system.add_checked_sparse_equation(
            prefix + "gravity_friction_pressure_balance",
            [evaluate](const std::vector<double>& x,
                       double& residual) {
                const auto result = evaluate(x);
                residual = result.residual;
                return result.status;
            },
            variable_pattern(derivatives),
            [evaluate, derivatives](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                return assemble_restriction_numerical_row(
                    evaluate, derivatives, x, jacobian);
            },
            100000.0);
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        const auto data = compile_data(context);
        const std::string prefix =
            "component." + context.component.id + ".";
        system.add_linear_equation(
            prefix + "mass_continuity",
            {{data.outlet_m, 1.0, 0.0},
             {data.inlet_m, -1.0, 0.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "isenthalpic_transport",
            {{data.outlet_h, 1.0, 0.0},
             {data.inlet_h, -1.0, 0.0}},
            0.0, 100000.0);
        const auto evaluate = evaluator(data);
        const auto derivatives = derivative_variables(data);
        system.add_sparse_equation(
            prefix + "gravity_friction_pressure_balance",
            variable_pattern(derivatives),
            [evaluate, derivatives](
                double, const std::vector<double>& x,
                const std::vector<double>&, double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                return assemble_restriction_numerical_dae_row(
                    evaluate, derivatives, x, residual, jacobian);
            },
            100000.0);
    }

private:
    struct Data {
        std::shared_ptr<const physics::PropertyPackage> properties;
        std::size_t inlet_m{}, inlet_p{}, inlet_h{};
        std::size_t outlet_m{}, outlet_p{}, outlet_h{};
        double loss_scale{};
        double elevation_change{};
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
        data.inlet_m =
            require_port_variable(context, "inlet.m_dot");
        data.inlet_p = require_port_variable(context, "inlet.p");
        data.inlet_h = require_port_variable(context, "inlet.h");
        data.outlet_m =
            require_port_variable(context, "outlet.m_dot");
        data.outlet_p = require_port_variable(context, "outlet.p");
        data.outlet_h = require_port_variable(context, "outlet.h");
        const double diameter =
            required_parameter(context.component, "flow_diameter");
        const double area = std::numbers::pi * diameter * diameter /
            4.0;
        data.loss_scale = required_parameter(
            context.component, "loss_coefficient") /
            (2.0 * area * area);
        data.elevation_change = parameter_or(
            context.component, "elevation_change", 0.0);
        return data;
    }

    static RestrictionEvaluator evaluator(const Data& data) {
        return [data](const std::vector<double>& x) {
            constexpr double gravity = 9.80665;
            const double inlet_pressure = x.at(data.inlet_p);
            const double outlet_pressure = x.at(data.outlet_p);
            const double mean_pressure =
                0.5 * (inlet_pressure + outlet_pressure);
            if (!std::isfinite(mean_pressure) ||
                mean_pressure <= 0.0) {
                return RestrictionEvaluation{
                    EvaluationStatus::recoverable(
                        "homogeneous-equilibrium gravity pipe requires "
                        "positive finite mean pressure")};
            }
            const auto state = data.properties->state_ph(
                mean_pressure, x.at(data.inlet_h));
            if (!state.ok()) {
                return RestrictionEvaluation{property_failure(state)};
            }
            const double density = state.state.density_kg_m3;
            if (!std::isfinite(density) || density <= 0.0) {
                return RestrictionEvaluation{
                    EvaluationStatus::recoverable(
                        "homogeneous-equilibrium gravity pipe requires "
                        "positive finite mixture density")};
            }
            const double mass_flow = x.at(data.inlet_m);
            if (!std::isfinite(mass_flow)) {
                return RestrictionEvaluation{
                    EvaluationStatus::recoverable(
                        "homogeneous-equilibrium gravity pipe requires "
                        "finite mass flow")};
            }
            return RestrictionEvaluation{
                EvaluationStatus::success(),
                inlet_pressure - outlet_pressure -
                    data.loss_scale * mass_flow *
                        std::abs(mass_flow) / density -
                    density * gravity * data.elevation_change};
        };
    }

    static std::vector<std::pair<std::size_t, double>>
    derivative_variables(const Data& data) {
        return {
            {data.inlet_m, 1.0e-7},
            {data.inlet_p, 0.1},
            {data.inlet_h, 0.1},
            {data.outlet_p, 0.1},
        };
    }

    static std::vector<std::size_t> variable_pattern(
        const std::vector<std::pair<std::size_t, double>>& variables) {
        std::vector<std::size_t> pattern;
        pattern.reserve(variables.size());
        for (const auto& [variable, _] : variables) {
            pattern.push_back(variable);
        }
        return pattern;
    }

    ComponentModelDescriptor descriptor_;
};

class SlipAwareTwoPhaseGravityPipeModel final
    : public ComponentModel {
public:
    explicit SlipAwareTwoPhaseGravityPipeModel(
        bool correlation_driven)
        : descriptor_(make_descriptor(
              correlation_driven
                  ? "pipe.fluid.correlated_two_phase_pressure_drop"
                  : "pipe.fluid.constant_slip_two_phase_local_loss",
              {{"inlet", "fluid", "in"},
               {"outlet", "fluid", "out"}})),
          correlation_driven_(correlation_driven) {
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "pipe.fluid";
        descriptor_.display_name = correlation_driven_
            ? "Correlation-driven two-phase pressure-drop pipe"
            : "Constant-slip two-phase gravity pipe";
        descriptor_.category = "Fluid transport";
        descriptor_.model_name = correlation_driven_
            ? "Correlated void fraction and distributed friction"
            : "Separated-flow mixture density with elevation head";
        descriptor_.parameters = {
            {"flow_diameter", "length", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"loss_coefficient", "dimensionless",
             !correlation_driven_,
             correlation_driven_
                 ? std::optional<double>{0.0}
                 : std::nullopt,
             0.0,
             std::numeric_limits<double>::infinity(), true, true},
            {"elevation_change", "length", false, 0.0,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(), true, true},
        };
        if (correlation_driven_) {
            descriptor_.parameters.insert(
                descriptor_.parameters.begin() + 1,
                {"length", "length", true, std::nullopt, 0.0,
                 std::numeric_limits<double>::infinity(), false, true});
            descriptor_.parameters.insert(
                descriptor_.parameters.begin() + 2,
                {"roughness", "length", false, 0.0, 0.0,
                 std::numeric_limits<double>::infinity(), true, true});
            descriptor_.artifacts = {
                {"void_fraction_correlation",
                 correlation_artifact_type, true},
                {"friction_pressure_gradient_correlation",
                 correlation_artifact_type, true},
                {"friction_regime_map",
                 regime_map_artifact_type, false},
            };
        } else {
            descriptor_.parameters.push_back(
                {"slip_ratio", "dimensionless", true, std::nullopt,
                 0.0, std::numeric_limits<double>::infinity(),
                 false, true});
        }
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph,
            physics::PropertyCapability::saturation_p};
        descriptor_.supports_steady = true;
        descriptor_.supports_transient = true;
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto data = compile_data(context, correlation_driven_);
        const std::string prefix =
            "component." + context.component.id + ".";
        system.add_linear_equation(
            prefix + "mass_continuity",
            {{data.outlet_m, 1.0}, {data.inlet_m, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "isenthalpic_transport",
            {{data.outlet_h, 1.0}, {data.inlet_h, -1.0}},
            0.0, 100000.0);
        const auto evaluate = evaluator(data);
        const auto derivatives = derivative_variables(data);
        system.add_checked_sparse_equation(
            prefix + (correlation_driven_
                ? "correlated_two_phase_pressure_balance"
                : "constant_slip_pressure_balance"),
            [evaluate](const std::vector<double>& x,
                       double& residual) {
                const auto result = evaluate(x);
                residual = result.residual;
                return result.status;
            },
            variable_pattern(derivatives),
            [evaluate, derivatives](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                return assemble_restriction_numerical_row(
                    evaluate, derivatives, x, jacobian);
            },
            100000.0);
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        const auto data = compile_data(context, correlation_driven_);
        const std::string prefix =
            "component." + context.component.id + ".";
        system.add_linear_equation(
            prefix + "mass_continuity",
            {{data.outlet_m, 1.0, 0.0},
             {data.inlet_m, -1.0, 0.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "isenthalpic_transport",
            {{data.outlet_h, 1.0, 0.0},
             {data.inlet_h, -1.0, 0.0}},
            0.0, 100000.0);
        const auto evaluate = evaluator(data);
        const auto derivatives = derivative_variables(data);
        system.add_sparse_equation(
            prefix + (correlation_driven_
                ? "correlated_two_phase_pressure_balance"
                : "constant_slip_pressure_balance"),
            variable_pattern(derivatives),
            [evaluate, derivatives](
                double, const std::vector<double>& x,
                const std::vector<double>&, double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                return assemble_restriction_numerical_dae_row(
                    evaluate, derivatives, x, residual, jacobian);
            },
            100000.0);
    }

private:
    struct Data {
        std::shared_ptr<const physics::PropertyPackage> properties;
        std::size_t inlet_m{}, inlet_p{}, inlet_h{};
        std::size_t outlet_m{}, outlet_p{}, outlet_h{};
        double loss_scale{};
        double area{};
        double diameter{};
        double length{};
        double roughness{};
        double elevation_change{};
        double slip_ratio{};
        bool include_acceleration{false};
        std::shared_ptr<const CorrelationArtifact>
            void_fraction_correlation;
        std::shared_ptr<const CorrelationArtifact>
            friction_pressure_gradient_correlation;
        std::shared_ptr<const RegimeMapArtifact>
            friction_regime_map;
    };

    static Data compile_data(
        const ComponentCompileContext& context,
        bool correlation_driven) {
        Data data;
        data.properties = require_property_package(context, "inlet");
        if (data.properties !=
            require_property_package(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' inlet and outlet must use the same medium");
        }
        data.inlet_m =
            require_port_variable(context, "inlet.m_dot");
        data.inlet_p = require_port_variable(context, "inlet.p");
        data.inlet_h = require_port_variable(context, "inlet.h");
        data.outlet_m =
            require_port_variable(context, "outlet.m_dot");
        data.outlet_p = require_port_variable(context, "outlet.p");
        data.outlet_h = require_port_variable(context, "outlet.h");
        data.diameter =
            required_parameter(context.component, "flow_diameter");
        data.area = std::numbers::pi * data.diameter *
            data.diameter / 4.0;
        data.loss_scale = parameter_or(
            context.component, "loss_coefficient", 0.0) /
            (2.0 * data.area * data.area);
        data.elevation_change = parameter_or(
            context.component, "elevation_change", 0.0);
        if (correlation_driven) {
            data.include_acceleration = true;
            data.length = required_parameter(
                context.component, "length");
            data.roughness = parameter_or(
                context.component, "roughness", 0.0);
            data.void_fraction_correlation = require_correlation(
                context, "void_fraction_correlation");
            if (data.void_fraction_correlation->output().name !=
                    "void_fraction" ||
                data.void_fraction_correlation->output().dimension !=
                    "dimensionless") {
                throw std::invalid_argument(
                    "void-fraction correlation output must be named "
                    "'void_fraction' with dimensionless dimension");
            }
            const std::map<std::string, std::string>
                supported_inputs{
                    {"vapor_quality", "dimensionless"},
                    {"liquid_density", "density"},
                    {"vapor_density", "density"},
                    {"mass_flow", "mass_flow"},
                    {"mass_flux", "mass_flux"},
                    {"area", "area"},
                    {"diameter", "length"},
                    {"pressure", "pressure"},
                };
            for (const auto& input :
                 data.void_fraction_correlation->inputs()) {
                const auto supported =
                    supported_inputs.find(input.name);
                if (supported == supported_inputs.end()) {
                    throw std::invalid_argument(
                        "void-fraction correlation has unsupported "
                        "input: " + input.name);
                }
                if (input.dimension != supported->second) {
                    throw std::invalid_argument(
                        "void-fraction correlation input '" +
                        input.name + "' must have dimension '" +
                        supported->second + "'");
                }
            }
            data.friction_pressure_gradient_correlation =
                require_correlation(
                    context,
                    "friction_pressure_gradient_correlation");
            if (data.friction_pressure_gradient_correlation->output().name !=
                    "friction_pressure_gradient" ||
                data.friction_pressure_gradient_correlation->output()
                        .dimension != "pressure_gradient") {
                throw std::invalid_argument(
                    "friction correlation output must be named "
                    "'friction_pressure_gradient' with "
                    "pressure_gradient dimension");
            }
            const std::map<std::string, std::string>
                supported_friction_inputs{
                    {"vapor_quality", "dimensionless"},
                    {"void_fraction", "dimensionless"},
                    {"mixture_density", "density"},
                    {"liquid_density", "density"},
                    {"vapor_density", "density"},
                    {"liquid_viscosity", "dynamic_viscosity"},
                    {"vapor_viscosity", "dynamic_viscosity"},
                    {"mass_flow", "mass_flow"},
                    {"mass_flux", "mass_flux"},
                    {"liquid_mass_flux", "mass_flux"},
                    {"vapor_mass_flux", "mass_flux"},
                    {"liquid_reynolds_number", "dimensionless"},
                    {"vapor_reynolds_number", "dimensionless"},
                    {"area", "area"},
                    {"diameter", "length"},
                    {"length", "length"},
                    {"roughness", "length"},
                    {"pressure", "pressure"},
                };
            for (const auto& input :
                 data.friction_pressure_gradient_correlation->inputs()) {
                const auto supported =
                    supported_friction_inputs.find(input.name);
                if (supported == supported_friction_inputs.end()) {
                    throw std::invalid_argument(
                        "friction pressure-gradient correlation has "
                        "unsupported input: " + input.name);
                }
                if (input.dimension != supported->second) {
                    throw std::invalid_argument(
                        "friction pressure-gradient correlation input '" +
                        input.name + "' must have dimension '" +
                        supported->second + "'");
                }
            }
            data.friction_regime_map = optional_regime_map(
                context, "friction_regime_map");
            if (data.friction_regime_map) {
                if (!data.properties->supports(
                        physics::PropertyCapability::surface_tension)) {
                    throw std::invalid_argument(
                        "friction regime maps require a property backend "
                        "with surface_tension capability");
                }
                const std::map<std::string, std::string>
                    supported_regime_inputs{
                        {"vapor_quality", "dimensionless"},
                        {"liquid_density", "density"},
                        {"vapor_density", "density"},
                        {"liquid_viscosity", "dynamic_viscosity"},
                        {"vapor_viscosity", "dynamic_viscosity"},
                        {"mass_flux", "mass_flux"},
                        {"liquid_mass_flux", "mass_flux"},
                        {"vapor_mass_flux", "mass_flux"},
                        {"liquid_superficial_velocity", "speed"},
                        {"vapor_superficial_velocity", "speed"},
                        {"liquid_reynolds_number", "dimensionless"},
                        {"vapor_reynolds_number", "dimensionless"},
                        {"liquid_froude_number", "dimensionless"},
                        {"vapor_froude_number", "dimensionless"},
                        {"liquid_weber_number", "dimensionless"},
                        {"vapor_weber_number", "dimensionless"},
                        {"bond_number", "dimensionless"},
                        {"density_ratio_liquid_to_vapor",
                         "dimensionless"},
                        {"viscosity_ratio_liquid_to_vapor",
                         "dimensionless"},
                        {"diameter", "length"},
                        {"surface_tension", "surface_tension"},
                        {"pressure", "pressure"},
                        {"void_fraction", "dimensionless"},
                        {"gravity", "acceleration"},
                    };
                for (const auto& input :
                     data.friction_regime_map->inputs()) {
                    const auto supported =
                        supported_regime_inputs.find(input.name);
                    if (supported == supported_regime_inputs.end() ||
                        input.dimension != supported->second) {
                        throw std::invalid_argument(
                            "friction regime-map input has unsupported "
                            "name or dimension: " + input.name);
                    }
                }
                for (const auto& region :
                     data.friction_regime_map->regions()) {
                    const bool covered = std::any_of(
                        data.friction_pressure_gradient_correlation
                            ->candidates().begin(),
                        data.friction_pressure_gradient_correlation
                            ->candidates().end(),
                        [&](const auto& candidate) {
                            return std::find(
                                candidate.flow_regimes.begin(),
                                candidate.flow_regimes.end(),
                                region.regime) !=
                                    candidate.flow_regimes.end() ||
                                candidate
                                    .fallback_for_unmapped_flow_regime;
                        });
                    if (!covered) {
                        throw std::invalid_argument(
                            "friction regime map selects regime '" +
                            region.regime +
                            "' with no matching correlation candidate");
                    }
                }
            }
        } else {
            data.slip_ratio =
                required_parameter(context.component, "slip_ratio");
        }
        return data;
    }

    static RestrictionEvaluator evaluator(const Data& data) {
        return [data](const std::vector<double>& x) {
            constexpr double gravity = 9.80665;
            const double inlet_pressure = x.at(data.inlet_p);
            const double outlet_pressure = x.at(data.outlet_p);
            const double mean_pressure =
                0.5 * (inlet_pressure + outlet_pressure);
            const double mass_flow = x.at(data.inlet_m);
            if (!std::isfinite(mean_pressure) || mean_pressure <= 0.0) {
                return RestrictionEvaluation{
                    EvaluationStatus::recoverable(
                        "two-phase pressure-drop pipe requires positive "
                        "finite mean pressure")};
            }
            if (!std::isfinite(mass_flow)) {
                return RestrictionEvaluation{
                    EvaluationStatus::recoverable(
                        "two-phase pressure-drop pipe requires finite "
                        "mass flow")};
            }
            const auto evaluate_void_fraction =
                [&](double quality, double rho_l, double rho_v,
                    double pressure) {
                    if (!data.void_fraction_correlation) {
                        const double value = 1.0 /
                            (1.0 + ((1.0 - quality) / quality) *
                                (rho_v / rho_l) * data.slip_ratio);
                        return std::pair{
                            EvaluationStatus::success(), value};
                    }
                    std::map<std::string, double> inputs;
                    for (const auto& input :
                         data.void_fraction_correlation->inputs()) {
                        if (input.name == "vapor_quality") {
                            inputs.emplace(input.name, quality);
                        } else if (input.name == "liquid_density") {
                            inputs.emplace(input.name, rho_l);
                        } else if (input.name == "vapor_density") {
                            inputs.emplace(input.name, rho_v);
                        } else if (input.name == "mass_flow") {
                            inputs.emplace(input.name, mass_flow);
                        } else if (input.name == "mass_flux") {
                            inputs.emplace(
                                input.name,
                                std::abs(mass_flow) / data.area);
                        } else if (input.name == "area") {
                            inputs.emplace(input.name, data.area);
                        } else if (input.name == "diameter") {
                            inputs.emplace(input.name, data.diameter);
                        } else if (input.name == "pressure") {
                            inputs.emplace(input.name, pressure);
                        }
                    }
                    const auto evaluated =
                        data.void_fraction_correlation->evaluate(inputs);
                    if (!evaluated.error.empty()) {
                        return std::pair{
                            EvaluationStatus::recoverable(
                                evaluated.error),
                            0.0};
                    }
                    return std::pair{
                        EvaluationStatus::success(), evaluated.value};
                };
            const auto state = data.properties->state_ph(
                mean_pressure, x.at(data.inlet_h));
            if (!state.ok()) {
                return RestrictionEvaluation{property_failure(state)};
            }
            if (state.state.phase != physics::Phase::two_phase ||
                !std::isfinite(state.state.vapor_quality) ||
                state.state.vapor_quality <= 0.0 ||
                state.state.vapor_quality >= 1.0) {
                return RestrictionEvaluation{
                    EvaluationStatus::recoverable(
                        "two-phase pressure-drop pipe requires a mean "
                        "state with 0 < vapor quality < 1")};
            }
            const auto saturation =
                data.properties->saturation_p(mean_pressure);
            if (!saturation.ok()) {
                return RestrictionEvaluation{
                    property_failure(saturation)};
            }
            const double rho_l = saturation.liquid.density_kg_m3;
            const double rho_v = saturation.vapor.density_kg_m3;
            if (!std::isfinite(rho_l) || !std::isfinite(rho_v) ||
                rho_l <= 0.0 || rho_v <= 0.0) {
                return RestrictionEvaluation{
                    EvaluationStatus::recoverable(
                        "two-phase pressure-drop pipe requires positive "
                        "finite saturation densities")};
            }
            const double quality = state.state.vapor_quality;
            const auto [void_status, void_fraction] =
                evaluate_void_fraction(
                    quality, rho_l, rho_v, mean_pressure);
            if (!void_status.ok()) {
                return RestrictionEvaluation{void_status};
            }
            if (!std::isfinite(void_fraction) ||
                void_fraction <= 0.0 || void_fraction >= 1.0) {
                return RestrictionEvaluation{
                    EvaluationStatus::recoverable(
                        "two-phase pipe void-fraction closure must "
                        "produce 0 < alpha < 1")};
            }
            const double mixture_density =
                void_fraction * rho_v +
                (1.0 - void_fraction) * rho_l;
            if (!std::isfinite(mixture_density) ||
                mixture_density <= 0.0) {
                return RestrictionEvaluation{
                    EvaluationStatus::recoverable(
                        "two-phase pressure-drop pipe requires finite "
                        "flow and positive mixture density")};
            }
            double distributed_friction_loss = 0.0;
            if (data.friction_pressure_gradient_correlation &&
                std::abs(mass_flow) > 1.0e-14) {
                std::string selected_friction_regime;
                if (data.friction_regime_map) {
                    TwoPhaseFlowGroups groups;
                    try {
                        groups = calculate_two_phase_flow_groups({
                            std::abs(mass_flow) / data.area,
                            quality,
                            rho_l,
                            rho_v,
                            saturation.liquid.viscosity_pa_s,
                            saturation.vapor.viscosity_pa_s,
                            data.diameter,
                            saturation.surface_tension_n_m,
                        });
                    } catch (const std::exception& error) {
                        return RestrictionEvaluation{
                            EvaluationStatus::recoverable(error.what())};
                    }
                    std::map<std::string, double> map_inputs;
                    for (const auto& input :
                         data.friction_regime_map->inputs()) {
                        if (input.name == "vapor_quality") {
                            map_inputs.emplace(input.name, quality);
                        } else if (input.name == "void_fraction") {
                            map_inputs.emplace(input.name, void_fraction);
                        } else if (input.name == "liquid_density") {
                            map_inputs.emplace(input.name, rho_l);
                        } else if (input.name == "vapor_density") {
                            map_inputs.emplace(input.name, rho_v);
                        } else if (input.name == "liquid_viscosity") {
                            map_inputs.emplace(
                                input.name,
                                saturation.liquid.viscosity_pa_s);
                        } else if (input.name == "vapor_viscosity") {
                            map_inputs.emplace(
                                input.name,
                                saturation.vapor.viscosity_pa_s);
                        } else if (input.name == "mass_flux") {
                            map_inputs.emplace(
                                input.name,
                                std::abs(mass_flow) / data.area);
                        } else if (input.name == "liquid_mass_flux") {
                            map_inputs.emplace(
                                input.name,
                                groups.liquid_mass_flux_kg_m2_s);
                        } else if (input.name == "vapor_mass_flux") {
                            map_inputs.emplace(
                                input.name,
                                groups.vapor_mass_flux_kg_m2_s);
                        } else if (input.name ==
                                   "liquid_superficial_velocity") {
                            map_inputs.emplace(
                                input.name,
                                groups.liquid_superficial_velocity_m_s);
                        } else if (input.name ==
                                   "vapor_superficial_velocity") {
                            map_inputs.emplace(
                                input.name,
                                groups.vapor_superficial_velocity_m_s);
                        } else if (input.name ==
                                   "liquid_reynolds_number") {
                            map_inputs.emplace(
                                input.name,
                                groups.liquid_reynolds_number);
                        } else if (input.name ==
                                   "vapor_reynolds_number") {
                            map_inputs.emplace(
                                input.name,
                                groups.vapor_reynolds_number);
                        } else if (input.name ==
                                   "liquid_froude_number") {
                            map_inputs.emplace(
                                input.name,
                                groups.liquid_froude_number);
                        } else if (input.name ==
                                   "vapor_froude_number") {
                            map_inputs.emplace(
                                input.name,
                                groups.vapor_froude_number);
                        } else if (input.name ==
                                   "liquid_weber_number") {
                            map_inputs.emplace(
                                input.name,
                                groups.liquid_weber_number);
                        } else if (input.name ==
                                   "vapor_weber_number") {
                            map_inputs.emplace(
                                input.name,
                                groups.vapor_weber_number);
                        } else if (input.name == "bond_number") {
                            map_inputs.emplace(
                                input.name, groups.bond_number);
                        } else if (input.name ==
                                   "density_ratio_liquid_to_vapor") {
                            map_inputs.emplace(
                                input.name,
                                groups.density_ratio_liquid_to_vapor);
                        } else if (input.name ==
                                   "viscosity_ratio_liquid_to_vapor") {
                            map_inputs.emplace(
                                input.name,
                                groups.viscosity_ratio_liquid_to_vapor);
                        } else if (input.name == "diameter") {
                            map_inputs.emplace(input.name, data.diameter);
                        } else if (input.name == "surface_tension") {
                            map_inputs.emplace(
                                input.name,
                                saturation.surface_tension_n_m);
                        } else if (input.name == "pressure") {
                            map_inputs.emplace(input.name, mean_pressure);
                        } else if (input.name == "gravity") {
                            map_inputs.emplace(
                                input.name, groups.gravity_m_s2);
                        }
                    }
                    const auto classified =
                        data.friction_regime_map->classify(map_inputs);
                    if (!classified.succeeded()) {
                        return RestrictionEvaluation{
                            EvaluationStatus::recoverable(
                                classified.error)};
                    }
                    selected_friction_regime =
                        classified.selected_regime;
                }
                std::map<std::string, double> inputs;
                for (const auto& input :
                     data.friction_pressure_gradient_correlation->inputs()) {
                    if (input.name == "vapor_quality") {
                        inputs.emplace(input.name, quality);
                    } else if (input.name == "void_fraction") {
                        inputs.emplace(input.name, void_fraction);
                    } else if (input.name == "mixture_density") {
                        inputs.emplace(input.name, mixture_density);
                    } else if (input.name == "liquid_density") {
                        inputs.emplace(input.name, rho_l);
                    } else if (input.name == "vapor_density") {
                        inputs.emplace(input.name, rho_v);
                    } else if (input.name == "liquid_viscosity") {
                        inputs.emplace(
                            input.name,
                            saturation.liquid.viscosity_pa_s);
                    } else if (input.name == "vapor_viscosity") {
                        inputs.emplace(
                            input.name,
                            saturation.vapor.viscosity_pa_s);
                    } else if (input.name == "mass_flow") {
                        inputs.emplace(input.name, mass_flow);
                    } else if (input.name == "mass_flux") {
                        inputs.emplace(
                            input.name,
                            std::abs(mass_flow) / data.area);
                    } else if (input.name == "liquid_mass_flux") {
                        inputs.emplace(
                            input.name, (1.0 - quality) *
                                std::abs(mass_flow) / data.area);
                    } else if (input.name == "vapor_mass_flux") {
                        inputs.emplace(
                            input.name, quality *
                                std::abs(mass_flow) / data.area);
                    } else if (
                        input.name == "liquid_reynolds_number") {
                        inputs.emplace(
                            input.name, (1.0 - quality) *
                                std::abs(mass_flow) * data.diameter /
                                (data.area *
                                 saturation.liquid.viscosity_pa_s));
                    } else if (
                        input.name == "vapor_reynolds_number") {
                        inputs.emplace(
                            input.name, quality *
                                std::abs(mass_flow) * data.diameter /
                                (data.area *
                                 saturation.vapor.viscosity_pa_s));
                    } else if (input.name == "area") {
                        inputs.emplace(input.name, data.area);
                    } else if (input.name == "diameter") {
                        inputs.emplace(input.name, data.diameter);
                    } else if (input.name == "length") {
                        inputs.emplace(input.name, data.length);
                    } else if (input.name == "roughness") {
                        inputs.emplace(input.name, data.roughness);
                    } else if (input.name == "pressure") {
                        inputs.emplace(input.name, mean_pressure);
                    }
                }
                const auto evaluated =
                    data.friction_pressure_gradient_correlation->evaluate(
                        inputs, selected_friction_regime);
                if (!evaluated.error.empty()) {
                    return RestrictionEvaluation{
                        EvaluationStatus::recoverable(
                            evaluated.error)};
                }
                if (!std::isfinite(evaluated.value) ||
                    evaluated.value < 0.0) {
                    return RestrictionEvaluation{
                        EvaluationStatus::recoverable(
                            "two-phase friction correlation must produce "
                            "a finite nonnegative pressure gradient")};
                }
                distributed_friction_loss = std::copysign(
                    evaluated.value * data.length, mass_flow);
            }
            double acceleration_pressure_drop = 0.0;
            if (data.include_acceleration &&
                std::abs(mass_flow) > 1.0e-14) {
                const auto momentum_flux =
                    [&](double pressure, double& value) {
                        const auto endpoint_state =
                            data.properties->state_ph(
                                pressure, x.at(data.inlet_h));
                        if (!endpoint_state.ok()) {
                            return property_failure(endpoint_state);
                        }
                        const double endpoint_quality =
                            endpoint_state.state.vapor_quality;
                        if (endpoint_state.state.phase !=
                                physics::Phase::two_phase ||
                            !std::isfinite(endpoint_quality) ||
                            endpoint_quality <= 0.0 ||
                            endpoint_quality >= 1.0) {
                            return EvaluationStatus::recoverable(
                                "two-phase acceleration closure requires "
                                "both endpoint states to satisfy "
                                "0 < vapor quality < 1");
                        }
                        const auto endpoint_saturation =
                            data.properties->saturation_p(pressure);
                        if (!endpoint_saturation.ok()) {
                            return property_failure(
                                endpoint_saturation);
                        }
                        const double endpoint_rho_l =
                            endpoint_saturation.liquid.density_kg_m3;
                        const double endpoint_rho_v =
                            endpoint_saturation.vapor.density_kg_m3;
                        if (!std::isfinite(endpoint_rho_l) ||
                            !std::isfinite(endpoint_rho_v) ||
                            endpoint_rho_l <= 0.0 ||
                            endpoint_rho_v <= 0.0) {
                            return EvaluationStatus::recoverable(
                                "two-phase acceleration closure requires "
                                "positive finite endpoint densities");
                        }
                        const auto [endpoint_void_status,
                                    endpoint_void_fraction] =
                            evaluate_void_fraction(
                                endpoint_quality, endpoint_rho_l,
                                endpoint_rho_v, pressure);
                        if (!endpoint_void_status.ok()) {
                            return endpoint_void_status;
                        }
                        if (!std::isfinite(endpoint_void_fraction) ||
                            endpoint_void_fraction <= 0.0 ||
                            endpoint_void_fraction >= 1.0) {
                            return EvaluationStatus::recoverable(
                                "two-phase acceleration closure requires "
                                "0 < endpoint void fraction < 1");
                        }
                        const double liquid_fraction =
                            1.0 - endpoint_quality;
                        const double momentum_specific_volume =
                            endpoint_quality * endpoint_quality /
                                (endpoint_rho_v *
                                 endpoint_void_fraction) +
                            liquid_fraction * liquid_fraction /
                                (endpoint_rho_l *
                                 (1.0 - endpoint_void_fraction));
                        const double mass_flux =
                            mass_flow / data.area;
                        value = mass_flux * mass_flux *
                            momentum_specific_volume;
                        if (!std::isfinite(value) || value < 0.0) {
                            return EvaluationStatus::recoverable(
                                "two-phase acceleration closure produced "
                                "nonphysical momentum flux");
                        }
                        return EvaluationStatus::success();
                    };
                double inlet_momentum_flux = 0.0;
                double outlet_momentum_flux = 0.0;
                const auto inlet_status = momentum_flux(
                    inlet_pressure, inlet_momentum_flux);
                if (!inlet_status.ok()) {
                    return RestrictionEvaluation{inlet_status};
                }
                const auto outlet_status = momentum_flux(
                    outlet_pressure, outlet_momentum_flux);
                if (!outlet_status.ok()) {
                    return RestrictionEvaluation{outlet_status};
                }
                acceleration_pressure_drop =
                    outlet_momentum_flux - inlet_momentum_flux;
            }
            return RestrictionEvaluation{
                EvaluationStatus::success(),
                inlet_pressure - outlet_pressure -
                    data.loss_scale * mass_flow *
                        std::abs(mass_flow) / mixture_density -
                    distributed_friction_loss -
                    acceleration_pressure_drop -
                    mixture_density * gravity *
                        data.elevation_change};
        };
    }

    static std::vector<std::pair<std::size_t, double>>
    derivative_variables(const Data& data) {
        return {
            {data.inlet_m, 1.0e-7},
            {data.inlet_p, 0.1},
            {data.inlet_h, 0.1},
            {data.outlet_p, 0.1},
        };
    }

    static std::vector<std::size_t> variable_pattern(
        const std::vector<std::pair<std::size_t, double>>& variables) {
        std::vector<std::size_t> pattern;
        pattern.reserve(variables.size());
        for (const auto& [variable, _] : variables) {
            pattern.push_back(variable);
        }
        return pattern;
    }

    ComponentModelDescriptor descriptor_;
    bool correlation_driven_{false};
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

class PerfectGasMachScaledMaterialDuctModel final
    : public ComponentModel {
public:
    PerfectGasMachScaledMaterialDuctModel()
        : descriptor_(make_descriptor(
              "transport.material.perfect_gas_mach_scaled_loss",
              {{"inlet", "material", "in"},
               {"outlet", "material", "out"}})) {
        descriptor_.template_kind = "transport.material.duct";
        descriptor_.display_name = "Mach-scaled gas duct";
        descriptor_.category = "Gas-path transport";
        descriptor_.model_name = "Perfect-gas area and quadratic loss";
        descriptor_.parameters = {
            {"flow_area", "area", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"design_mach", "dimensionless", true, std::nullopt,
             0.0, 1.0, false, true},
            {"design_pressure_loss_fraction", "dimensionless", true,
             std::nullopt, 0.0, 1.0, true, false},
            {"heat_capacity_ratio", "dimensionless", false, 1.4,
             1.0, 2.0, false, true},
        };
        descriptor_.internal_variables = {
            {"mach", DaeVariableKind::algebraic, 0.3, 1.0,
             0.0, 1.0, 0.0, 1.0, "dimensionless"},
        };
        descriptor_.required_thermochemistry_capabilities = {
            physics::ThermochemistryCapability::state_ph,
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
        require_same_material(context, {"inlet", "outlet"});
        const auto properties =
            require_thermochemistry_package(context, "inlet");
        (void)require_thermochemistry_package(context, "outlet");
        const auto species = require_port_species(context, "inlet");
        if (species != require_port_species(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' material ports must use the same species basis");
        }
        const double area = required_parameter(
            context.component, "flow_area");
        const double design_mach = required_parameter(
            context.component, "design_mach");
        const double loss_fraction = required_parameter(
            context.component, "design_pressure_loss_fraction");
        const double gamma = parameter_or(
            context.component, "heat_capacity_ratio", 1.4);
        const auto inlet_p = require_port_variable(context, "inlet.p");
        const auto inlet_h = require_port_variable(context, "inlet.h");
        const auto outlet_p = require_port_variable(context, "outlet.p");
        const auto outlet_h = require_port_variable(context, "outlet.h");
        const auto mach = require_internal_variable(context, "mach");
        std::vector<std::size_t> inlet_flows;
        inlet_flows.reserve(species.size());
        const std::string prefix =
            "component." + context.component.id + ".";
        for (const auto& name : species) {
            const std::string variable = "m_dot[" + name + "]";
            const auto inlet = require_port_variable(
                context, "inlet." + variable);
            inlet_flows.push_back(inlet);
            system.add_linear_equation(
                prefix + "species." + name,
                {{require_port_variable(
                      context, "outlet." + variable), 1.0},
                 {inlet, -1.0}},
                0.0, 100.0);
        }
        system.add_linear_equation(
            prefix + "adiabatic_enthalpy",
            {{outlet_h, 1.0}, {inlet_h, -1.0}},
            0.0, 100000.0);

        const auto evaluate_flow =
            [properties, species, inlet_flows, inlet_p, inlet_h,
             mach, area, gamma](const std::vector<double>& x,
                                double& residual) {
                double total_flow = 0.0;
                std::vector<double> fractions;
                fractions.reserve(inlet_flows.size());
                for (const auto flow : inlet_flows) {
                    const double value = x.at(flow);
                    if (!std::isfinite(value) || value < 0.0) {
                        return EvaluationStatus::recoverable(
                            "Mach-scaled material duct requires finite "
                            "nonnegative species flows");
                    }
                    total_flow += value;
                    fractions.push_back(value);
                }
                if (!(total_flow > 0.0)) {
                    return EvaluationStatus::recoverable(
                        "Mach-scaled material duct requires positive "
                        "total mass flow");
                }
                for (double& fraction : fractions) {
                    fraction /= total_flow;
                }
                const auto state = properties->state_ph(
                    x.at(inlet_p), x.at(inlet_h),
                    {physics::CompositionBasis::mass_fraction,
                     species, std::move(fractions)});
                if (!state.ok()) {
                    return state.status ==
                            physics::PropertyStatus::backend_error
                        ? EvaluationStatus::fatal(state.message)
                        : EvaluationStatus::recoverable(state.message);
                }
                const double density =
                    state.state.thermodynamic.density_kg_m3;
                const double pressure = x.at(inlet_p);
                const double local_mach = x.at(mach);
                if (!std::isfinite(density) || density <= 0.0 ||
                    !std::isfinite(pressure) || pressure <= 0.0 ||
                    !std::isfinite(local_mach) || local_mach < 0.0 ||
                    local_mach > 1.0) {
                    return EvaluationStatus::recoverable(
                        "Mach-scaled material duct requires positive "
                        "density/pressure and subsonic Mach in [0, 1]");
                }
                const double sound_speed =
                    std::sqrt(gamma * pressure / density);
                const double correction = std::pow(
                    1.0 + 0.5 * (gamma - 1.0) *
                        local_mach * local_mach,
                    -(gamma + 1.0) /
                        (2.0 * (gamma - 1.0)));
                residual = total_flow -
                    area * density * sound_speed * local_mach * correction;
                return EvaluationStatus::success();
            };
        std::vector<std::size_t> flow_pattern = inlet_flows;
        flow_pattern.push_back(inlet_p);
        flow_pattern.push_back(inlet_h);
        flow_pattern.push_back(mach);
        system.add_checked_sparse_equation(
            prefix + "subsonic_mass_flow",
            evaluate_flow, flow_pattern,
            [evaluate_flow, flow_pattern](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                double base = 0.0;
                const auto status = evaluate_flow(x, base);
                if (!status.ok()) {
                    throw std::runtime_error(status.message);
                }
                for (const auto variable : flow_pattern) {
                    const double step = std::max(
                        variable == flow_pattern.back() ? 1.0e-7 : 1.0e-6,
                        std::abs(x.at(variable)) * 1.0e-6);
                    auto plus = x;
                    auto minus = x;
                    plus.at(variable) += step;
                    minus.at(variable) -= step;
                    double plus_value = 0.0;
                    double minus_value = 0.0;
                    const auto plus_status =
                        evaluate_flow(plus, plus_value);
                    const auto minus_status =
                        evaluate_flow(minus, minus_value);
                    if (plus_status.ok() && minus_status.ok()) {
                        jacobian.push_back({
                            variable,
                            (plus_value - minus_value) / (2.0 * step)});
                    } else if (plus_status.ok()) {
                        jacobian.push_back(
                            {variable, (plus_value - base) / step});
                    } else if (minus_status.ok()) {
                        jacobian.push_back(
                            {variable, (base - minus_value) / step});
                    } else {
                        throw std::runtime_error(
                            "could not evaluate a local derivative for "
                            "Mach-scaled material duct");
                    }
                }
                return base;
            },
            100.0);

        system.add_sparse_equation(
            prefix + "pressure_loss",
            {inlet_p, outlet_p, mach},
            [inlet_p, outlet_p, mach, design_mach, loss_fraction](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                const double ratio = x.at(mach) / design_mach;
                const double retained =
                    1.0 - loss_fraction * ratio * ratio;
                jacobian.push_back({outlet_p, 1.0});
                jacobian.push_back({inlet_p, -retained});
                jacobian.push_back({
                    mach,
                    2.0 * x.at(inlet_p) * loss_fraction *
                        x.at(mach) / (design_mach * design_mach)});
                return x.at(outlet_p) - retained * x.at(inlet_p);
            },
            100000.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class FreestreamMomentumMaterialModel final : public ComponentModel {
public:
    FreestreamMomentumMaterialModel()
        : descriptor_(make_descriptor(
              "transport.material.freestream_momentum",
              {{"inlet", "material", "in"},
               {"outlet", "material", "out"},
               {"drag", "force", "out"}})) {
        descriptor_.template_kind = "transport.freestream_momentum";
        descriptor_.display_name = "Freestream momentum accounting";
        descriptor_.category = "Propulsion performance";
        descriptor_.model_name =
            "Zero-loss stream pass-through with ram drag";
        descriptor_.parameters = {
            {"freestream_velocity", "speed", true, std::nullopt,
             0.0, std::numeric_limits<double>::infinity(), true, true},
            {"momentum_coefficient", "dimensionless", false, 1.0,
             0.0, std::numeric_limits<double>::infinity(), true, true},
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
        const auto inlet_species =
            require_port_species(context, "inlet");
        const auto outlet_species =
            require_port_species(context, "outlet");
        if (inlet_species != outlet_species) {
            throw std::invalid_argument(
                "freestream momentum component requires identical "
                "inlet and outlet material species bases");
        }
        const auto inlet_p =
            require_port_variable(context, "inlet.p");
        const auto outlet_p =
            require_port_variable(context, "outlet.p");
        const auto inlet_h =
            require_port_variable(context, "inlet.h");
        const auto outlet_h =
            require_port_variable(context, "outlet.h");
        const auto drag = require_port_variable(context, "drag.F");
        const double velocity = required_parameter(
            context.component, "freestream_velocity");
        const double coefficient = parameter_or(
            context.component, "momentum_coefficient", 1.0);
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "pressure_continuity",
            {{outlet_p, 1.0}, {inlet_p, -1.0}}, 0.0, 100000.0);
        system.add_linear_equation(
            prefix + "enthalpy_continuity",
            {{outlet_h, 1.0}, {inlet_h, -1.0}}, 0.0, 100000.0);
        std::vector<LinearTerm> momentum{{drag, 1.0}};
        for (const auto& species : inlet_species) {
            const auto inlet_flow = require_port_variable(
                context, "inlet.m_dot[" + species + "]");
            const auto outlet_flow = require_port_variable(
                context, "outlet.m_dot[" + species + "]");
            system.add_linear_equation(
                prefix + "species_" + species,
                {{outlet_flow, 1.0}, {inlet_flow, -1.0}},
                0.0, 100.0);
            momentum.push_back(
                {inlet_flow, -coefficient * velocity});
        }
        system.add_linear_equation(
            prefix + "ram_drag", std::move(momentum),
            0.0, 100000.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class PerfectGasConvergentMaterialNozzleModel final
    : public ComponentModel {
public:
    PerfectGasConvergentMaterialNozzleModel()
        : descriptor_(make_descriptor(
              "terminal.material.perfect_gas_convergent_nozzle",
              {{"inlet", "material", "in"},
               {"area_ratio", "signal", "in"},
               {"back_pressure_ratio", "signal", "in"},
               {"thrust", "force", "out"}})) {
        descriptor_.template_kind = "terminal.material.nozzle";
        descriptor_.display_name = "Convergent gas nozzle";
        descriptor_.category = "Gas-path terminals";
        descriptor_.model_name = "Perfect-gas choked/un-choked flow";
        descriptor_.system_boundary_role = "sink";
        descriptor_.parameters = {
            {"reference_throat_area", "area", true, std::nullopt,
             0.0, std::numeric_limits<double>::infinity(), false, true},
            {"reference_back_pressure", "pressure", true, std::nullopt,
             0.0, std::numeric_limits<double>::infinity(), false, true},
            {"discharge_coefficient", "dimensionless", false, 1.0,
             0.0, 1.0, false, true},
            {"gross_thrust_coefficient", "dimensionless", false, 1.0,
             0.0, std::numeric_limits<double>::infinity(), false, true},
            {"heat_capacity_ratio", "dimensionless", false, 1.4,
             1.0, 2.0, false, true},
        };
        descriptor_.internal_variables = {
            {"mach", DaeVariableKind::algebraic, 0.8, 1.0,
             0.0, 1.0, 0.0, 1.0, "dimensionless"},
        };
        descriptor_.required_thermochemistry_capabilities = {
            physics::ThermochemistryCapability::state_ph,
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
            require_thermochemistry_package(context, "inlet");
        const auto species = require_port_species(context, "inlet");
        std::vector<std::size_t> flows;
        flows.reserve(species.size());
        for (const auto& name : species) {
            flows.push_back(require_port_variable(
                context, "inlet.m_dot[" + name + "]"));
        }
        const auto inlet_p = require_port_variable(context, "inlet.p");
        const auto inlet_h = require_port_variable(context, "inlet.h");
        const auto area_ratio =
            require_port_variable(context, "area_ratio.value");
        const auto back_pressure_ratio = require_port_variable(
            context, "back_pressure_ratio.value");
        const auto mach = require_internal_variable(context, "mach");
        const auto gross_thrust =
            require_port_variable(context, "thrust.F");
        const double reference_area = required_parameter(
            context.component, "reference_throat_area");
        const double reference_pressure = required_parameter(
            context.component, "reference_back_pressure");
        const double discharge = parameter_or(
            context.component, "discharge_coefficient", 1.0);
        const double thrust_coefficient = parameter_or(
            context.component, "gross_thrust_coefficient", 1.0);
        const double gamma = parameter_or(
            context.component, "heat_capacity_ratio", 1.4);

        const auto evaluate =
            [properties, species, flows, inlet_p, inlet_h, area_ratio,
             back_pressure_ratio, mach, gross_thrust, reference_area,
             reference_pressure, discharge, thrust_coefficient, gamma](
                const std::vector<double>& x,
                std::array<double, 3>& residuals) {
                double total_flow = 0.0;
                std::vector<double> fractions;
                fractions.reserve(flows.size());
                for (const auto flow : flows) {
                    const double value = x.at(flow);
                    if (!std::isfinite(value) || value < 0.0) {
                        return EvaluationStatus::recoverable(
                            "convergent material nozzle requires finite "
                            "nonnegative species flows");
                    }
                    total_flow += value;
                    fractions.push_back(value);
                }
                if (!(total_flow > 0.0)) {
                    return EvaluationStatus::recoverable(
                        "convergent material nozzle requires positive "
                        "total mass flow");
                }
                for (double& fraction : fractions) fraction /= total_flow;
                const auto state = properties->state_ph(
                    x.at(inlet_p), x.at(inlet_h),
                    {physics::CompositionBasis::mass_fraction,
                     species, std::move(fractions)});
                if (!state.ok()) {
                    return state.status ==
                            physics::PropertyStatus::backend_error
                        ? EvaluationStatus::fatal(state.message)
                        : EvaluationStatus::recoverable(state.message);
                }
                const double density =
                    state.state.thermodynamic.density_kg_m3;
                const double pressure = x.at(inlet_p);
                const double area =
                    reference_area * x.at(area_ratio);
                const double back_pressure =
                    reference_pressure * x.at(back_pressure_ratio);
                if (!(density > 0.0) || !(pressure > back_pressure) ||
                    !(back_pressure > 0.0) || !(area > 0.0)) {
                    return EvaluationStatus::recoverable(
                        "convergent material nozzle requires positive area "
                        "and total pressure above back pressure");
                }
                const double exponent = (gamma - 1.0) / gamma;
                const double unchoked_mach = std::sqrt(
                    2.0 / (gamma - 1.0) *
                    (std::pow(pressure / back_pressure, exponent) - 1.0));
                const double target_mach =
                    std::min(1.0, unchoked_mach);
                const double local_mach = x.at(mach);
                const double correction = std::pow(
                    1.0 + 0.5 * (gamma - 1.0) *
                        local_mach * local_mach,
                    -(gamma + 1.0) /
                        (2.0 * (gamma - 1.0)));
                const double sound_speed =
                    std::sqrt(gamma * pressure / density);
                const double capacity = discharge * area * density *
                    sound_speed * local_mach * correction;
                const double ideal_velocity = std::sqrt(
                    2.0 * gamma / (gamma - 1.0) * pressure / density *
                    (1.0 - std::pow(
                        back_pressure / pressure, exponent)));
                residuals = {
                    total_flow - capacity,
                    local_mach - target_mach,
                    x.at(gross_thrust) - thrust_coefficient *
                        total_flow * ideal_velocity,
                };
                return EvaluationStatus::success();
            };
        std::vector<std::size_t> pattern = flows;
        pattern.insert(pattern.end(), {
            inlet_p, inlet_h, area_ratio, back_pressure_ratio,
            mach, gross_thrust});
        const std::string prefix =
            "component." + context.component.id + ".";
        const auto add_equation = [&](const std::string& name,
                                      std::size_t residual_index,
                                      double scale) {
            system.add_checked_sparse_equation(
                prefix + name,
                [evaluate, residual_index](
                    const std::vector<double>& x, double& residual) {
                    std::array<double, 3> values{};
                    const auto status = evaluate(x, values);
                    residual = values.at(residual_index);
                    return status;
                },
                pattern,
                [evaluate, pattern, residual_index](
                    const std::vector<double>& x,
                    std::vector<EquationPartial>& jacobian) {
                    std::array<double, 3> base{};
                    const auto status = evaluate(x, base);
                    if (!status.ok()) {
                        throw std::runtime_error(status.message);
                    }
                    for (const auto variable : pattern) {
                        const double step = std::max(
                            1.0e-7,
                            std::abs(x.at(variable)) * 1.0e-6);
                        auto plus = x;
                        auto minus = x;
                        plus.at(variable) += step;
                        minus.at(variable) -= step;
                        std::array<double, 3> plus_values{};
                        std::array<double, 3> minus_values{};
                        const auto plus_status = evaluate(plus, plus_values);
                        const auto minus_status = evaluate(minus, minus_values);
                        if (plus_status.ok() && minus_status.ok()) {
                            jacobian.push_back({
                                variable,
                                (plus_values.at(residual_index) -
                                 minus_values.at(residual_index)) /
                                    (2.0 * step)});
                        } else if (plus_status.ok()) {
                            jacobian.push_back({
                                variable,
                                (plus_values.at(residual_index) -
                                 base.at(residual_index)) / step});
                        } else if (minus_status.ok()) {
                            jacobian.push_back({
                                variable,
                                (base.at(residual_index) -
                                 minus_values.at(residual_index)) / step});
                        } else {
                            throw std::runtime_error(
                                "could not evaluate a local derivative for "
                                "convergent material nozzle");
                        }
                    }
                    return base.at(residual_index);
                },
                scale);
        };
        add_equation("mass_capacity", 0, 100.0);
        add_equation("back_pressure_mach", 1, 1.0);
        add_equation("gross_thrust", 2, 100000.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class IsenthalpicMaterialPressureRegulatorModel final
    : public ComponentModel {
public:
    IsenthalpicMaterialPressureRegulatorModel()
        : descriptor_(make_descriptor(
              "regulator.material.isenthalpic_network_pressure",
              {{"inlet", "material", "in"},
               {"outlet", "material", "out"}})) {}

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto inlet_species =
            require_port_species(context, "inlet");
        if (inlet_species !=
            require_port_species(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' material ports must use the same species basis");
        }
        const std::string prefix =
            "component." + context.component.id + ".";
        system.add_linear_equation(
            prefix + "isenthalpic",
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
        system.add_initialization_relation(
            {{require_port_variable(context, "outlet.p"), 1.0},
             {require_port_variable(context, "inlet.p"), -1.0}},
            0.0);
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

class ControlledFractionMaterialSplitterModel final
    : public ComponentModel {
public:
    ControlledFractionMaterialSplitterModel()
        : descriptor_(make_descriptor(
              "junction.material.splitter.controlled_fraction",
              {{"inlet", "material", "in"},
               {"outlet_a", "material", "out"},
               {"outlet_b", "material", "out"},
               {"fraction", "signal", "in"}})) {
        descriptor_.supports_transient = true;
        descriptor_.uses_quasi_steady_transient_equations = true;
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        require_same_material(
            context, {"inlet", "outlet_a", "outlet_b"});
        const auto species = require_port_species(context, "inlet");
        if (species != require_port_species(context, "outlet_a") ||
            species != require_port_species(context, "outlet_b")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' material ports must use the same species basis");
        }
        const auto fraction =
            require_port_variable(context, "fraction.value");
        const std::string prefix =
            "component." + context.component.id + ".";
        const auto valid_fraction = [](double value) {
            return std::isfinite(value) && value >= 0.0 && value <= 1.0;
        };
        for (const auto& name : species) {
            const std::string variable = "m_dot[" + name + "]";
            const auto inlet = require_port_variable(
                context, "inlet." + variable);
            const auto outlet_a = require_port_variable(
                context, "outlet_a." + variable);
            const auto outlet_b = require_port_variable(
                context, "outlet_b." + variable);
            system.add_checked_sparse_equation(
                prefix + "outlet_a_species." + name,
                [inlet, outlet_a, fraction, valid_fraction](
                    const std::vector<double>& x,
                    double& residual) {
                    if (!valid_fraction(x.at(fraction))) {
                        return EvaluationStatus::recoverable(
                            "controlled material splitter fraction must "
                            "be finite and in [0, 1]");
                    }
                    residual = x.at(outlet_a) -
                        x.at(fraction) * x.at(inlet);
                    return EvaluationStatus::success();
                },
                {inlet, outlet_a, fraction},
                [inlet, outlet_a, fraction](
                    const std::vector<double>& x,
                    std::vector<EquationPartial>& jacobian) {
                    jacobian.push_back({outlet_a, 1.0});
                    jacobian.push_back({inlet, -x.at(fraction)});
                    jacobian.push_back({fraction, -x.at(inlet)});
                    return x.at(outlet_a) -
                        x.at(fraction) * x.at(inlet);
                },
                100.0);
            system.add_checked_sparse_equation(
                prefix + "outlet_b_species." + name,
                [inlet, outlet_b, fraction, valid_fraction](
                    const std::vector<double>& x,
                    double& residual) {
                    if (!valid_fraction(x.at(fraction))) {
                        return EvaluationStatus::recoverable(
                            "controlled material splitter fraction must "
                            "be finite and in [0, 1]");
                    }
                    residual = x.at(outlet_b) -
                        (1.0 - x.at(fraction)) * x.at(inlet);
                    return EvaluationStatus::success();
                },
                {inlet, outlet_b, fraction},
                [inlet, outlet_b, fraction](
                    const std::vector<double>& x,
                    std::vector<EquationPartial>& jacobian) {
                    jacobian.push_back({outlet_b, 1.0});
                    jacobian.push_back(
                        {inlet, -(1.0 - x.at(fraction))});
                    jacobian.push_back({fraction, x.at(inlet)});
                    return x.at(outlet_b) -
                        (1.0 - x.at(fraction)) * x.at(inlet);
                },
                100.0);
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

class ControlledMaterialCrossBleedModel final
    : public ComponentModel {
public:
    ControlledMaterialCrossBleedModel()
        : descriptor_(make_descriptor(
              "junction.material.cross_bleed.performance_map",
              {{"donor_inlet", "material", "in"},
               {"donor_outlet", "material", "out"},
               {"receiver_inlet", "material", "in"},
               {"receiver_outlet", "material", "out"},
               {"position", "signal", "in"}})) {
        descriptor_.template_kind = "junction.material.cross_bleed";
        descriptor_.display_name = "Controlled material cross-bleed";
        descriptor_.category = "Material-flow junctions";
        descriptor_.model_name =
            "Corrected-flow map with conservative mixing";
        descriptor_.parameters = {
            {"reference_pressure", "pressure", true, std::nullopt,
             0.0, std::numeric_limits<double>::infinity(), false, true},
            {"reference_temperature", "temperature", true, std::nullopt,
             0.0, std::numeric_limits<double>::infinity(), false, true},
            {"flow_capacity_scale", "dimensionless", false, 1.0,
             0.0, std::numeric_limits<double>::infinity(), false, true},
        };
        descriptor_.artifacts = {
            {"flow_characteristic", performance_map_artifact_type, true},
        };
        descriptor_.internal_variables = {
            {"bleed_mass_flow", DaeVariableKind::algebraic, 1.0, 10.0,
             0.0, 1.0, 0.0,
             std::numeric_limits<double>::infinity(), "mass_flow"},
        };
        descriptor_.required_thermochemistry_capabilities = {
            physics::ThermochemistryCapability::state_ph,
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
        require_same_material(context, {
            "donor_inlet", "donor_outlet",
            "receiver_inlet", "receiver_outlet"});
        const auto species = require_port_species(context, "donor_inlet");
        for (const auto& port : {"donor_outlet", "receiver_inlet",
                                 "receiver_outlet"}) {
            if (species != require_port_species(context, port)) {
                throw std::invalid_argument(
                    "component '" + context.component.id +
                    "' material ports must use the same species basis");
            }
        }
        const auto properties =
            require_thermochemistry_package(context, "donor_inlet");
        (void)require_thermochemistry_package(context, "donor_outlet");
        (void)require_thermochemistry_package(context, "receiver_inlet");
        (void)require_thermochemistry_package(context, "receiver_outlet");
        const auto artifact = require_performance_map(
            context, "flow_characteristic");
        if (!artifact->map || artifact->conditioned_map) {
            throw std::invalid_argument(
                "cross-bleed flow characteristic must provide an ordinary "
                "two-coordinate performance map");
        }
        const auto& map = *artifact->map;
        if (map.primary_variable().name != "pressure_ratio" ||
            map.primary_variable().dimension != "dimensionless" ||
            map.family_variable().name != "position" ||
            map.family_variable().dimension != "dimensionless" ||
            map.output_variables().size() != 1 ||
            map.output_variables().front().name !=
                "corrected_mass_flow" ||
            map.output_variables().front().dimension != "mass_flow") {
            throw std::invalid_argument(
                "cross-bleed map axes must be dimensionless pressure_ratio "
                "and position, with one mass_flow corrected_mass_flow output");
        }

        const double reference_pressure = required_parameter(
            context.component, "reference_pressure");
        const double reference_temperature = required_parameter(
            context.component, "reference_temperature");
        const double capacity_scale = parameter_or(
            context.component, "flow_capacity_scale", 1.0);
        const auto donor_p =
            require_port_variable(context, "donor_inlet.p");
        const auto donor_h =
            require_port_variable(context, "donor_inlet.h");
        const auto donor_out_p =
            require_port_variable(context, "donor_outlet.p");
        const auto donor_out_h =
            require_port_variable(context, "donor_outlet.h");
        const auto receiver_p =
            require_port_variable(context, "receiver_inlet.p");
        const auto receiver_h =
            require_port_variable(context, "receiver_inlet.h");
        const auto receiver_out_p =
            require_port_variable(context, "receiver_outlet.p");
        const auto receiver_out_h =
            require_port_variable(context, "receiver_outlet.h");
        const auto position =
            require_port_variable(context, "position.value");
        const auto bleed =
            require_internal_variable(context, "bleed_mass_flow");
        const std::string prefix =
            "component." + context.component.id + ".";

        std::vector<std::size_t> donor_in_flows;
        std::vector<std::size_t> receiver_in_flows;
        std::vector<std::size_t> receiver_out_flows;
        donor_in_flows.reserve(species.size());
        receiver_in_flows.reserve(species.size());
        receiver_out_flows.reserve(species.size());
        for (const auto& name : species) {
            const std::string variable = "m_dot[" + name + "]";
            donor_in_flows.push_back(require_port_variable(
                context, "donor_inlet." + variable));
            receiver_in_flows.push_back(require_port_variable(
                context, "receiver_inlet." + variable));
            receiver_out_flows.push_back(require_port_variable(
                context, "receiver_outlet." + variable));
        }
        for (std::size_t species_index = 0;
             species_index < species.size(); ++species_index) {
            const auto& name = species.at(species_index);
            const std::string variable = "m_dot[" + name + "]";
            const auto donor_in = donor_in_flows.at(species_index);
            const auto donor_out = require_port_variable(
                context, "donor_outlet." + variable);
            const auto receiver_in =
                receiver_in_flows.at(species_index);
            const auto receiver_out =
                receiver_out_flows.at(species_index);

            const auto validate = [donor_in_flows, bleed](
                const std::vector<double>& x, double& donor_total) {
                donor_total = 0.0;
                for (const auto flow : donor_in_flows) {
                    if (!std::isfinite(x.at(flow)) || x.at(flow) < 0.0) {
                        return false;
                    }
                    donor_total += x.at(flow);
                }
                return donor_total > 0.0 &&
                    std::isfinite(x.at(bleed)) && x.at(bleed) >= 0.0 &&
                    x.at(bleed) <= donor_total;
            };
            const auto donor_pattern = [&] {
                auto out = donor_in_flows;
                out.push_back(donor_out);
                out.push_back(bleed);
                return out;
            }();
            system.add_checked_sparse_equation(
                prefix + "donor_species." + name,
                [donor_in_flows, donor_in, donor_out, bleed, validate](
                    const std::vector<double>& x, double& residual) {
                    double total = 0.0;
                    if (!validate(x, total)) {
                        return EvaluationStatus::recoverable(
                            "cross-bleed requires positive donor flow and "
                            "bleed flow within donor capacity");
                    }
                    residual = x.at(donor_out) - x.at(donor_in) *
                        (1.0 - x.at(bleed) / total);
                    return EvaluationStatus::success();
                },
                donor_pattern,
                [donor_in_flows, donor_in, donor_out, bleed](
                    const std::vector<double>& x,
                    std::vector<EquationPartial>& jacobian) {
                    double total = 0.0;
                    for (const auto flow : donor_in_flows) total += x.at(flow);
                    const double fraction = x.at(bleed) / total;
                    for (const auto flow : donor_in_flows) {
                        jacobian.push_back({
                            flow,
                            -(flow == donor_in ? 1.0 - fraction : 0.0) -
                                x.at(donor_in) * x.at(bleed) /
                                    (total * total)});
                    }
                    jacobian.push_back({donor_out, 1.0});
                    jacobian.push_back({bleed, x.at(donor_in) / total});
                    return x.at(donor_out) - x.at(donor_in) *
                        (1.0 - fraction);
                },
                100.0);

            auto receiver_pattern = donor_in_flows;
            receiver_pattern.push_back(receiver_in);
            receiver_pattern.push_back(receiver_out);
            receiver_pattern.push_back(bleed);
            system.add_checked_sparse_equation(
                prefix + "receiver_species." + name,
                [donor_in_flows, donor_in, receiver_in, receiver_out,
                 bleed, validate](const std::vector<double>& x,
                                  double& residual) {
                    double total = 0.0;
                    if (!validate(x, total)) {
                        return EvaluationStatus::recoverable(
                            "cross-bleed requires positive donor flow and "
                            "bleed flow within donor capacity");
                    }
                    residual = x.at(receiver_out) - x.at(receiver_in) -
                        x.at(donor_in) * x.at(bleed) / total;
                    return EvaluationStatus::success();
                },
                receiver_pattern,
                [donor_in_flows, donor_in, receiver_in, receiver_out, bleed](
                    const std::vector<double>& x,
                    std::vector<EquationPartial>& jacobian) {
                    double total = 0.0;
                    for (const auto flow : donor_in_flows) total += x.at(flow);
                    for (const auto flow : donor_in_flows) {
                        jacobian.push_back({
                            flow,
                            -(flow == donor_in ? x.at(bleed) / total : 0.0) +
                                x.at(donor_in) * x.at(bleed) /
                                    (total * total)});
                    }
                    jacobian.push_back({receiver_in, -1.0});
                    jacobian.push_back({receiver_out, 1.0});
                    jacobian.push_back({bleed, -x.at(donor_in) / total});
                    return x.at(receiver_out) - x.at(receiver_in) -
                        x.at(donor_in) * x.at(bleed) / total;
                },
                100.0);
        }

        system.add_linear_equation(
            prefix + "donor_pressure",
            {{donor_out_p, 1.0}, {donor_p, -1.0}}, 0.0, 100000.0);
        system.add_linear_equation(
            prefix + "donor_enthalpy",
            {{donor_out_h, 1.0}, {donor_h, -1.0}}, 0.0, 100000.0);
        system.add_linear_equation(
            prefix + "receiver_pressure",
            {{receiver_out_p, 1.0}, {receiver_p, -1.0}}, 0.0, 100000.0);

        auto energy_pattern = donor_in_flows;
        energy_pattern.insert(energy_pattern.end(),
            receiver_in_flows.begin(), receiver_in_flows.end());
        energy_pattern.insert(energy_pattern.end(),
            receiver_out_flows.begin(), receiver_out_flows.end());
        energy_pattern.insert(energy_pattern.end(),
            {donor_h, receiver_h, receiver_out_h, bleed});
        const auto evaluate_energy =
            [donor_in_flows, receiver_in_flows, receiver_out_flows,
             donor_h, receiver_h, receiver_out_h, bleed](
                const std::vector<double>& x, double& residual) {
                double donor_total = 0.0;
                double receiver_in_total = 0.0;
                double receiver_out_total = 0.0;
                for (const auto flow : donor_in_flows) donor_total += x.at(flow);
                for (const auto flow : receiver_in_flows)
                    receiver_in_total += x.at(flow);
                for (const auto flow : receiver_out_flows)
                    receiver_out_total += x.at(flow);
                if (!(donor_total > 0.0) || x.at(bleed) < 0.0 ||
                    x.at(bleed) > donor_total) {
                    return EvaluationStatus::recoverable(
                        "cross-bleed energy balance requires positive, "
                        "physically bounded flows");
                }
                residual = receiver_out_total * x.at(receiver_out_h) -
                    receiver_in_total * x.at(receiver_h) -
                    x.at(bleed) * x.at(donor_h);
                return EvaluationStatus::success();
            };
        system.add_checked_sparse_equation(
            prefix + "receiver_energy", evaluate_energy, energy_pattern,
            [evaluate_energy, energy_pattern](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                double base = 0.0;
                const auto status = evaluate_energy(x, base);
                if (!status.ok()) throw std::runtime_error(status.message);
                for (const auto variable : energy_pattern) {
                    const double step = std::max(
                        1.0e-7, std::abs(x.at(variable)) * 1.0e-6);
                    auto plus = x;
                    auto minus = x;
                    plus.at(variable) += step;
                    minus.at(variable) -= step;
                    double plus_value = 0.0;
                    double minus_value = 0.0;
                    const auto plus_status = evaluate_energy(plus, plus_value);
                    const auto minus_status = evaluate_energy(minus, minus_value);
                    if (plus_status.ok() && minus_status.ok()) {
                        jacobian.push_back({variable,
                            (plus_value - minus_value) / (2.0 * step)});
                    } else if (plus_status.ok()) {
                        jacobian.push_back({variable,
                            (plus_value - base) / step});
                    } else if (minus_status.ok()) {
                        jacobian.push_back({variable,
                            (base - minus_value) / step});
                    } else {
                        throw std::runtime_error(
                            "could not evaluate a local derivative for "
                            "cross-bleed energy balance");
                    }
                }
                return base;
            },
            1.0e8);

        const auto evaluate_capacity =
            [properties, artifact, species, donor_in_flows, donor_p,
             donor_h, receiver_p, position, bleed, reference_pressure,
             reference_temperature, capacity_scale](
                const std::vector<double>& x, double& residual) {
                double donor_total = 0.0;
                std::vector<double> fractions;
                fractions.reserve(donor_in_flows.size());
                for (const auto flow : donor_in_flows) {
                    if (!std::isfinite(x.at(flow)) || x.at(flow) < 0.0) {
                        return EvaluationStatus::recoverable(
                            "cross-bleed requires nonnegative donor species flows");
                    }
                    donor_total += x.at(flow);
                    fractions.push_back(x.at(flow));
                }
                if (!(donor_total > 0.0) || !(x.at(donor_p) > 0.0) ||
                    !(x.at(receiver_p) > 0.0) ||
                    !std::isfinite(x.at(position))) {
                    return EvaluationStatus::recoverable(
                        "cross-bleed requires positive flow/pressures and "
                        "finite position");
                }
                for (double& fraction : fractions) fraction /= donor_total;
                const auto state = properties->state_ph(
                    x.at(donor_p), x.at(donor_h),
                    {physics::CompositionBasis::mass_fraction,
                     species, std::move(fractions)});
                if (!state.ok()) {
                    return state.status == physics::PropertyStatus::backend_error
                        ? EvaluationStatus::fatal(state.message)
                        : EvaluationStatus::recoverable(state.message);
                }
                const double temperature =
                    state.state.thermodynamic.temperature_k;
                if (!(temperature > 0.0)) {
                    return EvaluationStatus::recoverable(
                        "cross-bleed donor temperature must be positive");
                }
                try {
                    const auto mapped = artifact->map->evaluate(
                        x.at(donor_p) / x.at(receiver_p), x.at(position));
                    const double capacity = capacity_scale *
                        mapped.outputs.front() *
                        (x.at(donor_p) / reference_pressure) *
                        std::sqrt(reference_temperature / temperature);
                    if (!std::isfinite(capacity) || capacity < 0.0 ||
                        capacity > donor_total) {
                        return EvaluationStatus::recoverable(
                            "cross-bleed mapped capacity is outside donor flow");
                    }
                    residual = x.at(bleed) - capacity;
                    return EvaluationStatus::success();
                } catch (const MapDomainError& error) {
                    return EvaluationStatus::recoverable(error.what());
                }
            };
        auto capacity_pattern = donor_in_flows;
        capacity_pattern.insert(capacity_pattern.end(),
            {donor_p, donor_h, receiver_p, position, bleed});
        system.add_checked_sparse_equation(
            prefix + "mapped_bleed_capacity", evaluate_capacity,
            capacity_pattern,
            [evaluate_capacity, capacity_pattern](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                double base = 0.0;
                const auto status = evaluate_capacity(x, base);
                if (!status.ok()) throw std::runtime_error(status.message);
                for (const auto variable : capacity_pattern) {
                    const double step = std::max(
                        1.0e-7, std::abs(x.at(variable)) * 1.0e-6);
                    auto plus = x;
                    auto minus = x;
                    plus.at(variable) += step;
                    minus.at(variable) -= step;
                    double plus_value = 0.0;
                    double minus_value = 0.0;
                    const auto plus_status = evaluate_capacity(plus, plus_value);
                    const auto minus_status = evaluate_capacity(minus, minus_value);
                    if (plus_status.ok() && minus_status.ok()) {
                        jacobian.push_back({variable,
                            (plus_value - minus_value) / (2.0 * step)});
                    } else if (plus_status.ok()) {
                        jacobian.push_back({variable,
                            (plus_value - base) / step});
                    } else if (minus_status.ok()) {
                        jacobian.push_back({variable,
                            (base - minus_value) / step});
                    } else {
                        throw std::runtime_error(
                            "could not evaluate a local derivative for "
                            "cross-bleed mapped capacity");
                    }
                }
                return base;
            },
            10.0);
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
        std::make_shared<FluidHydraulicInertanceModel>());
    registry.register_model(
        std::make_shared<
            IsenthalpicPressureRatioValveModel>());
    registry.register_model(
        std::make_shared<FlowAreaRestrictionModel>(false));
    registry.register_model(
        std::make_shared<FlowAreaRestrictionModel>(true));
    registry.register_model(
        std::make_shared<HomogeneousTwoPhaseLocalLossModel>());
    registry.register_model(
        std::make_shared<HomogeneousEquilibriumGravityPipeModel>());
    registry.register_model(
        std::make_shared<SlipAwareTwoPhaseGravityPipeModel>(false));
    registry.register_model(
        std::make_shared<SlipAwareTwoPhaseGravityPipeModel>(true));
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
    registry.register_model(std::make_shared<
        IsenthalpicMaterialPressureRegulatorModel>());
    registry.register_model(
        std::make_shared<TwoInletMaterialMixerModel>());
    registry.register_model(
        std::make_shared<
            FixedFractionMaterialSplitterModel>());
    registry.register_model(std::make_shared<
        ControlledFractionMaterialSplitterModel>());
    registry.register_model(std::make_shared<
        ControlledMaterialCrossBleedModel>());
    registry.register_model(std::make_shared<
        PerfectGasMachScaledMaterialDuctModel>());
    registry.register_model(std::make_shared<
        FreestreamMomentumMaterialModel>());
    registry.register_model(std::make_shared<
        PerfectGasConvergentMaterialNozzleModel>());
}

}  // namespace thermox::platform
