#include "thermox/continuation_solver.hpp"
#include "thermox/nonlinear_solver.hpp"
#include "thermox/platform/calibration.hpp"
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
#include <numbers>
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
                    lower_heating_value ||
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
    thermox::physics::HeatingValueResult lower_heating_value(
        double,
        double,
        const thermox::physics::SpeciesComposition&) const override {
        return {
            10.0e6,
            thermox::physics::PropertyStatus::success,
            {},
        };
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
        state.thermodynamic.density_kg_m3 =
            pressure / (287.0 * temperature);
        state.thermodynamic.internal_energy_j_kg =
            713.0 * temperature;
        state.thermodynamic.cp_j_kg_k = 1000.0;
        state.thermodynamic.cv_j_kg_k = 713.0;
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
            context.artifacts.find("performance_map");
        if (found == context.artifacts.end() ||
            !found->second ||
            !std::dynamic_pointer_cast<const
                thermox::platform::PerformanceMapArtifact>(
                    found->second)) {
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

class StructurallySingularSignalModel final
    : public thermox::platform::ComponentModel {
public:
    StructurallySingularSignalModel() {
        descriptor_.kind = "test.structurally_singular";
        descriptor_.version = "1.0.0";
        descriptor_.ports = {
            {"left", "signal", "out"},
            {"right", "signal", "out"},
        };
    }

    const thermox::platform::ComponentModelDescriptor&
    descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const thermox::platform::ComponentCompileContext& context,
        thermox::EquationSystemBuilder& system) const override {
        const auto left =
            context.port_variables.at("left.value");
        const std::string prefix =
            "component." + context.component.id + ".";
        system.add_linear_equation(
            prefix + "left_target_a",
            {{left, 1.0}}, 1.0, 1.0);
        system.add_linear_equation(
            prefix + "left_target_b",
            {{left, 1.0}}, 2.0, 1.0);
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
    thermox::platform::EngineeringArtifactRegistry maps;
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
                thermox::platform::EngineeringArtifactRegistry{});
        },
        "no engineering artifact registered");
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
        "kind": "compressor.fluid.isentropic_efficiency",
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
        "kind": "turbine.fluid.isentropic_efficiency",
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
      "parameter_overrides": {
        "components.compressor.parameters.eta_is": {
          "value": 82.0,
          "unit": "%"
        }
      },
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
    require(compressor.kind == "compressor.fluid.isentropic_efficiency", "component kind should load");
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
    require_near(
        document.cases.at(0).parameter_overrides.at(
            "components.compressor.parameters.eta_is").value_si,
        0.82, 1.0e-12,
        "case parameter override should normalize and retain its target");
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
        "kind": "shaft.train.multi_load",
        "port_counts": {"load": 2},
        "parameters": {
          "fixed_loss": {"value": 2.0, "unit": "MW"}
        }
      }
    ],
    "connections": []
  },
  "cases": [
    {
      "id": "baseline",
      "mode": "steady_state_design",
      "fixed_values": {
        "air_source.outlet.m_dot": {"value": 600.0, "unit": "kg/s"}
      }
    },
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
        },
        {
          "id": "baseline_airflow",
          "scope": "system",
          "targets": [
            "cases.baseline.fixed_values.air_source.outlet.m_dot"
          ],
          "cases": ["baseline"],
          "bounds": {
            "lower": {"value": 500.0, "unit": "kg/s"},
            "upper": {"value": 700.0, "unit": "kg/s"}
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
        calibration.parameters.size() == 3,
        "component, system, and case-boundary calibration parameters "
        "should coexist");
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
    require(
        calibration.parameters.at(2).targets.front() ==
            "cases.baseline.fixed_values.air_source.outlet.m_dot",
        "system calibration should retain explicit case-boundary "
        "ownership");
    auto mutable_document = document;
    auto& airflow = thermox::platform::
        require_calibration_parameter_target(
            mutable_document,
            calibration.parameters.at(2).targets.front());
    airflow.value_si = 625.0;
    require_near(
        mutable_document.cases.front().fixed_values.at(
            "air_source.outlet.m_dot").value_si,
        625.0, 1.0e-12,
        "case-boundary calibration target should resolve to its owned "
        "fixed value");
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
            "unit": "furlong/fortnight"
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
    require(registry.contains("compressor.fluid.isentropic_efficiency"),
            "default registry should contain compressor");
    require(registry.contains("compressor.fluid.isentropic_efficiency"),
            "default registry should contain generic-fluid compressor");
    require(registry.contains("compressor.fluid.performance_map"),
            "default registry should contain map-driven compressor");
    require(registry.contains(
                "compressor.fluid.variable_geometry_map"),
            "default registry should contain variable-geometry "
            "compressor");
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
    require(registry.contains(
                "junction.material.splitter.controlled_fraction"),
            "default registry should contain controlled material splitter");
    require(registry.contains(
                "junction.material.cross_bleed.performance_map"),
            "default registry should contain map-driven material cross-bleed");
    require(registry.contains(
                "transport.material.perfect_gas_mach_scaled_loss"),
            "default registry should contain Mach-scaled material duct");
    require(registry.contains(
                "terminal.material.convergent_nozzle"),
            "default registry should contain convergent material nozzle");
    require(registry.contains("shaft.combiner.two_driver"),
            "default registry should contain shaft combiner");
    require(registry.contains("gearbox.shaft.fixed_ratio"),
            "default registry should contain fixed-ratio gearbox");
    require(registry.contains("shaft.train.multi_load"),
            "default registry should contain instance-sized shaft train");
    require(registry.contains(
                "converter.fluid_to_electrical.polynomial_efficiency"),
            "default registry should contain the conservative "
            "fluid-to-electric reduced-order converter");
    require(registry.contains("valve.fluid.isenthalpic_pressure_ratio"),
            "default registry should contain fluid valve");
    require(registry.contains("heat_exchanger.fluid.fixed_duty"),
            "default registry should contain fixed-duty heat exchanger");
    require(registry.contains("heat_exchanger.fluid.counterflow_ua"),
            "default registry should contain counterflow UA heat exchanger");
    require(registry.contains(
                "heat_exchanger.material_fluid.fixed_duty"),
            "default registry should contain material-fluid fixed-duty "
            "heat exchanger");
    require(registry.contains(
                "heat_exchanger.material_fluid.counterflow_ua"),
            "default registry should contain material-fluid UA heat "
            "exchanger");
    require(registry.contains(
                "heat_exchanger.material_fluid.energy_balance"),
            "default registry should contain material-fluid "
            "design-point energy balance");
    require(registry.contains("heater.material.fixed_duty"),
            "default registry should contain fixed-duty material heater");
    require(registry.contains("cooler.material.fixed_duty"),
            "default registry should contain fixed-duty material cooler");
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
        "source.material.fixed_composition",
        "source.material.controlled_fixed_composition",
        "sink.material.boundary",
        "source.heat.boundary",
        "sink.heat.boundary",
        "source.shaft.boundary",
        "sink.shaft.boundary",
        "source.electrical.boundary",
        "sink.electrical.boundary",
        "source.signal.boundary",
        "sink.signal.boundary",
        "source.control.boundary",
        "sink.control.boundary",
        "source.force.boundary",
        "sink.force.boundary",
        "compressor.fluid.isentropic_efficiency",
        "compressor.fluid.performance_map",
        "compressor.fluid.variable_geometry_map",
        "compressor.material.isentropic_efficiency",
        "compressor.material.iso2314_equivalent_cooling",
        "compressor.material.performance_map",
        "compressor.material.coordinate_map",
        "compressor.material.coordinate_map.fractional_bleeds",
        "compressor.material.variable_geometry_map",
        "pump.fluid.isentropic_efficiency",
        "turbine.fluid.isentropic_efficiency",
        "turbine.fluid.performance_map",
        "turbine.fluid.variable_geometry_map",
        "turbine.material.isentropic_efficiency",
        "turbine.material.performance_map",
        "turbine.material.coordinate_map",
        "turbine.material.coordinate_map.cooling_injections",
        "turbine.material.variable_geometry_map",
        "junction.fluid.mixer.two_inlet",
        "junction.fluid.splitter.two_outlet",
        "pipe.fluid.hydraulic_inertance",
        "junction.material.mixer.two_inlet",
        "junction.material.splitter.fixed_fraction",
        "junction.material.splitter.controlled_fraction",
        "junction.material.cross_bleed.performance_map",
        "transport.material.perfect_gas_mach_scaled_loss",
        "transport.material.freestream_momentum",
        "terminal.material.convergent_nozzle",
        "valve.fluid.isenthalpic_pressure_ratio",
        "valve.fluid.actuated_nonflashing_liquid",
        "restriction.fluid.orifice.nonflashing_liquid",
        "restriction.fluid.orifice.perfect_gas",
        "restriction.fluid.local_loss.homogeneous_two_phase",
        "pipe.fluid.homogeneous_equilibrium_local_loss",
        "pipe.fluid.constant_slip_two_phase_local_loss",
        "pipe.fluid.correlated_two_phase_pressure_drop",
        "fitting.fluid.return_bend.fixed_loss_coefficient",
        "fitting.fluid.return_bend.correlation",
        "pipe.fluid.darcy_weisbach",
        "pipe.fluid.darcy_weisbach_heat_transfer",
        "separator.fluid.equilibrium_flash",
        "transport.material.frozen_pressure_ratio",
        "regulator.material.isenthalpic_network_pressure",
        "combustor.material.adiabatic_equilibrium",
        "combustor.material.equilibrium_heat_release_efficiency",
        "combustor.material.equilibrium_declared_lhv",
        "heat_exchanger.fluid.fixed_duty",
        "heat_exchanger.fluid.counterflow_ua",
        "heat_exchanger.fluid.dynamic_cell",
        "heat_exchanger.material_fluid.fixed_duty",
        "heat_exchanger.material_fluid.energy_balance",
        "heat_exchanger.material_fluid.counterflow_ua",
        "heat_exchanger.material_fluid.dynamic_cell",
        "heat_exchanger.material_fluid.equilibrium_two_phase_cell",
        "heater.material.fixed_duty",
        "cooler.material.fixed_duty",
        "evaporator.fluid.fixed_outlet_quality",
        "condenser.fluid.fixed_outlet_quality",
        "drum.fluid.equilibrium_two_phase",
        "volume.fluid.rigid_adiabatic",
        "volume.fluid.rigid_heat_transfer",
        "volume.fluid.equilibrium_two_phase_correlated_outlet",
        "storage.thermal.lumped",
        "storage.thermal.wall_two_sided",
        "shaft.train.multi_load",
        "shaft.combiner.two_driver",
        "gearbox.shaft.fixed_ratio",
        "shaft.inertia.two_port",
        "shaft.inertia.multi_load",
        "sensor.shaft.normalized_speed",
        "generator.electrical.efficiency",
        "converter.fluid_to_electrical.polynomial_efficiency",
        "balance.force.propulsive",
        "control.proportional.normalized",
        "control.first_order_lag.normalized",
        "sensor.first_order_lag.normalized",
        "control.pi_bounded.normalized"};
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
    bool saw_propagated_mass_flow = false;
    for (std::size_t i = 0; i < graph.problem.variable_names.size(); ++i) {
        if (graph.problem.variable_names.at(i) == "compressor.inlet.p") {
            require_near(graph.problem.initial_guess.at(i), 100000.0, 1.0e-9,
                         "case initial guess should seed compiled variable");
            saw_pressure_guess = true;
        } else if (
            graph.problem.variable_names.at(i) ==
            "compressor.inlet.m_dot") {
            require_near(
                graph.problem.initial_guess.at(i), 100.0,
                1.0e-12,
                "fixed source flow should seed the connected port");
            saw_propagated_mass_flow = true;
        }
    }
    require(saw_pressure_guess, "compiled variables should include compressor inlet pressure");
    require(
        saw_propagated_mass_flow,
        "compiled variables should include propagated inlet flow");

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
    const auto mass_connection = std::find(
        graph.problem.residual_names.begin(),
        graph.problem.residual_names.end(),
        "connection.air_link.m_dot");
    require(
        mass_connection != graph.problem.residual_names.end(),
        "compiled graph exposes mass-flow connection residual");
    require_near(
        residual.at(static_cast<std::size_t>(
            std::distance(
                graph.problem.residual_names.begin(),
                mass_connection))),
        0.0, 1.0e-12, "mass-flow connection residual");
}


void test_generic_model_solves_ideal_gas_compressor_residuals() {
    auto document = thermox::platform::parse_model_document_text(R"json({
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
    require(
        static_cast<bool>(
            graph.problem.continuation_checked_residual),
        "compressor graph exposes a physical continuation path");
    require(
        static_cast<bool>(
            graph.problem
                .continuation_partial_sparse_jacobian),
        "compressor continuation preserves hybrid derivatives");

    auto equal_pressure_state = graph.problem.initial_guess;
    std::size_t inlet_p_index = 0;
    std::size_t outlet_p_index = 0;
    for (std::size_t index = 0;
         index < graph.problem.variable_names.size(); ++index) {
        if (graph.problem.variable_names[index] ==
            "compressor.inlet.p") {
            inlet_p_index = index;
        } else if (graph.problem.variable_names[index] ==
                   "compressor.outlet.p") {
            outlet_p_index = index;
        }
    }
    equal_pressure_state[inlet_p_index] = 101325.0;
    equal_pressure_state[outlet_p_index] = 101325.0;
    std::vector<double> easy_residual(
        graph.problem.residual_names.size(), 0.0);
    const auto easy_status =
        graph.problem.continuation_checked_residual(
            equal_pressure_state, equal_pressure_state,
            0.0, easy_residual);
    require(easy_status.ok(), easy_status.message);
    const auto pressure_equation = std::find(
        graph.problem.residual_names.begin(),
        graph.problem.residual_names.end(),
        "component.compressor.pressure_ratio");
    require(
        pressure_equation !=
            graph.problem.residual_names.end(),
        "compiled compressor carries pressure-ratio equation");
    const auto pressure_equation_index =
        static_cast<std::size_t>(std::distance(
            graph.problem.residual_names.begin(),
            pressure_equation));
    require_near(
        easy_residual.at(pressure_equation_index),
        0.0, 0.0,
        "compressor continuation begins at unity pressure ratio");

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

    auto off_design = document.cases.front();
    off_design.id = "off_design";
    off_design.mode = "steady_state_off_design";
    off_design.parameter_overrides = {
        {"components.compressor.parameters.pressure_ratio",
         {6.0, "dimensionless", "dimensionless"}},
        {"components.compressor.parameters.eta_is",
         {0.8, "dimensionless", "dimensionless"}},
    };
    document.cases.push_back(off_design);
    const auto off_design_graph =
        thermox::platform::compile_model_graph(
            document, registry, "off_design");
    const auto off_design_result =
        thermox::solve_newton(off_design_graph.problem);
    require(off_design_result.diagnostics.converged,
            off_design_result.diagnostics.message);
    require_near(
        off_design_result.x.at(require_variable_index(
            off_design_graph.problem.variable_names,
            "compressor.outlet.p")),
        6.0 * 101325.0, 1.0e-5,
        "case override must change the effective component "
        "pressure ratio");
    require_near(
        document.components.front()
            .parameters.at("pressure_ratio").value_si,
        12.0, 1.0e-12,
        "case override must not mutate the topology component");

    auto invalid = document;
    invalid.cases.back().parameter_overrides = {
        {"components.compressor.parameters.pressure_ratio",
         {-1.0, "dimensionless", "dimensionless"}},
    };
    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                invalid, registry, "off_design");
        },
        "outside its declared bounds");
}

void test_fixed_boundary_continuation_is_explicit_and_reaches_target() {
    const auto compile = [](bool enabled) {
        const std::string option = enabled
            ? R"json(,"solver_options":{"boundary_continuation":1.0})json"
            : "";
        return thermox::platform::compile_model_graph(
            thermox::platform::parse_model_document_text(
                std::string(R"json({
  "schema_version":"thermox.model/v2",
  "model":{
    "id":"boundary_continuation",
    "media":[{"id":"air","backend":"ideal_gas","substance":"Air"}],
    "components":[{
      "id":"source","kind":"source.fluid.boundary",
      "media":{"outlet":"air"}
    }],
    "connections":[]
  },
  "cases":[{
    "id":"target","mode":"steady_state_off_design",
    "fixed_values":{
      "source.outlet.m_dot":{"value":10.0,"unit":"kg/s"},
      "source.outlet.p":{"value":200.0,"unit":"kPa"},
      "source.outlet.T":{"value":600.0,"unit":"K"}
    },
    "initial_guesses":{
      "source.outlet.m_dot":{"value":5.0,"unit":"kg/s"},
      "source.outlet.p":{"value":100.0,"unit":"kPa"},
      "source.outlet.h":{"value":300.0,"unit":"kJ/kg"}
    })json") + option + R"json(}]
})json"),
            thermox::platform::make_default_component_registry(),
            "target");
    };
    const auto staged = compile(true);
    const auto direct = compile(false);
    const auto pressure_row = require_variable_index(
        staged.problem.residual_names, "fixed.target.source.outlet.p");
    const auto temperature_row = require_variable_index(
        staged.problem.residual_names, "fixed.target.source.outlet.T");
    std::vector<double> residual(staged.problem.residual_names.size());
    auto status = staged.problem.continuation_checked_residual(
        staged.problem.initial_guess, staged.problem.initial_guess,
        0.0, residual);
    require(status.ok(), status.message);
    require_near(residual.at(pressure_row), 0.0, 0.0,
        "boundary continuation starts from the declared initial state");
    require_near(residual.at(temperature_row), 0.0, 1.0e-10,
        "temperature boundary continuation starts from anchor p/h state");
    status = staged.problem.continuation_checked_residual(
        staged.problem.initial_guess, staged.problem.initial_guess,
        1.0, residual);
    require(status.ok(), status.message);
    require_near(residual.at(pressure_row), -100000.0, 1.0e-8,
        "boundary continuation reaches the exact fixed target");

    status = direct.problem.continuation_checked_residual(
        direct.problem.initial_guess, direct.problem.initial_guess,
        0.0, residual);
    require(status.ok(), status.message);
    require_near(residual.at(pressure_row), -100000.0, 1.0e-8,
        "fixed boundaries remain at target when continuation is disabled");
    const auto solved = thermox::solve_continuation(staged.problem);
    require(solved.continuation.converged, solved.continuation.message);
    require_near(solved.x.at(require_variable_index(
                     staged.problem.variable_names, "source.outlet.p")),
                 200000.0, 1.0e-8,
        "boundary continuation finishes at the requested pressure");
}

