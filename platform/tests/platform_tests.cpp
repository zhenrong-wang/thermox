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
    std::string_view version() const noexcept override { return "1.0.0"; }
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
    thermox::physics::SaturationResult saturation_p(double p) const override {
        return delegate_.saturation_p(p);
    }

private:
    thermox::physics::IdealGasPropertyPackage delegate_;
};

class TestThermochemistryPackage final
    : public thermox::physics::ThermochemistryPackage {
public:
    std::string_view name() const noexcept override {
        return "test-thermochemistry";
    }
    std::string_view version() const noexcept override {
        return "1.0.0";
    }
    std::string_view mechanism() const noexcept override {
        return "test.yaml";
    }
    std::string_view phase() const noexcept override {
        return "gas";
    }
    const std::vector<std::string>& species_basis()
        const noexcept override {
        return species_;
    }
    bool supports(
        thermox::physics::ThermochemistryCapability capability)
        const noexcept override {
        return capability ==
                thermox::physics::ThermochemistryCapability::state_ph ||
            capability ==
                thermox::physics::ThermochemistryCapability::state_ps ||
            capability ==
                thermox::physics::ThermochemistryCapability::
                    equilibrium_hp;
    }
    thermox::physics::ThermochemicalResult state_pt(
        double, double,
        const thermox::physics::SpeciesComposition&) const override {
        return {};
    }
    thermox::physics::ThermochemicalResult state_ph(
        double pressure, double enthalpy,
        const thermox::physics::SpeciesComposition&
            composition) const override {
        return state(
            pressure, enthalpy / 1000.0, composition);
    }
    thermox::physics::ThermochemicalResult state_ps(
        double pressure, double entropy,
        const thermox::physics::SpeciesComposition&
            composition) const override {
        const double temperature =
            300.0 * std::exp(
                (entropy +
                 287.0 * std::log(pressure / 101325.0)) /
                1000.0);
        return state(pressure, temperature, composition);
    }
    thermox::physics::ThermochemicalResult equilibrate_hp(
        double pressure, double enthalpy,
        const thermox::physics::SpeciesComposition&
            reactants) const override {
        thermox::physics::ThermochemicalState state;
        state.thermodynamic.pressure_pa = pressure;
        state.thermodynamic.enthalpy_j_kg = enthalpy;
        state.composition = reactants;
        return {
            std::move(state),
            thermox::physics::PropertyStatus::success,
            {}};
    }

private:
    thermox::physics::ThermochemicalResult state(
        double pressure,
        double temperature,
        const thermox::physics::SpeciesComposition&
            composition) const {
        thermox::physics::ThermochemicalState state;
        state.thermodynamic.pressure_pa = pressure;
        state.thermodynamic.temperature_k = temperature;
        state.thermodynamic.enthalpy_j_kg =
            1000.0 * temperature;
        state.thermodynamic.entropy_j_kg_k =
            1000.0 * std::log(temperature / 300.0) -
            287.0 * std::log(pressure / 101325.0);
        state.composition = composition;
        return {
            std::move(state),
            thermox::physics::PropertyStatus::success,
            {}};
    }

    std::vector<std::string> species_{"N2", "O2"};
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

const thermox::platform::ComponentResult& require_component_result(
    const thermox::platform::GraphResult& graph,
    const std::string& component_id) {
    const auto component = std::find_if(
        graph.components.begin(), graph.components.end(),
        [&](const auto& candidate) {
            return candidate.component_id == component_id;
        });
    if (component == graph.components.end()) {
        throw std::runtime_error(
            "missing graph result component: " + component_id);
    }
    return *component;
}

const thermox::platform::PortResult& require_port_result(
    const thermox::platform::GraphResult& graph,
    const std::string& component_id,
    const std::string& port_name) {
    const auto& component =
        require_component_result(graph, component_id);
    const auto port = std::find_if(
        component.ports.begin(), component.ports.end(),
        [&](const auto& candidate) {
            return candidate.port_name == port_name;
        });
    if (port == component.ports.end()) {
        throw std::runtime_error(
            "missing graph result port: " +
            component_id + "." + port_name);
    }
    return *port;
}

double require_result_value(
    const std::vector<thermox::platform::ResultValue>& values,
    const std::string& name) {
    const auto value = std::find_if(
        values.begin(), values.end(),
        [&](const auto& candidate) {
            return candidate.name == name;
        });
    if (value == values.end()) {
        throw std::runtime_error(
            "missing graph result value: " + name);
    }
    return value->value_si;
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

class ArtifactConsumerModel final
    : public thermox::platform::ComponentModel {
public:
    ArtifactConsumerModel() {
        descriptor_.kind = "test.map_consumer";
        descriptor_.version = "1.0.0";
        descriptor_.ports = {
            {"result", "signal", "out"},
        };
        descriptor_.artifacts = {{
            "performance_map",
            thermox::platform::performance_map_artifact_type,
            true,
        }};
    }

    const thermox::platform::ComponentModelDescriptor&
    descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const thermox::platform::ComponentCompileContext& context,
        thermox::EquationSystemBuilder& system) const override {
        const auto found =
            context.performance_maps.find("performance_map");
        if (found == context.performance_maps.end() ||
            !found->second || !found->second->map) {
            throw std::runtime_error(
                "resolved performance map was not supplied");
        }
        system.add_linear_equation(
            "component." + context.component.id + ".result",
            {{context.port_variables.at("result.value"), 1.0}},
            0.0,
            1.0);
    }

private:
    thermox::platform::ComponentModelDescriptor descriptor_;
};

thermox::platform::PerformanceMapArtifact
make_test_map_artifact() {
    return {
        "oem-map",
        thermox::platform::performance_map_artifact_schema_v1,
        "revision-1",
        std::string(64, 'b'),
        std::make_shared<
            const thermox::platform::PerformanceMap>(
            thermox::platform::MapVariable{
                "corrected_flow", "mass_flow"},
            thermox::platform::MapVariable{
                "corrected_speed", "angular_speed"},
            std::vector<thermox::platform::MapVariable>{
                {"pressure_ratio", "dimensionless"}},
            std::vector<thermox::platform::MapCurve>{
                {100.0, {{1.0, {2.0}}, {2.0, {3.0}}}},
                {200.0, {{1.0, {3.0}}, {2.0, {4.0}}}},
            }),
    };
}

void test_component_artifact_bindings_resolve_at_compile_time() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "artifact_binding",
    "media": [],
    "components": [{
      "id": "machine",
      "kind": "test.map_consumer",
      "artifacts": {"performance_map": "oem-map"}
    }],
    "connections": []
  },
  "cases": []
})json");
    require(
        document.components.at(0).artifact_bindings.at(
            "performance_map") == "oem-map",
        "component artifact binding must survive parsing");

    thermox::platform::ComponentRegistry components;
    components.register_model(
        std::make_shared<const ArtifactConsumerModel>());
    thermox::platform::PerformanceMapRegistry maps;
    maps.register_artifact(make_test_map_artifact());
    const auto graph = thermox::platform::compile_model_graph(
        document, components,
        thermox::physics::make_default_property_package_registry(),
        maps);
    require(
        graph.problem.variable_names.size() == 1 &&
            graph.problem.residual_names.size() == 1,
        "artifact-only test model should compile as a square graph");

    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                document, components,
                thermox::physics::
                    make_default_property_package_registry(),
                thermox::platform::PerformanceMapRegistry{});
        },
        "no performance-map artifact registered");
}

void test_generic_model_document_loads_components_connections_and_cases() {
    const std::string path = write_temp_model(
        "generic_valid",
        R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "flexible_cycle",
    "name": "Flexible thermal graph",
    "revision": "rev_001",
    "media": [
      {
        "id": "air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      }
    ],
    "components": [
      {
        "id": "ambient",
        "kind": "source.fluid.boundary",
        "parameters": {
          "p": {
            "value": 101.325,
            "unit": "kPa"
          },
          "T": {
            "value": 15.0,
            "unit": "degC"
          },
          "m_dot": {
            "value": 360000.0,
            "unit": "kg/h"
          }
        },
        "media": {
          "outlet": "air"
        }
      },
      {
        "id": "compressor",
        "label": "Main compressor",
        "kind": "compressor.gas.isentropic_efficiency",
        "version": "1.0.0",
        "parameters": {
          "pressure_ratio": 12.0,
          "eta_is": {
            "value": 86.0,
            "unit": "%"
          }
        },
        "media": {
          "inlet": "air",
          "outlet": "air"
        }
      },
      {
        "id": "turbine",
        "kind": "turbine.gas.isentropic_efficiency",
        "parameters": {
          "eta_is": {
            "value": 0.89,
            "unit": "dimensionless"
          },
          "target_power": {
            "value": 25.0,
            "unit": "MW"
          }
        },
        "media": {
          "inlet": "air",
          "outlet": "air"
        }
      },
      {
        "id": "exhaust",
        "kind": "sink.fluid.boundary",
        "media": {
          "inlet": "air"
        }
      }
    ],
    "connections": [
      {
        "id": "c1",
        "from": "ambient.outlet",
        "to": "compressor.inlet",
        "kind": "fluid_link"
      },
      {
        "id": "c2",
        "from": "compressor.outlet",
        "to": "turbine.inlet",
        "kind": "fluid_link"
      },
      {
        "id": "c3",
        "from": "turbine.outlet",
        "to": "exhaust.inlet",
        "kind": "fluid_link"
      },
      {
        "id": "shaft",
        "from": "turbine.shaft",
        "to": "compressor.shaft",
        "kind": "shaft_link"
      }
    ]
  },
  "cases": [
    {
      "id": "design",
      "label": "100% load",
      "mode": "steady_state_design",
      "fixed_values": {
        "ambient.outlet.p": {
          "value": 1.01325,
          "unit": "bar"
        },
        "ambient.outlet.h": {
          "value": 289.446675,
          "unit": "kJ/kg"
        },
        "generator.W_dot": {
          "value": 20.0,
          "unit": "MW"
        }
      },
      "initial_guesses": {
        "compressor.outlet.h": {
          "value": 650.0,
          "unit": "kJ/kg"
        }
      },
      "solver_options": {
        "tolerance": 1e-08,
        "max_iterations": 80
      }
    }
  ]
})json");

    const auto document = thermox::platform::load_model_document(path);
    require(document.schema_version == "thermox.model/v2", "schema version should load");
    require(document.model_id == "flexible_cycle", "generic model id should load");
    require(document.media.size() == 1, "one medium should load");
    require(document.components.size() == 4, "four components should load");
    require(document.connections.size() == 4, "four connections should load");
    require(document.cases.size() == 1, "one case should load");

    const auto& compressor = document.components.at(1);
    require(compressor.kind == "compressor.gas.isentropic_efficiency", "component kind should load");
    require(
        compressor.medium_bindings.at("inlet") == "air",
        "fluid medium binding should load");
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
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "bad_medium",
    "media": [],
    "components": [
      {
        "id": "source",
        "kind": "source.fluid.boundary",
        "media": {
          "outlet": "air"
        }
      }
    ],
    "connections": []
  },
  "cases": []
})json");
    require_throws([&]() { thermox::platform::load_model_document(path); }, "unknown medium referenced");
}

