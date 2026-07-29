#include "thermox/service/serialization.hpp"
#include "thermox/service/in_memory_artifacts.hpp"
#include "thermox/service/native_runtime.hpp"
#include "thermox/service/simulation_service.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string read_source_file(const std::string& relative_path) {
    const std::string path =
        std::string(THERMOX_SOURCE_DIR) + "/" + relative_path;
    std::ifstream input(path);
    require(static_cast<bool>(input), "cannot read test model: " + path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

const thermox::service::ComponentResult& require_component_result(
    const thermox::service::GraphResult& graph,
    const std::string& component_id) {
    const auto component = std::find_if(
        graph.components.begin(), graph.components.end(),
        [&](const auto& candidate) {
            return candidate.component_id == component_id;
        });
    require(
        component != graph.components.end(),
        "missing service graph component: " + component_id);
    return *component;
}

const thermox::service::PortResult& require_port_result(
    const thermox::service::GraphResult& graph,
    const std::string& component_id,
    const std::string& port_name) {
    const auto& component =
        require_component_result(graph, component_id);
    const auto port = std::find_if(
        component.ports.begin(), component.ports.end(),
        [&](const auto& candidate) {
            return candidate.port_name == port_name;
        });
    require(
        port != component.ports.end(),
        "missing service graph port: " +
            component_id + "." + port_name);
    return *port;
}

const thermox::service::ResultValue& require_result_value(
    const std::vector<thermox::service::ResultValue>& values,
    const std::string& name) {
    const auto value = std::find_if(
        values.begin(), values.end(),
        [&](const auto& candidate) {
            return candidate.name == name;
        });
    require(
        value != values.end(),
        "missing service result value: " + name);
    return *value;
}

thermox::service::PerformanceMapArtifactInput compressor_map() {
    using namespace thermox::service;
    PerformanceMapArtifactInput artifact;
    artifact.id = "request-compressor-map";
    artifact.schema_version = "thermox.performance_map/v1";
    artifact.revision = "service-test-1";
    artifact.checksum_sha256 = std::string(64, 'a');
    PerformanceMapPayloadInput map;
    map.primary_variable = {
        "corrected_mass_flow", "mass_flow"};
    map.family_variable = {
        "corrected_speed", "angular_speed"};
    map.output_variables = {
        {"pressure_ratio", "dimensionless"},
        {"isentropic_efficiency", "dimensionless"},
    };
    map.curves = {
        {250.0,
         {{80.0, {10.0, 0.85}},
          {120.0, {10.0, 0.85}}}},
        {400.0,
         {{80.0, {10.0, 0.85}},
          {120.0, {10.0, 0.85}}}},
    };
    artifact.map = std::move(map);
    return artifact;
}

thermox::service::EngineeringArtifactReference map_reference(
    const thermox::service::PerformanceMapArtifactInput& artifact) {
    return {
        artifact.id,
        "thermox.performance_map",
        artifact.schema_version,
        artifact.revision,
        artifact.checksum_sha256,
    };
}

std::string mapped_compressor_model() {
    return R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "request_scoped_map",
    "media": [{
      "id": "air",
      "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "components": [{
      "id": "compressor",
      "kind": "compressor.fluid.performance_map",
      "artifacts": {"performance_map": "request-compressor-map"},
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
})json";
}

void test_request_scoped_performance_map_artifacts() {
    thermox::service::SimulationService service;
    const auto catalog_before = service.get_catalog();

    thermox::service::ValidateModelRequest validation;
    validation.model_json = mapped_compressor_model();
    validation.case_id = "operating_point";
    const auto missing = service.validate_model(validation);
    require(
        !missing.succeeded(),
        "a model must not resolve a request artifact from global state");

    validation.artifacts.performance_maps.push_back(
        compressor_map());
    const auto valid = service.validate_model(validation);
    require(
        valid.succeeded(),
        "validation must resolve a request-scoped performance map");

    thermox::service::SteadySimulationRequest request;
    request.model_json = validation.model_json;
    request.case_id = validation.case_id;
    request.artifacts = validation.artifacts;
    const auto response = service.run_steady(request);
    require(
        response.succeeded(),
        "steady execution must use the request-scoped map");
    require(
        response.metadata.artifacts.size() == 1 &&
            response.metadata.artifacts.front().id ==
                "request-compressor-map" &&
            response.metadata.artifacts.front().checksum_sha256 ==
                std::string(64, 'a'),
        "execution metadata must retain engineering artifact provenance");
    const auto serialized =
        thermox::service::serialize_steady_response_json(response);
    require(
        serialized.find("\"artifacts\": [") !=
                std::string::npos &&
            serialized.find("\"request-compressor-map\"") !=
                std::string::npos,
        "result JSON must expose artifact provenance");

    const auto catalog_after = service.get_catalog();
    require(
        catalog_after.fingerprint == catalog_before.fingerprint,
        "request artifacts must not mutate the runtime catalog");
    thermox::service::ValidateModelRequest isolated;
    isolated.model_json = validation.model_json;
    isolated.case_id = validation.case_id;
    require(
        !service.validate_model(isolated).succeeded(),
        "request artifacts must not leak into later requests");
}

void test_resolved_performance_map_artifacts() {
    const auto artifact = compressor_map();
    const auto resolver =
        thermox::service::
            make_in_memory_engineering_artifact_resolver(
                {artifact});
    thermox::service::SimulationService service(
        thermox::service::make_default_simulation_runtime(),
        resolver);

    thermox::service::SteadySimulationRequest request;
    request.model_json = mapped_compressor_model();
    request.case_id = "operating_point";
    request.artifacts.references.push_back(
        map_reference(artifact));
    const auto response = service.run_steady(request);
    require(
        response.succeeded() &&
            response.metadata.artifacts.size() == 1 &&
            response.metadata.artifacts.front().id == artifact.id,
        "a checksum-pinned artifact reference must resolve into "
        "the scoped execution registry");

    auto mismatched = request;
    mismatched.artifacts.references.front().revision =
        "different-revision";
    const auto mismatch = service.run_steady(mismatched);
    require(
        mismatch.status ==
                thermox::service::OperationStatus::invalid_request &&
            mismatch.error.code == "invalid_artifacts" &&
            mismatch.error.stage == "artifacts",
        "resolved artifact metadata must exactly match its reference");

    thermox::service::SimulationService no_resolver;
    const auto unavailable = no_resolver.run_steady(request);
    require(
        unavailable.status ==
                thermox::service::OperationStatus::invalid_request &&
            unavailable.error.code == "invalid_artifacts",
        "artifact references must fail explicitly without a resolver");
}

void test_request_contract_validation() {
    thermox::service::SimulationService service;
    thermox::service::SteadySimulationRequest request;
    request.schema_version = "thermox.command/v999";
    request.model_json = "{}";
    const auto response = service.run_steady(request);
    require(
        response.status ==
            thermox::service::OperationStatus::invalid_request,
        "unsupported command schema must be rejected");
    require(
        response.error.schema_version ==
            thermox::service::error_schema_v1,
        "service error contract must be versioned");
    require(
        response.error.stage == "request",
        "request error must identify its stage");
}

void test_catalog_discovery() {
    thermox::service::SimulationService service;
    const auto response = service.get_catalog();
    require(response.succeeded(), "default catalog must load");
    require(
        response.schema_version ==
            thermox::service::catalog_schema_v2,
        "catalog contract must be versioned");
    require(
        !response.fingerprint.empty(),
        "catalog must have a deterministic fingerprint");
    require(
        response.components.size() == 38,
        "service must expose the complete component registry");
    const auto compressor = std::find_if(
        response.components.begin(),
        response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "compressor.fluid.isentropic_efficiency";
        });
    require(
        compressor != response.components.end(),
        "catalog must expose fluid compressor");
    require(
        compressor->ports.size() == 3 &&
            compressor->parameters.size() == 2 &&
            std::all_of(
                compressor->ports.begin(),
                compressor->ports.end(),
                [](const auto& port) {
                    return port.maximum_connections == 1;
                }),
        "catalog must expose ports, cardinality, and parameter forms");
    const auto mapped_compressor = std::find_if(
        response.components.begin(),
        response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "compressor.fluid.performance_map";
        });
    require(
        mapped_compressor != response.components.end() &&
            mapped_compressor->artifacts.size() == 1 &&
            mapped_compressor->artifacts.front().role ==
                "performance_map" &&
            mapped_compressor->artifacts.front().artifact_type ==
                thermox::platform::performance_map_artifact_type,
        "catalog must expose mapped compressor artifact contract");
    const auto variable_geometry_compressor = std::find_if(
        response.components.begin(),
        response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "compressor.material.variable_geometry_map";
        });
    require(
        variable_geometry_compressor !=
                response.components.end() &&
            variable_geometry_compressor->artifacts.size() == 1 &&
            std::any_of(
                variable_geometry_compressor->parameters.begin(),
                variable_geometry_compressor->parameters.end(),
                [](const auto& parameter) {
                    return parameter.name == "geometry_setting" &&
                        parameter.dimension == "angle";
                }),
        "catalog must expose variable-geometry map and angle "
        "contracts");
    const auto mapped_turbine = std::find_if(
        response.components.begin(),
        response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "turbine.fluid.performance_map";
        });
    require(
        mapped_turbine != response.components.end() &&
            mapped_turbine->artifacts.size() == 1 &&
            mapped_turbine->artifacts.front().role ==
                "performance_map",
        "catalog must expose mapped turbine artifact contract");
    const auto combustor = std::find_if(
        response.components.begin(),
        response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "combustor.material.adiabatic_equilibrium";
        });
    require(
        combustor != response.components.end() &&
            combustor->required_thermochemistry_capabilities ==
                std::vector<std::string>{"equilibrium_hp"},
        "catalog must expose combustor thermochemistry contract");
    const auto material_splitter = std::find_if(
        response.components.begin(),
        response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "junction.material.splitter.fixed_fraction";
        });
    require(
        material_splitter != response.components.end() &&
            material_splitter->parameters.size() == 1 &&
            material_splitter->parameters.front().name ==
                "outlet_a_fraction",
        "catalog must expose material splitter calibration contract");
    const auto storage = std::find_if(
        response.components.begin(),
        response.components.end(),
        [](const auto& component) {
            return component.kind == "storage.thermal.lumped";
        });
    require(
        storage != response.components.end() &&
            storage->internal_variables.size() == 1 &&
            storage->internal_variables.front().name ==
                "temperature" &&
            storage->internal_variables.front().dimension ==
                "temperature" &&
            storage->internal_variables.front().kind ==
                "differential",
        "catalog must expose graph-result internal state metadata");
    const auto if97 = std::find_if(
        response.property_backends.begin(),
        response.property_backends.end(),
        [](const auto& backend) {
            return backend.backend == "water_steam_if97";
        });
    require(
        if97 != response.property_backends.end() &&
            if97->implementation_name == "coolprop-if97" &&
            if97->implementation_version == "8.0.0" &&
            std::find(
                if97->supported_substances.begin(),
                if97->supported_substances.end(),
                "Water") != if97->supported_substances.end() &&
            std::find(
                if97->capabilities.begin(),
                if97->capabilities.end(),
                "saturation_p") != if97->capabilities.end(),
        "catalog must expose property compatibility metadata");
    require(
        response.connector_domains.size() == 7,
        "catalog must expose connector contracts");
    const auto json =
        thermox::service::serialize_catalog_response_json(response);
    require(
        json.find("\"schema_version\": \"thermox.catalog/v2\"") !=
            std::string::npos,
        "catalog JSON must expose its schema");
}