void test_hierarchical_assembly_composes_compressor_stages() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "segmented_compressor",
    "media": [{
      "id": "air", "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "components": [],
    "assemblies": [{
      "id": "compressor",
      "label": "Two-stage compressor assembly",
      "ports": [
        {"name": "inlet", "endpoint": "stage_01.inlet"},
        {"name": "outlet", "endpoint": "stage_02.outlet"}
      ],
      "parameters": [{
        "name": "high_stage_pressure_ratio",
        "target": "stage_02.pressure_ratio"
      }],
      "components": [{
        "id": "stage_01",
        "kind": "compressor.fluid.isentropic_efficiency",
        "parameters": {"pressure_ratio": 3.0, "eta_is": 0.88},
        "media": {"inlet": "air", "outlet": "air"}
      }, {
        "id": "stage_02",
        "kind": "compressor.fluid.isentropic_efficiency",
        "parameters": {"pressure_ratio": 4.0, "eta_is": 0.86},
        "media": {"inlet": "air", "outlet": "air"}
      }],
      "connections": [{
        "id": "stage_01_to_stage_02",
        "kind": "fluid_link",
        "from": "stage_01.outlet",
        "to": "stage_02.inlet"
      }]
    }],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "parameter_overrides": {
      "components.compressor.parameters.high_stage_pressure_ratio": 5.0
    },
    "fixed_values": {
      "compressor.inlet.m_dot": {"value": 100.0, "unit": "kg/s"},
      "compressor.inlet.p": {"value": 101.325, "unit": "kPa"},
      "compressor.inlet.T": {"value": 300.0, "unit": "K"},
      "compressor/stage_01.shaft.omega": 314.1592653589793,
      "compressor/stage_02.shaft.omega": 314.1592653589793
    },
    "initial_guesses": {
      "compressor/stage_01.outlet.p": {"value": 300.0, "unit": "kPa"},
      "compressor/stage_01.outlet.h": {"value": 400.0, "unit": "kJ/kg"},
      "compressor/stage_01.shaft.W_dot": {"value": 10.0, "unit": "MW"},
      "compressor/stage_02.outlet.p": {"value": 1.2, "unit": "MPa"},
      "compressor/stage_02.outlet.h": {"value": 550.0, "unit": "kJ/kg"},
      "compressor/stage_02.shaft.W_dot": {"value": 15.0, "unit": "MW"}
    }
  }]
})json");

    require(document.assemblies.size() == 1U &&
                document.components.empty(),
            "parser must preserve declaration-time hierarchy");
    const auto flattened =
        thermox::platform::flatten_model_document(document);
    require(flattened.assemblies.empty() &&
                flattened.components.size() == 2U &&
                flattened.components.front().id ==
                    "compressor/stage_01" &&
                flattened.connections.front().id ==
                    "compressor/stage_01_to_stage_02" &&
                flattened.cases.front().fixed_values.contains(
                    "compressor/stage_01.inlet.p") &&
                flattened.cases.front().parameter_overrides.contains(
                    "components.compressor/stage_02.parameters.pressure_ratio"),
            "assembly expansion must namespace children and resolve "
            "public case references deterministically");

    const auto graph = thermox::platform::compile_model_graph(
        document,
        thermox::platform::make_default_component_registry(),
        "design");
    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged,
            result.diagnostics.message);
    require_near(
        result.x.at(require_variable_index(
            graph.problem.variable_names,
            "compressor/stage_02.outlet.p")),
        15.0 * 101325.0, 1.0e-5,
        "stage pressure ratios must compose through the ordinary graph");
    const thermox::platform::GraphResultEvaluator evaluator(
        document, graph,
        thermox::physics::make_default_property_package_registry());
    const auto graph_result = evaluator.evaluate(result.x);
    require(
        graph_result.components.size() == 2U &&
            require_component_result(
                graph_result, "compressor/stage_02").kind ==
                "compressor.fluid.isentropic_efficiency",
        "result evaluation must retain every expanded stage");

    const auto nested =
        thermox::platform::parse_topology_document_text(R"json({
      "schema_version": "thermox.topology/v1",
      "model": {
        "id": "nested_assembly",
        "media": [{
          "id": "air", "backend": "ideal_gas_mixture",
          "substance": "Air"
        }],
        "components": [{
          "id": "source", "kind": "source.fluid.boundary",
          "media": {"outlet": "air"}
        }],
        "assemblies": [{
          "id": "machine",
          "ports": [{"name": "inlet", "endpoint": "section.inlet"}],
          "components": [],
          "assemblies": [{
            "id": "section",
            "ports": [{"name": "inlet", "endpoint": "stage.inlet"}],
            "components": [{
              "id": "stage",
              "kind": "compressor.fluid.isentropic_efficiency",
              "parameters": {"pressure_ratio": 2.0, "eta_is": 0.9},
              "media": {"inlet": "air", "outlet": "air"}
            }],
            "connections": []
          }],
          "connections": []
        }],
        "connections": [{
          "id": "source_to_machine", "kind": "fluid_link",
          "from": "source.outlet", "to": "machine.inlet"
        }]
      }
    })json");
    const auto nested_flat =
        thermox::platform::flatten_model_document(nested);
    require(
        nested_flat.components.back().id ==
                "machine/section/stage" &&
            nested_flat.connections.front().to ==
                "machine/section/stage.inlet",
        "nested assembly ports must resolve recursively into ordinary "
        "connections");

    require_throws(
        [&]() {
            (void)thermox::platform::parse_topology_document_text(
                R"json({
                  "schema_version": "thermox.topology/v1",
                  "model": {
                    "id": "invalid_assembly", "media": [],
                    "components": [],
                    "assemblies": [{
                      "id": "broken",
                      "ports": [{"name": "inlet", "endpoint": "missing.inlet"}],
                      "components": [{
                        "id": "present", "kind": "source.fluid.boundary"
                      }],
                      "connections": []
                    }],
                    "connections": []
                  }
                })json");
        },
        "unknown child");
}

void test_map_driven_compressor_solves_bound_operating_point() {
    auto document =
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

    thermox::platform::EngineeringArtifactRegistry maps;
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
                 {{60.0, {8.0, 0.85}},
                  {90.0, {8.0, 0.85}},
                  {95.0, {10.0, 0.85}},
                  {105.0, {10.0, 0.85}},
                  {110.0, {12.0, 0.85}},
                  {140.0, {12.0, 0.85}}}},
                {400.0,
                 {{60.0, {8.0, 0.85}},
                  {90.0, {8.0, 0.85}},
                  {95.0, {10.0, 0.85}},
                  {105.0, {10.0, 0.85}},
                  {110.0, {12.0, 0.85}},
                  {140.0, {12.0, 0.85}}}},
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

    auto corrected_document = document;
    auto& corrected_component =
        corrected_document.components.front();
    corrected_component.parameters["flow_capacity_scale"] = {
        1.25, "dimensionless", "dimensionless"};
    corrected_component.parameters["pressure_ratio_scale"] = {
        0.5, "dimensionless", "dimensionless"};
    corrected_component.parameters["efficiency_scale"] = {
        0.9, "dimensionless", "dimensionless"};
    const auto corrected_graph =
        thermox::platform::compile_model_graph(
            corrected_document,
            thermox::platform::make_default_component_registry(),
            thermox::physics::
                make_default_property_package_registry(),
            maps, "operating_point");
    const auto corrected =
        thermox::solve_newton(corrected_graph.problem);
    require(corrected.diagnostics.converged,
            corrected.diagnostics.message);
    const auto corrected_value = [&](const std::string& name) {
        return corrected.x.at(require_variable_index(
            corrected_graph.problem.variable_names, name));
    };
    constexpr double corrected_pressure_ratio = 4.5;
    constexpr double corrected_efficiency = 0.765;
    const double corrected_outlet_temperature =
        300.0 *
        (1.0 +
         (std::pow(
              corrected_pressure_ratio,
              (gamma - 1.0) / gamma) -
          1.0) /
             corrected_efficiency);
    require_near(
        corrected_value("compressor.outlet.p"),
        corrected_pressure_ratio * 101325.0, 1.0e-4,
        "map correction scales must shift flow lookup and pressure ratio");
    require_near(
        corrected_value("compressor.outlet.h"),
        1004.5 * corrected_outlet_temperature, 1.0e-4,
        "map efficiency correction scale must change compressor work");

    const auto geometry_layer = [](
        double pressure_ratio, double efficiency) {
        return std::make_shared<
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
                 {{80.0, {pressure_ratio, efficiency}},
                  {120.0, {pressure_ratio, efficiency}}}},
                {400.0,
                 {{80.0, {pressure_ratio, efficiency}},
                  {120.0, {pressure_ratio, efficiency}}}},
            });
    };
    maps.register_artifact({
        "variable-geometry-compressor-map",
        thermox::platform::performance_map_artifact_schema_v2,
        "test-variable-geometry-map",
        std::string(64, '9'),
        nullptr,
        std::make_shared<const
            thermox::platform::ConditionedPerformanceMap>(
            thermox::platform::MapVariable{
                "geometry_setting", "angle"},
            std::vector<thermox::platform::ConditionedMapLayer>{
                {60.0 * std::acos(-1.0) / 180.0,
                 geometry_layer(8.0, 0.80)},
                {80.0 * std::acos(-1.0) / 180.0,
                 geometry_layer(12.0, 0.90)},
            }),
    });
    auto geometry_document = document;
    auto& variable_geometry =
        geometry_document.components.front();
    variable_geometry.kind =
        "compressor.fluid.variable_geometry_map";
    variable_geometry.artifact_bindings["performance_map"] =
        "variable-geometry-compressor-map";
    variable_geometry.parameters["geometry_setting"] = {
        60.0 * std::acos(-1.0) / 180.0, "rad", "angle"};
    geometry_document.cases.front().parameter_overrides = {
        {"components.compressor.parameters.geometry_setting",
         {70.0 * std::acos(-1.0) / 180.0, "rad", "angle"}},
    };
    const auto geometry_graph =
        thermox::platform::compile_model_graph(
            geometry_document,
            thermox::platform::make_default_component_registry(),
            thermox::physics::
                make_default_property_package_registry(),
            maps, "operating_point");
    const auto geometry_result =
        thermox::solve_newton(geometry_graph.problem);
    require(geometry_result.diagnostics.converged,
            geometry_result.diagnostics.message);
    require_near(
        geometry_result.x.at(require_variable_index(
            geometry_graph.problem.variable_names,
            "compressor.outlet.p")),
        10.0 * 101325.0, 1.0e-4,
        "case geometry setting must interpolate conditioned "
        "compressor pressure ratio");

    thermox::platform::EngineeringArtifactRegistry invalid_maps;
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

void test_map_continuation_recovers_out_of_domain_flow_guess() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "map_continuation_seed",
    "media": [{
      "id": "air",
      "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "components": [
      {
        "id": "source",
        "kind": "source.fluid.boundary",
        "media": {"outlet": "air"}
      },
      {
        "id": "compressor",
        "kind": "compressor.fluid.performance_map",
        "artifacts": {"performance_map": "flow-map"},
        "parameters": {
          "reference_pressure": {"value": 101.325, "unit": "kPa"},
          "reference_temperature": {"value": 300.0, "unit": "K"}
        },
        "media": {"inlet": "air", "outlet": "air"}
      },
      {
        "id": "sink",
        "kind": "sink.fluid.boundary",
        "media": {"inlet": "air"}
      }
    ],
    "connections": [
      {
        "id": "source_to_compressor",
        "from": "source.outlet",
        "to": "compressor.inlet",
        "kind": "fluid_link"
      },
      {
        "id": "compressor_to_sink",
        "from": "compressor.outlet",
        "to": "sink.inlet",
        "kind": "fluid_link"
      }
    ]
  },
  "cases": [{
    "id": "off_design",
    "mode": "steady_state_off_design",
    "fixed_values": {
      "source.outlet.p": {"value": 101.325, "unit": "kPa"},
      "source.outlet.T": {"value": 300.0, "unit": "K"},
      "compressor.shaft.omega": 300.0,
      "sink.inlet.p": {"value": 202.65, "unit": "kPa"}
    },
    "initial_guesses": {
      "source.outlet.m_dot": {"value": 1.0, "unit": "kg/s"},
      "compressor.inlet.m_dot": {"value": 1.0, "unit": "kg/s"},
      "compressor.inlet.p": {"value": 101.325, "unit": "kPa"},
      "compressor.inlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "compressor.outlet.m_dot": {"value": 1.0, "unit": "kg/s"},
      "compressor.outlet.p": {"value": 202.65, "unit": "kPa"},
      "compressor.outlet.h": {"value": 370.0, "unit": "kJ/kg"},
      "compressor.shaft.W_dot": {"value": 0.7, "unit": "MW"},
      "sink.inlet.m_dot": {"value": 1.0, "unit": "kg/s"},
      "sink.inlet.h": {"value": 370.0, "unit": "kJ/kg"}
    }
  }]
})json");

    thermox::platform::EngineeringArtifactRegistry maps;
    maps.register_artifact({
        "flow-map",
        thermox::platform::performance_map_artifact_schema_v1,
        "out-of-domain-seed-map",
        std::string(64, '9'),
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
                 {{8.0, {1.6, 0.8}},
                  {12.0, {2.4, 0.8}}}},
                {350.0,
                 {{8.0, {1.6, 0.8}},
                  {12.0, {2.4, 0.8}}}},
            }),
    });
    const auto graph = thermox::platform::compile_model_graph(
        document,
        thermox::platform::make_default_component_registry(),
        thermox::physics::
            make_default_property_package_registry(),
        maps, "off_design");

    const auto direct = thermox::solve_newton(graph.problem);
    require(
        !direct.diagnostics.converged &&
            direct.diagnostics.message.find(
                "primary coordinate") !=
                std::string::npos &&
            direct.diagnostics.message.find(
                "outside [8, 12]") !=
                std::string::npos,
        "direct map solve must expose out-of-domain initial flow: " +
            direct.diagnostics.message);

    thermox::ContinuationOptions options;
    options.initial_step = 0.25;
    options.minimum_step = 1.0 / 1024.0;
    const auto continued =
        thermox::solve_continuation(
            graph.problem, {}, options);
    require(
        continued.continuation.converged,
        continued.continuation.message);
    require(
        continued.continuation.used_informed_path,
        "map solve reports informed coordinate path");
    require(
        continued.continuation.accepted_stages > 1,
        "map continuation advances through staged coordinates");
    const auto flow = continued.x.at(
        require_variable_index(
            graph.problem.variable_names,
            "compressor.inlet.m_dot"));
    require_near(
        flow, 10.0, 1.0e-7,
        "map continuation solves the in-domain operating flow");

    auto impossible = document;
    impossible.cases.front()
        .fixed_values.at("sink.inlet.p")
        .value_si = 303975.0;
    const auto impossible_graph =
        thermox::platform::compile_model_graph(
            impossible,
            thermox::platform::
                make_default_component_registry(),
            thermox::physics::
                make_default_property_package_registry(),
            maps, "off_design");
    const auto impossible_result =
        thermox::solve_continuation(
            impossible_graph.problem, {}, options);
    require(
        !impossible_result.continuation.converged &&
            impossible_result.continuation
                    .reached_parameter < 1.0,
        "continuation-only map extension must not admit an "
        "out-of-domain target operating point");
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

    thermox::platform::EngineeringArtifactRegistry maps;
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
        "kind": "turbine.fluid.isentropic_efficiency",
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

void test_fluid_junctions_compile_in_transient_graphs() {
    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto mixer_document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "dynamic_mixer",
    "media": [{
      "id": "air",
      "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "components": [{
      "id": "mixer",
      "kind": "junction.fluid.mixer.two_inlet",
      "media": {"inlet_a": "air", "inlet_b": "air", "outlet": "air"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "dynamic",
    "mode": "dynamic_transient",
    "fixed_values": {
      "mixer.inlet_a.m_dot": 2.0,
      "mixer.inlet_a.p": 100000.0,
      "mixer.inlet_a.h": 300000.0,
      "mixer.inlet_b.m_dot": 1.0,
      "mixer.inlet_b.h": 600000.0
    }
  }]
})json");
    const auto mixer_graph =
        thermox::platform::compile_transient_model_graph(
            mixer_document, registry, "dynamic");
    const auto mixer = thermox::make_consistent_initial_conditions(
        mixer_graph.problem, 0.0);
    require(mixer.diagnostics.converged,
            "transient mixer initialization: " +
                mixer.diagnostics.message);
    require_near(
        mixer.state.at(require_variable_index(
            mixer_graph.problem.variable_names,
            "mixer.outlet.h")),
        400000.0, 1.0e-7,
        "transient mixer conserves enthalpy flow");

    const auto splitter_document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "dynamic_splitter",
    "media": [{
      "id": "air",
      "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "components": [{
      "id": "splitter",
      "kind": "junction.fluid.splitter.two_outlet",
      "media": {"inlet": "air", "outlet_a": "air", "outlet_b": "air"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "dynamic",
    "mode": "dynamic_transient",
    "fixed_values": {
      "splitter.inlet.m_dot": 10.0,
      "splitter.inlet.p": 100000.0,
      "splitter.inlet.h": 400000.0,
      "splitter.outlet_a.m_dot": 4.0
    }
  }]
})json");
    const auto splitter_graph =
        thermox::platform::compile_transient_model_graph(
            splitter_document, registry, "dynamic");
    const auto splitter =
        thermox::make_consistent_initial_conditions(
            splitter_graph.problem, 0.0);
    require(splitter.diagnostics.converged,
            "transient splitter initialization: " +
                splitter.diagnostics.message);
    require_near(
        splitter.state.at(require_variable_index(
            splitter_graph.problem.variable_names,
            "splitter.outlet_b.m_dot")),
        6.0, 1.0e-10,
        "transient splitter conserves mass");
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

void test_nonflashing_liquid_orifice_solves_flow_and_guards_flashing() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "liquid_orifice",
    "media": [{
      "id": "water",
      "backend": "water_steam_if97",
      "substance": "Water"
    }],
    "components": [{
      "id": "orifice",
      "kind": "restriction.fluid.orifice.nonflashing_liquid",
      "parameters": {
        "flow_diameter": {"value": 20.0, "unit": "mm"},
        "discharge_coefficient": 0.62
      },
      "media": {"inlet": "water", "outlet": "water"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "subcooled",
    "mode": "steady_state_design",
    "fixed_values": {
      "orifice.inlet.p": {"value": 5.0, "unit": "bar"},
      "orifice.inlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "orifice.outlet.p": {"value": 4.0, "unit": "bar"}
    }
  }, {
    "id": "would_flash",
    "mode": "steady_state_design",
    "fixed_values": {
      "orifice.inlet.p": {"value": 5.0, "unit": "bar"},
      "orifice.inlet.h": {"value": 600.0, "unit": "kJ/kg"},
      "orifice.outlet.p": {"value": 1.0, "unit": "bar"}
    }
  }]
})json");
    const auto properties =
        thermox::physics::make_default_property_package_registry();
    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto graph = thermox::platform::compile_model_graph(
        document, registry, properties, "subcooled");
    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged,
            result.diagnostics.message);
    const auto water = properties.create("water_steam_if97", "Water");
    const auto inlet = water->state_ph(5.0e5, 3.0e5);
    require(inlet.ok(), "subcooled inlet must evaluate");
    const double area = std::numbers::pi * 0.02 * 0.02 / 4.0;
    const double expected_flow = 0.62 * area * std::sqrt(
        2.0 * inlet.state.density_kg_m3 * 1.0e5);
    require_near(
        result.x.at(require_variable_index(
            graph.problem.variable_names, "orifice.inlet.m_dot")),
        expected_flow, 1.0e-10,
        "liquid orifice follows discharge-area pressure law");
    require_near(
        result.x.at(require_variable_index(
            graph.problem.variable_names, "orifice.outlet.h")),
        3.0e5, 1.0e-8,
        "liquid restriction is isenthalpic");

    const auto flashing_graph =
        thermox::platform::compile_model_graph(
            document, registry, properties, "would_flash");
    const auto flashing_result =
        thermox::solve_newton(flashing_graph.problem);
    require(!flashing_result.diagnostics.converged,
            "non-flashing model must not extrapolate into flashing");
    require(
        flashing_result.diagnostics.message.find(
            "outlet saturation boundary") != std::string::npos,
        "flashing rejection should explain the physical boundary");
}

void test_compressible_orifice_transitions_to_choked_flow() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "gas_orifice",
    "media": [{
      "id": "air",
      "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "components": [{
      "id": "orifice",
      "kind": "restriction.fluid.orifice.perfect_gas",
      "parameters": {
        "flow_diameter": {"value": 30.0, "unit": "mm"},
        "discharge_coefficient": 0.70
      },
      "media": {"inlet": "air", "outlet": "air"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "subcritical",
    "mode": "steady_state_design",
    "fixed_values": {
      "orifice.inlet.p": {"value": 2.0, "unit": "bar"},
      "orifice.inlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "orifice.outlet.p": {"value": 1.6, "unit": "bar"}
    }
  }, {
    "id": "choked_a",
    "mode": "steady_state_design",
    "fixed_values": {
      "orifice.inlet.p": {"value": 2.0, "unit": "bar"},
      "orifice.inlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "orifice.outlet.p": {"value": 0.8, "unit": "bar"}
    }
  }, {
    "id": "choked_b",
    "mode": "steady_state_design",
    "fixed_values": {
      "orifice.inlet.p": {"value": 2.0, "unit": "bar"},
      "orifice.inlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "orifice.outlet.p": {"value": 0.4, "unit": "bar"}
    }
  }]
})json");
    const auto properties =
        thermox::physics::make_default_property_package_registry();
    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto solve_case = [&](const std::string& case_id) {
        const auto graph = thermox::platform::compile_model_graph(
            document, registry, properties, case_id);
        const auto result = thermox::solve_newton(graph.problem);
        require(result.diagnostics.converged,
                result.diagnostics.message);
        return result.x.at(require_variable_index(
            graph.problem.variable_names, "orifice.inlet.m_dot"));
    };
    const double subcritical_flow = solve_case("subcritical");
    const double choked_a = solve_case("choked_a");
    const double choked_b = solve_case("choked_b");
    require(choked_a > subcritical_flow,
            "gas orifice flow should rise before choking");
    require_near(choked_a, choked_b, 1.0e-12,
                 "choked mass flow is independent of downstream pressure");

    const auto air = properties.create("ideal_gas_mixture", "Air");
    const auto inlet = air->state_ph(2.0e5, 3.0e5);
    require(inlet.ok(), "gas-orifice inlet must evaluate");
    const double gamma =
        inlet.state.cp_j_kg_k / inlet.state.cv_j_kg_k;
    const double critical_ratio = std::pow(
        2.0 / (gamma + 1.0), gamma / (gamma - 1.0));
    const double flux = std::sqrt(
        2.0 * gamma / (gamma - 1.0) *
        inlet.state.density_kg_m3 * 2.0e5 *
        (std::pow(critical_ratio, 2.0 / gamma) -
         std::pow(critical_ratio, (gamma + 1.0) / gamma)));
    const double expected_choked_flow = 0.70 *
        std::numbers::pi * 0.03 * 0.03 / 4.0 * flux;
    require_near(choked_a, expected_choked_flow, 1.0e-9,
                 "gas orifice matches ideal-gas critical mass flux");
}

