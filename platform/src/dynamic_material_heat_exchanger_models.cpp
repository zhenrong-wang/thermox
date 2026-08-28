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

using component_model_support::property_failure;
using component_model_support::require_internal_variable;
using component_model_support::require_port_species;
using component_model_support::require_port_variable;
using component_model_support::require_property_package;
using component_model_support::require_thermochemistry_package;
using component_model_support::required_parameter;

EvaluationStatus thermochemistry_failure(
    const physics::ThermochemicalResult& result) {
    if (result.status == physics::PropertyStatus::backend_error) {
        return EvaluationStatus::fatal(result.message);
    }
    return EvaluationStatus::recoverable(result.message);
}

double total_flow(
    const std::vector<double>& values,
    const std::vector<std::size_t>& flows) {
    double total = 0.0;
    for (const auto flow : flows) total += values.at(flow);
    return total;
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
                "material-fluid cell species flows must be finite and nonnegative");
        }
        fractions.push_back(value);
        total += value;
    }
    if (!std::isfinite(total) || total <= 0.0) {
        throw std::domain_error(
            "material-fluid cell total hot-gas flow must be positive");
    }
    for (double& fraction : fractions) fraction /= total;
    return {physics::CompositionBasis::mass_fraction,
            species, std::move(fractions)};
}

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
            jacobian.insert(jacobian.end(),
                            exact_rate_partials.begin(),
                            exact_rate_partials.end());
            return EvaluationStatus::success();
        },
        scale);
}

