#include "thermox/nonlinear_solver.hpp"
#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/results.hpp"
#include "thermox/physics/ideal_gas_package.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class PtOnlyPropertyPackage final : public thermox::physics::PropertyPackage {
public:
    std::string_view name() const noexcept override { return "pt-only-test"; }
    thermox::physics::PropertyLimits limits() const noexcept override {
        return delegate_.limits();
    }
    bool supports(thermox::physics::PropertyCapability capability) const noexcept override {
        return capability == thermox::physics::PropertyCapability::state_pt;
    }
    thermox::physics::PropertyResult state_pt(double p, double t) const override {
        return delegate_.state_pt(p, t);
    }
    thermox::physics::PropertyResult state_ph(double p, double h) const override {
        return delegate_.state_ph(p, h);
    }
    thermox::physics::PropertyResult state_ps(double p, double s) const override {
        return delegate_.state_ps(p, s);
    }

private:
    thermox::physics::IdealGasPropertyPackage delegate_;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(double actual, double expected, double tolerance, const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message + ": actual=" + std::to_string(actual) +
                                 " expected=" + std::to_string(expected));
    }
}

std::size_t require_variable_index(
    const std::vector<std::string>& names,
    const std::string& expected_name) {
    const auto it = std::find(names.begin(), names.end(), expected_name);
    if (it == names.end()) {
        throw std::runtime_error(
            "missing compiled variable: " + expected_name);
    }
    return static_cast<std::size_t>(
        std::distance(names.begin(), it));
}

std::string write_temp_model(const std::string& name, const std::string& content) {
    const std::string path = "thermox_" + name + ".json";
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("failed to write temporary model: " + path);
    }
    file << content;
    return path;
}

template <typename Func>
void require_throws(Func&& func, const std::string& expected_message_fragment) {
    try {
        func();
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (message.find(expected_message_fragment) == std::string::npos) {
            throw std::runtime_error("exception message did not contain '" + expected_message_fragment +
                                     "': " + message);
        }
        return;
    }
    throw std::runtime_error("expected exception containing: " + expected_message_fragment);
}

void test_generic_model_document_loads_components_connections_and_cases() {
    const std::string path = write_temp_model(
        "generic_valid",
        R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "flexible_cycle",
    "name": "Flexible thermal graph",
    "revision": "rev_001",
    "media": [
      {"id": "air", "backend": "ideal_gas_mixture", "substance": "Air"}
    ],
    "components": [
      {
        "id": "ambient",
        "kind": "source.fluid.boundary",
        "ports": {
          "outlet": {"domain": "fluid", "medium": "air", "direction": "out"}
        },
        "parameters": {
          "p": {"value": 101.325, "unit": "kPa"},
          "T": {"value": 15.0, "unit": "degC"},
          "m_dot": {"value": 360000.0, "unit": "kg/h"}
        }
      },
      {
        "id": "compressor",
        "label": "Main compressor",
        "kind": "compressor.gas.isentropic_efficiency",
        "version": "1.0.0",
        "ports": {
          "inlet": {"domain": "fluid", "medium": "air", "direction": "in"},
          "outlet": {"domain": "fluid", "medium": "air", "direction": "out"},
          "shaft": {"domain": "shaft", "direction": "in"}
        },
        "parameters": {
          "pressure_ratio": 12.0,
          "eta_is": {"value": 86.0, "unit": "%"}
        }
      },
      {
        "id": "turbine",
        "kind": "turbine.gas.isentropic_efficiency",
        "ports": {
          "inlet": {"domain": "fluid", "medium": "air", "direction": "in"},
          "outlet": {"domain": "fluid", "medium": "air", "direction": "out"},
          "shaft": {"domain": "shaft", "direction": "out"}
        },
        "parameters": {
          "eta_is": {"value": 0.89, "unit": "dimensionless"},
          "target_power": {"value": 25.0, "unit": "MW"}
        }
      },
      {
        "id": "exhaust",
        "kind": "sink.fluid.boundary",
        "ports": {
          "inlet": {"domain": "fluid", "medium": "air", "direction": "in"}
        }
      }
    ],
    "connections": [
      {"id": "c1", "from": "ambient.outlet", "to": "compressor.inlet", "kind": "fluid_link"},
      {"id": "c2", "from": "compressor.outlet", "to": "turbine.inlet", "kind": "fluid_link"},
      {"id": "c3", "from": "turbine.outlet", "to": "exhaust.inlet", "kind": "fluid_link"},
      {"id": "shaft", "from": "turbine.shaft", "to": "compressor.shaft", "kind": "shaft_link"}
    ]
  },
  "cases": [
    {
      "id": "design",
      "label": "100% load",
      "mode": "steady_state_design",
      "fixed_values": {
        "ambient.outlet.p": {"value": 1.01325, "unit": "bar"},
        "ambient.outlet.h": {"value": 289.446675, "unit": "kJ/kg"},
        "generator.W_dot": {"value": 20.0, "unit": "MW"}
      },
      "initial_guesses": {
        "compressor.outlet.h": {"value": 650.0, "unit": "kJ/kg"}
      },
      "solver_options": {
        "tolerance": 1.0e-8,
        "max_iterations": 80
      }
    }
  ]
})json");

    const auto document = thermox::platform::load_model_document(path);
    require(document.schema_version == "thermox.model/v1", "schema version should load");
    require(document.model_id == "flexible_cycle", "generic model id should load");
    require(document.media.size() == 1, "one medium should load");
    require(document.components.size() == 4, "four components should load");
    require(document.connections.size() == 4, "four connections should load");
    require(document.cases.size() == 1, "one case should load");

    const auto& compressor = document.components.at(1);
    require(compressor.kind == "compressor.gas.isentropic_efficiency", "component kind should load");
    require(compressor.ports.at("shaft").domain == "shaft", "shaft port should load");
    require_near(compressor.parameters.at("eta_is").value_si, 0.86, 1.0e-12,
                 "percent parameter should normalize");
    require_near(document.components.at(0).parameters.at("p").value_si, 101325.0, 1.0e-9,
                 "component pressure should normalize");
    require_near(document.components.at(0).parameters.at("m_dot").value_si, 100.0, 1.0e-12,
                 "component mass flow should normalize");
    require_near(document.components.at(2).parameters.at("target_power").value_si, 25.0e6, 1.0e-6,
                 "component power should normalize");
    require_near(document.cases.at(0).initial_guesses.at("compressor.outlet.h").value_si, 650000.0,
                 1.0e-9, "specific enthalpy guess should normalize");
}

