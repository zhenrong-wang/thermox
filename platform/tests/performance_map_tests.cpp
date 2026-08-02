#include "thermox/platform/performance_map.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

struct CorrelationArtifact final
    : thermox::platform::EngineeringArtifact {
    [[nodiscard]] std::string_view artifact_type()
        const noexcept override {
        return "example.correlation";
    }
    void validate() const override {}
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_close(
    double actual,
    double expected,
    const std::string& message) {
    if (std::abs(actual - expected) > 1.0e-12) {
        throw std::runtime_error(message);
    }
}

thermox::platform::PerformanceMap sample_map(
    thermox::platform::MapExtrapolationPolicy primary =
        thermox::platform::MapExtrapolationPolicy::reject,
    thermox::platform::MapExtrapolationPolicy family =
        thermox::platform::MapExtrapolationPolicy::reject) {
    return {
        {"corrected_flow", "mass_flow"},
        {"corrected_speed", "angular_speed"},
        {
            {"pressure_ratio", "dimensionless"},
            {"efficiency", "dimensionless"},
        },
        {
            {
                100.0,
                {
                    {0.0, {1.0, 10.0}},
                    {10.0, {3.0, 20.0}},
                },
            },
            {
                200.0,
                {
                    {0.0, {2.0, 30.0}},
                    {20.0, {6.0, 50.0}},
                },
            },
        },
        primary,
        family,
    };
}

thermox::platform::PerformanceMapArtifact sample_artifact(
    std::string id = "compressor-map") {
    return {
        std::move(id),
        thermox::platform::performance_map_artifact_schema_v1,
        "vendor-revision-7",
        std::string(64, 'a'),
        std::make_shared<const thermox::platform::PerformanceMap>(
            sample_map()),
    };
}

std::shared_ptr<const thermox::platform::PerformanceMap>
scaled_sample_map(double scale) {
    return std::make_shared<
        const thermox::platform::PerformanceMap>(
        thermox::platform::MapVariable{
            "corrected_flow", "mass_flow"},
        thermox::platform::MapVariable{
            "corrected_speed", "angular_speed"},
        std::vector<thermox::platform::MapVariable>{
            {"pressure_ratio", "dimensionless"},
            {"efficiency", "dimensionless"},
        },
        std::vector<thermox::platform::MapCurve>{
            {100.0,
             {{0.0, {scale, 10.0 * scale}},
              {10.0, {3.0 * scale, 20.0 * scale}}}},
            {200.0,
             {{0.0, {2.0 * scale, 30.0 * scale}},
              {20.0, {6.0 * scale, 50.0 * scale}}}},
        });
}

void test_interpolates_non_rectangular_curve_families() {
    const auto map = sample_map();
    require(
        map.primary_variable().name == "corrected_flow" &&
            map.primary_variable().dimension == "mass_flow" &&
            map.family_variable().name == "corrected_speed" &&
            map.output_variables().at(0).name ==
                "pressure_ratio",
        "map variable metadata must retain names and dimensions");
    const auto value = map.evaluate(5.0, 150.0);
    require(
        value.outputs.size() == 2 &&
            value.primary_derivatives.size() == 2 &&
            value.family_derivatives.size() == 2,
        "map evaluation must preserve output dimensions");
    require_close(
        value.outputs[0], 2.5,
        "pressure-ratio interpolation is incorrect");
    require_close(
        value.outputs[1], 25.0,
        "efficiency interpolation is incorrect");
    require_close(
        value.primary_derivatives[0], 0.2,
        "primary derivative is incorrect");
    require_close(
        value.primary_derivatives[1], 1.0,
        "primary derivative is incorrect");
    require_close(
        value.family_derivatives[0], 0.01,
        "family derivative is incorrect");
    require_close(
        value.family_derivatives[1], 0.2,
        "family derivative is incorrect");
    require(
        !value.primary_extrapolated &&
            !value.family_extrapolated,
        "interior interpolation must not report extrapolation");
}

void test_reject_policy_reports_domain_failures() {
    const auto map = sample_map();
    bool primary_rejected = false;
    try {
        (void)map.evaluate(-1.0, 150.0);
    } catch (const thermox::platform::MapDomainError&) {
        primary_rejected = true;
    }
    require(
        primary_rejected,
        "reject policy must reject an out-of-range primary "
        "coordinate");

    bool family_rejected = false;
    try {
        (void)map.evaluate(5.0, 250.0);
    } catch (const thermox::platform::MapDomainError&) {
        family_rejected = true;
    }
    require(
        family_rejected,
        "reject policy must reject an out-of-range family "
        "coordinate");
}

void test_clamp_policy_has_zero_boundary_derivatives() {
    const auto map = sample_map(
        thermox::platform::MapExtrapolationPolicy::clamp,
        thermox::platform::MapExtrapolationPolicy::clamp);
    const auto value = map.evaluate(-5.0, 250.0);
    require_close(
        value.outputs[0], 2.0,
        "clamped map output is incorrect");
    require_close(
        value.outputs[1], 30.0,
        "clamped map output is incorrect");
    require_close(
        value.primary_derivatives[0], 0.0,
        "clamped primary derivative must be zero");
    require_close(
        value.family_derivatives[0], 0.0,
        "clamped family derivative must be zero");
    require(
        value.primary_extrapolated &&
            value.family_extrapolated,
        "clamped evaluation must report both extrapolated axes");
}

void test_linear_policy_extrapolates_with_derivatives() {
    const auto map = sample_map(
        thermox::platform::MapExtrapolationPolicy::linear,
        thermox::platform::MapExtrapolationPolicy::linear);
    const auto value = map.evaluate(-5.0, 50.0);
    require_close(
        value.outputs[0], -0.5,
        "linear two-axis extrapolation is incorrect");
    require_close(
        value.outputs[1], -5.0,
        "linear two-axis extrapolation is incorrect");
    require_close(
        value.primary_derivatives[0], 0.2,
        "linear primary derivative is incorrect");
    require_close(
        value.family_derivatives[0], 0.01,
        "linear family derivative is incorrect");
    require(
        value.primary_extrapolated &&
            value.family_extrapolated,
        "linear evaluation must report extrapolated axes");
}

void test_definition_validation() {
    bool duplicate_output_rejected = false;
    try {
        (void)thermox::platform::PerformanceMap(
            {"x", "dimensionless"},
            {"y", "dimensionless"},
            {
                {"value", "dimensionless"},
                {"value", "dimensionless"},
            },
            {
                {1.0, {{0.0, {1.0, 2.0}},
                       {1.0, {2.0, 3.0}}}},
                {2.0, {{0.0, {2.0, 3.0}},
                       {1.0, {3.0, 4.0}}}},
            });
    } catch (const std::invalid_argument&) {
        duplicate_output_rejected = true;
    }
    require(
        duplicate_output_rejected,
        "duplicate output names must be rejected");

    bool ordering_rejected = false;
    try {
        (void)thermox::platform::PerformanceMap(
            {"x", "dimensionless"},
            {"y", "dimensionless"},
            {{"value", "dimensionless"}},
            {
                {1.0, {{1.0, {1.0}}, {0.0, {2.0}}}},
                {2.0, {{0.0, {2.0}}, {1.0, {3.0}}}},
            });
    } catch (const std::invalid_argument&) {
        ordering_rejected = true;
    }
    require(
        ordering_rejected,
        "unsorted samples must be rejected");

    bool non_finite_rejected = false;
    try {
        (void)thermox::platform::PerformanceMap(
            {"x", "dimensionless"},
            {"y", "dimensionless"},
            {{"value", "dimensionless"}},
            {
                {1.0, {{0.0, {1.0}},
                       {1.0,
                        {std::numeric_limits<double>::quiet_NaN()}}}},
                {2.0, {{0.0, {2.0}}, {1.0, {3.0}}}},
            });
    } catch (const std::invalid_argument&) {
        non_finite_rejected = true;
    }
    require(
        non_finite_rejected,
        "non-finite map outputs must be rejected");
}

void test_conditioned_map_interpolates_third_coordinate() {
    const thermox::platform::ConditionedPerformanceMap map(
        {"geometry_setting", "angle"},
        {
            {0.0, scaled_sample_map(1.0)},
            {1.0, scaled_sample_map(2.0)},
        });
    const auto value = map.evaluate(5.0, 150.0, 0.5);
    require_close(
        value.map.outputs.at(0), 3.75,
        "conditioned output interpolation is incorrect");
    require_close(
        value.map.primary_derivatives.at(0), 0.3,
        "conditioned primary derivative is incorrect");
    require_close(
        value.map.family_derivatives.at(0), 0.015,
        "conditioned family derivative is incorrect");
    require_close(
        value.condition_derivatives.at(0), 2.5,
        "condition derivative is incorrect");
    require(
        map.condition_variable().name == "geometry_setting" &&
            map.layers().size() == 2 &&
            !value.condition_extrapolated,
        "conditioned map must retain its third-axis contract");

    const thermox::platform::ConditionedPerformanceMap clamped(
        {"geometry_setting", "angle"},
        {
            {0.0, scaled_sample_map(1.0)},
            {1.0, scaled_sample_map(2.0)},
        },
        thermox::platform::MapExtrapolationPolicy::clamp);
    const auto boundary = clamped.evaluate(5.0, 150.0, -1.0);
    require_close(
        boundary.map.outputs.at(0), 2.5,
        "condition clamp must use the boundary layer");
    require_close(
        boundary.condition_derivatives.at(0), 0.0,
        "condition clamp derivative must be zero");
    require(
        boundary.condition_extrapolated,
        "condition clamp must report extrapolation");
}

void test_versioned_artifact_registry() {
    thermox::platform::EngineeringArtifactRegistry registry;
    registry.register_artifact(sample_artifact());
    require(
        registry.contains("compressor-map") &&
            registry.ids() ==
                std::vector<std::string>{"compressor-map"},
        "registered map artifact must be discoverable");
    const auto artifact =
        registry.require_as<
            thermox::platform::PerformanceMapArtifact>(
                "compressor-map",
                thermox::platform::performance_map_artifact_type);
    require(
        artifact->revision == "vendor-revision-7" &&
            artifact->map->evaluate(5.0, 150.0).outputs.at(0) ==
                2.5,
        "artifact registry must preserve identity and payload");

    bool duplicate_rejected = false;
    try {
        registry.register_artifact(sample_artifact());
    } catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    require(
        duplicate_rejected,
        "artifact registry must reject duplicate identities");

    auto bad_checksum = sample_artifact("bad-checksum");
    bad_checksum.checksum_sha256 = "not-a-sha256";
    bool checksum_rejected = false;
    try {
        registry.register_artifact(std::move(bad_checksum));
    } catch (const std::invalid_argument&) {
        checksum_rejected = true;
    }
    require(
        checksum_rejected,
        "artifact registry must reject malformed checksums");

    registry.register_artifact({
        "conditioned-compressor-map",
        thermox::platform::performance_map_artifact_schema_v2,
        "vendor-variable-geometry-3",
        std::string(64, 'b'),
        nullptr,
        std::make_shared<const
            thermox::platform::ConditionedPerformanceMap>(
            thermox::platform::MapVariable{
                "geometry_setting", "angle"},
            std::vector<thermox::platform::ConditionedMapLayer>{
                {0.0, scaled_sample_map(1.0)},
                {1.0, scaled_sample_map(2.0)},
            }),
    });
    require(
        registry.require_as<
                    thermox::platform::PerformanceMapArtifact>(
                    "conditioned-compressor-map",
                    thermox::platform::performance_map_artifact_type)
                ->conditioned_map
                ->evaluate(5.0, 150.0, 0.5)
                .map.outputs.at(0) == 3.75,
        "v2 artifact must preserve conditioned map payload");
}

void test_registry_is_artifact_type_neutral() {
    thermox::platform::EngineeringArtifactRegistry registry;
    CorrelationArtifact correlation;
    correlation.id = "bend-loss-correlation";
    correlation.schema_version = "example.correlation/v1";
    correlation.revision = "engineering-review-1";
    correlation.checksum_sha256 = std::string(64, 'c');
    registry.register_artifact(std::move(correlation));

    require(
        registry.require_artifact(
                    "bend-loss-correlation",
                    "example.correlation")
                ->artifact_type() == "example.correlation",
        "generic registry must preserve non-map artifact types");

    bool wrong_type_rejected = false;
    try {
        static_cast<void>(registry.require_artifact(
            "bend-loss-correlation",
            thermox::platform::performance_map_artifact_type));
    } catch (const std::invalid_argument&) {
        wrong_type_rejected = true;
    }
    require(
        wrong_type_rejected,
        "generic registry must reject a mismatched artifact binding type");
}

}  // namespace

int main() {
    try {
        test_interpolates_non_rectangular_curve_families();
        test_reject_policy_reports_domain_failures();
        test_clamp_policy_has_zero_boundary_derivatives();
        test_linear_policy_extrapolates_with_derivatives();
        test_definition_validation();
        test_conditioned_map_interpolates_third_coordinate();
        test_versioned_artifact_registry();
        test_registry_is_artifact_type_neutral();
        std::cout << "thermox performance map tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "thermox performance map tests failed: "
                  << error.what() << "\n";
        return 1;
    }
}