void test_model_document_supports_component_and_system_calibration() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "calibrated_plant",
    "media": [],
    "components": [
      {
        "id": "compressor",
        "kind": "compressor.fluid.isentropic_efficiency",
        "parameters": {
          "eta_is": 0.86
        }
      },
      {
        "id": "shaft_train",
        "kind": "shaft.train.two_load",
        "parameters": {
          "fixed_loss": {"value": 2.0, "unit": "MW"}
        }
      }
    ],
    "connections": []
  },
  "cases": [
    {"id": "baseline", "mode": "steady_state_design"},
    {"id": "part_load", "mode": "steady_state_off_design"}
  ],
  "calibrations": [
    {
      "id": "acceptance_fit",
      "parameters": [
        {
          "id": "compressor_eta",
          "scope": "component",
          "targets": [
            "components.compressor.parameters.eta_is"
          ],
          "bounds": {"lower": 0.75, "upper": 0.95},
          "prior": {"mean": 0.86, "sigma": 0.02}
        },
        {
          "id": "plant_mechanical_loss",
          "scope": "system",
          "targets": [
            "components.shaft_train.parameters.fixed_loss"
          ],
          "cases": ["baseline", "part_load"],
          "bounds": {
            "lower": {"value": 0.0, "unit": "MW"},
            "upper": {"value": 8.0, "unit": "MW"}
          }
        }
      ],
      "observations": [
        {
          "id": "baseline_net_power",
          "case": "baseline",
          "target": "generator.electrical.P",
          "measured": {"value": 257.5, "unit": "MW"},
          "sigma": {"value": 1.0, "unit": "MW"}
        }
      ]
    }
  ]
})json");

    require(
        document.calibrations.size() == 1,
        "one calibration definition should load");
    const auto& calibration = document.calibrations.front();
    require(
        calibration.parameters.size() == 2,
        "component and system calibration parameters should coexist");
    require(
        calibration.parameters.at(0).scope == "component" &&
            calibration.parameters.at(0).targets.front() ==
                "components.compressor.parameters.eta_is",
        "component calibration should retain physical target ownership");
    require(
        calibration.parameters.at(1).scope == "system" &&
            calibration.parameters.at(1).case_ids.size() == 2,
        "system calibration should support sharing across cases");
    require_near(
        calibration.parameters.at(1).upper_bound->value_si,
        8.0e6, 1.0e-9,
        "calibration bounds should normalize to SI");
    require_near(
        calibration.observations.front().sigma.value_si,
        1.0e6, 1.0e-9,
        "observation uncertainty should normalize to SI");
}

void test_model_document_rejects_invalid_calibration_target() {
    require_throws(
        []() {
            (void)thermox::platform::parse_model_document_text(
                R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "bad_calibration",
    "media": [],
    "components": [],
    "connections": []
  },
  "cases": [
    {"id": "baseline", "mode": "steady_state_design"}
  ],
  "calibrations": [
    {
      "id": "fit",
      "parameters": [
        {
          "id": "hidden_correction",
          "scope": "system",
          "targets": ["system.parameters.magic_factor"]
        }
      ],
      "observations": [
        {
          "id": "power",
          "case": "baseline",
          "target": "grid.P",
          "measured": {"value": 1.0, "unit": "MW"},
          "sigma": {"value": 0.1, "unit": "MW"}
        }
      ]
    }
  ]
})json");
        },
        "calibration target must use");
}

void test_generic_model_document_rejects_invalid_topology() {
    const std::string path = write_temp_model(
        "generic_bad_topology",
        R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "bad_topology",
    "media": [
      {
        "id": "air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      },
      {
        "id": "water",
        "backend": "coolprop_if97",
        "substance": "Water"
      }
    ],
    "components": [
      {
        "id": "gas",
        "kind": "source.fluid.boundary",
        "media": {
          "outlet": "air"
        }
      },
      {
        "id": "steam",
        "kind": "sink.fluid.boundary",
        "media": {
          "inlet": "water"
        }
      }
    ],
    "connections": [
      {
        "id": "bad_link",
        "from": "gas.outlet",
        "to": "steam.inlet",
        "kind": "fluid_link"
      }
    ]
  },
  "cases": []
})json");
    const auto document =
        thermox::platform::load_model_document(path);
    const auto registry =
        thermox::platform::make_default_component_registry();
    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                document, registry);
        },
        "incompatible fluid media");
}

void test_generic_model_document_rejects_unsupported_units() {
    const std::string path = write_temp_model(
        "generic_bad_unit",
        R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "bad_unit",
    "media": [
      {
        "id": "air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      }
    ],
    "components": [
      {
        "id": "source",
        "kind": "source",
        "parameters": {
          "p": {
            "value": 14.7,
            "unit": "psi"
          }
        },
        "media": {
          "outlet": "air"
        }
      }
    ],
    "connections": []
  },
  "cases": []
})json");
    require_throws([&]() { thermox::platform::load_model_document(path); }, "unsupported unit");
}

void test_compiler_enforces_connection_contracts() {
    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto wrong_kind =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "wrong_connection_kind",
    "media": [],
    "components": [
      {
        "id": "source",
        "kind": "source.heat.boundary"
      },
      {
        "id": "sink",
        "kind": "sink.heat.boundary"
      }
    ],
    "connections": [
      {
        "id": "heat",
        "from": "source.outlet",
        "to": "sink.inlet",
        "kind": "fluid_link"
      }
    ]
  },
  "cases": []
})json");
    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                wrong_kind, registry);
        },
        "incompatible with domain 'heat'");

    const auto fan_out =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "implicit_fan_out",
    "media": [],
    "components": [
      {
        "id": "source",
        "kind": "source.heat.boundary"
      },
      {
        "id": "sink_a",
        "kind": "sink.heat.boundary"
      },
      {
        "id": "sink_b",
        "kind": "sink.heat.boundary"
      }
    ],
    "connections": [
      {
        "id": "a",
        "from": "source.outlet",
        "to": "sink_a.inlet",
        "kind": "heat_link"
      },
      {
        "id": "b",
        "from": "source.outlet",
        "to": "sink_b.inlet",
        "kind": "heat_link"
      }
    ]
  },
  "cases": []
})json");
    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                fan_out, registry);
        },
        "maximum connection count of 1");
}

void test_component_registry_exposes_default_models() {
    const auto registry = thermox::platform::make_default_component_registry();
    require(registry.contains("source.fluid.boundary"), "default registry should contain fluid source");
    require(registry.contains("compressor.gas.isentropic_efficiency"),
            "default registry should contain compressor");
    require(registry.contains("compressor.fluid.isentropic_efficiency"),
            "default registry should contain generic-fluid compressor");
    require(registry.contains("compressor.fluid.performance_map"),
            "default registry should contain map-driven compressor");
    require(registry.contains("turbine.fluid.performance_map"),
            "default registry should contain map-driven turbine");
    require(registry.contains("storage.thermal.lumped"),
            "default registry should contain lumped thermal storage");
    require(registry.contains("source.heat.boundary"),
            "default registry should contain transient heat boundary");
    require(registry.contains("sink.heat.boundary"),
            "default registry should contain heat sink boundary");
    require(registry.contains("pump.fluid.isentropic_efficiency"),
            "default registry should contain property-aware pump");
    require(registry.contains("junction.fluid.mixer.two_inlet"),
            "default registry should contain two-inlet mixer");
    require(registry.contains("junction.fluid.splitter.two_outlet"),
            "default registry should contain two-outlet splitter");
    require(registry.contains("junction.material.mixer.two_inlet"),
            "default registry should contain material mixer");
    require(registry.contains(
                "junction.material.splitter.fixed_fraction"),
            "default registry should contain material splitter");
    require(registry.contains("valve.fluid.isenthalpic_pressure_ratio"),
            "default registry should contain fluid valve");
    require(registry.contains("heat_exchanger.fluid.fixed_duty"),
            "default registry should contain fixed-duty heat exchanger");
    require(registry.contains("heat_exchanger.fluid.counterflow_ua"),
            "default registry should contain counterflow UA heat exchanger");
    require(registry.contains(
                "evaporator.fluid.fixed_outlet_quality"),
            "default registry should contain quality-target evaporator");
    require(registry.contains(
                "condenser.fluid.fixed_outlet_quality"),
            "default registry should contain quality-target condenser");
    require(registry.contains("volume.fluid.rigid_adiabatic"),
            "default registry should contain rigid fluid volume");
    require(registry.kinds().size() >= 4, "default registry should contain multiple component kinds");
}

void test_component_registry_rejects_unknown_kind() {
    const auto registry = thermox::platform::make_default_component_registry();
    require_throws([&]() { (void)registry.require_model("not.registered"); },
                   "no component model registered");
}

void test_component_catalog_exposes_parameter_contracts() {
    const auto registry =
        thermox::platform::make_default_component_registry();
    std::vector<std::string> expected_kinds = {
        "source.fluid.boundary",
        "sink.fluid.boundary",
        "source.material.boundary",
        "sink.material.boundary",
        "source.heat.boundary",
        "sink.heat.boundary",
        "source.electrical.boundary",
        "sink.electrical.boundary",
        "compressor.gas.isentropic_efficiency",
        "compressor.fluid.isentropic_efficiency",
        "compressor.fluid.performance_map",
        "compressor.material.isentropic_efficiency",
        "compressor.material.performance_map",
        "pump.fluid.isentropic_efficiency",
        "turbine.gas.isentropic_efficiency",
        "turbine.fluid.isentropic_efficiency",
        "turbine.fluid.performance_map",
        "turbine.material.isentropic_efficiency",
        "turbine.material.performance_map",
        "junction.fluid.mixer.two_inlet",
        "junction.fluid.splitter.two_outlet",
        "junction.material.mixer.two_inlet",
        "junction.material.splitter.fixed_fraction",
        "valve.fluid.isenthalpic_pressure_ratio",
        "transport.material.frozen_pressure_ratio",
        "combustor.material.adiabatic_equilibrium",
        "heat_exchanger.fluid.fixed_duty",
        "heat_exchanger.fluid.counterflow_ua",
        "evaporator.fluid.fixed_outlet_quality",
        "condenser.fluid.fixed_outlet_quality",
        "volume.fluid.rigid_adiabatic",
        "storage.thermal.lumped",
        "shaft.train.two_load",
        "generator.electrical.efficiency"};
    std::sort(expected_kinds.begin(), expected_kinds.end());
    require(registry.kinds() == expected_kinds,
            "default component modules should preserve the complete catalog");
    require(registry.descriptors().size() == registry.kinds().size(),
            "component catalog should expose every registered descriptor");
    const auto& descriptor = registry.require_model(
        "heat_exchanger.fluid.counterflow_ua").descriptor();
    const auto find_parameter = [&](const std::string& name)
        -> const thermox::platform::ParameterModelDescriptor& {
        const auto it = std::find_if(
            descriptor.parameters.begin(),
            descriptor.parameters.end(),
            [&](const auto& parameter) {
                return parameter.name == name;
            });
        require(it != descriptor.parameters.end(),
                "component catalog parameter should exist: " + name);
        return *it;
    };
    const auto& conductance = find_parameter("UA");
    require(conductance.required,
            "UA should be a required component parameter");
    require(conductance.dimension == "thermal_conductance",
            "UA should declare thermal-conductance dimension");
    require(!conductance.lower_inclusive &&
                conductance.lower_bound == 0.0,
            "UA should declare a strictly positive bound");
    const auto& pressure_loss =
        find_parameter("hot_pressure_loss_fraction");
    require(!pressure_loss.required &&
                pressure_loss.default_value.has_value(),
            "pressure loss should expose its optional default");
    require_near(*pressure_loss.default_value, 0.0, 0.0,
                 "pressure-loss default");
    require(!pressure_loss.upper_inclusive &&
                pressure_loss.upper_bound == 1.0,
            "pressure loss should exclude unity");
}

