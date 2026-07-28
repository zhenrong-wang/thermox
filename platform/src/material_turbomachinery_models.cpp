#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace thermox::platform {

namespace {

using component_model_support::require_port_species;
using component_model_support::require_port_variable;
using component_model_support::require_thermochemistry_package;
using component_model_support::required_parameter;

EvaluationStatus thermochemistry_failure(
    const physics::ThermochemicalResult& result) {
    if (result.status == physics::PropertyStatus::backend_error) {
        return EvaluationStatus::fatal(result.message);
    }
    return EvaluationStatus::recoverable(result.message);
}

physics::SpeciesComposition inlet_composition(
    const std::vector<double>& x,
    const std::vector<std::string>& species,
    const std::vector<std::size_t>& flows) {
    std::vector<double> fractions;
    fractions.reserve(flows.size());
    double total_mass_flow = 0.0;
    for (const auto variable : flows) {
        const double value = x.at(variable);
        if (value < 0.0) {
            throw std::domain_error(
                "material turbomachinery species mass flows must "
                "be nonnegative");
        }
        fractions.push_back(value);
        total_mass_flow += value;
    }
    if (total_mass_flow <= 0.0) {
        throw std::domain_error(
            "material turbomachinery total mass flow must be "
            "positive");
    }
    for (double& fraction : fractions) {
        fraction /= total_mass_flow;
    }
    return {
        physics::CompositionBasis::mass_fraction,
        species, std::move(fractions)};
}

struct IsentropicEvaluation {
    EvaluationStatus status{
        EvaluationStatus::fatal("isentropic evaluation has not run")};
    double residual{0.0};
};

class IsentropicEvaluationCache {
public:
    IsentropicEvaluationCache(
        std::shared_ptr<const physics::ThermochemistryPackage> properties,
        std::vector<std::string> species,
        std::vector<std::size_t> flows,
        std::size_t inlet_p, std::size_t inlet_h,
        std::size_t outlet_p, std::size_t outlet_h,
        double efficiency, bool compressor)
        : properties_(std::move(properties)),
          species_(std::move(species)),
          flows_(std::move(flows)),
          inlet_p_(inlet_p),
          inlet_h_(inlet_h),
          outlet_p_(outlet_p),
          outlet_h_(outlet_h),
          efficiency_(efficiency),
          compressor_(compressor) {}