void test_validation_and_canonicalization() {
    thermox::service::SimulationService service;
    thermox::service::ValidateModelRequest request;
    request.model_json =
        read_source_file("core/examples/air_compressor.json");
    const auto response = service.validate_model(request);
    require(response.succeeded(), "valid model must validate");
    require(
        response.model.model_id == "air_compressor",
        "validation must return model identity");
    require(
        response.compilation.compiled &&
            response.compilation.mode == "steady" &&
            response.compilation.variable_count ==
                response.compilation.equation_count,
        "validation must compile and structurally analyze the model");
    require(
        response.canonical_model_json.find("thermox.model/v2") !=
            std::string::npos,
        "canonical model must retain its schema");
    require(
        response.canonical_model_json.find("\"calibrations\"") !=
                std::string::npos &&
            response.canonical_model_json.find(
                "components.compressor.parameters.eta_is") !=
                std::string::npos,
        "canonical model must retain calibration contracts");

    thermox::service::ValidateModelRequest round_trip;
    round_trip.model_json = response.canonical_model_json;
    const auto reparsed = service.validate_model(round_trip);
    require(
        reparsed.succeeded(),
        "canonical service model must be parseable");
    require(
        reparsed.model.model_id == response.model.model_id,
        "canonical round trip must preserve model identity");

    thermox::service::SteadySimulationRequest simulation;
    simulation.model_json = response.canonical_model_json;
    simulation.case_id = "design";
    const auto solved = service.run_steady(simulation);
    require(
        solved.succeeded(),
        "canonical model must preserve dimensional solve semantics: " +
            solved.error.message);
}