void test_component_parameter_contracts_are_enforced() {
    const auto unknown_parameter =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "unknown_component_parameter",
    "media": [
      {
        "id": "air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      }
    ],
    "components": [
      {
        "id": "compressor",
        "kind": "compressor.fluid.isentropic_efficiency",
        "parameters": {
          "pressure_ratio": 2.0,
          "eta_is": 0.8,
          "cp": {
            "value": 1.0,
            "unit": "kJ/kg/K"
          }
        },
        "media": {
          "inlet": "air",
          "outlet": "air"
        }
      }
    ],
    "connections": []
  },
  "cases": []
})json");
    const auto wrong_dimension =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "wrong_parameter_dimension",
    "media": [
      {
        "id": "hot",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      },
      {
        "id": "cold",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      }
    ],
    "components": [
      {
        "id": "hx",
        "kind": "heat_exchanger.fluid.counterflow_ua",
        "parameters": {
          "UA": {
            "value": 1.0,
            "unit": "MW"
          }
        },
        "media": {
          "hot_in": "hot",
          "hot_out": "hot",
          "cold_in": "cold",
          "cold_out": "cold"
        }
      }
    ],
    "connections": []
  },
  "cases": []
})json");
    const auto invalid_bound =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "invalid_parameter_bound",
    "media": [
      {
        "id": "air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      }
    ],
    "components": [
      {
        "id": "compressor",
        "kind": "compressor.fluid.isentropic_efficiency",
        "parameters": {
          "pressure_ratio": 2.0,
          "eta_is": 1.1
        },
        "media": {
          "inlet": "air",
          "outlet": "air"
        }
      }
    ],
    "connections": []
  },
  "cases": []
})json");
    const auto registry =
        thermox::platform::make_default_component_registry();
    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                unknown_parameter, registry);
        },
        "supplies unknown parameter");
    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                wrong_dimension, registry);
        },
        "requires dimension 'thermal_conductance'");
    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                invalid_bound, registry);
        },
        "outside its declared bounds");
}

void test_generic_model_compiles_to_connection_equations() {
    const auto document = thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "compile_demo",
    "media": [
      {
        "id": "air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      }
    ],
    "components": [
      {
        "id": "ambient",
        "kind": "source.fluid.boundary",
        "media": {
          "outlet": "air"
        }
      },
      {
        "id": "compressor",
        "kind": "compressor.fluid.isentropic_efficiency",
        "parameters": {
          "pressure_ratio": 12.0,
          "eta_is": 0.86
        },
        "media": {
          "inlet": "air",
          "outlet": "air"
        }
      }
    ],
    "connections": [
      {
        "id": "air_link",
        "from": "ambient.outlet",
        "to": "compressor.inlet",
        "kind": "fluid_link"
      }
    ]
  },
  "cases": [
    {
      "id": "design",
      "mode": "steady_state_design",
      "fixed_values": {
        "ambient.outlet.m_dot": {
          "value": 100.0,
          "unit": "kg/s"
        },
        "ambient.outlet.p": {
          "value": 101.325,
          "unit": "kPa"
        },
        "ambient.outlet.T": {
          "value": 288.15,
          "unit": "K"
        },
        "compressor.shaft.omega": 314.1592653589793
      },
      "initial_guesses": {
        "compressor.inlet.p": {
          "value": 100.0,
          "unit": "kPa"
        }
      }
    }
  ]
})json");

    const auto registry = thermox::platform::make_default_component_registry();
    const auto graph = thermox::platform::compile_model_graph(document, registry, "design");

    require(graph.model_id == "compile_demo", "compiled graph carries model id");
    require(graph.case_id && *graph.case_id == "design", "compiled graph carries selected case id");
    require(graph.port_variables.size() == 11, "compiled graph should expose primary port variables");
    require(graph.connection_equations.size() == 3, "fluid connection lowers conserved variables");
    require(graph.reduced_connection_equations.empty(),
            "open graph should not reduce connection equations");
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
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "compressor_physics",
    "media": [
      {
        "id": "air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      }
    ],
    "components": [
      {
        "id": "compressor",
        "kind": "compressor.fluid.isentropic_efficiency",
        "parameters": {
          "pressure_ratio": 12.0,
          "eta_is": 0.86
        },
        "media": {
          "inlet": "air",
          "outlet": "air"
        }
      }
    ],
    "connections": []
  },
  "cases": [
    {
      "id": "design",
      "mode": "steady_state_design",
      "fixed_values": {
        "compressor.inlet.m_dot": {
          "value": 100.0,
          "unit": "kg/s"
        },
        "compressor.inlet.p": {
          "value": 101.325,
          "unit": "kPa"
        },
        "compressor.inlet.T": {
          "value": 300.0,
          "unit": "K"
        },
        "compressor.shaft.omega": 314.1592653589793
      },
      "initial_guesses": {
        "compressor.outlet.p": {
          "value": 1200.0,
          "unit": "kPa"
        },
        "compressor.outlet.h": {
          "value": 650.0,
          "unit": "kJ/kg"
        },
        "compressor.shaft.W_dot": {
          "value": 35.0,
          "unit": "MW"
        }
      }
    }
  ]
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

    const thermox::platform::GraphResultEvaluator evaluator(
        document, graph,
        thermox::physics::
            make_default_property_package_registry());
    const auto graph_result = evaluator.evaluate(result.x);
    const auto& compressor =
        require_component_result(graph_result, "compressor");
    require(compressor.ports.size() == 3,
            "compressor exposes fluid and shaft graph ports");
    const auto& outlet =
        require_port_result(
            graph_result, "compressor", "outlet");
    require(outlet.domain == "fluid" &&
                outlet.primary_values.size() == 3,
            "compressor exposes two derived fluid-port results");
    require_near(
        require_result_value(outlet.derived_values, "T"),
        expected_temperature, 1.0e-8,
        "result layer derives compressor outlet temperature");
}

void test_map_driven_compressor_solves_bound_operating_point() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "mapped_compressor",
    "media": [{
      "id": "air",
      "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "components": [{
      "id": "compressor",
      "kind": "compressor.fluid.performance_map",
      "artifacts": {"performance_map": "compressor-map"},
      "parameters": {
        "reference_pressure": {"value": 101.325, "unit": "kPa"},
        "reference_temperature": {"value": 300.0, "unit": "K"}
      },
      "media": {"inlet": "air", "outlet": "air"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "operating_point",
    "mode": "steady_state_off_design",
    "fixed_values": {
      "compressor.inlet.m_dot": {"value": 100.0, "unit": "kg/s"},
      "compressor.inlet.p": {"value": 101.325, "unit": "kPa"},
      "compressor.inlet.T": {"value": 300.0, "unit": "K"},
      "compressor.shaft.omega": 314.1592653589793
    },
    "initial_guesses": {
      "compressor.outlet.p": {"value": 1.0, "unit": "MPa"},
      "compressor.outlet.h": {"value": 600.0, "unit": "kJ/kg"},
      "compressor.shaft.W_dot": {"value": 30.0, "unit": "MW"}
    }
  }]
})json");

    thermox::platform::PerformanceMapRegistry maps;
    maps.register_artifact({
        "compressor-map",
        thermox::platform::performance_map_artifact_schema_v1,
        "test-operating-map",
        std::string(64, 'c'),
        std::make_shared<
            const thermox::platform::PerformanceMap>(
            thermox::platform::MapVariable{
                "corrected_mass_flow", "mass_flow"},
            thermox::platform::MapVariable{
                "corrected_speed", "angular_speed"},
            std::vector<thermox::platform::MapVariable>{
                {"pressure_ratio", "dimensionless"},
                {"isentropic_efficiency", "dimensionless"},
            },
            std::vector<thermox::platform::MapCurve>{
                {250.0,
                 {{80.0, {10.0, 0.85}},
                  {120.0, {10.0, 0.85}}}},
                {400.0,
                 {{80.0, {10.0, 0.85}},
                  {120.0, {10.0, 0.85}}}},
            }),
    });
    const auto graph = thermox::platform::compile_model_graph(
        document,
        thermox::platform::make_default_component_registry(),
        thermox::physics::
            make_default_property_package_registry(),
        maps,
        "operating_point");
    require(
        static_cast<bool>(
            graph.problem.partial_sparse_jacobian),
        "mapped compressor must retain partial analytic "
        "Jacobian assembly");
    const auto result = thermox::solve_newton(graph.problem);
    require(
        result.diagnostics.converged,
        result.diagnostics.message);

    const auto value = [&](const std::string& name) {
        const auto found = std::find(
            graph.problem.variable_names.begin(),
            graph.problem.variable_names.end(), name);
        require(
            found != graph.problem.variable_names.end(),
            "mapped compressor result variable missing: " + name);
        return result.x.at(static_cast<std::size_t>(
            std::distance(
                graph.problem.variable_names.begin(), found)));
    };
    const double gamma = 1004.5 / (1004.5 - 287.0);
    const double expected_outlet_temperature =
        300.0 *
        (1.0 +
         (std::pow(10.0, (gamma - 1.0) / gamma) - 1.0) /
             0.85);
    const double expected_outlet_enthalpy =
        1004.5 * expected_outlet_temperature;
    require_near(
        value("compressor.outlet.p"), 10.0 * 101325.0,
        1.0e-4, "mapped compressor pressure ratio");
    require_near(
        value("compressor.outlet.h"),
        expected_outlet_enthalpy, 1.0e-4,
        "mapped compressor efficiency");
    require_near(
        value("compressor.shaft.W_dot"),
        100.0 *
            (expected_outlet_enthalpy - 1004.5 * 300.0),
        1.0e-2, "mapped compressor shaft power");

    thermox::platform::PerformanceMapRegistry invalid_maps;
    invalid_maps.register_artifact({
        "compressor-map",
        thermox::platform::performance_map_artifact_schema_v1,
        "invalid-axis-map",
        std::string(64, 'd'),
        std::make_shared<
            const thermox::platform::PerformanceMap>(
            thermox::platform::MapVariable{
                "uncorrected_flow", "mass_flow"},
            thermox::platform::MapVariable{
                "corrected_speed", "angular_speed"},
            std::vector<thermox::platform::MapVariable>{
                {"pressure_ratio", "dimensionless"},
                {"isentropic_efficiency", "dimensionless"},
            },
            std::vector<thermox::platform::MapCurve>{
                {250.0,
                 {{80.0, {10.0, 0.85}},
                  {120.0, {10.0, 0.85}}}},
                {400.0,
                 {{80.0, {10.0, 0.85}},
                  {120.0, {10.0, 0.85}}}},
            }),
    });
    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                document,
                thermox::platform::
                    make_default_component_registry(),
                thermox::physics::
                    make_default_property_package_registry(),
                invalid_maps,
                "operating_point");
        },
        "primary axis must be 'corrected_mass_flow'");
}

