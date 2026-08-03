#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

namespace thermox::platform {

namespace {

using component_model_support::property_failure;
using component_model_support::require_port_variable;
using component_model_support::require_port_species;
using component_model_support::require_property_package;
using component_model_support::require_thermochemistry_package;
using component_model_support::required_parameter;

ComponentModelDescriptor exchanger_descriptor(std::string kind) {
    ComponentModelDescriptor out;
    out.kind = std::move(kind);
    out.version = "1.0.0";
    out.ports = {
        {"hot_in", "fluid", "in"},
        {"hot_out", "fluid", "out"},
        {"cold_in", "fluid", "in"},
        {"cold_out", "fluid", "out"}};
    return out;
}

ComponentModelDescriptor material_fluid_exchanger_descriptor(
    std::string kind) {
    ComponentModelDescriptor out;
    out.kind = std::move(kind);
    out.version = "1.0.0";
    out.template_kind = "heat_exchanger.material_fluid";
    out.display_name = "Gas-to-fluid heat exchanger";
    out.category = "Heat transfer";
    out.ports = {
        {"hot_in", "material", "in"},
        {"hot_out", "material", "out"},
        {"cold_in", "fluid", "in"},
        {"cold_out", "fluid", "out"}};
    return out;
}

EvaluationStatus thermochemistry_failure(
    const physics::ThermochemicalResult& result) {
    if (result.status == physics::PropertyStatus::backend_error) {
        return EvaluationStatus::fatal(result.message);
    }
    return EvaluationStatus::recoverable(result.message);
}

physics::SpeciesComposition material_composition(
    const std::vector<double>& values,
    const std::vector<std::string>& species,
    const std::vector<std::size_t>& flows) {
    std::vector<double> fractions;
    fractions.reserve(flows.size());
    double total = 0.0;
    for (const auto flow : flows) {
        const double value = values.at(flow);
        if (!std::isfinite(value) || value < 0.0) {
            throw std::domain_error(
                "heat exchanger material species flows must be "
                "finite and nonnegative");
        }
        fractions.push_back(value);
        total += value;
    }
    if (!std::isfinite(total) || total <= 0.0) {
        throw std::domain_error(
            "heat exchanger material flow must be finite and positive");
    }
    for (double& fraction : fractions) fraction /= total;
    return {
        physics::CompositionBasis::mass_fraction,
        species,
        std::move(fractions)};
}

double total_flow(
    const std::vector<double>& values,
    const std::vector<std::size_t>& flows) {
    double total = 0.0;
    for (const auto flow : flows) total += values.at(flow);
    return total;
}

double parameter_value(
    const ComponentDefinition& component,
    const ComponentModelDescriptor& descriptor,
    const std::string& name) {
    const auto supplied = component.parameters.find(name);
    if (supplied != component.parameters.end()) {
        return supplied->second.value_si;
    }
    const auto declared = std::find_if(
        descriptor.parameters.begin(), descriptor.parameters.end(),
        [&](const auto& parameter) {
            return parameter.name == name;
        });
    if (declared == descriptor.parameters.end() ||
        !declared->default_value.has_value()) {
        throw std::logic_error(
            "component parameter default missing: " +
            descriptor.kind + "." + name);
    }
    return *declared->default_value;
}

EvaluationStatus log_mean_temperature_difference(
    double difference_a,
    double difference_b,
    double& value) {
    if (!std::isfinite(difference_a) ||
        !std::isfinite(difference_b) ||
        difference_a <= 0.0 || difference_b <= 0.0) {
        return EvaluationStatus::recoverable(
            "heat exchanger terminal temperature differences must be positive");
    }
    const double largest =
        std::max(std::abs(difference_a), std::abs(difference_b));
    if (std::abs(difference_a - difference_b) <=
        1.0e-8 * std::max(largest, 1.0)) {
        value = 0.5 * (difference_a + difference_b);
        return EvaluationStatus::success();
    }
    value = (difference_a - difference_b) /
            std::log(difference_a / difference_b);
    if (!std::isfinite(value) || value <= 0.0) {
        return EvaluationStatus::recoverable(
            "heat exchanger LMTD evaluation failed");
    }
    return EvaluationStatus::success();
}

EvaluationStatus continued_log_mean_temperature_difference(
    double difference_a,
    double difference_b,
    double anchor_difference_a,
    double anchor_difference_b,
    double anchor_inlet_span,
    double continuation_parameter,
    double& value) {
    if (continuation_parameter >= 1.0) {
        return log_mean_temperature_difference(
            difference_a, difference_b, value);
    }
    if (!std::isfinite(difference_a) ||
        !std::isfinite(difference_b)) {
        return EvaluationStatus::recoverable(
            "heat exchanger terminal temperature differences "
            "must be finite");
    }

    const double fallback =
        std::max(std::abs(anchor_inlet_span), 1.0);
    const auto positive_seed =
        [fallback](double difference) {
            if (!std::isfinite(difference)) return fallback;
            return std::max(std::abs(difference), 1.0);
        };
    const double seed_a = positive_seed(anchor_difference_a);
    const double seed_b = positive_seed(anchor_difference_b);
    const double positive_floor =
        std::max(
            (1.0 - continuation_parameter) * 1.0e-3,
            1.0e-12);
    const double staged_a = std::max(
        seed_a +
            continuation_parameter *
                (difference_a - seed_a),
        positive_floor);
    const double staged_b = std::max(
        seed_b +
            continuation_parameter *
                (difference_b - seed_b),
        positive_floor);
    return log_mean_temperature_difference(
        staged_a, staged_b, value);
}

class FixedDutyHeatExchangerModel final : public ComponentModel {
public:
    FixedDutyHeatExchangerModel()
        : descriptor_(exchanger_descriptor(
              "heat_exchanger.fluid.fixed_duty")) {
        descriptor_.parameters = {
            {"heat_duty", "power", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"hot_pressure_loss_fraction", "dimensionless", false,
             0.0, 0.0, 1.0, true, false},
            {"cold_pressure_loss_fraction", "dimensionless", false,
             0.0, 0.0, 1.0, true, false}};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const double duty =
            required_parameter(context.component, "heat_duty");
        const double hot_loss = parameter_value(
            context.component, descriptor_,
            "hot_pressure_loss_fraction");
        const double cold_loss = parameter_value(
            context.component, descriptor_,
            "cold_pressure_loss_fraction");
        if (require_property_package(context, "hot_in") !=
            require_property_package(context, "hot_out")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' hot-side ports must use the same medium");
        }
        if (require_property_package(context, "cold_in") !=
            require_property_package(context, "cold_out")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' cold-side ports must use the same medium");
        }
        const auto hot_in_m =
            require_port_variable(context, "hot_in.m_dot");
        const auto hot_in_p =
            require_port_variable(context, "hot_in.p");
        const auto hot_in_h =
            require_port_variable(context, "hot_in.h");
        const auto hot_out_m =
            require_port_variable(context, "hot_out.m_dot");
        const auto hot_out_p =
            require_port_variable(context, "hot_out.p");
        const auto hot_out_h =
            require_port_variable(context, "hot_out.h");
        const auto cold_in_m =
            require_port_variable(context, "cold_in.m_dot");
        const auto cold_in_p =
            require_port_variable(context, "cold_in.p");
        const auto cold_in_h =
            require_port_variable(context, "cold_in.h");
        const auto cold_out_m =
            require_port_variable(context, "cold_out.m_dot");
        const auto cold_out_p =
            require_port_variable(context, "cold_out.p");
        const auto cold_out_h =
            require_port_variable(context, "cold_out.h");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "hot_mass_continuity",
            {{hot_out_m, 1.0}, {hot_in_m, -1.0}}, 0.0, 100.0);
        system.add_linear_equation(
            prefix + "cold_mass_continuity",
            {{cold_out_m, 1.0}, {cold_in_m, -1.0}}, 0.0, 100.0);
        system.add_linear_equation(
            prefix + "hot_pressure_loss",
            {{hot_out_p, 1.0}, {hot_in_p, -(1.0 - hot_loss)}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "cold_pressure_loss",
            {{cold_out_p, 1.0}, {cold_in_p, -(1.0 - cold_loss)}},
            0.0, 100000.0);
        system.add_continuation_sparse_equation(
            prefix + "hot_energy",
            {hot_in_m, hot_in_h, hot_out_h},
            [hot_in_m, hot_in_h, hot_out_h, duty](
                const std::vector<double>& x,
                const std::vector<double>& anchor,
                double continuation_parameter,
                std::vector<EquationPartial>& jacobian) {
                const double delta_h =
                    x.at(hot_in_h) - x.at(hot_out_h);
                const double anchor_duty =
                    anchor.at(hot_in_m) *
                    (anchor.at(hot_in_h) -
                     anchor.at(hot_out_h));
                const double staged_duty =
                    anchor_duty +
                    continuation_parameter *
                        (duty - anchor_duty);
                jacobian.push_back({hot_in_m, delta_h});
                jacobian.push_back({hot_in_h, x.at(hot_in_m)});
                jacobian.push_back({hot_out_h, -x.at(hot_in_m)});
                return x.at(hot_in_m) * delta_h - staged_duty;
            },
            std::max(duty, 1.0));
        system.add_continuation_sparse_equation(
            prefix + "cold_energy",
            {cold_in_m, cold_in_h, cold_out_h},
            [cold_in_m, cold_in_h, cold_out_h, duty](
                const std::vector<double>& x,
                const std::vector<double>& anchor,
                double continuation_parameter,
                std::vector<EquationPartial>& jacobian) {
                const double delta_h =
                    x.at(cold_out_h) - x.at(cold_in_h);
                const double anchor_duty =
                    anchor.at(cold_in_m) *
                    (anchor.at(cold_out_h) -
                     anchor.at(cold_in_h));
                const double staged_duty =
                    anchor_duty +
                    continuation_parameter *
                        (duty - anchor_duty);
                jacobian.push_back({cold_in_m, delta_h});
                jacobian.push_back({cold_in_h, -x.at(cold_in_m)});
                jacobian.push_back({cold_out_h, x.at(cold_in_m)});
                return x.at(cold_in_m) * delta_h - staged_duty;
            },
            std::max(duty, 1.0));
    }

private:
    ComponentModelDescriptor descriptor_;
};

class CounterflowUaHeatExchangerModel final : public ComponentModel {
public:
    CounterflowUaHeatExchangerModel()
        : descriptor_(exchanger_descriptor(
              "heat_exchanger.fluid.counterflow_ua")) {
        descriptor_.parameters = {
            {"UA", "thermal_conductance", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"hot_pressure_loss_fraction", "dimensionless", false,
             0.0, 0.0, 1.0, true, false},
            {"cold_pressure_loss_fraction", "dimensionless", false,
             0.0, 0.0, 1.0, true, false}};
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const double conductance =
            required_parameter(context.component, "UA");
        const double hot_loss = parameter_value(
            context.component, descriptor_,
            "hot_pressure_loss_fraction");
        const double cold_loss = parameter_value(
            context.component, descriptor_,
            "cold_pressure_loss_fraction");
        const auto hot_properties =
            require_property_package(context, "hot_in");
        const auto cold_properties =
            require_property_package(context, "cold_in");
        if (hot_properties !=
            require_property_package(context, "hot_out")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' hot-side ports must use the same medium");
        }
        if (cold_properties !=
            require_property_package(context, "cold_out")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' cold-side ports must use the same medium");
        }
        const auto hot_in_m =
            require_port_variable(context, "hot_in.m_dot");
        const auto hot_in_p =
            require_port_variable(context, "hot_in.p");
        const auto hot_in_h =
            require_port_variable(context, "hot_in.h");
        const auto hot_out_m =
            require_port_variable(context, "hot_out.m_dot");
        const auto hot_out_p =
            require_port_variable(context, "hot_out.p");
        const auto hot_out_h =
            require_port_variable(context, "hot_out.h");
        const auto cold_in_m =
            require_port_variable(context, "cold_in.m_dot");
        const auto cold_in_p =
            require_port_variable(context, "cold_in.p");
        const auto cold_in_h =
            require_port_variable(context, "cold_in.h");
        const auto cold_out_m =
            require_port_variable(context, "cold_out.m_dot");
        const auto cold_out_p =
            require_port_variable(context, "cold_out.p");
        const auto cold_out_h =
            require_port_variable(context, "cold_out.h");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "hot_mass_continuity",
            {{hot_out_m, 1.0}, {hot_in_m, -1.0}}, 0.0, 100.0);
        system.add_linear_equation(
            prefix + "cold_mass_continuity",
            {{cold_out_m, 1.0}, {cold_in_m, -1.0}}, 0.0, 100.0);
        system.add_linear_equation(
            prefix + "hot_pressure_loss",
            {{hot_out_p, 1.0}, {hot_in_p, -(1.0 - hot_loss)}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "cold_pressure_loss",
            {{cold_out_p, 1.0}, {cold_in_p, -(1.0 - cold_loss)}},
            0.0, 100000.0);
        system.add_continuation_sparse_equation(
            prefix + "energy_balance",
            {hot_in_m, hot_in_h, hot_out_h,
             cold_in_m, cold_in_h, cold_out_h},
            [hot_in_m, hot_in_h, hot_out_h,
             cold_in_m, cold_in_h, cold_out_h](
                const std::vector<double>& x,
                const std::vector<double>& anchor,
                double continuation_parameter,
                std::vector<EquationPartial>& jacobian) {
                const double hot_delta =
                    x.at(hot_in_h) - x.at(hot_out_h);
                const double cold_delta =
                    x.at(cold_out_h) - x.at(cold_in_h);
                const double anchor_imbalance =
                    anchor.at(hot_in_m) *
                        (anchor.at(hot_in_h) -
                         anchor.at(hot_out_h)) -
                    anchor.at(cold_in_m) *
                        (anchor.at(cold_out_h) -
                         anchor.at(cold_in_h));
                jacobian.push_back({hot_in_m, hot_delta});
                jacobian.push_back({hot_in_h, x.at(hot_in_m)});
                jacobian.push_back({hot_out_h, -x.at(hot_in_m)});
                jacobian.push_back({cold_in_m, -cold_delta});
                jacobian.push_back({cold_in_h, x.at(cold_in_m)});
                jacobian.push_back({cold_out_h, -x.at(cold_in_m)});
                return x.at(hot_in_m) * hot_delta -
                       x.at(cold_in_m) * cold_delta -
                       (1.0 - continuation_parameter) *
                           anchor_imbalance;
            },
            1.0e7);
        system.add_continuation_checked_equation(
            prefix + "counterflow_heat_transfer",
            [hot_properties, cold_properties, conductance,
             hot_in_m, hot_in_p, hot_in_h, hot_out_p, hot_out_h,
             cold_in_p, cold_in_h, cold_out_p, cold_out_h](
                const std::vector<double>& x,
                const std::vector<double>& anchor,
                double continuation_parameter,
                double& residual) {
                const auto hot_in = hot_properties->state_ph(
                    x.at(hot_in_p), x.at(hot_in_h));
                if (!hot_in.ok()) return property_failure(hot_in);
                const auto hot_out = hot_properties->state_ph(
                    x.at(hot_out_p), x.at(hot_out_h));
                if (!hot_out.ok()) return property_failure(hot_out);
                const auto cold_in = cold_properties->state_ph(
                    x.at(cold_in_p), x.at(cold_in_h));
                if (!cold_in.ok()) return property_failure(cold_in);
                const auto cold_out = cold_properties->state_ph(
                    x.at(cold_out_p), x.at(cold_out_h));
                if (!cold_out.ok()) return property_failure(cold_out);
                const auto anchor_hot_in =
                    hot_properties->state_ph(
                        anchor.at(hot_in_p),
                        anchor.at(hot_in_h));
                if (!anchor_hot_in.ok())
                    return property_failure(anchor_hot_in);
                const auto anchor_hot_out =
                    hot_properties->state_ph(
                        anchor.at(hot_out_p),
                        anchor.at(hot_out_h));
                if (!anchor_hot_out.ok())
                    return property_failure(anchor_hot_out);
                const auto anchor_cold_in =
                    cold_properties->state_ph(
                        anchor.at(cold_in_p),
                        anchor.at(cold_in_h));
                if (!anchor_cold_in.ok())
                    return property_failure(anchor_cold_in);
                const auto anchor_cold_out =
                    cold_properties->state_ph(
                        anchor.at(cold_out_p),
                        anchor.at(cold_out_h));
                if (!anchor_cold_out.ok())
                    return property_failure(anchor_cold_out);
                double lmtd = 0.0;
                const auto status =
                    continued_log_mean_temperature_difference(
                    hot_in.state.temperature_k -
                        cold_out.state.temperature_k,
                    hot_out.state.temperature_k -
                        cold_in.state.temperature_k,
                    anchor_hot_in.state.temperature_k -
                        anchor_cold_out.state.temperature_k,
                    anchor_hot_out.state.temperature_k -
                        anchor_cold_in.state.temperature_k,
                    anchor_hot_in.state.temperature_k -
                        anchor_cold_in.state.temperature_k,
                    continuation_parameter,
                    lmtd);
                if (!status.ok()) return status;
                const double anchor_duty =
                    anchor.at(hot_in_m) *
                    (anchor.at(hot_in_h) -
                     anchor.at(hot_out_h));
                const double staged_heat_transfer =
                    anchor_duty +
                    continuation_parameter *
                        (conductance * lmtd - anchor_duty);
                residual =
                    x.at(hot_in_m) *
                        (x.at(hot_in_h) - x.at(hot_out_h)) -
                    staged_heat_transfer;
                return EvaluationStatus::success();
            },
            1.0e7);
    }

private:
    ComponentModelDescriptor descriptor_;
};

enum class MaterialFluidExchangerMode {
    fixed_duty,
    energy_balance,
    counterflow_ua,
};

class MaterialFluidHeatExchangerModel final : public ComponentModel {
public:
    explicit MaterialFluidHeatExchangerModel(
        MaterialFluidExchangerMode mode)
        : mode_(mode),
          descriptor_(material_fluid_exchanger_descriptor(
              mode == MaterialFluidExchangerMode::fixed_duty
                  ? "heat_exchanger.material_fluid.fixed_duty"
                  : mode == MaterialFluidExchangerMode::energy_balance
                      ? "heat_exchanger.material_fluid.energy_balance"
                      : "heat_exchanger.material_fluid.counterflow_ua")) {
        descriptor_.model_name =
            mode == MaterialFluidExchangerMode::fixed_duty
                ? "Fixed heat duty"
                : mode == MaterialFluidExchangerMode::energy_balance
                    ? "Energy balance"
                    : "Counterflow UA";
        descriptor_.parameters = {
            {"hot_pressure_loss_fraction", "dimensionless", false,
             0.0, 0.0, 1.0, true, false},
            {"cold_pressure_loss_fraction", "dimensionless", false,
             0.0, 0.0, 1.0, true, false}};
        if (mode != MaterialFluidExchangerMode::energy_balance) {
            descriptor_.parameters.insert(
                descriptor_.parameters.begin(),
                {mode == MaterialFluidExchangerMode::fixed_duty
                     ? "heat_duty"
                     : "UA",
                 mode == MaterialFluidExchangerMode::fixed_duty
                     ? "power"
                     : "thermal_conductance",
                 true, std::nullopt, 0.0,
                 std::numeric_limits<double>::infinity(), false,
                 true});
        }
        descriptor_.required_thermochemistry_capabilities = {
            physics::ThermochemistryCapability::state_ph};
        if (mode == MaterialFluidExchangerMode::counterflow_ua) {
            descriptor_.required_property_capabilities = {
                physics::PropertyCapability::state_ph};
        }
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto species = require_port_species(context, "hot_in");
        if (species != require_port_species(context, "hot_out")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' material heat-exchanger ports must use the same "
                "species basis");
        }
        const auto cold_properties =
            require_property_package(context, "cold_in");
        if (cold_properties !=
            require_property_package(context, "cold_out")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' fluid heat-exchanger ports must use the same medium");
        }
        const auto hot_properties =
            require_thermochemistry_package(context, "hot_in");
        const auto hot_out_properties =
            require_thermochemistry_package(context, "hot_out");
        if (hot_properties->name() != hot_out_properties->name() ||
            hot_properties->version() !=
                hot_out_properties->version() ||
            hot_properties->mechanism() !=
                hot_out_properties->mechanism() ||
            hot_properties->phase() != hot_out_properties->phase()) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' material heat-exchanger ports must resolve the "
                "same thermochemistry package");
        }

