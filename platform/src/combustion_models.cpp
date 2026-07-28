#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <algorithm>
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
        std::size_t outlet_pressure)
        : package_(std::move(package)),
          species_(std::move(species)),
          air_flow_(std::move(air_flow)),
          fuel_flow_(std::move(fuel_flow)),
          air_enthalpy_(air_enthalpy),
          fuel_enthalpy_(fuel_enthalpy),
          outlet_pressure_(outlet_pressure) {}

    EquilibriumEvaluation evaluate(
        const std::vector<double>& x) const {
        std::scoped_lock lock(mutex_);
        std::vector<double> key;
        key.reserve(
            air_flow_.size() + fuel_flow_.size() + 3);
        for (const auto variable : air_flow_) {
            key.push_back(x.at(variable));
        }
        for (const auto variable : fuel_flow_) {
            key.push_back(x.at(variable));
        }
        key.push_back(x.at(air_enthalpy_));
        key.push_back(x.at(fuel_enthalpy_));
        key.push_back(x.at(outlet_pressure_));
        if (key == last_key_) {
            return last_;
        }
        last_key_ = std::move(key);
        std::vector<double> mass_fractions(species_.size(), 0.0);
        double air_mass = 0.0;
        double fuel_mass = 0.0;
        for (std::size_t index = 0; index < species_.size();
             ++index) {
            const double air = x.at(air_flow_.at(index));
            const double fuel = x.at(fuel_flow_.at(index));
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
             fuel_mass * x.at(fuel_enthalpy_)) /
            total_mass;
        const auto result = package_->equilibrate_hp(
            x.at(outlet_pressure_), mixed_enthalpy,
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
    mutable std::mutex mutex_;
    mutable std::vector<double> last_key_;
    mutable EquilibriumEvaluation last_;
};

class AdiabaticEquilibriumCombustorModel final
    : public ComponentModel {
public:
    AdiabaticEquilibriumCombustorModel() {
        descriptor_.kind =
            "combustor.material.adiabatic_equilibrium";
        descriptor_.version = "1.0.0";
        descriptor_.ports = {
            {"air_inlet", "material", "in"},
            {"fuel_inlet", "material", "in"},
            {"outlet", "material", "out"}};
        descriptor_.parameters = {
            {"pressure_ratio", "dimensionless", true,
             std::nullopt, 0.0, 1.0, false, true}};
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
        const auto cache = std::make_shared<EquilibriumCache>(
            package, species, air_flow, fuel_flow,
            require_port_variable(context, "air_inlet.h"),
            require_port_variable(context, "fuel_inlet.h"),
            outlet_p);
        const std::string prefix =
            "component." + context.component.id + ".";
        system.add_linear_equation(
            prefix + "fuel_inlet_pressure",
            {{fuel_p, 1.0}, {air_p, -1.0}}, 0.0, 100000.0);
        system.add_linear_equation(
            prefix + "pressure_ratio",
            {{outlet_p, 1.0},
             {air_p, -required_parameter(
                         context.component, "pressure_ratio")}},
            0.0, 100000.0);
        system.add_checked_equation(
            prefix + "adiabatic_enthalpy",
            [cache, outlet_h](
                const std::vector<double>& x, double& residual) {
                const auto equilibrium = cache->evaluate(x);
                if (!equilibrium.status.ok()) {
                    return equilibrium.status;
                }
                residual = x.at(outlet_h) -
                    equilibrium.state.thermodynamic
                        .enthalpy_j_kg;
                return EvaluationStatus::success();
            },
            1000000.0);
        for (std::size_t index = 0; index < species.size();
             ++index) {
            system.add_checked_equation(
                prefix + "equilibrium_species." +
                    species.at(index),
                [cache, outlet = outlet_flow.at(index), index](
                    const std::vector<double>& x,
                    double& residual) {
                    const auto equilibrium = cache->evaluate(x);
                    if (!equilibrium.status.ok()) {
                        return equilibrium.status;
                    }
                    residual = x.at(outlet) -
                        equilibrium.inlet_mass_flow *
                            equilibrium.state.composition
                                .fractions()
                                .at(index);
                    return EvaluationStatus::success();
                },
                100.0);
        }
    }

private:
    ComponentModelDescriptor descriptor_;
};

}  // namespace

void register_combustion_component_models(
    ComponentRegistry& registry) {
    registry.register_model(std::make_shared<
        AdiabaticEquilibriumCombustorModel>());
}

}  // namespace thermox::platform