void test_map_driven_turbine_solves_bound_operating_point() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "mapped_turbine",
    "media": [{
      "id": "gas",
      "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "components": [{
      "id": "turbine",
      "kind": "turbine.fluid.performance_map",
      "artifacts": {"performance_map": "turbine-map"},
      "parameters": {
        "reference_pressure": {"value": 1.0, "unit": "MPa"},
        "reference_temperature": {"value": 1000.0, "unit": "K"}
      },
      "media": {"inlet": "gas", "outlet": "gas"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "operating_point",
    "mode": "steady_state_off_design",
    "fixed_values": {
      "turbine.inlet.m_dot": {"value": 50.0, "unit": "kg/s"},
      "turbine.inlet.p": {"value": 1.0, "unit": "MPa"},
      "turbine.inlet.T": {"value": 1000.0, "unit": "K"},
      "turbine.shaft.omega": 314.1592653589793
    },
    "initial_guesses": {
      "turbine.inlet.h": {"value": 1004.5, "unit": "kJ/kg"},
      "turbine.outlet.p": {"value": 0.2, "unit": "MPa"},
      "turbine.outlet.h": {"value": 700.0, "unit": "kJ/kg"},
      "turbine.shaft.W_dot": {"value": 15.0, "unit": "MW"}
    }
  }]
})json");

    thermox::platform::PerformanceMapRegistry maps;
    maps.register_artifact({
        "turbine-map",
        thermox::platform::performance_map_artifact_schema_v1,
        "test-turbine-map",
        std::string(64, 'e'),
        std::make_shared<
            const thermox::platform::PerformanceMap>(
            thermox::platform::MapVariable{
                "corrected_mass_flow", "mass_flow"},
            thermox::platform::MapVariable{
                "corrected_speed", "angular_speed"},
            std::vector<thermox::platform::MapVariable>{
                {"pressure_ratio", "dimensionless"},
                {"isentropic_efficiency", "dimensionless"},
            },
            std::vector<thermox::platform::MapCurve>{
                {250.0,
                 {{40.0, {5.0, 0.9}},
                  {60.0, {5.0, 0.9}}}},
                {400.0,
                 {{40.0, {5.0, 0.9}},
                  {60.0, {5.0, 0.9}}}},
            }),
    });
    const auto graph = thermox::platform::compile_model_graph(
        document,
        thermox::platform::make_default_component_registry(),
        thermox::physics::
            make_default_property_package_registry(),
        maps,
        "operating_point");
    const auto result = thermox::solve_newton(graph.problem);
    require(
        result.diagnostics.converged,
        result.diagnostics.message);
    const auto value = [&](const std::string& name) {
        const auto found = std::find(
            graph.problem.variable_names.begin(),
            graph.problem.variable_names.end(), name);
        require(
            found != graph.problem.variable_names.end(),
            "mapped turbine result variable missing: " + name);
        return result.x.at(static_cast<std::size_t>(
            std::distance(
                graph.problem.variable_names.begin(), found)));
    };
    const double gamma = 1004.5 / (1004.5 - 287.0);
    const double expected_outlet_temperature =
        1000.0 *
        (1.0 +
         0.9 *
             (std::pow(
                  1.0 / 5.0,
                  (gamma - 1.0) / gamma) -
              1.0));
    const double expected_outlet_enthalpy =
        1004.5 * expected_outlet_temperature;
    require_near(
        value("turbine.outlet.p"), 1.0e6 / 5.0,
        1.0e-5, "mapped turbine pressure ratio");
    require_near(
        value("turbine.outlet.h"),
        expected_outlet_enthalpy, 1.0e-4,
        "mapped turbine efficiency");
    require_near(
        value("turbine.shaft.W_dot"),
        50.0 *
            (1004.5 * 1000.0 -
             expected_outlet_enthalpy),
        1.0e-2, "mapped turbine shaft power");
}

void test_generic_model_solves_ideal_gas_turbine_residuals() {
    const auto document = thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "turbine_physics",
    "media": [
      {
        "id": "air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      }
    ],
    "components": [
      {
        "id": "turbine",
        "kind": "turbine.gas.isentropic_efficiency",
        "parameters": {
          "pressure_ratio": 12.0,
          "eta_is": 0.89
        },
        "media": {
          "inlet": "air",
          "outlet": "air"
        }
      }
    ],
    "connections": []
  },
  "cases": [
    {
      "id": "design",
      "mode": "steady_state_design",
      "fixed_values": {
        "turbine.inlet.m_dot": {
          "value": 100.0,
          "unit": "kg/s"
        },
        "turbine.inlet.p": {
          "value": 1215.9,
          "unit": "kPa"
        },
        "turbine.inlet.h": {
          "value": 1406.3,
          "unit": "kJ/kg"
        },
        "turbine.shaft.omega": 314.1592653589793
      },
      "initial_guesses": {
        "turbine.outlet.p": {
          "value": 101.325,
          "unit": "kPa"
        },
        "turbine.outlet.h": {
          "value": 803.6,
          "unit": "kJ/kg"
        },
        "turbine.shaft.W_dot": {
          "value": 60.0,
          "unit": "MW"
        }
      }
    }
  ]
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
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "ideal_gas_mixer",
    "media": [
      {
        "id": "air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      }
    ],
    "components": [
      {
        "id": "mixer",
        "kind": "junction.fluid.mixer.two_inlet",
        "media": {
          "inlet_a": "air",
          "inlet_b": "air",
          "outlet": "air"
        }
      }
    ],
    "connections": []
  },
  "cases": [
    {
      "id": "design",
      "mode": "steady_state_design",
      "fixed_values": {
        "mixer.inlet_a.m_dot": {
          "value": 2.0,
          "unit": "kg/s"
        },
        "mixer.inlet_a.p": {
          "value": 1.0,
          "unit": "bar"
        },
        "mixer.inlet_a.h": {
          "value": 300.0,
          "unit": "kJ/kg"
        },
        "mixer.inlet_b.m_dot": {
          "value": 1.0,
          "unit": "kg/s"
        },
        "mixer.inlet_b.h": {
          "value": 600.0,
          "unit": "kJ/kg"
        }
      }
    }
  ]
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
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "ideal_gas_splitter",
    "media": [
      {
        "id": "air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      }
    ],
    "components": [
      {
        "id": "splitter",
        "kind": "junction.fluid.splitter.two_outlet",
        "media": {
          "inlet": "air",
          "outlet_a": "air",
          "outlet_b": "air"
        }
      }
    ],
    "connections": []
  },
  "cases": [
    {
      "id": "design",
      "mode": "steady_state_design",
      "fixed_values": {
        "splitter.inlet.m_dot": {
          "value": 10.0,
          "unit": "kg/s"
        },
        "splitter.inlet.p": {
          "value": 1.0,
          "unit": "bar"
        },
        "splitter.inlet.h": {
          "value": 400.0,
          "unit": "kJ/kg"
        },
        "splitter.outlet_a.m_dot": {
          "value": 4.0,
          "unit": "kg/s"
        }
      }
    }
  ]
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
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "isenthalpic_valve",
    "media": [
      {
        "id": "water",
        "backend": "water_steam_if97",
        "substance": "Water"
      }
    ],
    "components": [
      {
        "id": "valve",
        "kind": "valve.fluid.isenthalpic_pressure_ratio",
        "parameters": {
          "pressure_ratio": 10.0
        },
        "media": {
          "inlet": "water",
          "outlet": "water"
        }
      }
    ],
    "connections": []
  },
  "cases": [
    {
      "id": "design",
      "mode": "steady_state_design",
      "fixed_values": {
        "valve.inlet.m_dot": {
          "value": 5.0,
          "unit": "kg/s"
        },
        "valve.inlet.p": {
          "value": 10.0,
          "unit": "MPa"
        },
        "valve.inlet.h": {
          "value": 1200.0,
          "unit": "kJ/kg"
        }
      }
    }
  ]
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

void test_material_connector_and_frozen_transport() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "frozen_material_transport",
    "media": [],
    "materials": [{
      "id": "wet_air",
      "backend": "test_thermochemistry",
      "mechanism": "air.yaml",
      "phase": "gas",
      "species": ["N2", "O2", "H2O"]
    }],
    "components": [{
      "id": "source",
      "kind": "source.material.boundary",
      "materials": {"outlet": "wet_air"}
    }, {
      "id": "duct",
      "kind": "transport.material.frozen_pressure_ratio",
      "parameters": {"pressure_ratio": 0.9},
      "materials": {"inlet": "wet_air", "outlet": "wet_air"}
    }, {
      "id": "sink",
      "kind": "sink.material.boundary",
      "materials": {"inlet": "wet_air"}
    }],
    "connections": [{
      "id": "source_to_duct",
      "kind": "material_link",
      "contract_version": "thermox.connector.material/v1",
      "from": "source.outlet",
      "to": "duct.inlet"
    }, {
      "id": "duct_to_sink",
      "kind": "material_link",
      "from": "duct.outlet",
      "to": "sink.inlet"
    }]
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "source.outlet.p": {"value": 200.0, "unit": "kPa"},
      "source.outlet.h": {"value": 500.0, "unit": "kJ/kg"},
      "source.outlet.m_dot[N2]": {"value": 7.5, "unit": "kg/s"},
      "source.outlet.m_dot[O2]": {"value": 2.0, "unit": "kg/s"},
      "source.outlet.m_dot[H2O]": {"value": 0.5, "unit": "kg/s"}
    }
  }]
})json");

    require(document.materials.size() == 1 &&
                document.materials.front().species.size() == 3,
            "material species basis must survive model parsing");
    const auto graph = thermox::platform::compile_model_graph(
        document,
        thermox::platform::make_default_component_registry(),
        "design");
    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged,
            result.diagnostics.message);
    const auto value = [&](const std::string& name) {
        return result.x.at(require_variable_index(
            graph.problem.variable_names, name));
    };
    require_near(value("sink.inlet.p"), 180000.0, 1.0e-7,
                 "frozen material transport applies pressure ratio");
    require_near(value("sink.inlet.h"), 500000.0, 1.0e-7,
                 "frozen material transport preserves enthalpy");
    require_near(value("sink.inlet.m_dot[N2]"), 7.5, 1.0e-10,
                 "material connector conserves nitrogen mass");
    require_near(value("sink.inlet.m_dot[O2]"), 2.0, 1.0e-10,
                 "material connector conserves oxygen mass");
    require_near(value("sink.inlet.m_dot[H2O]"), 0.5, 1.0e-10,
                 "material connector conserves water mass");
}

