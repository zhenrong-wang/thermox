#include "thermox/nonlinear_solver.hpp"
#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/correlation.hpp"
#include "thermox/platform/model_document.hpp"
#include "thermox/platform/regime_map.hpp"
#include "thermox/physics/property_registry.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_close(
    double actual, double expected, double tolerance,
    const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message + ": expected " +
            std::to_string(expected) + ", got " +
            std::to_string(actual));
    }
}

thermox::platform::CorrelationArtifact bend_correlation() {
    return {
        "bend-correlation",
        thermox::platform::correlation_artifact_schema_v2,
        "vendor-revision-1",
        std::string(64, 'c'),
        {
            {"mass_flow", "mass_flow"},
            {"density", "density"},
            {"area", "area"},
        },
        {"pressure_loss", "pressure"},
        {{"default", "general", 0,
          {{"loss_coefficient", 1.5}},
          "loss_coefficient * mass_flow * abs(mass_flow) / "
          "(2 * density * area * area)", {}}},
    };
}

thermox::platform::CorrelationArtifact void_fraction_correlation() {
    return {
        "void-fraction-correlation",
        thermox::platform::correlation_artifact_schema_v2,
        "constant-slip-reference-1",
        std::string(64, 'e'),
        {
            {"vapor_quality", "dimensionless"},
            {"liquid_density", "density"},
            {"vapor_density", "density"},
        },
        {"void_fraction", "dimensionless"},
        {{"default", "general", 0, {{"slip_ratio", 2.0}},
          "1 / (1 + ((1 - vapor_quality) / vapor_quality) * "
          "(vapor_density / liquid_density) * slip_ratio)", {}}},
    };
}

thermox::platform::CorrelationArtifact
zuber_findlay_void_fraction_correlation() {
    const auto templates = thermox::platform::
        make_default_correlation_template_registry();
    return thermox::platform::instantiate_correlation_template(
        templates.require_template(
            "zuber_findlay_kinematic_void_fraction"),
        {"zuber-findlay-void-fraction", "test-1",
         std::string(64, '9')},
        {{"distribution_parameter", 1.2},
         {"drift_velocity", 0.5}});
}

thermox::platform::CorrelationArtifact
two_phase_friction_pressure_gradient_correlation() {
    const auto templates = thermox::platform::
        make_default_correlation_template_registry();
    return thermox::platform::instantiate_correlation_family_template(
        templates,
        templates.require_family_template(
            "chisholm_smooth_pipe_friction_family"),
        {"two-phase-friction-gradient", "test-1",
         std::string(64, '8')});
}

void test_packaged_zuber_findlay_template_has_physical_limits() {
    const auto templates = thermox::platform::
        make_default_correlation_template_registry();
    const auto& packaged_family = templates.require_family_template(
        "chisholm_smooth_pipe_friction_family");
    require(
        packaged_family.bindings.size() == 4U &&
            std::all_of(
                packaged_family.bindings.begin(),
                packaged_family.bindings.end(),
                [](const auto& binding) {
                    return binding.flow_regimes.empty() &&
                        binding.fallback_for_unmapped_flow_regime;
                }) &&
            packaged_family.scope.find("does not claim") !=
                std::string::npos,
        "packaged Chisholm family must explicitly declare general "
        "flow-pattern fallback scope");
    const auto& descriptor = templates.require_template(
        "zuber_findlay_kinematic_void_fraction");
    require(
        descriptor.reference.find("10.1115/1.3689137") !=
            std::string::npos,
        "packaged drift-flux template retains its primary reference");

    const auto homogeneous = thermox::platform::
        instantiate_correlation_template(
            descriptor,
            {"zf-homogeneous", "test-1", std::string(64, '1')},
            {{"distribution_parameter", 1.0},
             {"drift_velocity", 0.0}});
    const std::map<std::string, double> inputs{
        {"vapor_quality", 0.2},
        {"liquid_density", 900.0},
        {"vapor_density", 5.0},
        {"mass_flux", 100.0},
    };
    const auto homogeneous_result = homogeneous.evaluate(inputs);
    require(homogeneous_result.error.empty(),
            homogeneous_result.error);
    const double expected =
        (0.2 / 5.0) / (0.2 / 5.0 + 0.8 / 900.0);
    require_close(homogeneous_result.value, expected, 1.0e-12,
                  "zero-drift uniform-distribution limit");

    const auto drifting = thermox::platform::
        instantiate_correlation_template(
            descriptor,
            {"zf-drifting", "test-1", std::string(64, '2')},
            {{"distribution_parameter", 1.2},
             {"drift_velocity", 0.5}});
    const auto drifting_result = drifting.evaluate(inputs);
    require(drifting_result.error.empty(), drifting_result.error);
    require(
        drifting_result.value > 0.0 &&
            drifting_result.value < homogeneous_result.value &&
            drifting_result.input_derivatives.at("vapor_quality") > 0.0,
        "positive distribution and drift terms produce a physical, "
        "quality-responsive void fraction");

    bool missing_coefficient_rejected = false;
    try {
        (void)thermox::platform::instantiate_correlation_template(
            descriptor,
            {"zf-invalid", "test-1", std::string(64, '3')},
            {{"distribution_parameter", 1.2}});
    } catch (const std::invalid_argument& error) {
        missing_coefficient_rejected =
            std::string(error.what()).find("drift_velocity") !=
            std::string::npos;
    }
    require(missing_coefficient_rejected,
            "packaged template must not invent missing empirical coefficients");

    bool nonphysical_coefficient_rejected = false;
    try {
        (void)thermox::platform::instantiate_correlation_template(
            descriptor,
            {"zf-invalid", "test-2", std::string(64, '4')},
            {{"distribution_parameter", 0.0},
             {"drift_velocity", 0.5}});
    } catch (const std::invalid_argument& error) {
        nonphysical_coefficient_rejected =
            std::string(error.what()).find(
                "distribution_parameter") != std::string::npos;
    }
    require(nonphysical_coefficient_rejected,
            "packaged template must enforce coefficient bounds");
}

