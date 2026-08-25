#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <cmath>
#include <limits>
#include <memory>

namespace thermox::platform {

namespace {

using component_model_support::parameter_or;
using component_model_support::require_internal_variable;
using component_model_support::require_port_variable;
using component_model_support::required_parameter;

class TwoLoadShaftTrainModel final : public ComponentModel {
public:
    TwoLoadShaftTrainModel() {
        descriptor_.kind = "shaft.train.two_load";
        descriptor_.version = "1.0.0";
        descriptor_.ports = {
            {"driver", "shaft", "in"},
            {"load_1", "shaft", "out"},
            {"load_2", "shaft", "out"},
        };
        descriptor_.parameters = {
            {"mechanical_efficiency", "dimensionless", true,
             std::nullopt, 0.0, 1.0, false, true},
            {"fixed_loss", "power", false, 0.0, 0.0,
             std::numeric_limits<double>::infinity(), true, true},
        };
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const double efficiency = required_parameter(
            context.component, "mechanical_efficiency");
        const double fixed_loss = parameter_or(
            context.component, "fixed_loss", 0.0);
        const auto driver_power =
            require_port_variable(context, "driver.W_dot");
        const auto driver_speed =
            require_port_variable(context, "driver.omega");
        const auto load_1_power =
            require_port_variable(context, "load_1.W_dot");
        const auto load_1_speed =
            require_port_variable(context, "load_1.omega");
        const auto load_2_power =
            require_port_variable(context, "load_2.W_dot");
        const auto load_2_speed =
            require_port_variable(context, "load_2.omega");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "load_1_speed",
            {{driver_speed, 1.0}, {load_1_speed, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "load_2_speed",
            {{driver_speed, 1.0}, {load_2_speed, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "power_balance",
            {
                {driver_power, efficiency},
                {load_1_power, -1.0},
                {load_2_power, -1.0},
            },
            fixed_loss, 1.0e6);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class TwoDriverShaftCombinerModel final : public ComponentModel {
public:
    TwoDriverShaftCombinerModel() {
        descriptor_.kind = "shaft.combiner.two_driver";
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "shaft.combiner";
        descriptor_.display_name = "Two-driver shaft combiner";
        descriptor_.category = "Power transmission";
        descriptor_.model_name =
            "Common-speed two-driver mechanical power combiner";
        descriptor_.ports = {
            {"driver_a", "shaft", "in"},
            {"driver_b", "shaft", "in"},
            {"load", "shaft", "out"},
        };
        descriptor_.parameters = {
            {"mechanical_efficiency", "dimensionless", false,
             1.0, 0.0, 1.0, false, true},
            {"fixed_loss", "power", false, 0.0, 0.0,
             std::numeric_limits<double>::infinity(), true, true},
        };
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const double efficiency = parameter_or(
            context.component, "mechanical_efficiency", 1.0);
        const double fixed_loss = parameter_or(
            context.component, "fixed_loss", 0.0);
        const auto driver_a_power =
            require_port_variable(context, "driver_a.W_dot");
        const auto driver_a_speed =
            require_port_variable(context, "driver_a.omega");
        const auto driver_b_power =
            require_port_variable(context, "driver_b.W_dot");
        const auto driver_b_speed =
            require_port_variable(context, "driver_b.omega");
        const auto load_power =
            require_port_variable(context, "load.W_dot");
        const auto load_speed =
            require_port_variable(context, "load.omega");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "driver_b_speed",
            {{driver_a_speed, 1.0}, {driver_b_speed, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "load_speed",
            {{driver_a_speed, 1.0}, {load_speed, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "power_balance",
            {
                {driver_a_power, efficiency},
                {driver_b_power, efficiency},
                {load_power, -1.0},
            },
            fixed_loss, 1.0e6);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class FixedRatioShaftGearboxModel final : public ComponentModel {
public:
    FixedRatioShaftGearboxModel() {
        descriptor_.kind = "gearbox.shaft.fixed_ratio";
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "gearbox.shaft";
        descriptor_.display_name = "Fixed-ratio shaft gearbox";
        descriptor_.category = "Power transmission";
        descriptor_.model_name =
            "Fixed speed ratio with mechanical efficiency";
        descriptor_.ports = {
            {"driver", "shaft", "in"},
            {"load", "shaft", "out"},
        };
        descriptor_.parameters = {
            {"speed_ratio", "dimensionless", true,
             std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, false},
            {"mechanical_efficiency", "dimensionless", false,
             1.0, 0.0, 1.0, false, true},
        };
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const double speed_ratio = required_parameter(
            context.component, "speed_ratio");
        const double efficiency = parameter_or(
            context.component, "mechanical_efficiency", 1.0);
        const auto driver_power =
            require_port_variable(context, "driver.W_dot");
        const auto driver_speed =
            require_port_variable(context, "driver.omega");
        const auto load_power =
            require_port_variable(context, "load.W_dot");
        const auto load_speed =
            require_port_variable(context, "load.omega");
        const std::string prefix =
            "component." + context.component.id + ".";

        // speed_ratio is defined as driver speed divided by load speed.
        system.add_linear_equation(
            prefix + "speed_ratio",
            {{driver_speed, 1.0}, {load_speed, -speed_ratio}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "power_transfer",
            {{driver_power, efficiency}, {load_power, -1.0}},
            0.0, 1.0e6);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class GearedTwoLoadShaftTrainModel final : public ComponentModel {
public:
    GearedTwoLoadShaftTrainModel() {
        descriptor_.kind = "shaft.train.geared_two_load";
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "shaft.train";
        descriptor_.display_name = "Geared two-load shaft train";
        descriptor_.category = "Power transmission";
        descriptor_.model_name =
            "Direct and fixed-ratio geared loads on one driver";
        descriptor_.ports = {
            {"driver", "shaft", "in"},
            {"direct_load", "shaft", "out"},
            {"geared_load", "shaft", "out"},
        };
        descriptor_.parameters = {
            {"speed_ratio", "dimensionless", true,
             std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, false},
            {"shaft_efficiency", "dimensionless", false,
             1.0, 0.0, 1.0, false, true},
            {"gearbox_efficiency", "dimensionless", false,
             1.0, 0.0, 1.0, false, true},
            {"fixed_loss", "power", false, 0.0, 0.0,
             std::numeric_limits<double>::infinity(), true, true},
        };
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const double speed_ratio = required_parameter(
            context.component, "speed_ratio");
        const double shaft_efficiency = parameter_or(
            context.component, "shaft_efficiency", 1.0);
        const double gearbox_efficiency = parameter_or(
            context.component, "gearbox_efficiency", 1.0);
        const double fixed_loss = parameter_or(
            context.component, "fixed_loss", 0.0);
        const auto driver_power =
            require_port_variable(context, "driver.W_dot");
        const auto driver_speed =
            require_port_variable(context, "driver.omega");
        const auto direct_power =
            require_port_variable(context, "direct_load.W_dot");
        const auto direct_speed =
            require_port_variable(context, "direct_load.omega");
        const auto geared_power =
            require_port_variable(context, "geared_load.W_dot");
        const auto geared_speed =
            require_port_variable(context, "geared_load.omega");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "direct_load_speed",
            {{driver_speed, 1.0}, {direct_speed, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "geared_load_speed",
            {{driver_speed, 1.0}, {geared_speed, -speed_ratio}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "power_balance",
            {
                {driver_power, shaft_efficiency},
                {direct_power, -1.0},
                {geared_power, -1.0 / gearbox_efficiency},
            },
            fixed_loss, 1.0e6);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class GeneratorModel final : public ComponentModel {
public:
    GeneratorModel() {
        descriptor_.kind = "generator.electrical.efficiency";
        descriptor_.version = "1.0.0";
        descriptor_.ports = {
            {"shaft", "shaft", "in"},
            {"electrical", "electrical", "out"},
        };
        descriptor_.parameters = {
            {"generator_efficiency", "dimensionless", true,
             std::nullopt, 0.0, 1.0, false, true},
            {"pole_pairs", "dimensionless", true, std::nullopt,
             0.0, std::numeric_limits<double>::infinity(), false,
             true},
        };
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const double efficiency = required_parameter(
            context.component, "generator_efficiency");
        const double pole_pairs = required_parameter(
            context.component, "pole_pairs");
        const auto shaft_power =
            require_port_variable(context, "shaft.W_dot");
        const auto shaft_speed =
            require_port_variable(context, "shaft.omega");
        const auto electrical_power =
            require_port_variable(context, "electrical.P");
        const auto frequency =
            require_port_variable(
                context, "electrical.frequency");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "electrical_power",
            {
                {electrical_power, 1.0},
                {shaft_power, -efficiency},
            },
            0.0, 1.0e6);
        system.add_linear_equation(
            prefix + "electrical_frequency",
            {
                {frequency, 2.0 * std::acos(-1.0)},
                {shaft_speed, -pole_pairs},
            },
            0.0, 50.0);
    }

private:
    ComponentModelDescriptor descriptor_;
};

// A deliberately reduced-order boundary model for a thermal conversion
// island whose internal equipment is not resolved.  It preserves mass and
// total energy at its public ports, while representing the measured
// part-load conversion efficiency with a dimensionless polynomial.  This is
// useful for plant-level calibration studies; it must not be presented as a
// first-principles turbine or steam-cycle model.
class FluidToElectricalPolynomialModel final : public ComponentModel {
public:
    FluidToElectricalPolynomialModel() {
        descriptor_.kind =
            "converter.fluid_to_electrical.polynomial_efficiency";
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "converter.thermal_to_electrical";
        descriptor_.display_name =
            "Fluid-to-electric conversion island";
        descriptor_.category = "Reduced-order conversion";
        descriptor_.model_name =
            "Polynomial part-load gross efficiency";
        descriptor_.ports = {
            {"inlet", "fluid", "in"},
            {"outlet", "fluid", "out"},
            {"electrical", "electrical", "out"},
            {"rejected_heat", "heat", "out"},
        };
        descriptor_.parameters = {
            {"outlet_specific_enthalpy", "specific_enthalpy", true,
             std::nullopt},
            {"reference_thermal_power", "power", true, std::nullopt,
             0.0, std::numeric_limits<double>::infinity(), false, true},
            {"efficiency_intercept", "dimensionless", true,
             std::nullopt, 0.0, 1.0, true, true},
            {"efficiency_linear", "dimensionless", false, 0.0,
             -2.0, 2.0, true, true},
            {"efficiency_quadratic", "dimensionless", false, 0.0,
             -2.0, 2.0, true, true},
            {"electrical_frequency", "frequency", false, 50.0,
             0.0, std::numeric_limits<double>::infinity(), false, true},
            {"rejection_temperature", "temperature", false, 300.0,
             0.0, std::numeric_limits<double>::infinity(), false, true},
        };
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const double outlet_specific_enthalpy = required_parameter(
            context.component, "outlet_specific_enthalpy");
        const double reference_thermal_power = required_parameter(
            context.component, "reference_thermal_power");
        const double efficiency_intercept = required_parameter(
            context.component, "efficiency_intercept");
        const double efficiency_linear = parameter_or(
            context.component, "efficiency_linear", 0.0);
        const double efficiency_quadratic = parameter_or(
            context.component, "efficiency_quadratic", 0.0);
        const double electrical_frequency = parameter_or(
            context.component, "electrical_frequency", 50.0);
        const double rejection_temperature = parameter_or(
            context.component, "rejection_temperature", 300.0);
        const auto inlet_mass_flow =
            require_port_variable(context, "inlet.m_dot");
        const auto inlet_pressure =
            require_port_variable(context, "inlet.p");
        const auto inlet_enthalpy =
            require_port_variable(context, "inlet.h");
        const auto outlet_mass_flow =
            require_port_variable(context, "outlet.m_dot");
        const auto outlet_pressure =
            require_port_variable(context, "outlet.p");
        const auto outlet_enthalpy =
            require_port_variable(context, "outlet.h");
        const auto electrical_power =
            require_port_variable(context, "electrical.P");
        const auto frequency =
            require_port_variable(context, "electrical.frequency");
        const auto rejected_heat =
            require_port_variable(context, "rejected_heat.Q_dot");
        const auto rejected_heat_temperature =
            require_port_variable(context, "rejected_heat.T");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_balance",
            {{outlet_mass_flow, 1.0}, {inlet_mass_flow, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "pressure_continuity",
            {{outlet_pressure, 1.0}, {inlet_pressure, -1.0}},
            0.0, 1.0e5);
        system.add_linear_equation(
            prefix + "outlet_enthalpy",
            {{outlet_enthalpy, 1.0}},
            outlet_specific_enthalpy, 1.0e5);
        system.add_linear_equation(
            prefix + "electrical_frequency",
            {{frequency, 1.0}}, electrical_frequency, 50.0);
        system.add_linear_equation(
            prefix + "rejection_temperature",
            {{rejected_heat_temperature, 1.0}},
            rejection_temperature, 100.0);
        system.add_checked_equation(
            prefix + "part_load_power",
            [inlet_mass_flow, inlet_enthalpy, electrical_power,
             outlet_specific_enthalpy, reference_thermal_power,
             efficiency_intercept, efficiency_linear,
             efficiency_quadratic](
                const std::vector<double>& x, double& residual) {
                const double thermal_power =
                    x.at(inlet_mass_flow) *
                    (x.at(inlet_enthalpy) -
                     outlet_specific_enthalpy);
                const double load =
                    thermal_power / reference_thermal_power;
                const double efficiency =
                    efficiency_intercept +
                    efficiency_linear * load +
                    efficiency_quadratic * load * load;
                if (!std::isfinite(thermal_power) ||
                    thermal_power < 0.0 ||
                    !std::isfinite(efficiency) || efficiency < 0.0 ||
                    efficiency > 1.0) {
                    return EvaluationStatus::recoverable(
                        "fluid-to-electric surrogate is outside its "
                        "physical thermal-power or efficiency domain");
                }
                residual = x.at(electrical_power) -
                    efficiency * thermal_power;
                return EvaluationStatus::success();
            },
            1.0e6);
        system.add_sparse_equation(
            prefix + "total_energy_balance",
            [inlet_mass_flow, inlet_enthalpy, electrical_power,
             rejected_heat, outlet_specific_enthalpy](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                const double enthalpy_drop =
                    x.at(inlet_enthalpy) -
                    outlet_specific_enthalpy;
                jacobian.push_back(
                    {inlet_mass_flow, -enthalpy_drop});
                jacobian.push_back(
                    {inlet_enthalpy, -x.at(inlet_mass_flow)});
                jacobian.push_back({electrical_power, 1.0});
                jacobian.push_back({rejected_heat, 1.0});
                return x.at(electrical_power) +
                    x.at(rejected_heat) -
                    x.at(inlet_mass_flow) * enthalpy_drop;
            },
            1.0e6);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class TwoPortShaftInertiaModel final : public ComponentModel {
public:
    TwoPortShaftInertiaModel() {
        descriptor_.kind = "shaft.inertia.two_port";
        descriptor_.version = "1.0.0";
        descriptor_.ports = {
            {"driver", "shaft", "in"},
            {"load", "shaft", "out"},
        };
        descriptor_.parameters = {
            {"moment_of_inertia", "moment_of_inertia", true,
             std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"mechanical_efficiency", "dimensionless", false,
             1.0, 0.0, 1.0, false, true},
            {"fixed_loss", "power", false, 0.0, 0.0,
             std::numeric_limits<double>::infinity(), true, true},
        };
        descriptor_.supports_steady = true;
        descriptor_.supports_transient = true;
        descriptor_.internal_variables = {
            {"rotational_energy", DaeVariableKind::differential,
             1.0e6, 1.0e6, 0.0, 1.0e6, 0.0,
             std::numeric_limits<double>::infinity(), "energy"},
            {"omega", DaeVariableKind::algebraic,
             100.0, 100.0, 0.0, 100.0, 0.0,
             std::numeric_limits<double>::infinity(),
             "angular_speed"},
        };
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const double efficiency = parameter_or(
            context.component, "mechanical_efficiency", 1.0);
        const double fixed_loss = parameter_or(
            context.component, "fixed_loss", 0.0);
        const auto driver_power =
            require_port_variable(context, "driver.W_dot");
        const auto driver_speed =
            require_port_variable(context, "driver.omega");
        const auto load_power =
            require_port_variable(context, "load.W_dot");
        const auto load_speed =
            require_port_variable(context, "load.omega");
        const std::string prefix =
            "component." + context.component.id + ".";
        system.add_linear_equation(
            prefix + "speed_continuity",
            {{driver_speed, 1.0}, {load_speed, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "steady_power_balance",
            {{driver_power, efficiency}, {load_power, -1.0}},
            fixed_loss, 1.0e6);
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        const double inertia =
            required_parameter(context.component, "moment_of_inertia");
        const double efficiency = parameter_or(
            context.component, "mechanical_efficiency", 1.0);
        const double fixed_loss = parameter_or(
            context.component, "fixed_loss", 0.0);
        const auto driver_power =
            require_port_variable(context, "driver.W_dot");
        const auto driver_speed =
            require_port_variable(context, "driver.omega");
        const auto load_power =
            require_port_variable(context, "load.W_dot");
        const auto load_speed =
            require_port_variable(context, "load.omega");
        const auto energy =
            require_internal_variable(
                context, "rotational_energy");
        const auto speed =
            require_internal_variable(context, "omega");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "driver_speed",
            {{driver_speed, 1.0, 0.0},
             {speed, -1.0, 0.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "load_speed",
            {{load_speed, 1.0, 0.0},
             {speed, -1.0, 0.0}},
            0.0, 100.0);
        system.add_sparse_equation(
            prefix + "kinetic_energy_closure",
            {energy, speed},
            [inertia, energy, speed](
                double, const std::vector<double>& state,
                const std::vector<double>&, double& residual,
                std::vector<DaeEquationPartial>& jacobian) {
                residual = state.at(energy) -
                    0.5 * inertia * state.at(speed) *
                        state.at(speed);
                jacobian.push_back({energy, 1.0, 0.0});
                jacobian.push_back(
                    {speed, -inertia * state.at(speed), 0.0});
                return EvaluationStatus::success();
            },
            1.0e6);
        system.add_linear_equation(
            prefix + "energy_accumulation",
            {{energy, 0.0, 1.0},
             {driver_power, -efficiency, 0.0},
             {load_power, 1.0, 0.0}},
            -fixed_loss, 1.0e6);
    }

private:
    ComponentModelDescriptor descriptor_;
};

}  // namespace

void register_power_component_models(ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<TwoLoadShaftTrainModel>());
    registry.register_model(
        std::make_shared<TwoDriverShaftCombinerModel>());
    registry.register_model(
        std::make_shared<FixedRatioShaftGearboxModel>());
    registry.register_model(
        std::make_shared<GearedTwoLoadShaftTrainModel>());
    registry.register_model(std::make_shared<GeneratorModel>());
    registry.register_model(
        std::make_shared<FluidToElectricalPolynomialModel>());
    registry.register_model(
        std::make_shared<TwoPortShaftInertiaModel>());
}

}  // namespace thermox::platform
