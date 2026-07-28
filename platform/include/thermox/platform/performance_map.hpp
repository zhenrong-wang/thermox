#pragma once

#include <map>
#include <memory>
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

struct ConditionedMapLayer {
    double condition_coordinate{0.0};
    std::shared_ptr<const PerformanceMap> map;
};

struct ConditionedMapEvaluation {
    MapEvaluation map;
    std::vector<double> condition_derivatives;
    bool condition_extrapolated{false};
};

// A third-coordinate family of ordinary two-coordinate maps.
//
// Every layer retains the non-rectangular primary/family structure of
// PerformanceMap. Evaluation interpolates complete map outputs and
// derivatives between adjacent condition layers.
class ConditionedPerformanceMap {
public:
    ConditionedPerformanceMap(
        MapVariable condition_variable,
        std::vector<ConditionedMapLayer> layers,
        MapExtrapolationPolicy condition_extrapolation =
            MapExtrapolationPolicy::reject);

    [[nodiscard]] const MapVariable& condition_variable()
        const noexcept;
    [[nodiscard]] const std::vector<ConditionedMapLayer>& layers()
        const noexcept;
    [[nodiscard]] MapExtrapolationPolicy
    condition_extrapolation() const noexcept;

    [[nodiscard]] ConditionedMapEvaluation evaluate(
        double primary_coordinate,
        double family_coordinate,
        double condition_coordinate) const;

private:
    MapVariable condition_variable_;
    std::vector<ConditionedMapLayer> layers_;
    MapExtrapolationPolicy condition_extrapolation_;
};

inline constexpr const char* performance_map_artifact_type =
    "thermox.performance_map";
inline constexpr const char* performance_map_artifact_schema_v1 =
    "thermox.performance_map/v1";
inline constexpr const char* performance_map_artifact_schema_v2 =
    "thermox.performance_map/v2";

// Immutable identity and payload for user-supplied engineering data. The
// checksum identifies the canonical source payload; revision is a
// user-facing data revision and is not used as a substitute for identity.
struct PerformanceMapArtifact {
    std::string id;
    std::string schema_version;
    std::string revision;
    std::string checksum_sha256;
    std::shared_ptr<const PerformanceMap> map;
    std::shared_ptr<const ConditionedPerformanceMap> conditioned_map;
};

class PerformanceMapRegistry {
public:
    void register_artifact(PerformanceMapArtifact artifact);
    [[nodiscard]] std::shared_ptr<const PerformanceMapArtifact>
    require_artifact(const std::string& id) const;
    [[nodiscard]] bool contains(const std::string& id) const;
    [[nodiscard]] std::vector<std::string> ids() const;

private:
    std::map<
        std::string,
        std::shared_ptr<const PerformanceMapArtifact>>
        artifacts_;
};

}  // namespace thermox::platform
