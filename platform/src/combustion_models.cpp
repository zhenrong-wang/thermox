#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace thermox::platform {

namespace {

using component_model_support::require_port_species;
using component_model_support::require_port_variable;
using component_model_support::require_thermochemistry_package;
using component_model_support::required_parameter;

struct EquilibriumEvaluation {
    EvaluationStatus status{EvaluationStatus::fatal(
        "equilibrium evaluation has not run")};
    physics::ThermochemicalState state;
    double inlet_mass_flow{0.0};
};

class EquilibriumCache {
public:
    EquilibriumCache(
        std::shared_ptr<const physics::ThermochemistryPackage>
            package,
        std::vector<std::string> species,
        std::vector<std::size_t> air_flow,
        std::vector<std::size_t> fuel_flow,
        std::size_t air_enthalpy,
        std::size_t fuel_enthalpy,
        std::size_t outlet_pressure,
        double fuel_heat_loss_j_kg)
        : package_(std::move(package)),
          species_(std::move(species)),
          air_flow_(std::move(air_flow)),
          fuel_flow_(std::move(fuel_flow)),
          air_enthalpy_(air_enthalpy),
          fuel_enthalpy_(fuel_enthalpy),
          outlet_pressure_(outlet_pressure),
          fuel_heat_loss_j_kg_(fuel_heat_loss_j_kg) {}