void test_actuated_liquid_valve_scales_area_with_command() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "actuated_liquid_valve",
    "media": [{
      "id": "water",
      "backend": "water_steam_if97",
      "substance": "Water"
    }],
    "components": [{
      "id": "valve",
      "kind": "valve.fluid.actuated_nonflashing_liquid",
      "parameters": {
        "full_open_diameter": {"value": 20.0, "unit": "mm"},
        "discharge_coefficient": 0.62,
        "minimum_opening": 0.10
      },
      "media": {"inlet": "water", "outlet": "water"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "quarter_command",
    "mode": "steady_state_design",
    "fixed_values": {
      "valve.inlet.p": {"value": 5.0, "unit": "bar"},
      "valve.inlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "valve.outlet.p": {"value": 4.0, "unit": "bar"},
      "valve.command.value": 0.25
    }
  }, {
    "id": "three_quarter_command",
    "mode": "steady_state_design",
    "fixed_values": {
      "valve.inlet.p": {"value": 5.0, "unit": "bar"},
      "valve.inlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "valve.outlet.p": {"value": 4.0, "unit": "bar"},
      "valve.command.value": 0.75
    }
  }]
})json");
    const auto properties =
        thermox::physics::make_default_property_package_registry();
    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto solve_case = [&](const std::string& case_id) {
        const auto graph = thermox::platform::compile_model_graph(
            document, registry, properties, case_id);
        const auto result = thermox::solve_newton(graph.problem);
        require(result.diagnostics.converged,
                result.diagnostics.message);
        return result.x.at(require_variable_index(
            graph.problem.variable_names, "valve.inlet.m_dot"));
    };
    const double lower_flow = solve_case("quarter_command");
    const double higher_flow = solve_case("three_quarter_command");
    require_near(
        higher_flow / lower_flow,
        (0.10 + 0.90 * 0.75) / (0.10 + 0.90 * 0.25),
        1.0e-10,
        "actuated valve command scales effective flow area");
}

void test_actuated_valve_composes_with_dynamic_control_lag() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "dynamic_actuated_valve",
    "media": [{
      "id": "water",
      "backend": "water_steam_if97",
      "substance": "Water"
    }],
    "components": [{
      "id": "lag",
      "kind": "control.first_order_lag.normalized",
      "parameters": {"gain": 1.0, "time_constant": 2.0}
    }, {
      "id": "valve",
      "kind": "valve.fluid.actuated_nonflashing_liquid",
      "parameters": {
        "full_open_diameter": {"value": 20.0, "unit": "mm"},
        "discharge_coefficient": 0.62
      },
      "media": {"inlet": "water", "outlet": "water"}
    }],
    "connections": [{
      "id": "actuator_to_valve",
      "kind": "signal_link",
      "contract_version": "thermox.connector.control/v1",
      "from": "lag.response",
      "to": "valve.command"
    }]
  },
  "cases": [{
    "id": "open",
    "mode": "dynamic_transient",
    "fixed_values": {
      "lag.command.value": 0.75,
      "valve.inlet.p": {"value": 5.0, "unit": "bar"},
      "valve.inlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "valve.outlet.p": {"value": 4.0, "unit": "bar"}
    },
    "initial_guesses": {"lag.response.value": 0.25}
  }]
})json");
    const auto graph =
        thermox::platform::compile_transient_model_graph(
            document,
            thermox::platform::make_default_component_registry(),
            thermox::physics::make_default_property_package_registry(),
            "open");
    const auto response = require_variable_index(
        graph.problem.variable_names, "lag.response.value");
    const auto initialized = thermox::make_consistent_initial_conditions(
        graph.problem, 0.0);
    require(initialized.diagnostics.converged,
            initialized.diagnostics.message);
    require_near(initialized.derivative.at(response), 0.25, 1.0e-10,
                 "actuator lag initializes command response rate");
    thermox::TimeIntegrationOptions options;
    options.end_time = 0.1;
    options.initial_step = 0.01;
    options.max_step = 0.02;
    const auto result = thermox::integrate_dae(graph.problem, options);
    require(result.diagnostics.success,
            result.diagnostics.message);
    require(result.trajectory.back().state.at(response) > 0.25,
            "actuator response moves toward commanded opening");
    require(
        result.trajectory.back().state.at(require_variable_index(
            graph.problem.variable_names, "valve.inlet.m_dot")) > 0.0,
        "dynamic valve solves positive liquid flow");
}

void test_return_bend_fixed_loss_uses_fluid_density() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "return_bend_loss",
    "media": [{
      "id": "air",
      "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "components": [{
      "id": "bend",
      "kind": "fitting.fluid.return_bend.fixed_loss_coefficient",
      "parameters": {
        "inner_diameter": {"value": 0.5, "unit": "m"},
        "loss_coefficient": 1.5
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
    const auto properties =
        thermox::physics::make_default_property_package_registry();
    const auto graph = thermox::platform::compile_model_graph(
        document,
        thermox::platform::make_default_component_registry(),
        properties, "design");
    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged,
            result.diagnostics.message);

    const auto air = properties.create("ideal_gas_mixture", "Air");
    const auto inlet = air->state_ph(2.0e5, 3.0e5);
    require(inlet.ok(), "ideal-gas inlet state must evaluate");
    const double area = std::numbers::pi * 0.5 * 0.5 / 4.0;
    const double expected_loss =
        1.5 * 2.0 * 2.0 /
        (2.0 * inlet.state.density_kg_m3 * area * area);
    require_near(
        result.x.at(require_variable_index(
            graph.problem.variable_names, "bend.outlet.m_dot")),
        2.0, 1.0e-10, "return bend conserves mass");
    require_near(
        result.x.at(require_variable_index(
            graph.problem.variable_names, "bend.outlet.h")),
        3.0e5, 1.0e-8,
        "adiabatic return bend preserves enthalpy");
    require_near(
        result.x.at(require_variable_index(
            graph.problem.variable_names, "bend.outlet.p")),
        2.0e5 - expected_loss, 1.0e-7,
        "return bend applies K rho v squared pressure loss");
}

void test_homogeneous_gravity_pipe_supports_reverse_flow_and_transient() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "bidirectional_gravity_pipe",
    "media": [{
      "id": "air",
      "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "components": [{
      "id": "pipe",
      "kind": "pipe.fluid.homogeneous_equilibrium_local_loss",
      "parameters": {
        "flow_diameter": {"value": 0.5, "unit": "m"},
        "loss_coefficient": 2.0,
        "elevation_change": {"value": -3.0, "unit": "m"}
      },
      "media": {"inlet": "air", "outlet": "air"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "reverse",
    "mode": "steady_state_design",
    "fixed_values": {
      "pipe.inlet.m_dot": {"value": -1.0, "unit": "kg/s"},
      "pipe.inlet.p": {"value": 1.0, "unit": "bar"},
      "pipe.inlet.h": {"value": 300.0, "unit": "kJ/kg"}
    }
  }, {
    "id": "reverse_dynamic",
    "mode": "dynamic_transient",
    "fixed_values": {
      "pipe.inlet.m_dot": {"value": -1.0, "unit": "kg/s"},
      "pipe.inlet.p": {"value": 1.0, "unit": "bar"},
      "pipe.inlet.h": {"value": 300.0, "unit": "kJ/kg"}
    }
  }]
})json");
    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto steady = thermox::platform::compile_model_graph(
        document, registry, "reverse");
    const auto solved = thermox::solve_newton(steady.problem);
    require(solved.diagnostics.converged,
            solved.diagnostics.message);
    require_near(
        solved.x.at(require_variable_index(
            steady.problem.variable_names,
            "pipe.outlet.m_dot")),
        -1.0, 1.0e-10,
        "gravity pipe conserves signed reverse flow");
    require(
        solved.x.at(require_variable_index(
            steady.problem.variable_names, "pipe.outlet.p")) >
            1.0e5,
        "signed friction and downward elevation produce a reverse-flow "
        "outlet pressure above the declared inlet");

    const auto transient =
        thermox::platform::compile_transient_model_graph(
            document, registry, "reverse_dynamic");
    const auto initialized =
        thermox::make_consistent_initial_conditions(
            transient.problem, 0.0);
    require(initialized.diagnostics.converged,
            "transient gravity-pipe initialization: " +
                initialized.diagnostics.message);
}

void test_constant_slip_two_phase_pipe_closes_void_fraction_pressure_balance() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "constant_slip_riser",
    "media": [{
      "id": "water",
      "backend": "water_steam_if97",
      "substance": "Water"
    }],
    "components": [{
      "id": "riser",
      "kind": "pipe.fluid.constant_slip_two_phase_local_loss",
      "parameters": {
        "flow_diameter": {"value": 0.1, "unit": "m"},
        "loss_coefficient": 2.0,
        "elevation_change": {"value": 5.0, "unit": "m"},
        "slip_ratio": 2.0
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
  }, {
    "id": "single_phase_rejected",
    "mode": "steady_state_design",
    "fixed_values": {
      "riser.inlet.m_dot": {"value": 0.2, "unit": "kg/s"},
      "riser.inlet.p": {"value": 1.0, "unit": "MPa"},
      "riser.inlet.h": {"value": 500.0, "unit": "kJ/kg"}
    }
  }]
})json");
    const auto properties =
        thermox::physics::make_default_property_package_registry();
    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto graph = thermox::platform::compile_model_graph(
        document, registry, properties, "steady");
    const auto solved = thermox::solve_newton(graph.problem);
    require(solved.diagnostics.converged,
            solved.diagnostics.message);
    const auto value = [&](const std::string& name) {
        return solved.x.at(require_variable_index(
            graph.problem.variable_names, name));
    };
    const double outlet_pressure = value("riser.outlet.p");
    const double mean_pressure = 0.5 * (1.0e6 + outlet_pressure);
    const auto water = properties.create("water_steam_if97", "Water");
    const auto state = water->state_ph(mean_pressure, 1.5e6);
    const auto saturation = water->saturation_p(mean_pressure);
    require(state.ok() && saturation.ok() &&
                state.state.phase == thermox::physics::Phase::two_phase,
            "constant-slip test mean state must be two phase");
    const double quality = state.state.vapor_quality;
    const double rho_l = saturation.liquid.density_kg_m3;
    const double rho_v = saturation.vapor.density_kg_m3;
    const double alpha = 1.0 /
        (1.0 + ((1.0 - quality) / quality) *
            (rho_v / rho_l) * 2.0);
    const double density = alpha * rho_v + (1.0 - alpha) * rho_l;
    const double area = std::numbers::pi * 0.1 * 0.1 / 4.0;
    const double expected_drop =
        2.0 * 0.2 * 0.2 / (2.0 * density * area * area) +
        density * 9.80665 * 5.0;
    require(alpha > 0.0 && alpha < 1.0,
            "constant-slip closure produces physical void fraction");
    require_near(1.0e6 - outlet_pressure, expected_drop, 1.0e-5,
                 "constant-slip riser pressure balance");
    require_near(value("riser.outlet.m_dot"), 0.2, 1.0e-12,
                 "constant-slip riser conserves mass");
    require_near(value("riser.outlet.h"), 1.5e6, 1.0e-8,
                 "constant-slip riser transports enthalpy");

    const auto transient =
        thermox::platform::compile_transient_model_graph(
            document, registry, properties, "dynamic");
    const auto initialized =
        thermox::make_consistent_initial_conditions(
            transient.problem, 0.0);
    require(initialized.diagnostics.converged,
            "transient constant-slip initialization: " +
                initialized.diagnostics.message);

    const auto incompatible =
        thermox::platform::compile_model_graph(
            document, registry, properties,
            "single_phase_rejected");
    const auto incompatible_result =
        thermox::solve_newton(incompatible.problem);
    require(!incompatible_result.diagnostics.converged,
            "constant-slip pipe must reject a single-phase mean state");
}

void test_darcy_weisbach_pipe_uses_transport_properties() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "single_phase_pipe",
    "media": [{
      "id": "water",
      "backend": "water_steam_if97",
      "substance": "Water"
    }],
    "components": [{
      "id": "pipe",
      "kind": "pipe.fluid.darcy_weisbach",
      "parameters": {
        "length": {"value": 40.0, "unit": "m"},
        "inner_diameter": {"value": 0.10, "unit": "m"},
        "roughness": {"value": 0.000045, "unit": "m"},
        "elevation_change": {"value": 3.0, "unit": "m"},
        "local_loss_coefficient": 1.2
      },
      "media": {"inlet": "water", "outlet": "water"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "pipe.inlet.m_dot": {"value": 1.5, "unit": "kg/s"},
      "pipe.inlet.p": {"value": 5.0, "unit": "bar"},
      "pipe.inlet.h": {"value": 500.0, "unit": "kJ/kg"}
    }
  }]
})json");
    const auto properties =
        thermox::physics::make_default_property_package_registry();
    const auto graph = thermox::platform::compile_model_graph(
        document,
        thermox::platform::make_default_component_registry(),
        properties, "design");
    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged,
            result.diagnostics.message);
    const auto value = [&](const std::string& name) {
        return result.x.at(require_variable_index(
            graph.problem.variable_names, name));
    };
    require_near(value("pipe.outlet.m_dot"), 1.5, 1.0e-10,
                 "pipe conserves mass");
    require_near(value("pipe.outlet.h"), 5.0e5, 1.0e-8,
                 "adiabatic pipe preserves enthalpy");

    const auto water = properties.create("water_steam_if97", "Water");
    const double outlet_pressure = value("pipe.outlet.p");
    const auto mean_state = water->state_ph(
        0.5 * (5.0e5 + outlet_pressure), 5.0e5);
    require(mean_state.ok(), "pipe mean water state must evaluate");
    const double diameter = 0.10;
    const double area = std::numbers::pi * diameter * diameter / 4.0;
    const double reynolds = 1.5 * diameter /
        (area * mean_state.state.viscosity_pa_s);
    const double haaland = 1.0 / std::pow(
        -1.8 * std::log10(
            std::pow(0.000045 / (3.7 * diameter), 1.11) +
            6.9 / reynolds),
        2.0);
    const double expected_drop =
        (haaland * 40.0 / diameter + 1.2) * 1.5 * 1.5 /
            (2.0 * mean_state.state.density_kg_m3 * area * area) +
        mean_state.state.density_kg_m3 * 9.80665 * 3.0;
    require_near(
        5.0e5 - outlet_pressure, expected_drop, 1.0e-5,
        "pipe applies friction, local, and elevation pressure loss");
}

void test_darcy_weisbach_pipe_exposes_ambient_heat_boundary() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "heat_losing_pipe",
    "media": [{
      "id": "water",
      "backend": "water_steam_if97",
      "substance": "Water"
    }],
    "components": [{
      "id": "pipe",
      "kind": "pipe.fluid.darcy_weisbach_heat_transfer",
      "parameters": {
        "length": {"value": 10.0, "unit": "m"},
        "inner_diameter": {"value": 0.08, "unit": "m"},
        "overall_thermal_conductance": {"value": 200.0, "unit": "W/K"}
      },
      "media": {"inlet": "water", "outlet": "water"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "pipe.inlet.m_dot": {"value": 2.0, "unit": "kg/s"},
      "pipe.inlet.p": {"value": 5.0, "unit": "bar"},
      "pipe.inlet.h": {"value": 600.0, "unit": "kJ/kg"},
      "pipe.ambient.T": {"value": 300.0, "unit": "K"}
    }
  }]
})json");
    const auto properties =
        thermox::physics::make_default_property_package_registry();
    const auto graph = thermox::platform::compile_model_graph(
        document,
        thermox::platform::make_default_component_registry(),
        properties, "design");
    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged,
            result.diagnostics.message);
    const auto value = [&](const std::string& name) {
        return result.x.at(require_variable_index(
            graph.problem.variable_names, name));
    };
    const double heat_loss = value("pipe.ambient.Q_dot");
    require(heat_loss > 0.0,
            "hot pipe should reject positive heat to ambient");
    require(value("pipe.outlet.h") < 6.0e5,
            "heat rejection should reduce outlet enthalpy");
    require_near(
        2.0 * (value("pipe.outlet.h") - 6.0e5) + heat_loss,
        0.0, 1.0e-6,
        "pipe fluid and ambient heat boundary close energy");

    const auto water = properties.create("water_steam_if97", "Water");
    const auto inlet = water->state_ph(5.0e5, 6.0e5);
    const auto outlet = water->state_ph(
        value("pipe.outlet.p"), value("pipe.outlet.h"));
    require(inlet.ok() && outlet.ok(),
            "pipe endpoint states must evaluate");
    require_near(
        heat_loss,
        200.0 * (0.5 * (inlet.state.temperature_k +
                         outlet.state.temperature_k) - 300.0),
        1.0e-4,
        "pipe heat loss follows the declared conductance law");
}

void test_equilibrium_flash_separator_closes_phase_split() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "equilibrium_flash",
    "media": [{
      "id": "water",
      "backend": "water_steam_if97",
      "substance": "Water"
    }],
    "components": [{
      "id": "separator",
      "kind": "separator.fluid.equilibrium_flash",
      "parameters": {"pressure_loss_fraction": 0.10},
      "media": {
        "inlet": "water",
        "vapor_outlet": "water",
        "liquid_outlet": "water"
      }
    }],
    "connections": []
  },
  "cases": [{
    "id": "two_phase",
    "mode": "steady_state_design",
    "fixed_values": {
      "separator.inlet.m_dot": {"value": 10.0, "unit": "kg/s"},
      "separator.inlet.p": {"value": 1.0, "unit": "MPa"},
      "separator.inlet.h": {"value": 1200.0, "unit": "kJ/kg"}
    }
  }, {
    "id": "subcooled",
    "mode": "steady_state_design",
    "fixed_values": {
      "separator.inlet.m_dot": {"value": 10.0, "unit": "kg/s"},
      "separator.inlet.p": {"value": 1.0, "unit": "MPa"},
      "separator.inlet.h": {"value": 500.0, "unit": "kJ/kg"}
    }
  }]
})json");
    const auto properties =
        thermox::physics::make_default_property_package_registry();
    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto graph = thermox::platform::compile_model_graph(
        document, registry, properties, "two_phase");
    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged,
            result.diagnostics.message);
    const auto value = [&](const std::string& name) {
        return result.x.at(require_variable_index(
            graph.problem.variable_names, name));
    };
    const auto water = properties.create("water_steam_if97", "Water");
    const auto saturation = water->saturation_p(9.0e5);
    require(saturation.ok(), "separator saturation state must evaluate");
    const double quality =
        (1.2e6 - saturation.liquid.enthalpy_j_kg) /
        (saturation.vapor.enthalpy_j_kg -
         saturation.liquid.enthalpy_j_kg);
    require(quality > 0.0 && quality < 1.0,
            "separator test inlet must lie inside saturation dome");
    require_near(value("separator.vapor_outlet.p"), 9.0e5, 1.0e-7,
                 "separator vapor pressure");
    require_near(value("separator.liquid_outlet.p"), 9.0e5, 1.0e-7,
                 "separator liquid pressure");
    require_near(
        value("separator.vapor_outlet.h"),
        saturation.vapor.enthalpy_j_kg, 1.0e-6,
        "separator saturated-vapor enthalpy");
    require_near(
        value("separator.liquid_outlet.h"),
        saturation.liquid.enthalpy_j_kg, 1.0e-6,
        "separator saturated-liquid enthalpy");
    require_near(value("separator.vapor_outlet.m_dot"),
                 10.0 * quality, 1.0e-9,
                 "separator vapor fraction follows the lever rule");
    require_near(value("separator.liquid_outlet.m_dot"),
                 10.0 * (1.0 - quality), 1.0e-9,
                 "separator liquid fraction follows the lever rule");

    const thermox::platform::GraphResultEvaluator evaluator(
        document, graph, properties);
    const auto graph_result = evaluator.evaluate(result.x);
    require_near(
        require_result_value(
            graph_result.system_balances,
            "net_boundary_mass_flow"),
        0.0, 1.0e-10,
        "separator system mass audit");
    require_near(
        require_result_value(
            graph_result.system_balances,
            "net_boundary_energy_flow"),
        0.0, 1.0e-5,
        "separator system energy audit");

    const auto subcooled_graph =
        thermox::platform::compile_model_graph(
            document, registry, properties, "subcooled");
    const auto subcooled_result =
        thermox::solve_newton(subcooled_graph.problem);
    require(!subcooled_result.diagnostics.converged,
            "equilibrium separator must reject a subcooled inlet");
    require(
        subcooled_result.diagnostics.message.find(
            "outside the liquid-vapor saturation dome") !=
            std::string::npos,
        "separator validity diagnostic must identify saturation dome: " +
            subcooled_result.diagnostics.message);
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

    const auto regulator_document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "material_pressure_regulator",
    "media": [],
    "materials": [{
      "id": "fuel", "backend": "test_thermochemistry",
      "mechanism": "fuel.yaml", "phase": "gas",
      "species": ["N2", "O2"]
    }],
    "components": [{
      "id": "regulator",
      "kind": "regulator.material.isenthalpic_network_pressure",
      "materials": {"inlet": "fuel", "outlet": "fuel"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "design", "mode": "steady_state_design",
    "fixed_values": {
      "regulator.inlet.p": {"value": 3.2, "unit": "MPa"},
      "regulator.outlet.p": {"value": 1.5, "unit": "MPa"},
      "regulator.inlet.h": {"value": 450.0, "unit": "kJ/kg"},
      "regulator.inlet.m_dot[N2]": {"value": 1.0, "unit": "kg/s"},
      "regulator.inlet.m_dot[O2]": {"value": 0.2, "unit": "kg/s"}
    }
  }]
})json");
    const auto regulator_graph =
        thermox::platform::compile_model_graph(
            regulator_document,
            thermox::platform::make_default_component_registry(),
            "design");
    const auto regulator_result =
        thermox::solve_newton(regulator_graph.problem);
    require(
        regulator_result.diagnostics.converged,
        regulator_result.diagnostics.message);
    const auto regulator_value = [&](const std::string& name) {
        return regulator_result.x.at(require_variable_index(
            regulator_graph.problem.variable_names, name));
    };
    require_near(
        regulator_value("regulator.outlet.p"), 1.5e6, 1.0e-7,
        "material regulator accepts downstream network pressure");
    require_near(
        regulator_value("regulator.outlet.h"), 450000.0, 1.0e-7,
        "material regulator preserves enthalpy across pressure reduction");
    require_near(
        regulator_value("regulator.outlet.m_dot[N2]"), 1.0, 1.0e-10,
        "material regulator conserves species flow");
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

    const auto controlled_document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "controlled_material_splitter",
    "media": [],
    "materials": [{
      "id": "gas", "backend": "unresolved_test_backend",
      "mechanism": "test.yaml", "phase": "gas",
      "species": ["N2", "O2"]
    }],
    "components": [{
      "id": "splitter",
      "kind": "junction.material.splitter.controlled_fraction",
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
      "splitter.inlet.m_dot[O2]": {"value": 2.0, "unit": "kg/s"},
      "splitter.fraction.value": 0.35
    }
  }]
})json");
    const auto controlled_graph =
        thermox::platform::compile_model_graph(
            controlled_document, registry, "design");
    const auto controlled =
        thermox::solve_newton(controlled_graph.problem);
    require(
        controlled.diagnostics.converged,
        controlled.diagnostics.message);
    const auto controlled_value = [&](const std::string& name) {
        return controlled.x.at(require_variable_index(
            controlled_graph.problem.variable_names, name));
    };
    require_near(
        controlled_value("splitter.outlet_a.m_dot[N2]"),
        2.8, 1.0e-10,
        "controlled material splitter applies its signal fraction");
    require_near(
        controlled_value("splitter.outlet_b.m_dot[O2]"),
        1.3, 1.0e-10,
        "controlled material splitter closes its remaining flow");

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

