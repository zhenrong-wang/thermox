#include "thermox/platform/engineering_artifact.hpp"
#include "thermox/platform/regime_map.hpp"
#include "thermox/platform/two_phase_flow_groups.hpp"

#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

thermox::platform::RegimeMapCriterion below_or_equal(
    std::string expression,
    double maximum) {
    return {
        std::move(expression), "dimensionless", std::nullopt,
        maximum, true, true,
    };
}

thermox::platform::RegimeMapCriterion above(
    std::string expression,
    double minimum) {
    return {
        std::move(expression), "dimensionless", minimum,
        std::nullopt, false, true,
    };
}

thermox::platform::RegimeMapArtifact test_map() {
    return {
        "synthetic-flow-pattern-map",
        thermox::platform::regime_map_artifact_schema_v1,
        "test-1", std::string(64, 'a'),
        {
            {"gas_froude", "dimensionless"},
            {"liquid_loading", "dimensionless"},
        },
        {
            {
                "low_gas", "stratified", 10,
                {
                    below_or_equal("gas_froude", 1.0),
                    above("liquid_loading", 0.0),
                },
            },
            {
                "high_gas", "annular", 10,
                {
                    above("gas_froude", 1.0),
                    above("liquid_loading", 0.0),
                },
            },
        },
    };
}

void test_classification_and_boundary() {
    const auto map = test_map();
    map.validate();
    const auto low = map.classify({
        {"gas_froude", 0.8}, {"liquid_loading", 0.2},
    });
    require(
        low.succeeded() && low.selected_region == "low_gas" &&
            low.selected_regime == "stratified",
        "regime map must select a matching region");
    const auto boundary = map.classify({
        {"gas_froude", 1.0}, {"liquid_loading", 0.2},
    });
    require(
        boundary.succeeded() &&
            boundary.selected_region == "low_gas",
        "inclusive/exclusive region boundaries must be deterministic");
    const auto high = map.classify({
        {"gas_froude", 1.2}, {"liquid_loading", 0.2},
    });
    require(
        high.succeeded() && high.selected_regime == "annular",
        "derived-expression criteria must select the other region");
}

void test_priority_gap_and_ambiguity() {
    auto prioritized = thermox::platform::RegimeMapArtifact{
        "priority", thermox::platform::regime_map_artifact_schema_v1,
        "1", std::string(64, 'b'),
        {{"coordinate", "dimensionless"}},
        {
            {"fallback", "unknown", 0,
             {below_or_equal("abs(coordinate)", 100.0)}},
            {"specific", "preferred", 20,
             {below_or_equal("abs(coordinate)", 1.0)}},
        },
    };
    prioritized.validate();
    const auto selected = prioritized.classify({{"coordinate", 0.5}});
    require(
        selected.succeeded() &&
            selected.selected_region == "specific",
        "higher-priority regions must resolve intentional overlap");

    auto ambiguous = thermox::platform::RegimeMapArtifact{
        "ambiguous", thermox::platform::regime_map_artifact_schema_v1,
        "1", std::string(64, 'c'),
        {{"coordinate", "dimensionless"}},
        {
            {"first", "a", 0,
             {below_or_equal("abs(coordinate)", 1.0)}},
            {"second", "b", 0,
             {below_or_equal("abs(coordinate)", 2.0)}},
        },
    };
    ambiguous.validate();
    require(
        ambiguous.classify({{"coordinate", 0.5}}).error.find(
            "ambiguous") != std::string::npos,
        "equal-priority overlap must fail explicitly");
    require(
        prioritized.classify({{"coordinate", 101.0}}).error.find(
            "no regime-map region") != std::string::npos,
        "classification gaps must fail explicitly");
}

void test_contract_validation_and_registry() {
    thermox::platform::EngineeringArtifactRegistry registry;
    registry.register_artifact(test_map());
    const auto resolved = registry.require_as<
        thermox::platform::RegimeMapArtifact>(
            "synthetic-flow-pattern-map",
            thermox::platform::regime_map_artifact_type);
    require(
        resolved->regions().size() == 2U,
        "regime maps must use the generic engineering-artifact registry");

    bool rejected = false;
    try {
        thermox::platform::RegimeMapArtifact invalid{
            "invalid", thermox::platform::regime_map_artifact_schema_v1,
            "1", std::string(64, 'd'),
            {{"coordinate", "dimensionless"}},
            {{"region", "regime", 0,
              {below_or_equal("undeclared", 1.0)}}},
        };
        invalid.validate();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(
        rejected,
        "criteria must not reference undeclared inputs");

    const auto missing = test_map().classify({{"gas_froude", 1.0}});
    require(
        missing.error.find("missing or non-finite") !=
            std::string::npos,
        "classification must reject incomplete input vectors");
}

void test_two_phase_dimensionless_groups() {
    const auto groups =
        thermox::platform::calculate_two_phase_flow_groups({
            100.0, 0.2, 1000.0, 10.0, 1.0e-3, 1.0e-5,
            0.05, 0.06,
        });
    require(
        std::abs(groups.liquid_mass_flux_kg_m2_s - 80.0) <
                1.0e-12 &&
            std::abs(groups.vapor_mass_flux_kg_m2_s - 20.0) <
                1.0e-12 &&
            std::abs(groups.liquid_superficial_velocity_m_s - 0.08) <
                1.0e-12 &&
            std::abs(groups.vapor_superficial_velocity_m_s - 2.0) <
                1.0e-12,
        "phase fluxes and superficial velocities must follow their "
        "definitions");
    require(
        std::abs(groups.liquid_reynolds_number - 4000.0) <
                1.0e-9 &&
            std::abs(groups.vapor_reynolds_number - 100000.0) <
                1.0e-9 &&
            std::abs(groups.liquid_weber_number -
                     5.333333333333333) < 1.0e-12 &&
            std::abs(groups.vapor_weber_number -
                     33.333333333333333) < 1.0e-12 &&
            std::abs(groups.density_ratio_liquid_to_vapor - 100.0) <
                1.0e-12 &&
            std::abs(groups.viscosity_ratio_liquid_to_vapor - 100.0) <
                1.0e-12,
        "Reynolds, Weber, and phase-property ratios must follow their "
        "definitions");
    require(
        std::abs(groups.liquid_froude_number -
                 0.08 / std::sqrt(9.80665 * 0.05)) < 1.0e-12 &&
            std::abs(groups.vapor_froude_number -
                     2.0 / std::sqrt(9.80665 * 0.05)) < 1.0e-12 &&
            std::abs(groups.bond_number -
                     9.80665 * 990.0 * 0.05 * 0.05 / 0.06) <
                1.0e-12,
        "Froude and Bond groups must use the declared gravity and "
        "hydraulic diameter");

    bool rejected = false;
    try {
        (void)thermox::platform::calculate_two_phase_flow_groups({
            100.0, 0.2, 1000.0, 10.0, 1.0e-3, 1.0e-5,
            0.05, 0.0,
        });
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(
        rejected,
        "dimensionless groups must reject missing surface tension");
}

}  // namespace

int main() {
    try {
        test_classification_and_boundary();
        test_priority_gap_and_ambiguity();
        test_contract_validation_and_registry();
        test_two_phase_dimensionless_groups();
        std::cout << "thermox regime-map tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "thermox regime-map tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