    IsentropicEvaluation evaluate(
        const std::vector<double>& x) const {
        std::scoped_lock lock(mutex_);
        std::vector<double> key;
        key.reserve(flows_.size() + 4);
        for (const auto flow : flows_) {
            key.push_back(x.at(flow));
        }
        key.push_back(x.at(inlet_p_));
        key.push_back(x.at(inlet_h_));
        key.push_back(x.at(outlet_p_));
        key.push_back(x.at(outlet_h_));
        if (key == last_key_) {
            return last_;
        }
        last_key_ = std::move(key);
        physics::SpeciesComposition composition;
        try {
            composition =
                inlet_composition(x, species_, flows_);
        } catch (const std::domain_error& error) {
            last_ = {
                EvaluationStatus::recoverable(error.what()), 0.0};
            return last_;
        }
        const auto inlet = properties_->state_ph(
            x.at(inlet_p_), x.at(inlet_h_), composition);
        if (!inlet.ok()) {
            last_ = {thermochemistry_failure(inlet), 0.0};
            return last_;
        }
        const auto isentropic = properties_->state_ps(
            x.at(outlet_p_),
            inlet.state.thermodynamic.entropy_j_kg_k,
            composition);
        if (!isentropic.ok()) {
            last_ = {thermochemistry_failure(isentropic), 0.0};
            return last_;
        }
        const double ideal_change =
            isentropic.state.thermodynamic.enthalpy_j_kg -
            x.at(inlet_h_);
        last_ = {
            EvaluationStatus::success(),
            x.at(outlet_h_) - x.at(inlet_h_) -
                (compressor_
                     ? ideal_change / efficiency_
                     : efficiency_ * ideal_change)};
        return last_;
    }

private:
    std::shared_ptr<const physics::ThermochemistryPackage> properties_;
    std::vector<std::string> species_;
    std::vector<std::size_t> flows_;
    std::size_t inlet_p_;
    std::size_t inlet_h_;
    std::size_t outlet_p_;
    std::size_t outlet_h_;
    double efficiency_;
    bool compressor_;
    mutable std::mutex mutex_;
    mutable std::vector<double> last_key_;
    mutable IsentropicEvaluation last_;
};

class MaterialTurbomachineryModel final
    : public ComponentModel {
public:
    MaterialTurbomachineryModel(
        std::string kind, bool compressor)
        : compressor_(compressor) {
        descriptor_.kind = std::move(kind);
        descriptor_.version = "1.0.0";
        descriptor_.ports = {
            {"inlet", "material", "in"},
            {"outlet", "material", "out"},
            {"shaft", "shaft", compressor ? "in" : "out"}};
        descriptor_.parameters = {
            {"pressure_ratio", "dimensionless", true,
             std::nullopt, 1.0,
             std::numeric_limits<double>::infinity(), false,
             true},
            {"eta_is", "dimensionless", true, std::nullopt,
             0.0, 1.0, false, true}};
        descriptor_.required_thermochemistry_capabilities = {
            physics::ThermochemistryCapability::state_ph,
            physics::ThermochemistryCapability::state_ps};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto species =
            require_port_species(context, "inlet");
        if (species != require_port_species(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' inlet and outlet must share a species basis");
        }
        const auto properties =
            require_thermochemistry_package(context, "inlet");
        const auto outlet_properties =
            require_thermochemistry_package(context, "outlet");
        if (properties->name() != outlet_properties->name() ||
            properties->version() !=
                outlet_properties->version() ||
            properties->mechanism() !=
                outlet_properties->mechanism() ||
            properties->phase() != outlet_properties->phase()) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' inlet and outlet must resolve the same "
                "thermochemistry package");
        }

        std::vector<std::size_t> inlet_flows;
        const std::string prefix =
            "component." + context.component.id + ".";
        for (const auto& name : species) {
            const auto variable = "m_dot[" + name + "]";
            const auto inlet = require_port_variable(
                context, "inlet." + variable);
            const auto outlet = require_port_variable(
                context, "outlet." + variable);
            inlet_flows.push_back(inlet);
            system.add_linear_equation(
                prefix + "species_continuity." + name,
                {{outlet, 1.0}, {inlet, -1.0}},
                0.0, 100.0);
        }
        const auto inlet_p =
            require_port_variable(context, "inlet.p");
        const auto inlet_h =
            require_port_variable(context, "inlet.h");
        const auto outlet_p =
            require_port_variable(context, "outlet.p");
        const auto outlet_h =
            require_port_variable(context, "outlet.h");
        const auto shaft_w =
            require_port_variable(context, "shaft.W_dot");
        const double pressure_ratio = required_parameter(
            context.component, "pressure_ratio");
        const double efficiency = required_parameter(
            context.component, "eta_is");

        system.add_linear_equation(
            prefix + "pressure_ratio",
            compressor_
                ? std::vector<LinearTerm>{
                      {outlet_p, 1.0},
                      {inlet_p, -pressure_ratio}}
                : std::vector<LinearTerm>{
                      {inlet_p, 1.0},
                      {outlet_p, -pressure_ratio}},
            0.0, 100000.0 * pressure_ratio);
        const auto isentropic_cache =
            std::make_shared<IsentropicEvaluationCache>(
                properties, species, inlet_flows, inlet_p,
                inlet_h, outlet_p, outlet_h, efficiency,
                compressor_);
        system.add_checked_equation(
            prefix + "isentropic_efficiency",
            [isentropic_cache](
                const std::vector<double>& x,
                double& residual) {
                const auto evaluation =
                    isentropic_cache->evaluate(x);
                residual = evaluation.residual;
                return evaluation.status;
            },
            1000000.0);
        std::vector<std::size_t> power_variables = inlet_flows;
        power_variables.push_back(inlet_h);
        power_variables.push_back(outlet_h);
        power_variables.push_back(shaft_w);
        system.add_sparse_equation(
            prefix + "shaft_power",
            std::move(power_variables),
            [inlet_flows, inlet_h, outlet_h, shaft_w,
             compressor = compressor_](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                double mass_flow = 0.0;
                for (const auto variable : inlet_flows) {
                    mass_flow += x.at(variable);
                }
                const double direction =
                    compressor ? 1.0 : -1.0;
                const double enthalpy_change =
                    x.at(outlet_h) - x.at(inlet_h);
                jacobian.push_back({shaft_w, 1.0});
                for (const auto variable : inlet_flows) {
                    jacobian.push_back(
                        {variable,
                         -direction * enthalpy_change});
                }
                jacobian.push_back(
                    {inlet_h, direction * mass_flow});
                jacobian.push_back(
                    {outlet_h, -direction * mass_flow});
                return x.at(shaft_w) -
                    direction * mass_flow * enthalpy_change;
            },
            1000000.0);
    }

private:
    ComponentModelDescriptor descriptor_;
    bool compressor_{false};
};

}  // namespace

void register_material_turbomachinery_component_models(
    ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<MaterialTurbomachineryModel>(
            "compressor.material.isentropic_efficiency", true));
    registry.register_model(
        std::make_shared<MaterialTurbomachineryModel>(
            "turbine.material.isentropic_efficiency", false));
}

}  // namespace thermox::platform