void test_compile_aware_validation_diagnostics() {
    thermox::service::SimulationService service;
    thermox::service::ValidateModelRequest request;
    request.model_json = R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "unknown_component",
    "media": [],
    "components": [
      {
        "id": "custom",
        "kind": "not.registered"
      }
    ],
    "connections": []
  },
  "cases": [
    {
      "id": "design",
      "mode": "steady_state_design",
      "fixed_values": {
        "custom.signal.value": 1.0
      }
    }
  ]
})json";
    const auto response = service.validate_model(request);
    require(
        !response.succeeded() &&
            !response.diagnostics.empty(),
        "unknown component must fail compile-aware validation");
    require(
        response.diagnostics.front().code ==
            "unknown_component_type" &&
            response.diagnostics.front().stage == "compilation",
        "validation must return a stable catalog diagnostic");
}

void test_calibration_observation_contract_validation() {
    thermox::service::SimulationService service;
    thermox::service::ValidateModelRequest request;
    request.model_json =
        read_source_file("core/examples/air_compressor.json");
    const std::string valid_target{
        "compressor.shaft.W_dot"};
    const auto target = request.model_json.rfind(valid_target);
    require(
        target != std::string::npos,
        "calibration fixture must contain its observation target");
    request.model_json.replace(
        target, valid_target.size(),
        "compressor.shaft.unknown_power");

    const auto response = service.validate_model(request);
    require(
        response.status ==
            thermox::service::OperationStatus::invalid_model,
        "unknown calibration result target must invalidate model");
    require(
        response.error.message.find(
            "references unknown result value") !=
            std::string::npos,
        "calibration validation must identify the bad result target");
    require(
        !response.diagnostics.empty() &&
            response.diagnostics.front().code ==
                "invalid_calibration_observation",
        "bad calibration observation must have a stable diagnostic code");
}

