#include "thermox/platform/performance_map.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

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

}  // namespace

int main() {
    try {
        test_interpolates_non_rectangular_curve_families();
        test_reject_policy_reports_domain_failures();
        test_clamp_policy_has_zero_boundary_derivatives();
        test_linear_policy_extrapolates_with_derivatives();
        test_definition_validation();
        std::cout << "thermox performance map tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "thermox performance map tests failed: "
                  << error.what() << "\n";
        return 1;
    }
}
