#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <cmath>
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
using component_model_support::require_internal_variable;
using component_model_support::require_performance_map;
using component_model_support::require_thermochemistry_package;
using component_model_support::parameter_or;
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

struct MaterialMapEvaluation {
    EvaluationStatus status{
        EvaluationStatus::fatal(
            "material map evaluation has not run")};
    physics::ThermochemicalState inlet;
    double total_mass_flow{0.0};
    double pressure_ratio{0.0};
    double efficiency{0.0};
    double corrected_mass_flow_residual{0.0};
};

class MaterialMapEvaluationCache {
public:
    MaterialMapEvaluationCache(
        std::shared_ptr<const physics::ThermochemistryPackage>
            properties,
        std::shared_ptr<const PerformanceMapArtifact> artifact,
        std::vector<std::string> species,
        std::vector<std::size_t> flows,
        std::size_t inlet_p,
        std::size_t inlet_h,
        std::size_t shaft_omega,
        double reference_pressure,
        double reference_temperature,
        double flow_capacity_scale,
        double pressure_ratio_scale,
        double efficiency_scale,
        bool variable_geometry,
        double geometry_setting,
        bool latent_coordinate,
        std::optional<std::size_t> map_coordinate)
        : properties_(std::move(properties)),
          artifact_(std::move(artifact)),
          species_(std::move(species)),
          flows_(std::move(flows)),
          inlet_p_(inlet_p),
          inlet_h_(inlet_h),
          shaft_omega_(shaft_omega),
          reference_pressure_(reference_pressure),
          reference_temperature_(reference_temperature),
          flow_capacity_scale_(flow_capacity_scale),
          pressure_ratio_scale_(pressure_ratio_scale),
          efficiency_scale_(efficiency_scale),
          variable_geometry_(variable_geometry),
          geometry_setting_(geometry_setting),
          latent_coordinate_(latent_coordinate),
          map_coordinate_(map_coordinate) {
        const PerformanceMap* selected = artifact_->map.get();
        if (variable_geometry_) {
            if (!artifact_->conditioned_map ||
                artifact_->conditioned_map
                        ->condition_variable().name !=
                    "geometry_setting" ||
                artifact_->conditioned_map
                        ->condition_variable().dimension !=
                    "angle") {
                throw std::invalid_argument(
                    "performance-map artifact '" + artifact_->id +
                    "' variable-geometry turbomachinery condition "
                    "must be 'geometry_setting' with dimension "
                    "'angle'");
            }
            selected = artifact_->conditioned_map
                           ->layers().front().map.get();
        } else if (!selected) {
            throw std::invalid_argument(
                "performance-map artifact '" + artifact_->id +
                "' must provide an ordinary two-coordinate map");
        }
        const auto& map = *selected;
        const bool valid_primary = latent_coordinate_
            ? map.primary_variable().name == "map_coordinate" &&
                map.primary_variable().dimension == "dimensionless"
            : map.primary_variable().name == "corrected_mass_flow" &&
                map.primary_variable().dimension == "mass_flow";
        if (!valid_primary) {
            throw std::invalid_argument(
                "performance-map artifact '" + artifact_->id +
                (latent_coordinate_
                     ? "' coordinate-map turbomachinery primary axis "
                       "must be 'map_coordinate' with dimension "
                       "'dimensionless'"
                     : "' turbomachinery primary axis must be "
                       "'corrected_mass_flow' with dimension "
                       "'mass_flow'"));
        }
        if (map.family_variable().name != "corrected_speed" ||
            map.family_variable().dimension != "angular_speed") {
            throw std::invalid_argument(
                "performance-map artifact '" + artifact_->id +
                "' turbomachinery family axis must be "
                "'corrected_speed' with dimension 'angular_speed'");
        }
        const auto& outputs = map.output_variables();
        for (std::size_t index = 0; index < outputs.size();
             ++index) {
            if (outputs[index].name == "pressure_ratio" &&
                outputs[index].dimension == "dimensionless") {
                pressure_ratio_index_ = index;
            }
            if (outputs[index].name ==
                    "isentropic_efficiency" &&
                outputs[index].dimension == "dimensionless") {
                efficiency_index_ = index;
            }
            if (outputs[index].name ==
                    "corrected_mass_flow" &&
                outputs[index].dimension == "mass_flow") {
                corrected_mass_flow_index_ = index;
            }
        }
        if (!pressure_ratio_index_.has_value() ||
            !efficiency_index_.has_value() ||
            (latent_coordinate_ &&
             !corrected_mass_flow_index_.has_value())) {
            throw std::invalid_argument(
                "performance-map artifact '" + artifact_->id +
                "' turbomachinery outputs must include dimensionless "
                "'pressure_ratio' and 'isentropic_efficiency'" +
                (latent_coordinate_
                     ? ", plus mass-flow 'corrected_mass_flow'"
                     : ""));
        }
        if (latent_coordinate_ && !map_coordinate_.has_value()) {
            throw std::logic_error(
                "coordinate-map turbomachinery is missing its map-coordinate "
                "port variable");
        }
    }