void test_bounded_calibration_service() {
    thermox::service::SimulationService service;
    thermox::service::CalibrationRequest request;
    request.model_json =
        read_source_file("core/examples/air_compressor.json");
    request.calibration_id = "acceptance_fit";
    request.solver.max_iterations = 16;

    const auto response = service.run_calibration(request);
    require(
        response.succeeded(),
        "bounded calibration must succeed: " +
            response.error.message);
    require(
        response.parameters.size() == 1 &&
            response.observations.size() == 2,
        "multi-case calibration must report fitted parameter and residuals");
    require(
        response.diagnostics.final_objective <
            response.diagnostics.initial_objective,
        "calibration must reduce the weighted objective");
    require(
        response.parameters.front().fitted_value_si >= 0.75 &&
            response.parameters.front().fitted_value_si <= 0.95,
        "fitted parameter must remain inside declared bounds");
    require(
        !response.fitted_model_json.empty(),
        "calibration must return a reusable fitted model");
    require(
        response.metadata.solver.contract_version ==
            "thermox.coordinate-search/v1" &&
            std::any_of(
                response.metadata.solver.settings.begin(),
                response.metadata.solver.settings.end(),
                [](const auto& setting) {
                    return setting.name ==
                        "minimum_continuation_fraction";
                }),
        "calibration provenance must record continuation settings");
    const auto json =
        thermox::service::serialize_calibration_response_json(
            response);
    require(
        json.find("\"normalized_residual\":") !=
                std::string::npos &&
            json.find("\"fitted_model_json\":") !=
                std::string::npos,
        "calibration JSON must expose residuals and fitted model");
}