void test_study_operating_envelope_precedes_native_applicability() {
    auto correlation = bend_correlation();
    correlation = thermox::platform::CorrelationArtifact{
        correlation.id, correlation.schema_version,
        correlation.revision, correlation.checksum_sha256,
        correlation.inputs(), correlation.output(),
        correlation.candidates(),
        {{"mass_flow", "mass_flow", 1.0, 2.0, true, true}}};
    correlation.validate();
    const auto accepted = correlation.evaluate({
        {"mass_flow", 1.5}, {"density", 1.2}, {"area", 0.5},
    });
    require(
        accepted.error.empty(),
        "Study envelope must permit an in-policy correlation point");
    const auto rejected = correlation.evaluate({
        {"mass_flow", 2.5}, {"density", 1.2}, {"area", 0.5},
    });
    require(
        rejected.error.find("correlation operating envelope rejected") !=
            std::string::npos &&
            rejected.error.find("mass_flow") != std::string::npos,
        "Study envelope must reject a point even when the source "
        "correlation has no native applicability restriction");
}

void test_packaged_chisholm_family_selects_phase_regimes() {
    const auto family =
        two_phase_friction_pressure_gradient_correlation();
    const auto evaluate = [&](double liquid_reynolds,
                              double vapor_reynolds) {
        return family.evaluate({
            {"liquid_mass_flux", 20.0},
            {"vapor_mass_flux", 5.0},
            {"liquid_density", 900.0},
            {"vapor_density", 5.0},
            {"liquid_reynolds_number", liquid_reynolds},
            {"vapor_reynolds_number", vapor_reynolds},
            {"diameter", 0.1},
        });
    };
    const auto turbulent = evaluate(5000.0, 10000.0);
    require(
        turbulent.error.empty() &&
            turbulent.selected_candidate == "turbulent_turbulent" &&
            turbulent.selected_regime ==
                "liquid_turbulent_vapor_turbulent",
        "packaged Chisholm family must select turbulent/turbulent");
    const double liquid_gradient =
        2.0 * 0.079 * std::pow(5000.0, -0.25) * 20.0 * 20.0 /
        (0.1 * 900.0);
    const double vapor_gradient =
        2.0 * 0.079 * std::pow(10000.0, -0.25) * 5.0 * 5.0 /
        (0.1 * 5.0);
    require_close(
        turbulent.value,
        liquid_gradient +
            20.0 * std::sqrt(liquid_gradient * vapor_gradient) +
            vapor_gradient,
        1.0e-12, "packaged Chisholm turbulent pressure gradient");

    const auto boundary = evaluate(2000.0, 2000.0);
    require(
        boundary.error.empty() &&
            boundary.selected_candidate == "laminar_laminar",
        "Re=2000 boundary must belong deterministically to the "
        "laminar/laminar candidate");
    const auto mixed = evaluate(1000.0, 5000.0);
    require(
        mixed.error.empty() &&
            mixed.selected_candidate == "laminar_turbulent",
        "packaged Chisholm family must select mixed phase regimes");

    const auto templates = thermox::platform::
        make_default_correlation_template_registry();
    require(
        templates.require_template(
            "chisholm_turbulent_turbulent_friction_gradient")
                .reference.find("10.1016/0017-9310(67)90047-6") !=
            std::string::npos,
        "packaged Chisholm template retains its primary reference");
    bool fixed_constants_rejected = false;
    try {
        (void)thermox::platform::instantiate_correlation_template(
            templates.require_template(
                "chisholm_turbulent_turbulent_friction_gradient"),
            {"modified-chisholm", "test-1", std::string(64, '6')},
            {{"chisholm_parameter", 21.0}});
    } catch (const std::invalid_argument&) {
        fixed_constants_rejected = true;
    }
    require(
        fixed_constants_rejected,
        "packaged referenced constants must not be silently overridden");

    bool incompatible_family_rejected = false;
    try {
        (void)thermox::platform::instantiate_correlation_family(
            templates,
            {"incompatible-family", "test-1", std::string(64, '5')},
            {
                {"zuber_findlay_kinematic_void_fraction",
                 {{"distribution_parameter", 1.2},
                  {"drift_velocity", 0.5}},
                 "void", 0},
                {"chisholm_turbulent_turbulent_friction_gradient",
                 {}, "friction", 0},
            });
    } catch (const std::invalid_argument&) {
        incompatible_family_rejected = true;
    }
    require(
        incompatible_family_rejected,
        "family instantiation must reject incompatible contracts");
}

void test_correlation_evaluates_with_analytic_derivatives() {
    auto artifact = bend_correlation();
    artifact.validate();
    const auto result = artifact.evaluate({
        {"mass_flow", 2.0},
        {"density", 4.0},
        {"area", 0.5},
    });
    require(result.error.empty(), result.error);
    require_close(result.value, 3.0, 1.0e-12,
                  "correlation value");
    require_close(
        result.input_derivatives.at("mass_flow"), 3.0,
        1.0e-12, "mass-flow derivative");
    require_close(
        result.input_derivatives.at("density"), -0.75,
        1.0e-12, "density derivative");
}

void test_correlation_contract_rejects_undeclared_symbols() {
    auto invalid = thermox::platform::CorrelationArtifact{
        "invalid-correlation",
        thermox::platform::correlation_artifact_schema_v2,
        "revision-1",
        std::string(64, 'd'),
        {{"mass_flow", "mass_flow"}},
        {"pressure_loss", "pressure"},
        {{"default", "general", 0, {},
          "mass_flow * undeclared_coefficient", {}}},
    };
    bool rejected = false;
    try {
        invalid.validate();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected,
            "correlation must reject undeclared expression symbols");
}