        std::vector<std::size_t> hot_in_flows;
        std::vector<std::size_t> hot_out_flows;
        hot_in_flows.reserve(species.size());
        hot_out_flows.reserve(species.size());
        const std::string prefix =
            "component." + context.component.id + ".";
        for (const auto& name : species) {
            const std::string variable = "m_dot[" + name + "]";
            const auto inlet = require_port_variable(
                context, "hot_in." + variable);
            const auto outlet = require_port_variable(
                context, "hot_out." + variable);
            hot_in_flows.push_back(inlet);
            hot_out_flows.push_back(outlet);
            system.add_linear_equation(
                prefix + "hot_species_continuity." + name,
                {{outlet, 1.0}, {inlet, -1.0}}, 0.0, 100.0);
        }

        const auto hot_in_p =
            require_port_variable(context, "hot_in.p");
        const auto hot_in_h =
            require_port_variable(context, "hot_in.h");
        const auto hot_out_p =
            require_port_variable(context, "hot_out.p");
        const auto hot_out_h =
            require_port_variable(context, "hot_out.h");
        const auto cold_in_m =
            require_port_variable(context, "cold_in.m_dot");
        const auto cold_in_p =
            require_port_variable(context, "cold_in.p");
        const auto cold_in_h =
            require_port_variable(context, "cold_in.h");
        const auto cold_out_m =
            require_port_variable(context, "cold_out.m_dot");
        const auto cold_out_p =
            require_port_variable(context, "cold_out.p");
        const auto cold_out_h =
            require_port_variable(context, "cold_out.h");
        const double hot_loss = parameter_value(
            context.component, descriptor_,
            "hot_pressure_loss_fraction");
        const double cold_loss = parameter_value(
            context.component, descriptor_,
            "cold_pressure_loss_fraction");