void test_map_driven_material_cross_bleed() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "map_driven_cross_bleed",
    "media": [],
    "materials": [{
      "id": "air", "backend": "test_backend",
      "mechanism": "test.yaml", "phase": "gas",
      "species": ["N2", "O2"]
    }],
    "components": [{
      "id": "valve",
      "kind": "junction.material.cross_bleed.performance_map",
      "parameters": {
        "reference_pressure": {"value": 100.0, "unit": "kPa"},
        "reference_temperature": {"value": 600.0, "unit": "K"}
      },
      "artifacts": {"flow_characteristic": "bleed-map"},
      "materials": {
        "donor_inlet": "air", "donor_outlet": "air",
        "receiver_inlet": "air", "receiver_outlet": "air"
      }
    }],
    "connections": []
  },
  "cases": [{
    "id": "part_open", "mode": "steady_state_off_design",
    "fixed_values": {
      "valve.donor_inlet.p": {"value": 200.0, "unit": "kPa"},
      "valve.donor_inlet.h": {"value": 600.0, "unit": "kJ/kg"},
      "valve.donor_inlet.m_dot[N2]": {"value": 8.0, "unit": "kg/s"},
      "valve.donor_inlet.m_dot[O2]": {"value": 2.0, "unit": "kg/s"},
      "valve.receiver_inlet.p": {"value": 100.0, "unit": "kPa"},
      "valve.receiver_inlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "valve.receiver_inlet.m_dot[N2]": {"value": 4.0, "unit": "kg/s"},
      "valve.receiver_inlet.m_dot[O2]": {"value": 1.0, "unit": "kg/s"},
      "valve.position.value": 0.5
    },
    "initial_guesses": {
      "valve.bleed_mass_flow": {"value": 2.0, "unit": "kg/s"},
      "valve.donor_outlet.p": {"value": 200.0, "unit": "kPa"},
      "valve.donor_outlet.h": {"value": 600.0, "unit": "kJ/kg"},
      "valve.donor_outlet.m_dot[N2]": {"value": 6.4, "unit": "kg/s"},
      "valve.donor_outlet.m_dot[O2]": {"value": 1.6, "unit": "kg/s"},
      "valve.receiver_outlet.p": {"value": 100.0, "unit": "kPa"},
      "valve.receiver_outlet.h": {"value": 386.0, "unit": "kJ/kg"},
      "valve.receiver_outlet.m_dot[N2]": {"value": 5.6, "unit": "kg/s"},
      "valve.receiver_outlet.m_dot[O2]": {"value": 1.4, "unit": "kg/s"}
    }
  }]
})json");
    thermox::platform::EngineeringArtifactRegistry artifacts;
    artifacts.register_artifact({
        "bleed-map",
        thermox::platform::performance_map_artifact_schema_v1,
        "test-bleed-map",
        std::string(64, 'c'),
        std::make_shared<const thermox::platform::PerformanceMap>(
            thermox::platform::MapVariable{
                "pressure_ratio", "dimensionless"},
            thermox::platform::MapVariable{
                "position", "dimensionless"},
            std::vector<thermox::platform::MapVariable>{
                {"corrected_mass_flow", "mass_flow"}},
            std::vector<thermox::platform::MapCurve>{
                {0.0, {{1.0, {0.0}}, {3.0, {0.0}}}},
                {1.0, {{1.0, {0.0}}, {3.0, {4.0}}}},
            }),
    });
    thermox::physics::ThermochemistryPackageRegistry chemistry;
    chemistry.register_backend(
        {"test_backend", "test-thermochemistry", "1.0.0",
         {thermox::physics::ThermochemistryCapability::state_ph}},
        [](std::string_view, std::string_view) {
            return std::make_shared<const TestThermochemistryPackage>();
        });
    const auto graph = thermox::platform::compile_model_graph(
        document, thermox::platform::make_default_component_registry(),
        thermox::physics::make_default_property_package_registry(),
        artifacts, chemistry, "part_open");
    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged, result.diagnostics.message);
    const auto value = [&](const std::string& name) {
        return result.x.at(require_variable_index(
            graph.problem.variable_names, name));
    };
    require_near(value("valve.bleed_mass_flow"), 2.0, 1.0e-8,
        "cross-bleed applies corrected mapped flow capacity");
    require_near(value("valve.donor_outlet.m_dot[N2]"), 6.4, 1.0e-8,
        "cross-bleed removes donor species proportionally");
    require_near(value("valve.receiver_outlet.m_dot[N2]"), 5.6, 1.0e-8,
        "cross-bleed routes donor species into receiver");
    require_near(value("valve.receiver_outlet.m_dot[O2]"), 1.4, 1.0e-8,
        "cross-bleed conserves receiver oxygen flow");
    require_near(value("valve.receiver_outlet.h"),
        2700000.0 / 7.0, 1.0e-5,
        "cross-bleed conserves receiver and bleed enthalpy flow");
}

void test_perfect_gas_mach_scaled_material_duct() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "mach_scaled_material_duct",
    "media": [],
    "materials": [{
      "id": "air", "backend": "test_backend",
      "mechanism": "test.yaml", "phase": "gas",
      "species": ["N2", "O2"]
    }],
    "components": [{
      "id": "duct",
      "kind": "transport.material.perfect_gas_mach_scaled_loss",
      "parameters": {
        "flow_area": 0.1,
        "design_mach": 0.45,
        "design_pressure_loss_fraction": 0.01,
        "heat_capacity_ratio": 1.4
      },
      "materials": {"inlet": "air", "outlet": "air"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "design", "mode": "steady_state_design",
    "fixed_values": {
      "duct.inlet.p": {"value": 101.325, "unit": "kPa"},
      "duct.inlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "duct.inlet.m_dot[N2]": {"value": 8.0, "unit": "kg/s"},
      "duct.inlet.m_dot[O2]": {"value": 2.0, "unit": "kg/s"}
    }
  }]
})json");
    thermox::physics::ThermochemistryPackageRegistry chemistry;
    chemistry.register_backend(
        {"test_backend", "test-thermochemistry", "1.0.0",
         {thermox::physics::ThermochemistryCapability::state_ph}},
        [](std::string_view, std::string_view) {
            return std::make_shared<
                const TestThermochemistryPackage>();
        });
    const auto graph = thermox::platform::compile_model_graph(
        document, thermox::platform::make_default_component_registry(),
        thermox::physics::make_default_property_package_registry(),
        thermox::platform::EngineeringArtifactRegistry{},
        chemistry, "design");
    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged,
            result.diagnostics.message);
    const auto value = [&](const std::string& name) {
        return result.x.at(require_variable_index(
            graph.problem.variable_names, name));
    };
    const double mach = value("duct.mach");
    require(mach > 0.2 && mach < 0.3,
            "material duct must solve its subsonic local Mach number");
    const double expected_pressure = 101325.0 *
        (1.0 - 0.01 * mach * mach / (0.45 * 0.45));
    require_near(
        value("duct.outlet.p"), expected_pressure, 1.0e-7,
        "material duct applies Mach-scaled total-pressure loss");
    require_near(
        value("duct.outlet.h"), 300000.0, 1.0e-8,
        "material duct preserves total enthalpy");
    require_near(
        value("duct.outlet.m_dot[N2]"), 8.0, 1.0e-10,
        "material duct conserves species flow");
}

void test_perfect_gas_convergent_material_nozzle() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "convergent_material_nozzle",
    "media": [],
    "materials": [{
      "id": "air", "backend": "test_backend",
      "mechanism": "test.yaml", "phase": "gas",
      "species": ["N2", "O2"]
    }],
    "components": [{
      "id": "source", "kind": "source.material.fixed_composition",
      "parameters": {"mass_fraction[N2]": 0.8, "mass_fraction[O2]": 0.2},
      "materials": {"outlet": "air"}
    }, {
      "id": "nozzle",
      "kind": "terminal.material.convergent_nozzle",
      "parameters": {
        "reference_throat_area": 0.01,
        "reference_back_pressure": {"value": 100.0, "unit": "kPa"},
        "discharge_coefficient": 1.0,
        "velocity_coefficient": 0.99
      },
      "materials": {"inlet": "air"}
    }],
    "connections": [{
      "id": "source_nozzle", "from": "source.outlet",
      "to": "nozzle.inlet", "kind": "material_link"
    }]
  },
  "cases": [{
    "id": "choked", "mode": "steady_state_design",
    "fixed_values": {
      "source.outlet.p": {"value": 300.0, "unit": "kPa"},
      "source.outlet.T": {"value": 300.0, "unit": "K"},
      "nozzle.area_ratio.value": 1.0,
      "nozzle.back_pressure_ratio.value": 1.0
    },
    "initial_guesses": {
      "source.outlet.m_dot[N2]": {"value": 5.0, "unit": "kg/s"},
      "source.outlet.m_dot[O2]": {"value": 1.25, "unit": "kg/s"},
      "nozzle.inlet.p": {"value": 300.0, "unit": "kPa"},
      "nozzle.inlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "nozzle.inlet.m_dot[N2]": {"value": 5.0, "unit": "kg/s"},
      "nozzle.inlet.m_dot[O2]": {"value": 1.25, "unit": "kg/s"},
      "nozzle.mach": 1.0,
      "nozzle.thrust.F": 2000.0
    }
  }, {
    "id": "choked_transient", "mode": "dynamic_transient",
    "fixed_values": {
      "source.outlet.p": {"value": 300.0, "unit": "kPa"},
      "source.outlet.T": {"value": 300.0, "unit": "K"},
      "nozzle.area_ratio.value": 1.0,
      "nozzle.back_pressure_ratio.value": 1.0
    },
    "initial_guesses": {
      "source.outlet.m_dot[N2]": {"value": 5.0, "unit": "kg/s"},
      "source.outlet.m_dot[O2]": {"value": 1.25, "unit": "kg/s"},
      "nozzle.inlet.p": {"value": 300.0, "unit": "kPa"},
      "nozzle.inlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "nozzle.inlet.m_dot[N2]": {"value": 5.0, "unit": "kg/s"},
      "nozzle.inlet.m_dot[O2]": {"value": 1.25, "unit": "kg/s"},
      "nozzle.mach": 1.0,
      "nozzle.thrust.F": 2000.0
    }
  }]
})json");
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
        document, thermox::platform::make_default_component_registry(),
        thermox::physics::make_default_property_package_registry(),
        thermox::platform::EngineeringArtifactRegistry{},
        chemistry, "choked");
    const auto result = thermox::solve_newton(graph.problem);
    require(result.diagnostics.converged,
            result.diagnostics.message);
    const auto value = [&](const std::string& name) {
        return result.x.at(require_variable_index(
            graph.problem.variable_names, name));
    };
    require_near(value("nozzle.mach"), 1.0, 1.0e-10,
                 "convergent nozzle must choke at sufficient pressure ratio");
    require(value("nozzle.thrust.F") > 1000.0,
            "convergent nozzle must publish positive gross thrust");
    require_near(
        value("source.outlet.m_dot[N2]") /
            value("source.outlet.m_dot[O2]"),
        4.0, 1.0e-10,
        "nozzle capacity solve must preserve source composition");
    const auto transient_graph =
        thermox::platform::compile_transient_model_graph(
            document,
            thermox::platform::make_default_component_registry(),
            thermox::physics::make_default_property_package_registry(),
            thermox::platform::EngineeringArtifactRegistry{},
            chemistry, "choked_transient");
    const auto initialized =
        thermox::make_consistent_initial_conditions(
            transient_graph.problem, 0.0);
    require(
        initialized.diagnostics.converged,
        initialized.diagnostics.message);
    require_near(
        initialized.state.at(require_variable_index(
            transient_graph.problem.variable_names,
            "nozzle.mach")),
        1.0, 1.0e-10,
        "material temperature boundary and nozzle algebraic closure "
        "must lift into a transient DAE");
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
                thermox::platform::EngineeringArtifactRegistry{},
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
                thermox::platform::EngineeringArtifactRegistry{},
                chemistry);
        },
        "under-specified");
}

