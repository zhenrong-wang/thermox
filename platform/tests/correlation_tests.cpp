#include "thermox/nonlinear_solver.hpp"
#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/correlation.hpp"
#include "thermox/platform/model_document.hpp"
#include "thermox/physics/property_registry.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
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

}  // namespace

int main() {
    try {
        test_correlation_evaluates_with_analytic_derivatives();
        test_correlation_contract_rejects_undeclared_symbols();
        test_return_bend_uses_bound_correlation();
    } catch (const std::exception& error) {
        std::cerr << "correlation tests failed: "
                  << error.what() << '\n';
        return 1;
    }
    std::cout << "thermox_correlation_tests passed\n";
    return 0;
}