        system.add_linear_equation(
            prefix + "hot_pressure_loss",
            {{hot_out_p, 1.0},
             {hot_in_p, -(1.0 - hot_loss)}},
            0.0, 100000.0);
        system.add_linear_equation(
            prefix + "cold_mass_continuity",
            {{cold_out_m, 1.0}, {cold_in_m, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "cold_pressure_loss",
            {{cold_out_p, 1.0},
             {cold_in_p, -(1.0 - cold_loss)}},
            0.0, 100000.0);

        if (mode_ == MaterialFluidExchangerMode::fixed_duty) {
            add_fixed_duty_equations(
                context, system, prefix, hot_in_flows,
                hot_in_h, hot_out_h, cold_in_m,
                cold_in_h, cold_out_h);
            return;
        }
        if (mode_ == MaterialFluidExchangerMode::energy_balance) {
            add_energy_balance_equation(
                system, prefix, hot_in_flows,
                hot_in_h, hot_out_h, cold_in_m,
                cold_in_h, cold_out_h);
            return;
        }
        add_ua_equations(
            context, system, prefix, hot_properties,
            cold_properties, species, hot_in_flows,
            hot_in_p, hot_in_h, hot_out_p, hot_out_h,
            cold_in_m, cold_in_p, cold_in_h,
            cold_out_p, cold_out_h);
    }

private:
    static void add_fixed_duty_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system,
        const std::string& prefix,
        const std::vector<std::size_t>& hot_flows,
        std::size_t hot_in_h,
        std::size_t hot_out_h,
        std::size_t cold_in_m,
        std::size_t cold_in_h,
        std::size_t cold_out_h) {
        const double duty =
            required_parameter(context.component, "heat_duty");
        std::vector<std::size_t> hot_variables = hot_flows;
        hot_variables.push_back(hot_in_h);
        hot_variables.push_back(hot_out_h);
        system.add_continuation_sparse_equation(
            prefix + "hot_energy",
            std::move(hot_variables),
            [hot_flows, hot_in_h, hot_out_h, duty](
                const std::vector<double>& x,
                const std::vector<double>& anchor,
                double continuation_parameter,
                std::vector<EquationPartial>& jacobian) {
                const double mass = total_flow(x, hot_flows);
                const double delta_h =
                    x.at(hot_in_h) - x.at(hot_out_h);
                const double anchor_duty =
                    total_flow(anchor, hot_flows) *
                    (anchor.at(hot_in_h) -
                     anchor.at(hot_out_h));
                const double staged_duty =
                    anchor_duty + continuation_parameter *
                        (duty - anchor_duty);
                for (const auto flow : hot_flows) {
                    jacobian.push_back({flow, delta_h});
                }
                jacobian.push_back({hot_in_h, mass});
                jacobian.push_back({hot_out_h, -mass});
                return mass * delta_h - staged_duty;
            },
            std::max(duty, 1.0));
        system.add_continuation_sparse_equation(
            prefix + "cold_energy",
            {cold_in_m, cold_in_h, cold_out_h},
            [cold_in_m, cold_in_h, cold_out_h, duty](
                const std::vector<double>& x,
                const std::vector<double>& anchor,
                double continuation_parameter,
                std::vector<EquationPartial>& jacobian) {
                const double delta_h =
                    x.at(cold_out_h) - x.at(cold_in_h);
                const double anchor_duty =
                    anchor.at(cold_in_m) *
                    (anchor.at(cold_out_h) -
                     anchor.at(cold_in_h));
                const double staged_duty =
                    anchor_duty + continuation_parameter *
                        (duty - anchor_duty);
                jacobian.push_back({cold_in_m, delta_h});
                jacobian.push_back(
                    {cold_in_h, -x.at(cold_in_m)});
                jacobian.push_back(
                    {cold_out_h, x.at(cold_in_m)});
                return x.at(cold_in_m) * delta_h - staged_duty;
            },
            std::max(duty, 1.0));
    }

