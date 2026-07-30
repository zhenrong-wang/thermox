#include "component_modules.hpp"
#include "component_model_support.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace thermox::platform {

namespace {

using component_model_support::property_failure;
using component_model_support::parameter_or;
using component_model_support::require_performance_map;
using component_model_support::require_port_variable;
using component_model_support::require_property_package;
using component_model_support::required_parameter;

ComponentModelDescriptor make_descriptor(
    std::string kind,
    std::string shaft_direction) {
    ComponentModelDescriptor out;
    out.kind = std::move(kind);
    out.version = "1.0.0";
    out.ports = {
        {"inlet", "fluid", "in"},
        {"outlet", "fluid", "out"},
        {"shaft", "shaft", std::move(shaft_direction)}};
    out.parameters = {
        {"pressure_ratio", "dimensionless", true, std::nullopt,
         1.0, std::numeric_limits<double>::infinity(), false, true},
        {"eta_is", "dimensionless", true, std::nullopt,
         0.0, 1.0, false, true}};
    out.required_property_capabilities = {
        physics::PropertyCapability::state_ph,
        physics::PropertyCapability::state_ps};
    return out;
}

ComponentModelDescriptor make_map_turbomachinery_descriptor(
    std::string kind,
    std::string shaft_direction,
    bool variable_geometry) {
    auto out = make_descriptor(
        std::move(kind), std::move(shaft_direction));
    out.version = "2.0.0";
    out.parameters = {
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
         true},
    };
    if (variable_geometry) {
        out.parameters.push_back(
            {"geometry_setting", "angle", true, std::nullopt,
             -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(), true,
             true});
    }
    out.artifacts = {{
        "performance_map",
        performance_map_artifact_type,
        true,
    }};
    return out;
}

struct TurbomachineryMapPoint {
    double pressure_ratio{0.0};
    double efficiency{0.0};
    double inlet_temperature{0.0};
    double corrected_mass_flow{0.0};
    double corrected_speed{0.0};
    double pressure_ratio_flow_derivative{0.0};
    double pressure_ratio_speed_derivative{0.0};
};

struct TurbomachineryMapContract {
    std::size_t pressure_ratio{0};
    std::size_t efficiency{0};
};

TurbomachineryMapContract validate_turbomachinery_map(
    const PerformanceMapArtifact& artifact,
    bool variable_geometry) {
    const PerformanceMap* selected = artifact.map.get();
    if (variable_geometry) {
        if (!artifact.conditioned_map ||
            artifact.conditioned_map->condition_variable().name !=
                "geometry_setting" ||
            artifact.conditioned_map->condition_variable().dimension !=
                "angle") {
            throw std::invalid_argument(
                "performance-map artifact '" + artifact.id +
                "' variable-geometry turbomachinery condition "
                "must be 'geometry_setting' with dimension 'angle'");
        }
        selected =
            artifact.conditioned_map->layers().front().map.get();
    } else if (!artifact.map) {
        throw std::invalid_argument(
            "performance-map artifact '" + artifact.id +
            "' must provide an ordinary two-coordinate map");
    }
    const auto& map = *selected;
    if (map.primary_variable().name !=
            "corrected_mass_flow" ||
        map.primary_variable().dimension != "mass_flow") {
        throw std::invalid_argument(
            "performance-map artifact '" + artifact.id +
            "' turbomachinery primary axis must be "
            "'corrected_mass_flow' with dimension 'mass_flow'");
    }
    if (map.family_variable().name != "corrected_speed" ||
        map.family_variable().dimension != "angular_speed") {
        throw std::invalid_argument(
            "performance-map artifact '" + artifact.id +
            "' turbomachinery family axis must be "
            "'corrected_speed' with dimension 'angular_speed'");
    }
    TurbomachineryMapContract result;
    bool found_pressure_ratio = false;
    bool found_efficiency = false;
    const auto& outputs = map.output_variables();
    for (std::size_t index = 0; index < outputs.size();
         ++index) {
        if (outputs[index].name == "pressure_ratio" &&
            outputs[index].dimension == "dimensionless") {
            result.pressure_ratio = index;
            found_pressure_ratio = true;
        }
        if (outputs[index].name ==
                "isentropic_efficiency" &&
            outputs[index].dimension == "dimensionless") {
            result.efficiency = index;
            found_efficiency = true;
        }
    }
    if (!found_pressure_ratio || !found_efficiency) {
        throw std::invalid_argument(
            "performance-map artifact '" + artifact.id +
            "' turbomachinery outputs must include dimensionless "
            "'pressure_ratio' and 'isentropic_efficiency'");
    }
    return result;
}

