#include "thermox/service/serialization.hpp"
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
            thermox::service::catalog_schema_v1,
        "catalog contract must be versioned");
    require(
        !response.fingerprint.empty(),
        "catalog must have a deterministic fingerprint");
    require(
        response.components.size() == 18,
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
            compressor->parameters.size() == 2,
        "catalog must expose ports and parameter forms");
    const auto if97 = std::find_if(
        response.property_backends.begin(),
        response.property_backends.end(),
        [](const auto& backend) {
            return backend.backend == "water_steam_if97";
        });
    require(
        if97 != response.property_backends.end() &&
            if97->implementation_name == "water-steam-if97" &&
            if97->implementation_version == "0.4.0" &&
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
        response.connector_domains.size() == 5,
        "catalog must expose connector contracts");
    const auto json =
        thermox::service::serialize_catalog_response_json(response);
    require(
        json.find("\"schema_version\": \"thermox.catalog/v1\"") !=
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
        response.canonical_model_json.find("thermox.model/v1") !=
            std::string::npos,
        "canonical model must retain its schema");

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
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "unknown_component",
    "media": [],
    "components": [{
      "id": "custom",
      "kind": "not.registered",
      "ports": {
        "signal": {
          "domain": "signal",
          "direction": "out"
        }
      }
    }],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "custom.signal.value": 1.0
    }
  }]
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

void test_component_version_is_enforced() {
    thermox::service::SimulationService service;
    thermox::service::ValidateModelRequest request;
    request.model_json = R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "version_mismatch",
    "media": [{
      "id": "water",
      "backend": "water_steam_if97",
      "substance": "Water"
    }],
    "components": [{
      "id": "source",
      "kind": "source.fluid.boundary",
      "version": "99.0.0",
      "ports": {
        "outlet": {
          "domain": "fluid",
          "medium": "water",
          "direction": "out"
        }
      }
    }],
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

void test_injectable_native_runtime() {
    auto components =
        thermox::platform::make_default_component_registry();
    thermox::platform::ComponentModelDescriptor descriptor;
    descriptor.kind = "sensor.signal.custom";
    descriptor.version = "0.1.0";
    descriptor.ports = {{"signal", "signal", "out"}};
    components.register_model(
        std::make_shared<
            thermox::platform::MetadataComponentModel>(
            descriptor));
    auto runtime = thermox::service::make_simulation_runtime(
        std::move(components),
        thermox::physics::
            make_default_property_package_registry());
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

    thermox::service::ValidateModelRequest request;
    request.model_json = R"json({
  "schema_version": "thermox.model/v1",
  "model": {
    "id": "custom_runtime_model",
    "media": [],
    "components": [{
      "id": "sensor",
      "kind": "sensor.signal.custom",
      "ports": {
        "signal": {
          "domain": "signal",
          "direction": "out"
        }
      }
    }],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "sensor.signal.value": 42.0
    }
  }]
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
            thermox::service::result_schema_v1,
        "steady result contract must be versioned");
    require(
        response.metadata.solver_contract == "thermox.newton/v1",
        "steady result must record solver contract");
    require(
        !response.metadata.catalog_fingerprint.empty(),
        "steady result must record runtime catalog fingerprint");
    require(
        !response.metadata.components.empty() &&
            !response.metadata.components.front()
                 .implementation_version.empty(),
        "component provenance must include implementation version");
    require(
        !response.metadata.media.empty() &&
            response.metadata.media.front().package == "ideal-gas" &&
            response.metadata.media.front().package_version ==
                "1.0.0",
        "medium provenance must include resolved package version");
    require(
        response.diagnostics.converged && !response.variables.empty(),
        "steady result must contain converged variables");
    require(
        !response.fluid_ports.empty(),
        "steady result must contain evaluated fluid ports");

    const auto json =
        thermox::service::serialize_steady_response_json(response);
    require(
        json.find("\"status\": \"succeeded\"") !=
            std::string::npos,
        "steady JSON must expose service status");
    require(
        json.find("\"schema_version\": \"thermox.result/v1\"") !=
            std::string::npos,
        "steady JSON must expose result schema");
}

void test_transient_service() {
    thermox::service::SimulationService service;
    thermox::service::TransientSimulationRequest request;
    request.model_json = read_source_file(
        "core/examples/lumped_thermal_storage.json");
    request.case_id = "charge";
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
        response.metadata.solver_contract ==
            "thermox.dae-bdf1/v1",
        "transient result must record solver contract");
    require(
        response.diagnostics.success &&
            !response.trajectory.empty(),
        "transient result must contain a successful trajectory");
    require(
        std::abs(response.trajectory.back().time - 0.2) < 1.0e-12,
        "transient service must honor requested end time");
    require(
        response.trajectory.back().state.size() ==
            response.variable_names.size(),
        "trajectory state must match service variable contract");

    const auto json =
        thermox::service::serialize_transient_response_json(response);
    require(
        json.find("\"trajectory\": [") != std::string::npos,
        "transient JSON must expose trajectory");
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
        test_catalog_discovery();
        test_validation_and_canonicalization();
        test_compile_aware_validation_diagnostics();
        test_component_version_is_enforced();
        test_injectable_native_runtime();
        test_steady_service();
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
