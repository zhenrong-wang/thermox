#include "thermox/examples/brayton_cycle.hpp"
#include "thermox/examples/component_registry.hpp"
#include "thermox/examples/ideal_gas.hpp"
#include "thermox/examples/schema.hpp"
#include "thermox/nonlinear_solver.hpp"

#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

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

void test_ideal_gas() {
    const thermox::examples::IdealGas air;
    require_near(air.enthalpy_from_temperature(300.0), 301350.0, 1.0e-9, "ideal gas h(T)");
    require_near(air.isentropic_temperature_out(300.0, 1.0), 300.0, 1.0e-12,
                 "isentropic pressure ratio one");
}

void test_brayton_cycle() {
    thermox::examples::BraytonCycleInput input;
    input.pressure_ratio = 12.0;
    input.turbine_inlet_temperature_k = 1400.0;
    const auto result = thermox::examples::solve_brayton_cycle(input);
    require(result.diagnostics.converged, result.diagnostics.message);
    require(result.net_power_w > 0.0, "Brayton net power should be positive");
    require(result.thermal_efficiency > 0.25 && result.thermal_efficiency < 0.5,
            "Brayton efficiency should be plausible");
    require_near(result.compressor_outlet_temperature_k, 634.5790109, 1.0e-3,
                 "Brayton compressor outlet temperature");
}

void test_brayton_model_loads_canonical_json() {
    const std::string path = write_temp_model(
        "canonical",
        R"json({
  "schema_version": "thermox.model/v1",
  "model_id": "brayton_simple",
  "case_id": "design",
  "kind": "example.brayton.simple",
  "ambient_pressure_pa": 101325.0,
  "ambient_temperature_k": 288.15,
  "pressure_ratio": 12.0,
  "turbine_inlet_temperature_k": 1400.0,
  "compressor_efficiency": 0.86,
  "turbine_efficiency": 0.89,
  "mass_flow_kg_s": 100.0,
  "cp_j_kg_k": 1004.5,
  "gamma": 1.4
})json");

    const auto input = thermox::examples::load_brayton_cycle_model(path);
    require(input.model_id == "brayton_simple", "model id should load");
    require(input.case_id == "design", "case id should load");
    require_near(input.ambient_pressure_pa, 101325.0, 1.0e-12, "ambient pressure canonical");
    require_near(input.cp_j_kg_k, 1004.5, 1.0e-12, "cp canonical");
}

void test_brayton_model_normalizes_units() {
    const std::string path = write_temp_model(
        "units",
        R"json({
  "schema_version": "thermox.model/v1",
  "model_id": "brayton_units",
  "case_id": "design",
  "kind": "example.brayton.simple",
  "ambient_pressure": {"value": 101.325, "unit": "kPa"},
  "ambient_temperature": {"value": 15.0, "unit": "degC"},
  "pressure_ratio": {"value": 12.0, "unit": "dimensionless"},
  "turbine_inlet_temperature": {"value": 1126.85, "unit": "degC"},
  "compressor_efficiency": {"value": 86.0, "unit": "%"},
  "turbine_efficiency": {"value": 89.0, "unit": "%"},
  "mass_flow": {"value": 360000.0, "unit": "kg/h"},
  "cp": {"value": 1.0045, "unit": "kJ/kg/K"},
  "gamma": 1.4
})json");

    const auto input = thermox::examples::load_brayton_cycle_model(path);
    require_near(input.ambient_pressure_pa, 101325.0, 1.0e-9, "pressure unit conversion");
    require_near(input.ambient_temperature_k, 288.15, 1.0e-9, "temperature unit conversion");
    require_near(input.turbine_inlet_temperature_k, 1400.0, 1.0e-9,
                 "turbine temperature conversion");
    require_near(input.compressor_efficiency, 0.86, 1.0e-12, "efficiency percent conversion");
    require_near(input.turbine_efficiency, 0.89, 1.0e-12, "turbine percent conversion");
    require_near(input.mass_flow_kg_s, 100.0, 1.0e-12, "mass flow conversion");
    require_near(input.cp_j_kg_k, 1004.5, 1.0e-12, "cp conversion");
}

void test_brayton_model_rejects_malformed_json() {
    const std::string path = write_temp_model("malformed", R"json({"schema_version": )json");
    require_throws([&]() { thermox::examples::load_brayton_cycle_model(path); }, "invalid JSON");
}