void test_generic_model_document_rejects_unknown_medium() {
    const std::string path = write_temp_model(
        "generic_unknown_medium",
        R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "bad_medium",
    "media": [],
    "components": [
      {"id": "source", "kind": "source", "ports": {
        "outlet": {"domain": "fluid", "medium": "air", "direction": "out"}
      }}
    ],
    "connections": []
  },
  "cases": []
})json");
    require_throws([&]() { thermox::platform::load_model_document(path); }, "unknown medium referenced");
}

void test_generic_model_document_rejects_invalid_topology() {
    const std::string path = write_temp_model(
        "generic_bad_topology",
        R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "bad_topology",
    "media": [
      {"id": "air", "backend": "ideal_gas_mixture", "substance": "Air"},
      {"id": "water", "backend": "coolprop_if97", "substance": "Water"}
    ],
    "components": [
      {"id": "gas", "kind": "source", "ports": {
        "outlet": {"domain": "fluid", "medium": "air", "direction": "out"}
      }},
      {"id": "steam", "kind": "sink", "ports": {
        "inlet": {"domain": "fluid", "medium": "water", "direction": "in"}
      }}
    ],
    "connections": [
      {"id": "bad_link", "from": "gas.outlet", "to": "steam.inlet", "kind": "fluid_link"}
    ]
  },
  "cases": []
})json");
    require_throws([&]() { thermox::platform::load_model_document(path); }, "incompatible fluid media");
}

void test_generic_model_document_rejects_unsupported_units() {
    const std::string path = write_temp_model(
        "generic_bad_unit",
        R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "bad_unit",
    "media": [{"id": "air", "backend": "ideal_gas_mixture", "substance": "Air"}],
    "components": [
      {"id": "source", "kind": "source", "ports": {
        "outlet": {"domain": "fluid", "medium": "air", "direction": "out"}
      }, "parameters": {"p": {"value": 14.7, "unit": "psi"}}}
    ],
    "connections": []
  },
  "cases": []
})json");
    require_throws([&]() { thermox::platform::load_model_document(path); }, "unsupported unit");
}

void test_component_registry_exposes_default_models() {
    const auto registry = thermox::platform::make_default_component_registry();
    require(registry.contains("source.fluid.boundary"), "default registry should contain fluid source");
    require(registry.contains("compressor.gas.isentropic_efficiency"),
            "default registry should contain compressor");
    require(registry.contains("compressor.fluid.isentropic_efficiency"),
            "default registry should contain generic-fluid compressor");
    require(registry.contains("storage.thermal.lumped"),
            "default registry should contain lumped thermal storage");
    require(registry.contains("source.heat.boundary"),
            "default registry should contain transient heat boundary");
    require(registry.contains("pump.fluid.isentropic_efficiency"),
            "default registry should contain property-aware pump");
    require(registry.contains("junction.fluid.mixer.two_inlet"),
            "default registry should contain two-inlet mixer");
    require(registry.contains("junction.fluid.splitter.two_outlet"),
            "default registry should contain two-outlet splitter");
    require(registry.contains("valve.fluid.isenthalpic_pressure_ratio"),
            "default registry should contain fluid valve");
    require(registry.contains("heat_exchanger.fluid.fixed_duty"),
            "default registry should contain fixed-duty heat exchanger");
    require(registry.contains("volume.fluid.rigid_adiabatic"),
            "default registry should contain rigid fluid volume");
    require(registry.kinds().size() >= 4, "default registry should contain multiple component kinds");
}

void test_component_registry_rejects_unknown_kind() {
    const auto registry = thermox::platform::make_default_component_registry();
    require_throws([&]() { (void)registry.require_model("not.registered"); },
                   "no component model registered");
}

void test_generic_model_compiles_to_connection_equations() {
    const auto document = thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "compile_demo",
    "media": [
      {"id": "air", "backend": "ideal_gas_mixture", "substance": "Air"}
    ],
    "components": [
      {"id": "ambient", "kind": "source.fluid.boundary", "ports": {
        "outlet": {"domain": "fluid", "medium": "air", "direction": "out"}
      }},
      {"id": "compressor", "kind": "compressor.fluid.isentropic_efficiency", "ports": {
        "inlet": {"domain": "fluid", "medium": "air", "direction": "in"},
        "outlet": {"domain": "fluid", "medium": "air", "direction": "out"},
        "shaft": {"domain": "shaft", "direction": "in"}
      }, "parameters": {"pressure_ratio": 12.0, "eta_is": 0.86}}
    ],
    "connections": [
      {"id": "air_link", "from": "ambient.outlet", "to": "compressor.inlet", "kind": "fluid_link"}
    ]
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "ambient.outlet.m_dot": {"value": 100.0, "unit": "kg/s"},
      "ambient.outlet.p": {"value": 101.325, "unit": "kPa"},
      "ambient.outlet.T": {"value": 288.15, "unit": "K"},
      "compressor.shaft.omega": 314.1592653589793
    },
    "initial_guesses": {
      "compressor.inlet.p": {"value": 100.0, "unit": "kPa"}
    }
  }]
})json");

    const auto registry = thermox::platform::make_default_component_registry();
    const auto graph = thermox::platform::compile_model_graph(document, registry, "design");

    require(graph.model_id == "compile_demo", "compiled graph carries model id");
    require(graph.case_id && *graph.case_id == "design", "compiled graph carries selected case id");
    require(graph.port_variables.size() == 11, "compiled graph should expose primary port variables");
    require(graph.connection_equations.size() == 3, "fluid connection lowers conserved variables");
    require(graph.fixed_value_equations.size() == 4, "fixed values lower to equations");
    require(graph.problem.variable_names.size() == 11, "problem has variables");
    require(std::find(graph.problem.variable_names.begin(),
                      graph.problem.variable_names.end(),
                      "ambient.outlet.T") ==
                graph.problem.variable_names.end(),
            "temperature boundary specification is not a solver variable");
    require(static_cast<bool>(graph.problem.partial_sparse_jacobian),
            "property-aware graph provides mixed sparse Jacobian assembly");

    bool saw_pressure_guess = false;
    for (std::size_t i = 0; i < graph.problem.variable_names.size(); ++i) {
        if (graph.problem.variable_names.at(i) == "compressor.inlet.p") {
            require_near(graph.problem.initial_guess.at(i), 100000.0, 1.0e-9,
                         "case initial guess should seed compiled variable");
            saw_pressure_guess = true;
        }
    }
    require(saw_pressure_guess, "compiled variables should include compressor inlet pressure");

    std::vector<double> residual(graph.problem.residual_names.size(), 0.0);
    std::vector<double> x = graph.problem.initial_guess;
    for (std::size_t i = 0; i < graph.problem.variable_names.size(); ++i) {
        if (graph.problem.variable_names.at(i) == "ambient.outlet.p") {
            x.at(i) = 101325.0;
        }
        if (graph.problem.variable_names.at(i) == "compressor.inlet.p") {
            x.at(i) = 101325.0;
        }
    }
    graph.problem.residual(x, residual);
    require_near(residual.at(0), 0.0, 1.0e-12, "mass-flow connection residual");
}