    static void add_energy_balance_equation(
        EquationSystemBuilder& system,
        const std::string& prefix,
        const std::vector<std::size_t>& hot_flows,
        std::size_t hot_in_h,
        std::size_t hot_out_h,
        std::size_t cold_in_m,
        std::size_t cold_in_h,
        std::size_t cold_out_h) {
        std::vector<std::size_t> energy_variables = hot_flows;
        energy_variables.insert(
            energy_variables.end(),
            {hot_in_h, hot_out_h, cold_in_m,
             cold_in_h, cold_out_h});
        system.add_continuation_sparse_equation(
            prefix + "energy_balance",
            std::move(energy_variables),
            [hot_flows, hot_in_h, hot_out_h,
             cold_in_m, cold_in_h, cold_out_h](
                const std::vector<double>& x,
                const std::vector<double>& anchor,
                double continuation_parameter,
                std::vector<EquationPartial>& jacobian) {
                const double hot_mass = total_flow(x, hot_flows);
                const double hot_delta =
                    x.at(hot_in_h) - x.at(hot_out_h);
                const double cold_delta =
                    x.at(cold_out_h) - x.at(cold_in_h);
                const double anchor_imbalance =
                    total_flow(anchor, hot_flows) *
                        (anchor.at(hot_in_h) -
                         anchor.at(hot_out_h)) -
                    anchor.at(cold_in_m) *
                        (anchor.at(cold_out_h) -
                         anchor.at(cold_in_h));
                for (const auto flow : hot_flows) {
                    jacobian.push_back({flow, hot_delta});
                }
                jacobian.push_back({hot_in_h, hot_mass});
                jacobian.push_back({hot_out_h, -hot_mass});
                jacobian.push_back({cold_in_m, -cold_delta});
                jacobian.push_back(
                    {cold_in_h, x.at(cold_in_m)});
                jacobian.push_back(
                    {cold_out_h, -x.at(cold_in_m)});
                return hot_mass * hot_delta -
                    x.at(cold_in_m) * cold_delta -
                    (1.0 - continuation_parameter) *
                        anchor_imbalance;
            },
            1.0e7);
    }