void test_material_mixer_and_fixed_fraction_splitter() {
    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto splitter_document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "material_splitter",
    "media": [],
    "materials": [{
      "id": "gas", "backend": "unresolved_test_backend",
      "mechanism": "test.yaml", "phase": "gas",
      "species": ["N2", "O2"]
    }],
    "components": [{
      "id": "splitter",
      "kind": "junction.material.splitter.fixed_fraction",
      "parameters": {"outlet_a_fraction": 0.15},
      "materials": {
        "inlet": "gas", "outlet_a": "gas", "outlet_b": "gas"
      }
    }],
    "connections": []
  },
  "cases": [{
    "id": "design", "mode": "steady_state_design",
    "fixed_values": {
      "splitter.inlet.p": {"value": 1.5, "unit": "MPa"},
      "splitter.inlet.h": {"value": 600.0, "unit": "kJ/kg"},
      "splitter.inlet.m_dot[N2]": {"value": 8.0, "unit": "kg/s"},
      "splitter.inlet.m_dot[O2]": {"value": 2.0, "unit": "kg/s"}
    }
  }]
})json");
    const auto splitter_graph =
        thermox::platform::compile_model_graph(
            splitter_document, registry, "design");
    const auto splitter =
        thermox::solve_newton(splitter_graph.problem);
    require(
        splitter.diagnostics.converged,
        splitter.diagnostics.message);
    const auto split_value = [&](const std::string& name) {
        return splitter.x.at(require_variable_index(
            splitter_graph.problem.variable_names, name));
    };
    require_near(
        split_value("splitter.outlet_a.m_dot[N2]"),
        1.2, 1.0e-10,
        "material splitter preserves nitrogen composition");
    require_near(
        split_value("splitter.outlet_a.m_dot[O2]"),
        0.3, 1.0e-10,
        "material splitter preserves oxygen composition");
    require_near(
        split_value("splitter.outlet_b.m_dot[N2]"),
        6.8, 1.0e-10,
        "material splitter closes remaining nitrogen flow");
    require_near(
        split_value("splitter.outlet_b.h"),
        600000.0, 1.0e-8,
        "material splitter preserves specific enthalpy");

    const auto mixer_document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "material_mixer",
    "media": [],
    "materials": [{
      "id": "gas", "backend": "unresolved_test_backend",
      "mechanism": "test.yaml", "phase": "gas",
      "species": ["N2", "O2"]
    }],
    "components": [{
      "id": "mixer",
      "kind": "junction.material.mixer.two_inlet",
      "materials": {
        "inlet_a": "gas", "inlet_b": "gas", "outlet": "gas"
      }
    }],
    "connections": []
  },
  "cases": [{
    "id": "design", "mode": "steady_state_design",
    "fixed_values": {
      "mixer.inlet_a.p": {"value": 200.0, "unit": "kPa"},
      "mixer.inlet_a.h": {"value": 300.0, "unit": "kJ/kg"},
      "mixer.inlet_a.m_dot[N2]": {"value": 6.0, "unit": "kg/s"},
      "mixer.inlet_a.m_dot[O2]": {"value": 2.0, "unit": "kg/s"},
      "mixer.inlet_b.h": {"value": 500.0, "unit": "kJ/kg"},
      "mixer.inlet_b.m_dot[N2]": {"value": 1.0, "unit": "kg/s"},
      "mixer.inlet_b.m_dot[O2]": {"value": 1.0, "unit": "kg/s"}
    }
  }]
})json");
    const auto mixer_graph =
        thermox::platform::compile_model_graph(
            mixer_document, registry, "design");
    const auto mixer =
        thermox::solve_newton(mixer_graph.problem);
    require(mixer.diagnostics.converged,
            mixer.diagnostics.message);
    const auto mix_value = [&](const std::string& name) {
        return mixer.x.at(require_variable_index(
            mixer_graph.problem.variable_names, name));
    };
    require_near(
        mix_value("mixer.outlet.m_dot[N2]"),
        7.0, 1.0e-10,
        "material mixer conserves nitrogen");
    require_near(
        mix_value("mixer.outlet.m_dot[O2]"),
        3.0, 1.0e-10,
        "material mixer conserves oxygen");
    require_near(
        mix_value("mixer.outlet.h"),
        340000.0, 1.0e-7,
        "material mixer conserves enthalpy flow");
    require_near(
        mix_value("mixer.outlet.p"),
        200000.0, 1.0e-8,
        "material mixer equalizes pressure");
}

void test_material_thermochemistry_resolves_on_demand() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "thermochemistry_resolution",
    "media": [],
    "materials": [{
      "id": "air",
      "backend": "test_backend",
      "mechanism": "test.yaml",
      "phase": "gas",
      "package_version": "1.0.0",
      "species": ["N2", "O2"]
    }],
    "components": [{
      "id": "consumer",
      "kind": "test.material.consumer",
      "materials": {"inlet": "air"}
    }],
    "connections": []
  },
  "cases": []
})json");
    thermox::platform::ComponentModelDescriptor descriptor;
    descriptor.kind = "test.material.consumer";
    descriptor.version = "1.0.0";
    descriptor.ports = {{"inlet", "material", "in"}};
    descriptor.required_thermochemistry_capabilities = {
        thermox::physics::ThermochemistryCapability::state_ph};
    thermox::platform::ComponentRegistry components;
    components.register_model(std::make_shared<
        const thermox::platform::MetadataComponentModel>(
        descriptor));

    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                document, components,
                thermox::physics::
                    make_default_property_package_registry(),
                thermox::platform::PerformanceMapRegistry{},
                thermox::physics::
                    ThermochemistryPackageRegistry{});
        },
        "no thermochemistry package registered");

    thermox::physics::ThermochemistryPackageRegistry chemistry;
    chemistry.register_backend(
        {"test_backend", "test-thermochemistry", "1.0.0",
         {thermox::physics::ThermochemistryCapability::state_ph}},
        [](std::string_view, std::string_view) {
            return std::make_shared<
                const TestThermochemistryPackage>();
        });
    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                document, components,
                thermox::physics::
                    make_default_property_package_registry(),
                thermox::platform::PerformanceMapRegistry{},
                chemistry);
        },
        "under-specified");
}

void test_adiabatic_equilibrium_combustor() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "equilibrium_combustor",
    "media": [],
    "materials": [{
      "id": "reacting_gas",
      "backend": "test_backend",
      "mechanism": "test.yaml",
      "phase": "gas",
      "species": ["N2", "O2"]
    }],
    "components": [{
      "id": "combustor",
      "kind": "combustor.material.adiabatic_equilibrium",
      "parameters": {"pressure_ratio": 0.9},
      "materials": {
        "air_inlet": "reacting_gas",
        "fuel_inlet": "reacting_gas",
        "outlet": "reacting_gas"
      }
    }],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "combustor.air_inlet.p": {"value": 100.0, "unit": "kPa"},
      "combustor.air_inlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "combustor.air_inlet.m_dot[N2]": {"value": 8.0, "unit": "kg/s"},
      "combustor.air_inlet.m_dot[O2]": {"value": 2.0, "unit": "kg/s"},
      "combustor.fuel_inlet.h": {"value": 500.0, "unit": "kJ/kg"},
      "combustor.fuel_inlet.m_dot[N2]": {"value": 1.0, "unit": "kg/s"},
      "combustor.fuel_inlet.m_dot[O2]": {"value": 1.0, "unit": "kg/s"}
    }
  }]
})json");
    thermox::physics::ThermochemistryPackageRegistry chemistry;
    chemistry.register_backend(
        {"test_backend", "test-thermochemistry", "1.0.0",
         {thermox::physics::ThermochemistryCapability::
              equilibrium_hp}},
        [](std::string_view, std::string_view) {
            return std::make_shared<
                const TestThermochemistryPackage>();
        });
    const auto graph = thermox::platform::compile_model_graph(
        document,
        thermox::platform::make_default_component_registry(),
        thermox::physics::
            make_default_property_package_registry(),
        thermox::platform::PerformanceMapRegistry{},
        chemistry, "design");
    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged,
            result.diagnostics.message);
    const auto value = [&](const std::string& name) {
        return result.x.at(require_variable_index(
            graph.problem.variable_names, name));
    };
    require_near(value("combustor.outlet.p"), 90000.0, 1.0e-6,
                 "combustor applies pressure loss");
    require_near(
        value("combustor.outlet.h"), 1000000.0 / 3.0,
        1.0e-5, "combustor preserves adiabatic mixture enthalpy");
    require_near(
        value("combustor.outlet.m_dot[N2]"), 9.0, 1.0e-9,
        "combustor returns backend equilibrium nitrogen flow");
    require_near(
        value("combustor.outlet.m_dot[O2]"), 3.0, 1.0e-9,
        "combustor returns backend equilibrium oxygen flow");
    const thermox::platform::GraphResultEvaluator evaluator(
        document, graph,
        thermox::physics::
            make_default_property_package_registry(),
        chemistry);
    const auto graph_result = evaluator.evaluate(result.x);
    const auto& outlet = require_port_result(
        graph_result, "combustor", "outlet");
    require_near(
        require_result_value(outlet.derived_values, "T"),
        1000.0 / 3.0, 1.0e-6,
        "material result layer derives combustor outlet temperature");
}

void test_material_compressor_and_turbine() {
    const auto chemistry_registry = [] {
        thermox::physics::ThermochemistryPackageRegistry registry;
        registry.register_backend(
            {"test_backend", "test-thermochemistry", "1.0.0",
             {thermox::physics::ThermochemistryCapability::state_ph,
              thermox::physics::ThermochemistryCapability::state_ps}},
            [](std::string_view, std::string_view) {
                return std::make_shared<
                    const TestThermochemistryPackage>();
            });
        return registry;
    };
    const auto compile_and_solve = [&](
        const std::string& text) {
        return thermox::platform::compile_model_graph(
            thermox::platform::parse_model_document_text(text),
            thermox::platform::make_default_component_registry(),
            thermox::physics::
                make_default_property_package_registry(),
            thermox::platform::PerformanceMapRegistry{},
            chemistry_registry(), "design");
    };

    const auto compressor_graph = compile_and_solve(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "material_compressor",
    "media": [],
    "materials": [{
      "id": "gas", "backend": "test_backend",
      "mechanism": "test.yaml", "phase": "gas",
      "species": ["N2", "O2"]
    }],
    "components": [{
      "id": "machine",
      "kind": "compressor.material.isentropic_efficiency",
      "parameters": {"pressure_ratio": 10.0, "eta_is": 0.8},
      "materials": {"inlet": "gas", "outlet": "gas"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "design", "mode": "steady_state_design",
    "fixed_values": {
      "machine.inlet.p": {"value": 100.0, "unit": "kPa"},
      "machine.inlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "machine.inlet.m_dot[N2]": {"value": 8.0, "unit": "kg/s"},
      "machine.inlet.m_dot[O2]": {"value": 2.0, "unit": "kg/s"},
      "machine.shaft.omega": 314.1592653589793
    }
  }]
})json");
    const auto compressor =
        thermox::solve_newton(compressor_graph.problem);
    require(compressor.diagnostics.converged,
            compressor.diagnostics.message);
    const auto compressor_value = [&](const std::string& name) {
        return compressor.x.at(require_variable_index(
            compressor_graph.problem.variable_names, name));
    };
    const double compressor_isentropic_h =
        300000.0 * std::pow(10.0, 0.287);
    const double compressor_outlet_h =
        300000.0 +
        (compressor_isentropic_h - 300000.0) / 0.8;
    require_near(
        compressor_value("machine.outlet.h"),
        compressor_outlet_h, 1.0e-5,
        "material compressor applies isentropic efficiency");
    require_near(
        compressor_value("machine.shaft.W_dot"),
        10.0 * (compressor_outlet_h - 300000.0), 1.0e-4,
        "material compressor closes shaft input power");

    const auto turbine_graph = compile_and_solve(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "material_turbine",
    "media": [],
    "materials": [{
      "id": "gas", "backend": "test_backend",
      "mechanism": "test.yaml", "phase": "gas",
      "species": ["N2", "O2"]
    }],
    "components": [{
      "id": "machine",
      "kind": "turbine.material.isentropic_efficiency",
      "parameters": {"pressure_ratio": 10.0, "eta_is": 0.9},
      "materials": {"inlet": "gas", "outlet": "gas"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "design", "mode": "steady_state_design",
    "fixed_values": {
      "machine.inlet.p": {"value": 1.0, "unit": "MPa"},
      "machine.inlet.h": {"value": 1000.0, "unit": "kJ/kg"},
      "machine.inlet.m_dot[N2]": {"value": 8.0, "unit": "kg/s"},
      "machine.inlet.m_dot[O2]": {"value": 2.0, "unit": "kg/s"},
      "machine.shaft.omega": 314.1592653589793
    }
  }]
})json");
    const auto turbine = thermox::solve_newton(
        turbine_graph.problem);
    require(turbine.diagnostics.converged,
            turbine.diagnostics.message);
    const auto turbine_value = [&](const std::string& name) {
        return turbine.x.at(require_variable_index(
            turbine_graph.problem.variable_names, name));
    };
    const double turbine_isentropic_h =
        1000000.0 * std::pow(0.1, 0.287);
    const double turbine_outlet_h =
        1000000.0 +
        0.9 * (turbine_isentropic_h - 1000000.0);
    require_near(
        turbine_value("machine.outlet.h"),
        turbine_outlet_h, 1.0e-5,
        "material turbine applies isentropic efficiency");
    require_near(
        turbine_value("machine.shaft.W_dot"),
        10.0 * (1000000.0 - turbine_outlet_h), 1.0e-4,
        "material turbine closes shaft output power");
}