void test_generic_model_solves_ideal_gas_compressor_residuals() {
    const auto document = thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "compressor_physics",
    "media": [
      {"id": "air", "backend": "ideal_gas_mixture", "substance": "Air"}
    ],
    "components": [
      {"id": "compressor", "kind": "compressor.fluid.isentropic_efficiency", "ports": {
        "inlet": {"domain": "fluid", "medium": "air", "direction": "in"},
        "outlet": {"domain": "fluid", "medium": "air", "direction": "out"},
        "shaft": {"domain": "shaft", "direction": "in"}
      }, "parameters": {
        "pressure_ratio": 12.0,
        "eta_is": 0.86,
        "cp": {"value": 1.0045, "unit": "kJ/kg/K"},
        "gamma": 1.4
      }}
    ],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "compressor.inlet.m_dot": {"value": 100.0, "unit": "kg/s"},
      "compressor.inlet.p": {"value": 101.325, "unit": "kPa"},
      "compressor.inlet.T": {"value": 300.0, "unit": "K"},
      "compressor.shaft.omega": 314.1592653589793
    },
    "initial_guesses": {
      "compressor.outlet.p": {"value": 1200.0, "unit": "kPa"},
      "compressor.outlet.h": {"value": 650.0, "unit": "kJ/kg"},
      "compressor.shaft.W_dot": {"value": 35.0, "unit": "MW"}
    }
  }]
})json");

    const auto registry = thermox::platform::make_default_component_registry();
    const auto graph = thermox::platform::compile_model_graph(document, registry, "design");
    require(graph.problem.variable_names.size() == graph.problem.residual_names.size(),
            "compressor physical residual problem should be square");

    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged, result.diagnostics.message);

    double outlet_pressure = 0.0;
    double outlet_enthalpy = 0.0;
    double shaft_power = 0.0;
    for (std::size_t i = 0; i < graph.problem.variable_names.size(); ++i) {
        const auto& name = graph.problem.variable_names.at(i);
        if (name == "compressor.outlet.p") {
            outlet_pressure = result.x.at(i);
        } else if (name == "compressor.outlet.h") {
            outlet_enthalpy = result.x.at(i);
        } else if (name == "compressor.shaft.W_dot") {
            shaft_power = result.x.at(i);
        }
    }

    constexpr double gamma = 1.4;
    constexpr double cp = 1004.5;
    const double expected_temperature = 300.0 *
        (1.0 + (std::pow(12.0, (gamma - 1.0) / gamma) - 1.0) / 0.86);
    const double expected_enthalpy = cp * expected_temperature;
    const double expected_power = 100.0 * (expected_enthalpy - cp * 300.0);

    require_near(outlet_pressure, 12.0 * 101325.0, 1.0e-5, "compressor outlet pressure");
    require_near(outlet_enthalpy, expected_enthalpy, 1.0e-5, "compressor outlet enthalpy");
    require_near(outlet_enthalpy / cp, expected_temperature, 1.0e-8,
                 "compressor derived outlet temperature");
    require_near(shaft_power, expected_power, 1.0e-2, "compressor shaft power");

    const auto port_results =
        thermox::platform::evaluate_fluid_port_results(
            document, graph, result.x);
    require(port_results.size() == 2,
            "compressor exposes two derived fluid-port results");
    for (const auto& port : port_results) {
        if (port.port_name == "outlet") {
            require_near(port.state.temperature_k,
                         expected_temperature, 1.0e-8,
                         "result layer derives compressor outlet temperature");
        }
    }
}

void test_generic_model_solves_ideal_gas_turbine_residuals() {
    const auto document = thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "turbine_physics",
    "media": [
      {"id": "air", "backend": "ideal_gas_mixture", "substance": "Air"}
    ],
    "components": [
      {"id": "turbine", "kind": "turbine.gas.isentropic_efficiency", "ports": {
        "inlet": {"domain": "fluid", "medium": "air", "direction": "in"},
        "outlet": {"domain": "fluid", "medium": "air", "direction": "out"},
        "shaft": {"domain": "shaft", "direction": "out"}
      }, "parameters": {
        "pressure_ratio": 12.0,
        "eta_is": 0.89,
        "cp": {"value": 1.0045, "unit": "kJ/kg/K"},
        "gamma": 1.4
      }}
    ],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "turbine.inlet.m_dot": {"value": 100.0, "unit": "kg/s"},
      "turbine.inlet.p": {"value": 1215.9, "unit": "kPa"},
      "turbine.inlet.h": {"value": 1406.3, "unit": "kJ/kg"},
      "turbine.shaft.omega": 314.1592653589793
    },
    "initial_guesses": {
      "turbine.outlet.p": {"value": 101.325, "unit": "kPa"},
      "turbine.outlet.h": {"value": 803.6, "unit": "kJ/kg"},
      "turbine.shaft.W_dot": {"value": 60.0, "unit": "MW"}
    }
  }]
})json");

    const auto registry = thermox::platform::make_default_component_registry();
    const auto graph = thermox::platform::compile_model_graph(document, registry, "design");
    require(graph.problem.variable_names.size() == graph.problem.residual_names.size(),
            "turbine physical residual problem should be square");

    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged, result.diagnostics.message);

    double outlet_pressure = 0.0;
    double outlet_enthalpy = 0.0;
    double shaft_power = 0.0;
    for (std::size_t i = 0; i < graph.problem.variable_names.size(); ++i) {
        const auto& name = graph.problem.variable_names.at(i);
        if (name == "turbine.outlet.p") {
            outlet_pressure = result.x.at(i);
        } else if (name == "turbine.outlet.h") {
            outlet_enthalpy = result.x.at(i);
        } else if (name == "turbine.shaft.W_dot") {
            shaft_power = result.x.at(i);
        }
    }

    constexpr double gamma = 1.4;
    constexpr double cp = 1004.5;
    const double pressure_ratio = 12.0;
    const double expected_temperature = 1400.0 *
        (1.0 - 0.89 * (1.0 - std::pow(1.0 / pressure_ratio,
                                        (gamma - 1.0) / gamma)));
    const double expected_enthalpy = cp * expected_temperature;
    const double expected_power = 100.0 * (cp * 1400.0 - expected_enthalpy);

    require_near(outlet_pressure, 1215900.0 / pressure_ratio, 1.0e-5,
                 "turbine outlet pressure");
    require_near(outlet_enthalpy, expected_enthalpy, 1.0e-3, "turbine outlet enthalpy");
    require_near(outlet_enthalpy / cp, expected_temperature, 1.0e-6,
                 "turbine derived outlet temperature");
    require_near(shaft_power, expected_power, 1.0e-1, "turbine shaft power");
}