    static void add_ua_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system,
        const std::string& prefix,
        std::shared_ptr<const physics::ThermochemistryPackage>
            hot_properties,
        std::shared_ptr<const physics::PropertyPackage>
            cold_properties,
        const std::vector<std::string>& species,
        const std::vector<std::size_t>& hot_flows,
        std::size_t hot_in_p,
        std::size_t hot_in_h,
        std::size_t hot_out_p,
        std::size_t hot_out_h,
        std::size_t cold_in_m,
        std::size_t cold_in_p,
        std::size_t cold_in_h,
        std::size_t cold_out_p,
        std::size_t cold_out_h) {
        const double conductance =
            required_parameter(context.component, "UA");
        add_energy_balance_equation(
            system, prefix, hot_flows,
            hot_in_h, hot_out_h, cold_in_m,
            cold_in_h, cold_out_h);

        system.add_continuation_checked_equation(
            prefix + "counterflow_heat_transfer",
            [hot_properties = std::move(hot_properties),
             cold_properties = std::move(cold_properties),
             species, hot_flows, conductance,
             hot_in_p, hot_in_h, hot_out_p, hot_out_h,
             cold_in_p, cold_in_h, cold_out_p, cold_out_h](
                const std::vector<double>& x,
                const std::vector<double>& anchor,
                double continuation_parameter,
                double& residual) {
                try {
                    const auto composition = material_composition(
                        x, species, hot_flows);
                    const auto anchor_composition =
                        material_composition(
                            anchor, species, hot_flows);
                    const auto hot_in = hot_properties->state_ph(
                        x.at(hot_in_p), x.at(hot_in_h), composition);
                    if (!hot_in.ok())
                        return thermochemistry_failure(hot_in);
                    const auto hot_out = hot_properties->state_ph(
                        x.at(hot_out_p), x.at(hot_out_h), composition);
                    if (!hot_out.ok())
                        return thermochemistry_failure(hot_out);
                    const auto cold_in = cold_properties->state_ph(
                        x.at(cold_in_p), x.at(cold_in_h));
                    if (!cold_in.ok()) return property_failure(cold_in);
                    const auto cold_out = cold_properties->state_ph(
                        x.at(cold_out_p), x.at(cold_out_h));
                    if (!cold_out.ok()) return property_failure(cold_out);
                    const auto anchor_hot_in = hot_properties->state_ph(
                        anchor.at(hot_in_p), anchor.at(hot_in_h),
                        anchor_composition);
                    if (!anchor_hot_in.ok())
                        return thermochemistry_failure(anchor_hot_in);
                    const auto anchor_hot_out = hot_properties->state_ph(
                        anchor.at(hot_out_p), anchor.at(hot_out_h),
                        anchor_composition);
                    if (!anchor_hot_out.ok())
                        return thermochemistry_failure(anchor_hot_out);
                    const auto anchor_cold_in =
                        cold_properties->state_ph(
                            anchor.at(cold_in_p),
                            anchor.at(cold_in_h));
                    if (!anchor_cold_in.ok())
                        return property_failure(anchor_cold_in);
                    const auto anchor_cold_out =
                        cold_properties->state_ph(
                            anchor.at(cold_out_p),
                            anchor.at(cold_out_h));
                    if (!anchor_cold_out.ok())
                        return property_failure(anchor_cold_out);
                    double lmtd = 0.0;
                    const auto status =
                        continued_log_mean_temperature_difference(
                            hot_in.state.thermodynamic.temperature_k -
                                cold_out.state.temperature_k,
                            hot_out.state.thermodynamic.temperature_k -
                                cold_in.state.temperature_k,
                            anchor_hot_in.state.thermodynamic.temperature_k -
                                anchor_cold_out.state.temperature_k,
                            anchor_hot_out.state.thermodynamic.temperature_k -
                                anchor_cold_in.state.temperature_k,
                            anchor_hot_in.state.thermodynamic.temperature_k -
                                anchor_cold_in.state.temperature_k,
                            continuation_parameter,
                            lmtd);
                    if (!status.ok()) return status;
                    const double hot_mass =
                        total_flow(x, hot_flows);
                    const double anchor_duty =
                        total_flow(anchor, hot_flows) *
                        (anchor.at(hot_in_h) -
                         anchor.at(hot_out_h));
                    const double staged_heat_transfer =
                        anchor_duty + continuation_parameter *
                            (conductance * lmtd - anchor_duty);
                    residual = hot_mass *
                        (x.at(hot_in_h) - x.at(hot_out_h)) -
                        staged_heat_transfer;
                    return EvaluationStatus::success();
                } catch (const std::domain_error& error) {
                    return EvaluationStatus::recoverable(error.what());
                }
            },
            1.0e7);
    }

    MaterialFluidExchangerMode mode_;
    ComponentModelDescriptor descriptor_;
};