void test_material_boundary_temperature_specification() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "material_temperature_boundary",
    "media": [],
    "materials": [{
      "id": "air",
      "backend": "test_backend",
      "mechanism": "test.yaml",
      "phase": "gas",
      "species": ["N2", "O2"]
    }],
    "components": [{
      "id": "source",
      "kind": "source.material.fixed_composition",
      "parameters": {
        "mass_fraction[N2]": 0.8,
        "mass_fraction[O2]": 0.2
      },
      "materials": {"outlet": "air"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "ambient",
    "mode": "steady_state_design",
    "fixed_values": {
      "source.outlet.m_dot[N2]": {
        "value": 8.0,
        "unit": "kg/s"
      },
      "source.outlet.p": {
        "value": 101.325,
        "unit": "kPa"
      },
      "source.outlet.T": {
        "value": 300.0,
        "unit": "K"
      }
    }
  }]
})json");
    thermox::physics::ThermochemistryPackageRegistry chemistry;
    chemistry.register_backend(
        {"test_backend", "test-thermochemistry", "1.0.0",
         {thermox::physics::ThermochemistryCapability::state_ph}},
        [](std::string_view, std::string_view) {
            return std::make_shared<
                const TestThermochemistryPackage>();
        });
    const auto graph =
        thermox::platform::compile_model_graph(
            document,
            thermox::platform::make_default_component_registry(),
            thermox::physics::
                make_default_property_package_registry(),
            thermox::platform::EngineeringArtifactRegistry{},
            chemistry, "ambient");
    const auto result = thermox::solve_newton(graph.problem);
    require(
        result.diagnostics.converged,
        result.diagnostics.message);
    const auto value = [&](const std::string& name) {
        return result.x.at(require_variable_index(
            graph.problem.variable_names, name));
    };
    require_near(
        value("source.outlet.h"), 300000.0, 1.0e-7,
        "material temperature boundary closes PH enthalpy");
    require_near(
        value("source.outlet.m_dot[O2]"), 2.0, 1.0e-10,
        "material temperature boundary preserves composition");
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
              equilibrium_hp,
          thermox::physics::ThermochemistryCapability::
              lower_heating_value}},
        [](std::string_view, std::string_view) {
            return std::make_shared<
                const TestThermochemistryPackage>();
        });
    const auto graph = thermox::platform::compile_model_graph(
        document,
        thermox::platform::make_default_component_registry(),
        thermox::physics::
            make_default_property_package_registry(),
        thermox::platform::EngineeringArtifactRegistry{},
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
    require_near(
        require_result_value(
            graph_result.system_balances,
            "net_boundary_mass_flow"),
        0.0, 1.0e-9,
        "material combustor boundary mass flow closes by species");
    require_near(
        require_result_value(
            graph_result.system_balances,
            "net_boundary_energy_flow"),
        0.0, 1.0e-4,
        "material combustor boundary enthalpy flow closes");
    const auto& combustor = require_component_result(
        graph_result, "combustor");
    require_near(
        require_result_value(
            combustor.metrics, "net_mass_flow"),
        0.0, 1.0e-9,
        "combustor component metric exposes mass closure");
    require_near(
        require_result_value(
            combustor.metrics, "net_energy_flow"),
        0.0, 1.0e-4,
        "combustor component metric exposes energy closure");

    const auto efficient_document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "equilibrium_combustor_efficiency",
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
      "kind": "combustor.material.equilibrium_heat_release_efficiency",
      "parameters": {
        "pressure_ratio": 0.9,
        "combustion_efficiency": 0.9,
        "fuel_lower_heating_value": {
          "value": 10.0,
          "unit": "MJ/kg"
        }
      },
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
    const auto efficient_graph =
        thermox::platform::compile_model_graph(
            efficient_document,
            thermox::platform::make_default_component_registry(),
            thermox::physics::
                make_default_property_package_registry(),
            thermox::platform::EngineeringArtifactRegistry{},
            chemistry, "design");
    const auto efficient_result =
        thermox::solve_newton(efficient_graph.problem);
    require(
        efficient_result.diagnostics.converged,
        efficient_result.diagnostics.message);
    const auto efficient_value = [&](const std::string& name) {
        return efficient_result.x.at(require_variable_index(
            efficient_graph.problem.variable_names, name));
    };
    require_near(
        efficient_value("combustor.outlet.h"),
        2000000.0 / 12.0, 1.0e-5,
        "combustion efficiency removes declared unreleased fuel heat");
    const thermox::platform::GraphResultEvaluator efficient_evaluator(
        efficient_document, efficient_graph,
        thermox::physics::make_default_property_package_registry(),
        chemistry);
    const auto efficient_graph_result =
        efficient_evaluator.evaluate(efficient_result.x);
    require_near(
        require_result_value(
            require_component_result(
                efficient_graph_result, "combustor").metrics,
            "net_energy_flow"),
        2000000.0, 1.0e-4,
        "combustor attributes unreleased fuel heat as an energy loss");

    auto difficult = document;
    difficult.cases.front().initial_guesses = {
        {"combustor.air_inlet.m_dot[N2]",
         {-8.0, "kg/s", "mass_flow"}},
        {"combustor.air_inlet.m_dot[O2]",
         {-2.0, "kg/s", "mass_flow"}},
        {"combustor.fuel_inlet.m_dot[N2]",
         {-1.0, "kg/s", "mass_flow"}},
        {"combustor.fuel_inlet.m_dot[O2]",
         {-1.0, "kg/s", "mass_flow"}},
        {"combustor.outlet.p",
         {-100000.0, "Pa", "pressure"}},
        {"combustor.outlet.h",
         {-100000.0, "J/kg", "specific_enthalpy"}},
        {"combustor.outlet.m_dot[N2]",
         {-1.0, "kg/s", "mass_flow"}},
        {"combustor.outlet.m_dot[O2]",
         {-1.0, "kg/s", "mass_flow"}},
    };
    const auto difficult_graph =
        thermox::platform::compile_model_graph(
            difficult,
            thermox::platform::make_default_component_registry(),
            thermox::physics::
                make_default_property_package_registry(),
            thermox::platform::EngineeringArtifactRegistry{},
            chemistry, "design");
    const auto difficult_direct =
        thermox::solve_newton(difficult_graph.problem);
    require(
        !difficult_direct.diagnostics.converged &&
            difficult_direct.diagnostics.message.find(
                "species mass flows must be nonnegative") !=
                std::string::npos,
        "direct equilibrium solve must reject invalid reactant "
        "guesses: " +
            difficult_direct.diagnostics.message);

    thermox::ContinuationOptions continuation_options;
    continuation_options.minimum_step = 1.0 / 1024.0;
    const auto difficult_continued =
        thermox::solve_continuation(
            difficult_graph.problem, {},
            continuation_options);
    require(
        difficult_continued.continuation.converged,
        difficult_continued.continuation.message);
    require(
        difficult_continued.continuation.used_informed_path,
        "equilibrium combustor reports its informed chemistry path");
    const auto continued_value =
        [&](const std::string& name) {
            return difficult_continued.x.at(
                require_variable_index(
                    difficult_graph.problem.variable_names,
                    name));
        };
    require_near(
        continued_value("combustor.outlet.p"),
        value("combustor.outlet.p"), 1.0e-6,
        "combustor continuation preserves target pressure");
    require_near(
        continued_value("combustor.outlet.h"),
        value("combustor.outlet.h"), 1.0e-4,
        "combustor continuation preserves target enthalpy");
    require_near(
        continued_value("combustor.outlet.m_dot[N2]"),
        value("combustor.outlet.m_dot[N2]"), 1.0e-8,
        "combustor continuation preserves target composition");
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
            thermox::platform::EngineeringArtifactRegistry{},
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
    // The default dimensionless Newton tolerance and the component's
    // 1 MJ/kg residual scale permit a 1e-3 J/kg equation residual.
    // Keep this physics assertion tighter than engineering precision while
    // avoiding dependence on the finite-difference iteration path.
    constexpr double enthalpy_solver_tolerance = 2.0e-3;
    constexpr double power_solver_tolerance = 2.0e-2;
    require_near(
        compressor_value("machine.outlet.h"),
        compressor_outlet_h, enthalpy_solver_tolerance,
        "material compressor applies isentropic efficiency");
    require_near(
        compressor_value("machine.shaft.W_dot"),
        10.0 * (compressor_outlet_h - 300000.0),
        power_solver_tolerance,
        "material compressor closes shaft input power");

    const auto iso_compressor_graph = compile_and_solve(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "iso_equivalent_cooling_compressor",
    "media": [],
    "materials": [{
      "id": "gas", "backend": "test_backend",
      "mechanism": "test.yaml", "phase": "gas",
      "species": ["N2", "O2"]
    }],
    "components": [{
      "id": "machine",
      "kind": "compressor.material.iso2314_equivalent_cooling",
      "parameters": {
        "pressure_ratio": 10.0,
        "eta_is": 0.8,
        "relative_equivalent_flow_difference_md": 0.25
      },
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
    const auto iso_compressor = thermox::solve_newton(
        iso_compressor_graph.problem);
    require(iso_compressor.diagnostics.converged,
            iso_compressor.diagnostics.message);
    const auto iso_compressor_value = [&](const std::string& name) {
        return iso_compressor.x.at(require_variable_index(
            iso_compressor_graph.problem.variable_names, name));
    };
    const double equivalent_outlet_h = 300000.0 +
        (compressor_outlet_h - 300000.0) / 1.25;
    require_near(
        iso_compressor_value("machine.outlet.h"),
        equivalent_outlet_h, enthalpy_solver_tolerance,
        "ISO compressor applies the equivalent-flow work relation");
    require_near(
        iso_compressor_value("machine.shaft.W_dot"),
        10.0 * (equivalent_outlet_h - 300000.0),
        power_solver_tolerance,
        "ISO compressor closes equivalent compressor power");
    require_near(
        iso_compressor_value("machine.outlet.m_dot[N2]"),
        8.0, 1.0e-10,
        "ISO compressor preserves nitrogen flow");
    require_near(
        iso_compressor_value("machine.outlet.m_dot[O2]"),
        2.0, 1.0e-10,
        "ISO compressor preserves oxygen flow");

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
        turbine_outlet_h, enthalpy_solver_tolerance,
        "material turbine applies isentropic efficiency");
    require_near(
        turbine_value("machine.shaft.W_dot"),
        10.0 * (1000000.0 - turbine_outlet_h),
        power_solver_tolerance,
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
        "reference_temperature": {"value": 300.0, "unit": "K"},
        "flow_capacity_scale": 1.25,
        "pressure_ratio_scale": 0.5,
        "efficiency_scale": 0.9
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
    thermox::platform::EngineeringArtifactRegistry maps;
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
    maps.register_artifact({
        "material-latent-coordinate-map",
        thermox::platform::performance_map_artifact_schema_v1,
        "test-material-latent-coordinate-map",
        std::string(64, 'b'),
        std::make_shared<
            const thermox::platform::PerformanceMap>(
            thermox::platform::MapVariable{
                "map_coordinate", "dimensionless"},
            thermox::platform::MapVariable{
                "corrected_speed", "angular_speed"},
            std::vector<thermox::platform::MapVariable>{
                {"corrected_mass_flow", "mass_flow"},
                {"pressure_ratio", "dimensionless"},
                {"isentropic_efficiency", "dimensionless"},
            },
            std::vector<thermox::platform::MapCurve>{
                {250.0,
                 {{1.0, {8.0, 2.2, 0.78}},
                  {3.0, {12.0, 1.8, 0.82}}}},
                {350.0,
                 {{1.0, {8.0, 2.2, 0.78}},
                  {3.0, {12.0, 1.8, 0.82}}}},
            }),
    });
    const auto material_geometry_layer = [](
        double pressure_ratio, double efficiency) {
        return std::make_shared<
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
                 {{8.0, {pressure_ratio, efficiency}},
                  {12.0, {pressure_ratio, efficiency}}}},
                {350.0,
                 {{8.0, {pressure_ratio, efficiency}},
                  {12.0, {pressure_ratio, efficiency}}}},
            });
    };
    maps.register_artifact({
        "material-variable-geometry-map",
        thermox::platform::performance_map_artifact_schema_v2,
        "test-material-variable-geometry-map",
        std::string(64, '8'),
        nullptr,
        std::make_shared<const
            thermox::platform::ConditionedPerformanceMap>(
            thermox::platform::MapVariable{
                "geometry_setting", "angle"},
            std::vector<thermox::platform::ConditionedMapLayer>{
                {60.0 * std::acos(-1.0) / 180.0,
                 material_geometry_layer(1.5, 0.75)},
                {80.0 * std::acos(-1.0) / 180.0,
                 material_geometry_layer(2.5, 0.85)},
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
    constexpr double corrected_compressor_pressure_ratio = 1.5;
    constexpr double corrected_compressor_efficiency = 0.72;
    const double isentropic_temperature =
        300.0 *
        std::exp(
            287.0 *
            std::log(corrected_compressor_pressure_ratio) /
            1000.0);
    const double expected_enthalpy =
        300000.0 +
        (1000.0 * isentropic_temperature - 300000.0) /
            corrected_compressor_efficiency;
    require_near(
        value("compressor.outlet.p"),
        corrected_compressor_pressure_ratio * 101325.0,
        1.0e-6,
        "material map applies flow and pressure-ratio corrections");
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

    auto latent_document = document;
    auto& latent_machine = latent_document.components.front();
    latent_machine.kind =
        "compressor.material.coordinate_map";
    latent_machine.artifact_bindings["performance_map"] =
        "material-latent-coordinate-map";
    latent_machine.parameters["flow_capacity_scale"].value_si = 1.0;
    latent_machine.parameters["pressure_ratio_scale"].value_si = 1.0;
    latent_machine.parameters["efficiency_scale"].value_si = 1.0;
    latent_document.cases.front().initial_guesses[
        "compressor.map_coordinate.value"] = {
            2.0, "1", "dimensionless"};
    const auto latent_graph =
        thermox::platform::compile_model_graph(
            latent_document,
            thermox::platform::make_default_component_registry(),
            thermox::physics::
                make_default_property_package_registry(),
            maps, chemistry, "off_design");
    const auto latent_result =
        thermox::solve_newton(latent_graph.problem);
    require(
        latent_result.diagnostics.converged,
        latent_result.diagnostics.message);
    require_near(
        latent_result.x.at(require_variable_index(
            latent_graph.problem.variable_names,
            "compressor.map_coordinate.value")),
        2.0, 1.0e-10,
        "latent compressor coordinate must solve corrected-flow "
        "closure without inverting the map");
    require_near(
        latent_result.x.at(require_variable_index(
            latent_graph.problem.variable_names,
            "compressor.outlet.p")),
        2.0 * 101325.0, 1.0e-6,
        "latent compressor map must interpolate pressure ratio at "
        "the solved coordinate");

    auto geometry_document = document;
    auto& geometry_machine =
        geometry_document.components.front();
    geometry_machine.kind =
        "compressor.material.variable_geometry_map";
    geometry_machine.artifact_bindings["performance_map"] =
        "material-variable-geometry-map";
    geometry_machine.parameters["geometry_setting"] = {
        60.0 * std::acos(-1.0) / 180.0, "rad", "angle"};
    geometry_document.cases.front().parameter_overrides = {
        {"components.compressor.parameters.geometry_setting",
         {70.0 * std::acos(-1.0) / 180.0, "rad", "angle"}},
    };
    const auto geometry_graph =
        thermox::platform::compile_model_graph(
            geometry_document,
            thermox::platform::make_default_component_registry(),
            thermox::physics::
                make_default_property_package_registry(),
            maps, chemistry, "off_design");
    const auto geometry_result =
        thermox::solve_newton(geometry_graph.problem);
    require(geometry_result.diagnostics.converged,
            geometry_result.diagnostics.message);
    require_near(
        geometry_result.x.at(require_variable_index(
            geometry_graph.problem.variable_names,
            "compressor.outlet.p")),
        1.5 * 101325.0, 1.0e-6,
        "material variable-geometry map must apply corrections "
        "after interpolating the case-selected pressure ratio");

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
    constexpr double corrected_turbine_pressure_ratio = 3.0;
    constexpr double corrected_turbine_efficiency = 0.81;
    const double turbine_isentropic_temperature =
        1000.0 *
        std::exp(
            287.0 *
            std::log(1.0 / corrected_turbine_pressure_ratio) /
            1000.0);
    const double expected_turbine_enthalpy =
        1.0e6 +
        corrected_turbine_efficiency *
            (1000.0 * turbine_isentropic_temperature - 1.0e6);
    require_near(
        turbine_value("turbine.outlet.p"),
        1.0e6 / corrected_turbine_pressure_ratio,
        1.0e-6,
        "material turbine map applies pressure-ratio correction");
    require_near(
        turbine_value("turbine.outlet.h"),
        expected_turbine_enthalpy, 1.0e-5,
        "material turbine map applies isentropic efficiency");
    require_near(
        turbine_value("turbine.shaft.W_dot"),
        10.0 * (1.0e6 - expected_turbine_enthalpy),
        1.0e-4, "material turbine map closes shaft power");
}

void test_fixed_composition_source_allows_map_solved_flow() {
    auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "composition_controlled_map_flow",
    "media": [],
    "materials": [{
      "id": "gas", "backend": "test_backend",
      "mechanism": "test.yaml", "phase": "gas",
      "species": ["N2", "O2"]
    }],
    "components": [
      {
        "id": "source",
        "kind": "source.material.fixed_composition",
        "parameters": {
          "mass_fraction[N2]": 0.8,
          "mass_fraction[O2]": 0.2
        },
        "materials": {"outlet": "gas"}
      },
      {
        "id": "compressor",
        "kind": "compressor.material.performance_map",
        "artifacts": {"performance_map": "flow-solving-map"},
        "parameters": {
          "reference_pressure": {"value": 101.325, "unit": "kPa"},
          "reference_temperature": {"value": 300.0, "unit": "K"}
        },
        "materials": {"inlet": "gas", "outlet": "gas"}
      },
      {
        "id": "sink",
        "kind": "sink.material.boundary",
        "materials": {"inlet": "gas"}
      }
    ],
    "connections": [
      {
        "id": "source_to_compressor",
        "from": "source.outlet",
        "to": "compressor.inlet",
        "kind": "material_link"
      },
      {
        "id": "compressor_to_sink",
        "from": "compressor.outlet",
        "to": "sink.inlet",
        "kind": "material_link"
      }
    ]
  },
  "cases": [{
    "id": "operating_point",
    "mode": "steady_state_off_design",
    "fixed_values": {
      "source.outlet.p": {"value": 101.325, "unit": "kPa"},
      "source.outlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "compressor.shaft.omega": 300.0,
      "sink.inlet.p": {"value": 202.65, "unit": "kPa"}
    },
    "initial_guesses": {
      "source.outlet.m_dot[N2]": {"value": 8.0, "unit": "kg/s"},
      "source.outlet.m_dot[O2]": {"value": 2.0, "unit": "kg/s"},
      "compressor.inlet.p": {"value": 101.325, "unit": "kPa"},
      "compressor.inlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "compressor.inlet.m_dot[N2]": {"value": 8.0, "unit": "kg/s"},
      "compressor.inlet.m_dot[O2]": {"value": 2.0, "unit": "kg/s"},
      "compressor.outlet.p": {"value": 202.65, "unit": "kPa"},
      "compressor.outlet.m_dot[N2]": {"value": 8.0, "unit": "kg/s"},
      "compressor.outlet.m_dot[O2]": {"value": 2.0, "unit": "kg/s"},
      "compressor.outlet.h": {"value": 370.0, "unit": "kJ/kg"},
      "compressor.shaft.W_dot": {"value": 0.7, "unit": "MW"},
      "sink.inlet.h": {"value": 370.0, "unit": "kJ/kg"},
      "sink.inlet.m_dot[N2]": {"value": 8.0, "unit": "kg/s"},
      "sink.inlet.m_dot[O2]": {"value": 2.0, "unit": "kg/s"}
    }
  }]
})json");

    thermox::platform::EngineeringArtifactRegistry maps;
    maps.register_artifact({
        "flow-solving-map",
        thermox::platform::performance_map_artifact_schema_v1,
        "synthetic-sloped-flow-map",
        std::string(64, '7'),
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
                 {{8.0, {1.6, 0.8}},
                  {12.0, {2.4, 0.8}}}},
                {350.0,
                 {{8.0, {1.6, 0.8}},
                  {12.0, {2.4, 0.8}}}},
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
    const auto solve = [&](const auto& selected_document) {
        const auto graph =
            thermox::platform::compile_model_graph(
                selected_document,
                thermox::platform::
                    make_default_component_registry(),
                thermox::physics::
                    make_default_property_package_registry(),
                maps, chemistry, "operating_point");
        require(
            thermox::analyze_problem_structure(
                graph.problem).valid_for_newton(),
            "composition-controlled source graph must be square");
        const auto result =
            thermox::solve_newton(graph.problem);
        require(result.diagnostics.converged,
                result.diagnostics.message);
        return std::pair{graph, result};
    };

    const auto [graph, result] = solve(document);
    const auto flow = [&](const std::string& species) {
        return result.x.at(require_variable_index(
            graph.problem.variable_names,
            "source.outlet.m_dot[" + species + "]"));
    };
    require_near(
        flow("N2"), 8.0, 1.0e-8,
        "map pressure closure must solve nitrogen flow");
    require_near(
        flow("O2"), 2.0, 1.0e-8,
        "map pressure closure must preserve source composition");

    document.cases.front().parameter_overrides = {
        {"components.source.parameters.mass_fraction[N2]",
         {0.75, "dimensionless", "dimensionless"}},
        {"components.source.parameters.mass_fraction[O2]",
         {0.25, "dimensionless", "dimensionless"}},
    };
    const auto [override_graph, override_result] =
        solve(document);
    require_near(
        override_result.x.at(require_variable_index(
            override_graph.problem.variable_names,
            "source.outlet.m_dot[N2]")),
        7.5, 1.0e-8,
        "case override must resolve a species-keyed parameter");
    require_near(
        override_result.x.at(require_variable_index(
            override_graph.problem.variable_names,
            "source.outlet.m_dot[O2]")),
        2.5, 1.0e-8,
        "case composition override must retain map-solved total flow");

    auto sparse = document;
    sparse.components.front().parameters.erase(
        "mass_fraction[O2]");
    sparse.components.front()
        .parameters.at("mass_fraction[N2]")
        .value_si = 1.0;
    sparse.cases.front().parameter_overrides.clear();
    const auto [sparse_graph, sparse_result] = solve(sparse);
    require_near(
        sparse_result.x.at(require_variable_index(
            sparse_graph.problem.variable_names,
            "source.outlet.m_dot[O2]")),
        0.0, 1.0e-8,
        "omitted source species defaults to zero fraction");

    auto invalid_sum = document;
    invalid_sum.components.front().parameters.erase(
        "mass_fraction[O2]");
    invalid_sum.cases.front().parameter_overrides.clear();
    require_throws(
        [&]() { (void)solve(invalid_sum); },
        "mass fractions must sum to one");

    auto unknown_species = document;
    unknown_species.components.front().parameters[
        "mass_fraction[CO2]"] = {
        0.0, "dimensionless", "dimensionless"};
    require_throws(
        [&]() { (void)solve(unknown_species); },
        "outside its material basis");
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

    const auto continued =
        thermox::solve_continuation(graph.problem);
    require(
        continued.continuation.converged,
        continued.continuation.message);
    require(
        continued.continuation.used_informed_path,
        "fixed-duty exchanger exposes an informed duty path");
    require_near(
        continued.x.at(require_variable_index(
            graph.problem.variable_names, "hx.hot_out.h")),
        value("hx.hot_out.h"), 1.0e-7,
        "fixed-duty continuation preserves the target hot state");
    require_near(
        continued.x.at(require_variable_index(
            graph.problem.variable_names, "hx.cold_out.h")),
        value("hx.cold_out.h"), 1.0e-7,
        "fixed-duty continuation preserves the target cold state");
}

void test_material_fluid_heat_exchangers() {
    const auto chemistry_registry = [] {
        thermox::physics::ThermochemistryPackageRegistry registry;
        registry.register_backend(
            {"test_backend", "test-thermochemistry", "1.0.0",
             {thermox::physics::ThermochemistryCapability::state_ph}},
            [](std::string_view, std::string_view) {
                return std::make_shared<
                    const TestThermochemistryPackage>();
            });
        return registry;
    };
    const auto compile = [&](const std::string& text) {
        return thermox::platform::compile_model_graph(
            thermox::platform::parse_model_document_text(text),
            thermox::platform::make_default_component_registry(),
            thermox::physics::make_default_property_package_registry(),
            thermox::platform::EngineeringArtifactRegistry{},
            chemistry_registry(), "design");
    };
    const auto fixed_graph = compile(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "material_fluid_fixed_duty",
    "media": [{
      "id": "cold_air", "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "materials": [{
      "id": "exhaust", "backend": "test_backend",
      "mechanism": "test.yaml", "phase": "gas",
      "species": ["N2", "O2"]
    }],
    "components": [{
      "id": "hx",
      "kind": "heat_exchanger.material_fluid.fixed_duty",
      "parameters": {
        "heat_duty": {"value": 1.0, "unit": "MW"},
        "hot_pressure_loss_fraction": 0.05,
        "cold_pressure_loss_fraction": 0.02
      },
      "materials": {"hot_in": "exhaust", "hot_out": "exhaust"},
      "media": {"cold_in": "cold_air", "cold_out": "cold_air"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "design", "mode": "steady_state_design",
    "fixed_values": {
      "hx.hot_in.m_dot[N2]": {"value": 8.0, "unit": "kg/s"},
      "hx.hot_in.m_dot[O2]": {"value": 2.0, "unit": "kg/s"},
      "hx.hot_in.p": {"value": 2.0, "unit": "bar"},
      "hx.hot_in.h": {"value": 600.0, "unit": "kJ/kg"},
      "hx.cold_in.m_dot": {"value": 20.0, "unit": "kg/s"},
      "hx.cold_in.p": {"value": 1.0, "unit": "bar"},
      "hx.cold_in.h": {"value": 300.0, "unit": "kJ/kg"}
    }
  }]
})json");
    const auto fixed = thermox::solve_newton(fixed_graph.problem);
    require(fixed.diagnostics.converged, fixed.diagnostics.message);
    const auto fixed_value = [&](const std::string& name) {
        return fixed.x.at(require_variable_index(
            fixed_graph.problem.variable_names, name));
    };
    require_near(fixed_value("hx.hot_out.m_dot[N2]"), 8.0,
                 1.0e-9, "fixed-duty exchanger preserves nitrogen");
    require_near(fixed_value("hx.hot_out.m_dot[O2]"), 2.0,
                 1.0e-9, "fixed-duty exchanger preserves oxygen");
    require_near(fixed_value("hx.hot_out.h"), 5.0e5,
                 1.0e-6, "material side supplies fixed duty");
    require_near(fixed_value("hx.cold_out.h"), 3.5e5,
                 1.0e-6, "fluid side receives fixed duty");
    require_near(fixed_value("hx.hot_out.p"), 1.9e5,
                 1.0e-6, "material-side pressure loss is applied");
    require_near(fixed_value("hx.cold_out.p"), 9.8e4,
                 1.0e-6, "fluid-side pressure loss is applied");

    const auto balanced_graph = compile(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "material_fluid_energy_balance",
    "media": [{
      "id": "cold_air", "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "materials": [{
      "id": "exhaust", "backend": "test_backend",
      "mechanism": "test.yaml", "phase": "gas",
      "species": ["N2", "O2"]
    }],
    "components": [{
      "id": "hx",
      "kind": "heat_exchanger.material_fluid.energy_balance",
      "parameters": {
        "hot_pressure_loss_fraction": 0.05,
        "cold_pressure_loss_fraction": 0.02
      },
      "materials": {"hot_in": "exhaust", "hot_out": "exhaust"},
      "media": {"cold_in": "cold_air", "cold_out": "cold_air"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "design", "mode": "steady_state_design",
    "fixed_values": {
      "hx.hot_in.m_dot[N2]": {"value": 8.0, "unit": "kg/s"},
      "hx.hot_in.m_dot[O2]": {"value": 2.0, "unit": "kg/s"},
      "hx.hot_in.p": {"value": 2.0, "unit": "bar"},
      "hx.hot_in.h": {"value": 600.0, "unit": "kJ/kg"},
      "hx.cold_in.m_dot": {"value": 20.0, "unit": "kg/s"},
      "hx.cold_in.p": {"value": 1.0, "unit": "bar"},
      "hx.cold_in.h": {"value": 300.0, "unit": "kJ/kg"},
      "hx.cold_out.T": {"value": 320.0, "unit": "K"}
    },
    "initial_guesses": {
      "hx.hot_out.h": {"value": 557.0, "unit": "kJ/kg"},
      "hx.cold_out.h": {"value": 321.4, "unit": "kJ/kg"}
    }
  }]
})json");
    const auto balanced =
        thermox::solve_continuation(balanced_graph.problem);
    require(balanced.continuation.converged,
            balanced.continuation.message);
    const auto balanced_value = [&](const std::string& name) {
        return balanced.x.at(require_variable_index(
            balanced_graph.problem.variable_names, name));
    };
    const double balanced_hot_duty = 10.0 *
        (balanced_value("hx.hot_in.h") -
         balanced_value("hx.hot_out.h"));
    const double balanced_cold_duty = 20.0 *
        (balanced_value("hx.cold_out.h") -
         balanced_value("hx.cold_in.h"));
    require(balanced_hot_duty > 0.0,
            "design-point exchanger transfers positive heat");
    require_near(balanced_hot_duty, balanced_cold_duty, 1.0e-5,
                 "design-point exchanger closes energy balance");
    require_near(balanced_value("hx.hot_out.p"), 1.9e5,
                 1.0e-6,
                 "design-point exchanger applies hot pressure loss");
    require_near(balanced_value("hx.cold_out.p"), 9.8e4,
                 1.0e-6,
                 "design-point exchanger applies cold pressure loss");

    const auto conditioner_graph = compile(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "material_fixed_duty_conditioner",
    "media": [],
    "materials": [{
      "id": "exhaust", "backend": "test_backend",
      "mechanism": "test.yaml", "phase": "gas",
      "species": ["N2", "O2"]
    }],
    "components": [{
      "id": "cooler", "kind": "cooler.material.fixed_duty",
      "parameters": {
        "heat_duty": {"value": 1.0, "unit": "MW"},
        "pressure_loss_fraction": 0.05
      },
      "materials": {"inlet": "exhaust", "outlet": "exhaust"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "design", "mode": "steady_state_design",
    "fixed_values": {
      "cooler.inlet.m_dot[N2]": {"value": 8.0, "unit": "kg/s"},
      "cooler.inlet.m_dot[O2]": {"value": 2.0, "unit": "kg/s"},
      "cooler.inlet.p": {"value": 2.0, "unit": "bar"},
      "cooler.inlet.h": {"value": 600.0, "unit": "kJ/kg"}
    }
  }]
})json");
    const auto conditioner =
        thermox::solve_continuation(conditioner_graph.problem);
    require(conditioner.continuation.converged,
            conditioner.continuation.message);
    const auto conditioner_value = [&](const std::string& name) {
        return conditioner.x.at(require_variable_index(
            conditioner_graph.problem.variable_names, name));
    };
    require_near(conditioner_value("cooler.outlet.m_dot[N2]"),
                 8.0, 1.0e-9,
                 "material cooler preserves nitrogen");
    require_near(conditioner_value("cooler.outlet.m_dot[O2]"),
                 2.0, 1.0e-9,
                 "material cooler preserves oxygen");
    require_near(conditioner_value("cooler.outlet.h"), 5.0e5,
                 1.0e-6,
                 "material cooler removes prescribed duty");
    require_near(conditioner_value("cooler.outlet.p"), 1.9e5,
                 1.0e-6,
                 "material cooler applies pressure loss");

    const auto ua_graph = compile(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "material_fluid_ua",
    "media": [{
      "id": "cold_air", "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "materials": [{
      "id": "exhaust", "backend": "test_backend",
      "mechanism": "test.yaml", "phase": "gas",
      "species": ["N2", "O2"]
    }],
    "components": [{
      "id": "hx",
      "kind": "heat_exchanger.material_fluid.counterflow_ua",
      "parameters": {
        "UA": {"value": 1.0, "unit": "kW/K"},
        "hot_pressure_loss_fraction": 0.01,
        "cold_pressure_loss_fraction": 0.02
      },
      "materials": {"hot_in": "exhaust", "hot_out": "exhaust"},
      "media": {"cold_in": "cold_air", "cold_out": "cold_air"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "design", "mode": "steady_state_design",
    "fixed_values": {
      "hx.hot_in.m_dot[N2]": {"value": 8.0, "unit": "kg/s"},
      "hx.hot_in.m_dot[O2]": {"value": 2.0, "unit": "kg/s"},
      "hx.hot_in.p": {"value": 2.0, "unit": "bar"},
      "hx.hot_in.h": {"value": 600.0, "unit": "kJ/kg"},
      "hx.cold_in.m_dot": {"value": 20.0, "unit": "kg/s"},
      "hx.cold_in.p": {"value": 1.0, "unit": "bar"},
      "hx.cold_in.T": {"value": 300.0, "unit": "K"}
    },
    "initial_guesses": {
      "hx.hot_out.h": {"value": 580.0, "unit": "kJ/kg"},
      "hx.cold_in.h": {"value": 301.35, "unit": "kJ/kg"},
      "hx.cold_out.h": {"value": 311.0, "unit": "kJ/kg"}
    }
  }]
})json");
    const auto ua = thermox::solve_continuation(ua_graph.problem);
    require(ua.continuation.converged, ua.continuation.message);
    const auto ua_value = [&](const std::string& name) {
        return ua.x.at(require_variable_index(
            ua_graph.problem.variable_names, name));
    };
    const double hot_duty = 10.0 *
        (ua_value("hx.hot_in.h") - ua_value("hx.hot_out.h"));
    const double cold_duty = ua_value("hx.cold_in.m_dot") *
        (ua_value("hx.cold_out.h") - ua_value("hx.cold_in.h"));
    require(hot_duty > 0.0, "UA exchanger transfers positive heat");
    require_near(hot_duty, cold_duty, 1.0e-4,
                 "material-fluid UA exchanger conserves energy");
    require(ua_value("hx.hot_out.h") < ua_value("hx.hot_in.h"),
            "material-fluid UA exchanger cools the material side");
    require(ua_value("hx.cold_out.h") > ua_value("hx.cold_in.h"),
            "material-fluid UA exchanger heats the fluid side");
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

    auto reversed = document;
    reversed.cases.front()
        .initial_guesses.at("hx.hot_out.h")
        .value_si = 2.5e5;
    reversed.cases.front()
        .initial_guesses.at("hx.cold_out.h")
        .value_si = 5.5e5;
    const auto reversed_graph =
        thermox::platform::compile_model_graph(
            reversed, registry, "design");
    const auto reversed_direct =
        thermox::solve_newton(reversed_graph.problem);
    require(
        !reversed_direct.diagnostics.converged &&
            reversed_direct.diagnostics.message.find(
                "temperature differences must be positive") !=
                std::string::npos,
        "direct UA solve must reject reversed terminal "
        "temperature guesses: " +
            reversed_direct.diagnostics.message);

    thermox::ContinuationOptions continuation_options;
    continuation_options.minimum_step = 1.0 / 1024.0;
    const auto reversed_continued =
        thermox::solve_continuation(
            reversed_graph.problem, {},
            continuation_options);
    require(
        reversed_continued.continuation.converged,
        reversed_continued.continuation.message);
    require(
        reversed_continued.continuation.used_informed_path,
        "UA exchanger reports its informed thermal path");
    const auto continued_value =
        [&](const std::string& name) {
            return reversed_continued.x.at(
                require_variable_index(
                    reversed_graph.problem.variable_names,
                    name));
        };
    require_near(
        continued_value("hx.hot_out.h"),
        value("hx.hot_out.h"), 1.0e-3,
        "UA continuation reaches the unchanged target hot state");
    require_near(
        continued_value("hx.cold_out.h"),
        value("hx.cold_out.h"), 1.0e-3,
        "UA continuation reaches the unchanged target cold state");
}

void test_dynamic_heat_exchanger_cell_has_conservative_steady_and_transient_forms() {
    const auto properties =
        thermox::physics::make_default_property_package_registry();
    const auto air = properties.create("ideal_gas_mixture", "Air");
    const auto water = properties.create("water_steam_if97", "Water");
    const auto hot_initial = air->state_pt(2.0e5, 500.0);
    const auto cold_initial = water->state_pt(2.0e5, 300.0);
    require(hot_initial.ok() && cold_initial.ok(),
            "dynamic exchanger initial ideal-gas states evaluate");
    constexpr double hot_volume = 0.50;
    constexpr double cold_volume = 0.05;
    const double hot_mass =
        hot_volume * hot_initial.state.density_kg_m3;
    const double cold_mass =
        cold_volume * cold_initial.state.density_kg_m3;
    const double hot_energy =
        hot_mass * hot_initial.state.internal_energy_j_kg;
    const double cold_energy =
        cold_mass * cold_initial.state.internal_energy_j_kg;
    constexpr double flow_diameter = 0.35;
    const double flow_area = std::numbers::pi *
        flow_diameter * flow_diameter / 4.0;
    const double loss_scale = 10.0 /
        (2.0 * flow_area * flow_area);
    const double hot_outlet_pressure =
        2.0e5 - loss_scale / hot_initial.state.density_kg_m3;
    const double cold_outlet_pressure =
        2.0e5 - loss_scale / cold_initial.state.density_kg_m3;
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
    "id": "dynamic_heat_exchanger_cell",
    "media": [{
      "id": "hot_air",
      "backend": "ideal_gas_mixture",
      "substance": "Air"
    }, {
      "id": "cold_water",
      "backend": "water_steam_if97",
      "substance": "Water"
    }],
    "components": [{
      "id": "cell",
      "kind": "heat_exchanger.fluid.dynamic_cell",
      "parameters": {
        "hot_fluid_mass": __HOT_MASS__,
        "cold_fluid_mass": __COLD_MASS__,
        "wall_thermal_capacity": {"value": 50.0, "unit": "kJ/K"},
        "hot_side_UA": {"value": 1.0, "unit": "kW/K"},
        "cold_side_UA": {"value": 1.0, "unit": "kW/K"},
        "hot_flow_diameter": {"value": 0.35, "unit": "m"},
        "cold_flow_diameter": {"value": 0.35, "unit": "m"},
        "hot_loss_coefficient": 10.0,
        "cold_loss_coefficient": 10.0
      },
      "media": {
        "hot_in": "hot_air",
        "hot_out": "hot_air",
        "cold_in": "cold_water",
        "cold_out": "cold_water"
      }
    }],
    "connections": []
  },
  "cases": [{
    "id": "steady",
    "mode": "steady_state_design",
    "fixed_values": {
      "cell.hot_in.m_dot": {"value": 1.0, "unit": "kg/s"},
      "cell.hot_in.p": {"value": 2.0, "unit": "bar"},
      "cell.hot_in.T": {"value": 500.0, "unit": "K"},
      "cell.cold_in.m_dot": {"value": 1.0, "unit": "kg/s"},
      "cell.cold_in.p": {"value": 2.0, "unit": "bar"},
      "cell.cold_in.T": {"value": 300.0, "unit": "K"}
    },
    "initial_guesses": {
      "cell.hot_in.h": __HOT_H__,
      "cell.hot_out.h": 420000.0,
      "cell.cold_in.h": __COLD_H__,
      "cell.cold_out.h": 150000.0
    }
  }, {
    "id": "transient",
    "mode": "dynamic_transient",
    "fixed_values": {
      "cell.hot_in.m_dot": {"value": 1.0, "unit": "kg/s"},
      "cell.hot_in.p": {"value": 2.0, "unit": "bar"},
      "cell.hot_in.h": __HOT_H__,
      "cell.cold_in.m_dot": {"value": 1.0, "unit": "kg/s"},
      "cell.cold_in.p": {"value": 2.0, "unit": "bar"},
      "cell.cold_in.h": __COLD_H__
    },
    "initial_guesses": {
      "cell.hot_total_energy": __HOT_ENERGY__,
      "cell.hot_enthalpy": __HOT_H__,
      "cell.cold_total_energy": __COLD_ENERGY__,
      "cell.cold_enthalpy": __COLD_H__,
      "cell.wall_temperature": 400.0,
      "cell.hot_out.m_dot": 1.0,
      "cell.hot_out.p": __HOT_OUT_P__,
      "cell.hot_out.h": __HOT_H__,
      "cell.cold_out.m_dot": 1.0,
      "cell.cold_out.p": __COLD_OUT_P__,
      "cell.cold_out.h": __COLD_H__
    }
  }]
})json";
    const auto replace = [&](const std::string& token, double value) {
        const std::string replacement = format(value);
        std::size_t position = 0;
        while ((position = text.find(token, position)) !=
               std::string::npos) {
            text.replace(position, token.size(), replacement);
            position += replacement.size();
        }
    };
    replace("__HOT_H__", hot_initial.state.enthalpy_j_kg);
    replace("__COLD_H__", cold_initial.state.enthalpy_j_kg);
    replace("__HOT_MASS__", hot_mass);
    replace("__HOT_ENERGY__", hot_energy);
    replace("__COLD_MASS__", cold_mass);
    replace("__COLD_ENERGY__", cold_energy);
    replace("__HOT_OUT_P__", hot_outlet_pressure);
    replace("__COLD_OUT_P__", cold_outlet_pressure);
    const auto document =
        thermox::platform::parse_model_document_text(text);
    const auto registry =
        thermox::platform::make_default_component_registry();

    const auto steady_graph =
        thermox::platform::compile_model_graph(
            document, registry, properties, "steady");
    const auto steady = thermox::solve_newton(steady_graph.problem);
    require(steady.diagnostics.converged,
            "dynamic-cell steady solve: " +
                steady.diagnostics.message);
    const auto steady_value = [&](const std::string& name) {
        return steady.x.at(require_variable_index(
            steady_graph.problem.variable_names, name));
    };
    const double steady_hot_duty =
        steady_value("cell.hot_in.m_dot") *
        (steady_value("cell.hot_in.h") -
         steady_value("cell.hot_out.h"));
    const double steady_cold_duty =
        steady_value("cell.cold_in.m_dot") *
        (steady_value("cell.cold_out.h") -
         steady_value("cell.cold_in.h"));
    require(steady_hot_duty > 0.0,
            "steady dynamic-cell limit transfers heat hot to cold");
    require_near(steady_hot_duty, steady_cold_duty, 1.0e-6,
                 "steady dynamic-cell limit conserves energy");

    const auto graph =
        thermox::platform::compile_transient_model_graph(
            document, registry, properties, "transient");
    const auto initialized =
        thermox::make_consistent_initial_conditions(
            graph.problem, 0.0);
    require(initialized.diagnostics.converged,
            "dynamic-cell consistent initialization: " +
                initialized.diagnostics.message);
    const auto index = [&](const std::string& name) {
        return require_variable_index(graph.problem.variable_names, name);
    };
    const auto hot_energy_index = index("cell.hot_total_energy");
    const auto cold_energy_index = index("cell.cold_total_energy");
    const auto wall_temperature = index("cell.wall_temperature");
    constexpr double wall_capacity = 50000.0;
    const double stored_energy_rate =
        initialized.derivative.at(hot_energy_index) +
        initialized.derivative.at(cold_energy_index) +
        wall_capacity *
            initialized.derivative.at(wall_temperature);
    const auto& x = initialized.state;
    const double boundary_energy_rate =
        x.at(index("cell.hot_in.m_dot")) *
            x.at(index("cell.hot_in.h")) -
        x.at(index("cell.hot_out.m_dot")) *
            x.at(index("cell.hot_out.h")) +
        x.at(index("cell.cold_in.m_dot")) *
            x.at(index("cell.cold_in.h")) -
        x.at(index("cell.cold_out.m_dot")) *
            x.at(index("cell.cold_out.h"));
    require_near(stored_energy_rate, boundary_energy_rate, 1.0e-7,
                 "transient cell closes total stored-energy rate");
    thermox::TimeIntegrationOptions options;
    options.end_time = 0.5;
    options.initial_step = 0.01;
    options.max_step = 0.05;
    const auto result = thermox::integrate_dae(graph.problem, options);
    require(result.diagnostics.success,
            "dynamic-cell transient integration: " +
                result.diagnostics.message + ", solves=" +
                std::to_string(result.diagnostics.nonlinear_solves) +
                ", iterations=" +
                std::to_string(result.diagnostics.nonlinear_iterations));
    const auto& final = result.trajectory.back().state;
    require(final.at(index("cell.hot_enthalpy")) <
                hot_initial.state.enthalpy_j_kg,
            "hot inventory cools during transient heat transfer");
    require(final.at(index("cell.cold_enthalpy")) >
                cold_initial.state.enthalpy_j_kg,
            "cold inventory warms during transient heat transfer");
    require(std::isfinite(final.at(hot_energy_index)) &&
                std::isfinite(final.at(cold_energy_index)),
            "dynamic cell retains finite stored fluid energies");
}