void test_component_version_is_enforced() {
    thermox::service::SimulationService service;
    thermox::service::ValidateModelRequest request;
    request.model_json = R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "version_mismatch",
    "media": [
      {
        "id": "water",
        "backend": "water_steam_if97",
        "substance": "Water"
      }
    ],
    "components": [
      {
        "id": "source",
        "kind": "source.fluid.boundary",
        "version": "99.0.0",
        "media": {
          "outlet": "water"
        }
      }
    ],
    "connections": []
  },
  "cases": []
})json";
    const auto response = service.validate_model(request);
    require(
        !response.succeeded() &&
            !response.diagnostics.empty() &&
            response.diagnostics.front().code ==
                "component_version_mismatch",
        "requested component version must be enforced");
}

void test_property_and_connector_versions_are_enforced() {
    thermox::service::SimulationService service;
    thermox::service::ValidateModelRequest property_request;
    property_request.model_json = R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "property_version_mismatch",
    "media": [{
      "id": "water",
      "backend": "water_steam_if97",
      "substance": "Water",
      "package_version": "99.0.0"
    }],
    "components": [{
      "id": "source",
      "kind": "source.fluid.boundary",
      "media": {"outlet": "water"}
    }],
    "connections": []
  },
  "cases": []
})json";
    const auto property_response =
        service.validate_model(property_request);
    require(
        !property_response.succeeded() &&
            !property_response.diagnostics.empty() &&
            property_response.diagnostics.front().code ==
                "property_package_version_mismatch",
        "requested property package version must be enforced");

    thermox::service::ValidateModelRequest connector_request;
    connector_request.model_json = R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "connector_version_mismatch",
    "media": [],
    "components": [
      {"id": "source", "kind": "source.heat.boundary"},
      {"id": "sink", "kind": "sink.heat.boundary"}
    ],
    "connections": [{
      "id": "link",
      "from": "source.outlet",
      "to": "sink.inlet",
      "kind": "heat_link",
      "contract_version": "thermox.connector.heat/v99"
    }]
  },
  "cases": []
})json";
    const auto connector_response =
        service.validate_model(connector_request);
    require(
        !connector_response.succeeded() &&
            !connector_response.diagnostics.empty() &&
            connector_response.diagnostics.front().code ==
                "connector_contract_version_mismatch",
        "requested connector contract version must be enforced");
}

void test_connection_contract_diagnostic() {
    thermox::service::SimulationService service;
    thermox::service::ValidateModelRequest request;
    request.model_json = R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "wrong_link",
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
        "id": "link",
        "from": "source.outlet",
        "to": "sink.inlet",
        "kind": "fluid_link"
      }
    ]
  },
  "cases": []
})json";
    const auto response = service.validate_model(request);
    require(
        !response.succeeded() &&
            !response.diagnostics.empty() &&
            response.diagnostics.front().code ==
                "incompatible_connection_kind",
        "connection-domain mismatch must have a stable diagnostic");
}

