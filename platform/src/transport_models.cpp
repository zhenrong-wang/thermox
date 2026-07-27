#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <limits>
#include <memory>
#include <stdexcept>

namespace thermox::platform {

namespace {

using component_model_support::require_port_variable;
using component_model_support::require_property_package;
using component_model_support::required_parameter;

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
}

}  // namespace thermox::platform
