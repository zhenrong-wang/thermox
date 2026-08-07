#include "thermox/platform/performance_map.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <sstream>
#include <utility>

namespace thermox::platform {

namespace {

struct Bracket {
    std::size_t lower{0};
    std::size_t upper{1};
    double coordinate{0.0};
    bool extrapolated{false};
    bool clamped{false};
};

Bracket bracket(
    const std::vector<double>& coordinates,
    double coordinate,
    MapExtrapolationPolicy policy,
    const char* axis_name) {
    if (!std::isfinite(coordinate)) {
        throw MapDomainError(
            std::string(axis_name) +
            " coordinate must be finite");
    }

    const auto below = coordinate < coordinates.front();
    const auto above = coordinate > coordinates.back();
    if ((below || above) &&
        policy == MapExtrapolationPolicy::reject) {
        std::ostringstream message;
        message << axis_name << " coordinate " << coordinate
                << " is outside [" << coordinates.front() << ", "
                << coordinates.back() << "]";
        throw MapDomainError(message.str());
    }

    if (below) {
        return {
            0,
            1,
            policy == MapExtrapolationPolicy::clamp
                ? coordinates.front()
                : coordinate,
            true,
            policy == MapExtrapolationPolicy::clamp,
        };
    }
    if (above) {
        return {
            coordinates.size() - 2,
            coordinates.size() - 1,
            policy == MapExtrapolationPolicy::clamp
                ? coordinates.back()
                : coordinate,
            true,
            policy == MapExtrapolationPolicy::clamp,
        };
    }

    const auto upper = std::upper_bound(
        coordinates.begin(), coordinates.end(), coordinate);
    if (upper == coordinates.end()) {
        return {
            coordinates.size() - 2,
            coordinates.size() - 1,
            coordinate,
            false,
            false,
        };
    }
    if (upper == coordinates.begin()) {
        return {0, 1, coordinate, false, false};
    }
    const auto upper_index = static_cast<std::size_t>(
        std::distance(coordinates.begin(), upper));
    return {
        upper_index - 1,
        upper_index,
        coordinate,
        false,
        false,
    };
}

struct CurveEvaluation {
    std::vector<double> outputs;
    std::vector<double> derivatives;
    bool extrapolated{false};
};

CurveEvaluation evaluate_curve(
    const MapCurve& curve,
    double coordinate,
    MapExtrapolationPolicy policy) {
    std::vector<double> coordinates;
    coordinates.reserve(curve.samples.size());
    for (const auto& sample : curve.samples) {
        coordinates.push_back(sample.coordinate);
    }
    const auto selected = bracket(
        coordinates, coordinate, policy, "primary");
    const auto& lower = curve.samples[selected.lower];
    const auto& upper = curve.samples[selected.upper];
    const auto span = upper.coordinate - lower.coordinate;
    const auto fraction =
        (selected.coordinate - lower.coordinate) / span;

    CurveEvaluation result;
    result.outputs.resize(lower.outputs.size());
    result.derivatives.resize(lower.outputs.size());
    result.extrapolated = selected.extrapolated;
    for (std::size_t i = 0; i < lower.outputs.size(); ++i) {
        const auto slope =
            (upper.outputs[i] - lower.outputs[i]) / span;
        result.outputs[i] =
            lower.outputs[i] +
            fraction * (upper.outputs[i] - lower.outputs[i]);
        result.derivatives[i] =
            selected.clamped ? 0.0 : slope;
    }
    return result;
}

std::vector<double> family_slope_probes(
    const MapCurve& lower,
    const MapCurve& upper,
    MapExtrapolationPolicy primary_extrapolation) {
    const double minimum =
        primary_extrapolation == MapExtrapolationPolicy::reject
        ? std::max(
              lower.samples.front().coordinate,
              upper.samples.front().coordinate)
        : std::min(
              lower.samples.front().coordinate,
              upper.samples.front().coordinate);
    const double maximum =
        primary_extrapolation == MapExtrapolationPolicy::reject
        ? std::min(
              lower.samples.back().coordinate,
              upper.samples.back().coordinate)
        : std::max(
              lower.samples.back().coordinate,
              upper.samples.back().coordinate);
    if (maximum < minimum) return {};
    std::vector<double> probes{minimum, maximum};
    const auto append = [&](const MapCurve& curve) {
        for (const auto& sample : curve.samples) {
            if (sample.coordinate >= minimum &&
                sample.coordinate <= maximum) {
                probes.push_back(sample.coordinate);
            }
        }
    };
    append(lower);
    append(upper);
    std::sort(probes.begin(), probes.end());
    probes.erase(
        std::unique(probes.begin(), probes.end()), probes.end());
    return probes;
}

void validate(
    const MapVariable& primary_variable,
    const MapVariable& family_variable,
    const std::vector<MapVariable>& output_variables,
    const std::vector<MapCurve>& curves,
    MapExtrapolationPolicy primary_extrapolation) {
    const auto valid_variable = [](const MapVariable& variable) {
        return !variable.name.empty() &&
            !variable.dimension.empty();
    };
    if (!valid_variable(primary_variable) ||
        !valid_variable(family_variable) ||
        primary_variable.name == family_variable.name) {
        throw std::invalid_argument(
            "performance map axes must have distinct non-empty "
            "names and dimensions");
    }
    if (output_variables.empty()) {
        throw std::invalid_argument(
            "performance map must declare at least one output");
    }
    std::set<std::string> unique_names;
    unique_names.insert(primary_variable.name);
    unique_names.insert(family_variable.name);
    for (const auto& variable : output_variables) {
        if (!valid_variable(variable) ||
            !unique_names.insert(variable.name).second) {
            throw std::invalid_argument(
                "performance map variables must have unique "
                "non-empty names and dimensions");
        }
    }
    if (curves.size() < 2) {
        throw std::invalid_argument(
            "performance map must contain at least two curves");
    }

    double previous_family = 0.0;
    for (std::size_t curve_index = 0;
         curve_index < curves.size();
         ++curve_index) {
        const auto& curve = curves[curve_index];
        if (!std::isfinite(curve.family_coordinate) ||
            (curve_index != 0 &&
             curve.family_coordinate <= previous_family)) {
            throw std::invalid_argument(
                "performance map family coordinates must be "
                "finite and strictly increasing");
        }
        previous_family = curve.family_coordinate;
        if (curve.samples.size() < 2) {
            throw std::invalid_argument(
                "every performance map curve must contain at "
                "least two samples");
        }

        double previous_coordinate = 0.0;
        for (std::size_t sample_index = 0;
             sample_index < curve.samples.size();
             ++sample_index) {
            const auto& sample = curve.samples[sample_index];
            if (!std::isfinite(sample.coordinate) ||
                (sample_index != 0 &&
                 sample.coordinate <= previous_coordinate)) {
                throw std::invalid_argument(
                    "performance map sample coordinates must be "
                    "finite and strictly increasing");
            }
            previous_coordinate = sample.coordinate;
            if (sample.outputs.size() !=
                output_variables.size()) {
                throw std::invalid_argument(
                    "performance map sample output dimension "
                    "does not match its declared outputs");
            }
            if (!std::all_of(
                    sample.outputs.begin(),
                    sample.outputs.end(),
                    [](double value) {
                        return std::isfinite(value);
                    })) {
                throw std::invalid_argument(
                    "performance map outputs must be finite");
            }
            if (sample_index != 0U) {
                const auto& previous =
                    curve.samples[sample_index - 1U];
                const double span =
                    sample.coordinate - previous.coordinate;
                if (!std::isfinite(span)) {
                    throw std::invalid_argument(
                        "performance map primary-coordinate span is "
                        "non-finite");
                }
                for (std::size_t output = 0;
                     output < sample.outputs.size(); ++output) {
                    if (!std::isfinite(
                            (sample.outputs[output] -
                             previous.outputs[output]) / span)) {
                        throw std::invalid_argument(
                            "performance map primary slope is "
                            "non-finite");
                    }
                }
            }
        }
    }

    for (std::size_t index = 1; index < curves.size(); ++index) {
        const auto& lower = curves[index - 1U];
        const auto& upper = curves[index];
        const double overlap_minimum = std::max(
            lower.samples.front().coordinate,
            upper.samples.front().coordinate);
        const double overlap_maximum = std::min(
            lower.samples.back().coordinate,
            upper.samples.back().coordinate);
        if (!std::isfinite(overlap_maximum - overlap_minimum)) {
            throw std::invalid_argument(
                "adjacent performance-map primary domains have a "
                "non-finite separation or overlap");
        }
        if (primary_extrapolation == MapExtrapolationPolicy::reject &&
            overlap_maximum <= overlap_minimum) {
            throw std::invalid_argument(
                "adjacent performance-map curves have no positive shared "
                "primary-coordinate domain under reject extrapolation");
        }
        const double family_span =
            upper.family_coordinate - lower.family_coordinate;
        if (!std::isfinite(family_span)) {
            throw std::invalid_argument(
                "performance map family-coordinate span is non-finite");
        }
        for (const double probe : family_slope_probes(
                 lower, upper, primary_extrapolation)) {
            const auto lower_value = evaluate_curve(
                lower, probe, primary_extrapolation);
            const auto upper_value = evaluate_curve(
                upper, probe, primary_extrapolation);
            for (std::size_t output = 0;
                 output < lower_value.outputs.size(); ++output) {
                if (!std::isfinite(
                        (upper_value.outputs[output] -
                         lower_value.outputs[output]) / family_span) ||
                    !std::isfinite(lower_value.derivatives[output]) ||
                    !std::isfinite(upper_value.derivatives[output])) {
                    throw std::invalid_argument(
                        "performance map interpolation derivative is "
                        "non-finite");
                }
            }
        }
    }
}

MapQualityReport assess_quality(
    const std::vector<MapVariable>& output_variables,
    const std::vector<MapCurve>& curves,
    MapExtrapolationPolicy primary_extrapolation,
    MapExtrapolationPolicy family_extrapolation) {
    MapQualityReport report;
    report.curve_count = curves.size();
    report.family_minimum = curves.front().family_coordinate;
    report.family_maximum = curves.back().family_coordinate;
    report.common_primary_minimum =
        curves.front().samples.front().coordinate;
    report.common_primary_maximum =
        curves.front().samples.back().coordinate;
    report.minimum_adjacent_primary_overlap =
        std::numeric_limits<double>::infinity();
    for (const auto& output : output_variables) {
        report.outputs.push_back({output.name});
    }
    for (const auto& curve : curves) {
        report.sample_count += curve.samples.size();
        report.common_primary_minimum = std::max(
            report.common_primary_minimum,
            curve.samples.front().coordinate);
        report.common_primary_maximum = std::min(
            report.common_primary_maximum,
            curve.samples.back().coordinate);
        std::vector<double> previous_slopes;
        for (std::size_t sample = 0;
             sample < curve.samples.size(); ++sample) {
            const auto& point = curve.samples[sample];
            for (std::size_t output = 0;
                 output < point.outputs.size(); ++output) {
                report.outputs[output].minimum = std::min(
                    report.outputs[output].minimum,
                    point.outputs[output]);
                report.outputs[output].maximum = std::max(
                    report.outputs[output].maximum,
                    point.outputs[output]);
            }
            if (sample == 0U) continue;
            const auto& previous = curve.samples[sample - 1U];
            const double span = point.coordinate - previous.coordinate;
            std::vector<double> slopes(point.outputs.size());
            for (std::size_t output = 0;
                 output < point.outputs.size(); ++output) {
                slopes[output] =
                    (point.outputs[output] - previous.outputs[output]) /
                    span;
                report.outputs[output]
                    .maximum_absolute_primary_slope = std::max(
                        report.outputs[output]
                            .maximum_absolute_primary_slope,
                        std::abs(slopes[output]));
                if (!previous_slopes.empty()) {
                    report.outputs[output]
                        .maximum_absolute_primary_slope_jump = std::max(
                            report.outputs[output]
                                .maximum_absolute_primary_slope_jump,
                            std::abs(
                                slopes[output] -
                                previous_slopes[output]));
                }
            }
            previous_slopes = std::move(slopes);
        }
    }
    report.has_global_common_primary_domain =
        report.common_primary_maximum >
        report.common_primary_minimum;
    for (std::size_t index = 1; index < curves.size(); ++index) {
        const auto& lower = curves[index - 1U];
        const auto& upper = curves[index];
        const double overlap_minimum = std::max(
            lower.samples.front().coordinate,
            upper.samples.front().coordinate);
        const double overlap_maximum = std::min(
            lower.samples.back().coordinate,
            upper.samples.back().coordinate);
        report.minimum_adjacent_primary_overlap = std::min(
            report.minimum_adjacent_primary_overlap,
            overlap_maximum - overlap_minimum);
        const double span =
            upper.family_coordinate - lower.family_coordinate;
        for (const double probe : family_slope_probes(
                 lower, upper, primary_extrapolation)) {
            const auto lower_value = evaluate_curve(
                lower, probe, primary_extrapolation);
            const auto upper_value = evaluate_curve(
                upper, probe, primary_extrapolation);
            for (std::size_t output = 0;
                 output < lower_value.outputs.size(); ++output) {
                report.outputs[output].maximum_absolute_family_slope =
                    std::max(
                        report.outputs[output]
                            .maximum_absolute_family_slope,
                        std::abs(
                            (upper_value.outputs[output] -
                             lower_value.outputs[output]) / span));
            }
        }
    }
    if (!report.has_global_common_primary_domain) {
        report.advisories.push_back(
            "no primary-coordinate interval is shared by every family "
            "curve; usable primary bounds vary with family coordinate");
    }
    if (primary_extrapolation == MapExtrapolationPolicy::linear) {
        report.advisories.push_back(
            "linear primary-coordinate extrapolation is enabled");
    }
    if (family_extrapolation == MapExtrapolationPolicy::linear) {
        report.advisories.push_back(
            "linear family-coordinate extrapolation is enabled");
    }
    return report;
}

}  // namespace

PerformanceMap::PerformanceMap(
    MapVariable primary_variable,
    MapVariable family_variable,
    std::vector<MapVariable> output_variables,
    std::vector<MapCurve> curves,
    MapExtrapolationPolicy primary_extrapolation,
    MapExtrapolationPolicy family_extrapolation)
    : primary_variable_(std::move(primary_variable)),
      family_variable_(std::move(family_variable)),
      output_variables_(std::move(output_variables)),
      curves_(std::move(curves)),
      primary_extrapolation_(primary_extrapolation),
      family_extrapolation_(family_extrapolation) {
    validate(
        primary_variable_,
        family_variable_,
        output_variables_,
        curves_,
        primary_extrapolation_);
    quality_report_ = assess_quality(
        output_variables_, curves_, primary_extrapolation_,
        family_extrapolation_);
}

const MapVariable& PerformanceMap::primary_variable()
    const noexcept {
    return primary_variable_;
}

const MapVariable& PerformanceMap::family_variable()
    const noexcept {
    return family_variable_;
}

const std::vector<MapVariable>&
PerformanceMap::output_variables() const noexcept {
    return output_variables_;
}

const std::vector<MapCurve>& PerformanceMap::curves()
    const noexcept {
    return curves_;
}

MapExtrapolationPolicy PerformanceMap::primary_extrapolation()
    const noexcept {
    return primary_extrapolation_;
}

MapExtrapolationPolicy PerformanceMap::family_extrapolation()
    const noexcept {
    return family_extrapolation_;
}

const MapQualityReport& PerformanceMap::quality_report()
    const noexcept {
    return quality_report_;
}

MapEvaluation PerformanceMap::evaluate(
    double primary_coordinate,
    double family_coordinate) const {
    std::vector<double> family_coordinates;
    family_coordinates.reserve(curves_.size());
    for (const auto& curve : curves_) {
        family_coordinates.push_back(curve.family_coordinate);
    }
    const auto selected = bracket(
        family_coordinates,
        family_coordinate,
        family_extrapolation_,
        "family");
    const auto lower = evaluate_curve(
        curves_[selected.lower],
        primary_coordinate,
        primary_extrapolation_);
    const auto upper = evaluate_curve(
        curves_[selected.upper],
        primary_coordinate,
        primary_extrapolation_);

    const auto family_span =
        curves_[selected.upper].family_coordinate -
        curves_[selected.lower].family_coordinate;
    const auto fraction =
        (selected.coordinate -
         curves_[selected.lower].family_coordinate) /
        family_span;

    MapEvaluation result;
    result.outputs.resize(output_variables_.size());
    result.primary_derivatives.resize(output_variables_.size());
    result.family_derivatives.resize(output_variables_.size());
    result.primary_extrapolated =
        lower.extrapolated || upper.extrapolated;
    result.family_extrapolated = selected.extrapolated;
    for (std::size_t i = 0;
         i < output_variables_.size();
         ++i) {
        result.outputs[i] =
            lower.outputs[i] +
            fraction * (upper.outputs[i] - lower.outputs[i]);
        result.primary_derivatives[i] =
            lower.derivatives[i] +
            fraction *
                (upper.derivatives[i] - lower.derivatives[i]);
        result.family_derivatives[i] =
            selected.clamped
            ? 0.0
            : (upper.outputs[i] - lower.outputs[i]) /
                  family_span;
    }
    return result;
}

ConditionedPerformanceMap::ConditionedPerformanceMap(
    MapVariable condition_variable,
    std::vector<ConditionedMapLayer> layers,
    MapExtrapolationPolicy condition_extrapolation)
    : condition_variable_(std::move(condition_variable)),
      layers_(std::move(layers)),
      condition_extrapolation_(condition_extrapolation) {
    if (condition_variable_.name.empty() ||
        condition_variable_.dimension.empty()) {
        throw std::invalid_argument(
            "conditioned performance map must declare a non-empty "
            "condition variable");
    }
    if (layers_.size() < 2) {
        throw std::invalid_argument(
            "conditioned performance map must contain at least "
            "two layers");
    }
    const auto& reference = layers_.front().map;
    if (!reference) {
        throw std::invalid_argument(
            "conditioned performance map layer has no map payload");
    }
    if (condition_variable_.name ==
            reference->primary_variable().name ||
        condition_variable_.name ==
            reference->family_variable().name ||
        std::any_of(
            reference->output_variables().begin(),
            reference->output_variables().end(),
            [&](const auto& output) {
                return output.name == condition_variable_.name;
            })) {
        throw std::invalid_argument(
            "conditioned performance map variable names must be "
            "unique");
    }
    const auto same_variable = [](
        const MapVariable& left,
        const MapVariable& right) {
        return left.name == right.name &&
            left.dimension == right.dimension;
    };
    double previous_coordinate = 0.0;
    for (std::size_t index = 0; index < layers_.size();
         ++index) {
        const auto& layer = layers_[index];
        if (!std::isfinite(layer.condition_coordinate) ||
            (index != 0 &&
             layer.condition_coordinate <= previous_coordinate)) {
            throw std::invalid_argument(
                "conditioned performance map coordinates must be "
                "finite and strictly increasing");
        }
        previous_coordinate = layer.condition_coordinate;
        if (!layer.map) {
            throw std::invalid_argument(
                "conditioned performance map layer has no map "
                "payload");
        }
        if (!same_variable(
                layer.map->primary_variable(),
                reference->primary_variable()) ||
            !same_variable(
                layer.map->family_variable(),
                reference->family_variable()) ||
            layer.map->output_variables().size() !=
                reference->output_variables().size() ||
            layer.map->primary_extrapolation() !=
                reference->primary_extrapolation() ||
            layer.map->family_extrapolation() !=
                reference->family_extrapolation()) {
            throw std::invalid_argument(
                "conditioned performance map layers must share "
                "axis, output, and extrapolation contracts");
        }
        for (std::size_t output = 0;
             output < reference->output_variables().size();
             ++output) {
            if (!same_variable(
                    layer.map->output_variables()[output],
                    reference->output_variables()[output])) {
                throw std::invalid_argument(
                    "conditioned performance map layers must share "
                    "axis, output, and extrapolation contracts");
            }
        }
    }
}

const MapVariable&
ConditionedPerformanceMap::condition_variable() const noexcept {
    return condition_variable_;
}

const std::vector<ConditionedMapLayer>&
ConditionedPerformanceMap::layers() const noexcept {
    return layers_;
}

MapExtrapolationPolicy
ConditionedPerformanceMap::condition_extrapolation()
    const noexcept {
    return condition_extrapolation_;
}

ConditionedMapEvaluation ConditionedPerformanceMap::evaluate(
    double primary_coordinate,
    double family_coordinate,
    double condition_coordinate) const {
    std::vector<double> coordinates;
    coordinates.reserve(layers_.size());
    for (const auto& layer : layers_) {
        coordinates.push_back(layer.condition_coordinate);
    }
    const auto selected = bracket(
        coordinates, condition_coordinate,
        condition_extrapolation_, "condition");
    const auto lower = layers_[selected.lower].map->evaluate(
        primary_coordinate, family_coordinate);
    const auto upper = layers_[selected.upper].map->evaluate(
        primary_coordinate, family_coordinate);
    const double span =
        layers_[selected.upper].condition_coordinate -
        layers_[selected.lower].condition_coordinate;
    const double fraction =
        (selected.coordinate -
         layers_[selected.lower].condition_coordinate) /
        span;

    ConditionedMapEvaluation result;
    result.map.outputs.resize(lower.outputs.size());
    result.map.primary_derivatives.resize(
        lower.primary_derivatives.size());
    result.map.family_derivatives.resize(
        lower.family_derivatives.size());
    result.condition_derivatives.resize(lower.outputs.size());
    result.map.primary_extrapolated =
        lower.primary_extrapolated ||
        upper.primary_extrapolated;
    result.map.family_extrapolated =
        lower.family_extrapolated ||
        upper.family_extrapolated;
    result.condition_extrapolated = selected.extrapolated;
    for (std::size_t index = 0;
         index < lower.outputs.size(); ++index) {
        result.map.outputs[index] =
            lower.outputs[index] +
            fraction *
                (upper.outputs[index] - lower.outputs[index]);
        result.map.primary_derivatives[index] =
            lower.primary_derivatives[index] +
            fraction *
                (upper.primary_derivatives[index] -
                 lower.primary_derivatives[index]);
        result.map.family_derivatives[index] =
            lower.family_derivatives[index] +
            fraction *
                (upper.family_derivatives[index] -
                 lower.family_derivatives[index]);
        result.condition_derivatives[index] =
            selected.clamped
            ? 0.0
            : (upper.outputs[index] - lower.outputs[index]) /
                  span;
    }
    return result;
}

void EngineeringArtifactRegistry::register_artifact(
    PerformanceMapArtifact artifact) {
    register_artifact(
        std::shared_ptr<const EngineeringArtifact>(
            std::make_shared<const PerformanceMapArtifact>(
                std::move(artifact))));
}

void PerformanceMapArtifact::validate() const {
    if (schema_version !=
            performance_map_artifact_schema_v1 &&
        schema_version !=
            performance_map_artifact_schema_v2) {
        throw std::invalid_argument(
            "performance-map artifact '" + id +
            "' has unsupported schema version: " +
            schema_version);
    }
    const bool ordinary =
        schema_version ==
        performance_map_artifact_schema_v1;
    if ((ordinary && (!map || conditioned_map)) ||
        (!ordinary && (map || !conditioned_map))) {
        throw std::invalid_argument(
            "performance-map artifact '" + id +
            "' payload does not match its schema version");
    }
}

}  // namespace thermox::platform