    MaterialMapEvaluation evaluate(
        const std::vector<double>& x) const {
        std::scoped_lock lock(mutex_);
        std::vector<double> key;
        key.reserve(flows_.size() + 4);
        for (const auto flow : flows_) {
            key.push_back(x.at(flow));
        }
        key.push_back(x.at(inlet_p_));
        key.push_back(x.at(inlet_h_));
        key.push_back(x.at(shaft_omega_));
        if (map_coordinate_.has_value()) {
            key.push_back(x.at(*map_coordinate_));
        }
        if (key == last_key_) return last_;
        last_key_ = std::move(key);

        physics::SpeciesComposition composition;
        try {
            composition =
                inlet_composition(x, species_, flows_);
        } catch (const std::domain_error& error) {
            last_ = {
                EvaluationStatus::recoverable(error.what()),
                {}, 0.0, 0.0, 0.0, 0.0};
            return last_;
        }
        const auto inlet = properties_->state_ph(
            x.at(inlet_p_), x.at(inlet_h_), composition);
        if (!inlet.ok()) {
            last_ = {
                thermochemistry_failure(inlet),
                {}, 0.0, 0.0, 0.0, 0.0};
            return last_;
        }
        double total_mass_flow = 0.0;
        for (const auto flow : flows_) {
            total_mass_flow += x.at(flow);
        }
        const double theta =
            inlet.state.thermodynamic.temperature_k /
            reference_temperature_;
        const double delta =
            x.at(inlet_p_) / reference_pressure_;
        if (!std::isfinite(theta) || !std::isfinite(delta) ||
            theta <= 0.0 || delta <= 0.0) {
            last_ = {
                EvaluationStatus::recoverable(
                    "turbomachinery corrected-state ratios must be "
                    "finite and positive"),
                {}, 0.0, 0.0, 0.0, 0.0};
            return last_;
        }
        try {
            const double root_theta = std::sqrt(theta);
            const double corrected_mass_flow =
                total_mass_flow * root_theta / delta;
            const double corrected_speed =
                x.at(shaft_omega_) / root_theta;
            const double primary_coordinate = latent_coordinate_
                ? x.at(*map_coordinate_)
                : corrected_mass_flow / flow_capacity_scale_;
            const auto map = variable_geometry_
                ? artifact_->conditioned_map
                      ->evaluate(
                          primary_coordinate,
                          corrected_speed,
                          geometry_setting_)
                      .map
                : artifact_->map->evaluate(
                      primary_coordinate, corrected_speed);
            const double pressure_ratio =
                1.0 + pressure_ratio_scale_ *
                    (map.outputs.at(
                         *pressure_ratio_index_) -
                     1.0);
            const double efficiency =
                efficiency_scale_ *
                map.outputs.at(*efficiency_index_);
            const double corrected_mass_flow_residual =
                latent_coordinate_
                ? corrected_mass_flow -
                    flow_capacity_scale_ *
                        map.outputs.at(
                            *corrected_mass_flow_index_)
                : 0.0;
            if (!std::isfinite(pressure_ratio) ||
                pressure_ratio <= 1.0) {
                last_ = {
                    EvaluationStatus::recoverable(
                        "turbomachinery map pressure ratio must be "
                        "finite and greater than one"),
                    {}, 0.0, 0.0, 0.0, 0.0};
                return last_;
            }
            if (!std::isfinite(efficiency) ||
                efficiency <= 0.0 || efficiency > 1.0) {
                last_ = {
                    EvaluationStatus::recoverable(
                        "turbomachinery map isentropic efficiency "
                        "must be in (0, 1]"),
                    {}, 0.0, 0.0, 0.0, 0.0};
                return last_;
            }
            last_ = {
                EvaluationStatus::success(),
                inlet.state,
                total_mass_flow,
                pressure_ratio,
                efficiency,
                corrected_mass_flow_residual};
        } catch (const MapDomainError& error) {
            last_ = {
                EvaluationStatus::recoverable(error.what()),
                {}, 0.0, 0.0, 0.0, 0.0};
        }
        return last_;
    }

private:
    std::shared_ptr<const physics::ThermochemistryPackage>
        properties_;
    std::shared_ptr<const PerformanceMapArtifact> artifact_;
    std::vector<std::string> species_;
    std::vector<std::size_t> flows_;
    std::size_t inlet_p_;
    std::size_t inlet_h_;
    std::size_t shaft_omega_;
    double reference_pressure_;
    double reference_temperature_;
    double flow_capacity_scale_;
    double pressure_ratio_scale_;
    double efficiency_scale_;
    bool variable_geometry_;
    double geometry_setting_;
    bool latent_coordinate_{false};
    std::optional<std::size_t> map_coordinate_;
    std::optional<std::size_t> pressure_ratio_index_;
    std::optional<std::size_t> efficiency_index_;
    std::optional<std::size_t> corrected_mass_flow_index_;
    mutable std::mutex mutex_;
    mutable std::vector<double> last_key_;
    mutable MaterialMapEvaluation last_;
};

class MaterialMapIsentropicEvaluationCache {
public:
    MaterialMapIsentropicEvaluationCache(
        std::shared_ptr<const MaterialMapEvaluationCache>
            map_cache,
        std::shared_ptr<const physics::ThermochemistryPackage>
            properties,
        std::vector<std::size_t> flows,
        std::size_t inlet_p,
        std::size_t inlet_h,
        std::size_t outlet_p,
        std::size_t outlet_h,
        std::size_t shaft_omega,
        bool compressor,
        std::optional<std::size_t> map_coordinate)
        : map_cache_(std::move(map_cache)),
          properties_(std::move(properties)),
          flows_(std::move(flows)),
          inlet_p_(inlet_p),
          inlet_h_(inlet_h),
          outlet_p_(outlet_p),
          outlet_h_(outlet_h),
          shaft_omega_(shaft_omega),
          compressor_(compressor),
          map_coordinate_(map_coordinate) {}