void test_injectable_native_runtime() {
    auto components =
        thermox::platform::make_default_component_registry();
    thermox::platform::ComponentModelDescriptor descriptor;
    descriptor.kind = "sensor.signal.custom";
    descriptor.version = "0.1.0";
    descriptor.ports = {
        {"signal", "signal", "out"},
        {"command", "control", "in"},
    };
    components.register_model(
        std::make_shared<
            thermox::platform::MetadataComponentModel>(
            descriptor));
    thermox::physics::ThermochemistryPackageRegistry chemistry;
    chemistry.register_backend(
        {"catalog_test", "catalog-test", "1.0.0",
         {thermox::physics::ThermochemistryCapability::
              equilibrium_hp}},
        [](std::string_view, std::string_view) {
            return std::shared_ptr<
                const thermox::physics::
                    ThermochemistryPackage>{};
        });
    auto runtime = thermox::service::make_simulation_runtime(
        std::move(components),
        thermox::physics::
            make_default_property_package_registry(),
        {}, std::move(chemistry));
    thermox::service::SimulationService service(runtime);
    const auto catalog = service.get_catalog();
    require(
        std::any_of(
            catalog.components.begin(),
            catalog.components.end(),
            [](const auto& component) {
                return component.kind ==
                    "sensor.signal.custom";
            }),
        "custom runtime component must reach service catalog");
    require(
        catalog.thermochemistry_backends.size() == 1 &&
            catalog.thermochemistry_backends.front().backend ==
                "catalog_test" &&
            catalog.thermochemistry_backends.front()
                    .capabilities.front() ==
                "equilibrium_hp",
        "custom thermochemistry backend must reach service catalog");

    thermox::service::ValidateModelRequest request;
    request.model_json = R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "custom_runtime_model",
    "media": [],
    "components": [
      {
        "id": "sensor",
        "kind": "sensor.signal.custom"
      }
    ],
    "connections": []
  },
  "cases": [
    {
      "id": "design",
      "mode": "steady_state_design",
      "fixed_values": {
        "sensor.signal.value": 42.0,
        "sensor.command.value": 0.5
      }
    }
  ]
})json";
    const auto validation = service.validate_model(request);
    require(
        validation.succeeded(),
        "custom runtime model must compile through service: " +
            validation.error.message);
    require(
        validation.compilation.catalog_fingerprint ==
            catalog.fingerprint,
        "validation must identify the exact runtime catalog");

    thermox::service::SteadySimulationRequest simulation;
    simulation.model_json = request.model_json;
    simulation.case_id = "design";
    const auto solved = service.run_steady(simulation);
    require(
        solved.succeeded(),
        "custom signal/control graph must solve");
    require(
        require_port_result(
            solved.graph, "sensor", "signal")
                .domain == "signal" &&
            require_port_result(
                solved.graph, "sensor", "command")
                .domain == "control",
        "custom runtime graph must expose signal and control results");
}