class DynamicMaterialFluidHeatExchangerCellModel final
    : public ComponentModel {
public:
    DynamicMaterialFluidHeatExchangerCellModel() {
        descriptor_.kind =
            "heat_exchanger.material_fluid.dynamic_cell";
        descriptor_.version = "1.0.0";
        descriptor_.template_kind =
            "heat_exchanger.material_fluid.cell";
        descriptor_.display_name =
            "Dynamic gas-to-fluid heat-exchanger cell";
        descriptor_.category = "Heat transfer";
        descriptor_.model_name =
            "Quasi-steady material gas with fluid and wall capacitance";
        descriptor_.ports = {
            {"hot_in", "material", "in"},
            {"hot_out", "material", "out"},
            {"cold_in", "fluid", "in"},
            {"cold_out", "fluid", "out"},
            {"cold_inventory", "inventory", "out"}};
        descriptor_.parameters = {
            {"cold_fluid_mass", "mass", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"wall_thermal_capacity", "thermal_capacity", true,
             std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"hot_side_UA", "thermal_conductance", true,
             std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"cold_side_UA", "thermal_conductance", true,
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
        descriptor_.required_thermochemistry_capabilities = {
            physics::ThermochemistryCapability::state_ph};
        descriptor_.required_property_capabilities = {
            physics::PropertyCapability::state_ph};
        descriptor_.supports_steady = true;
        descriptor_.supports_transient = true;
        descriptor_.internal_variables = {
            {"cold_total_energy", DaeVariableKind::differential,
             5.0e6, 1.0e7, 0.0, 1.0e6,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(), "energy"},
            {"cold_enthalpy", DaeVariableKind::algebraic,
             300000.0, 100000.0, 0.0, 100000.0,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(),
             "specific_enthalpy"},
            {"wall_temperature", DaeVariableKind::differential,
             500.0, 100.0, 0.0, 10.0, 0.0,
             std::numeric_limits<double>::infinity(), "temperature"}};
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
        for (std::size_t i = 0; i < data.species.size(); ++i) {
            system.add_linear_equation(
                prefix + "hot_species_continuity." + data.species[i],
                {{data.hot_out_flows[i], 1.0},
                 {data.hot_in_flows[i], -1.0}},
                0.0, 100.0);
        }
        system.add_linear_equation(
            prefix + "cold_mass_continuity",
            {{data.cold_out_m, 1.0}, {data.cold_in_m, -1.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "cold_inventory",
            {{data.cold_inventory, 1.0}}, data.cold_mass, 10.0);
        add_steady_hot_pressure_loss(
            system, prefix + "hot_pressure_loss", data);
        add_steady_cold_pressure_loss(
            system, prefix + "cold_pressure_loss", data);
        add_steady_energy_balance(
            system, prefix + "energy_balance", data);
        add_steady_heat_transfer(
            system, prefix + "mixed_cell_heat_transfer", data);
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        auto data = compile_data(context);
        data.cold_energy = require_internal_variable(
            context, "cold_total_energy");
        data.cold_enthalpy = require_internal_variable(
            context, "cold_enthalpy");
        data.wall_temperature = require_internal_variable(
            context, "wall_temperature");
        const std::string prefix =
            "component." + context.component.id + ".";
        for (std::size_t i = 0; i < data.species.size(); ++i) {
            system.add_linear_equation(
                prefix + "hot_species_continuity." + data.species[i],
                {{data.hot_out_flows[i], 1.0, 0.0},
                 {data.hot_in_flows[i], -1.0, 0.0}},
                0.0, 100.0);
        }
        system.add_linear_equation(
            prefix + "cold_mass_continuity",
            {{data.cold_out_m, 1.0, 0.0},
             {data.cold_in_m, -1.0, 0.0}},
            0.0, 100.0);
        system.add_linear_equation(
            prefix + "cold_inventory",
            {{data.cold_inventory, 1.0, 0.0}},
            data.cold_mass, 10.0);
        add_transient_hot_pressure_loss(
            system, prefix + "hot_pressure_loss", data);
        add_transient_cold_pressure_loss(
            system, prefix + "cold_pressure_loss", data);
        add_transient_hot_energy(
            system, prefix + "hot_energy", data);
        add_transient_cold_energy(
            system, prefix + "cold_energy_accumulation", data);
        add_transient_wall_energy(
            system, prefix + "wall_energy_accumulation", data);
        system.add_linear_equation(
            prefix + "cold_outlet_enthalpy",
            {{data.cold_out_h, 1.0, 0.0},
             {data.cold_enthalpy, -1.0, 0.0}},
            0.0, 100000.0);
        add_cold_energy_closure(
            system, prefix + "cold_energy_closure", data);
    }

private:
    struct Data {
        std::vector<std::string> species;
        std::vector<std::size_t> hot_in_flows;
        std::vector<std::size_t> hot_out_flows;
        std::shared_ptr<const physics::ThermochemistryPackage> hot;
        std::shared_ptr<const physics::PropertyPackage> cold;
        std::size_t hot_in_p{}, hot_in_h{}, hot_out_p{}, hot_out_h{};
        std::size_t cold_in_m{}, cold_in_p{}, cold_in_h{};
        std::size_t cold_out_m{}, cold_out_p{}, cold_out_h{};
        std::size_t cold_inventory{};
        std::size_t cold_energy{}, cold_enthalpy{}, wall_temperature{};
        double cold_mass{}, wall_capacity{}, hot_ua{}, cold_ua{};
        double hot_loss_scale{}, cold_loss_scale{};
    };

    Data compile_data(
        const ComponentCompileContext& context) const {
        Data data;
        data.species = require_port_species(context, "hot_in");
        if (data.species != require_port_species(context, "hot_out")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' material ports must use the same species basis");
        }
        data.hot = require_thermochemistry_package(context, "hot_in");
        const auto hot_out =
            require_thermochemistry_package(context, "hot_out");
        if (data.hot->name() != hot_out->name() ||
            data.hot->version() != hot_out->version() ||
            data.hot->mechanism() != hot_out->mechanism() ||
            data.hot->phase() != hot_out->phase()) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' material ports must resolve the same thermochemistry package");
        }
        data.cold = require_property_package(context, "cold_in");
        if (data.cold != require_property_package(context, "cold_out")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' fluid ports must use the same medium");
        }
        for (const auto& species : data.species) {
            const std::string variable = "m_dot[" + species + "]";
            data.hot_in_flows.push_back(require_port_variable(
                context, "hot_in." + variable));
            data.hot_out_flows.push_back(require_port_variable(
                context, "hot_out." + variable));
        }
        data.hot_in_p = require_port_variable(context, "hot_in.p");
        data.hot_in_h = require_port_variable(context, "hot_in.h");
        data.hot_out_p = require_port_variable(context, "hot_out.p");
        data.hot_out_h = require_port_variable(context, "hot_out.h");
        data.cold_in_m = require_port_variable(context, "cold_in.m_dot");
        data.cold_in_p = require_port_variable(context, "cold_in.p");
        data.cold_in_h = require_port_variable(context, "cold_in.h");
        data.cold_out_m = require_port_variable(context, "cold_out.m_dot");
        data.cold_out_p = require_port_variable(context, "cold_out.p");
        data.cold_out_h = require_port_variable(context, "cold_out.h");
        data.cold_inventory = require_port_variable(
            context, "cold_inventory.mass");
        data.cold_mass = required_parameter(
            context.component, "cold_fluid_mass");
        data.wall_capacity = required_parameter(
            context.component, "wall_thermal_capacity");
        data.hot_ua = required_parameter(context.component, "hot_side_UA");
        data.cold_ua = required_parameter(context.component, "cold_side_UA");
        const double hot_diameter = required_parameter(
            context.component, "hot_flow_diameter");
        const double cold_diameter = required_parameter(
            context.component, "cold_flow_diameter");
        const double hot_area = std::numbers::pi *
            hot_diameter * hot_diameter / 4.0;
        const double cold_area = std::numbers::pi *
            cold_diameter * cold_diameter / 4.0;
        data.hot_loss_scale = required_parameter(
            context.component, "hot_loss_coefficient") /
            (2.0 * hot_area * hot_area);
        data.cold_loss_scale = required_parameter(
            context.component, "cold_loss_coefficient") /
            (2.0 * cold_area * cold_area);
        return data;
    }

    static EvaluationStatus hot_state(
        const Data& data,
        const std::vector<double>& x,
        double pressure,
        double enthalpy,
        physics::ThermochemicalResult& result) {
        try {
            result = data.hot->state_ph(
                pressure, enthalpy,
                material_composition(
                    x, data.species, data.hot_in_flows));
        } catch (const std::exception& ex) {
            return EvaluationStatus::recoverable(ex.what());
        }
        return result.ok()
            ? EvaluationStatus::success()
            : thermochemistry_failure(result);
    }

    static void add_steady_hot_pressure_loss(
        EquationSystemBuilder& system,
        const std::string& name,
        const Data& data) {
        system.add_checked_equation(
            name, [data](const std::vector<double>& x, double& residual) {
                physics::ThermochemicalResult state;
                auto status = hot_state(
                    data, x, x.at(data.hot_out_p),
                    x.at(data.hot_out_h), state);
                if (!status.ok()) return status;
                const double rho = state.state.thermodynamic.density_kg_m3;
                if (!std::isfinite(rho) || rho <= 0.0) {
                    return EvaluationStatus::recoverable(
                        "material-fluid cell hot pressure loss requires positive density");
                }
                const double flow = total_flow(x, data.hot_in_flows);
                residual = x.at(data.hot_in_p) - x.at(data.hot_out_p) -
                    data.hot_loss_scale * flow * std::abs(flow) / rho;
                return EvaluationStatus::success();
            }, 100000.0);
    }

    static void add_steady_cold_pressure_loss(
        EquationSystemBuilder& system,
        const std::string& name,
        const Data& data) {
        system.add_checked_equation(
            name, [data](const std::vector<double>& x, double& residual) {
                const auto state = data.cold->state_ph(
                    x.at(data.cold_in_p), x.at(data.cold_out_h));
                if (!state.ok()) return property_failure(state);
                const double flow = x.at(data.cold_in_m);
                residual = x.at(data.cold_in_p) - x.at(data.cold_out_p) -
                    data.cold_loss_scale * flow * std::abs(flow) /
                        state.state.density_kg_m3;
                return EvaluationStatus::success();
            }, 100000.0);
    }

    static void add_steady_energy_balance(
        EquationSystemBuilder& system,
        const std::string& name,
        const Data& data) {
        system.add_equation(
            name, [data](const std::vector<double>& x) {
                const double hot_flow = total_flow(x, data.hot_in_flows);
                return hot_flow *
                           (x.at(data.hot_in_h) - x.at(data.hot_out_h)) -
                       x.at(data.cold_in_m) *
                           (x.at(data.cold_out_h) - x.at(data.cold_in_h));
            }, 1.0e6);
    }

    static void add_steady_heat_transfer(
        EquationSystemBuilder& system,
        const std::string& name,
        const Data& data) {
        system.add_checked_equation(
            name, [data](const std::vector<double>& x, double& residual) {
                physics::ThermochemicalResult hot;
                auto status = hot_state(
                    data, x, x.at(data.hot_out_p),
                    x.at(data.hot_out_h), hot);
                if (!status.ok()) return status;
                const auto cold = data.cold->state_ph(
                    x.at(data.cold_in_p), x.at(data.cold_out_h));
                if (!cold.ok()) return property_failure(cold);
                const double difference =
                    hot.state.thermodynamic.temperature_k -
                    cold.state.temperature_k;
                if (!std::isfinite(difference) || difference <= 0.0) {
                    return EvaluationStatus::recoverable(
                        "material-fluid cell requires hot bulk temperature above cold bulk temperature");
                }
                const double effective_ua = data.hot_ua * data.cold_ua /
                    (data.hot_ua + data.cold_ua);
                residual = total_flow(x, data.hot_in_flows) *
                               (x.at(data.hot_in_h) - x.at(data.hot_out_h)) -
                           effective_ua * difference;
                return EvaluationStatus::success();
            }, 1.0e6);
    }

    static std::vector<std::size_t> hot_variables(
        const Data& data) {
        std::vector<std::size_t> variables = data.hot_in_flows;
        variables.insert(variables.end(),
                         {data.hot_in_p, data.hot_in_h,
                          data.hot_out_p, data.hot_out_h});
        return variables;
    }

    static void add_transient_hot_pressure_loss(
        DaeEquationSystemBuilder& system,
        const std::string& name,
        const Data& data) {
        auto variables = hot_variables(data);
        add_numeric_sparse_dae_equation(
            system, name, std::move(variables), {},
            [data](const std::vector<double>& x,
                   const std::vector<double>&, double& residual) {
                physics::ThermochemicalResult state;
                auto status = hot_state(
                    data, x, x.at(data.hot_out_p),
                    x.at(data.hot_out_h), state);
                if (!status.ok()) return status;
                const double rho = state.state.thermodynamic.density_kg_m3;
                if (!std::isfinite(rho) || rho <= 0.0) {
                    return EvaluationStatus::recoverable(
                        "material-fluid cell hot pressure loss requires positive density");
                }
                const double flow = total_flow(x, data.hot_in_flows);
                residual = x.at(data.hot_in_p) - x.at(data.hot_out_p) -
                    data.hot_loss_scale * flow * std::abs(flow) / rho;
                return EvaluationStatus::success();
            }, 100000.0);
    }

    static void add_transient_cold_pressure_loss(
        DaeEquationSystemBuilder& system,
        const std::string& name,
        const Data& data) {
        system.add_sparse_equation(
            name,
            {data.cold_in_m, data.cold_in_p,
             data.cold_enthalpy, data.cold_out_p},
            [data](double, const std::vector<double>& x,
                   const std::vector<double>&, double& residual,
                   std::vector<DaeEquationPartial>& jacobian) {
                const auto state =
                    physics::state_ph_derivatives_with_fallback(
                        *data.cold, x.at(data.cold_in_p),
                        x.at(data.cold_enthalpy));
                if (!state.ok()) return property_failure(state);
                const double rho = state.state.density_kg_m3;
                const double flow = x.at(data.cold_in_m);
                const double square = flow * std::abs(flow);
                const double factor =
                    data.cold_loss_scale * square / (rho * rho);
                residual = x.at(data.cold_in_p) -
                    x.at(data.cold_out_p) -
                    data.cold_loss_scale * square / rho;
                jacobian.push_back(
                    {data.cold_in_m,
                     -2.0 * data.cold_loss_scale *
                         std::abs(flow) / rho, 0.0});
                jacobian.push_back(
                    {data.cold_in_p,
                     1.0 + factor * state.derivatives
                         .density_wrt_pressure_at_enthalpy, 0.0});
                jacobian.push_back(
                    {data.cold_enthalpy,
                     factor * state.derivatives
                         .density_wrt_enthalpy_at_pressure, 0.0});
                jacobian.push_back({data.cold_out_p, -1.0, 0.0});
                return EvaluationStatus::success();
            }, 100000.0);
    }

    static void add_transient_hot_energy(
        DaeEquationSystemBuilder& system,
        const std::string& name,
        const Data& data) {
        auto variables = hot_variables(data);
        variables.push_back(data.wall_temperature);
        add_numeric_sparse_dae_equation(
            system, name, std::move(variables), {},
            [data](const std::vector<double>& x,
                   const std::vector<double>&, double& residual) {
                physics::ThermochemicalResult hot;
                auto status = hot_state(
                    data, x, x.at(data.hot_out_p),
                    x.at(data.hot_out_h), hot);
                if (!status.ok()) return status;
                const double heat = data.hot_ua *
                    (hot.state.thermodynamic.temperature_k -
                     x.at(data.wall_temperature));
                residual = total_flow(x, data.hot_in_flows) *
                               (x.at(data.hot_in_h) - x.at(data.hot_out_h)) -
                           heat;
                return EvaluationStatus::success();
            }, 1.0e6);
    }

    static void add_transient_cold_energy(
        DaeEquationSystemBuilder& system,
        const std::string& name,
        const Data& data) {
        system.add_sparse_equation(
            name,
            {data.cold_energy, data.cold_in_m, data.cold_in_h,
             data.cold_out_m, data.cold_in_p,
             data.cold_enthalpy, data.wall_temperature},
            [data](double, const std::vector<double>& x,
                   const std::vector<double>& x_dot, double& residual,
                   std::vector<DaeEquationPartial>& jacobian) {
                const auto state =
                    physics::state_ph_derivatives_with_fallback(
                        *data.cold, x.at(data.cold_in_p),
                        x.at(data.cold_enthalpy));
                if (!state.ok()) return property_failure(state);
                const double heat = data.cold_ua *
                    (state.state.temperature_k -
                     x.at(data.wall_temperature));
                residual = x_dot.at(data.cold_energy) -
                    x.at(data.cold_in_m) * x.at(data.cold_in_h) +
                    x.at(data.cold_out_m) * x.at(data.cold_enthalpy) +
                    heat;
                jacobian.push_back({data.cold_energy, 0.0, 1.0});
                jacobian.push_back(
                    {data.cold_in_m, -x.at(data.cold_in_h), 0.0});
                jacobian.push_back(
                    {data.cold_in_h, -x.at(data.cold_in_m), 0.0});
                jacobian.push_back(
                    {data.cold_out_m, x.at(data.cold_enthalpy), 0.0});
                jacobian.push_back(
                    {data.cold_in_p,
                     data.cold_ua * state.derivatives
                         .temperature_wrt_pressure_at_enthalpy, 0.0});
                jacobian.push_back(
                    {data.cold_enthalpy,
                     x.at(data.cold_out_m) +
                     data.cold_ua * state.derivatives
                         .temperature_wrt_enthalpy_at_pressure, 0.0});
                jacobian.push_back(
                    {data.wall_temperature, -data.cold_ua, 0.0});
                return EvaluationStatus::success();
            }, 1.0e6);
    }

    static void add_transient_wall_energy(
        DaeEquationSystemBuilder& system,
        const std::string& name,
        const Data& data) {
        auto variables = hot_variables(data);
        variables.insert(variables.end(),
                         {data.cold_in_p, data.cold_enthalpy,
                          data.wall_temperature});
        add_numeric_sparse_dae_equation(
            system, name, std::move(variables),
            {{data.wall_temperature, 0.0, data.wall_capacity}},
            [data](const std::vector<double>& x,
                   const std::vector<double>& x_dot, double& residual) {
                physics::ThermochemicalResult hot;
                auto status = hot_state(
                    data, x, x.at(data.hot_out_p),
                    x.at(data.hot_out_h), hot);
                if (!status.ok()) return status;
                const auto cold = data.cold->state_ph(
                    x.at(data.cold_in_p), x.at(data.cold_enthalpy));
                if (!cold.ok()) return property_failure(cold);
                const double hot_heat = data.hot_ua *
                    (hot.state.thermodynamic.temperature_k -
                     x.at(data.wall_temperature));
                const double cold_heat = data.cold_ua *
                    (x.at(data.wall_temperature) -
                     cold.state.temperature_k);
                residual = data.wall_capacity *
                               x_dot.at(data.wall_temperature) -
                           hot_heat + cold_heat;
                return EvaluationStatus::success();
            }, std::max(data.wall_capacity, 1.0));
    }

    static void add_cold_energy_closure(
        DaeEquationSystemBuilder& system,
        const std::string& name,
        const Data& data) {
        system.add_sparse_equation(
            name,
            {data.cold_energy, data.cold_in_p, data.cold_enthalpy},
            [data](double, const std::vector<double>& x,
                   const std::vector<double>&, double& residual,
                   std::vector<DaeEquationPartial>& jacobian) {
                const auto state =
                    physics::state_ph_derivatives_with_fallback(
                        *data.cold, x.at(data.cold_in_p),
                        x.at(data.cold_enthalpy));
                if (!state.ok()) return property_failure(state);
                residual = x.at(data.cold_energy) -
                    data.cold_mass * state.state.internal_energy_j_kg;
                jacobian.push_back({data.cold_energy, 1.0, 0.0});
                jacobian.push_back(
                    {data.cold_in_p,
                     -data.cold_mass * state.derivatives
                         .internal_energy_wrt_pressure_at_enthalpy, 0.0});
                jacobian.push_back(
                    {data.cold_enthalpy,
                     -data.cold_mass * state.derivatives
                         .internal_energy_wrt_enthalpy_at_pressure, 0.0});
                return EvaluationStatus::success();
            }, 1.0e7);
    }

    ComponentModelDescriptor descriptor_;
};

}  // namespace

void register_dynamic_material_heat_transfer_component_models(
    ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<DynamicMaterialFluidHeatExchangerCellModel>());
}

}  // namespace thermox::platform