void test_generic_model_solves_two_inlet_mixer() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "ideal_gas_mixer",
    "media": [
      {"id": "air", "backend": "ideal_gas_mixture", "substance": "Air"}
    ],
    "components": [
      {
        "id": "mixer",
        "kind": "junction.fluid.mixer.two_inlet",
        "ports": {
          "inlet_a": {"domain": "fluid", "medium": "air", "direction": "in"},
          "inlet_b": {"domain": "fluid", "medium": "air", "direction": "in"},
          "outlet": {"domain": "fluid", "medium": "air", "direction": "out"}
        }
      }
    ],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "mixer.inlet_a.m_dot": {"value": 2.0, "unit": "kg/s"},
      "mixer.inlet_a.p": {"value": 1.0, "unit": "bar"},
      "mixer.inlet_a.h": {"value": 300.0, "unit": "kJ/kg"},
      "mixer.inlet_b.m_dot": {"value": 1.0, "unit": "kg/s"},
      "mixer.inlet_b.h": {"value": 600.0, "unit": "kJ/kg"}
    }
  }]
})json");

    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto graph = thermox::platform::compile_model_graph(
        document, registry, "design");
    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged,
            result.diagnostics.message);

    double outlet_mass = 0.0;
    double outlet_pressure = 0.0;
    double outlet_enthalpy = 0.0;
    for (std::size_t i = 0;
         i < graph.problem.variable_names.size(); ++i) {
        const auto& name = graph.problem.variable_names.at(i);
        if (name == "mixer.outlet.m_dot") {
            outlet_mass = result.x.at(i);
        } else if (name == "mixer.outlet.p") {
            outlet_pressure = result.x.at(i);
        } else if (name == "mixer.outlet.h") {
            outlet_enthalpy = result.x.at(i);
        }
    }
    require_near(outlet_mass, 3.0, 1.0e-10,
                 "mixer conserves mass");
    require_near(outlet_pressure, 1.0e5, 1.0e-8,
                 "mixer equalizes pressure");
    require_near(outlet_enthalpy, 4.0e5, 1.0e-7,
                 "mixer conserves enthalpy flow");
}

void test_generic_model_solves_two_outlet_splitter() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "ideal_gas_splitter",
    "media": [
      {"id": "air", "backend": "ideal_gas_mixture", "substance": "Air"}
    ],
    "components": [
      {
        "id": "splitter",
        "kind": "junction.fluid.splitter.two_outlet",
        "ports": {
          "inlet": {"domain": "fluid", "medium": "air", "direction": "in"},
          "outlet_a": {"domain": "fluid", "medium": "air", "direction": "out"},
          "outlet_b": {"domain": "fluid", "medium": "air", "direction": "out"}
        }
      }
    ],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "splitter.inlet.m_dot": {"value": 10.0, "unit": "kg/s"},
      "splitter.inlet.p": {"value": 1.0, "unit": "bar"},
      "splitter.inlet.h": {"value": 400.0, "unit": "kJ/kg"},
      "splitter.outlet_a.m_dot": {"value": 4.0, "unit": "kg/s"}
    }
  }]
})json");

    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto graph = thermox::platform::compile_model_graph(
        document, registry, "design");
    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged,
            result.diagnostics.message);

    double outlet_b_mass = 0.0;
    double outlet_b_pressure = 0.0;
    double outlet_b_enthalpy = 0.0;
    for (std::size_t i = 0;
         i < graph.problem.variable_names.size(); ++i) {
        const auto& name = graph.problem.variable_names.at(i);
        if (name == "splitter.outlet_b.m_dot") {
            outlet_b_mass = result.x.at(i);
        } else if (name == "splitter.outlet_b.p") {
            outlet_b_pressure = result.x.at(i);
        } else if (name == "splitter.outlet_b.h") {
            outlet_b_enthalpy = result.x.at(i);
        }
    }
    require_near(outlet_b_mass, 6.0, 1.0e-10,
                 "splitter conserves mass");
    require_near(outlet_b_pressure, 1.0e5, 1.0e-8,
                 "splitter propagates pressure");
    require_near(outlet_b_enthalpy, 4.0e5, 1.0e-8,
                 "splitter propagates enthalpy");
}

void test_generic_model_solves_isenthalpic_valve() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "isenthalpic_valve",
    "media": [
      {"id": "water", "backend": "water_steam_if97", "substance": "Water"}
    ],
    "components": [{
      "id": "valve",
      "kind": "valve.fluid.isenthalpic_pressure_ratio",
      "ports": {
        "inlet": {"domain": "fluid", "medium": "water", "direction": "in"},
        "outlet": {"domain": "fluid", "medium": "water", "direction": "out"}
      },
      "parameters": {"pressure_ratio": 10.0}
    }],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "valve.inlet.m_dot": {"value": 5.0, "unit": "kg/s"},
      "valve.inlet.p": {"value": 10.0, "unit": "MPa"},
      "valve.inlet.h": {"value": 1200.0, "unit": "kJ/kg"}
    }
  }]
})json");
    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto graph = thermox::platform::compile_model_graph(
        document, registry, "design");
    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged,
            result.diagnostics.message);
    require_near(
        result.x.at(require_variable_index(
            graph.problem.variable_names, "valve.outlet.m_dot")),
        5.0, 1.0e-10, "valve conserves mass");
    require_near(
        result.x.at(require_variable_index(
            graph.problem.variable_names, "valve.outlet.p")),
        1.0e6, 1.0e-6, "valve applies pressure ratio");
    require_near(
        result.x.at(require_variable_index(
            graph.problem.variable_names, "valve.outlet.h")),
        1.2e6, 1.0e-7, "valve preserves enthalpy");
}

