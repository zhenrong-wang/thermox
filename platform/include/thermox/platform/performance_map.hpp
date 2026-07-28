#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace thermox::platform {

enum class MapExtrapolationPolicy {
    reject,
    clamp,
    linear,
};

struct MapVariable {
    std::string name;
    std::string dimension;
};

struct MapSample {
    double coordinate{0.0};
    std::vector<double> outputs;
};

struct MapCurve {
    double family_coordinate{0.0};
    std::vector<MapSample> samples;
};

struct MapEvaluation {
    std::vector<double> outputs;
    std::vector<double> primary_derivatives;
    std::vector<double> family_derivatives;
    bool primary_extrapolated{false};
    bool family_extrapolated{false};
};

class MapDomainError : public std::domain_error {
public:
    using std::domain_error::domain_error;
};

// A validated two-coordinate family of piecewise-linear curves.
//
// Curves need not share primary-coordinate sample locations. Evaluation
// first interpolates along each bracketing curve and then interpolates
// between their family coordinates. This matches common engineering
// characteristic data such as speed lines while remaining domain-neutral.
class PerformanceMap {
public:
    PerformanceMap(
        MapVariable primary_variable,
        MapVariable family_variable,
        std::vector<MapVariable> output_variables,
        std::vector<MapCurve> curves,
        MapExtrapolationPolicy primary_extrapolation =
            MapExtrapolationPolicy::reject,
        MapExtrapolationPolicy family_extrapolation =
            MapExtrapolationPolicy::reject);

    [[nodiscard]] const MapVariable& primary_variable()
        const noexcept;
    [[nodiscard]] const MapVariable& family_variable()
        const noexcept;
    [[nodiscard]] const std::vector<MapVariable>& output_variables()
        const noexcept;
    [[nodiscard]] const std::vector<MapCurve>& curves()
        const noexcept;
    [[nodiscard]] MapExtrapolationPolicy primary_extrapolation()
        const noexcept;
    [[nodiscard]] MapExtrapolationPolicy family_extrapolation()
        const noexcept;

    [[nodiscard]] MapEvaluation evaluate(
        double primary_coordinate,
        double family_coordinate) const;

private:
    MapVariable primary_variable_;
    MapVariable family_variable_;
    std::vector<MapVariable> output_variables_;
    std::vector<MapCurve> curves_;
    MapExtrapolationPolicy primary_extrapolation_;
    MapExtrapolationPolicy family_extrapolation_;
};

}  // namespace thermox::platform
