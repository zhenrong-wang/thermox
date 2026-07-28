#include "thermox/platform/performance_map.hpp"

#include <algorithm>
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

void validate(
    const MapVariable& primary_variable,
    const MapVariable& family_variable,
    const std::vector<MapVariable>& output_variables,
    const std::vector<MapCurve>& curves) {
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
        }
    }
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
        curves_);
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

}  // namespace thermox::platform