EvaluationStatus evaluate_turbomachinery_map(
    const PerformanceMapArtifact& artifact,
    const TurbomachineryMapContract& contract,
    const physics::PropertyPackage& properties,
    double mass_flow,
    double pressure,
    double enthalpy,
    double angular_speed,
    double reference_pressure,
    double reference_temperature,
    double flow_capacity_scale,
    double pressure_ratio_scale,
    double efficiency_scale,
    bool variable_geometry,
    double geometry_setting,
    TurbomachineryMapPoint& point) {
    const auto inlet = properties.state_ph(
        pressure, enthalpy);
    if (!inlet.ok()) {
        return property_failure(inlet);
    }
    const double theta =
        inlet.state.temperature_k / reference_temperature;
    const double delta = pressure / reference_pressure;
    if (!std::isfinite(theta) || !std::isfinite(delta) ||
        theta <= 0.0 || delta <= 0.0) {
        return EvaluationStatus::recoverable(
            "turbomachinery corrected-state ratios must be "
            "finite and positive");
    }
    const double root_theta = std::sqrt(theta);
    const double corrected_mass_flow =
        mass_flow * root_theta / delta;
    const double corrected_speed =
        angular_speed / root_theta;
    const double map_mass_flow =
        corrected_mass_flow / flow_capacity_scale;
    try {
        const auto evaluated = variable_geometry
            ? artifact.conditioned_map
                  ->evaluate(
                      map_mass_flow, corrected_speed,
                      geometry_setting)
                  .map
            : artifact.map->evaluate(
                  map_mass_flow, corrected_speed);
        point.pressure_ratio =
            1.0 + pressure_ratio_scale *
                (evaluated.outputs.at(
                     contract.pressure_ratio) -
                 1.0);
        point.efficiency =
            efficiency_scale *
            evaluated.outputs.at(contract.efficiency);
        point.pressure_ratio_flow_derivative =
            pressure_ratio_scale /
            flow_capacity_scale *
            evaluated.primary_derivatives.at(
                contract.pressure_ratio);
        point.pressure_ratio_speed_derivative =
            pressure_ratio_scale *
            evaluated.family_derivatives.at(
                contract.pressure_ratio);
    } catch (const MapDomainError& error) {
        return EvaluationStatus::recoverable(error.what());
    }
    if (!std::isfinite(point.pressure_ratio) ||
        point.pressure_ratio <= 1.0) {
        return EvaluationStatus::recoverable(
            "turbomachinery map pressure ratio must be finite and "
            "greater than one");
    }
    if (!std::isfinite(point.efficiency) ||
        point.efficiency <= 0.0 ||
        point.efficiency > 1.0) {
        return EvaluationStatus::recoverable(
            "turbomachinery map isentropic efficiency must be in "
            "(0, 1]");
    }
    point.inlet_temperature = inlet.state.temperature_k;
    point.corrected_mass_flow = corrected_mass_flow;
    point.corrected_speed = corrected_speed;
    return EvaluationStatus::success();
}

std::pair<double, double> inlet_temperature_derivatives(
    const physics::PropertyPackage& properties,
    double pressure,
    double enthalpy) {
    const auto result =
        physics::state_ph_derivatives_with_fallback(
            properties, pressure, enthalpy);
    if (!result.ok()) {
        throw std::runtime_error(result.message);
    }
    return {
        result.derivatives
            .temperature_wrt_pressure_at_enthalpy,
        result.derivatives
            .temperature_wrt_enthalpy_at_pressure,
    };
}