void test_map_driven_material_turbomachinery() {
    auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "mapped_material_compressor",
    "media": [],
    "materials": [{
      "id": "gas", "backend": "test_backend",
      "mechanism": "test.yaml", "phase": "gas",
      "species": ["N2", "O2"]
    }],
    "components": [{
      "id": "compressor",
      "kind": "compressor.material.performance_map",
      "artifacts": {"performance_map": "material-compressor-map"},
      "parameters": {
        "reference_pressure": {"value": 101.325, "unit": "kPa"},
        "reference_temperature": {"value": 300.0, "unit": "K"}
      },
      "materials": {"inlet": "gas", "outlet": "gas"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "off_design", "mode": "steady_state_off_design",
    "fixed_values": {
      "compressor.inlet.p": {"value": 101.325, "unit": "kPa"},
      "compressor.inlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "compressor.inlet.m_dot[N2]": {"value": 8.0, "unit": "kg/s"},
      "compressor.inlet.m_dot[O2]": {"value": 2.0, "unit": "kg/s"},
      "compressor.shaft.omega": 300.0
    },
    "initial_guesses": {
      "compressor.outlet.p": {"value": 200.0, "unit": "kPa"},
      "compressor.outlet.h": {"value": 400.0, "unit": "kJ/kg"},
      "compressor.shaft.W_dot": {"value": 1.0, "unit": "MW"}
    }
  }]
})json");
    thermox::platform::PerformanceMapRegistry maps;
    maps.register_artifact({
        "material-compressor-map",
        thermox::platform::performance_map_artifact_schema_v1,
        "test-material-map",
        std::string(64, 'f'),
        std::make_shared<
            const thermox::platform::PerformanceMap>(
            thermox::platform::MapVariable{
                "corrected_mass_flow", "mass_flow"},
            thermox::platform::MapVariable{
                "corrected_speed", "angular_speed"},
            std::vector<thermox::platform::MapVariable>{
                {"pressure_ratio", "dimensionless"},
                {"isentropic_efficiency", "dimensionless"},
            },
            std::vector<thermox::platform::MapCurve>{
                {250.0,
                 {{8.0, {2.0, 0.8}},
                  {12.0, {2.0, 0.8}}}},
                {350.0,
                 {{8.0, {2.0, 0.8}},
                  {12.0, {2.0, 0.8}}}},
            }),
    });
    maps.register_artifact({
        "material-turbine-map",
        thermox::platform::performance_map_artifact_schema_v1,
        "test-material-turbine-map",
        std::string(64, 'a'),
        std::make_shared<
            const thermox::platform::PerformanceMap>(
            thermox::platform::MapVariable{
                "corrected_mass_flow", "mass_flow"},
            thermox::platform::MapVariable{
                "corrected_speed", "angular_speed"},
            std::vector<thermox::platform::MapVariable>{
                {"pressure_ratio", "dimensionless"},
                {"isentropic_efficiency", "dimensionless"},
            },
            std::vector<thermox::platform::MapCurve>{
                {250.0,
                 {{8.0, {5.0, 0.9}},
                  {12.0, {5.0, 0.9}}}},
                {350.0,
                 {{8.0, {5.0, 0.9}},
                  {12.0, {5.0, 0.9}}}},
            }),
    });
    thermox::physics::ThermochemistryPackageRegistry chemistry;
    chemistry.register_backend(
        {"test_backend", "test-thermochemistry", "1.0.0",
         {thermox::physics::ThermochemistryCapability::state_ph,
          thermox::physics::ThermochemistryCapability::state_ps}},
        [](std::string_view, std::string_view) {
            return std::make_shared<
                const TestThermochemistryPackage>();
        });
    const auto graph = thermox::platform::compile_model_graph(
        document,
        thermox::platform::make_default_component_registry(),
        thermox::physics::
            make_default_property_package_registry(),
        maps, chemistry, "off_design");
    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged,
            result.diagnostics.message);
    const auto value = [&](const std::string& name) {
        return result.x.at(require_variable_index(
            graph.problem.variable_names, name));
    };
    const double isentropic_temperature =
        300.0 * std::exp(287.0 * std::log(2.0) / 1000.0);
    const double expected_enthalpy =
        300000.0 +
        (1000.0 * isentropic_temperature - 300000.0) / 0.8;
    require_near(
        value("compressor.outlet.p"), 202650.0, 1.0e-6,
        "material map applies pressure ratio");
    require_near(
        value("compressor.outlet.h"), expected_enthalpy,
        1.0e-5, "material map applies isentropic efficiency");
    require_near(
        value("compressor.outlet.m_dot[N2]"), 8.0,
        1.0e-10, "material map preserves composition");
    require_near(
        value("compressor.shaft.W_dot"),
        10.0 * (expected_enthalpy - 300000.0),
        1.0e-4, "material map closes shaft power");

    document.model_id = "mapped_material_turbine";
    auto& machine = document.components.front();
    machine.id = "turbine";
    machine.kind = "turbine.material.performance_map";
    machine.artifact_bindings["performance_map"] =
        "material-turbine-map";
    machine.parameters["reference_pressure"] =
        {1.0e6, "Pa", "pressure"};
    machine.parameters["reference_temperature"] =
        {1000.0, "K", "temperature"};
    auto& turbine_case = document.cases.front();
    turbine_case.fixed_values.clear();
    turbine_case.initial_guesses.clear();
    turbine_case.fixed_values = {
        {"turbine.inlet.p", {1.0e6, "Pa", "pressure"}},
        {"turbine.inlet.h",
         {1.0e6, "J/kg", "specific_enthalpy"}},
        {"turbine.inlet.m_dot[N2]",
         {8.0, "kg/s", "mass_flow"}},
        {"turbine.inlet.m_dot[O2]",
         {2.0, "kg/s", "mass_flow"}},
        {"turbine.shaft.omega",
         {300.0, "rad/s", "angular_speed"}},
    };
    turbine_case.initial_guesses = {
        {"turbine.outlet.p", {2.0e5, "Pa", "pressure"}},
        {"turbine.outlet.h",
         {700000.0, "J/kg", "specific_enthalpy"}},
        {"turbine.shaft.W_dot", {3.0e6, "W", "power"}},
    };
    const auto turbine_graph =
        thermox::platform::compile_model_graph(
            document,
            thermox::platform::make_default_component_registry(),
            thermox::physics::
                make_default_property_package_registry(),
            maps, chemistry, "off_design");
    const auto turbine =
        thermox::solve_newton(turbine_graph.problem);
    require(turbine.diagnostics.converged,
            turbine.diagnostics.message);
    const auto turbine_value = [&](const std::string& name) {
        return turbine.x.at(require_variable_index(
            turbine_graph.problem.variable_names, name));
    };
    const double turbine_isentropic_temperature =
        1000.0 *
        std::exp(287.0 * std::log(1.0 / 5.0) / 1000.0);
    const double expected_turbine_enthalpy =
        1.0e6 +
        0.9 *
            (1000.0 * turbine_isentropic_temperature - 1.0e6);
    require_near(
        turbine_value("turbine.outlet.p"), 2.0e5,
        1.0e-6, "material turbine map applies pressure ratio");
    require_near(
        turbine_value("turbine.outlet.h"),
        expected_turbine_enthalpy, 1.0e-5,
        "material turbine map applies isentropic efficiency");
    require_near(
        turbine_value("turbine.shaft.W_dot"),
        10.0 * (1.0e6 - expected_turbine_enthalpy),
        1.0e-4, "material turbine map closes shaft power");
}

void test_generic_model_solves_cross_medium_fixed_duty_heat_exchanger() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "cross_medium_heat_exchanger",
    "media": [
      {
        "id": "gas",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      },
      {
        "id": "water",
        "backend": "water_steam_if97",
        "substance": "Water"
      }
    ],
    "components": [
      {
        "id": "hx",
        "kind": "heat_exchanger.fluid.fixed_duty",
        "parameters": {
          "heat_duty": {
            "value": 1.0,
            "unit": "MW"
          },
          "hot_pressure_loss_fraction": 0.05,
          "cold_pressure_loss_fraction": 0.02
        },
        "media": {
          "hot_in": "gas",
          "hot_out": "gas",
          "cold_in": "water",
          "cold_out": "water"
        }
      }
    ],
    "connections": []
  },
  "cases": [
    {
      "id": "design",
      "mode": "steady_state_design",
      "fixed_values": {
        "hx.hot_in.m_dot": {
          "value": 10.0,
          "unit": "kg/s"
        },
        "hx.hot_in.p": {
          "value": 10.0,
          "unit": "bar"
        },
        "hx.hot_in.h": {
          "value": 600.0,
          "unit": "kJ/kg"
        },
        "hx.cold_in.m_dot": {
          "value": 20.0,
          "unit": "kg/s"
        },
        "hx.cold_in.p": {
          "value": 5.0,
          "unit": "bar"
        },
        "hx.cold_in.h": {
          "value": 200.0,
          "unit": "kJ/kg"
        }
      }
    }
  ]
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

