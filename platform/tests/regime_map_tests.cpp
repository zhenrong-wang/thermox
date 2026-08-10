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

std::vector<thermox::platform::RegimeMapBranch> one_branch(
    std::vector<thermox::platform::RegimeMapCriterion> criteria,
    std::string id = "default") {
    return {{std::move(id), 0, std::move(criteria)}};
}

thermox::platform::RegimeMapArtifact test_map() {
    return {
        "synthetic-flow-pattern-map",
        thermox::platform::regime_map_artifact_schema_v2,
        "test-1", std::string(64, 'a'),
        {
            {"gas_froude", "dimensionless"},
            {"liquid_loading", "dimensionless"},
        },
        {
            {
                "low_gas", "stratified", 10,
                one_branch({
                    below_or_equal("gas_froude", 1.0),
                    above("liquid_loading", 0.0),
                }),
            },
            {
                "high_gas", "annular", 10,
                one_branch({
                    above("gas_froude", 1.0),
                    above("liquid_loading", 0.0),
                }),
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

void test_study_operating_envelope_restricts_classification() {
    const auto source = test_map();
    thermox::platform::RegimeMapArtifact map{
        source.id, source.schema_version, source.revision,
        source.checksum_sha256, source.inputs(), source.regions(),
        {{"gas_froude", "dimensionless", 0.5, 1.5, true, true}}};
    map.validate();
    require(
        map.classify({
            {"gas_froude", 1.2}, {"liquid_loading", 0.2},
        }).succeeded(),
        "Study envelope must permit an in-policy regime-map point");
    const auto rejected = map.classify({
        {"gas_froude", 2.0}, {"liquid_loading", 0.2},
    });
    require(
        rejected.error.find("regime-map operating envelope rejected") !=
            std::string::npos &&
            rejected.error.find("gas_froude") != std::string::npos,
        "Study envelope must reject classification outside policy");
}

void test_priority_gap_and_ambiguity() {
    auto prioritized = thermox::platform::RegimeMapArtifact{
        "priority", thermox::platform::regime_map_artifact_schema_v2,
        "1", std::string(64, 'b'),
        {{"coordinate", "dimensionless"}},
        {
            {"fallback", "unknown", 0,
             one_branch({below_or_equal("abs(coordinate)", 100.0)})},
            {"specific", "preferred", 20,
             one_branch({below_or_equal("abs(coordinate)", 1.0)})},
        },
    };
    prioritized.validate();
    const auto selected = prioritized.classify({{"coordinate", 0.5}});
    require(
        selected.succeeded() &&
            selected.selected_region == "specific",
        "higher-priority regions must resolve intentional overlap");

    auto ambiguous = thermox::platform::RegimeMapArtifact{
        "ambiguous", thermox::platform::regime_map_artifact_schema_v2,
        "1", std::string(64, 'c'),
        {{"coordinate", "dimensionless"}},
        {
            {"first", "a", 0,
             one_branch({below_or_equal("abs(coordinate)", 1.0)})},
            {"second", "b", 0,
             one_branch({below_or_equal("abs(coordinate)", 2.0)})},
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

void test_alternative_branch_selection() {
    const thermox::platform::RegimeMapArtifact map{
        "alternative-mechanisms",
        thermox::platform::regime_map_artifact_schema_v2,
        "1", std::string(64, '3'),
        {
            {"film_reversal_coordinate", "dimensionless"},
            {"entrainment_coordinate", "dimensionless"},
        },
        {{
            "annular", "annular", 10,
            {
                {"film_reversal", 10,
                 {above("film_reversal_coordinate", 0.0)}},
                {"wave_entrainment", 20,
                 {above("entrainment_coordinate", 0.0)}},
            },
        }},
    };
    map.validate();
    const auto film = map.classify({
        {"film_reversal_coordinate", 1.0},
        {"entrainment_coordinate", -1.0},
    });
    const auto entrainment = map.classify({
        {"film_reversal_coordinate", -1.0},
        {"entrainment_coordinate", 1.0},
    });
    const auto overlap = map.classify({
        {"film_reversal_coordinate", 1.0},
        {"entrainment_coordinate", 1.0},
    });
    require(
        film.succeeded() &&
            film.selected_branch == "film_reversal" &&
            entrainment.succeeded() &&
            entrainment.selected_branch == "wave_entrainment" &&
            overlap.succeeded() &&
            overlap.selected_branch == "wave_entrainment",
        "named OR branches must select either physical mechanism and "
        "use explicit branch priority when mechanisms overlap");
    require(
        !map.classify({
            {"film_reversal_coordinate", -1.0},
            {"entrainment_coordinate", -1.0},
        }).succeeded(),
        "a region must not match when none of its alternative branches "
        "is satisfied");
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
            "invalid", thermox::platform::regime_map_artifact_schema_v2,
            "1", std::string(64, 'd'),
            {{"coordinate", "dimensionless"}},
            {{"region", "regime", 0,
              one_branch({below_or_equal("undeclared", 1.0)})}},
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

void test_packaged_mishima_ishii_annular_boundary() {
    const auto registry =
        thermox::platform::make_default_regime_map_template_registry();
    const auto& descriptor = registry.require_template(
        "mishima_ishii_vertical_upflow_annular_entrainment");
    require(
        descriptor.scope.find("vertical upward") != std::string::npos &&
            descriptor.scope.find("only") != std::string::npos,
        "packaged physical maps must expose their geometry and limited "
        "classification scope");
    const auto map = thermox::platform::instantiate_regime_map_template(
        descriptor,
        {"mishima-ishii-test", "test-1", std::string(64, 'e')});

    constexpr double rho_l = 1000.0;
    constexpr double rho_v = 1.2;
    constexpr double mu_l = 1.0e-3;
    constexpr double sigma = 0.072;
    constexpr double gravity = 9.80665;
    const double laplace_length = std::sqrt(
        sigma / (gravity * (rho_l - rho_v)));
    const double viscosity_number = mu_l /
        std::sqrt(rho_l * sigma * laplace_length);
    const double transition_velocity = std::pow(
        sigma * gravity * (rho_l - rho_v) / (rho_v * rho_v),
        0.25) * std::pow(viscosity_number, -0.2);
    const auto classify = [&](double velocity, double liquid_reynolds) {
        return map.classify({
            {"vapor_superficial_velocity", velocity},
            {"liquid_density", rho_l},
            {"vapor_density", rho_v},
            {"liquid_viscosity", mu_l},
            {"surface_tension", sigma},
            {"gravity", gravity},
            {"liquid_reynolds_number", liquid_reynolds},
        });
    };
    const auto below = classify(0.5 * transition_velocity, 5000.0);
    const auto above = classify(1.5 * transition_velocity, 5000.0);
    require(
        below.succeeded() && below.selected_regime == "pre_annular" &&
            above.succeeded() && above.selected_regime == "annular",
        "Mishima-Ishii template must classify both sides of the cited "
        "entrainment boundary");
    require(
        !classify(transition_velocity, 1000.0).succeeded(),
        "Mishima-Ishii template must refuse evaluation outside its "
        "declared liquid-Reynolds applicability domain");
}

void test_packaged_mishima_ishii_void_fraction_boundaries() {
    const auto registry =
        thermox::platform::make_default_regime_map_template_registry();
    require(
        registry.descriptors().size() == 4U,
        "default registry must expose each independently auditable "
        "Mishima-Ishii transition template");
    const auto bubbly =
        thermox::platform::instantiate_regime_map_template(
            registry.require_template(
                "mishima_ishii_vertical_upflow_bubbly_to_slug"),
            {"bubbly-slug-test", "test-1", std::string(64, '1')});
    require(
        bubbly.classify({{"void_fraction", 0.299}})
                .selected_regime == "bubbly" &&
            bubbly.classify({{"void_fraction", 0.3}})
                .selected_regime == "slug_side",
        "bubbly-to-slug template must implement a deterministic "
        "alpha=0.30 boundary");

    const auto& descriptor = registry.require_template(
        "mishima_ishii_vertical_upflow_slug_to_churn");
    require(
        descriptor.scope.find("insufficient churn data") !=
            std::string::npos,
        "weak validation evidence must be visible in template scope");
    const auto slug_churn =
        thermox::platform::instantiate_regime_map_template(
            descriptor,
            {"slug-churn-test", "test-1", std::string(64, '2')});
    constexpr double j_l = 0.5;
    constexpr double j_g = 0.2;
    constexpr double rho_l = 1000.0;
    constexpr double rho_g = 1.2;
    constexpr double mu_l = 1.0e-3;
    constexpr double diameter = 0.05;
    constexpr double gravity = 9.80665;
    const double delta_rho = rho_l - rho_g;
    const double mixture_velocity = j_l + j_g;
    const double distribution_parameter =
        1.2 - 0.2 * std::sqrt(rho_g / rho_l);
    const double a =
        0.35 * std::sqrt(delta_rho * gravity * diameter / rho_l);
    const double nu_l = mu_l / rho_l;
    const double b =
        0.75 * std::sqrt(delta_rho * gravity * diameter / rho_l) *
        std::pow(
            delta_rho * gravity * std::pow(diameter, 3) /
                (rho_l * nu_l * nu_l),
            1.0 / 18.0);
    const double transition_alpha = 1.0 - 0.813 * std::pow(
        ((distribution_parameter - 1.0) * mixture_velocity + a) /
            (mixture_velocity + b),
        0.75);
    const auto classify = [&](double alpha) {
        return slug_churn.classify({
            {"void_fraction", alpha},
            {"liquid_superficial_velocity", j_l},
            {"vapor_superficial_velocity", j_g},
            {"liquid_density", rho_l},
            {"vapor_density", rho_g},
            {"liquid_viscosity", mu_l},
            {"diameter", diameter},
            {"gravity", gravity},
        });
    };
    require(
        transition_alpha > 0.3 && transition_alpha < 1.0 &&
            classify(transition_alpha - 1.0e-4).selected_regime ==
                "slug" &&
            classify(transition_alpha + 1.0e-4).selected_regime ==
                "churn",
        "slug-to-churn template must reproduce both sides of the "
        "published mechanistic boundary");
    require(
        !classify(0.2).succeeded(),
        "slug-to-churn template must refuse void fractions below its "
        "declared slug-side domain");
}

void test_packaged_mishima_ishii_composite_map() {
    const auto registry =
        thermox::platform::make_default_regime_map_template_registry();
    const auto& descriptor = registry.require_template(
        "mishima_ishii_vertical_upflow_composite");
    const auto map = thermox::platform::instantiate_regime_map_template(
        descriptor,
        {"composite-map-test", "test-1", std::string(64, '4')});
    require(
        map.regions().size() == 4U &&
            map.regions().back().branches.size() == 2U,
        "composite map must retain four regimes and both cited "
        "annular mechanisms");

    constexpr double j_l = 0.5;
    constexpr double rho_l = 1000.0;
    constexpr double rho_g = 1.2;
    constexpr double mu_l = 1.0e-3;
    constexpr double sigma = 0.072;
    constexpr double diameter = 0.05;
    constexpr double gravity = 9.80665;
    constexpr double liquid_reynolds = 5000.0;
    const double delta_rho = rho_l - rho_g;
    const double laplace_length = std::sqrt(
        sigma / (gravity * delta_rho));
    const double viscosity_number = mu_l /
        std::sqrt(rho_l * sigma * laplace_length);
    const double entrainment_velocity = std::pow(
        sigma * gravity * delta_rho / (rho_g * rho_g), 0.25) *
        std::pow(viscosity_number, -0.2);
    const auto classify = [&](double alpha, double j_g,
                              double reynolds) {
        return map.classify({
            {"void_fraction", alpha},
            {"liquid_superficial_velocity", j_l},
            {"vapor_superficial_velocity", j_g},
            {"liquid_density", rho_l},
            {"vapor_density", rho_g},
            {"liquid_viscosity", mu_l},
            {"surface_tension", sigma},
            {"diameter", diameter},
            {"gravity", gravity},
            {"liquid_reynolds_number", reynolds},
        });
    };
    require(
        classify(0.2, 0.2, liquid_reynolds).selected_regime ==
                "bubbly" &&
            classify(0.5, 0.2, liquid_reynolds).selected_regime ==
                "slug" &&
            classify(0.8, 0.2, liquid_reynolds).selected_regime ==
                "churn",
        "composite map must reproduce the ordered low-velocity "
        "bubbly, slug, and churn regions");

    const auto entrainment =
        classify(0.5, 1.1 * entrainment_velocity, liquid_reynolds);
    require(
        entrainment.succeeded() &&
            entrainment.selected_regime == "annular" &&
            entrainment.selected_branch == "wave_entrainment",
        "composite map must permit a direct wave-entrainment "
        "transition to annular flow");
    const double film_velocity =
        std::sqrt(delta_rho * gravity * diameter / rho_g) *
        (0.8 - 0.11);
    require(
        film_velocity < entrainment_velocity,
        "test point must isolate film reversal below entrainment");
    const auto film = classify(
        0.8, 0.5 * (film_velocity + entrainment_velocity),
        liquid_reynolds);
    require(
        film.succeeded() && film.selected_regime == "annular" &&
            film.selected_branch == "film_reversal",
        "composite map must select film reversal independently of "
        "wave entrainment");
    require(
        !classify(0.5, 0.2, 1000.0).succeeded(),
        "composite map must refuse states outside its conservative "
        "global applicability envelope");
}

}  // namespace

int main() {
    try {
        test_classification_and_boundary();
        test_study_operating_envelope_restricts_classification();
        test_priority_gap_and_ambiguity();
        test_alternative_branch_selection();
        test_contract_validation_and_registry();
        test_two_phase_dimensionless_groups();
        test_packaged_mishima_ishii_annular_boundary();
        test_packaged_mishima_ishii_void_fraction_boundaries();
        test_packaged_mishima_ishii_composite_map();
        std::cout << "thermox regime-map tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "thermox regime-map tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