    IsentropicEvaluation evaluate(
        const std::vector<double>& x) const {
        std::scoped_lock lock(mutex_);
        std::vector<double> key;
        key.reserve(flows_.size() + 6);
        for (const auto flow : flows_) {
            key.push_back(x.at(flow));
        }
        key.push_back(x.at(inlet_p_));
        key.push_back(x.at(inlet_h_));
        key.push_back(x.at(outlet_p_));
        key.push_back(x.at(outlet_h_));
        key.push_back(x.at(shaft_omega_));
        if (map_coordinate_.has_value()) {
            key.push_back(x.at(*map_coordinate_));
        }
        if (key == last_key_) return last_;
        last_key_ = std::move(key);

        const auto map = map_cache_->evaluate(x);
        if (!map.status.ok()) {
            last_ = {map.status, 0.0};
            return last_;
        }
        const auto isentropic = properties_->state_ps(
            x.at(outlet_p_),
            map.inlet.thermodynamic.entropy_j_kg_k,
            map.inlet.composition);
        if (!isentropic.ok()) {
            last_ = {
                thermochemistry_failure(isentropic), 0.0};
            return last_;
        }
        const double ideal_change =
            isentropic.state.thermodynamic.enthalpy_j_kg -
            x.at(inlet_h_);
        last_ = {
            EvaluationStatus::success(),
            x.at(outlet_h_) - x.at(inlet_h_) -
                (compressor_
                     ? ideal_change / map.efficiency
                     : map.efficiency * ideal_change)};
        return last_;
    }

private:
    std::shared_ptr<const MaterialMapEvaluationCache> map_cache_;
    std::shared_ptr<const physics::ThermochemistryPackage>
        properties_;
    std::vector<std::size_t> flows_;
    std::size_t inlet_p_;
    std::size_t inlet_h_;
    std::size_t outlet_p_;
    std::size_t outlet_h_;
    std::size_t shaft_omega_;
    bool compressor_;
    std::optional<std::size_t> map_coordinate_;
    mutable std::mutex mutex_;
    mutable std::vector<double> last_key_;
    mutable IsentropicEvaluation last_;
};

class MaterialCooledTurbineIsentropicCache {
public:
    MaterialCooledTurbineIsentropicCache(
        std::shared_ptr<const MaterialMapEvaluationCache> map_cache,
        std::shared_ptr<const physics::ThermochemistryPackage> properties,
        std::vector<std::string> species,
        std::vector<std::size_t> main_flows,
        std::vector<std::vector<std::size_t>> front_cooling_flows,
        std::size_t main_enthalpy,
        std::vector<std::size_t> front_cooling_enthalpies,
        std::size_t inlet_pressure,
        std::size_t outlet_pressure,
        std::size_t stage_outlet_enthalpy)
        : map_cache_(std::move(map_cache)),
          properties_(std::move(properties)),
          species_(std::move(species)),
          main_flows_(std::move(main_flows)),
          front_cooling_flows_(std::move(front_cooling_flows)),
          main_enthalpy_(main_enthalpy),
          front_cooling_enthalpies_(
              std::move(front_cooling_enthalpies)),
          inlet_pressure_(inlet_pressure),
          outlet_pressure_(outlet_pressure),
          stage_outlet_enthalpy_(stage_outlet_enthalpy) {}

    IsentropicEvaluation evaluate(const std::vector<double>& x) const {
        const auto map = map_cache_->evaluate(x);
        if (!map.status.ok()) return {map.status, 0.0};
        std::vector<double> species_flows(species_.size(), 0.0);
        double total_flow = 0.0;
        double enthalpy_flow = 0.0;
        const auto accumulate = [&](
                                    const std::vector<std::size_t>& flows,
                                    std::size_t enthalpy) {
            double stream_flow = 0.0;
            for (std::size_t i = 0; i < flows.size(); ++i) {
                const double flow = x.at(flows[i]);
                if (!std::isfinite(flow) || flow < 0.0) {
                    throw std::domain_error(
                        "cooled turbine species flows must be finite "
                        "and nonnegative");
                }
                species_flows[i] += flow;
                stream_flow += flow;
            }
            total_flow += stream_flow;
            enthalpy_flow += stream_flow * x.at(enthalpy);
        };
        try {
            accumulate(main_flows_, main_enthalpy_);
            for (std::size_t i = 0;
                 i < front_cooling_flows_.size(); ++i) {
                accumulate(
                    front_cooling_flows_[i],
                    front_cooling_enthalpies_[i]);
            }
        } catch (const std::domain_error& error) {
            return {EvaluationStatus::recoverable(error.what()), 0.0};
        }
        if (!std::isfinite(total_flow) || total_flow <= 0.0) {
            return {EvaluationStatus::recoverable(
                        "cooled turbine stage flow must be positive"),
                    0.0};
        }
        for (double& flow : species_flows) flow /= total_flow;
        const double stage_enthalpy = enthalpy_flow / total_flow;
        physics::SpeciesComposition composition{
            physics::CompositionBasis::mass_fraction,
            species_, std::move(species_flows)};
        const auto stage_inlet = properties_->state_ph(
            x.at(inlet_pressure_), stage_enthalpy, composition);
        if (!stage_inlet.ok()) {
            return {thermochemistry_failure(stage_inlet), 0.0};
        }
        const auto isentropic = properties_->state_ps(
            x.at(outlet_pressure_),
            stage_inlet.state.thermodynamic.entropy_j_kg_k,
            composition);
        if (!isentropic.ok()) {
            return {thermochemistry_failure(isentropic), 0.0};
        }
        const double ideal_change =
            isentropic.state.thermodynamic.enthalpy_j_kg -
            stage_enthalpy;
        return {
            EvaluationStatus::success(),
            x.at(stage_outlet_enthalpy_) - stage_enthalpy -
                map.efficiency * ideal_change};
    }

private:
    std::shared_ptr<const MaterialMapEvaluationCache> map_cache_;
    std::shared_ptr<const physics::ThermochemistryPackage> properties_;
    std::vector<std::string> species_;
    std::vector<std::size_t> main_flows_;
    std::vector<std::vector<std::size_t>> front_cooling_flows_;
    std::size_t main_enthalpy_;
    std::vector<std::size_t> front_cooling_enthalpies_;
    std::size_t inlet_pressure_;
    std::size_t outlet_pressure_;
    std::size_t stage_outlet_enthalpy_;
};