void add_turbomachinery_equations(
    const ComponentCompileContext& context,
    EquationSystemBuilder& system,
    bool compressor) {
    const double pressure_ratio =
        required_parameter(context.component, "pressure_ratio");
    const double eta_is =
        required_parameter(context.component, "eta_is");
    const auto properties =
        require_property_package(context, "inlet");
    if (properties !=
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
    const auto shaft_w =
        require_port_variable(context, "shaft.W_dot");
    const std::string prefix =
        "component." + context.component.id + ".";

    system.add_linear_equation(
        prefix + "mass_continuity",
        {{outlet_m, 1.0}, {inlet_m, -1.0}},
        0.0, 100.0);
    system.add_linear_equation(
        prefix + "pressure_ratio",
        compressor
            ? std::vector<LinearTerm>{
                  {outlet_p, 1.0},
                  {inlet_p, -pressure_ratio}}
            : std::vector<LinearTerm>{
                  {inlet_p, 1.0},
                  {outlet_p, -pressure_ratio}},
        0.0, 100000.0 * pressure_ratio);
    system.add_checked_equation(
        prefix + "isentropic_efficiency",
        [properties, compressor, eta_is, inlet_p, inlet_h,
         outlet_p, outlet_h](
            const std::vector<double>& x, double& residual) {
            const auto inlet = properties->state_ph(
                x.at(inlet_p), x.at(inlet_h));
            if (!inlet.ok()) return property_failure(inlet);
            const auto isentropic = properties->state_ps(
                x.at(outlet_p), inlet.state.entropy_j_kg_k);
            if (!isentropic.ok()) {
                return property_failure(isentropic);
            }
            const double ideal_change =
                isentropic.state.enthalpy_j_kg -
                x.at(inlet_h);
            residual =
                x.at(outlet_h) - x.at(inlet_h) -
                (compressor ? ideal_change / eta_is
                            : eta_is * ideal_change);
            return EvaluationStatus::success();
        },
        1.0e6);
    system.add_sparse_equation(
        prefix + "shaft_power",
        [compressor, inlet_m, inlet_h, outlet_h, shaft_w](
            const std::vector<double>& x,
            std::vector<EquationPartial>& jacobian) {
            const double direction = compressor ? 1.0 : -1.0;
            const double enthalpy_change =
                x.at(outlet_h) - x.at(inlet_h);
            jacobian.push_back({shaft_w, 1.0});
            jacobian.push_back(
                {inlet_m, -direction * enthalpy_change});
            jacobian.push_back(
                {inlet_h, direction * x.at(inlet_m)});
            jacobian.push_back(
                {outlet_h, -direction * x.at(inlet_m)});
            return x.at(shaft_w) -
                   direction * x.at(inlet_m) *
                       enthalpy_change;
        },
        1.0e6);
}

class TurbomachineryModel final : public ComponentModel {
public:
    TurbomachineryModel(
        std::string kind,
        bool compressor)
        : descriptor_(
              make_descriptor(
                  std::move(kind),
                  compressor ? "in" : "out")),
          compressor_(compressor) {}

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        add_turbomachinery_equations(
            context, system, compressor_);
    }

private:
    ComponentModelDescriptor descriptor_;
    bool compressor_{false};
};

class MapTurbomachineryModel final : public ComponentModel {
public:
    MapTurbomachineryModel(
        std::string kind,
        bool compressor,
        bool variable_geometry = false)
        : descriptor_(make_map_turbomachinery_descriptor(
              std::move(kind),
              compressor ? "in" : "out",
              variable_geometry)),
          compressor_(compressor),
          variable_geometry_(variable_geometry) {}

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto artifact =
            require_performance_map(
                context, "performance_map");
        const auto contract =
            validate_turbomachinery_map(
                *artifact, variable_geometry_);
        const auto properties =
            require_property_package(context, "inlet");
        if (properties !=
            require_property_package(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' inlet and outlet must use the same medium");
        }
        const double reference_pressure = parameter_or(
            context.component, "reference_pressure", 101325.0);
        const double reference_temperature = parameter_or(
            context.component, "reference_temperature", 288.15);
        const double flow_capacity_scale = parameter_or(
            context.component, "flow_capacity_scale", 1.0);
        const double pressure_ratio_scale = parameter_or(
            context.component, "pressure_ratio_scale", 1.0);
        const double efficiency_scale = parameter_or(
            context.component, "efficiency_scale", 1.0);
        const double geometry_setting = variable_geometry_
            ? required_parameter(
                  context.component, "geometry_setting")
            : 0.0;

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
        const auto shaft_w =
            require_port_variable(context, "shaft.W_dot");
        const auto shaft_omega =
            require_port_variable(context, "shaft.omega");
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_continuity",
            {{outlet_m, 1.0}, {inlet_m, -1.0}},
            0.0, 100.0);