void test_steady_service() {
    thermox::service::SimulationService service;
    thermox::service::SteadySimulationRequest request;
    request.model_json =
        read_source_file("core/examples/air_compressor.json");
    request.case_id = "design";
    const auto response = service.run_steady(request);
    require(
        response.succeeded(),
        "steady service execution failed: " + response.error.message);
    require(
        response.metadata.operation == "steady",
        "steady result must identify operation");
    require(
        response.metadata.result_schema_version ==
            thermox::service::result_schema_v3,
        "steady result contract must be versioned");
    require(
        response.metadata.platform_version == "0.2.0",
        "steady result must identify the platform build");
    require(
        response.metadata.solver.contract_version ==
            "thermox.newton/v1" &&
            !response.metadata.solver.settings.empty(),
        "steady result must record solver contract");
    require(
        !response.metadata.catalog_fingerprint.empty(),
        "steady result must record runtime catalog fingerprint");
    require(
        !response.metadata.components.empty() &&
            response.metadata.components.front()
                    .requested_version == "1.0.0" &&
            !response.metadata.components.front()
                 .resolved_version.empty(),
        "component provenance must include requested and resolved versions");
    require(
        !response.metadata.media.empty() &&
            response.metadata.media.front().package == "ideal-gas" &&
            response.metadata.media.front()
                    .requested_package_version == "1.0.0" &&
            response.metadata.media.front()
                    .resolved_package_version ==
                "1.0.0",
        "medium provenance must include requested and resolved package versions");
    require(
        response.metadata.connector_domains.size() == 7,
        "result provenance must include connector contracts");
    require(
        response.diagnostics.converged &&
            response.graph.components.size() == 1,
        "steady result must contain a graph-addressable result");
    const auto& compressor =
        require_component_result(response.graph, "compressor");
    require(
        compressor.ports.size() == 3,
        "steady graph must contain every component port domain");
    const auto& outlet =
        require_port_result(
            response.graph, "compressor", "outlet");
    require(
        outlet.domain == "fluid" &&
            outlet.primary_values.size() == 3 &&
            !outlet.derived_values.empty(),
        "fluid graph port must contain primary and derived values");
    const auto& shaft =
        require_port_result(
            response.graph, "compressor", "shaft");
    require(
        shaft.domain == "shaft" &&
            shaft.primary_values.size() == 2,
        "non-fluid graph ports must be first-class results");
    require(
        std::abs(require_result_value(
                     response.graph.system_balances,
                     "net_boundary_mass_flow")
                     .value_si) < 1.0e-9,
        "steady result must close external compressor mass flow");
    require(
        std::abs(require_result_value(
                     response.graph.system_balances,
                     "net_boundary_energy_flow")
                     .value_si) < 1.0e-5,
        "steady result must include unconnected shaft work in "
        "the system energy boundary");

    const auto json =
        thermox::service::serialize_steady_response_json(response);
    require(
        json.find("\"status\": \"succeeded\"") !=
            std::string::npos,
        "steady JSON must expose service status");
    require(
        json.find("\"schema_version\": \"thermox.result/v3\"") !=
            std::string::npos,
        "steady JSON must expose result schema");
    require(
        json.find("\"platform_version\": \"0.2.0\"") !=
                std::string::npos &&
            json.find("\"settings\": {") != std::string::npos,
        "steady JSON must serialize complete execution provenance");
}

void test_explicit_system_boundary_balance() {
    thermox::service::SimulationService service;
    thermox::service::SteadySimulationRequest request;
    request.model_json = R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "explicit_heat_boundary",
    "media": [],
    "components": [
      {"id": "source", "kind": "source.heat.boundary"},
      {"id": "sink", "kind": "sink.heat.boundary"}
    ],
    "connections": [{
      "id": "heat",
      "from": "source.outlet",
      "to": "sink.inlet",
      "kind": "heat_link"
    }]
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "source.outlet.Q_dot": {"value": 5.0, "unit": "MW"},
      "source.outlet.T": {"value": 400.0, "unit": "K"}
    }
  }]
})json";
    request.case_id = "design";
    const auto response = service.run_steady(request);
    require(
        response.succeeded(),
        "explicit boundary graph must solve: " +
            response.error.message);
    require(
        response.graph.system_balances.size() == 1 &&
            std::abs(require_result_value(
                         response.graph.system_balances,
                         "net_boundary_energy_flow")
                         .value_si) < 1.0e-9,
        "registered source and sink roles must close a connected "
        "system boundary");

    const auto catalog = service.get_catalog();
    const auto source = std::find_if(
        catalog.components.begin(), catalog.components.end(),
        [](const auto& component) {
            return component.kind == "source.heat.boundary";
        });
    require(
        source != catalog.components.end() &&
            source->system_boundary_role == "source",
        "catalog must expose registered system boundary semantics");
}