void test_correlation_enforces_qualified_operating_envelope() {
    auto bounded = thermox::platform::CorrelationArtifact{
        "bounded-correlation",
        thermox::platform::correlation_artifact_schema_v2,
        "revision-1",
        std::string(64, 'a'),
        {{"vapor_quality", "dimensionless"}},
        {"void_fraction", "dimensionless"},
        {{"default", "general", 0, {}, "vapor_quality",
          {{"vapor_quality", 0.1, 0.8, true, false}}}},
    };
    bounded.validate();
    require(
        bounded.assess_applicability({{"vapor_quality", 0.1}})
            .applicable,
        "inclusive applicability boundary must be accepted");
    const auto inside = bounded.evaluate({{"vapor_quality", 0.5}});
    require(inside.error.empty(), inside.error);
    const auto outside = bounded.evaluate({{"vapor_quality", 0.8}});
    require(
        outside.error.find("vapor_quality=0.8") != std::string::npos &&
            outside.error.find("[0.1, 0.8)") != std::string::npos,
        "exclusive applicability violation must report input, value, "
        "and qualified range");

    auto invalid = thermox::platform::CorrelationArtifact{
        "invalid-envelope",
        thermox::platform::correlation_artifact_schema_v2,
        "revision-1",
        std::string(64, 'b'),
        {{"vapor_quality", "dimensionless"}},
        {"void_fraction", "dimensionless"},
        {{"default", "general", 0, {}, "vapor_quality",
          {{"unknown_input", 0.0, 1.0, true, true}}}},
    };
    bool rejected = false;
    try {
        invalid.validate();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected,
            "applicability must reference a declared correlation input");
}

void test_correlation_family_selects_deterministically() {
    auto family = thermox::platform::CorrelationArtifact{
        "void-fraction-family",
        thermox::platform::correlation_artifact_schema_v2,
        "revision-1",
        std::string(64, 'f'),
        {{"vapor_quality", "dimensionless"}},
        {"void_fraction", "dimensionless"},
        {
            {"bubbly", "bubbly", 10, {{"factor", 1.0}},
             "factor * vapor_quality",
             {{"vapor_quality", 0.0, 0.5, true, true}}},
            {"annular", "annular", 20, {{"factor", 2.0}},
             "factor * vapor_quality",
             {{"vapor_quality", 0.5, 1.0, true, true}}},
        },
    };
    family.validate();
    const auto low = family.evaluate({{"vapor_quality", 0.25}});
    require(
        low.error.empty() && low.selected_candidate == "bubbly" &&
            low.selected_regime == "bubbly",
        "family must select the only applicable candidate");
    require_close(low.value, 0.25, 1.0e-12,
                  "selected bubbly correlation value");
    const auto overlap = family.evaluate({{"vapor_quality", 0.5}});
    require(
        overlap.error.empty() && overlap.selected_candidate == "annular",
        "higher priority must deterministically resolve overlap");
    require_close(overlap.value, 1.0, 1.0e-12,
                  "selected annular correlation value");
    const auto gap = family.evaluate({{"vapor_quality", 1.1}});
    require(
        gap.error.find("no correlation candidate is applicable") !=
                std::string::npos &&
            gap.error.find("bubbly") != std::string::npos &&
            gap.error.find("annular") != std::string::npos,
        "uncovered operating points must be rejected");
    require(
        !family.assess_applicability({{"vapor_quality", 1.1}})
            .applicable,
        "family applicability assessment must expose coverage gaps");

    auto ambiguous = thermox::platform::CorrelationArtifact{
        "ambiguous-family",
        thermox::platform::correlation_artifact_schema_v2,
        "revision-1",
        std::string(64, 'a'),
        {{"x", "dimensionless"}},
        {"y", "dimensionless"},
        {
            {"first", "regime-a", 5, {}, "x",
             {{"x", 0.0, 1.0, true, true}}},
            {"second", "regime-b", 5, {}, "x",
             {{"x", 0.0, 1.0, true, true}}},
        },
    };
    ambiguous.validate();
    require(
        ambiguous.evaluate({{"x", 0.5}}).error.find("ambiguous") !=
            std::string::npos,
        "equal-priority overlap must be rejected as ambiguous");
}

void test_flow_regime_routing_separates_physical_taxonomies() {
    const auto family = thermox::platform::CorrelationArtifact{
        "flow-routed-family",
        thermox::platform::correlation_artifact_schema_v2,
        "revision-1", std::string(64, '4'),
        {{"x", "dimensionless"}},
        {"y", "dimensionless"},
        {
            {"slug_law", "high_reynolds", 0, {{"factor", 2.0}},
             "factor * x", {}, {"slug"}, false},
            {"general_low", "low_reynolds", 0, {{"factor", 3.0}},
             "factor * x", {{"x", 0.0, 0.5, true, true}}, {}, true},
            {"general_high", "high_reynolds", 0, {{"factor", 4.0}},
             "factor * x", {{"x", 0.5, 1.0, false, true}}, {}, true},
        }};
    family.validate();

    const auto exact = family.evaluate({{"x", 0.25}}, "slug");
    require(
        exact.error.empty() && exact.selected_candidate == "slug_law" &&
            exact.selected_regime == "high_reynolds" &&
            !exact.used_flow_regime_fallback,
        "an exact physical flow-regime route must override general laws");

    const auto fallback = family.evaluate({{"x", 0.25}}, "bubbly");
    require(
        fallback.error.empty() &&
            fallback.selected_candidate == "general_low" &&
            fallback.used_flow_regime_fallback,
        "an explicitly declared general family must serve an unmapped "
        "physical flow regime using its own applicability taxonomy");

    const auto unrouted = thermox::platform::CorrelationArtifact{
        "unrouted-family",
        thermox::platform::correlation_artifact_schema_v2,
        "revision-1", std::string(64, '5'),
        {{"x", "dimensionless"}}, {"y", "dimensionless"},
        {{"native", "high_reynolds", 0, {}, "x", {}, {}, false}}};
    unrouted.validate();
    require(
        unrouted.evaluate({{"x", 0.25}}, "annular").error.find(
            "no correlation route is declared") != std::string::npos,
        "a candidate native regime must never be mistaken for a physical "
        "flow-pattern route");
}

