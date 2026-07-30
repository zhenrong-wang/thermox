#pragma once

#include "thermox/platform/performance_map.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace thermox::platform::performance_map_continuation {

struct Seed {
    double primary_coordinate{0.0};
    double family_coordinate{0.0};
    double condition_coordinate{0.0};
};

inline std::optional<std::pair<double, double>>
common_coordinate_seed(
    const std::vector<const PerformanceMap*>& maps) {
    if (maps.empty()) return std::nullopt;
    double family_lower =
        -std::numeric_limits<double>::infinity();
    double family_upper =
        std::numeric_limits<double>::infinity();
    bool family_constrained = false;
    double fallback_family_lower{0.0};
    double fallback_family_upper{0.0};
    std::vector<double> breakpoints;
    for (std::size_t map_index = 0;
         map_index < maps.size(); ++map_index) {
        const auto* map = maps[map_index];
        if (map == nullptr || map->curves().size() < 2) {
            return std::nullopt;
        }
        if (map_index == 0) {
            fallback_family_lower =
                map->curves().front().family_coordinate;
            fallback_family_upper =
                map->curves().back().family_coordinate;
        }
        if (map->family_extrapolation() ==
            MapExtrapolationPolicy::reject) {
            family_constrained = true;
            family_lower = std::max(
                family_lower,
                map->curves().front().family_coordinate);
            family_upper = std::min(
                family_upper,
                map->curves().back().family_coordinate);
        }
        for (const auto& curve : map->curves()) {
            breakpoints.push_back(curve.family_coordinate);
        }
    }
    if (!family_constrained) {
        family_lower = fallback_family_lower;
        family_upper = fallback_family_upper;
    }
    if (!(family_lower < family_upper)) {
        return std::nullopt;
    }
    breakpoints.push_back(family_lower);
    breakpoints.push_back(family_upper);
    std::sort(breakpoints.begin(), breakpoints.end());
    breakpoints.erase(
        std::unique(
            breakpoints.begin(), breakpoints.end()),
        breakpoints.end());

    for (std::size_t interval = 0;
         interval + 1 < breakpoints.size(); ++interval) {
        const double lower =
            std::max(breakpoints[interval], family_lower);
        const double upper =
            std::min(
                breakpoints[interval + 1], family_upper);
        if (!(lower < upper)) continue;
        const double family = 0.5 * (lower + upper);
        double primary_lower =
            -std::numeric_limits<double>::infinity();
        double primary_upper =
            std::numeric_limits<double>::infinity();
        bool primary_constrained = false;
        double fallback_primary_lower{0.0};
        double fallback_primary_upper{0.0};
        bool has_primary_fallback = false;
        for (const auto* map : maps) {
            auto upper_curve = std::upper_bound(
                map->curves().begin(), map->curves().end(),
                family,
                [](double coordinate,
                   const MapCurve& curve) {
                    return coordinate <
                        curve.family_coordinate;
                });
            if (upper_curve == map->curves().begin()) {
                if (map->family_extrapolation() ==
                    MapExtrapolationPolicy::reject) {
                    primary_lower = 1.0;
                    primary_upper = 0.0;
                    break;
                }
                ++upper_curve;
            } else if (
                upper_curve == map->curves().end()) {
                if (map->family_extrapolation() ==
                    MapExtrapolationPolicy::reject) {
                    primary_lower = 1.0;
                    primary_upper = 0.0;
                    break;
                }
                --upper_curve;
            }
            const auto& high = *upper_curve;
            const auto& low = *(upper_curve - 1);
            if (!has_primary_fallback) {
                fallback_primary_lower =
                    low.samples.front().coordinate;
                fallback_primary_upper =
                    low.samples.back().coordinate;
                has_primary_fallback = true;
            }
            if (map->primary_extrapolation() ==
                MapExtrapolationPolicy::reject) {
                primary_constrained = true;
                primary_lower = std::max(
                    primary_lower,
                    std::max(
                        low.samples.front().coordinate,
                        high.samples.front().coordinate));
                primary_upper = std::min(
                    primary_upper,
                    std::min(
                        low.samples.back().coordinate,
                        high.samples.back().coordinate));
            }
        }
        if (!primary_constrained &&
            has_primary_fallback) {
            primary_lower = fallback_primary_lower;
            primary_upper = fallback_primary_upper;
        }
        if (primary_lower < primary_upper &&
            std::isfinite(primary_lower) &&
            std::isfinite(primary_upper)) {
            return std::pair<double, double>{
                0.5 * (primary_lower + primary_upper),
                family};
        }
    }
    return std::nullopt;
}

inline Seed seed(
    const PerformanceMapArtifact& artifact,
    bool conditioned) {
    if (!conditioned) {
        const auto coordinates =
            common_coordinate_seed({artifact.map.get()});
        if (!coordinates.has_value()) {
            throw std::invalid_argument(
                "performance-map artifact '" + artifact.id +
                "' has no common in-domain continuation seed");
        }
        return {
            coordinates->first, coordinates->second, 0.0};
    }

    const auto& layers = artifact.conditioned_map->layers();
    for (std::size_t layer = 0;
         layer + 1 < layers.size(); ++layer) {
        const auto coordinates = common_coordinate_seed(
            {layers[layer].map.get(),
             layers[layer + 1].map.get()});
        if (!coordinates.has_value()) continue;
        return {
            coordinates->first,
            coordinates->second,
            0.5 *
                (layers[layer].condition_coordinate +
                 layers[layer + 1].condition_coordinate)};
    }
    throw std::invalid_argument(
        "conditioned performance-map artifact '" +
        artifact.id +
        "' has no common in-domain continuation seed");
}

inline std::shared_ptr<const PerformanceMap>
linear_surface(const PerformanceMap& source) {
    return std::make_shared<const PerformanceMap>(
        source.primary_variable(),
        source.family_variable(),
        source.output_variables(),
        source.curves(),
        MapExtrapolationPolicy::linear,
        MapExtrapolationPolicy::linear);
}

inline PerformanceMapArtifact linear_extension(
    const PerformanceMapArtifact& source,
    bool conditioned) {
    PerformanceMapArtifact result = source;
    if (!conditioned) {
        result.map = linear_surface(*source.map);
        result.conditioned_map.reset();
        return result;
    }
    std::vector<ConditionedMapLayer> layers;
    layers.reserve(source.conditioned_map->layers().size());
    for (const auto& layer :
         source.conditioned_map->layers()) {
        layers.push_back({
            layer.condition_coordinate,
            linear_surface(*layer.map)});
    }
    result.map.reset();
    result.conditioned_map = std::make_shared<
        const ConditionedPerformanceMap>(
        source.conditioned_map->condition_variable(),
        std::move(layers),
        MapExtrapolationPolicy::linear);
    return result;
}

}  // namespace thermox::platform::performance_map_continuation