void test_generic_model_solves_cross_medium_fixed_duty_heat_exchanger() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "cross_medium_heat_exchanger",
    "media": [
      {"id": "gas", "backend": "ideal_gas_mixture", "substance": "Air"},
      {"id": "water", "backend": "water_steam_if97", "substance": "Water"}
    ],
    "components": [{
      "id": "hx",
      "kind": "heat_exchanger.fluid.fixed_duty",
      "ports": {
        "hot_in": {"domain": "fluid", "medium": "gas", "direction": "in"},
        "hot_out": {"domain": "fluid", "medium": "gas", "direction": "out"},
        "cold_in": {"domain": "fluid", "medium": "water", "direction": "in"},
        "cold_out": {"domain": "fluid", "medium": "water", "direction": "out"}
      },
      "parameters": {
        "heat_duty": {"value": 1.0, "unit": "MW"},
        "hot_pressure_loss_fraction": 0.05,
        "cold_pressure_loss_fraction": 0.02
      }
    }],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "hx.hot_in.m_dot": {"value": 10.0, "unit": "kg/s"},
      "hx.hot_in.p": {"value": 10.0, "unit": "bar"},
      "hx.hot_in.h": {"value": 600.0, "unit": "kJ/kg"},
      "hx.cold_in.m_dot": {"value": 20.0, "unit": "kg/s"},
      "hx.cold_in.p": {"value": 5.0, "unit": "bar"},
      "hx.cold_in.h": {"value": 200.0, "unit": "kJ/kg"}
    }
  }]
})json");
    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto graph = thermox::platform::compile_model_graph(
        document, registry, "design");
    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged,
            result.diagnostics.message);
    const auto value = [&](const std::string& name) {
        return result.x.at(require_variable_index(
            graph.problem.variable_names, name));
    };
    require_near(value("hx.hot_out.h"), 5.0e5, 1.0e-7,
                 "hot side supplies fixed duty");
    require_near(value("hx.cold_out.h"), 2.5e5, 1.0e-7,
                 "cold side receives fixed duty");
    require_near(value("hx.hot_out.p"), 9.5e5, 1.0e-7,
                 "hot pressure loss is applied");
    require_near(value("hx.cold_out.p"), 4.9e5, 1.0e-7,
                 "cold pressure loss is applied");
}

void test_generic_model_solves_if97_pump() {
    const auto properties =
        thermox::physics::make_default_property_package_registry().create(
            "water_steam_if97", "Water");
    const auto inlet_state = properties->state_pt(1.0e5, 300.0);
    const auto outlet_guess = properties->state_pt(5.0e6, 305.0);
    require(inlet_state.ok() && outlet_guess.ok(),
            "IF97 pump test states should be valid");

    std::string model_text = R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "if97_pump",
    "media": [
      {"id": "water", "backend": "water_steam_if97", "substance": "Water"}
    ],
    "components": [
      {
        "id": "pump",
        "kind": "pump.fluid.isentropic_efficiency",
        "ports": {
          "inlet": {"domain": "fluid", "medium": "water", "direction": "in"},
          "outlet": {"domain": "fluid", "medium": "water", "direction": "out"},
          "shaft": {"domain": "shaft", "direction": "in"}
        },
        "parameters": {"pressure_ratio": 50.0, "eta_is": 0.8}
      }
    ],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "pump.inlet.m_dot": {"value": 10.0, "unit": "kg/s"},
      "pump.inlet.p": {"value": 1.0, "unit": "bar"},
      "pump.inlet.h": __INLET_H__,
      "pump.shaft.omega": 314.1592653589793
    },
    "initial_guesses": {
      "pump.outlet.p": {"value": 5.0, "unit": "MPa"},
      "pump.outlet.h": __OUTLET_H__,
      "pump.shaft.W_dot": {"value": 0.1, "unit": "MW"}
    }
  }]
})json";
    const auto replace_number =
        [&model_text](const std::string& token, double value) {
            const auto position = model_text.find(token);
            require(position != std::string::npos,
                    "IF97 model token should exist");
            model_text.replace(position, token.size(),
                               std::to_string(value));
        };
    replace_number("__INLET_H__", inlet_state.state.enthalpy_j_kg);
    replace_number("__OUTLET_H__", outlet_guess.state.enthalpy_j_kg);
    const auto document =
        thermox::platform::parse_model_document_text(model_text);
    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto graph = thermox::platform::compile_model_graph(
        document, registry, "design");
    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged,
            result.diagnostics.message);

    double outlet_pressure = 0.0;
    double outlet_enthalpy = 0.0;
    double shaft_power = 0.0;
    for (std::size_t i = 0;
         i < graph.problem.variable_names.size(); ++i) {
        const auto& name = graph.problem.variable_names.at(i);
        if (name == "pump.outlet.p") {
            outlet_pressure = result.x.at(i);
        } else if (name == "pump.outlet.h") {
            outlet_enthalpy = result.x.at(i);
        } else if (name == "pump.shaft.W_dot") {
            shaft_power = result.x.at(i);
        }
    }
    require_near(outlet_pressure, 5.0e6, 1.0e-3,
                 "IF97 pump outlet pressure");
    require(outlet_enthalpy > inlet_state.state.enthalpy_j_kg,
            "IF97 pump raises enthalpy");
    require(shaft_power > 0.0,
            "IF97 pump consumes shaft power");
}

void test_generic_model_solves_supercritical_co2_compressor() {
    const auto properties =
        thermox::physics::make_default_property_package_registry().create(
            "co2_span_wagner", "CO2");
    const auto inlet_state = properties->state_pt(8.0e6, 350.0);
    const auto outlet_guess = properties->state_pt(16.0e6, 400.0);
    require(inlet_state.ok() && outlet_guess.ok(),
            "sCO2 test states should be valid");

    std::string model_text = R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "sco2_compressor",
    "media": [
      {"id": "co2", "backend": "co2_span_wagner", "substance": "CO2"}
    ],
    "components": [
      {"id": "compressor", "kind": "compressor.fluid.isentropic_efficiency", "ports": {
        "inlet": {"domain": "fluid", "medium": "co2", "direction": "in"},
        "outlet": {"domain": "fluid", "medium": "co2", "direction": "out"},
        "shaft": {"domain": "shaft", "direction": "in"}
      }, "parameters": {
        "pressure_ratio": 2.0,
        "eta_is": 0.82
      }}
    ],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "compressor.inlet.m_dot": {"value": 10.0, "unit": "kg/s"},
      "compressor.inlet.p": {"value": 8.0, "unit": "MPa"},
      "compressor.inlet.h": __INLET_H__,
      "compressor.shaft.omega": 314.1592653589793
    },
    "initial_guesses": {
      "compressor.outlet.p": {"value": 16.0, "unit": "MPa"},
      "compressor.outlet.h": __OUTLET_H__,
      "compressor.shaft.W_dot": {"value": 1.0, "unit": "MW"}
    }
  }]
})json";
    const auto replace_number =
        [&model_text](const std::string& token, double value) {
            const auto position = model_text.find(token);
            require(position != std::string::npos,
                    "sCO2 model token should exist");
            model_text.replace(position, token.size(),
                               std::to_string(value));
        };
    replace_number("__INLET_H__", inlet_state.state.enthalpy_j_kg);
    replace_number("__OUTLET_H__", outlet_guess.state.enthalpy_j_kg);
    const auto document =
        thermox::platform::parse_model_document_text(model_text);

    const auto registry = thermox::platform::make_default_component_registry();
    const auto graph =
        thermox::platform::compile_model_graph(document, registry, "design");
    require(static_cast<bool>(graph.problem.checked_residual),
            "real-fluid graph uses checked property residuals");
    thermox::SolverOptions options;
    options.max_iterations = 80;
    options.residual_tolerance = 1e-8;
    const auto result = thermox::solve_newton(graph.problem, options);
    require(result.diagnostics.converged, result.diagnostics.message);

    double inlet_h = 0.0;
    double outlet_h = 0.0;
    double outlet_p = 0.0;
    double shaft_w = 0.0;
    for (std::size_t i = 0; i < graph.problem.variable_names.size(); ++i) {
        const auto& name = graph.problem.variable_names.at(i);
        if (name == "compressor.inlet.h") inlet_h = result.x.at(i);
        if (name == "compressor.outlet.h") outlet_h = result.x.at(i);
        if (name == "compressor.outlet.p") outlet_p = result.x.at(i);
        if (name == "compressor.shaft.W_dot") shaft_w = result.x.at(i);
    }
    const auto solved_outlet = properties->state_ph(outlet_p, outlet_h);
    require(solved_outlet.ok(), "sCO2 solved outlet state should be valid");
    require_near(outlet_p, 16e6, 1e-4, "sCO2 compressor outlet pressure");
    require(solved_outlet.state.temperature_k > 350.0,
            "sCO2 compression raises derived temperature");
    require(outlet_h > inlet_h, "sCO2 compression raises enthalpy");
    require_near(shaft_w, 10.0 * (outlet_h - inlet_h), 1e-3,
                 "sCO2 compressor energy balance");
}