void test_brayton_model_rejects_missing_required_fields() {
    const std::string path = write_temp_model(
        "missing",
        R"json({
  "schema_version": "thermox.model/v1",
  "model_id": "brayton_missing",
  "kind": "example.brayton.simple"
})json");
    require_throws([&]() { thermox::examples::load_brayton_cycle_model(path); },
                   "missing required field: case_id");
}

void test_brayton_model_rejects_invalid_values() {
    const std::string path = write_temp_model(
        "invalid",
        R"json({
  "schema_version": "thermox.model/v1",
  "model_id": "brayton_invalid",
  "case_id": "design",
  "kind": "example.brayton.simple",
  "ambient_pressure_pa": 101325.0,
  "ambient_temperature_k": 288.15,
  "pressure_ratio": 0.9,
  "turbine_inlet_temperature_k": 1400.0,
  "compressor_efficiency": 0.86,
  "turbine_efficiency": 0.89,
  "mass_flow_kg_s": 100.0,
  "cp_j_kg_k": 1004.5,
  "gamma": 1.4
})json");
    require_throws([&]() { thermox::examples::load_brayton_cycle_model(path); },
                   "pressure_ratio must be greater than 1");
}

void test_brayton_model_rejects_unsupported_units() {
    const std::string path = write_temp_model(
        "bad_unit",
        R"json({
  "schema_version": "thermox.model/v1",
  "model_id": "brayton_bad_unit",
  "case_id": "design",
  "kind": "example.brayton.simple",
  "ambient_pressure": {"value": 1.0, "unit": "psi"},
  "ambient_temperature_k": 288.15,
  "pressure_ratio": 12.0,
  "turbine_inlet_temperature_k": 1400.0,
  "compressor_efficiency": 0.86,
  "turbine_efficiency": 0.89,
  "mass_flow_kg_s": 100.0,
  "cp_j_kg_k": 1004.5,
  "gamma": 1.4
})json");
    require_throws([&]() { thermox::examples::load_brayton_cycle_model(path); },
                   "unsupported pressure unit");
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
        "ambient.outlet.T": {"value": 288.15, "unit": "K"},
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

    const auto document = thermox::examples::load_model_document(path);
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
    require_throws([&]() { thermox::examples::load_model_document(path); }, "unknown medium referenced");
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
    require_throws([&]() { thermox::examples::load_model_document(path); }, "incompatible fluid media");
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
    require_throws([&]() { thermox::examples::load_model_document(path); }, "unsupported unit");
}

void test_component_registry_exposes_default_models() {
    const auto registry = thermox::examples::make_default_component_registry();
    require(registry.contains("source.fluid.boundary"), "default registry should contain fluid source");
    require(registry.contains("compressor.gas.isentropic_efficiency"),
            "default registry should contain compressor");
    require(registry.contains("compressor.fluid.isentropic_efficiency"),
            "default registry should contain generic-fluid compressor");
    require(registry.kinds().size() >= 4, "default registry should contain multiple component kinds");
}

void test_component_registry_rejects_unknown_kind() {
    const auto registry = thermox::examples::make_default_component_registry();
    require_throws([&]() { (void)registry.require_model("not.registered"); },
                   "no component model registered");
}

void test_generic_model_compiles_to_connection_equations() {
    const auto document = thermox::examples::parse_model_document_text(R"json({
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
      "ambient.outlet.p": {"value": 101.325, "unit": "kPa"},
      "ambient.outlet.T": {"value": 288.15, "unit": "K"}
    },
    "initial_guesses": {
      "compressor.inlet.p": {"value": 100.0, "unit": "kPa"}
    }
  }]
})json");

    const auto registry = thermox::examples::make_default_component_registry();
    const auto graph = thermox::examples::compile_model_graph(document, registry, "design");

    require(graph.model_id == "compile_demo", "compiled graph carries model id");
    require(graph.case_id && *graph.case_id == "design", "compiled graph carries selected case id");
    require(graph.port_variables.size() == 14, "compiled graph should expose canonical port variables");
    require(graph.connection_equations.size() == 4, "fluid connection lowers to four equations");
    require(graph.fixed_value_equations.size() == 2, "fixed values lower to equations");
    require(graph.problem.variable_names.size() == 14, "problem has variables");
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
        if (graph.problem.variable_names.at(i) == "ambient.outlet.T") {
            x.at(i) = 288.15;
        }
    }
    graph.problem.residual(x, residual);
    require_near(residual.at(0), 0.0, 1.0e-12, "mass-flow connection residual");
}