class MaterialTurbomachineryModel final
    : public ComponentModel {
public:
    MaterialTurbomachineryModel(
        std::string kind, bool compressor)
        : compressor_(compressor) {
        descriptor_.kind = std::move(kind);
        descriptor_.version = "2.0.0";
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

        system.add_continuation_linear_equation(
            prefix + "pressure_ratio",
            compressor_
                ? std::vector<LinearTerm>{
                      {outlet_p, 1.0},
                      {inlet_p, -pressure_ratio}}
                : std::vector<LinearTerm>{
                      {inlet_p, 1.0},
                      {outlet_p, -pressure_ratio}},
            0.0,
            [compressor = compressor_, inlet_p, outlet_p,
             pressure_ratio](
                const std::vector<double>& x,
                const std::vector<double>& anchor,
                double continuation_parameter,
                std::vector<EquationPartial>& jacobian) {
                double anchor_pressure_ratio = compressor
                    ? anchor.at(outlet_p) /
                          anchor.at(inlet_p)
                    : anchor.at(inlet_p) /
                          anchor.at(outlet_p);
                if (!std::isfinite(
                        anchor_pressure_ratio) ||
                    anchor_pressure_ratio <= 0.0) {
                    anchor_pressure_ratio = 1.0;
                }
                const double staged_pressure_ratio =
                    anchor_pressure_ratio +
                    continuation_parameter *
                        (pressure_ratio -
                         anchor_pressure_ratio);
                if (compressor) {
                    jacobian.push_back({outlet_p, 1.0});
                    jacobian.push_back(
                        {inlet_p, -staged_pressure_ratio});
                    return x.at(outlet_p) -
                           staged_pressure_ratio *
                               x.at(inlet_p);
                }
                jacobian.push_back({inlet_p, 1.0});
                jacobian.push_back(
                    {outlet_p, -staged_pressure_ratio});
                return x.at(inlet_p) -
                       staged_pressure_ratio *
                           x.at(outlet_p);
            },
            100000.0 * pressure_ratio);
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

class Iso2314EquivalentCoolingCompressorModel final
    : public ComponentModel {
public:
    Iso2314EquivalentCoolingCompressorModel() {
        descriptor_.kind =
            "compressor.material.iso2314_equivalent_cooling";
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = "compressor.material";
        descriptor_.display_name =
            "Compressor (ISO 2314 equivalent cooling)";
        descriptor_.category = "Turbomachinery";
        descriptor_.model_name =
            "Isentropic-efficiency compressor with work-equivalent "
            "cooling extraction";
        descriptor_.ports = {
            {"inlet", "material", "in"},
            {"outlet", "material", "out"},
            {"shaft", "shaft", "in"}};
        descriptor_.parameters = {
            {"pressure_ratio", "dimensionless", true,
             std::nullopt, 1.0,
             std::numeric_limits<double>::infinity(), false, true},
            {"eta_is", "dimensionless", true, std::nullopt,
             0.0, 1.0, false, true},
            {"relative_equivalent_flow_difference_md",
             "dimensionless", true, std::nullopt, 0.0,
             std::numeric_limits<double>::infinity(), true, true}};
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
        const auto species = require_port_species(context, "inlet");
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
            properties->version() != outlet_properties->version() ||
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
        const auto shaft_w =
            require_port_variable(context, "shaft.W_dot");
        const double pressure_ratio = required_parameter(
            context.component, "pressure_ratio");
        const double efficiency = required_parameter(
            context.component, "eta_is");
        const double md = required_parameter(
            context.component,
            "relative_equivalent_flow_difference_md");
        const double equivalent_fraction = 1.0 / (1.0 + md);

        system.add_linear_equation(
            prefix + "pressure_ratio",
            {{outlet_p, 1.0},
             {inlet_p, -pressure_ratio}},
            0.0, 100000.0 * pressure_ratio);
        const auto isentropic_cache =
            std::make_shared<IsentropicEvaluationCache>(
                properties, species, inlet_flows, inlet_p,
                inlet_h, outlet_p, outlet_h,
                efficiency / equivalent_fraction, true);
        system.add_checked_equation(
            prefix + "iso2314_equivalent_discharge_enthalpy",
            [isentropic_cache](
                const std::vector<double>& x,
                double& residual) {
                const auto evaluation =
                    isentropic_cache->evaluate(x);
                residual = evaluation.residual;
                return evaluation.status;
            },
            1.0e6);

        std::vector<std::size_t> power_variables = inlet_flows;
        power_variables.push_back(inlet_h);
        power_variables.push_back(outlet_h);
        power_variables.push_back(shaft_w);
        system.add_sparse_equation(
            prefix + "equivalent_compressor_power",
            std::move(power_variables),
            [inlet_flows, inlet_h, outlet_h, shaft_w](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                double mass_flow = 0.0;
                for (const auto variable : inlet_flows) {
                    mass_flow += x.at(variable);
                }
                const double enthalpy_change =
                    x.at(outlet_h) - x.at(inlet_h);
                jacobian.push_back({shaft_w, 1.0});
                for (const auto variable : inlet_flows) {
                    jacobian.push_back(
                        {variable, -enthalpy_change});
                }
                jacobian.push_back({inlet_h, mass_flow});
                jacobian.push_back({outlet_h, -mass_flow});
                return x.at(shaft_w) -
                    mass_flow * enthalpy_change;
            },
            1.0e6);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class MaterialMapTurbomachineryModel final
    : public ComponentModel {
public:
    MaterialMapTurbomachineryModel(
        std::string kind, bool compressor,
        bool variable_geometry = false,
        bool latent_coordinate = false,
        bool fractional_bleeds = false,
        bool cooling_injections = false)
        : compressor_(compressor),
          variable_geometry_(variable_geometry),
          latent_coordinate_(latent_coordinate),
          fractional_bleeds_(fractional_bleeds),
          cooling_injections_(cooling_injections) {
        if (latent_coordinate_ && variable_geometry_) {
            throw std::invalid_argument(
                "latent map coordinates require fixed geometry");
        }
        if (fractional_bleeds_ && !latent_coordinate_) {
            throw std::invalid_argument(
                "fractional bleed ports require a coordinate-map "
                "compressor");
        }
        if (cooling_injections_ &&
            (compressor_ || !latent_coordinate_ || fractional_bleeds_)) {
            throw std::invalid_argument(
                "cooling injections require a coordinate-map turbine");
        }
        descriptor_.kind = std::move(kind);
        descriptor_.version = "1.0.0";
        descriptor_.template_kind = compressor_
            ? "compressor.material"
            : "turbine.material";
        descriptor_.display_name = compressor_
            ? "Material compressor"
            : "Material turbine";
        descriptor_.category = "Turbomachinery";
        descriptor_.model_name = latent_coordinate_
            ? (fractional_bleeds_
                  ? "Coordinate map with fractional bleed extraction"
                  : cooling_injections_
                      ? "Coordinate map with staged cooling injection"
                  : "Coordinate-based performance map")
            : (variable_geometry_
                  ? "Variable-geometry performance map"
                  : "Performance map");
        descriptor_.ports = {
            {"inlet", "material", "in"},
            {"outlet", "material", "out"},
            {"shaft", "shaft", compressor ? "in" : "out"}};
        if (latent_coordinate_) {
            descriptor_.ports.push_back(
                {"map_coordinate", "signal", "in"});
        }
        descriptor_.parameters = {
            {"reference_pressure", "pressure", false, 101325.0,
             0.0, std::numeric_limits<double>::infinity(), false,
             true},
            {"reference_temperature", "temperature", false, 288.15,
             0.0, std::numeric_limits<double>::infinity(), false,
             true},
            {"flow_capacity_scale", "dimensionless", false, 1.0,
             0.0, std::numeric_limits<double>::infinity(), false,
             true},
            {"pressure_ratio_scale", "dimensionless", false, 1.0,
             0.0, std::numeric_limits<double>::infinity(), false,
             true},
            {"efficiency_scale", "dimensionless", false, 1.0,
             0.0, std::numeric_limits<double>::infinity(), false,
             true}};
        if (fractional_bleeds_) {
            descriptor_.port_groups.push_back({
                "bleed", "bleed_", "material", "out", 1U, 32U,
                1U});
            descriptor_.parameters.push_back({
                "bleed_fraction[{index}]", "dimensionless", true,
                std::nullopt, 0.0, 1.0, true, false});
            descriptor_.parameters.push_back({
                "bleed_pressure_fraction[{index}]", "dimensionless",
                true, std::nullopt, 0.0, 1.0, true, true});
            descriptor_.parameters.push_back({
                "bleed_enthalpy_fraction[{index}]", "dimensionless",
                true, std::nullopt, 0.0, 1.0, true, true});
        }
        if (cooling_injections_) {
            descriptor_.port_groups.push_back({
                "cooling", "cooling_", "material", "in", 1U, 32U,
                1U});
            descriptor_.parameters.push_back({
                "cooling_position[{index}]", "dimensionless", true,
                std::nullopt, 0.0, 1.0, true, true});
            descriptor_.internal_variables.push_back({
                "stage_outlet_enthalpy", DaeVariableKind::algebraic,
                500000.0, 1000000.0, 0.0, 1.0,
                -std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity(),
                "specific_enthalpy"});
        }
        if (variable_geometry_) {
            descriptor_.parameters.push_back(
                {"geometry_setting", "angle", true, std::nullopt,
                 -std::numeric_limits<double>::infinity(),
                 std::numeric_limits<double>::infinity(), true,
                 true});
        }
        descriptor_.artifacts = {{
            "performance_map",
            performance_map_artifact_type,
            true,
        }};
        descriptor_.required_thermochemistry_capabilities = {
            physics::ThermochemistryCapability::state_ph,
            physics::ThermochemistryCapability::state_ps};
    }

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    ComponentModelDescriptor instance_descriptor(
        const ComponentDefinition& component) const override {
        auto result = descriptor_;
        if (!fractional_bleeds_ && !cooling_injections_) {
            if (!component.port_counts.empty()) {
                throw std::invalid_argument(
                    "component '" + component.id +
                    "' does not declare an instance-sized port group");
            }
            return result;
        }
        const std::string group =
            fractional_bleeds_ ? "bleed" : "cooling";
        if (component.port_counts.size() != 1U ||
            !component.port_counts.contains(group)) {
            throw std::invalid_argument(
                "component '" + component.id +
                "' must declare exactly port_counts." + group);
        }
        const auto count = component.port_counts.at(group);
        if (count == 0U || count > 32U) {
            throw std::invalid_argument(
                "component '" + component.id +
                "' " + group + " port count must be in [1, 32]");
        }
        for (std::size_t index = 1; index <= count; ++index) {
            const std::string suffix = "[" + std::to_string(index) + "]";
            result.ports.push_back({
                group + "_" + std::to_string(index),
                "material", fractional_bleeds_ ? "out" : "in"});
            if (fractional_bleeds_) {
                result.parameters.push_back({
                    "bleed_fraction" + suffix, "dimensionless", true,
                    std::nullopt, 0.0, 1.0, true, false});
                result.parameters.push_back({
                    "bleed_pressure_fraction" + suffix,
                    "dimensionless", true, std::nullopt,
                    0.0, 1.0, true, true});
                result.parameters.push_back({
                    "bleed_enthalpy_fraction" + suffix,
                    "dimensionless", true, std::nullopt,
                    0.0, 1.0, true, true});
            } else {
                result.parameters.push_back({
                    "cooling_position" + suffix, "dimensionless", true,
                    std::nullopt, 0.0, 1.0, true, true});
            }
        }
        return result;
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

        const std::size_t bleed_count = fractional_bleeds_
            ? context.component.port_counts.at("bleed")
            : 0U;
        std::vector<double> bleed_fractions;
        std::vector<double> bleed_pressure_fractions;
        std::vector<double> bleed_enthalpy_fractions;
        for (std::size_t index = 1; index <= bleed_count; ++index) {
            const std::string suffix =
                "[" + std::to_string(index) + "]";
            bleed_fractions.push_back(required_parameter(
                context.component, "bleed_fraction" + suffix));
            bleed_pressure_fractions.push_back(required_parameter(
                context.component,
                "bleed_pressure_fraction" + suffix));
            bleed_enthalpy_fractions.push_back(required_parameter(
                context.component,
                "bleed_enthalpy_fraction" + suffix));
            const std::string port =
                "bleed_" + std::to_string(index);
            if (context.component.material_bindings.at(port) !=
                context.component.material_bindings.at("inlet")) {
                throw std::invalid_argument(
                    "component '" + context.component.id +
                    "' bleed ports must share the inlet material binding");
            }
            if (species != require_port_species(context, port)) {
                throw std::invalid_argument(
                    "component '" + context.component.id +
                    "' bleed ports must share the inlet species basis");
            }
        }
        double total_bleed_fraction = 0.0;
        for (const double fraction : bleed_fractions) {
            total_bleed_fraction += fraction;
        }
        if (!std::isfinite(total_bleed_fraction) ||
            total_bleed_fraction >= 1.0) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' total bleed fraction must be finite and below one");
        }
        const std::size_t cooling_count = cooling_injections_
            ? context.component.port_counts.at("cooling")
            : 0U;
        std::vector<bool> cooling_at_exit;
        for (std::size_t index = 1; index <= cooling_count; ++index) {
            const std::string suffix = "[" + std::to_string(index) + "]";
            const double position = required_parameter(
                context.component, "cooling_position" + suffix);
            if (position != 0.0 && position != 1.0) {
                throw std::invalid_argument(
                    "component '" + context.component.id +
                    "' cooling position must be exactly 0 (stage inlet) "
                    "or 1 (turbine exit)");
            }
            cooling_at_exit.push_back(position == 1.0);
            const std::string port =
                "cooling_" + std::to_string(index);
            if (species != require_port_species(context, port)) {
                throw std::invalid_argument(
                    "component '" + context.component.id +
                    "' cooling ports must share the turbine species basis");
            }
            const auto cooling_package =
                require_thermochemistry_package(context, port);
            if (properties->name() != cooling_package->name() ||
                properties->version() != cooling_package->version() ||
                properties->mechanism() != cooling_package->mechanism() ||
                properties->phase() != cooling_package->phase()) {
                throw std::invalid_argument(
                    "component '" + context.component.id +
                    "' cooling ports must resolve the turbine "
                    "thermochemistry package");
            }
        }

        std::vector<std::size_t> inlet_flows;
        std::vector<std::vector<std::size_t>> bleed_flows(
            bleed_count);
        std::vector<std::vector<std::size_t>> cooling_flows(
            cooling_count);
        const std::string prefix =
            "component." + context.component.id + ".";
        for (const auto& name : species) {
            const std::string variable =
                "m_dot[" + name + "]";
            const auto inlet = require_port_variable(
                context, "inlet." + variable);
            const auto outlet = require_port_variable(
                context, "outlet." + variable);
            inlet_flows.push_back(inlet);
            std::vector<LinearTerm> continuity{
                {outlet, 1.0}, {inlet, -1.0}};
            for (std::size_t bleed = 0;
                 bleed < bleed_count; ++bleed) {
                const auto flow = require_port_variable(
                    context,
                    "bleed_" + std::to_string(bleed + 1) +
                        "." + variable);
                bleed_flows[bleed].push_back(flow);
                continuity.push_back({flow, 1.0});
                system.add_linear_equation(
                    prefix + "bleed_" +
                        std::to_string(bleed + 1) +
                        "_species." + name,
                    {{flow, 1.0},
                     {inlet, -bleed_fractions[bleed]}},
                    0.0, 100.0);
            }
            for (std::size_t cooling = 0;
                 cooling < cooling_count; ++cooling) {
                const auto flow = require_port_variable(
                    context,
                    "cooling_" + std::to_string(cooling + 1) +
                        "." + variable);
                cooling_flows[cooling].push_back(flow);
                continuity.push_back({flow, -1.0});
            }
            system.add_linear_equation(
                prefix + "species_continuity." + name,
                std::move(continuity),
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
        const auto shaft_omega =
            require_port_variable(context, "shaft.omega");
        std::vector<std::size_t> bleed_pressures;
        std::vector<std::size_t> bleed_enthalpies;
        for (std::size_t bleed = 0;
             bleed < bleed_count; ++bleed) {
            const std::string port =
                "bleed_" + std::to_string(bleed + 1);
            const auto pressure = require_port_variable(
                context, port + ".p");
            const auto enthalpy = require_port_variable(
                context, port + ".h");
            bleed_pressures.push_back(pressure);
            bleed_enthalpies.push_back(enthalpy);
            const double pressure_fraction =
                bleed_pressure_fractions[bleed];
            const double enthalpy_fraction =
                bleed_enthalpy_fractions[bleed];
            system.add_linear_equation(
                prefix + port + "_pressure_state",
                {{pressure, 1.0},
                 {inlet_p, -(1.0 - pressure_fraction)},
                 {outlet_p, -pressure_fraction}},
                0.0, 1.0e6);
            system.add_linear_equation(
                prefix + port + "_enthalpy_state",
                {{enthalpy, 1.0},
                 {inlet_h, -(1.0 - enthalpy_fraction)},
                 {outlet_h, -enthalpy_fraction}},
                0.0, 1.0e6);
        }
        std::vector<std::size_t> cooling_enthalpies;
        for (std::size_t cooling = 0;
             cooling < cooling_count; ++cooling) {
            cooling_enthalpies.push_back(require_port_variable(
                context,
                "cooling_" + std::to_string(cooling + 1) + ".h"));
        }
        const std::optional<std::size_t> map_coordinate =
            latent_coordinate_
            ? std::optional<std::size_t>{
                  require_port_variable(
                      context, "map_coordinate.value")}
            : std::nullopt;
        const auto cache =
            std::make_shared<MaterialMapEvaluationCache>(
                properties,
                require_performance_map(
                    context, "performance_map"),
                species, inlet_flows, inlet_p, inlet_h,
                shaft_omega,
                parameter_or(
                    context.component, "reference_pressure",
                    101325.0),
                parameter_or(
                    context.component, "reference_temperature",
                    288.15),
                parameter_or(
                    context.component, "flow_capacity_scale",
                    1.0),
                parameter_or(
                    context.component, "pressure_ratio_scale",
                    1.0),
                parameter_or(
                    context.component, "efficiency_scale",
                    1.0),
                variable_geometry_,
                variable_geometry_
                    ? required_parameter(
                          context.component,
                          "geometry_setting")
                    : 0.0,
                latent_coordinate_, map_coordinate);

        if (latent_coordinate_) {
            system.add_checked_equation(
                prefix + "map_corrected_flow",
                [cache](const std::vector<double>& x,
                        double& residual) {
                    const auto map = cache->evaluate(x);
                    if (!map.status.ok()) return map.status;
                    residual = map.corrected_mass_flow_residual;
                    return EvaluationStatus::success();
                },
                100.0);
        }

        system.add_checked_equation(
            prefix + "map_pressure_ratio",
            [cache, inlet_p, outlet_p,
             compressor = compressor_](
                const std::vector<double>& x,
                double& residual) {
                const auto map = cache->evaluate(x);
                if (!map.status.ok()) return map.status;
                residual = compressor
                    ? x.at(outlet_p) -
                        x.at(inlet_p) * map.pressure_ratio
                    : x.at(inlet_p) -
                        x.at(outlet_p) * map.pressure_ratio;
                return EvaluationStatus::success();
            },
            1.0e6);
        if (cooling_injections_) {
            std::vector<std::vector<std::size_t>> front_flows;
            std::vector<std::size_t> front_enthalpies;
            for (std::size_t cooling = 0;
                 cooling < cooling_count; ++cooling) {
                if (!cooling_at_exit[cooling]) {
                    front_flows.push_back(cooling_flows[cooling]);
                    front_enthalpies.push_back(
                        cooling_enthalpies[cooling]);
                }
            }
            const auto stage_outlet_h =
                require_internal_variable(
                    context, "stage_outlet_enthalpy");
            const auto cooled_cache = std::make_shared<
                MaterialCooledTurbineIsentropicCache>(
                    cache, properties, species, inlet_flows,
                    front_flows, inlet_h, front_enthalpies,
                    inlet_p, outlet_p, stage_outlet_h);
            system.add_checked_equation(
                prefix + "map_isentropic_efficiency",
                [cooled_cache](const std::vector<double>& x,
                               double& residual) {
                    const auto evaluation = cooled_cache->evaluate(x);
                    residual = evaluation.residual;
                    return evaluation.status;
                },
                1.0e6);
            system.add_checked_equation(
                prefix + "cooling_exit_energy_balance",
                [inlet_flows, cooling_flows, cooling_enthalpies,
                 cooling_at_exit, outlet_h,
                 stage_outlet_h](const std::vector<double>& x,
                                 double& residual) {
                    double main_flow = 0.0;
                    for (const auto variable : inlet_flows) {
                        main_flow += x.at(variable);
                    }
                    double stage_flow = main_flow;
                    double total_flow = main_flow;
                    for (std::size_t cooling = 0;
                         cooling < cooling_flows.size(); ++cooling) {
                        double flow = 0.0;
                        for (const auto variable : cooling_flows[cooling]) {
                            flow += x.at(variable);
                        }
                        total_flow += flow;
                        if (!cooling_at_exit[cooling]) stage_flow += flow;
                    }
                    double outlet_energy =
                        stage_flow * x.at(stage_outlet_h);
                    for (std::size_t cooling = 0;
                         cooling < cooling_flows.size(); ++cooling) {
                        if (!cooling_at_exit[cooling]) continue;
                        double flow = 0.0;
                        for (const auto variable : cooling_flows[cooling]) {
                            flow += x.at(variable);
                        }
                        outlet_energy +=
                            flow * x.at(cooling_enthalpies[cooling]);
                    }
                    residual = total_flow * x.at(outlet_h) -
                        outlet_energy;
                    return EvaluationStatus::success();
                },
                1.0e7);
            std::vector<std::size_t> shaft_power_variables =
                inlet_flows;
            shaft_power_variables.push_back(inlet_h);
            shaft_power_variables.push_back(stage_outlet_h);
            shaft_power_variables.push_back(shaft_w);
            for (std::size_t cooling = 0;
                 cooling < cooling_flows.size(); ++cooling) {
                if (cooling_at_exit[cooling]) continue;
                shaft_power_variables.insert(
                    shaft_power_variables.end(),
                    cooling_flows[cooling].begin(),
                    cooling_flows[cooling].end());
                shaft_power_variables.push_back(
                    cooling_enthalpies[cooling]);
            }
            system.add_sparse_equation(
                prefix + "shaft_power",
                std::move(shaft_power_variables),
                [inlet_flows, cooling_flows, cooling_enthalpies,
                 cooling_at_exit, inlet_h, stage_outlet_h,
                 shaft_w](const std::vector<double>& x,
                          std::vector<EquationPartial>& jacobian) {
                    double main_flow = 0.0;
                    for (const auto variable : inlet_flows) {
                        main_flow += x.at(variable);
                        jacobian.push_back({
                            variable,
                            x.at(stage_outlet_h) - x.at(inlet_h)});
                    }
                    double stage_flow = main_flow;
                    double stage_inlet_energy =
                        main_flow * x.at(inlet_h);
                    jacobian.push_back({inlet_h, -main_flow});
                    for (std::size_t cooling = 0;
                         cooling < cooling_flows.size(); ++cooling) {
                        if (cooling_at_exit[cooling]) continue;
                        double flow = 0.0;
                        for (const auto variable : cooling_flows[cooling]) {
                            flow += x.at(variable);
                            jacobian.push_back({
                                variable,
                                x.at(stage_outlet_h) -
                                    x.at(cooling_enthalpies[cooling])});
                        }
                        stage_flow += flow;
                        stage_inlet_energy +=
                            flow * x.at(cooling_enthalpies[cooling]);
                        jacobian.push_back({
                            cooling_enthalpies[cooling], -flow});
                    }
                    jacobian.push_back({
                        stage_outlet_h, stage_flow});
                    jacobian.push_back({shaft_w, 1.0});
                    return x.at(shaft_w) -
                        (stage_inlet_energy -
                         stage_flow * x.at(stage_outlet_h));
                },
                1.0e6);
            return;
        }
        const auto isentropic_cache = std::make_shared<
            MaterialMapIsentropicEvaluationCache>(
                cache, properties, inlet_flows, inlet_p,
                inlet_h, outlet_p, outlet_h, shaft_omega,
                compressor_, map_coordinate);
        system.add_checked_equation(
            prefix + "map_isentropic_efficiency",
            [isentropic_cache](const std::vector<double>& x,
                               double& residual) {
                const auto evaluation = isentropic_cache->evaluate(x);
                residual = evaluation.residual;
                return evaluation.status;
            },
            1.0e6);
        std::vector<std::size_t> power_variables = inlet_flows;
        power_variables.push_back(inlet_h);
        power_variables.push_back(outlet_h);
        power_variables.push_back(shaft_w);
        for (std::size_t bleed = 0;
             bleed < bleed_count; ++bleed) {
            power_variables.insert(
                power_variables.end(), bleed_flows[bleed].begin(),
                bleed_flows[bleed].end());
            power_variables.push_back(bleed_enthalpies[bleed]);
        }
        system.add_sparse_equation(
            prefix + "shaft_power",
            std::move(power_variables),
            [inlet_flows, bleed_flows, bleed_enthalpies,
             inlet_h, outlet_h, shaft_w,
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
                    jacobian.push_back({
                        variable,
                        -direction * enthalpy_change});
                }
                jacobian.push_back(
                    {inlet_h, direction * mass_flow});
                jacobian.push_back(
                    {outlet_h, -direction * mass_flow});
                double residual = x.at(shaft_w) -
                    direction * mass_flow * enthalpy_change;
                double total_bleed_flow = 0.0;
                for (std::size_t bleed = 0;
                     bleed < bleed_flows.size(); ++bleed) {
                    double bleed_mass_flow = 0.0;
                    for (const auto variable : bleed_flows[bleed]) {
                        bleed_mass_flow += x.at(variable);
                        jacobian.push_back({
                            variable,
                            direction *
                                (x.at(outlet_h) -
                                 x.at(bleed_enthalpies[bleed]))});
                    }
                    total_bleed_flow += bleed_mass_flow;
                    jacobian.push_back({
                        bleed_enthalpies[bleed],
                        -direction * bleed_mass_flow});
                    residual += direction * bleed_mass_flow *
                        (x.at(outlet_h) -
                         x.at(bleed_enthalpies[bleed]));
                }
                if (total_bleed_flow != 0.0) {
                    jacobian.push_back({
                        outlet_h,
                        direction * total_bleed_flow});
                }
                return residual;
            },
            1.0e6);
    }

private:
    ComponentModelDescriptor descriptor_;
    bool compressor_{false};
    bool variable_geometry_{false};
    bool latent_coordinate_{false};
    bool fractional_bleeds_{false};
    bool cooling_injections_{false};
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
    registry.register_model(
        std::make_shared<Iso2314EquivalentCoolingCompressorModel>());
    registry.register_model(
        std::make_shared<MaterialMapTurbomachineryModel>(
            "compressor.material.performance_map", true));
    registry.register_model(
        std::make_shared<MaterialMapTurbomachineryModel>(
            "turbine.material.performance_map", false));
    registry.register_model(
        std::make_shared<MaterialMapTurbomachineryModel>(
            "compressor.material.variable_geometry_map",
            true, true));
    registry.register_model(
        std::make_shared<MaterialMapTurbomachineryModel>(
            "turbine.material.variable_geometry_map",
            false, true));
    registry.register_model(
        std::make_shared<MaterialMapTurbomachineryModel>(
            "compressor.material.coordinate_map",
            true, false, true));
    registry.register_model(
        std::make_shared<MaterialMapTurbomachineryModel>(
            "turbine.material.coordinate_map",
            false, false, true));
    registry.register_model(
        std::make_shared<MaterialMapTurbomachineryModel>(
            "turbine.material.coordinate_map.cooling_injections",
            false, false, true, false, true));
    registry.register_model(
        std::make_shared<MaterialMapTurbomachineryModel>(
            "compressor.material.coordinate_map.fractional_bleeds",
            true, false, true, true));
}

}  // namespace thermox::platform
