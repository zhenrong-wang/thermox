#include "thermox/nonlinear_solver.hpp"
#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/correlation.hpp"
#include "thermox/platform/model_document.hpp"
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
        thermox::platform::correlation_artifact_schema_v1,
        "vendor-revision-1",
        std::string(64, 'c'),
        {
            {"mass_flow", "mass_flow"},
            {"density", "density"},
            {"area", "area"},
        },
        {"pressure_loss", "pressure"},
        {{"loss_coefficient", 1.5}},
        "loss_coefficient * mass_flow * abs(mass_flow) / "
        "(2 * density * area * area)",
    };
}

thermox::platform::CorrelationArtifact void_fraction_correlation() {
    return {
        "void-fraction-correlation",
        thermox::platform::correlation_artifact_schema_v1,
        "constant-slip-reference-1",
        std::string(64, 'e'),
        {
            {"vapor_quality", "dimensionless"},
            {"liquid_density", "density"},
            {"vapor_density", "density"},
        },
        {"void_fraction", "dimensionless"},
        {{"slip_ratio", 2.0}},
        "1 / (1 + ((1 - vapor_quality) / vapor_quality) * "
        "(vapor_density / liquid_density) * slip_ratio)",
    };
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
        thermox::platform::correlation_artifact_schema_v1,
        "revision-1",
        std::string(64, 'd'),
        {{"mass_flow", "mass_flow"}},
        {"pressure_loss", "pressure"},
        {},
        "mass_flow * undeclared_coefficient",
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
        thermox::platform::correlation_artifact_schema_v1,
        "revision-1",
        std::string(64, 'a'),
        {{"vapor_quality", "dimensionless"}},
        {"void_fraction", "dimensionless"},
        {},
        "vapor_quality",
        {{"vapor_quality", 0.1, 0.8, true, false}},
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
        thermox::platform::correlation_artifact_schema_v1,
        "revision-1",
        std::string(64, 'b'),
        {{"vapor_quality", "dimensionless"}},
        {"void_fraction", "dimensionless"},
        {},
        "vapor_quality",
        {{"unknown_input", 0.0, 1.0, true, true}},
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
      "kind": "pipe.fluid.void_fraction_correlation_local_loss",
      "parameters": {
        "flow_diameter": {"value": 0.1, "unit": "m"},
        "loss_coefficient": 2.0,
        "elevation_change": {"value": 5.0, "unit": "m"}
      },
      "artifacts": {
        "void_fraction_correlation": "void-fraction-correlation"
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
    artifacts.register_artifact(void_fraction_correlation());
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
    const auto alpha = void_fraction_correlation().evaluate({
        {"vapor_quality", state.state.vapor_quality},
        {"liquid_density", saturation.liquid.density_kg_m3},
        {"vapor_density", saturation.vapor.density_kg_m3},
    });
    require(alpha.error.empty() && alpha.value > 0.0 &&
                alpha.value < 1.0,
            "bound correlation must produce physical void fraction");
    const double density =
        alpha.value * saturation.vapor.density_kg_m3 +
        (1.0 - alpha.value) *
            saturation.liquid.density_kg_m3;
    const double area = std::numbers::pi * 0.1 * 0.1 / 4.0;
    const double expected_drop =
        2.0 * 0.2 * 0.2 /
            (2.0 * density * area * area) +
        density * 9.80665 * 5.0;
    require_close(
        1.0e6 - outlet_pressure, expected_drop, 1.0e-5,
        "correlated void fraction closes riser pressure balance");
    require_close(variable("riser.outlet.m_dot"), 0.2, 1.0e-12,
                  "correlated riser conserves mass");
    require_close(variable("riser.outlet.h"), 1.5e6, 1.0e-8,
                  "correlated riser transports enthalpy");

    const auto transient =
        thermox::platform::compile_transient_model_graph(
            document, registry, properties, artifacts, "dynamic");
    const auto initialized =
        thermox::make_consistent_initial_conditions(
            transient.problem, 0.0);
    require(initialized.diagnostics.converged,
            "correlated riser DAE initialization: " +
                initialized.diagnostics.message);

    auto nonphysical_document = document;
    nonphysical_document.components.at(0)
        .artifact_bindings["void_fraction_correlation"] =
        "nonphysical-void-fraction";
    thermox::platform::EngineeringArtifactRegistry
        nonphysical_artifacts;
    nonphysical_artifacts.register_artifact(
        thermox::platform::CorrelationArtifact{
            "nonphysical-void-fraction",
            thermox::platform::correlation_artifact_schema_v1,
            "invalid-range-1", std::string(64, 'f'),
            {{"vapor_quality", "dimensionless"}},
            {"void_fraction", "dimensionless"},
            {{"invalid_alpha", 1.2}},
            "invalid_alpha + 0 * vapor_quality"});
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
            thermox::platform::correlation_artifact_schema_v1,
            "wrong-dimension-1", std::string(64, 'a'),
            {{"vapor_quality", "dimensionless"}},
            {"void_fraction", "pressure"}, {},
            "vapor_quality"});
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
        test_correlation_contract_rejects_undeclared_symbols();
        test_correlation_enforces_qualified_operating_envelope();
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