        const auto map_point =
            [artifact, contract, properties, inlet_m, inlet_p,
             inlet_h, shaft_omega, reference_pressure,
             reference_temperature, flow_capacity_scale,
             pressure_ratio_scale, efficiency_scale,
             variable_geometry = variable_geometry_,
             geometry_setting](
                const std::vector<double>& x,
                TurbomachineryMapPoint& point) {
                return evaluate_turbomachinery_map(
                    *artifact, contract, *properties,
                    x.at(inlet_m), x.at(inlet_p),
                    x.at(inlet_h), x.at(shaft_omega),
                    reference_pressure, reference_temperature,
                    flow_capacity_scale, pressure_ratio_scale,
                    efficiency_scale,
                    variable_geometry, geometry_setting,
                    point);
            };

        system.add_checked_sparse_equation(
            prefix + "map_pressure_ratio",
            [map_point, inlet_p, outlet_p,
             compressor = compressor_](
                const std::vector<double>& x,
                double& residual) {
                TurbomachineryMapPoint point;
                const auto status = map_point(x, point);
                if (!status.ok()) return status;
                residual = compressor
                    ? x.at(outlet_p) -
                          x.at(inlet_p) *
                              point.pressure_ratio
                    : x.at(inlet_p) -
                          x.at(outlet_p) *
                              point.pressure_ratio;
                return EvaluationStatus::success();
            },
            {inlet_m, inlet_p, inlet_h, outlet_p, shaft_omega},
            [map_point, properties, inlet_m, inlet_p, inlet_h,
             outlet_p, shaft_omega, reference_pressure,
             reference_temperature,
             compressor = compressor_](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                TurbomachineryMapPoint point;
                const auto status = map_point(x, point);
                if (!status.ok()) {
                    throw std::runtime_error(status.message);
                }
                const auto [temperature_pressure_derivative,
                            temperature_enthalpy_derivative] =
                    inlet_temperature_derivatives(
                        *properties, x.at(inlet_p),
                        x.at(inlet_h));
                const double flow_mass_derivative =
                    std::sqrt(
                        point.inlet_temperature /
                        reference_temperature) /
                    (x.at(inlet_p) / reference_pressure);
                const double flow_temperature_factor =
                    point.corrected_mass_flow /
                    (2.0 * point.inlet_temperature);
                const double speed_temperature_factor =
                    -point.corrected_speed /
                    (2.0 * point.inlet_temperature);
                const double flow_pressure_derivative =
                    -point.corrected_mass_flow /
                        x.at(inlet_p) +
                    flow_temperature_factor *
                        temperature_pressure_derivative;
                const double flow_enthalpy_derivative =
                    flow_temperature_factor *
                    temperature_enthalpy_derivative;
                const double speed_pressure_derivative =
                    speed_temperature_factor *
                    temperature_pressure_derivative;
                const double speed_enthalpy_derivative =
                    speed_temperature_factor *
                    temperature_enthalpy_derivative;
                const double speed_omega_derivative =
                    std::sqrt(
                        reference_temperature /
                        point.inlet_temperature);
                const auto pressure_ratio_derivative =
                    [&](double flow_derivative,
                        double speed_derivative) {
                        return
                            point
                                .pressure_ratio_flow_derivative *
                                flow_derivative +
                            point
                                .pressure_ratio_speed_derivative *
                                speed_derivative;
                    };
                const double inlet_pressure = x.at(inlet_p);
                const double pressure_multiplier = compressor
                    ? inlet_pressure
                    : x.at(outlet_p);
                jacobian.push_back({
                    inlet_m,
                    -pressure_multiplier *
                        pressure_ratio_derivative(
                            flow_mass_derivative, 0.0)});
                jacobian.push_back({
                    inlet_p,
                    (compressor ? -point.pressure_ratio : 1.0) -
                        pressure_multiplier *
                            pressure_ratio_derivative(
                                flow_pressure_derivative,
                                speed_pressure_derivative)});
                jacobian.push_back({
                    inlet_h,
                    -pressure_multiplier *
                        pressure_ratio_derivative(
                            flow_enthalpy_derivative,
                            speed_enthalpy_derivative)});
                jacobian.push_back({
                    outlet_p,
                    compressor ? 1.0 : -point.pressure_ratio});
                jacobian.push_back({
                    shaft_omega,
                    -pressure_multiplier *
                        pressure_ratio_derivative(
                            0.0, speed_omega_derivative)});
                return compressor
                    ? x.at(outlet_p) -
                          inlet_pressure * point.pressure_ratio
                    : inlet_pressure -
                          x.at(outlet_p) *
                              point.pressure_ratio;
            },
            1.0e6);
        system.add_checked_equation(
            prefix + "map_isentropic_efficiency",
            [map_point, properties, inlet_p, inlet_h, outlet_p,
             outlet_h, compressor = compressor_](
                const std::vector<double>& x,
                       double& residual) {
                TurbomachineryMapPoint point;
                const auto status = map_point(x, point);
                if (!status.ok()) return status;
                const auto inlet = properties->state_ph(
                    x.at(inlet_p), x.at(inlet_h));
                if (!inlet.ok()) return property_failure(inlet);
                const auto isentropic = properties->state_ps(
                    x.at(outlet_p),
                    inlet.state.entropy_j_kg_k);
                if (!isentropic.ok()) {
                    return property_failure(isentropic);
                }
                residual =
                    x.at(outlet_h) - x.at(inlet_h) -
                    (compressor
                         ? (isentropic.state.enthalpy_j_kg -
                            x.at(inlet_h)) /
                               point.efficiency
                         : point.efficiency *
                               (isentropic.state.enthalpy_j_kg -
                                x.at(inlet_h)));
                return EvaluationStatus::success();
            },
            1.0e6);
        system.add_sparse_equation(
            prefix + "shaft_power",
            [inlet_m, inlet_h, outlet_h, shaft_w,
             compressor = compressor_](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                const double direction =
                    compressor ? 1.0 : -1.0;
                const double enthalpy_change =
                    x.at(outlet_h) - x.at(inlet_h);
                jacobian.push_back({shaft_w, 1.0});
                jacobian.push_back(
                    {inlet_m,
                     -direction * enthalpy_change});
                jacobian.push_back(
                    {inlet_h,
                     direction * x.at(inlet_m)});
                jacobian.push_back(
                    {outlet_h,
                     -direction * x.at(inlet_m)});
                return x.at(shaft_w) -
                    direction * x.at(inlet_m) *
                        enthalpy_change;
            },
            1.0e6);
    }