    EquilibriumEvaluation evaluate(
        const std::vector<double>& x,
        double continuation_parameter = 1.0) const {
        std::scoped_lock lock(mutex_);
        std::vector<double> key;
        key.reserve(
            air_flow_.size() + fuel_flow_.size() + 4);
        for (const auto variable : air_flow_) {
            key.push_back(x.at(variable));
        }
        for (const auto variable : fuel_flow_) {
            key.push_back(x.at(variable));
        }
        key.push_back(x.at(air_enthalpy_));
        key.push_back(x.at(fuel_enthalpy_));
        key.push_back(x.at(outlet_pressure_));
        key.push_back(continuation_parameter);
        if (key == last_key_) {
            return last_;
        }
        last_key_ = std::move(key);
        std::vector<double> mass_fractions(species_.size(), 0.0);
        double air_mass = 0.0;
        double fuel_mass = 0.0;
        const bool target_problem =
            continuation_parameter >= 1.0;
        const double intermediate_flow_floor =
            std::max(
                (1.0 - continuation_parameter) * 1.0e-12,
                std::numeric_limits<double>::min());
        for (std::size_t index = 0; index < species_.size();
             ++index) {
            const double raw_air =
                x.at(air_flow_.at(index));
            const double raw_fuel =
                x.at(fuel_flow_.at(index));
            if (!std::isfinite(raw_air) ||
                !std::isfinite(raw_fuel)) {
                last_ = {
                    EvaluationStatus::recoverable(
                        "combustor inlet species mass flows must "
                        "be finite"),
                    {}, 0.0};
                return last_;
            }
            const double air = target_problem
                ? raw_air
                : std::max(
                      raw_air, intermediate_flow_floor);
            const double fuel = target_problem
                ? raw_fuel
                : std::max(
                      raw_fuel, intermediate_flow_floor);
            if (air < 0.0 || fuel < 0.0) {
                last_ = {
                    EvaluationStatus::recoverable(
                        "combustor inlet species mass flows must "
                        "be nonnegative"),
                    {}, 0.0};
                return last_;
            }
            mass_fractions[index] = air + fuel;
            air_mass += air;
            fuel_mass += fuel;
        }
        const double total_mass = air_mass + fuel_mass;
        if (total_mass <= 0.0) {
            last_ = {
                EvaluationStatus::recoverable(
                    "combustor total inlet mass flow must be "
                    "positive"),
                {}, 0.0};
            return last_;
        }
        for (double& value : mass_fractions) {
            value /= total_mass;
        }
        const double mixed_enthalpy =
            (air_mass * x.at(air_enthalpy_) +
             fuel_mass * x.at(fuel_enthalpy_) -
             fuel_mass * fuel_heat_loss_j_kg_) /
            total_mass;
        const double outlet_pressure = target_problem
            ? x.at(outlet_pressure_)
            : std::max(x.at(outlet_pressure_), 1.0);
        const auto result = package_->equilibrate_hp(
            outlet_pressure, mixed_enthalpy,
            physics::SpeciesComposition{
                physics::CompositionBasis::mass_fraction,
                species_, std::move(mass_fractions)});
        if (!result.ok()) {
            const auto status =
                result.status == physics::PropertyStatus::backend_error
                    ? EvaluationStatus::fatal(result.message)
                    : EvaluationStatus::recoverable(result.message);
            last_ = {status, {}, total_mass};
            return last_;
        }
        last_ = {
            EvaluationStatus::success(), result.state, total_mass};
        return last_;
    }

private:
    std::shared_ptr<const physics::ThermochemistryPackage> package_;
    std::vector<std::string> species_;
    std::vector<std::size_t> air_flow_;
    std::vector<std::size_t> fuel_flow_;
    std::size_t air_enthalpy_;
    std::size_t fuel_enthalpy_;
    std::size_t outlet_pressure_;
    double fuel_heat_loss_j_kg_{0.0};
    mutable std::mutex mutex_;
    mutable std::vector<double> last_key_;
    mutable EquilibriumEvaluation last_;
};

class EquilibriumCombustorModel final
    : public ComponentModel {
public:
    explicit EquilibriumCombustorModel(
        bool heat_release_efficiency = false)
        : heat_release_efficiency_(heat_release_efficiency) {
        descriptor_.kind = heat_release_efficiency_
            ? "combustor.material.equilibrium_heat_release_efficiency"
            : "combustor.material.adiabatic_equilibrium";
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "combustor.material";
        descriptor_.display_name = heat_release_efficiency_
            ? "Equilibrium combustor (heat-release efficiency)"
            : "Adiabatic equilibrium combustor";
        descriptor_.category = "Combustion";
        descriptor_.model_name = heat_release_efficiency_
            ? "Equilibrium combustor with declared fuel heat loss"
            : "Adiabatic equilibrium combustor";
        descriptor_.ports = {
            {"air_inlet", "material", "in"},
            {"fuel_inlet", "material", "in"},
            {"outlet", "material", "out"}};
        descriptor_.parameters = {
            {"pressure_ratio", "dimensionless", true,
             std::nullopt, 0.0, 1.0, false, true}};
        if (heat_release_efficiency_) {
            descriptor_.parameters.push_back(
                {"combustion_efficiency", "dimensionless", true,
                 std::nullopt, 0.0, 1.0, false, true});
            descriptor_.parameters.push_back(
                {"fuel_lower_heating_value", "specific_enthalpy", true,
                 std::nullopt, 0.0,
                 std::numeric_limits<double>::infinity(), false, true});
        }
        descriptor_.required_thermochemistry_capabilities = {
            physics::ThermochemistryCapability::equilibrium_hp};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto species =
            require_port_species(context, "air_inlet");
        if (species != require_port_species(
                           context, "fuel_inlet") ||
            species != require_port_species(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' combustor ports must share a species basis");
        }
        const auto package = require_thermochemistry_package(
            context, "outlet");
        if (species != package->species_basis()) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' combustor material must declare the complete "
                "thermochemistry species basis in backend order");
        }
        const auto same_package = [&](const std::string& port) {
            const auto candidate =
                require_thermochemistry_package(context, port);
            return candidate->name() == package->name() &&
                candidate->version() == package->version() &&
                candidate->mechanism() == package->mechanism() &&
                candidate->phase() == package->phase();
        };
        if (!same_package("air_inlet") ||
            !same_package("fuel_inlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' combustor ports must resolve the same "
                "thermochemistry package");
        }

        std::vector<std::size_t> air_flow;
        std::vector<std::size_t> fuel_flow;
        std::vector<std::size_t> outlet_flow;
        for (const auto& name : species) {
            const auto variable = "m_dot[" + name + "]";
            air_flow.push_back(require_port_variable(
                context, "air_inlet." + variable));
            fuel_flow.push_back(require_port_variable(
                context, "fuel_inlet." + variable));
            outlet_flow.push_back(require_port_variable(
                context, "outlet." + variable));
        }
        const auto air_p =
            require_port_variable(context, "air_inlet.p");
        const auto fuel_p =
            require_port_variable(context, "fuel_inlet.p");
        const auto outlet_p =
            require_port_variable(context, "outlet.p");
        const auto outlet_h =
            require_port_variable(context, "outlet.h");
        const double pressure_ratio =
            required_parameter(
                context.component, "pressure_ratio");
        double fuel_heat_loss_j_kg = 0.0;
        if (heat_release_efficiency_) {
            const double efficiency = required_parameter(
                context.component, "combustion_efficiency");
            const double lower_heating_value = required_parameter(
                context.component, "fuel_lower_heating_value");
            fuel_heat_loss_j_kg =
                (1.0 - efficiency) * lower_heating_value;
        }
        const auto cache = std::make_shared<EquilibriumCache>(
            package, species, air_flow, fuel_flow,
            require_port_variable(context, "air_inlet.h"),
            require_port_variable(context, "fuel_inlet.h"),
            outlet_p, fuel_heat_loss_j_kg);
        const std::string prefix =
            "component." + context.component.id + ".";
        system.add_linear_equation(
            prefix + "fuel_inlet_pressure",
            {{fuel_p, 1.0}, {air_p, -1.0}}, 0.0, 100000.0);
        system.add_continuation_linear_equation(
            prefix + "pressure_ratio",
            {{outlet_p, 1.0},
             {air_p, -pressure_ratio}},
            0.0,
            [air_p, outlet_p, pressure_ratio](
                const std::vector<double>& x,
                const std::vector<double>& anchor,
                double continuation_parameter,
                std::vector<EquationPartial>& jacobian) {
                double anchor_pressure_ratio =
                    anchor.at(outlet_p) /
                    anchor.at(air_p);
                if (!std::isfinite(anchor_pressure_ratio) ||
                    anchor_pressure_ratio <= 0.0) {
                    anchor_pressure_ratio = 1.0;
                }
                const double staged_pressure_ratio =
                    anchor_pressure_ratio +
                    continuation_parameter *
                        (pressure_ratio -
                         anchor_pressure_ratio);
                jacobian.push_back({outlet_p, 1.0});
                jacobian.push_back(
                    {air_p, -staged_pressure_ratio});
                return x.at(outlet_p) -
                    staged_pressure_ratio * x.at(air_p);
            },
            100000.0);
        system.add_continuation_checked_equation(
            prefix + "adiabatic_enthalpy",
            [cache, outlet_h](
                const std::vector<double>& x,
                const std::vector<double>& anchor,
                double continuation_parameter,
                double& residual) {
                const auto equilibrium = cache->evaluate(
                    x, continuation_parameter);
                if (!equilibrium.status.ok()) {
                    return equilibrium.status;
                }
                const double staged_enthalpy =
                    anchor.at(outlet_h) +
                    continuation_parameter *
                        (equilibrium.state.thermodynamic
                             .enthalpy_j_kg -
                         anchor.at(outlet_h));
                residual =
                    x.at(outlet_h) - staged_enthalpy;
                return EvaluationStatus::success();
            },
            1000000.0);
        for (std::size_t index = 0; index < species.size();
             ++index) {
            system.add_continuation_checked_equation(
                prefix + "equilibrium_species." +
                    species.at(index),
                [cache, outlet = outlet_flow.at(index), index](
                    const std::vector<double>& x,
                    const std::vector<double>& anchor,
                    double continuation_parameter,
                    double& residual) {
                    const auto equilibrium = cache->evaluate(
                        x, continuation_parameter);
                    if (!equilibrium.status.ok()) {
                        return equilibrium.status;
                    }
                    const double equilibrium_flow =
                        equilibrium.inlet_mass_flow *
                            equilibrium.state.composition
                                .fractions()
                                .at(index);
                    const double staged_flow =
                        anchor.at(outlet) +
                        continuation_parameter *
                            (equilibrium_flow -
                             anchor.at(outlet));
                    residual =
                        x.at(outlet) - staged_flow;
                    return EvaluationStatus::success();
                },
                100.0);
        }
    }

private:
    ComponentModelDescriptor descriptor_;
    bool heat_release_efficiency_{false};
};

}  // namespace

void register_combustion_component_models(
    ComponentRegistry& registry) {
    registry.register_model(std::make_shared<
        EquilibriumCombustorModel>());
    registry.register_model(std::make_shared<
        EquilibriumCombustorModel>(true));
}

}  // namespace thermox::platform