class PrescribedDutyMaterialConditionerModel final
    : public ComponentModel {
public:
    explicit PrescribedDutyMaterialConditionerModel(bool heating)
        : heating_(heating) {
        descriptor_.kind = heating
            ? "heater.material.fixed_duty"
            : "cooler.material.fixed_duty";
        descriptor_.version = "1.0.0";
        descriptor_.template_kind =
            "thermal_conditioner.material";
        descriptor_.display_name = heating
            ? "Material heater (fixed duty)"
            : "Material cooler (fixed duty)";
        descriptor_.category = "Heat transfer";
        descriptor_.ports = {
            {"inlet", "material", "in"},
            {"outlet", "material", "out"}};
        descriptor_.parameters = {
            {"heat_duty", "power", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"pressure_loss_fraction", "dimensionless", false,
             0.0, 0.0, 1.0, true, false}};
        descriptor_.required_thermochemistry_capabilities = {
            physics::ThermochemistryCapability::state_ph};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto species = require_port_species(context, "inlet");
        if (species != require_port_species(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' material conditioner ports must use the same "
                "species basis");
        }
        const auto properties =
            require_thermochemistry_package(context, "inlet");
        const auto outlet_properties =
            require_thermochemistry_package(context, "outlet");
        if (properties->name() != outlet_properties->name() ||
            properties->version() != outlet_properties->version() ||
            properties->mechanism() !=
                outlet_properties->mechanism() ||
            properties->phase() != outlet_properties->phase()) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' material conditioner ports must resolve the "
                "same thermochemistry package");
        }

        const std::string prefix =
            "component." + context.component.id + ".";
        std::vector<std::size_t> inlet_flows;
        inlet_flows.reserve(species.size());
        for (const auto& name : species) {
            const std::string variable = "m_dot[" + name + "]";
            const auto inlet = require_port_variable(
                context, "inlet." + variable);
            const auto outlet = require_port_variable(
                context, "outlet." + variable);
            inlet_flows.push_back(inlet);
            system.add_linear_equation(
                prefix + "species_continuity." + name,
                {{outlet, 1.0}, {inlet, -1.0}}, 0.0, 100.0);
        }

        const auto inlet_p =
            require_port_variable(context, "inlet.p");
        const auto inlet_h =
            require_port_variable(context, "inlet.h");
        const auto outlet_p =
            require_port_variable(context, "outlet.p");
        const auto outlet_h =
            require_port_variable(context, "outlet.h");
        const double pressure_loss = parameter_value(
            context.component, descriptor_,
            "pressure_loss_fraction");
        const double duty =
            required_parameter(context.component, "heat_duty");
        system.add_linear_equation(
            prefix + "pressure_loss",
            {{outlet_p, 1.0},
             {inlet_p, -(1.0 - pressure_loss)}},
            0.0, 100000.0);

        std::vector<std::size_t> energy_variables = inlet_flows;
        energy_variables.push_back(inlet_h);
        energy_variables.push_back(outlet_h);
        system.add_continuation_sparse_equation(
            prefix + "energy",
            std::move(energy_variables),
            [flows = std::move(inlet_flows), inlet_h, outlet_h,
             duty, direction = heating_ ? -1.0 : 1.0](
                const std::vector<double>& x,
                const std::vector<double>& anchor,
                double continuation_parameter,
                std::vector<EquationPartial>& jacobian) {
                const double mass = total_flow(x, flows);
                const double delta_h = direction *
                    (x.at(inlet_h) - x.at(outlet_h));
                const double anchor_duty =
                    total_flow(anchor, flows) * direction *
                    (anchor.at(inlet_h) -
                     anchor.at(outlet_h));
                const double staged_duty =
                    anchor_duty + continuation_parameter *
                        (duty - anchor_duty);
                for (const auto flow : flows) {
                    jacobian.push_back({flow, delta_h});
                }
                jacobian.push_back(
                    {inlet_h, direction * mass});
                jacobian.push_back(
                    {outlet_h, -direction * mass});
                return mass * delta_h - staged_duty;
            },
            std::max(duty, 1.0));
    }