void test_generic_model_solves_counterflow_ua_heat_exchanger() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "counterflow_ua_heat_exchanger",
    "media": [
      {
        "id": "hot_air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      },
      {
        "id": "cold_air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      }
    ],
    "components": [
      {
        "id": "hx",
        "kind": "heat_exchanger.fluid.counterflow_ua",
        "parameters": {
          "UA": {
            "value": 1.0,
            "unit": "kW/K"
          },
          "hot_pressure_loss_fraction": 0.01,
          "cold_pressure_loss_fraction": 0.02
        },
        "media": {
          "hot_in": "hot_air",
          "hot_out": "hot_air",
          "cold_in": "cold_air",
          "cold_out": "cold_air"
        }
      }
    ],
    "connections": []
  },
  "cases": [
    {
      "id": "design",
      "mode": "steady_state_design",
      "fixed_values": {
        "hx.hot_in.m_dot": {
          "value": 1.0,
          "unit": "kg/s"
        },
        "hx.hot_in.p": {
          "value": 2.0,
          "unit": "bar"
        },
        "hx.hot_in.T": {
          "value": 500.0,
          "unit": "K"
        },
        "hx.cold_in.m_dot": {
          "value": 2.0,
          "unit": "kg/s"
        },
        "hx.cold_in.p": {
          "value": 1.0,
          "unit": "bar"
        },
        "hx.cold_in.T": {
          "value": 300.0,
          "unit": "K"
        }
      },
      "initial_guesses": {
        "hx.hot_in.h": {
          "value": 502.25,
          "unit": "kJ/kg"
        },
        "hx.hot_out.h": {
          "value": 400.0,
          "unit": "kJ/kg"
        },
        "hx.cold_in.h": {
          "value": 301.35,
          "unit": "kJ/kg"
        },
        "hx.cold_out.h": {
          "value": 350.0,
          "unit": "kJ/kg"
        }
      }
    }
  ]
})json");
    require_near(
        document.components.at(0).parameters.at("UA").value_si,
        1000.0, 1.0e-12,
        "heat-transfer conductance normalizes to W/K");
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
    const auto package =
        thermox::physics::make_default_property_package_registry()
            .create("ideal_gas_mixture", "Air");
    const auto hot_in =
        package->state_ph(value("hx.hot_in.p"),
                          value("hx.hot_in.h"));
    const auto hot_out =
        package->state_ph(value("hx.hot_out.p"),
                          value("hx.hot_out.h"));
    const auto cold_in =
        package->state_ph(value("hx.cold_in.p"),
                          value("hx.cold_in.h"));
    const auto cold_out =
        package->state_ph(value("hx.cold_out.p"),
                          value("hx.cold_out.h"));
    require(hot_in.ok() && hot_out.ok() &&
                cold_in.ok() && cold_out.ok(),
            "UA exchanger solved states should be valid");
    require(hot_out.state.temperature_k <
                hot_in.state.temperature_k,
            "UA exchanger cools hot stream");
    require(cold_out.state.temperature_k >
                cold_in.state.temperature_k,
            "UA exchanger heats cold stream");
    const double hot_duty =
        value("hx.hot_in.m_dot") *
        (value("hx.hot_in.h") - value("hx.hot_out.h"));
    const double cold_duty =
        value("hx.cold_in.m_dot") *
        (value("hx.cold_out.h") - value("hx.cold_in.h"));
    require_near(hot_duty, cold_duty, 1.0e-5,
                 "UA exchanger conserves energy");
}

void test_if97_fixed_quality_evaporator_and_condenser() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "if97_phase_change_components",
    "media": [
      {
        "id": "water",
        "backend": "water_steam_if97",
        "substance": "Water"
      }
    ],
    "components": [
      {
        "id": "evaporator",
        "kind": "evaporator.fluid.fixed_outlet_quality",
        "parameters": {
          "outlet_quality": 1.0,
          "pressure_loss_fraction": 0.02
        },
        "media": {
          "inlet": "water",
          "outlet": "water"
        }
      },
      {
        "id": "condenser",
        "kind": "condenser.fluid.fixed_outlet_quality",
        "parameters": {
          "outlet_quality": 0.0
        },
        "media": {
          "inlet": "water",
          "outlet": "water"
        }
      }
    ],
    "connections": []
  },
  "cases": [
    {
      "id": "design",
      "mode": "steady_state_design",
      "fixed_values": {
        "evaporator.inlet.m_dot": {
          "value": 2.0,
          "unit": "kg/s"
        },
        "evaporator.inlet.p": {
          "value": 5.0,
          "unit": "MPa"
        },
        "evaporator.inlet.h": {
          "value": 500.0,
          "unit": "kJ/kg"
        },
        "condenser.inlet.m_dot": {
          "value": 2.0,
          "unit": "kg/s"
        },
        "condenser.inlet.p": {
          "value": 1.0,
          "unit": "bar"
        },
        "condenser.inlet.h": {
          "value": 2500.0,
          "unit": "kJ/kg"
        }
      },
      "initial_guesses": {
        "evaporator.outlet.h": {
          "value": 2500.0,
          "unit": "kJ/kg"
        },
        "evaporator.heat.Q_dot": {
          "value": 4.0,
          "unit": "MW"
        },
        "evaporator.heat.T": {
          "value": 540.0,
          "unit": "K"
        },
        "condenser.outlet.h": {
          "value": 530.0,
          "unit": "kJ/kg"
        },
        "condenser.heat.Q_dot": {
          "value": 4.0,
          "unit": "MW"
        },
        "condenser.heat.T": {
          "value": 373.0,
          "unit": "K"
        }
      }
    }
  ]
})json");
    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto graph = thermox::platform::compile_model_graph(
        document, registry, "design");
    thermox::SolverOptions options;
    options.max_iterations = 80;
    const auto result = thermox::solve_newton(
        graph.problem, options);
    require(result.diagnostics.converged,
            result.diagnostics.message);
    const auto value = [&](const std::string& name) {
        return result.x.at(require_variable_index(
            graph.problem.variable_names, name));
    };
    const auto package =
        thermox::physics::make_default_property_package_registry()
            .create("water_steam_if97", "Water");
    const auto evaporator_saturation = package->saturation_p(
        value("evaporator.outlet.p"));
    const auto condenser_saturation = package->saturation_p(
        value("condenser.outlet.p"));
    require(evaporator_saturation.ok() &&
                condenser_saturation.ok(),
            "phase-change saturation pairs should be valid");
    require_near(
        value("evaporator.outlet.h"),
        evaporator_saturation.vapor.enthalpy_j_kg,
        1.0e-3, "saturated-vapor evaporator outlet");
    require_near(
        value("condenser.outlet.h"),
        condenser_saturation.liquid.enthalpy_j_kg,
        1.0e-3, "saturated-liquid condenser outlet");
    require(value("evaporator.heat.Q_dot") > 0.0,
            "evaporator receives positive heat");
    require(value("condenser.heat.Q_dot") > 0.0,
            "condenser rejects positive heat");
}

void test_if97_rankine_graph_regression() {
    const auto document = thermox::platform::load_model_document(
        std::string(THERMOX_SOURCE_DIR) +
        "/core/examples/simple_rankine.json");
    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto graph = thermox::platform::compile_model_graph(
        document, registry, "design");
    require(graph.reduced_connection_equations.size() == 2,
            "closed Rankine loop should reduce two exact linear closure rows; actual=" +
                std::to_string(
                    graph.reduced_connection_equations.size()));
    require(
        std::find(
            graph.reduced_connection_equations.begin(),
            graph.reduced_connection_equations.end(),
            "connection.condenser_to_pump.m_dot") !=
            graph.reduced_connection_equations.end(),
        "closed Rankine loop reduces mass-flow closure");
    require(
        std::find(
            graph.reduced_connection_equations.begin(),
            graph.reduced_connection_equations.end(),
            "connection.condenser_to_pump.p") !=
            graph.reduced_connection_equations.end(),
        "closed Rankine loop reduces pressure closure");
    thermox::SolverOptions options;
    options.max_iterations = 80;
    const auto result = thermox::solve_newton(
        graph.problem, options);
    require(result.diagnostics.converged,
            result.diagnostics.message);
    const auto value = [&](const std::string& name) {
        return result.x.at(require_variable_index(
            graph.problem.variable_names, name));
    };
    const double pump_power = value("pump.shaft.W_dot");
    const double turbine_power = value("turbine.shaft.W_dot");
    const double evaporator_heat =
        value("evaporator.heat.Q_dot");
    const double condenser_heat =
        value("condenser.heat.Q_dot");
    const double net_power = turbine_power - pump_power;
    require(pump_power > 0.0,
            "Rankine pump consumes positive shaft power");
    require(turbine_power > pump_power,
            "Rankine turbine output exceeds pump consumption");
    require(evaporator_heat > 0.0 && condenser_heat > 0.0,
            "Rankine heat duties use positive port magnitudes");
    require_near(
        net_power,
        evaporator_heat - condenser_heat,
        1.0e-3,
        "closed Rankine cycle energy balance closes");
    require_near(value("condenser.outlet.m_dot"),
                 value("pump.inlet.m_dot"), 1.0e-8,
                 "reduced mass-flow closure remains satisfied");
    require_near(value("condenser.outlet.p"),
                 value("pump.inlet.p"), 1.0e-6,
                 "reduced pressure closure remains satisfied");
    require_near(value("condenser.outlet.h"),
                 value("pump.inlet.h"), 1.0e-6,
                 "retained enthalpy closure remains satisfied");
    const double efficiency = net_power / evaporator_heat;
    require(efficiency > 0.20 && efficiency < 0.30,
            "Rankine thermal efficiency remains in regression range");

    const thermox::platform::GraphResultEvaluator evaluator(
        document, graph,
        thermox::physics::
            make_default_property_package_registry());
    const auto graph_result = evaluator.evaluate(result.x);
    const auto quality = [&](const std::string& component,
                             const std::string& port) {
        return require_result_value(
            require_port_result(
                graph_result, component, port)
                .derived_values,
            "vapor_quality");
    };
    require_near(quality("evaporator", "outlet"), 0.99,
                 1.0e-8,
                 "Rankine evaporator outlet quality");
    require_near(quality("condenser", "outlet"), 0.01,
                 1.0e-8,
                 "Rankine condenser outlet quality");
    require_near(value("pump.outlet.p"), 5.0e6, 1.0e-4,
                 "Rankine high pressure");
    require_near(value("turbine.outlet.p"), 1.0e5, 1.0e-5,
                 "Rankine condenser pressure");
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
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "if97_pump",
    "media": [
      {"id": "water", "backend": "water_steam_if97", "substance": "Water"}
    ],
    "components": [
      {
        "id": "pump",
        "kind": "pump.fluid.isentropic_efficiency",
        "media": {"inlet": "water", "outlet": "water"},
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
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "sco2_compressor",
    "media": [
      {"id": "co2", "backend": "co2_span_wagner", "substance": "CO2"}
    ],
    "components": [
      {"id": "compressor", "kind": "compressor.fluid.isentropic_efficiency",
       "media": {"inlet": "co2", "outlet": "co2"}, "parameters": {
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
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "unregistered_kind",
    "media": [
      {
        "id": "air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      }
    ],
    "components": [
      {
        "id": "x",
        "kind": "not.registered",
        "media": {
          "outlet": "air"
        }
      }
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
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "bad_contract",
    "media": [
      {
        "id": "air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      }
    ],
    "components": [
      {
        "id": "compressor",
        "kind": "compressor.gas.isentropic_efficiency",
        "media": {
          "inlet": "air",
          "outlet": "air",
          "shaft": "air"
        }
      }
    ],
    "connections": []
  },
  "cases": []
})json");
    const auto registry = thermox::platform::make_default_component_registry();
    require_throws([&]() { (void)thermox::platform::compile_model_graph(document, registry); },
                   "medium binding for non-fluid port: shaft");
}

void test_generic_model_compiler_rejects_unknown_case_variable() {
    const auto document = thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "unknown_case_var",
    "media": [
      {
        "id": "air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      }
    ],
    "components": [
      {
        "id": "ambient",
        "kind": "source.fluid.boundary",
        "media": {
          "outlet": "air"
        }
      }
    ],
    "connections": []
  },
  "cases": [
    {
      "id": "design",
      "mode": "steady_state_design",
      "fixed_values": {
        "missing.port.p": {
          "value": 1.0,
          "unit": "bar"
        }
      }
    }
  ]
})json");
    const auto registry = thermox::platform::make_default_component_registry();
    require_throws([&]() { (void)thermox::platform::compile_model_graph(document, registry, "design"); },
                   "fixed value references unknown variable");
}

void test_compiler_reports_under_and_over_specification() {
    const auto under_specified =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "under_specified",
    "media": [
      {
        "id": "air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      }
    ],
    "components": [
      {
        "id": "source",
        "kind": "source.fluid.boundary",
        "media": {
          "outlet": "air"
        }
      }
    ],
    "connections": []
  },
  "cases": [
    {
      "id": "design",
      "mode": "steady_state_design"
    }
  ]
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
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "over_specified",
    "media": [
      {
        "id": "air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      }
    ],
    "components": [
      {
        "id": "compressor",
        "kind": "compressor.fluid.isentropic_efficiency",
        "parameters": {
          "pressure_ratio": 2.0,
          "eta_is": 0.8
        },
        "media": {
          "inlet": "air",
          "outlet": "air"
        }
      }
    ],
    "connections": []
  },
  "cases": [
    {
      "id": "design",
      "mode": "steady_state_design",
      "fixed_values": {
        "compressor.inlet.m_dot": {
          "value": 1.0,
          "unit": "kg/s"
        },
        "compressor.inlet.p": {
          "value": 1.0,
          "unit": "bar"
        },
        "compressor.inlet.h": {
          "value": 300.0,
          "unit": "kJ/kg"
        },
        "compressor.outlet.p": {
          "value": 2.0,
          "unit": "bar"
        },
        "compressor.shaft.omega": 300.0
      }
    }
  ]
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
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "capability_check",
    "media": [
      {
        "id": "limited",
        "backend": "pt_only",
        "substance": "Test"
      }
    ],
    "components": [
      {
        "id": "compressor",
        "kind": "compressor.fluid.isentropic_efficiency",
        "parameters": {
          "pressure_ratio": 2.0,
          "eta_is": 0.8
        },
        "media": {
          "inlet": "limited",
          "outlet": "limited"
        }
      }
    ],
    "connections": []
  },
  "cases": []
})json");
    auto properties = thermox::physics::make_default_property_package_registry();
    properties.register_backend(
        {"pt_only", "pt-only-test", "1.0.0", {"Test"},
         {thermox::physics::PropertyCapability::state_pt}},
        [](std::string_view) {
            return std::make_shared<PtOnlyPropertyPackage>();
        });
    const auto components = thermox::platform::make_default_component_registry();
    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                document, components, properties);
        },
        "requires property capability 'state_ph'");

    const auto saturation_document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "saturation_capability_check",
    "media": [
      {
        "id": "air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      }
    ],
    "components": [
      {
        "id": "evaporator",
        "kind": "evaporator.fluid.fixed_outlet_quality",
        "parameters": {
          "outlet_quality": 1.0
        },
        "media": {
          "inlet": "air",
          "outlet": "air"
        }
      }
    ],
    "connections": []
  },
  "cases": []
})json");
    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                saturation_document, components);
        },
        "requires property capability 'saturation_p'");
}