void test_generic_model_compiler_rejects_unregistered_component_kind() {
    const auto document = thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "unregistered_kind",
    "media": [{"id": "air", "backend": "ideal_gas_mixture", "substance": "Air"}],
    "components": [
      {"id": "x", "kind": "not.registered", "ports": {
        "outlet": {"domain": "fluid", "medium": "air", "direction": "out"}
      }}
    ],
    "connections": []
  },
  "cases": []
})json");
    const auto registry = thermox::platform::make_default_component_registry();
    require_throws([&]() { (void)thermox::platform::compile_model_graph(document, registry); },
                   "no component model registered");
}

void test_generic_model_compiler_rejects_bad_port_contract() {
    const auto document = thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "bad_contract",
    "media": [{"id": "air", "backend": "ideal_gas_mixture", "substance": "Air"}],
    "components": [
      {"id": "compressor", "kind": "compressor.gas.isentropic_efficiency", "ports": {
        "inlet": {"domain": "fluid", "medium": "air", "direction": "in"},
        "outlet": {"domain": "fluid", "medium": "air", "direction": "out"}
      }}
    ],
    "connections": []
  },
  "cases": []
})json");
    const auto registry = thermox::platform::make_default_component_registry();
    require_throws([&]() { (void)thermox::platform::compile_model_graph(document, registry); },
                   "missing required port: shaft");
}

void test_generic_model_compiler_rejects_unknown_case_variable() {
    const auto document = thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "unknown_case_var",
    "media": [{"id": "air", "backend": "ideal_gas_mixture", "substance": "Air"}],
    "components": [
      {"id": "ambient", "kind": "source.fluid.boundary", "ports": {
        "outlet": {"domain": "fluid", "medium": "air", "direction": "out"}
      }}
    ],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {"missing.port.p": {"value": 1.0, "unit": "bar"}}
  }]
})json");
    const auto registry = thermox::platform::make_default_component_registry();
    require_throws([&]() { (void)thermox::platform::compile_model_graph(document, registry, "design"); },
                   "fixed value references unknown variable");
}

void test_compiler_reports_under_and_over_specification() {
    const auto under_specified =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "under_specified",
    "media": [
      {"id": "air", "backend": "ideal_gas_mixture", "substance": "Air"}
    ],
    "components": [
      {"id": "source", "kind": "source.fluid.boundary", "ports": {
        "outlet": {"domain": "fluid", "medium": "air", "direction": "out"}
      }}
    ],
    "connections": []
  },
  "cases": [{"id": "design", "mode": "steady_state_design"}]
})json");
    const auto registry =
        thermox::platform::make_default_component_registry();
    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                under_specified, registry, "design");
        },
        "under-specified: 3 variables and 0 equations");

    const auto over_specified =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "over_specified",
    "media": [
      {"id": "air", "backend": "ideal_gas_mixture", "substance": "Air"}
    ],
    "components": [
      {"id": "compressor", "kind": "compressor.fluid.isentropic_efficiency", "ports": {
        "inlet": {"domain": "fluid", "medium": "air", "direction": "in"},
        "outlet": {"domain": "fluid", "medium": "air", "direction": "out"},
        "shaft": {"domain": "shaft", "direction": "in"}
      }, "parameters": {"pressure_ratio": 2.0, "eta_is": 0.8}}
    ],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "compressor.inlet.m_dot": {"value": 1.0, "unit": "kg/s"},
      "compressor.inlet.p": {"value": 1.0, "unit": "bar"},
      "compressor.inlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "compressor.outlet.p": {"value": 2.0, "unit": "bar"},
      "compressor.shaft.omega": 300.0
    }
  }]
})json");
    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                over_specified, registry, "design");
        },
        "over-specified: 8 variables and 9 equations");
}

void test_component_property_capabilities_are_validated() {
    const auto document = thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "capability_check",
    "media": [{"id": "limited", "backend": "pt_only", "substance": "Test"}],
    "components": [
      {"id": "compressor", "kind": "compressor.fluid.isentropic_efficiency", "ports": {
        "inlet": {"domain": "fluid", "medium": "limited", "direction": "in"},
        "outlet": {"domain": "fluid", "medium": "limited", "direction": "out"},
        "shaft": {"domain": "shaft", "direction": "in"}
      }, "parameters": {"pressure_ratio": 2.0, "eta_is": 0.8}}
    ],
    "connections": []
  },
  "cases": []
})json");
    auto properties = thermox::physics::make_default_property_package_registry();
    properties.register_backend("pt_only", [](std::string_view) {
        return std::make_shared<PtOnlyPropertyPackage>();
    });
    const auto components = thermox::platform::make_default_component_registry();
    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                document, components, properties);
        },
        "requires property capability 'state_ph'");
}