void test_dynamic_material_fluid_cell_conserves_species_and_energy() {
    const auto properties =
        thermox::physics::make_default_property_package_registry();
    const auto water = properties.create("water_steam_if97", "Water");
    const auto cold_initial = water->state_pt(2.0e5, 300.0);
    require(cold_initial.ok(),
            "material-fluid cell initial water state evaluates");
    constexpr double cold_mass = 49.83;
    const double cold_energy =
        cold_mass * cold_initial.state.internal_energy_j_kg;
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
    "id": "dynamic_material_fluid_cell",
    "media": [{
      "id": "water",
      "backend": "water_steam_if97",
      "substance": "Water"
    }],
    "materials": [{
      "id": "exhaust",
      "backend": "test_backend",
      "mechanism": "test.yaml",
      "phase": "gas",
      "species": ["N2", "O2"]
    }],
    "components": [{
      "id": "cell",
      "kind": "heat_exchanger.material_fluid.dynamic_cell",
      "parameters": {
        "cold_fluid_mass": {"value": 49.83, "unit": "kg"},
        "wall_thermal_capacity": {"value": 50.0, "unit": "kJ/K"},
        "hot_side_UA": {"value": 1.0, "unit": "kW/K"},
        "cold_side_UA": {"value": 1.0, "unit": "kW/K"},
        "hot_flow_diameter": {"value": 0.35, "unit": "m"},
        "cold_flow_diameter": {"value": 0.35, "unit": "m"},
        "hot_loss_coefficient": 10.0,
        "cold_loss_coefficient": 10.0
      },
      "materials": {"hot_in": "exhaust", "hot_out": "exhaust"},
      "media": {"cold_in": "water", "cold_out": "water"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "steady",
    "mode": "steady_state_design",
    "fixed_values": {
      "cell.hot_in.m_dot[N2]": {"value": 0.8, "unit": "kg/s"},
      "cell.hot_in.m_dot[O2]": {"value": 0.2, "unit": "kg/s"},
      "cell.hot_in.p": {"value": 2.0, "unit": "bar"},
      "cell.hot_in.h": {"value": 600.0, "unit": "kJ/kg"},
      "cell.cold_in.m_dot": {"value": 1.0, "unit": "kg/s"},
      "cell.cold_in.p": {"value": 2.0, "unit": "bar"},
      "cell.cold_in.h": __COLD_H__
    },
    "initial_guesses": {
      "cell.hot_out.m_dot[N2]": 0.8,
      "cell.hot_out.m_dot[O2]": 0.2,
      "cell.hot_out.p": 199500.0,
      "cell.hot_out.h": 450000.0,
      "cell.cold_out.m_dot": 1.0,
      "cell.cold_out.p": 199999.0,
      "cell.cold_out.h": 150000.0
    }
  }, {
    "id": "transient",
    "mode": "dynamic_transient",
    "fixed_values": {
      "cell.hot_in.m_dot[N2]": {"value": 0.8, "unit": "kg/s"},
      "cell.hot_in.m_dot[O2]": {"value": 0.2, "unit": "kg/s"},
      "cell.hot_in.p": {"value": 2.0, "unit": "bar"},
      "cell.hot_in.h": {"value": 600.0, "unit": "kJ/kg"},
      "cell.cold_in.m_dot": {"value": 1.0, "unit": "kg/s"},
      "cell.cold_in.p": {"value": 2.0, "unit": "bar"},
      "cell.cold_in.h": __COLD_H__
    },
    "initial_guesses": {
      "cell.hot_out.m_dot[N2]": 0.8,
      "cell.hot_out.m_dot[O2]": 0.2,
      "cell.hot_out.p": 199500.0,
      "cell.hot_out.h": 450000.0,
      "cell.cold_out.m_dot": 1.0,
      "cell.cold_out.p": 199999.0,
      "cell.cold_out.h": __COLD_H__,
      "cell.cold_total_energy": __COLD_ENERGY__,
      "cell.cold_enthalpy": __COLD_H__,
      "cell.wall_temperature": 450.0
    }
  }]
})json";
    const auto replace = [&](const std::string& token, double value) {
        const std::string replacement = format(value);
        std::size_t position = 0;
        while ((position = text.find(token, position)) !=
               std::string::npos) {
            text.replace(position, token.size(), replacement);
            position += replacement.size();
        }
    };
    replace("__COLD_H__", cold_initial.state.enthalpy_j_kg);
    replace("__COLD_ENERGY__", cold_energy);
    const auto document =
        thermox::platform::parse_model_document_text(text);
    const auto registry =
        thermox::platform::make_default_component_registry();
    thermox::physics::ThermochemistryPackageRegistry chemistry;
    chemistry.register_backend(
        {"test_backend", "test-thermochemistry", "1.0.0",
         {thermox::physics::ThermochemistryCapability::state_ph}},
        [](std::string_view, std::string_view) {
            return std::make_shared<const TestThermochemistryPackage>();
        });

    const auto steady_graph = thermox::platform::compile_model_graph(
        document, registry, properties,
        thermox::platform::EngineeringArtifactRegistry{},
        chemistry, "steady");
    const auto steady = thermox::solve_newton(steady_graph.problem);
    require(steady.diagnostics.converged,
            "material-fluid steady cell: " +
                steady.diagnostics.message);
    const auto steady_value = [&](const std::string& name) {
        return steady.x.at(require_variable_index(
            steady_graph.problem.variable_names, name));
    };
    require_near(
        steady_value("cell.hot_out.m_dot[N2]"), 0.8, 1.0e-12,
        "material-fluid cell preserves N2 flow");
    require_near(
        steady_value("cell.hot_out.m_dot[O2]"), 0.2, 1.0e-12,
        "material-fluid cell preserves O2 flow");
    const double steady_hot_duty =
        steady_value("cell.hot_in.h") -
        steady_value("cell.hot_out.h");
    const double steady_cold_duty =
        steady_value("cell.cold_in.m_dot") *
        (steady_value("cell.cold_out.h") -
         steady_value("cell.cold_in.h"));
    require_near(steady_hot_duty, steady_cold_duty, 1.0e-5,
                 "material-fluid steady cell energy closure");

    const auto graph =
        thermox::platform::compile_transient_model_graph(
            document, registry, properties,
            thermox::platform::EngineeringArtifactRegistry{},
            chemistry, "transient");
    require(graph.problem.sparse_jacobian_pattern.has_value(),
            "material-fluid transient cell preserves sparse DAE assembly");
    const auto initialized =
        thermox::make_consistent_initial_conditions(
            graph.problem, 0.0);
    require(initialized.diagnostics.converged,
            "material-fluid consistent initialization: " +
                initialized.diagnostics.message);
    thermox::TimeIntegrationOptions options;
    options.end_time = 0.2;
    options.initial_step = 0.005;
    options.max_step = 0.02;
    const auto transient = thermox::integrate_dae(graph.problem, options);
    require(transient.diagnostics.success,
            "material-fluid transient cell: " +
                transient.diagnostics.message);
    const auto index = [&](const std::string& name) {
        return require_variable_index(graph.problem.variable_names, name);
    };
    const auto& final = transient.trajectory.back();
    require(final.state.at(index("cell.cold_enthalpy")) >
                cold_initial.state.enthalpy_j_kg,
            "material exhaust heats the cold-fluid holdup");
    constexpr double wall_capacity = 50000.0;
    const double stored_rate =
        final.derivative.at(index("cell.cold_total_energy")) +
        wall_capacity *
            final.derivative.at(index("cell.wall_temperature"));
    const double hot_in_flow =
        final.state.at(index("cell.hot_in.m_dot[N2]")) +
        final.state.at(index("cell.hot_in.m_dot[O2]"));
    const double hot_out_flow =
        final.state.at(index("cell.hot_out.m_dot[N2]")) +
        final.state.at(index("cell.hot_out.m_dot[O2]"));
    const double boundary_rate =
        hot_in_flow * final.state.at(index("cell.hot_in.h")) -
        hot_out_flow * final.state.at(index("cell.hot_out.h")) +
        final.state.at(index("cell.cold_in.m_dot")) *
            final.state.at(index("cell.cold_in.h")) -
        final.state.at(index("cell.cold_out.m_dot")) *
            final.state.at(index("cell.cold_out.h"));
    require_near(stored_rate, boundary_rate, 1.0e-5,
                 "material-fluid transient energy-rate closure");
}