void test_return_bend_uses_bound_correlation() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "correlated_return_bend",
    "media": [{
      "id": "air",
      "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "components": [{
      "id": "bend",
      "kind": "fitting.fluid.return_bend.correlation",
      "parameters": {
        "inner_diameter": {"value": 0.5, "unit": "m"}
      },
      "artifacts": {
        "pressure_loss_correlation": "bend-correlation"
      },
      "media": {"inlet": "air", "outlet": "air"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "bend.inlet.m_dot": {"value": 2.0, "unit": "kg/s"},
      "bend.inlet.p": {"value": 2.0, "unit": "bar"},
      "bend.inlet.h": {"value": 300.0, "unit": "kJ/kg"}
    }
  }]
})json");
    auto artifacts =
        thermox::platform::EngineeringArtifactRegistry{};
    artifacts.register_artifact(bend_correlation());
    const auto properties =
        thermox::physics::make_default_property_package_registry();
    const auto graph = thermox::platform::compile_model_graph(
        document,
        thermox::platform::make_default_component_registry(),
        properties, artifacts, "design");
    const auto solved = thermox::solve_newton(graph.problem);
    require(solved.diagnostics.converged,
            solved.diagnostics.message);

    const auto air = properties.create(
        "ideal_gas_mixture", "Air");
    const auto inlet = air->state_ph(2.0e5, 3.0e5);
    require(inlet.ok(), "inlet state must evaluate");
    const double area =
        std::numbers::pi * 0.5 * 0.5 / 4.0;
    const double expected_loss =
        1.5 * 2.0 * 2.0 /
        (2.0 * inlet.state.density_kg_m3 * area * area);
    const auto outlet_pressure = std::find(
        graph.problem.variable_names.begin(),
        graph.problem.variable_names.end(),
        "bend.outlet.p");
    require(outlet_pressure != graph.problem.variable_names.end(),
            "compiled graph must expose bend outlet pressure");
    const auto index = static_cast<std::size_t>(
        outlet_pressure - graph.problem.variable_names.begin());
    require_close(
        solved.x.at(index), 2.0e5 - expected_loss, 1.0e-7,
        "bound correlation pressure loss");
}