void test_generic_model_solves_ideal_gas_compressor_residuals() {
    const auto document = thermox::examples::parse_model_document_text(R"json({
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
      "compressor.outlet.T": {"value": 650.0, "unit": "K"},
      "compressor.shaft.W_dot": {"value": 35.0, "unit": "MW"}
    }
  }]
})json");

    const auto registry = thermox::examples::make_default_component_registry();
    const auto graph = thermox::examples::compile_model_graph(document, registry, "design");
    require(graph.problem.variable_names.size() == graph.problem.residual_names.size(),
            "compressor physical residual problem should be square");

    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged, result.diagnostics.message);

    double outlet_pressure = 0.0;
    double outlet_temperature = 0.0;
    double outlet_enthalpy = 0.0;
    double shaft_power = 0.0;
    for (std::size_t i = 0; i < graph.problem.variable_names.size(); ++i) {
        const auto& name = graph.problem.variable_names.at(i);
        if (name == "compressor.outlet.p") {
            outlet_pressure = result.x.at(i);
        } else if (name == "compressor.outlet.T") {
            outlet_temperature = result.x.at(i);
        } else if (name == "compressor.outlet.h") {
            outlet_enthalpy = result.x.at(i);
        } else if (name == "compressor.shaft.W_dot") {
            shaft_power = result.x.at(i);
        }
    }

    const thermox::examples::IdealGas air;
    const double expected_temperature = 300.0 *
        (1.0 + (std::pow(12.0, (air.gamma - 1.0) / air.gamma) - 1.0) / 0.86);
    const double expected_enthalpy = air.enthalpy_from_temperature(expected_temperature);
    const double expected_power = 100.0 * (expected_enthalpy - air.enthalpy_from_temperature(300.0));

    require_near(outlet_pressure, 12.0 * 101325.0, 1.0e-5, "compressor outlet pressure");
    require_near(outlet_temperature, expected_temperature, 1.0e-8, "compressor outlet temperature");
    require_near(outlet_enthalpy, expected_enthalpy, 1.0e-5, "compressor outlet enthalpy");
    require_near(shaft_power, expected_power, 1.0e-2, "compressor shaft power");
}

void test_generic_model_solves_ideal_gas_turbine_residuals() {
    const auto document = thermox::examples::parse_model_document_text(R"json({
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
      "turbine.inlet.T": {"value": 1400.0, "unit": "K"},
      "turbine.shaft.omega": 314.1592653589793
    },
    "initial_guesses": {
      "turbine.outlet.p": {"value": 101.325, "unit": "kPa"},
      "turbine.outlet.T": {"value": 800.0, "unit": "K"},
      "turbine.shaft.W_dot": {"value": 60.0, "unit": "MW"}
    }
  }]
})json");

    const auto registry = thermox::examples::make_default_component_registry();
    const auto graph = thermox::examples::compile_model_graph(document, registry, "design");
    require(graph.problem.variable_names.size() == graph.problem.residual_names.size(),
            "turbine physical residual problem should be square");

    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged, result.diagnostics.message);

    double outlet_pressure = 0.0;
    double outlet_temperature = 0.0;
    double outlet_enthalpy = 0.0;
    double shaft_power = 0.0;
    for (std::size_t i = 0; i < graph.problem.variable_names.size(); ++i) {
        const auto& name = graph.problem.variable_names.at(i);
        if (name == "turbine.outlet.p") {
            outlet_pressure = result.x.at(i);
        } else if (name == "turbine.outlet.T") {
            outlet_temperature = result.x.at(i);
        } else if (name == "turbine.outlet.h") {
            outlet_enthalpy = result.x.at(i);
        } else if (name == "turbine.shaft.W_dot") {
            shaft_power = result.x.at(i);
        }
    }

    const thermox::examples::IdealGas air;
    const double pressure_ratio = 12.0;
    const double expected_temperature = 1400.0 *
        (1.0 - 0.89 * (1.0 - std::pow(1.0 / pressure_ratio,
                                        (air.gamma - 1.0) / air.gamma)));
    const double expected_enthalpy = air.enthalpy_from_temperature(expected_temperature);
    const double expected_power = 100.0 * (air.enthalpy_from_temperature(1400.0) - expected_enthalpy);

    require_near(outlet_pressure, 1215900.0 / pressure_ratio, 1.0e-5,
                 "turbine outlet pressure");
    require_near(outlet_temperature, expected_temperature, 1.0e-6,
                 "turbine outlet temperature");
    require_near(outlet_enthalpy, expected_enthalpy, 1.0e-3, "turbine outlet enthalpy");
    require_near(shaft_power, expected_power, 1.0e-1, "turbine shaft power");
}