void test_two_cell_counterflow_exchanger_composes_and_conserves() {
    const auto document = thermox::platform::load_model_document(
        std::string(THERMOX_SOURCE_DIR) +
        "/core/examples/two_cell_counterflow_heat_exchanger.json");
    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto properties =
        thermox::physics::make_default_property_package_registry();

    const auto steady_graph =
        thermox::platform::compile_model_graph(
            document, registry, properties, "steady");
    const auto steady = thermox::solve_newton(steady_graph.problem);
    require(steady.diagnostics.converged,
            "two-cell steady solve: " +
                steady.diagnostics.message);
    const auto steady_value = [&](const std::string& name) {
        return steady.x.at(require_variable_index(
            steady_graph.problem.variable_names, name));
    };
    const double hot_duty =
        steady_value("hot_source.outlet.m_dot") *
        (steady_value("hot_source.outlet.h") -
         steady_value("hot_sink.inlet.h"));
    const double cold_duty =
        steady_value("cold_source.outlet.m_dot") *
        (steady_value("cold_sink.inlet.h") -
         steady_value("cold_source.outlet.h"));
    require(hot_duty > 0.0,
            "two-cell counterflow exchanger transfers heat hot to cold");
    require_near(hot_duty, cold_duty, 1.0e-5,
                 "two-cell counterflow steady energy balance");
    require(
        steady_value("hot_source.outlet.p") >
            steady_value("cell_1.hot_out.p") &&
        steady_value("cell_1.hot_out.p") >
            steady_value("hot_sink.inlet.p"),
        "hot-side pressure falls through both cells");
    require(
        steady_value("cold_source.outlet.p") >
            steady_value("cell_2.cold_out.p") &&
        steady_value("cell_2.cold_out.p") >
            steady_value("cold_sink.inlet.p"),
        "counterflow cold-side pressure falls through both cells");

    const auto graph =
        thermox::platform::compile_transient_model_graph(
            document, registry, properties, "heat_up");
    const auto initialized =
        thermox::make_consistent_initial_conditions(
            graph.problem, 0.0);
    require(initialized.diagnostics.converged,
            "two-cell consistent initialization: " +
                initialized.diagnostics.message);
    thermox::TimeIntegrationOptions options;
    options.end_time = 0.5;
    options.initial_step = 0.01;
    options.max_step = 0.05;
    const auto transient =
        thermox::integrate_dae(graph.problem, options);
    require(transient.diagnostics.success,
            "two-cell transient integration: " +
                transient.diagnostics.message);
    const auto index = [&](const std::string& name) {
        return require_variable_index(graph.problem.variable_names, name);
    };
    const auto& final = transient.trajectory.back();
    require(final.state.at(index("cell_1.hot_enthalpy")) <
                initialized.state.at(index("cell_1.hot_enthalpy")) &&
                final.state.at(index("cell_2.hot_enthalpy")) <
                initialized.state.at(index("cell_2.hot_enthalpy")),
            "both counterflow cells cool their hot holdups");
    require(final.state.at(index("cell_1.cold_enthalpy")) >
                initialized.state.at(index("cell_1.cold_enthalpy")) &&
                final.state.at(index("cell_2.cold_enthalpy")) >
                initialized.state.at(index("cell_2.cold_enthalpy")),
            "both counterflow cells heat their cold holdups");
    constexpr double wall_capacity = 50000.0;
    double stored_rate = 0.0;
    for (const std::string cell : {"cell_1", "cell_2"}) {
        stored_rate += final.derivative.at(
            index(cell + ".hot_total_energy"));
        stored_rate += final.derivative.at(
            index(cell + ".cold_total_energy"));
        stored_rate += wall_capacity * final.derivative.at(
            index(cell + ".wall_temperature"));
    }
    const double boundary_rate =
        final.state.at(index("hot_source.outlet.m_dot")) *
            final.state.at(index("hot_source.outlet.h")) -
        final.state.at(index("hot_sink.inlet.m_dot")) *
            final.state.at(index("hot_sink.inlet.h")) +
        final.state.at(index("cold_source.outlet.m_dot")) *
            final.state.at(index("cold_source.outlet.h")) -
        final.state.at(index("cold_sink.inlet.m_dot")) *
            final.state.at(index("cold_sink.inlet.h"));
    require_near(stored_rate, boundary_rate, 1.0e-5,
                 "two-cell transient whole-system energy-rate closure");
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
    require(
        graph_result.system_balances.size() == 1,
        "closed Rankine graph should expose one external energy balance");
    require_near(
        require_result_value(
            graph_result.system_balances,
            "net_boundary_energy_flow"),
        0.0,
        1.0e-3,
        "generic system boundary audit closes Rankine energy");
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
        "kind": "compressor.fluid.isentropic_efficiency",
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
        "under-specified: 3 variables and 0 equations; "
        "3 additional independent equation(s) or specification(s) "
        "required; unmatched variable candidate(s): "
        "source.outlet.m_dot, source.outlet.p, source.outlet.h");
    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                under_specified, registry, "design");
        },
        "underdetermined structural region(s): "
        "{variables: source.outlet.m_dot; equations: none}");

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
    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                over_specified, registry, "design");
        },
        "unmatched equation candidate(s): "
        "fixed.design.compressor.outlet.p");
}

void test_compiler_reports_square_structural_singularity() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "square_singular",
    "media": [],
    "components": [{
      "id": "bad",
      "kind": "test.structurally_singular"
    }],
    "connections": []
  },
  "cases": []
})json");
    thermox::platform::ComponentRegistry registry;
    registry.register_model(
        std::make_shared<
            const StructurallySingularSignalModel>());
    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                document, registry);
        },
        "square but structurally singular: 2 variables and "
        "2 equations; unmatched variable candidate(s): "
        "bad.right.value; unmatched equation candidate(s): "
        "component.bad.left_target_b");
    require_throws(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                document, registry);
        },
        "underdetermined structural region(s): "
        "{variables: bad.right.value; equations: none}; "
        "overdetermined structural region(s): "
        "{variables: bad.left.value; equations: "
        "component.bad.left_target_a, "
        "component.bad.left_target_b}");
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
        {"coolprop_heos", "Water", 1.0e5, 300.0},
        {"coolprop_heos", "R245fa", 4.0e5, 336.0},
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

void test_heos_rigid_volume_crosses_both_saturation_boundaries() {
    struct TransitionCase {
        std::string id;
        double pressure;
        double enthalpy;
        double mass;
        double energy;
        double heat_flow;
        double duration;
        thermox::physics::Phase initial_phase;
        thermox::physics::Phase final_phase;
    };
    const std::vector<TransitionCase> cases = {
        {"liquid_to_two_phase", 1.0e6, 760000.0,
         0.88774715307865222, 673687.8363397771,
         -1000.0, 1.0, thermox::physics::Phase::liquid,
         thermox::physics::Phase::two_phase},
        {"two_phase_to_liquid", 981029.77881414222,
         758852.18418594659, 0.88774715307865222,
         672687.8363397771, 1000.0, 1.0,
         thermox::physics::Phase::two_phase,
         thermox::physics::Phase::liquid},
        {"two_phase_to_vapor", 1.0e6, 2777000.0,
         0.0051453165484018717, 13288.544054912041,
         1.0, 10.0, thermox::physics::Phase::two_phase,
         thermox::physics::Phase::vapor},
        {"vapor_to_two_phase", 1002796.364351989,
         2779486.9924528766, 0.0051453165484018717,
         13298.544054911899, -1.0, 10.0,
         thermox::physics::Phase::vapor,
         thermox::physics::Phase::two_phase}};

    const auto properties =
        thermox::physics::make_default_property_package_registry();
    const auto components =
        thermox::platform::make_default_component_registry();
    const auto water = properties.create("coolprop_heos", "Water");

    for (const auto& test_case : cases) {
        std::ostringstream json;
        json << std::setprecision(17) << R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "heos_regime_transition",
    "media": [{"id": "water", "backend": "coolprop_heos", "substance": "Water"}],
    "components": [{
      "id": "thermal_boundary", "kind": "source.heat.boundary"
    }, {
      "id": "vessel", "kind": "volume.fluid.rigid_heat_transfer",
      "parameters": {"volume": {"value": 1.0, "unit": "L"}},
      "media": {"inlet": "water", "outlet": "water"}
    }],
    "connections": [{
      "id": "heat", "kind": "heat_link",
      "from": "thermal_boundary.outlet", "to": "vessel.heat"
    }]
  },
  "cases": [{
    "id": ")json" << test_case.id << R"json(",
    "mode": "dynamic_transient",
    "fixed_values": {
      "thermal_boundary.outlet.Q_dot": )json" << test_case.heat_flow << R"json(,
      "vessel.inlet.m_dot": 0.0,
      "vessel.inlet.p": )json" << test_case.pressure << R"json(,
      "vessel.inlet.h": )json" << test_case.enthalpy << R"json(,
      "vessel.outlet.m_dot": 0.0
    },
    "initial_guesses": {
      "thermal_boundary.outlet.T": 453.0,
      "vessel.inlet.m_dot": 0.0,
      "vessel.inlet.p": )json" << test_case.pressure << R"json(,
      "vessel.inlet.h": )json" << test_case.enthalpy << R"json(,
      "vessel.outlet.m_dot": 0.0,
      "vessel.outlet.p": )json" << test_case.pressure << R"json(,
      "vessel.outlet.h": )json" << test_case.enthalpy << R"json(,
      "vessel.heat.Q_dot": )json" << test_case.heat_flow << R"json(,
      "vessel.heat.T": 453.0,
      "vessel.mass": )json" << test_case.mass << R"json(,
      "vessel.total_energy": )json" << test_case.energy << R"json(,
      "vessel.pressure": )json" << test_case.pressure << R"json(,
      "vessel.enthalpy": )json" << test_case.enthalpy << R"json(
    }
  }]
})json";
        const auto document =
            thermox::platform::parse_model_document_text(json.str());
        const auto graph =
            thermox::platform::compile_transient_model_graph(
                document, components, properties, test_case.id);
        thermox::TimeIntegrationOptions options;
        options.end_time = test_case.duration;
        const auto result = thermox::integrate_dae(graph.problem, options);
        require(result.diagnostics.success,
                test_case.id + ": " + result.diagnostics.message);
        require(result.diagnostics.rejected_steps == 0,
                test_case.id + " must cross without rejected steps");

        const auto mass = require_variable_index(
            graph.problem.variable_names, "vessel.mass");
        const auto energy = require_variable_index(
            graph.problem.variable_names, "vessel.total_energy");
        const auto pressure = require_variable_index(
            graph.problem.variable_names, "vessel.pressure");
        const auto enthalpy = require_variable_index(
            graph.problem.variable_names, "vessel.enthalpy");
        const auto& initial = result.trajectory.front();
        const auto& final = result.trajectory.back();
        const auto initial_properties = water->state_ph(
            initial.state.at(pressure), initial.state.at(enthalpy));
        const auto final_properties = water->state_ph(
            final.state.at(pressure), final.state.at(enthalpy));
        require(initial_properties.ok() && final_properties.ok(),
                test_case.id + " transition states must evaluate");
        require(initial_properties.state.phase == test_case.initial_phase &&
                    final_properties.state.phase == test_case.final_phase,
                test_case.id + " must reach the expected phases");
        require_near(final.state.at(mass), initial.state.at(mass),
                     1.0e-12, test_case.id + " conserves mass");
        require_near(
            final.state.at(energy) - initial.state.at(energy),
            test_case.heat_flow * test_case.duration, 1.0e-6,
            test_case.id + " integrates boundary heat exactly");
        require_near(final.derivative.at(mass), 0.0, 1.0e-12,
                     test_case.id + " has zero mass rate");
        require_near(final.derivative.at(energy),
                     test_case.heat_flow, 1.0e-8,
                     test_case.id + " closes the energy rate");
    }
}