void test_two_phase_pipe_uses_bound_void_fraction_correlation() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "correlated_two_phase_riser",
    "media": [{
      "id": "water",
      "backend": "water_steam_if97",
      "substance": "Water"
    }],
    "components": [{
      "id": "riser",
      "kind": "pipe.fluid.correlated_two_phase_pressure_drop",
      "parameters": {
        "flow_diameter": {"value": 0.1, "unit": "m"},
        "length": {"value": 10.0, "unit": "m"},
        "loss_coefficient": 2.0,
        "elevation_change": {"value": 5.0, "unit": "m"}
      },
      "artifacts": {
        "void_fraction_correlation": "zuber-findlay-void-fraction",
        "friction_pressure_gradient_correlation": "two-phase-friction-gradient"
      },
      "media": {"inlet": "water", "outlet": "water"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "steady",
    "mode": "steady_state_design",
    "fixed_values": {
      "riser.inlet.m_dot": {"value": 0.2, "unit": "kg/s"},
      "riser.inlet.p": {"value": 1.0, "unit": "MPa"},
      "riser.inlet.h": {"value": 1500.0, "unit": "kJ/kg"}
    }
  }, {
    "id": "reverse",
    "mode": "steady_state_design",
    "fixed_values": {
      "riser.inlet.m_dot": {"value": -0.2, "unit": "kg/s"},
      "riser.inlet.p": {"value": 1.0, "unit": "MPa"},
      "riser.inlet.h": {"value": 1500.0, "unit": "kJ/kg"}
    }
  }, {
    "id": "dynamic",
    "mode": "dynamic_transient",
    "fixed_values": {
      "riser.inlet.m_dot": {"value": 0.2, "unit": "kg/s"},
      "riser.inlet.p": {"value": 1.0, "unit": "MPa"},
      "riser.inlet.h": {"value": 1500.0, "unit": "kJ/kg"}
    }
  }]
})json");
    thermox::platform::EngineeringArtifactRegistry artifacts;
    artifacts.register_artifact(
        zuber_findlay_void_fraction_correlation());
    artifacts.register_artifact(
        two_phase_friction_pressure_gradient_correlation());
    const auto properties =
        thermox::physics::make_default_property_package_registry();
    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto graph = thermox::platform::compile_model_graph(
        document, registry, properties, artifacts, "steady");
    const auto solved = thermox::solve_newton(graph.problem);
    require(solved.diagnostics.converged,
            solved.diagnostics.message);
    const auto variable = [&](const std::string& name) {
        const auto found = std::find(
            graph.problem.variable_names.begin(),
            graph.problem.variable_names.end(), name);
        require(found != graph.problem.variable_names.end(),
                "compiled variable must exist: " + name);
        return solved.x.at(static_cast<std::size_t>(
            found - graph.problem.variable_names.begin()));
    };
    const double outlet_pressure = variable("riser.outlet.p");
    const double mean_pressure = 0.5 * (1.0e6 + outlet_pressure);
    const auto water = properties.create(
        "water_steam_if97", "Water");
    const auto state = water->state_ph(mean_pressure, 1.5e6);
    const auto saturation = water->saturation_p(mean_pressure);
    require(state.ok() && saturation.ok(),
            "correlated-riser properties must evaluate");
    const double area = std::numbers::pi * 0.1 * 0.1 / 4.0;
    const auto alpha =
        zuber_findlay_void_fraction_correlation().evaluate({
        {"vapor_quality", state.state.vapor_quality},
        {"liquid_density", saturation.liquid.density_kg_m3},
        {"vapor_density", saturation.vapor.density_kg_m3},
        {"mass_flux", 0.2 / area},
    });
    require(alpha.error.empty() && alpha.value > 0.0 &&
                alpha.value < 1.0,
            "bound correlation must produce physical void fraction");
    const double density =
        alpha.value * saturation.vapor.density_kg_m3 +
        (1.0 - alpha.value) *
            saturation.liquid.density_kg_m3;
    const auto chisholm_gradient =
        [&](double quality, const auto& phase_saturation) {
            const double total_mass_flux = 0.2 / area;
            const double liquid_mass_flux =
                (1.0 - quality) * total_mass_flux;
            const double vapor_mass_flux =
                quality * total_mass_flux;
            const double liquid_reynolds =
                liquid_mass_flux * 0.1 /
                phase_saturation.liquid.viscosity_pa_s;
            const double vapor_reynolds =
                vapor_mass_flux * 0.1 /
                phase_saturation.vapor.viscosity_pa_s;
            const auto fanning = [](double reynolds) {
                return reynolds <= 2000.0
                    ? 16.0 / reynolds
                    : 0.079 * std::pow(reynolds, -0.25);
            };
            const double liquid_gradient =
                2.0 * fanning(liquid_reynolds) *
                liquid_mass_flux * liquid_mass_flux /
                (0.1 *
                 phase_saturation.liquid.density_kg_m3);
            const double vapor_gradient =
                2.0 * fanning(vapor_reynolds) *
                vapor_mass_flux * vapor_mass_flux /
                (0.1 * phase_saturation.vapor.density_kg_m3);
            const double chisholm_parameter =
                liquid_reynolds <= 2000.0
                ? (vapor_reynolds <= 2000.0 ? 5.0 : 12.0)
                : (vapor_reynolds <= 2000.0 ? 10.0 : 20.0);
            return liquid_gradient + vapor_gradient +
                chisholm_parameter *
                    std::sqrt(liquid_gradient * vapor_gradient);
        };
    const auto momentum_flux = [&](double pressure) {
        const auto endpoint_state = water->state_ph(
            pressure, 1.5e6);
        const auto endpoint_saturation = water->saturation_p(
            pressure);
        require(endpoint_state.ok() && endpoint_saturation.ok(),
                "riser endpoint properties must evaluate");
        const auto endpoint_alpha =
            zuber_findlay_void_fraction_correlation().evaluate({
                {"vapor_quality",
                 endpoint_state.state.vapor_quality},
                {"liquid_density",
                 endpoint_saturation.liquid.density_kg_m3},
                {"vapor_density",
                 endpoint_saturation.vapor.density_kg_m3},
                {"mass_flux", 0.2 / area},
            });
        require(endpoint_alpha.error.empty(), endpoint_alpha.error);
        const double quality = endpoint_state.state.vapor_quality;
        const double liquid_fraction = 1.0 - quality;
        return std::pow(0.2 / area, 2.0) *
            (quality * quality /
                 (endpoint_saturation.vapor.density_kg_m3 *
                  endpoint_alpha.value) +
             liquid_fraction * liquid_fraction /
                 (endpoint_saturation.liquid.density_kg_m3 *
                  (1.0 - endpoint_alpha.value)));
    };
    const double acceleration_pressure_drop =
        momentum_flux(outlet_pressure) - momentum_flux(1.0e6);
    const double expected_drop =
        2.0 * 0.2 * 0.2 /
            (2.0 * density * area * area) +
        chisholm_gradient(
            state.state.vapor_quality, saturation) * 10.0 +
        density * 9.80665 * 5.0 +
        acceleration_pressure_drop;
    require_close(
        1.0e6 - outlet_pressure, expected_drop, 1.0e-5,
        "correlated void fraction closes riser pressure balance");
    require_close(variable("riser.outlet.m_dot"), 0.2, 1.0e-12,
                  "correlated riser conserves mass");
    require_close(variable("riser.outlet.h"), 1.5e6, 1.0e-8,
                  "correlated riser transports enthalpy");

    auto mapped_document = document;
    mapped_document.components.at(0).artifact_bindings
        ["friction_regime_map"] = "mishima-ishii-composite";
    const auto regime_templates = thermox::platform::
        make_default_regime_map_template_registry();
    artifacts.register_artifact(
        thermox::platform::instantiate_regime_map_template(
            regime_templates.require_template(
                "mishima_ishii_vertical_upflow_composite"),
            {"mishima-ishii-composite", "test-1",
             std::string(64, '6')}));
    const auto mapped_graph = thermox::platform::compile_model_graph(
        mapped_document, registry, properties, artifacts, "steady");
    const auto mapped_solved = thermox::solve_newton(mapped_graph.problem);
    require(
        mapped_solved.diagnostics.converged,
        "cited composite regime map must route the packaged general "
        "friction fallback in a connected pipe: " +
            mapped_solved.diagnostics.message);
    const auto mapped_outlet = std::find(
        mapped_graph.problem.variable_names.begin(),
        mapped_graph.problem.variable_names.end(), "riser.outlet.p");
    require(mapped_outlet != mapped_graph.problem.variable_names.end(),
            "mapped correlated riser outlet pressure must exist");
    require_close(
        mapped_solved.x.at(static_cast<std::size_t>(
            mapped_outlet - mapped_graph.problem.variable_names.begin())),
        outlet_pressure, 1.0e-7,
        "classification-only map binding must preserve a general "
        "fallback pressure-drop result");

    auto reverse_document = document;
    reverse_document.components.at(0)
        .parameters.at("elevation_change").value_si = 0.0;
    const auto reverse_graph =
        thermox::platform::compile_model_graph(
            reverse_document, registry, properties, artifacts,
            "reverse");
    const auto reverse_solved =
        thermox::solve_newton(reverse_graph.problem);
    require(reverse_solved.diagnostics.converged,
            reverse_solved.diagnostics.message);
    const auto reverse_variable = [&](const std::string& name) {
        const auto found = std::find(
            reverse_graph.problem.variable_names.begin(),
            reverse_graph.problem.variable_names.end(), name);
        require(found != reverse_graph.problem.variable_names.end(),
                "reverse-flow variable must exist: " + name);
        return reverse_solved.x.at(static_cast<std::size_t>(
            found - reverse_graph.problem.variable_names.begin()));
    };
    const double reverse_outlet_pressure =
        reverse_variable("riser.outlet.p");
    const double reverse_mean_pressure =
        0.5 * (1.0e6 + reverse_outlet_pressure);
    const auto reverse_state = water->state_ph(
        reverse_mean_pressure, 1.5e6);
    const auto reverse_saturation = water->saturation_p(
        reverse_mean_pressure);
    require(reverse_state.ok() && reverse_saturation.ok(),
            "reverse correlated-riser properties must evaluate");
    const auto reverse_alpha =
        zuber_findlay_void_fraction_correlation().evaluate({
            {"vapor_quality", reverse_state.state.vapor_quality},
            {"liquid_density",
             reverse_saturation.liquid.density_kg_m3},
            {"vapor_density",
             reverse_saturation.vapor.density_kg_m3},
            {"mass_flux", 0.2 / area},
        });
    require(reverse_alpha.error.empty(), reverse_alpha.error);
    const double reverse_density =
        reverse_alpha.value *
            reverse_saturation.vapor.density_kg_m3 +
        (1.0 - reverse_alpha.value) *
            reverse_saturation.liquid.density_kg_m3;
    const double reverse_loss_magnitude =
        2.0 * 0.2 * 0.2 /
            (2.0 * reverse_density * area * area) +
        chisholm_gradient(
            reverse_state.state.vapor_quality,
            reverse_saturation) * 10.0;
    const double reverse_acceleration_pressure_drop =
        momentum_flux(reverse_outlet_pressure) -
        momentum_flux(1.0e6);
    require_close(
        1.0e6 - reverse_outlet_pressure,
        -reverse_loss_magnitude +
            reverse_acceleration_pressure_drop,
        1.0e-5,
        "correlated pipe reverses friction while retaining oriented "
        "momentum-flux balance");

    const auto transient =
        thermox::platform::compile_transient_model_graph(
            document, registry, properties, artifacts, "dynamic");
    const auto initialized =
        thermox::make_consistent_initial_conditions(
            transient.problem, 0.0);
    require(initialized.diagnostics.converged,
            "correlated riser DAE initialization: " +
                initialized.diagnostics.message);

    auto regime_document = document;
    regime_document.components.at(0).artifact_bindings
        ["friction_pressure_gradient_correlation"] =
            "regime-friction-gradient";
    regime_document.components.at(0).artifact_bindings
        ["friction_regime_map"] = "flow-pattern-map";
    thermox::platform::EngineeringArtifactRegistry regime_artifacts;
    regime_artifacts.register_artifact(
        zuber_findlay_void_fraction_correlation());
    regime_artifacts.register_artifact(
        thermox::platform::CorrelationArtifact{
            "regime-friction-gradient",
            thermox::platform::correlation_artifact_schema_v2,
            "regime-test-1", std::string(64, '8'),
            {{"mass_flux", "mass_flux"}},
            {"friction_pressure_gradient", "pressure_gradient"},
            {
                {"stratified", "stratified", 0,
                 {{"gradient", 10.0}},
                 "gradient + 0 * mass_flux", {}, {"stratified"}, false},
                {"annular", "annular", 0,
                 {{"gradient", 20.0}},
                 "gradient + 0 * mass_flux", {}, {"annular"}, false},
            }});
    regime_artifacts.register_artifact(
        thermox::platform::RegimeMapArtifact{
            "flow-pattern-map",
            thermox::platform::regime_map_artifact_schema_v2,
            "regime-test-1", std::string(64, '9'),
            {{"vapor_weber_number", "dimensionless"}},
            {{"positive_weber", "annular", 0,
              {{"positive", 0,
                {{"vapor_weber_number", "dimensionless", 0.0,
                  std::nullopt, false, true}}}}}}});
    const auto regime_graph =
        thermox::platform::compile_model_graph(
            regime_document, registry, properties,
            regime_artifacts, "steady");
    const auto regime_solved =
        thermox::solve_newton(regime_graph.problem);
    require(
        regime_solved.diagnostics.converged,
        "bound flow-regime map must select one matching friction "
        "candidate: " + regime_solved.diagnostics.message);

    // Use a fresh registry to verify compile-time regime coverage.
    thermox::platform::EngineeringArtifactRegistry missing_artifacts;
    missing_artifacts.register_artifact(
        zuber_findlay_void_fraction_correlation());
    missing_artifacts.register_artifact(
        thermox::platform::CorrelationArtifact{
            "regime-friction-gradient",
            thermox::platform::correlation_artifact_schema_v2,
            "regime-test-1", std::string(64, '8'),
            {{"mass_flux", "mass_flux"}},
            {"friction_pressure_gradient", "pressure_gradient"},
            {{"annular", "annular", 0, {{"gradient", 20.0}},
              "gradient + 0 * mass_flux", {}, {"annular"}, false}}});
    missing_artifacts.register_artifact(
        thermox::platform::RegimeMapArtifact{
            "flow-pattern-map",
            thermox::platform::regime_map_artifact_schema_v2,
            "regime-test-2", std::string(64, 'a'),
            {{"vapor_weber_number", "dimensionless"}},
            {{"uncovered", "slug", 0,
              {{"positive", 0,
                {{"vapor_weber_number", "dimensionless", 0.0,
                  std::nullopt, false, true}}}}}}});
    bool uncovered_rejected = false;
    try {
        (void)thermox::platform::compile_model_graph(
            regime_document, registry, properties,
            missing_artifacts, "steady");
    } catch (const std::invalid_argument&) {
        uncovered_rejected = true;
    }
    require(
        uncovered_rejected,
        "every selected regime must have a matching friction closure");

    auto nonphysical_document = document;
    nonphysical_document.components.at(0)
        .artifact_bindings["void_fraction_correlation"] =
        "nonphysical-void-fraction";
    thermox::platform::EngineeringArtifactRegistry
        nonphysical_artifacts;
    nonphysical_artifacts.register_artifact(
        thermox::platform::CorrelationArtifact{
            "nonphysical-void-fraction",
            thermox::platform::correlation_artifact_schema_v2,
            "invalid-range-1", std::string(64, 'f'),
            {{"vapor_quality", "dimensionless"}},
            {"void_fraction", "dimensionless"},
            {{"default", "general", 0, {{"invalid_alpha", 1.2}},
              "invalid_alpha + 0 * vapor_quality", {}}}});
    nonphysical_artifacts.register_artifact(
        two_phase_friction_pressure_gradient_correlation());
    const auto nonphysical_graph =
        thermox::platform::compile_model_graph(
            nonphysical_document, registry, properties,
            nonphysical_artifacts, "steady");
    const auto nonphysical_result =
        thermox::solve_newton(nonphysical_graph.problem);
    require(!nonphysical_result.diagnostics.converged,
            "component must reject correlation output outside "
            "0 < alpha < 1");

    auto wrong_dimension_document = document;
    wrong_dimension_document.components.at(0)
        .artifact_bindings["void_fraction_correlation"] =
        "wrong-dimension-void-fraction";
    thermox::platform::EngineeringArtifactRegistry
        wrong_dimension_artifacts;
    wrong_dimension_artifacts.register_artifact(
        thermox::platform::CorrelationArtifact{
            "wrong-dimension-void-fraction",
            thermox::platform::correlation_artifact_schema_v2,
            "wrong-dimension-1", std::string(64, 'a'),
            {{"vapor_quality", "dimensionless"}},
            {"void_fraction", "pressure"},
            {{"default", "general", 0, {}, "vapor_quality", {}}}});
    wrong_dimension_artifacts.register_artifact(
        two_phase_friction_pressure_gradient_correlation());
    bool wrong_dimension_rejected = false;
    try {
        (void)thermox::platform::compile_model_graph(
            wrong_dimension_document, registry, properties,
            wrong_dimension_artifacts, "steady");
    } catch (const std::invalid_argument&) {
        wrong_dimension_rejected = true;
    }
    require(wrong_dimension_rejected,
            "component must reject dimensionally incompatible "
            "void-fraction correlation");

    auto negative_friction_document = document;
    negative_friction_document.components.at(0)
        .artifact_bindings["friction_pressure_gradient_correlation"] =
        "negative-friction-gradient";
    thermox::platform::EngineeringArtifactRegistry
        negative_friction_artifacts;
    negative_friction_artifacts.register_artifact(
        zuber_findlay_void_fraction_correlation());
    negative_friction_artifacts.register_artifact(
        thermox::platform::CorrelationArtifact{
            "negative-friction-gradient",
            thermox::platform::correlation_artifact_schema_v2,
            "invalid-range-1", std::string(64, '7'),
            {{"mass_flux", "mass_flux"}},
            {"friction_pressure_gradient", "pressure_gradient"},
            {{"default", "invalid", 0,
              {{"negative_gradient", -1.0}},
              "negative_gradient + 0 * mass_flux", {}}}});
    const auto negative_friction_graph =
        thermox::platform::compile_model_graph(
            negative_friction_document, registry, properties,
            negative_friction_artifacts, "steady");
    const auto negative_friction_result =
        thermox::solve_newton(negative_friction_graph.problem);
    require(
        !negative_friction_result.diagnostics.converged,
        "component must reject a negative friction pressure gradient");
}