void test_generic_model_solves_supercritical_co2_compressor() {
    const auto document = thermox::examples::parse_model_document_text(R"json({
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
      "compressor.inlet.T": {"value": 350.0, "unit": "K"},
      "compressor.shaft.omega": 314.1592653589793
    },
    "initial_guesses": {
      "compressor.outlet.p": {"value": 16.0, "unit": "MPa"},
      "compressor.outlet.T": {"value": 400.0, "unit": "K"},
      "compressor.shaft.W_dot": {"value": 1.0, "unit": "MW"}
    }
  }]
})json");

    const auto registry = thermox::examples::make_default_component_registry();
    const auto graph =
        thermox::examples::compile_model_graph(document, registry, "design");
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
    double outlet_t = 0.0;
    double shaft_w = 0.0;
    for (std::size_t i = 0; i < graph.problem.variable_names.size(); ++i) {
        const auto& name = graph.problem.variable_names.at(i);
        if (name == "compressor.inlet.h") inlet_h = result.x.at(i);
        if (name == "compressor.outlet.h") outlet_h = result.x.at(i);
        if (name == "compressor.outlet.p") outlet_p = result.x.at(i);
        if (name == "compressor.outlet.T") outlet_t = result.x.at(i);
        if (name == "compressor.shaft.W_dot") shaft_w = result.x.at(i);
    }
    require_near(outlet_p, 16e6, 1e-4, "sCO2 compressor outlet pressure");
    require(outlet_t > 350.0, "sCO2 compression raises temperature");
    require(outlet_h > inlet_h, "sCO2 compression raises enthalpy");
    require_near(shaft_w, 10.0 * (outlet_h - inlet_h), 1e-3,
                 "sCO2 compressor energy balance");
}

void test_generic_model_compiler_rejects_unregistered_component_kind() {
    const auto document = thermox::examples::parse_model_document_text(R"json({
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
    const auto registry = thermox::examples::make_default_component_registry();
    require_throws([&]() { (void)thermox::examples::compile_model_graph(document, registry); },
                   "no component model registered");
}

void test_generic_model_compiler_rejects_bad_port_contract() {
    const auto document = thermox::examples::parse_model_document_text(R"json({
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
    const auto registry = thermox::examples::make_default_component_registry();
    require_throws([&]() { (void)thermox::examples::compile_model_graph(document, registry); },
                   "missing required port: shaft");
}

void test_generic_model_compiler_rejects_unknown_case_variable() {
    const auto document = thermox::examples::parse_model_document_text(R"json({
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
    const auto registry = thermox::examples::make_default_component_registry();
    require_throws([&]() { (void)thermox::examples::compile_model_graph(document, registry, "design"); },
                   "fixed value references unknown variable");
}

}  // namespace

int main() {
    try {
        test_ideal_gas();
        test_brayton_cycle();
        test_brayton_model_loads_canonical_json();
        test_brayton_model_normalizes_units();
        test_brayton_model_rejects_malformed_json();
        test_brayton_model_rejects_missing_required_fields();
        test_brayton_model_rejects_invalid_values();
        test_brayton_model_rejects_unsupported_units();
        test_generic_model_document_loads_components_connections_and_cases();
        test_generic_model_document_rejects_unknown_medium();
        test_generic_model_document_rejects_invalid_topology();
        test_generic_model_document_rejects_unsupported_units();
        test_component_registry_exposes_default_models();
        test_component_registry_rejects_unknown_kind();
        test_generic_model_compiles_to_connection_equations();
        test_generic_model_solves_ideal_gas_compressor_residuals();
        test_generic_model_solves_ideal_gas_turbine_residuals();
        test_generic_model_solves_supercritical_co2_compressor();
        test_generic_model_compiler_rejects_unregistered_component_kind();
        test_generic_model_compiler_rejects_bad_port_contract();
        test_generic_model_compiler_rejects_unknown_case_variable();
    } catch (const std::exception& ex) {
        std::cerr << "test failure: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "thermox_brayton_example_tests passed\n";
    return 0;
}