private:
    bool heating_{false};
    ComponentModelDescriptor descriptor_;
};

class FixedOutletQualityPhaseChangeModel final : public ComponentModel {
public:
    FixedOutletQualityPhaseChangeModel(
        std::string kind, bool evaporator) : evaporator_(evaporator) {
        descriptor_.kind = std::move(kind);
        descriptor_.version = "2.0.0";
        descriptor_.ports = {
            {"inlet", "fluid", "in"},
            {"outlet", "fluid", "out"},
            {"heat", "heat", evaporator ? "in" : "out"}};
        descriptor_.parameters = {
            {"outlet_quality", "dimensionless", true, std::nullopt,
             0.0, 1.0, true, true},
            {"pressure_loss_fraction", "dimensionless", false,
             0.0, 0.0, 1.0, true, false}};
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::saturation_p};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const double target_quality =
            required_parameter(context.component, "outlet_quality");
        const double pressure_loss = parameter_value(
            context.component, descriptor_, "pressure_loss_fraction");
        const auto properties =
            require_property_package(context, "inlet");
        if (properties != require_property_package(context, "outlet")) {
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
        const auto heat_flow =
            require_port_variable(context, "heat.Q_dot");
        const auto heat_temperature =
            require_port_variable(context, "heat.T");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_continuity",
            {{outlet_m, 1.0}, {inlet_m, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "pressure_loss",
            {{outlet_p, 1.0},
             {inlet_p, -(1.0 - pressure_loss)}},
            0.0, 100000.0);
        system.add_checked_equation(
            prefix + "outlet_quality",
            [properties, outlet_p, outlet_h, target_quality](
                const std::vector<double>& x, double& residual) {
                const auto saturation =
                    properties->saturation_p(x.at(outlet_p));
                if (!saturation.ok())
                    return property_failure(saturation);
                const double target_enthalpy =
                    saturation.liquid.enthalpy_j_kg +
                    target_quality *
                        (saturation.vapor.enthalpy_j_kg -
                         saturation.liquid.enthalpy_j_kg);
                residual = x.at(outlet_h) - target_enthalpy;
                return EvaluationStatus::success();
            },
            1.0e5);
        system.add_sparse_equation(
            prefix + "heat_balance",
            {heat_flow, inlet_m, inlet_h, outlet_h},
            [evaporator = evaporator_, heat_flow, inlet_m,
             inlet_h, outlet_h](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                const double direction = evaporator ? 1.0 : -1.0;
                const double delta_h =
                    x.at(outlet_h) - x.at(inlet_h);
                jacobian.push_back({heat_flow, 1.0});
                jacobian.push_back(
                    {inlet_m, -direction * delta_h});
                jacobian.push_back(
                    {inlet_h, direction * x.at(inlet_m)});
                jacobian.push_back(
                    {outlet_h, -direction * x.at(inlet_m)});
                return x.at(heat_flow) -
                       direction * x.at(inlet_m) * delta_h;
            },
            1.0e7);
        system.add_checked_equation(
            prefix + "heat_temperature",
            [properties, outlet_p, heat_temperature](
                const std::vector<double>& x, double& residual) {
                const auto saturation =
                    properties->saturation_p(x.at(outlet_p));
                if (!saturation.ok())
                    return property_failure(saturation);
                residual = x.at(heat_temperature) -
                           saturation.liquid.temperature_k;
                return EvaluationStatus::success();
            },
            100.0);
    }

private:
    ComponentModelDescriptor descriptor_;
    bool evaporator_{false};
};

}  // namespace

void register_heat_transfer_component_models(
    ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<FixedDutyHeatExchangerModel>());
    registry.register_model(
        std::make_shared<CounterflowUaHeatExchangerModel>());
    registry.register_model(
        std::make_shared<MaterialFluidHeatExchangerModel>(
            MaterialFluidExchangerMode::fixed_duty));
    registry.register_model(
        std::make_shared<MaterialFluidHeatExchangerModel>(
            MaterialFluidExchangerMode::energy_balance));
    registry.register_model(
        std::make_shared<MaterialFluidHeatExchangerModel>(
            MaterialFluidExchangerMode::counterflow_ua));
    registry.register_model(
        std::make_shared<PrescribedDutyMaterialConditionerModel>(true));
    registry.register_model(
        std::make_shared<PrescribedDutyMaterialConditionerModel>(false));
    registry.register_model(
        std::make_shared<FixedOutletQualityPhaseChangeModel>(
            "evaporator.fluid.fixed_outlet_quality", true));
    registry.register_model(
        std::make_shared<FixedOutletQualityPhaseChangeModel>(
            "condenser.fluid.fixed_outlet_quality", false));
}

}  // namespace thermox::platform