private:
    ComponentModelDescriptor descriptor_;
    bool compressor_{false};
    bool variable_geometry_{false};
};

}  // namespace

void register_turbomachinery_component_models(
    ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<TurbomachineryModel>(
            "compressor.gas.isentropic_efficiency", true));
    registry.register_model(
        std::make_shared<TurbomachineryModel>(
            "compressor.fluid.isentropic_efficiency", true));
    registry.register_model(
        std::make_shared<TurbomachineryModel>(
            "pump.fluid.isentropic_efficiency", true));
    registry.register_model(
        std::make_shared<TurbomachineryModel>(
            "turbine.gas.isentropic_efficiency", false));
    registry.register_model(
        std::make_shared<TurbomachineryModel>(
            "turbine.fluid.isentropic_efficiency", false));
    registry.register_model(
        std::make_shared<MapTurbomachineryModel>(
            "compressor.fluid.performance_map", true));
    registry.register_model(
        std::make_shared<MapTurbomachineryModel>(
            "turbine.fluid.performance_map", false));
    registry.register_model(
        std::make_shared<MapTurbomachineryModel>(
            "compressor.fluid.variable_geometry_map",
            true, true));
    registry.register_model(
        std::make_shared<MapTurbomachineryModel>(
            "turbine.fluid.variable_geometry_map",
            false, true));
}

}  // namespace thermox::platform