void test_dynamic_equilibrium_drum_conserves_inventory() {
    const auto properties =
        thermox::physics::make_default_property_package_registry();
    const auto water = properties.create("water_steam_if97", "Water");
    constexpr double initial_pressure = 2.0e5;
    constexpr double initial_quality = 0.10;
    constexpr double volume = 1.0;
    constexpr double height = 2.0;
    const auto saturation = water->saturation_p(initial_pressure);
    require(saturation.ok(), "drum initial saturation must evaluate");
    const double specific_volume =
        (1.0 - initial_quality) /
            saturation.liquid.density_kg_m3 +
        initial_quality / saturation.vapor.density_kg_m3;
    const double initial_mass = volume / specific_volume;
    const double initial_specific_energy =
        (1.0 - initial_quality) *
            saturation.liquid.internal_energy_j_kg +
        initial_quality *
            saturation.vapor.internal_energy_j_kg;
    const double initial_energy =
        initial_mass * initial_specific_energy;
    const double initial_level = height / volume * initial_mass *
        (1.0 - initial_quality) /
        saturation.liquid.density_kg_m3;

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
    "id": "heated_equilibrium_drum",
    "media": [{
      "id": "water",
      "backend": "water_steam_if97",
      "substance": "Water"
    }],
    "components": [{
      "id": "drum",
      "kind": "drum.fluid.equilibrium_two_phase",
      "parameters": {
        "volume": {"value": 1.0, "unit": "m3"},
        "vessel_height": {"value": 2.0, "unit": "m"}
      },
      "media": {
        "inlet": "water",
        "vapor_outlet": "water",
        "liquid_outlet": "water"
      }
    }],
    "connections": []
  },
  "cases": [{
    "id": "heated_hold",
    "mode": "dynamic_transient",
    "fixed_values": {
      "drum.inlet.m_dot": {"value": 0.0, "unit": "kg/s"},
      "drum.inlet.h": {"value": 500.0, "unit": "kJ/kg"},
      "drum.vapor_outlet.m_dot": {"value": 0.0, "unit": "kg/s"},
      "drum.liquid_outlet.m_dot": {"value": 0.0, "unit": "kg/s"},
      "drum.heat.Q_dot": {"value": 10.0, "unit": "kW"}
    },
    "initial_guesses": {
      "drum.total_mass": __MASS__,
      "drum.total_internal_energy": __ENERGY__,
      "drum.pressure": __PRESSURE__,
      "drum.vapor_quality": __QUALITY__,
      "drum.liquid_level": __LEVEL__
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
    replace("__MASS__", format(initial_mass));
    replace("__ENERGY__", format(initial_energy));
    replace("__PRESSURE__", format(initial_pressure));
    replace("__QUALITY__", format(initial_quality));
    replace("__LEVEL__", format(initial_level));

    const auto document =
        thermox::platform::parse_model_document_text(text);
    const auto graph =
        thermox::platform::compile_transient_model_graph(
            document,
            thermox::platform::make_default_component_registry(),
            properties, "heated_hold");
    const auto mass = require_variable_index(
        graph.problem.variable_names, "drum.total_mass");
    const auto energy = require_variable_index(
        graph.problem.variable_names,
        "drum.total_internal_energy");
    const auto pressure = require_variable_index(
        graph.problem.variable_names, "drum.pressure");
    const auto quality = require_variable_index(
        graph.problem.variable_names, "drum.vapor_quality");
    const auto level = require_variable_index(
        graph.problem.variable_names, "drum.liquid_level");
    const auto initialized =
        thermox::make_consistent_initial_conditions(
            graph.problem, 0.0);
    require(initialized.diagnostics.converged,
            initialized.diagnostics.message);
    require_near(initialized.derivative.at(mass), 0.0, 1.0e-9,
                 "closed drum has zero mass derivative");
    require_near(initialized.derivative.at(energy), 1.0e4, 1.0e-5,
                 "drum heat input becomes stored-energy derivative");

    thermox::TimeIntegrationOptions options;
    options.end_time = 0.1;
    options.initial_step = 0.01;
    options.max_step = 0.02;
    const auto result = thermox::integrate_dae(graph.problem, options);
    require(result.diagnostics.success,
            result.diagnostics.message);
    const auto& final = result.trajectory.back().state;
    require_near(final.at(mass), initial_mass, 1.0e-8,
                 "closed drum conserves total mass");
    require_near(final.at(energy), initial_energy + 1000.0, 1.0e-3,
                 "drum integrates applied heat into internal energy");
    require(final.at(quality) >= 0.0 && final.at(quality) <= 1.0,
            "drum quality remains physical");
    const auto final_saturation =
        water->saturation_p(final.at(pressure));
    require(final_saturation.ok(),
            "final drum saturation must evaluate");
    const double final_specific_volume =
        (1.0 - final.at(quality)) /
            final_saturation.liquid.density_kg_m3 +
        final.at(quality) /
            final_saturation.vapor.density_kg_m3;
    require_near(final.at(mass) * final_specific_volume,
                 volume, 1.0e-8,
                 "drum final rigid-volume closure");
    const double expected_level = height / volume * final.at(mass) *
        (1.0 - final.at(quality)) /
        final_saturation.liquid.density_kg_m3;
    require_near(final.at(level), expected_level, 1.0e-9,
                 "drum reports liquid level from phase volume");
}

void test_bounded_pi_controller_prevents_integrator_windup() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "bounded_pi",
    "media": [],
    "components": [{
      "id": "controller",
      "kind": "control.pi_bounded.normalized",
      "parameters": {
        "proportional_gain": 2.0,
        "integral_time": {"value": 2.0, "unit": "s"},
        "tracking_time": {"value": 0.5, "unit": "s"},
        "bias": 0.0,
        "lower_limit": 0.0,
        "upper_limit": 1.0
      }
    }],
    "connections": []
  },
  "cases": [{
    "id": "upper_saturation",
    "mode": "dynamic_transient",
    "fixed_values": {
      "controller.setpoint.value": 1.0,
      "controller.measurement.value": 0.0
    },
    "initial_guesses": {"controller.integral_state": 0.0}
  }]
})json");
    const auto graph =
        thermox::platform::compile_transient_model_graph(
            document,
            thermox::platform::make_default_component_registry(),
            "upper_saturation");
    const auto integral = require_variable_index(
        graph.problem.variable_names, "controller.integral_state");
    const auto command = require_variable_index(
        graph.problem.variable_names, "controller.command.value");
    const auto initialized = thermox::make_consistent_initial_conditions(
        graph.problem, 0.0);
    require(initialized.diagnostics.converged,
            initialized.diagnostics.message);
    require_near(initialized.state.at(command), 1.0, 1.0e-12,
                 "PI output applies upper saturation");
    require_near(initialized.derivative.at(integral), -1.0, 1.0e-10,
                 "anti-windup drives integrator toward tracking state");

    thermox::TimeIntegrationOptions options;
    options.end_time = 2.0;
    options.initial_step = 0.02;
    options.max_step = 0.1;
    const auto result = thermox::integrate_dae(graph.problem, options);
    require(result.diagnostics.success,
            result.diagnostics.message);
    const auto& final = result.trajectory.back().state;
    require_near(final.at(command), 1.0, 1.0e-10,
                 "PI output remains bounded during persistent error");
    require(final.at(integral) < -0.45 && final.at(integral) > -0.55,
            "back-calculation converges to a finite anti-windup state");
}

void test_closed_loop_drum_feed_control_composes_platform_domains() {
    const auto properties =
        thermox::physics::make_default_property_package_registry();
    const auto water = properties.create("water_steam_if97", "Water");
    constexpr double initial_pressure = 2.0e5;
    constexpr double initial_quality = 0.10;
    const auto saturation = water->saturation_p(initial_pressure);
    require(saturation.ok(),
            "closed-loop drum initial saturation must evaluate");
    const double specific_volume =
        (1.0 - initial_quality) /
            saturation.liquid.density_kg_m3 +
        initial_quality / saturation.vapor.density_kg_m3;
    const double initial_mass = 1.0 / specific_volume;
    const double initial_energy = initial_mass *
        ((1.0 - initial_quality) *
             saturation.liquid.internal_energy_j_kg +
         initial_quality *
             saturation.vapor.internal_energy_j_kg);
    const double initial_level = 2.0 * initial_mass *
        (1.0 - initial_quality) /
        saturation.liquid.density_kg_m3;
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
    "id": "closed_loop_drum_feed",
    "media": [{
      "id": "water",
      "backend": "water_steam_if97",
      "substance": "Water"
    }],
    "components": [{
      "id": "feed",
      "kind": "source.fluid.boundary",
      "media": {"outlet": "water"}
    }, {
      "id": "feed_valve",
      "kind": "valve.fluid.actuated_nonflashing_liquid",
      "parameters": {
        "full_open_diameter": {"value": 5.0, "unit": "mm"},
        "discharge_coefficient": 0.62
      },
      "media": {"inlet": "water", "outlet": "water"}
    }, {
      "id": "drum",
      "kind": "drum.fluid.equilibrium_two_phase",
      "parameters": {
        "volume": {"value": 1.0, "unit": "m3"},
        "vessel_height": {"value": 2.0, "unit": "m"}
      },
      "media": {
        "inlet": "water",
        "vapor_outlet": "water",
        "liquid_outlet": "water"
      }
    }, {
      "id": "level_controller",
      "kind": "control.pi_bounded.normalized",
      "parameters": {
        "proportional_gain": 2.0,
        "integral_time": {"value": 5.0, "unit": "s"},
        "tracking_time": {"value": 1.0, "unit": "s"},
        "bias": 0.20,
        "lower_limit": 0.02,
        "upper_limit": 0.80
      }
    }, {
      "id": "feed_actuator",
      "kind": "control.first_order_lag.normalized",
      "parameters": {
        "gain": 1.0,
        "time_constant": {"value": 0.5, "unit": "s"}
      }
    }],
    "connections": [{
      "id": "feed_to_valve",
      "kind": "fluid_link",
      "from": "feed.outlet",
      "to": "feed_valve.inlet"
    }, {
      "id": "valve_to_drum",
      "kind": "fluid_link",
      "from": "feed_valve.outlet",
      "to": "drum.inlet"
    }, {
      "id": "level_measurement",
      "kind": "signal_link",
      "contract_version": "thermox.connector.signal/v1",
      "from": "drum.level_signal",
      "to": "level_controller.measurement"
    }, {
      "id": "controller_to_actuator",
      "kind": "signal_link",
      "contract_version": "thermox.connector.control/v1",
      "from": "level_controller.command",
      "to": "feed_actuator.command"
    }, {
      "id": "actuator_to_valve",
      "kind": "signal_link",
      "contract_version": "thermox.connector.control/v1",
      "from": "feed_actuator.response",
      "to": "feed_valve.command"
    }]
  },
  "cases": [{
    "id": "level_control",
    "mode": "dynamic_transient",
    "fixed_values": {
      "feed.outlet.p": {"value": 20.0, "unit": "bar"},
      "feed.outlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "drum.vapor_outlet.m_dot": {"value": 0.0, "unit": "kg/s"},
      "drum.liquid_outlet.m_dot": {"value": 0.05, "unit": "kg/s"},
      "drum.heat.Q_dot": {"value": 0.0, "unit": "kW"},
      "level_controller.setpoint.value": 0.015
    },
    "initial_guesses": {
      "drum.total_mass": __MASS__,
      "drum.total_internal_energy": __ENERGY__,
      "drum.pressure": __PRESSURE__,
      "drum.vapor_quality": __QUALITY__,
      "drum.liquid_level": __LEVEL__,
      "level_controller.integral_state": 0.0,
      "feed_actuator.response.value": 0.20,
      "feed_valve.inlet.p": {"value": 20.0, "unit": "bar"},
      "feed_valve.outlet.p": {"value": 2.0, "unit": "bar"},
      "feed_valve.inlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "feed_valve.outlet.h": {"value": 300.0, "unit": "kJ/kg"}
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
    replace("__MASS__", format(initial_mass));
    replace("__ENERGY__", format(initial_energy));
    replace("__PRESSURE__", format(initial_pressure));
    replace("__QUALITY__", format(initial_quality));
    replace("__LEVEL__", format(initial_level));
    const auto document =
        thermox::platform::parse_model_document_text(text);
    const auto graph =
        thermox::platform::compile_transient_model_graph(
            document,
            thermox::platform::make_default_component_registry(),
            properties, "level_control");
    const auto mass = require_variable_index(
        graph.problem.variable_names, "drum.total_mass");
    const auto level = require_variable_index(
        graph.problem.variable_names, "drum.liquid_level");
    const auto signal = require_variable_index(
        graph.problem.variable_names, "drum.level_signal.value");
    const auto command = require_variable_index(
        graph.problem.variable_names,
        "level_controller.command.value");
    const auto valve_flow = require_variable_index(
        graph.problem.variable_names, "feed_valve.outlet.m_dot");
    const auto initialized = thermox::make_consistent_initial_conditions(
        graph.problem, 0.0);
    require(initialized.diagnostics.converged,
            initialized.diagnostics.message);
    require(initialized.state.at(valve_flow) > 0.05,
            "initial commanded feed exceeds fixed liquid discharge");
    require_near(initialized.state.at(signal),
                 initialized.state.at(level) / 2.0, 1.0e-12,
                 "drum level signal is normalized in closed loop");

    thermox::TimeIntegrationOptions options;
    options.end_time = 0.5;
    options.initial_step = 0.01;
    options.max_step = 0.05;
    const auto result = thermox::integrate_dae(graph.problem, options);
    require(result.diagnostics.success,
            result.diagnostics.message);
    const auto& final = result.trajectory.back().state;
    require(final.at(mass) > initial_mass,
            "closed-loop feed increases inventory from low level");
    require(final.at(command) >= 0.02 && final.at(command) <= 0.80,
            "closed-loop PI command remains within declared bounds");
    require_near(final.at(signal), final.at(level) / 2.0, 1.0e-10,
                 "closed-loop level measurement remains consistent");
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
        "id": "combiner",
        "kind": "shaft.combiner.two_driver",
        "parameters": {
          "mechanical_efficiency": 0.995,
          "fixed_loss": {"value": 0.5, "unit": "MW"}
        }
      },
      {
        "id": "train",
        "kind": "shaft.train.multi_load",
        "port_counts": {"load": 2},
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
        "id": "gearbox",
        "kind": "gearbox.shaft.fixed_ratio",
        "parameters": {
          "speed_ratio": 3.0,
          "mechanical_efficiency": 0.97
        }
      },
      {
        "id": "geared_train",
        "kind": "shaft.train.multi_load",
        "port_counts": {"load": 2},
        "parameters": {
          "mechanical_efficiency": 0.98,
          "fixed_loss": {"value": 1.0, "unit": "MW"}
        }
      },
      {
        "id": "geared_train_gearbox",
        "kind": "gearbox.shaft.fixed_ratio",
        "parameters": {
          "speed_ratio": 3.0,
          "mechanical_efficiency": 0.95
        }
      },
      {
        "id": "grid",
        "kind": "sink.electrical.boundary"
      }
    ],
    "connections": [
      {
        "id": "combined_driver",
        "from": "combiner.load",
        "to": "train.driver",
        "kind": "shaft_link"
      },
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
      },
      {
        "id": "geared_train_link",
        "from": "geared_train.load_2",
        "to": "geared_train_gearbox.driver",
        "kind": "shaft_link"
      }
    ]
  },
  "cases": [{
    "id": "rated",
    "mode": "steady_state_design",
    "fixed_values": {
      "combiner.driver_a.W_dot": {"value": 190.0, "unit": "MW"},
      "combiner.driver_a.omega": 314.1592653589793,
      "combiner.driver_b.W_dot": {"value": 120.0, "unit": "MW"},
      "gearbox.driver.W_dot": {"value": 40.0, "unit": "MW"},
      "gearbox.driver.omega": 300.0,
      "geared_train.driver.W_dot": {"value": 50.0, "unit": "MW"},
      "geared_train.driver.omega": 300.0,
      "geared_train.load_1.W_dot": {"value": 10.0, "unit": "MW"},
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
    const double combined_shaft_power =
        0.995 * (190.0e6 + 120.0e6) - 0.5e6;
    require_near(
        value("train.driver.W_dot"),
        combined_shaft_power, 1.0e-6,
        "shaft combiner sums multiple stage drivers after losses");
    require_near(
        value("combiner.driver_b.omega"),
        314.1592653589793, 1.0e-10,
        "shaft combiner enforces common driver speed");
    const double generator_shaft_power =
        0.99 * combined_shaft_power - 40.0e6 - 1.0e6;
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
    require_near(
        value("gearbox.load.omega"), 100.0, 1.0e-12,
        "gearbox applies driver-to-load speed ratio");
    require_near(
        value("gearbox.load.W_dot"), 38.8e6, 1.0e-6,
        "gearbox transfers shaft power after mechanical loss");
    require_near(
        value("geared_train.load_1.omega"), 300.0, 1.0e-12,
        "geared shaft train preserves direct-load speed");
    require_near(
        value("geared_train_gearbox.load.omega"), 100.0, 1.0e-12,
        "geared shaft train applies its load speed ratio");
    require_near(
        value("geared_train_gearbox.load.W_dot"), 36.1e6, 1.0e-6,
        "geared shaft train accounts for shaft, gearbox, and fixed losses");
    const thermox::platform::GraphResultEvaluator evaluator(
        document, graph,
        thermox::physics::
            make_default_property_package_registry());
    const auto graph_result = evaluator.evaluate(result.x);
    const double combiner_loss =
        190.0e6 + 120.0e6 - combined_shaft_power;
    const double train_loss =
        (1.0 - 0.99) * combined_shaft_power + 1.0e6;
    const double gearbox_loss = (1.0 - 0.97) * 40.0e6;
    const double geared_train_loss = 50.0e6 - 10.0e6 - 36.1e6;
    require_near(
        require_result_value(
            require_component_result(
                graph_result, "combiner")
                .metrics,
            "net_energy_flow"),
        combiner_loss, 1.0e-6,
        "shaft-combiner metric attributes mechanical and fixed loss");
    require_near(
        require_result_value(
            require_component_result(
                graph_result, "train")
                .metrics,
            "net_energy_flow"),
        train_loss, 1.0e-6,
        "shaft-train metric attributes mechanical and fixed loss");
    require_near(
        require_result_value(
            require_component_result(
                graph_result, "generator")
                .metrics,
            "net_energy_flow"),
        0.02 * generator_shaft_power, 1.0e-6,
        "generator metric attributes conversion loss");
    require_near(
        require_result_value(
            graph_result.system_balances,
            "net_boundary_energy_flow"),
        combiner_loss + train_loss +
            gearbox_loss + geared_train_loss +
            0.02 * generator_shaft_power,
        1.0e-6,
        "system boundary energy equals attributed conversion losses");
}

void test_transient_compiler_expands_assemblies() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "assembled_storage",
    "media": [],
    "components": [{"id": "heater", "kind": "source.heat.boundary"}],
    "assemblies": [{
      "id": "storage_module",
      "ports": [{"name": "heat", "endpoint": "store.thermal"}],
      "components": [{
        "id": "store", "kind": "storage.thermal.lumped",
        "parameters": {"thermal_capacity": {"value": 2.0, "unit": "MJ/K"}}
      }],
      "connections": []
    }],
    "connections": [{
      "id": "charging_heat", "kind": "heat_link",
      "from": "heater.outlet", "to": "storage_module.heat"
    }]
  },
  "cases": [{
    "id": "charge", "mode": "dynamic_transient",
    "fixed_values": {
      "heater.outlet.Q_dot": {"value": 1.0, "unit": "MW"}
    },
    "initial_guesses": {
      "storage_module/store.temperature": {"value": 300.0, "unit": "K"}
    }
  }]
})json");
    const auto graph =
        thermox::platform::compile_transient_model_graph(
            document,
            thermox::platform::make_default_component_registry(),
            "charge");
    require(
        std::find(
            graph.problem.variable_names.begin(),
            graph.problem.variable_names.end(),
            "storage_module/store.temperature") !=
            graph.problem.variable_names.end(),
        "transient compilation must expand assembly hierarchy");
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
        test_fixed_boundary_continuation_is_explicit_and_reaches_target();
        test_hierarchical_assembly_composes_compressor_stages();
        test_map_driven_compressor_solves_bound_operating_point();
        test_map_continuation_recovers_out_of_domain_flow_guess();
        test_map_driven_turbine_solves_bound_operating_point();
        test_generic_model_solves_ideal_gas_turbine_residuals();
        test_generic_model_solves_two_inlet_mixer();
        test_generic_model_solves_two_outlet_splitter();
        test_fluid_junctions_compile_in_transient_graphs();
        test_generic_model_solves_isenthalpic_valve();
        test_nonflashing_liquid_orifice_solves_flow_and_guards_flashing();
        test_compressible_orifice_transitions_to_choked_flow();
        test_actuated_liquid_valve_scales_area_with_command();
        test_actuated_valve_composes_with_dynamic_control_lag();
        test_return_bend_fixed_loss_uses_fluid_density();
        test_homogeneous_gravity_pipe_supports_reverse_flow_and_transient();
        test_constant_slip_two_phase_pipe_closes_void_fraction_pressure_balance();
        test_darcy_weisbach_pipe_uses_transport_properties();
        test_darcy_weisbach_pipe_exposes_ambient_heat_boundary();
        test_equilibrium_flash_separator_closes_phase_split();
        test_material_connector_and_frozen_transport();
        test_material_mixer_and_fixed_fraction_splitter();
        test_map_driven_material_cross_bleed();
        test_perfect_gas_mach_scaled_material_duct();
        test_perfect_gas_convergent_material_nozzle();
        test_material_thermochemistry_resolves_on_demand();
        test_material_boundary_temperature_specification();
        test_adiabatic_equilibrium_combustor();
        test_material_compressor_and_turbine();
        test_map_driven_material_turbomachinery();
        test_fixed_composition_source_allows_map_solved_flow();
        test_generic_model_solves_cross_medium_fixed_duty_heat_exchanger();
        test_material_fluid_heat_exchangers();
        test_generic_model_solves_counterflow_ua_heat_exchanger();
        test_dynamic_heat_exchanger_cell_has_conservative_steady_and_transient_forms();
        test_dynamic_material_fluid_cell_conserves_species_and_energy();
        test_two_cell_counterflow_exchanger_composes_and_conserves();
        test_if97_fixed_quality_evaporator_and_condenser();
        test_if97_rankine_graph_regression();
        test_generic_model_solves_if97_pump();
        test_generic_model_solves_supercritical_co2_compressor();
        test_generic_model_compiler_rejects_unregistered_component_kind();
        test_generic_model_compiler_rejects_bad_port_contract();
        test_generic_model_compiler_rejects_unknown_case_variable();
        test_compiler_reports_under_and_over_specification();
        test_compiler_reports_square_structural_singularity();
        test_component_property_capabilities_are_validated();
        test_transient_model_compiles_and_integrates_lumped_storage();
        test_transient_model_integrates_rigid_fluid_volume();
        test_transient_fluid_volume_closes_with_real_fluid_backends();
        test_heos_rigid_volume_crosses_both_saturation_boundaries();
        test_dynamic_equilibrium_drum_conserves_inventory();
        test_bounded_pi_controller_prevents_integrator_windup();
        test_closed_loop_drum_feed_control_composes_platform_domains();
        test_shaft_train_and_generator_close_power_balance();
        test_transient_compiler_expands_assemblies();
        test_transient_compiler_rejects_steady_only_components();
        test_transient_compiler_rejects_fixed_differential_state();
    } catch (const std::exception& ex) {
        std::cerr << "test failure: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "thermox_platform_tests passed\n";
    return 0;
}