void test_transient_model_compiles_and_integrates_lumped_storage() {
    const auto document = thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "thermal_storage_transient",
    "media": [],
    "components": [
      {
        "id": "heater",
        "kind": "source.heat.boundary"
      },
      {
        "id": "store",
        "kind": "storage.thermal.lumped",
        "parameters": {
          "thermal_capacity": {
            "value": 2.0,
            "unit": "MJ/K"
          }
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
        "heater.outlet.Q_dot": {
          "value": 1.0,
          "unit": "MW"
        }
      },
      "initial_guesses": {
        "store.temperature": {
          "value": 300.0,
          "unit": "K"
        }
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
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "rigid_fluid_volume_transient",
    "media": [
      {
        "id": "air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      }
    ],
    "components": [
      {
        "id": "source",
        "kind": "source.fluid.boundary",
        "media": {
          "outlet": "air"
        }
      },
      {
        "id": "tank",
        "kind": "volume.fluid.rigid_adiabatic",
        "parameters": {
          "volume": {
            "value": 1000.0,
            "unit": "L"
          }
        },
        "media": {
          "inlet": "air",
          "outlet": "air"
        }
      },
      {
        "id": "sink",
        "kind": "sink.fluid.boundary",
        "media": {
          "inlet": "air"
        }
      }
    ],
    "connections": [
      {
        "id": "feed",
        "from": "source.outlet",
        "to": "tank.inlet",
        "kind": "fluid_link"
      },
      {
        "id": "discharge",
        "from": "tank.outlet",
        "to": "sink.inlet",
        "kind": "fluid_link"
      }
    ]
  },
  "cases": [
    {
      "id": "fill",
      "mode": "dynamic_transient",
      "fixed_values": {
        "source.outlet.m_dot": {
          "value": 2.0,
          "unit": "kg/s"
        },
        "source.outlet.p": {
          "value": 1.01325,
          "unit": "bar"
        },
        "source.outlet.h": {
          "value": 301.35,
          "unit": "kJ/kg"
        },
        "sink.inlet.m_dot": {
          "value": 1.0,
          "unit": "kg/s"
        }
      },
      "initial_guesses": {
        "tank.mass": {
          "value": 1.17683,
          "unit": "kg"
        },
        "tank.total_energy": {
          "value": 253.33,
          "unit": "kJ"
        },
        "tank.pressure": {
          "value": 1.01325,
          "unit": "bar"
        },
        "tank.enthalpy": {
          "value": 301.35,
          "unit": "kJ/kg"
        }
      }
    }
  ]
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
        const auto reconstructed = package->state_ph(
            backend_case.pressure, state.state.enthalpy_j_kg);
        require(reconstructed.ok(),
                backend_case.backend +
                    " volume PH initial state should be valid");
        const double initial_mass =
            reconstructed.state.density_kg_m3;
        const double initial_energy =
            initial_mass *
            reconstructed.state.internal_energy_j_kg;
        std::ostringstream number;
        number << std::setprecision(17);
        const auto format = [&](double value) {
            number.str({});
            number.clear();
            number << value;
            return number.str();
        };
        std::string text = R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "real_fluid_volume",
    "media": [{"id": "fluid", "backend": "__BACKEND__", "substance": "__SUBSTANCE__"}],
    "components": [{
      "id": "tank",
      "kind": "volume.fluid.rigid_adiabatic",
      "media": {"inlet": "fluid", "outlet": "fluid"},
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
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "invalid_dynamic_component",
    "media": [
      {
        "id": "air",
        "backend": "ideal_gas_mixture",
        "substance": "Air"
      }
    ],
    "components": [
      {
        "id": "valve",
        "kind": "valve.fluid.isenthalpic_pressure_ratio",
        "parameters": {
          "pressure_ratio": 2.0
        },
        "media": {
          "inlet": "air",
          "outlet": "air"
        }
      }
    ],
    "connections": []
  },
  "cases": [
    {
      "id": "dynamic",
      "mode": "dynamic_transient"
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
        "does not support transient compilation");
}

void test_shaft_train_and_generator_close_power_balance() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "shaft_power_balance",
    "media": [],
    "components": [
      {
        "id": "train",
        "kind": "shaft.train.two_load",
        "parameters": {
          "mechanical_efficiency": 0.99,
          "fixed_loss": {"value": 1.0, "unit": "MW"}
        }
      },
      {
        "id": "generator",
        "kind": "generator.electrical.efficiency",
        "parameters": {
          "generator_efficiency": 0.98,
          "pole_pairs": 1.0
        }
      },
      {
        "id": "grid",
        "kind": "sink.electrical.boundary"
      }
    ],
    "connections": [
      {
        "id": "generator_shaft",
        "from": "train.load_2",
        "to": "generator.shaft",
        "kind": "shaft_link"
      },
      {
        "id": "grid_link",
        "from": "generator.electrical",
        "to": "grid.inlet",
        "kind": "electrical_link"
      }
    ]
  },
  "cases": [{
    "id": "rated",
    "mode": "steady_state_design",
    "fixed_values": {
      "train.driver.W_dot": {"value": 300.0, "unit": "MW"},
      "train.driver.omega": 314.1592653589793,
      "train.load_1.W_dot": {"value": 40.0, "unit": "MW"}
    }
  }]
})json");
    const auto graph = thermox::platform::compile_model_graph(
        document,
        thermox::platform::make_default_component_registry(),
        "rated");
    const auto result = thermox::solve_newton(graph.problem);
    require(
        result.diagnostics.converged,
        result.diagnostics.message);
    const auto value = [&](const std::string& name) {
        const auto found = std::find(
            graph.problem.variable_names.begin(),
            graph.problem.variable_names.end(), name);
        require(
            found != graph.problem.variable_names.end(),
            "power-train result variable missing: " + name);
        return result.x.at(static_cast<std::size_t>(
            std::distance(
                graph.problem.variable_names.begin(), found)));
    };
    const double generator_shaft_power =
        0.99 * 300.0e6 - 40.0e6 - 1.0e6;
    require_near(
        value("generator.shaft.W_dot"),
        generator_shaft_power, 1.0e-6,
        "shaft train allocates driver power after losses");
    require_near(
        value("grid.inlet.P"),
        0.98 * generator_shaft_power, 1.0e-6,
        "generator converts shaft power to electrical power");
    require_near(
        value("grid.inlet.frequency"), 50.0, 1.0e-10,
        "generator converts shaft speed to electrical frequency");
}

void test_transient_compiler_rejects_fixed_differential_state() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "fixed_dynamic_state",
    "media": [],
    "components": [
      {
        "id": "store",
        "kind": "storage.thermal.lumped",
        "parameters": {
          "thermal_capacity": 1000.0
        }
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
        test_component_artifact_bindings_resolve_at_compile_time();
        test_generic_model_document_loads_components_connections_and_cases();
        test_model_document_supports_component_and_system_calibration();
        test_model_document_rejects_invalid_calibration_target();
        test_generic_model_document_rejects_unknown_medium();
        test_generic_model_document_rejects_invalid_topology();
        test_generic_model_document_rejects_unsupported_units();
        test_compiler_enforces_connection_contracts();
        test_component_registry_exposes_default_models();
        test_component_registry_rejects_unknown_kind();
        test_component_catalog_exposes_parameter_contracts();
        test_component_parameter_contracts_are_enforced();
        test_generic_model_compiles_to_connection_equations();
        test_generic_model_solves_ideal_gas_compressor_residuals();
        test_map_driven_compressor_solves_bound_operating_point();
        test_map_driven_turbine_solves_bound_operating_point();
        test_generic_model_solves_ideal_gas_turbine_residuals();
        test_generic_model_solves_two_inlet_mixer();
        test_generic_model_solves_two_outlet_splitter();
        test_generic_model_solves_isenthalpic_valve();
        test_material_connector_and_frozen_transport();
        test_material_mixer_and_fixed_fraction_splitter();
        test_material_thermochemistry_resolves_on_demand();
        test_adiabatic_equilibrium_combustor();
        test_material_compressor_and_turbine();
        test_map_driven_material_turbomachinery();
        test_generic_model_solves_cross_medium_fixed_duty_heat_exchanger();
        test_generic_model_solves_counterflow_ua_heat_exchanger();
        test_if97_fixed_quality_evaporator_and_condenser();
        test_if97_rankine_graph_regression();
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
        test_shaft_train_and_generator_close_power_balance();
        test_transient_compiler_rejects_steady_only_components();
        test_transient_compiler_rejects_fixed_differential_state();
    } catch (const std::exception& ex) {
        std::cerr << "test failure: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "thermox_platform_tests passed\n";
    return 0;
}