void test_two_phase_inventory_uses_correlation_for_outlet_quality() {
    constexpr double pressure = 1.0e6;
    constexpr double volume = 0.01;
    constexpr double holdup_quality = 0.2;
    constexpr double slip_ratio = 2.0;
    constexpr double outlet_quality =
        holdup_quality * slip_ratio /
        (1.0 - holdup_quality + holdup_quality * slip_ratio);
    const auto properties =
        thermox::physics::make_default_property_package_registry();
    const auto water = properties.create(
        "water_steam_if97", "Water");
    const auto saturation = water->saturation_p(pressure);
    require(saturation.ok(),
            "correlated inventory saturation must evaluate");
    const double specific_volume =
        (1.0 - holdup_quality) /
            saturation.liquid.density_kg_m3 +
        holdup_quality / saturation.vapor.density_kg_m3;
    const double void_fraction =
        (holdup_quality / saturation.vapor.density_kg_m3) /
        specific_volume;
    const double mass = volume / specific_volume;
    const double internal_energy =
        (1.0 - holdup_quality) *
            saturation.liquid.internal_energy_j_kg +
        holdup_quality * saturation.vapor.internal_energy_j_kg;
    const double energy = mass * internal_energy;
    const double outlet_enthalpy =
        (1.0 - outlet_quality) *
            saturation.liquid.enthalpy_j_kg +
        outlet_quality * saturation.vapor.enthalpy_j_kg;

    std::ostringstream json;
    json << std::setprecision(17) << R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "correlated_two_phase_inventory",
    "media": [{"id": "water", "backend": "water_steam_if97", "substance": "Water"}],
    "components": [{
      "id": "volume",
      "kind": "volume.fluid.equilibrium_two_phase_correlated_outlet",
      "parameters": {
        "volume": {"value": 0.01, "unit": "m3"},
        "flow_diameter": {"value": 0.1, "unit": "m"}
      },
      "artifacts": {"void_fraction_correlation": "void-fraction-correlation"},
      "media": {"inlet": "water", "outlet": "water"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "hold",
    "mode": "dynamic_transient",
    "fixed_values": {
      "volume.inlet.m_dot": 0.1,
      "volume.inlet.p": )json" << pressure << R"json(,
      "volume.inlet.h": )json" << outlet_enthalpy << R"json(,
      "volume.outlet.m_dot": 0.1,
      "volume.heat.Q_dot": 0.0
    },
    "initial_guesses": {
      "volume.outlet.p": )json" << pressure << R"json(,
      "volume.outlet.h": )json" << outlet_enthalpy << R"json(,
      "volume.heat.T": )json" << saturation.liquid.temperature_k << R"json(,
      "volume.mass": )json" << mass << R"json(,
      "volume.total_energy": )json" << energy << R"json(,
      "volume.pressure": )json" << pressure << R"json(,
      "volume.holdup_quality": )json" << holdup_quality << R"json(,
      "volume.void_fraction": )json" << void_fraction << R"json(,
      "volume.outlet_quality": )json" << outlet_quality << R"json(
    }
  }]
})json";
    const auto document =
        thermox::platform::parse_model_document_text(json.str());
    thermox::platform::EngineeringArtifactRegistry artifacts;
    artifacts.register_artifact(void_fraction_correlation());
    const auto components =
        thermox::platform::make_default_component_registry();
    const auto graph =
        thermox::platform::compile_transient_model_graph(
            document, components, properties, artifacts, "hold");
    const auto initialized =
        thermox::make_consistent_initial_conditions(
            graph.problem, 0.0);
    require(initialized.diagnostics.converged,
            initialized.diagnostics.message);
    const auto value = [&](const std::string& name) {
        const auto found = std::find(
            graph.problem.variable_names.begin(),
            graph.problem.variable_names.end(), name);
        require(found != graph.problem.variable_names.end(),
                "compiled inventory variable must exist: " + name);
        return initialized.state.at(static_cast<std::size_t>(
            found - graph.problem.variable_names.begin()));
    };
    require_close(value("volume.holdup_quality"),
                  holdup_quality, 1.0e-8,
                  "inventory thermodynamics retain holdup quality");
    require_close(value("volume.void_fraction"),
                  void_fraction, 1.0e-8,
                  "inventory exposes thermodynamic void fraction");
    require_close(value("volume.outlet_quality"),
                  outlet_quality, 1.0e-8,
                  "void-fraction correlation closes outlet quality");
    require_close(value("volume.outlet.h"),
                  outlet_enthalpy, 1.0e-5,
                  "outlet enthalpy follows correlated flow quality");

    thermox::TimeIntegrationOptions options;
    options.end_time = 0.1;
    options.initial_step = 0.01;
    options.max_step = 0.02;
    const auto integrated = thermox::integrate_dae(
        graph.problem, options);
    require(integrated.diagnostics.success,
            integrated.diagnostics.message);
    const auto mass_index = static_cast<std::size_t>(
        std::find(
            graph.problem.variable_names.begin(),
            graph.problem.variable_names.end(), "volume.mass") -
        graph.problem.variable_names.begin());
    const auto energy_index = static_cast<std::size_t>(
        std::find(
            graph.problem.variable_names.begin(),
            graph.problem.variable_names.end(), "volume.total_energy") -
        graph.problem.variable_names.begin());
    require_close(
        integrated.trajectory.back().state.at(mass_index), mass,
        1.0e-10, "balanced flow preserves correlated inventory mass");
    require_close(
        integrated.trajectory.back().state.at(energy_index), energy,
        1.0e-6, "balanced enthalpy flow preserves inventory energy");
}

}  // namespace

int main() {
    try {
        test_correlation_evaluates_with_analytic_derivatives();
        test_packaged_zuber_findlay_template_has_physical_limits();
        test_study_operating_envelope_precedes_native_applicability();
        test_packaged_chisholm_family_selects_phase_regimes();
        test_correlation_contract_rejects_undeclared_symbols();
        test_correlation_enforces_qualified_operating_envelope();
        test_correlation_family_selects_deterministically();
        test_flow_regime_routing_separates_physical_taxonomies();
        test_return_bend_uses_bound_correlation();
        test_two_phase_pipe_uses_bound_void_fraction_correlation();
        test_two_phase_inventory_uses_correlation_for_outlet_quality();
    } catch (const std::exception& error) {
        std::cerr << "correlation tests failed: "
                  << error.what() << '\n';
        return 1;
    }
    std::cout << "thermox_correlation_tests passed\n";
    return 0;
}
