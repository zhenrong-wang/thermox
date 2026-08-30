#include "component_modules.hpp"
#include "component_model_support.hpp"
#include "performance_map_continuation.hpp"
#include "thermox/platform/semi_physical_positive_displacement_pump.hpp"

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
    const bool is_compressor = out.kind.starts_with("compressor.");
    const bool is_turbine = out.kind.starts_with("turbine.");
    const bool is_pump = out.kind.starts_with("pump.");
    if (is_compressor || is_turbine || is_pump) {
        out.template_kind = is_compressor
            ? "compressor"
            : (is_turbine ? "turbine" : "pump");
        out.display_name = is_compressor
            ? "Compressor"
            : (is_turbine ? "Turbine" : "Pump");
        out.category = "Turbomachinery";
        out.model_name = "Isentropic efficiency";
    }
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
    out.supports_transient = true;
    return out;
}

ComponentModelDescriptor make_map_turbomachinery_descriptor(
    std::string kind,
    std::string shaft_direction,
    bool variable_geometry) {
    auto out = make_descriptor(
        std::move(kind), std::move(shaft_direction));
    out.version = "2.0.0";
    out.supports_transient = true;
    out.uses_quasi_steady_transient_equations = true;
    out.model_name = variable_geometry
        ? "Variable-geometry performance map"
        : "Performance map";
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

ComponentModelDescriptor make_pump_map_descriptor() {
    auto out = make_descriptor(
        "pump.fluid.performance_map", "in");
    out.version = "1.0.0";
    out.supports_transient = true;
    out.uses_quasi_steady_transient_equations = true;
    out.model_name = "Mass-flow/speed performance map";
    out.parameters = {
        {"flow_capacity_scale", "dimensionless", false, 1.0,
         0.0, std::numeric_limits<double>::infinity(), false, true},
        {"pressure_rise_scale", "dimensionless", false, 1.0,
         0.0, std::numeric_limits<double>::infinity(), false, true},
        {"efficiency_scale", "dimensionless", false, 1.0,
         0.0, std::numeric_limits<double>::infinity(), false, true},
    };
    out.artifacts = {{
        "performance_map",
        performance_map_artifact_type,
        true,
    }};
    return out;
}

ComponentModelDescriptor make_positive_displacement_pump_descriptor() {
    auto out = make_descriptor(
        "pump.fluid.semi_physical_positive_displacement", "in");
    out.version = "1.0.0";
    out.supports_transient = true;
    out.uses_quasi_steady_transient_equations = true;
    out.model_name =
        "Displacement capacity with pressure-driven leakage";
    out.parameters = {
        {"displacement_volume_per_revolution", "volume", true,
         std::nullopt, 0.0, std::numeric_limits<double>::infinity(),
         false, true},
        {"leakage_area", "area", false, 0.0, 0.0,
         std::numeric_limits<double>::infinity(), true, true},
        {"leakage_discharge_coefficient", "dimensionless", false,
         1.0, 0.0, 1.0, true, true},
        {"eta_is", "dimensionless", true, std::nullopt, 0.0, 1.0,
         false, true},
    };
    out.internal_variables = {
        {"ideal_displacement_mass_flow", DaeVariableKind::algebraic,
         0.02, 0.1, 0.0, 1.0, 0.0,
         std::numeric_limits<double>::infinity(), "mass_flow"},
        {"leakage_mass_flow", DaeVariableKind::algebraic,
         0.001, 0.1, 0.0, 1.0, 0.0,
         std::numeric_limits<double>::infinity(), "mass_flow"},
    };
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
    double coordinate_derivative_scale{1.0};
};

struct TurbomachineryMapContract {
    std::size_t pressure_ratio{0};
    std::size_t efficiency{0};
};

struct PumpMapPoint {
    double pressure_rise{0.0};
    double efficiency{0.0};
    double pressure_rise_flow_derivative{0.0};
    double pressure_rise_speed_derivative{0.0};
    double coordinate_derivative_scale{1.0};
};

struct PumpMapContract {
    std::size_t pressure_rise{0};
    std::size_t efficiency{0};
};

PumpMapContract validate_pump_map(
    const PerformanceMapArtifact& artifact) {
    if (!artifact.map) {
        throw std::invalid_argument(
            "performance-map artifact '" + artifact.id +
            "' pump model requires an ordinary two-coordinate map");
    }
    const auto& map = *artifact.map;
    if (map.primary_variable().name != "mass_flow" ||
        map.primary_variable().dimension != "mass_flow") {
        throw std::invalid_argument(
            "performance-map artifact '" + artifact.id +
            "' pump primary axis must be 'mass_flow' with dimension "
            "'mass_flow'");
    }
    if (map.family_variable().name != "angular_speed" ||
        map.family_variable().dimension != "angular_speed") {
        throw std::invalid_argument(
            "performance-map artifact '" + artifact.id +
            "' pump family axis must be 'angular_speed' with dimension "
            "'angular_speed'");
    }
    PumpMapContract result;
    bool found_pressure_rise = false;
    bool found_efficiency = false;
    const auto& outputs = map.output_variables();
    for (std::size_t index = 0; index < outputs.size(); ++index) {
        if (outputs[index].name == "pressure_rise" &&
            outputs[index].dimension == "pressure") {
            result.pressure_rise = index;
            found_pressure_rise = true;
        }
        if (outputs[index].name == "isentropic_efficiency" &&
            outputs[index].dimension == "dimensionless") {
            result.efficiency = index;
            found_efficiency = true;
        }
    }
    if (!found_pressure_rise || !found_efficiency) {
        throw std::invalid_argument(
            "performance-map artifact '" + artifact.id +
            "' pump outputs must include pressure-dimensioned "
            "'pressure_rise' and dimensionless "
            "'isentropic_efficiency'");
    }
    return result;
}

EvaluationStatus evaluate_pump_map(
    const PerformanceMapArtifact& artifact,
    const PumpMapContract& contract,
    double mass_flow,
    double angular_speed,
    double flow_capacity_scale,
    double pressure_rise_scale,
    double efficiency_scale,
    PumpMapPoint& point,
    const performance_map_continuation::Seed* continuation_seed = nullptr,
    double continuation_parameter = 1.0) {
    const double map_mass_flow = mass_flow / flow_capacity_scale;
    const double evaluation_mass_flow = continuation_seed == nullptr
        ? map_mass_flow
        : continuation_seed->primary_coordinate +
              continuation_parameter *
                  (map_mass_flow -
                   continuation_seed->primary_coordinate);
    const double evaluation_speed = continuation_seed == nullptr
        ? angular_speed
        : continuation_seed->family_coordinate +
              continuation_parameter *
                  (angular_speed -
                   continuation_seed->family_coordinate);
    try {
        const auto evaluated = artifact.map->evaluate(
            evaluation_mass_flow, evaluation_speed);
        point.pressure_rise = pressure_rise_scale *
            evaluated.outputs.at(contract.pressure_rise);
        point.efficiency = efficiency_scale *
            evaluated.outputs.at(contract.efficiency);
        point.pressure_rise_flow_derivative =
            pressure_rise_scale / flow_capacity_scale *
            evaluated.primary_derivatives.at(contract.pressure_rise);
        point.pressure_rise_speed_derivative =
            pressure_rise_scale *
            evaluated.family_derivatives.at(contract.pressure_rise);
    } catch (const MapDomainError& error) {
        return EvaluationStatus::recoverable(error.what());
    }
    if (!std::isfinite(point.pressure_rise) ||
        point.pressure_rise <= 0.0) {
        return EvaluationStatus::recoverable(
            "pump map pressure rise must be finite and positive");
    }
    if (!std::isfinite(point.efficiency) ||
        point.efficiency <= 0.0 || point.efficiency > 1.0) {
        return EvaluationStatus::recoverable(
            "pump map isentropic efficiency must be in (0, 1]");
    }
    point.coordinate_derivative_scale =
        continuation_seed == nullptr ? 1.0 : continuation_parameter;
    return EvaluationStatus::success();
}

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
    TurbomachineryMapPoint& point,
    const performance_map_continuation::Seed*
        continuation_seed = nullptr,
    double continuation_parameter = 1.0) {
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
    const double evaluation_mass_flow =
        continuation_seed == nullptr
        ? map_mass_flow
        : continuation_seed->primary_coordinate +
              continuation_parameter *
                  (map_mass_flow -
                   continuation_seed->primary_coordinate);
    const double evaluation_speed =
        continuation_seed == nullptr
        ? corrected_speed
        : continuation_seed->family_coordinate +
              continuation_parameter *
                  (corrected_speed -
                   continuation_seed->family_coordinate);
    const double evaluation_geometry =
        continuation_seed == nullptr
        ? geometry_setting
        : continuation_seed->condition_coordinate +
              continuation_parameter *
                  (geometry_setting -
                   continuation_seed->condition_coordinate);
    try {
        const auto evaluated = variable_geometry
            ? artifact.conditioned_map
                  ->evaluate(
                      evaluation_mass_flow, evaluation_speed,
                      evaluation_geometry)
                  .map
            : artifact.map->evaluate(
                  evaluation_mass_flow, evaluation_speed);
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
    point.coordinate_derivative_scale =
        continuation_seed == nullptr
        ? 1.0
        : continuation_parameter;
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
    system.add_continuation_linear_equation(
        prefix + "pressure_ratio",
        compressor
            ? std::vector<LinearTerm>{
                  {outlet_p, 1.0},
                  {inlet_p, -pressure_ratio}}
            : std::vector<LinearTerm>{
                  {inlet_p, 1.0},
                  {outlet_p, -pressure_ratio}},
        0.0,
        [compressor, inlet_p, outlet_p, pressure_ratio](
            const std::vector<double>& x,
            const std::vector<double>& anchor,
            double continuation_parameter,
            std::vector<EquationPartial>& jacobian) {
            double anchor_pressure_ratio = compressor
                ? anchor.at(outlet_p) /
                      anchor.at(inlet_p)
                : anchor.at(inlet_p) /
                      anchor.at(outlet_p);
            if (!std::isfinite(anchor_pressure_ratio) ||
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

void add_transient_turbomachinery_equations(
    const ComponentCompileContext& context,
    DaeEquationSystemBuilder& system,
    bool compressor) {
    const double pressure_ratio =
        required_parameter(context.component, "pressure_ratio");
    const double eta_is =
        required_parameter(context.component, "eta_is");
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
    const auto shaft_w =
        require_port_variable(context, "shaft.W_dot");
    const std::string prefix =
        "component." + context.component.id + ".";

    system.add_linear_equation(
        prefix + "mass_continuity",
        {{outlet_m, 1.0, 0.0}, {inlet_m, -1.0, 0.0}},
        0.0, 100.0);
    system.add_linear_equation(
        prefix + "pressure_ratio",
        compressor
            ? std::vector<DaeLinearTerm>{
                  {outlet_p, 1.0, 0.0},
                  {inlet_p, -pressure_ratio, 0.0}}
            : std::vector<DaeLinearTerm>{
                  {inlet_p, 1.0, 0.0},
                  {outlet_p, -pressure_ratio, 0.0}},
        0.0, 100000.0 * pressure_ratio);
    const auto efficiency_variables = std::vector<std::size_t>{
        inlet_p, inlet_h, outlet_p, outlet_h};
    const auto evaluate_efficiency =
        [properties, compressor, eta_is, inlet_p, inlet_h,
         outlet_p, outlet_h](
            const std::vector<double>& x, double& residual) {
            const auto inlet = properties->state_ph(
                x.at(inlet_p), x.at(inlet_h));
            if (!inlet.ok()) return property_failure(inlet);
            const auto isentropic = properties->state_ps(
                x.at(outlet_p), inlet.state.entropy_j_kg_k);
            if (!isentropic.ok()) return property_failure(isentropic);
            const double ideal_change =
                isentropic.state.enthalpy_j_kg - x.at(inlet_h);
            residual = x.at(outlet_h) - x.at(inlet_h) -
                (compressor ? ideal_change / eta_is
                            : eta_is * ideal_change);
            return EvaluationStatus::success();
        };
    system.add_sparse_equation(
        prefix + "isentropic_efficiency",
        efficiency_variables,
        [evaluate_efficiency, efficiency_variables](
            double, const std::vector<double>& x,
            const std::vector<double>&, double& residual,
            std::vector<DaeEquationPartial>& jacobian) {
            auto status = evaluate_efficiency(x, residual);
            if (!status.ok()) return status;
            for (const auto variable : efficiency_variables) {
                std::vector<double> perturbed = x;
                const double step = 1.0e-6 *
                    std::max(std::abs(x.at(variable)), 1.0);
                perturbed.at(variable) += step;
                double shifted = 0.0;
                status = evaluate_efficiency(perturbed, shifted);
                if (!status.ok()) return status;
                jacobian.push_back(
                    {variable, (shifted - residual) / step, 0.0});
            }
            return EvaluationStatus::success();
        },
        1.0e6);
    system.add_sparse_equation(
        prefix + "shaft_power",
        {inlet_m, inlet_h, outlet_h, shaft_w},
        [compressor, inlet_m, inlet_h, outlet_h, shaft_w](
            double, const std::vector<double>& x,
            const std::vector<double>&, double& residual,
            std::vector<DaeEquationPartial>& jacobian) {
            const double direction = compressor ? 1.0 : -1.0;
            const double enthalpy_change =
                x.at(outlet_h) - x.at(inlet_h);
            residual = x.at(shaft_w) - direction *
                x.at(inlet_m) * enthalpy_change;
            jacobian.push_back({shaft_w, 1.0, 0.0});
            jacobian.push_back(
                {inlet_m, -direction * enthalpy_change, 0.0});
            jacobian.push_back(
                {inlet_h, direction * x.at(inlet_m), 0.0});
            jacobian.push_back(
                {outlet_h, -direction * x.at(inlet_m), 0.0});
            return EvaluationStatus::success();
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

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        add_transient_turbomachinery_equations(
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
        const auto continuation_seed =
            performance_map_continuation::seed(
                *artifact, variable_geometry_);
        const auto continuation_artifact =
            std::make_shared<const PerformanceMapArtifact>(
                performance_map_continuation::linear_extension(
                    *artifact, variable_geometry_));
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

        const auto continued_map_point =
            [artifact, continuation_artifact, contract,
             properties, inlet_m, inlet_p,
             inlet_h, shaft_omega, reference_pressure,
             reference_temperature, flow_capacity_scale,
             pressure_ratio_scale, efficiency_scale,
             variable_geometry = variable_geometry_,
             geometry_setting, continuation_seed](
                const std::vector<double>& x,
                double continuation_parameter,
                TurbomachineryMapPoint& point) {
                const auto& selected_artifact =
                    continuation_parameter < 1.0
                    ? *continuation_artifact
                    : *artifact;
                return evaluate_turbomachinery_map(
                    selected_artifact, contract, *properties,
                    x.at(inlet_m), x.at(inlet_p),
                    x.at(inlet_h), x.at(shaft_omega),
                    reference_pressure, reference_temperature,
                    flow_capacity_scale, pressure_ratio_scale,
                    efficiency_scale,
                    variable_geometry, geometry_setting,
                    point, &continuation_seed,
                    continuation_parameter);
            };

        system.add_continuation_checked_sparse_equation(
            prefix + "map_pressure_ratio",
            [continued_map_point, inlet_p, outlet_p,
             compressor = compressor_](
                const std::vector<double>& x,
                const std::vector<double>& anchor,
                double continuation_parameter,
                double& residual) {
                TurbomachineryMapPoint point;
                const auto status = continued_map_point(
                    x, continuation_parameter, point);
                if (!status.ok()) return status;
                double anchor_pressure_ratio = compressor
                    ? anchor.at(outlet_p) /
                          anchor.at(inlet_p)
                    : anchor.at(inlet_p) /
                          anchor.at(outlet_p);
                if (!std::isfinite(anchor_pressure_ratio) ||
                    anchor_pressure_ratio <= 0.0) {
                    anchor_pressure_ratio = 1.0;
                }
                const double staged_pressure_ratio =
                    anchor_pressure_ratio +
                    continuation_parameter *
                        (point.pressure_ratio -
                         anchor_pressure_ratio);
                residual = compressor
                    ? x.at(outlet_p) -
                          x.at(inlet_p) *
                              staged_pressure_ratio
                    : x.at(inlet_p) -
                          x.at(outlet_p) *
                              staged_pressure_ratio;
                return EvaluationStatus::success();
            },
            {inlet_m, inlet_p, inlet_h, outlet_p, shaft_omega},
            [continued_map_point, properties, inlet_m, inlet_p,
             inlet_h,
             outlet_p, shaft_omega, reference_pressure,
             reference_temperature,
             compressor = compressor_](
                const std::vector<double>& x,
                const std::vector<double>& anchor,
                double continuation_parameter,
                std::vector<EquationPartial>& jacobian) {
                TurbomachineryMapPoint point;
                const auto status = continued_map_point(
                    x, continuation_parameter, point);
                if (!status.ok()) {
                    throw std::runtime_error(status.message);
                }
                double anchor_pressure_ratio = compressor
                    ? anchor.at(outlet_p) /
                          anchor.at(inlet_p)
                    : anchor.at(inlet_p) /
                          anchor.at(outlet_p);
                if (!std::isfinite(anchor_pressure_ratio) ||
                    anchor_pressure_ratio <= 0.0) {
                    anchor_pressure_ratio = 1.0;
                }
                const double staged_pressure_ratio =
                    anchor_pressure_ratio +
                    continuation_parameter *
                        (point.pressure_ratio -
                         anchor_pressure_ratio);
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
                        return continuation_parameter *
                            point.coordinate_derivative_scale *
                            point
                                .pressure_ratio_flow_derivative *
                                flow_derivative +
                            continuation_parameter *
                            point.coordinate_derivative_scale *
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
                    (compressor
                         ? -staged_pressure_ratio
                         : 1.0) -
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
                    compressor
                        ? 1.0
                        : -staged_pressure_ratio});
                jacobian.push_back({
                    shaft_omega,
                    -pressure_multiplier *
                        pressure_ratio_derivative(
                            0.0, speed_omega_derivative)});
                return compressor
                    ? x.at(outlet_p) -
                          inlet_pressure *
                              staged_pressure_ratio
                    : inlet_pressure -
                          x.at(outlet_p) *
                              staged_pressure_ratio;
            },
            1.0e6);
        system.add_continuation_checked_equation(
            prefix + "map_isentropic_efficiency",
            [continued_map_point, properties, inlet_p, inlet_h,
             outlet_p,
             outlet_h, compressor = compressor_](
                const std::vector<double>& x,
                const std::vector<double>&,
                double continuation_parameter,
                       double& residual) {
                TurbomachineryMapPoint point;
                const auto status = continued_map_point(
                    x, continuation_parameter, point);
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

class PumpMapModel final : public ComponentModel {
public:
    PumpMapModel() : descriptor_(make_pump_map_descriptor()) {}

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        const auto artifact = require_performance_map(
            context, "performance_map");
        const auto contract = validate_pump_map(*artifact);
        const auto continuation_seed =
            performance_map_continuation::seed(*artifact, false);
        const auto continuation_artifact =
            std::make_shared<const PerformanceMapArtifact>(
                performance_map_continuation::linear_extension(
                    *artifact, false));
        const auto properties =
            require_property_package(context, "inlet");
        if (properties !=
            require_property_package(context, "outlet")) {
            throw std::invalid_argument(
                "component '" + context.component.id +
                "' inlet and outlet must use the same medium");
        }
        const double flow_capacity_scale = parameter_or(
            context.component, "flow_capacity_scale", 1.0);
        const double pressure_rise_scale = parameter_or(
            context.component, "pressure_rise_scale", 1.0);
        const double efficiency_scale = parameter_or(
            context.component, "efficiency_scale", 1.0);

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

        const auto continued_map_point =
            [artifact, continuation_artifact, contract,
             inlet_m, shaft_omega, flow_capacity_scale,
             pressure_rise_scale, efficiency_scale,
             continuation_seed](
                const std::vector<double>& x,
                double continuation_parameter,
                PumpMapPoint& point) {
                const auto& selected_artifact =
                    continuation_parameter < 1.0
                    ? *continuation_artifact
                    : *artifact;
                return evaluate_pump_map(
                    selected_artifact, contract,
                    x.at(inlet_m), x.at(shaft_omega),
                    flow_capacity_scale, pressure_rise_scale,
                    efficiency_scale, point, &continuation_seed,
                    continuation_parameter);
            };

        system.add_continuation_checked_sparse_equation(
            prefix + "map_pressure_rise",
            [continued_map_point, inlet_p, outlet_p](
                const std::vector<double>& x,
                const std::vector<double>& anchor,
                double continuation_parameter,
                double& residual) {
                PumpMapPoint point;
                const auto status = continued_map_point(
                    x, continuation_parameter, point);
                if (!status.ok()) return status;
                double anchor_pressure_rise =
                    anchor.at(outlet_p) - anchor.at(inlet_p);
                if (!std::isfinite(anchor_pressure_rise)) {
                    anchor_pressure_rise = 0.0;
                }
                const double staged_pressure_rise =
                    anchor_pressure_rise + continuation_parameter *
                        (point.pressure_rise - anchor_pressure_rise);
                residual = x.at(outlet_p) - x.at(inlet_p) -
                    staged_pressure_rise;
                return EvaluationStatus::success();
            },
            {inlet_m, inlet_p, outlet_p, shaft_omega},
            [continued_map_point, inlet_m, inlet_p,
             outlet_p, shaft_omega](
                const std::vector<double>& x,
                const std::vector<double>& anchor,
                double continuation_parameter,
                std::vector<EquationPartial>& jacobian) {
                PumpMapPoint point;
                const auto status = continued_map_point(
                    x, continuation_parameter, point);
                if (!status.ok()) {
                    throw std::runtime_error(status.message);
                }
                double anchor_pressure_rise =
                    anchor.at(outlet_p) - anchor.at(inlet_p);
                if (!std::isfinite(anchor_pressure_rise)) {
                    anchor_pressure_rise = 0.0;
                }
                const double staged_pressure_rise =
                    anchor_pressure_rise + continuation_parameter *
                        (point.pressure_rise - anchor_pressure_rise);
                const double derivative_scale =
                    continuation_parameter *
                    point.coordinate_derivative_scale;
                jacobian.push_back({
                    inlet_m,
                    -derivative_scale *
                        point.pressure_rise_flow_derivative});
                jacobian.push_back({inlet_p, -1.0});
                jacobian.push_back({outlet_p, 1.0});
                jacobian.push_back({
                    shaft_omega,
                    -derivative_scale *
                        point.pressure_rise_speed_derivative});
                return x.at(outlet_p) - x.at(inlet_p) -
                    staged_pressure_rise;
            },
            1.0e6);
        system.add_continuation_checked_equation(
            prefix + "map_isentropic_efficiency",
            [continued_map_point, properties, inlet_p, inlet_h,
             outlet_p, outlet_h](
                const std::vector<double>& x,
                const std::vector<double>&,
                double continuation_parameter,
                double& residual) {
                PumpMapPoint point;
                const auto status = continued_map_point(
                    x, continuation_parameter, point);
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
                residual = x.at(outlet_h) - x.at(inlet_h) -
                    (isentropic.state.enthalpy_j_kg -
                     x.at(inlet_h)) / point.efficiency;
                return EvaluationStatus::success();
            },
            1.0e6);
        system.add_sparse_equation(
            prefix + "shaft_power",
            [inlet_m, inlet_h, outlet_h, shaft_w](
                const std::vector<double>& x,
                std::vector<EquationPartial>& jacobian) {
                const double enthalpy_change =
                    x.at(outlet_h) - x.at(inlet_h);
                jacobian.push_back({shaft_w, 1.0});
                jacobian.push_back({inlet_m, -enthalpy_change});
                jacobian.push_back({inlet_h, x.at(inlet_m)});
                jacobian.push_back({outlet_h, -x.at(inlet_m)});
                return x.at(shaft_w) -
                    x.at(inlet_m) * enthalpy_change;
            },
            1.0e6);
    }

private:
    ComponentModelDescriptor descriptor_;
};

class SemiPhysicalPositiveDisplacementPumpModel final
    : public ComponentModel {
public:
    SemiPhysicalPositiveDisplacementPumpModel()
        : descriptor_(make_positive_displacement_pump_descriptor()) {}

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
        const SemiPhysicalPositiveDisplacementPumpParameters parameters{
            required_parameter(
                context.component,
                "displacement_volume_per_revolution"),
            required_parameter(context.component, "leakage_area"),
            required_parameter(
                context.component, "leakage_discharge_coefficient"),
            required_parameter(context.component, "eta_is")};
        const auto inlet_m = require_port_variable(
            context, "inlet.m_dot");
        const auto inlet_p = require_port_variable(context, "inlet.p");
        const auto inlet_h = require_port_variable(context, "inlet.h");
        const auto outlet_m = require_port_variable(
            context, "outlet.m_dot");
        const auto outlet_p = require_port_variable(context, "outlet.p");
        const auto outlet_h = require_port_variable(context, "outlet.h");
        const auto shaft_w = require_port_variable(
            context, "shaft.W_dot");
        const auto shaft_omega = require_port_variable(
            context, "shaft.omega");
        const auto ideal_mass = component_model_support::
            require_internal_variable(
                context, "ideal_displacement_mass_flow");
        const auto leakage_mass = component_model_support::
            require_internal_variable(context, "leakage_mass_flow");
        const auto raw_evaluate =
            [properties, inlet_p, inlet_h, outlet_p, shaft_omega,
             parameters](
                const std::vector<double>& x,
                SemiPhysicalPositiveDisplacementPumpEvaluation& result) {
                return evaluate_semi_physical_positive_displacement_pump(
                    *properties, x.at(inlet_p), x.at(inlet_h),
                    x.at(outlet_p), x.at(shaft_omega), parameters,
                    result);
            };
        const std::vector<std::size_t> evaluator_dependencies{
            inlet_p, inlet_h, outlet_p, shaft_omega};
        const auto evaluate = component_model_support::
            memoize_component_evaluation<
                SemiPhysicalPositiveDisplacementPumpEvaluation>(
                raw_evaluate, evaluator_dependencies);
        const std::string prefix =
            "component." + context.component.id + ".";

        system.add_linear_equation(
            prefix + "mass_continuity",
            {{outlet_m, 1.0}, {inlet_m, -1.0}}, 0.0, 0.1);
        const auto add_result_equation =
            [&system, &prefix, evaluate](
                const std::string& name, std::size_t variable,
                double SemiPhysicalPositiveDisplacementPumpEvaluation::
                    *member,
                std::vector<std::size_t> dependencies,
                double scale) {
                dependencies.push_back(variable);
                component_model_support::
                    add_numeric_checked_sparse_equation(
                    system,
                    prefix + name,
                    [evaluate, variable, member](
                        const std::vector<double>& x,
                        double& residual) {
                        SemiPhysicalPositiveDisplacementPumpEvaluation
                            result;
                        const auto status = evaluate(x, result);
                        if (!status.ok()) return status;
                        residual = x.at(variable) - result.*member;
                        return EvaluationStatus::success();
                    },
                    std::move(dependencies),
                    scale);
            };
        add_result_equation(
            "displacement_capacity", inlet_m,
            &SemiPhysicalPositiveDisplacementPumpEvaluation::mass_flow,
            evaluator_dependencies,
            0.1);
        add_result_equation(
            "outlet_energy", outlet_h,
            &SemiPhysicalPositiveDisplacementPumpEvaluation::
                outlet_enthalpy,
            evaluator_dependencies,
            1.0e5);
        add_result_equation(
            "shaft_power", shaft_w,
            &SemiPhysicalPositiveDisplacementPumpEvaluation::shaft_power,
            evaluator_dependencies,
            1.0e3);
        add_result_equation(
            "ideal_displacement_mass_flow_diagnostic", ideal_mass,
            &SemiPhysicalPositiveDisplacementPumpEvaluation::
                ideal_displacement_mass_flow,
            evaluator_dependencies,
            0.1);
        add_result_equation(
            "leakage_mass_flow_diagnostic", leakage_mass,
            &SemiPhysicalPositiveDisplacementPumpEvaluation::
                leakage_mass_flow,
            evaluator_dependencies,
            0.1);
    }

private:
    ComponentModelDescriptor descriptor_;
};

}  // namespace

EvaluationStatus evaluate_semi_physical_positive_displacement_pump(
    const physics::PropertyPackage& properties,
    double inlet_pressure,
    double inlet_enthalpy,
    double outlet_pressure,
    double angular_speed,
    const SemiPhysicalPositiveDisplacementPumpParameters& parameters,
    SemiPhysicalPositiveDisplacementPumpEvaluation& result) {
    result = {};
    if (!std::isfinite(inlet_pressure) || inlet_pressure <= 0.0 ||
        !std::isfinite(inlet_enthalpy) ||
        !std::isfinite(outlet_pressure) ||
        outlet_pressure < inlet_pressure ||
        !std::isfinite(angular_speed) || angular_speed <= 0.0) {
        return EvaluationStatus::recoverable(
            "positive-displacement pump requires finite positive inlet "
            "state and speed with outlet pressure not below inlet pressure");
    }
    if (!std::isfinite(
            parameters.displacement_volume_per_revolution) ||
        parameters.displacement_volume_per_revolution <= 0.0 ||
        !std::isfinite(parameters.leakage_area) ||
        parameters.leakage_area < 0.0 ||
        !std::isfinite(parameters.leakage_discharge_coefficient) ||
        parameters.leakage_discharge_coefficient < 0.0 ||
        parameters.leakage_discharge_coefficient > 1.0 ||
        !std::isfinite(parameters.isentropic_efficiency) ||
        parameters.isentropic_efficiency <= 0.0 ||
        parameters.isentropic_efficiency > 1.0) {
        return EvaluationStatus::fatal(
            "positive-displacement pump parameters are outside their "
            "physical domains");
    }
    const auto inlet = properties.state_ph(
        inlet_pressure, inlet_enthalpy);
    if (!inlet.ok()) return property_failure(inlet);
    const double density = inlet.state.density_kg_m3;
    if (!std::isfinite(density) || density <= 0.0) {
        return EvaluationStatus::recoverable(
            "positive-displacement pump inlet density must be positive");
    }
    const double pressure_rise = outlet_pressure - inlet_pressure;
    result.ideal_displacement_mass_flow =
        density * parameters.displacement_volume_per_revolution *
        angular_speed / (2.0 * std::numbers::pi);
    result.leakage_mass_flow =
        parameters.leakage_discharge_coefficient *
        parameters.leakage_area *
        std::sqrt(2.0 * density * pressure_rise);
    result.mass_flow = result.ideal_displacement_mass_flow -
        result.leakage_mass_flow;
    if (!std::isfinite(result.mass_flow) || result.mass_flow <= 0.0) {
        return EvaluationStatus::recoverable(
            "positive-displacement pump leakage equals or exceeds its "
            "ideal displacement capacity");
    }
    const auto isentropic = properties.state_ps(
        outlet_pressure, inlet.state.entropy_j_kg_k);
    if (!isentropic.ok()) return property_failure(isentropic);
    result.outlet_enthalpy = inlet_enthalpy +
        (isentropic.state.enthalpy_j_kg - inlet_enthalpy) /
            parameters.isentropic_efficiency;
    result.shaft_power = result.mass_flow *
        (result.outlet_enthalpy - inlet_enthalpy);
    if (!std::isfinite(result.outlet_enthalpy) ||
        !std::isfinite(result.shaft_power) ||
        result.shaft_power < 0.0) {
        return EvaluationStatus::recoverable(
            "positive-displacement pump produced a nonphysical energy "
            "state");
    }
    return EvaluationStatus::success();
}

void register_turbomachinery_component_models(
    ComponentRegistry& registry) {
    registry.register_model(
        std::make_shared<TurbomachineryModel>(
            "compressor.fluid.isentropic_efficiency", true));
    registry.register_model(
        std::make_shared<TurbomachineryModel>(
            "pump.fluid.isentropic_efficiency", true));
    registry.register_model(std::make_shared<PumpMapModel>());
    registry.register_model(std::make_shared<
        SemiPhysicalPositiveDisplacementPumpModel>());
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