void test_transient_model_compiles_and_integrates_lumped_storage() {
    const auto document = thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "thermal_storage_transient",
    "media": [],
    "components": [
      {
        "id": "heater",
        "kind": "source.heat.boundary",
        "ports": {
          "outlet": {
            "domain": "heat",
            "direction": "out"
          }
        }
      },
      {
        "id": "store",
        "kind": "storage.thermal.lumped",
        "ports": {
          "thermal": {
            "domain": "heat",
            "direction": "in"
          }
        },
        "parameters": {
          "thermal_capacity": {"value": 2.0, "unit": "MJ/K"}
        }
      }
    ],
    "connections": [
      {
        "id": "charging_heat",
        "from": "heater.outlet",
        "to": "store.thermal",
        "kind": "heat_link"
      }
    ]
  },
  "cases": [
    {
      "id": "charge",
      "mode": "dynamic_transient",
      "fixed_values": {
        "heater.outlet.Q_dot": {"value": 1.0, "unit": "MW"}
      },
      "initial_guesses": {
        "store.temperature": {"value": 300.0, "unit": "K"}
      }
    }
  ]
})json");

    require_near(
        document.components.at(1).parameters.at("thermal_capacity").value_si,
        2.0e6, 1.0e-9, "thermal capacity normalizes to J/K");

    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto graph =
        thermox::platform::compile_transient_model_graph(
            document, registry, "charge");
    require(graph.problem.variable_names.size() == 5,
            "connected transient graph exposes boundary, port, and internal states");
    require(graph.internal_variables.size() == 1,
            "compiled graph reports the internal storage state");
    require(graph.connection_equations.size() == 2,
            "heat link compiles both canonical connection constraints");
    require(graph.problem.sparse_jacobian_pattern.has_value(),
            "compiled transient graph has a fixed sparse Jacobian");

    std::size_t temperature = graph.problem.variable_names.size();
    for (std::size_t i = 0;
         i < graph.problem.variable_names.size(); ++i) {
        if (graph.problem.variable_names.at(i) ==
            "store.temperature") {
            temperature = i;
        }
    }
    require(temperature < graph.problem.variable_names.size(),
            "compiled graph exposes storage temperature");
    require(graph.problem.variable_kinds.at(temperature) ==
                thermox::DaeVariableKind::differential,
            "storage temperature is a differential state");

    const auto initialized =
        thermox::make_consistent_initial_conditions(
            graph.problem, 0.0);
    require(initialized.diagnostics.converged,
            initialized.diagnostics.message);
    require_near(initialized.derivative.at(temperature), 0.5,
                 1.0e-9,
                 "compiled accumulation equation initializes Tdot");

    thermox::TimeIntegrationOptions options;
    options.end_time = 10.0;
    options.initial_step = 1.0;
    options.max_step = 2.0;
    const auto result =
        thermox::integrate_dae(graph.problem, options);
    require(result.diagnostics.success,
            result.diagnostics.message);
    require_near(
        result.trajectory.back().state.at(temperature),
        305.0, 1.0e-7,
        "compiled storage follows energy accumulation");
}

void test_transient_model_integrates_rigid_fluid_volume() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "rigid_fluid_volume_transient",
    "media": [
      {"id": "air", "backend": "ideal_gas_mixture", "substance": "Air"}
    ],
    "components": [
      {
        "id": "source",
        "kind": "source.fluid.boundary",
        "ports": {
          "outlet": {"domain": "fluid", "medium": "air", "direction": "out"}
        }
      },
      {
        "id": "tank",
        "kind": "volume.fluid.rigid_adiabatic",
        "ports": {
          "inlet": {"domain": "fluid", "medium": "air", "direction": "in"},
          "outlet": {"domain": "fluid", "medium": "air", "direction": "out"}
        },
        "parameters": {
          "volume": {"value": 1000.0, "unit": "L"}
        }
      },
      {
        "id": "sink",
        "kind": "sink.fluid.boundary",
        "ports": {
          "inlet": {"domain": "fluid", "medium": "air", "direction": "in"}
        }
      }
    ],
    "connections": [
      {"id": "feed", "from": "source.outlet", "to": "tank.inlet", "kind": "fluid_link"},
      {"id": "discharge", "from": "tank.outlet", "to": "sink.inlet", "kind": "fluid_link"}
    ]
  },
  "cases": [{
    "id": "fill",
    "mode": "dynamic_transient",
    "fixed_values": {
      "source.outlet.m_dot": {"value": 2.0, "unit": "kg/s"},
      "source.outlet.p": {"value": 1.01325, "unit": "bar"},
      "source.outlet.h": {"value": 301.35, "unit": "kJ/kg"},
      "sink.inlet.m_dot": {"value": 1.0, "unit": "kg/s"}
    },
    "initial_guesses": {
      "tank.mass": {"value": 1.17683, "unit": "kg"},
      "tank.total_energy": {"value": 253.33, "unit": "kJ"},
      "tank.pressure": {"value": 1.01325, "unit": "bar"},
      "tank.enthalpy": {"value": 301.35, "unit": "kJ/kg"}
    }
  }]
})json");
    require_near(
        document.components.at(1).parameters.at("volume").value_si,
        1.0, 1.0e-12, "litres normalize to cubic metres");
    require_near(
        document.cases.at(0).initial_guesses.at(
            "tank.total_energy").value_si,
        253330.0, 1.0e-9, "energy normalizes to joules");

    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto graph =
        thermox::platform::compile_transient_model_graph(
            document, registry, "fill");
    require(graph.problem.sparse_jacobian_pattern.has_value(),
            "fluid volume preserves fixed sparse DAE structure");
    const auto mass = require_variable_index(
        graph.problem.variable_names, "tank.mass");
    const auto energy = require_variable_index(
        graph.problem.variable_names, "tank.total_energy");
    require(graph.problem.variable_kinds.at(mass) ==
                thermox::DaeVariableKind::differential,
            "fluid mass is a differential state");
    require(graph.problem.variable_kinds.at(energy) ==
                thermox::DaeVariableKind::differential,
            "fluid total energy is a differential state");

    const auto initialized =
        thermox::make_consistent_initial_conditions(
            graph.problem, 0.0);
    require(initialized.diagnostics.converged,
            initialized.diagnostics.message);
    require_near(initialized.derivative.at(mass), 1.0,
                 1.0e-8,
                 "fluid mass derivative follows inlet minus outlet flow");

    thermox::TimeIntegrationOptions options;
    options.end_time = 0.1;
    options.initial_step = 0.01;
    options.max_step = 0.02;
    const auto result =
        thermox::integrate_dae(graph.problem, options);
    require(result.diagnostics.success,
            result.diagnostics.message);
    require_near(
        result.trajectory.back().state.at(mass),
        result.trajectory.front().state.at(mass) + 0.1,
        2.0e-7,
        "rigid volume integrates net mass inflow");
}