void test_transient_service() {
    thermox::service::SimulationService service;
    thermox::service::TransientSimulationRequest request;
    request.model_json = read_source_file(
        "core/examples/lumped_thermal_storage.json");
    request.case_id = "charge";
    thermox::service::ValidateModelRequest validation_request;
    validation_request.model_json = request.model_json;
    validation_request.case_id = request.case_id;
    const auto validation =
        service.validate_model(validation_request);
    require(
        validation.succeeded() &&
            validation.canonical_model_json.find(
                "\"parameter_overrides\"") !=
                std::string::npos &&
            validation.canonical_model_json.find(
                "components.store.parameters.thermal_capacity") !=
                std::string::npos,
        "canonical model must retain per-case parameter overrides");
    thermox::service::ValidateModelRequest round_trip;
    round_trip.model_json = validation.canonical_model_json;
    round_trip.case_id = request.case_id;
    require(
        service.validate_model(round_trip).succeeded(),
        "canonical case parameter overrides must round trip");
    request.solver.end_time = 0.2;
    request.solver.max_step = 0.05;
    const auto response = service.run_transient(request);
    require(
        response.succeeded(),
        "transient service execution failed: " +
            response.error.message);
    require(
        response.metadata.operation == "transient",
        "transient result must identify operation");
    require(
        response.metadata.solver.contract_version ==
            "thermox.dae-bdf1/v1",
        "transient result must record solver contract");
    const auto end_time_setting = std::find_if(
        response.metadata.solver.settings.begin(),
        response.metadata.solver.settings.end(),
        [](const auto& setting) {
            return setting.name == "end_time";
        });
    require(
        end_time_setting !=
                response.metadata.solver.settings.end() &&
            std::abs(end_time_setting->value - 0.2) < 1.0e-12,
        "transient provenance must record effective solver settings");
    require(
        response.diagnostics.success &&
            !response.trajectory.empty(),
        "transient result must contain a successful trajectory");
    require(
        std::abs(response.trajectory.back().time - 0.2) < 1.0e-12,
        "transient service must honor requested end time");
    require(
        response.trajectory.back().graph.components.size() == 2,
        "transient trajectory must use the graph result contract");
    const auto& store = require_component_result(
        response.trajectory.back().graph, "store");
    require(
        store.internal_values.size() == 1 &&
            store.internal_values.front().name ==
                "temperature" &&
            store.internal_values.front().has_derivative,
        "transient graph must expose internal state and derivative");

    const auto json =
        thermox::service::serialize_transient_response_json(response);
    require(
        json.find("\"trajectory\": [") != std::string::npos &&
            json.find("\"internal_values\": [") !=
                std::string::npos &&
            json.find("\"derivative_si_s\":") !=
                std::string::npos,
        "transient JSON must expose graph-native trajectory state");
}

void test_structured_compilation_failure() {
    thermox::service::SimulationService service;
    thermox::service::SteadySimulationRequest request;
    request.model_json = read_source_file(
        "core/examples/lumped_thermal_storage.json");
    request.case_id = "charge";
    const auto response = service.run_steady(request);
    require(
        response.status ==
            thermox::service::OperationStatus::compilation_failed,
        "mode mismatch must be a structured compilation failure");
    require(
        response.error.stage == "compilation" &&
            !response.error.message.empty(),
        "compilation failure must retain stage and message");
}

void test_invalid_solver_settings() {
    thermox::service::SimulationService service;
    thermox::service::TransientSimulationRequest request;
    request.model_json = read_source_file(
        "core/examples/lumped_thermal_storage.json");
    request.solver.end_time = request.solver.start_time;
    const auto response = service.run_transient(request);
    require(
        response.status ==
            thermox::service::OperationStatus::invalid_request,
        "invalid solver settings must be a request failure");
    require(
        response.error.code == "invalid_solver_settings",
        "invalid solver settings must have a stable error code");
}

}  // namespace

int main() {
    try {
        test_request_contract_validation();
        test_request_scoped_performance_map_artifacts();
        test_resolved_performance_map_artifacts();
        test_catalog_discovery();
        test_validation_and_canonicalization();
        test_compile_aware_validation_diagnostics();
        test_calibration_observation_contract_validation();
        test_bounded_calibration_service();
        test_component_version_is_enforced();
        test_property_and_connector_versions_are_enforced();
        test_connection_contract_diagnostic();
        test_injectable_native_runtime();
        test_steady_service();
        test_explicit_system_boundary_balance();
        test_transient_service();
        test_structured_compilation_failure();
        test_invalid_solver_settings();
        std::cout << "thermox service tests passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "thermox service tests failed: " << ex.what()
                  << "\n";
        return 1;
    }
}