void test_transient_fluid_volume_closes_with_real_fluid_backends() {
    struct BackendCase {
        std::string backend;
        std::string substance;
        double pressure;
        double temperature;
    };
    const std::vector<BackendCase> cases = {
        {"water_steam_if97", "Water", 1.0e5, 300.0},
        {"co2_span_wagner", "CO2", 8.0e6, 350.0}};
    const auto properties =
        thermox::physics::make_default_property_package_registry();
    const auto components =
        thermox::platform::make_default_component_registry();

    for (const auto& backend_case : cases) {
        const auto package = properties.create(
            backend_case.backend, backend_case.substance);
        const auto state = package->state_pt(
            backend_case.pressure, backend_case.temperature);
        require(state.ok(),
                backend_case.backend +
                    " volume initial state should be valid");
        const double initial_mass = state.state.density_kg_m3;
        const double initial_energy =
            initial_mass * state.state.internal_energy_j_kg;
        std::ostringstream number;
        number << std::setprecision(17);
        const auto format = [&](double value) {
            number.str({});
            number.clear();
            number << value;
            return number.str();
        };
        std::string text = R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "real_fluid_volume",
    "media": [{"id": "fluid", "backend": "__BACKEND__", "substance": "__SUBSTANCE__"}],
    "components": [{
      "id": "tank",
      "kind": "volume.fluid.rigid_adiabatic",
      "ports": {
        "inlet": {"domain": "fluid", "medium": "fluid", "direction": "in"},
        "outlet": {"domain": "fluid", "medium": "fluid", "direction": "out"}
      },
      "parameters": {"volume": {"value": 1.0, "unit": "m3"}}
    }],
    "connections": []
  },
  "cases": [{
    "id": "hold",
    "mode": "dynamic_initialization",
    "fixed_values": {
      "tank.inlet.m_dot": {"value": 1.0, "unit": "kg/s"},
      "tank.inlet.p": __PRESSURE__,
      "tank.inlet.h": __ENTHALPY__,
      "tank.outlet.m_dot": {"value": 1.0, "unit": "kg/s"}
    },
    "initial_guesses": {
      "tank.mass": __MASS__,
      "tank.total_energy": __ENERGY__,
      "tank.pressure": __PRESSURE__,
      "tank.enthalpy": __ENTHALPY__
    }
  }]
})json";
        const auto replace = [&](const std::string& token,
                                 const std::string& value) {
            std::size_t position = 0;
            while ((position = text.find(token, position)) !=
                   std::string::npos) {
                text.replace(position, token.size(), value);
                position += value.size();
            }
        };
        replace("__BACKEND__", backend_case.backend);
        replace("__SUBSTANCE__", backend_case.substance);
        replace("__PRESSURE__", format(backend_case.pressure));
        replace("__ENTHALPY__",
                format(state.state.enthalpy_j_kg));
        replace("__MASS__", format(initial_mass));
        replace("__ENERGY__", format(initial_energy));

        const auto document =
            thermox::platform::parse_model_document_text(text);
        const auto graph =
            thermox::platform::compile_transient_model_graph(
                document, components, properties, "hold");
        const auto initialized =
            thermox::make_consistent_initial_conditions(
                graph.problem, 0.0);
        require(initialized.diagnostics.converged,
                backend_case.backend + ": " +
                    initialized.diagnostics.message);
        const auto mass = require_variable_index(
            graph.problem.variable_names, "tank.mass");
        const auto energy = require_variable_index(
            graph.problem.variable_names, "tank.total_energy");
        require_near(initialized.derivative.at(mass), 0.0,
                     1.0e-9,
                     backend_case.backend +
                         " volume has zero mass accumulation");
        require_near(initialized.derivative.at(energy), 0.0,
                     1.0e-4,
                     backend_case.backend +
                         " volume has zero energy accumulation");
    }
}

void test_transient_compiler_rejects_steady_only_components() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "invalid_dynamic_component",
    "media": [
      {"id": "air", "backend": "ideal_gas_mixture", "substance": "Air"}
    ],
    "components": [
      {
        "id": "valve",
        "kind": "valve.fluid.isenthalpic_pressure_ratio",
        "ports": {
          "inlet": {
            "domain": "fluid",
            "medium": "air",
            "direction": "in"
          },
          "outlet": {
            "domain": "fluid",
            "medium": "air",
            "direction": "out"
          }
        },
        "parameters": {"pressure_ratio": 2.0}
      }
    ],
    "connections": []
  },
  "cases": [
    {"id": "dynamic", "mode": "dynamic_transient"}
  ]
})json");
    const auto registry =
        thermox::platform::make_default_component_registry();
    require_throws(
        [&]() {
            (void)thermox::platform::compile_transient_model_graph(
                document, registry, "dynamic");
        },
        "does not support transient compilation");
}

void test_transient_compiler_rejects_fixed_differential_state() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "fixed_dynamic_state",
    "media": [],
    "components": [
      {
        "id": "store",
        "kind": "storage.thermal.lumped",
        "ports": {
          "thermal": {
            "domain": "heat",
            "direction": "bidirectional"
          }
        },
        "parameters": {"thermal_capacity": 1000.0}
      }
    ],
    "connections": []
  },
  "cases": [
    {
      "id": "dynamic",
      "mode": "dynamic_transient",
      "fixed_values": {
        "store.thermal.Q_dot": 0.0,
        "store.temperature": 300.0
      }
    }
  ]
})json");
    const auto registry =
        thermox::platform::make_default_component_registry();
    require_throws(
        [&]() {
            (void)thermox::platform::compile_transient_model_graph(
                document, registry, "dynamic");
        },
        "cannot fix differential variable");
}

}  // namespace

int main() {
    try {
        test_generic_model_document_loads_components_connections_and_cases();
        test_generic_model_document_rejects_unknown_medium();
        test_generic_model_document_rejects_invalid_topology();
        test_generic_model_document_rejects_unsupported_units();
        test_component_registry_exposes_default_models();
        test_component_registry_rejects_unknown_kind();
        test_generic_model_compiles_to_connection_equations();
        test_generic_model_solves_ideal_gas_compressor_residuals();
        test_generic_model_solves_ideal_gas_turbine_residuals();
        test_generic_model_solves_two_inlet_mixer();
        test_generic_model_solves_two_outlet_splitter();
        test_generic_model_solves_isenthalpic_valve();
        test_generic_model_solves_cross_medium_fixed_duty_heat_exchanger();
        test_generic_model_solves_if97_pump();
        test_generic_model_solves_supercritical_co2_compressor();
        test_generic_model_compiler_rejects_unregistered_component_kind();
        test_generic_model_compiler_rejects_bad_port_contract();
        test_generic_model_compiler_rejects_unknown_case_variable();
        test_compiler_reports_under_and_over_specification();
        test_component_property_capabilities_are_validated();
        test_transient_model_compiles_and_integrates_lumped_storage();
        test_transient_model_integrates_rigid_fluid_volume();
        test_transient_fluid_volume_closes_with_real_fluid_backends();
        test_transient_compiler_rejects_steady_only_components();
        test_transient_compiler_rejects_fixed_differential_state();
    } catch (const std::exception& ex) {
        std::cerr << "test failure: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "thermox_platform_tests passed\n";
    return 0;
}
