#include "thermox/platform/expression_component.hpp"
#include "thermox/service/in_memory_artifacts.hpp"
#include "thermox/service/native_runtime.hpp"
#include "thermox/service/result_projection.hpp"
#include "thermox/service/serialization.hpp"
#include "thermox/service/simulation_service.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
         {{70.0, {10.0, 0.85}},
          {120.0, {10.0, 0.85}}}},
        {400.0,
         {{70.0, {10.0, 0.85}},
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
            thermox::service::catalog_schema_v5,
        "catalog contract must be versioned");
    require(
        !response.fingerprint.empty(),
        "catalog must have a deterministic fingerprint");
    require(
        response.components.size() == 48,
        "service must expose the complete component registry");
    const auto rotor = std::find_if(
        response.components.begin(),
        response.components.end(),
        [](const auto& component) {
            return component.kind == "shaft.inertia.two_port";
        });
    require(
        rotor != response.components.end() &&
            rotor->supports_steady &&
            rotor->supports_transient &&
            rotor->internal_variables.size() == 2,
        "catalog must expose reusable shaft-inertia dynamics");
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
        compressor->template_kind == "compressor" &&
            compressor->display_name == "Compressor" &&
            compressor->category == "Turbomachinery" &&
            compressor->model_name == "Isentropic efficiency",
        "catalog must distinguish physical templates from "
        "calculation models");
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
    const auto return_bend = std::find_if(
        response.components.begin(),
        response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "fitting.fluid.return_bend.fixed_loss_coefficient";
        });
    require(
        return_bend != response.components.end() &&
            return_bend->template_kind ==
                "fitting.fluid.return_bend" &&
            return_bend->display_name ==
                "Return bend (180 deg)" &&
            return_bend->model_name ==
                "Fixed loss coefficient" &&
            return_bend->required_property_capabilities ==
                std::vector<std::string>{"state_ph"},
        "catalog must expose the return bend as a physical "
        "template with a density-aware calculation model");
    const auto mapped_compressor = std::find_if(
        response.components.begin(),
        response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "compressor.fluid.performance_map";
        });
    const auto has_default_map_scale =
        [&](const std::string& name) {
            return mapped_compressor !=
                    response.components.end() &&
                std::any_of(
                    mapped_compressor->parameters.begin(),
                    mapped_compressor->parameters.end(),
                    [&](const auto& parameter) {
                        return parameter.name == name &&
                            parameter.default_value_si ==
                                std::optional<double>{1.0};
                    });
        };
    require(
        mapped_compressor != response.components.end() &&
            mapped_compressor->artifacts.size() == 1 &&
            mapped_compressor->artifacts.front().role ==
                "performance_map" &&
            mapped_compressor->artifacts.front().artifact_type ==
                thermox::platform::performance_map_artifact_type &&
            has_default_map_scale("flow_capacity_scale") &&
            has_default_map_scale("pressure_ratio_scale") &&
            has_default_map_scale("efficiency_scale"),
        "catalog must expose mapped compressor artifact and "
        "correction contracts");
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
    const auto composition_source = std::find_if(
        response.components.begin(),
        response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "source.material.fixed_composition";
        });
    require(
        composition_source != response.components.end() &&
            composition_source->system_boundary_role ==
                "source" &&
            composition_source->parameters.size() == 1 &&
            composition_source->parameters.front().name ==
                "mass_fraction[{species}]",
        "catalog must expose the species-keyed composition "
        "boundary contract");
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
    const auto pressure_units = std::find_if(
        response.unit_dimensions.begin(),
        response.unit_dimensions.end(),
        [](const auto& dimension) {
            return dimension.dimension == "pressure";
        });
    require(
        pressure_units != response.unit_dimensions.end() &&
            pressure_units->canonical_unit == "Pa" &&
            pressure_units->engineering_display.symbol == "bar" &&
            pressure_units->engineering_display.scale_from_si ==
                1.0e-5,
        "catalog must expose authoritative unit display metadata");
    const auto json =
        thermox::service::serialize_catalog_response_json(response);
    require(
        json.find("\"schema_version\": \"thermox.catalog/v5\"") !=
            std::string::npos,
        "catalog JSON must expose its schema");
    require(
        json.find("\"unit_dimensions\": [") !=
            std::string::npos,
        "catalog JSON must serialize unit dimensions");
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

#ifdef THERMOX_TEST_HAS_CANTERA
void test_cantera_brayton_integration_benchmark() {
    thermox::service::SimulationService service;
    thermox::service::SteadySimulationRequest request;
    request.model_json =
        read_source_file("core/examples/brayton_cantera.json");
    request.case_id = "design";
    request.solver.continuation_enabled = true;
    const auto response = service.run_steady(request);

    require(
        response.succeeded(),
        "Cantera Brayton benchmark must solve: " +
            response.error.message);
    require(
        response.diagnostics.converged &&
            response.diagnostics.final_residual_norm < 1.0e-10,
        "Cantera Brayton benchmark must close its normalized "
        "equations");
    require(
        response.continuation.converged &&
            response.continuation.reached_parameter == 1.0,
        "Cantera Brayton benchmark must reach the target problem");

    const auto& compressor_shaft =
        require_port_result(
            response.graph, "compressor", "shaft");
    const auto& turbine_shaft =
        require_port_result(
            response.graph, "turbine", "shaft");
    const auto& generator_electrical =
        require_port_result(
            response.graph, "generator", "electrical");
    const auto& combustor_outlet =
        require_port_result(
            response.graph, "combustor", "outlet");
    const auto& turbine_outlet =
        require_port_result(
            response.graph, "turbine", "outlet");

    require(
        std::abs(
            require_result_value(
                compressor_shaft.primary_values, "W_dot")
                    .value_si -
            34.80152099e6) < 100.0,
        "Brayton compressor power must match the independent "
        "Cantera reference");
    require(
        std::abs(
            require_result_value(
                turbine_shaft.primary_values, "W_dot")
                    .value_si -
            69.86066885e6) < 100.0,
        "Brayton turbine power must match the independent "
        "Cantera reference");
    require(
        std::abs(
            require_result_value(
                generator_electrical.primary_values, "P")
                    .value_si -
            33.84513305e6) < 100.0,
        "Brayton net electric power must close the shaft train");
    require(
        std::abs(
            require_result_value(
                combustor_outlet.derived_values, "T")
                    .value_si -
            1418.696978) < 1.0e-3,
        "Brayton equilibrium firing temperature must match the "
        "independent Cantera reference");
    require(
        std::abs(
            require_result_value(
                turbine_outlet.derived_values, "T")
                    .value_si -
            864.300347) < 1.0e-3,
        "Brayton exhaust temperature must match the independent "
        "Cantera reference");
}
#endif

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

void test_structurally_singular_validation_diagnostic() {
    thermox::service::SimulationService service;
    thermox::service::ValidateModelRequest request;
    request.model_json = R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "singular_heat_exchanger",
    "media": [{
      "id": "air",
      "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "components": [{
      "id": "hx",
      "kind": "heat_exchanger.fluid.fixed_duty",
      "parameters": {
        "heat_duty": {"value": 1.0, "unit": "MW"}
      },
      "media": {
        "hot_in": "air",
        "hot_out": "air",
        "cold_in": "air",
        "cold_out": "air"
      }
    }],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "hx.hot_in.m_dot": {"value": 10.0, "unit": "kg/s"},
      "hx.hot_out.m_dot": {"value": 10.0, "unit": "kg/s"},
      "hx.hot_in.p": {"value": 2.0, "unit": "bar"},
      "hx.hot_out.p": {"value": 2.0, "unit": "bar"},
      "hx.cold_in.p": {"value": 1.0, "unit": "bar"},
      "hx.cold_out.p": {"value": 1.0, "unit": "bar"}
    }
  }]
})json";
    const auto response = service.validate_model(request);
    require(
        !response.succeeded() &&
            !response.diagnostics.empty() &&
            response.diagnostics.front().code ==
                "structurally_singular_model" &&
            response.diagnostics.front().message.find(
                "unmatched variable candidate(s)") !=
                std::string::npos &&
            response.diagnostics.front().message.find(
                "unmatched equation candidate(s)") !=
                std::string::npos &&
            response.diagnostics.front().message.find(
                "underdetermined structural region(s)") !=
                std::string::npos &&
            response.diagnostics.front().message.find(
                "overdetermined structural region(s)") !=
                std::string::npos,
        "square singular validation must expose stable structural "
        "candidates and localized regions");
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

std::string independent_study_model(
    double baseline_power_w) {
    std::ostringstream model;
    model << std::setprecision(17) << R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "independent_study",
    "media": [{
      "id": "air",
      "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "components": [{
      "id": "compressor",
      "kind": "compressor.fluid.isentropic_efficiency",
      "parameters": {"pressure_ratio": 10.0, "eta_is": 0.80},
      "media": {"inlet": "air", "outlet": "air"}
    }],
    "connections": []
  },
  "cases": [
    {
      "id": "baseline",
      "mode": "steady_state_design",
      "fixed_values": {
        "compressor.inlet.m_dot": {"value": 100.0, "unit": "kg/s"},
        "compressor.inlet.p": {"value": 101.325, "unit": "kPa"},
        "compressor.inlet.T": {"value": 300.0, "unit": "K"},
        "compressor.shaft.omega": 314.1592653589793
      }
    },
    {
      "id": "validation",
      "mode": "steady_state_off_design",
      "fixed_values": {
        "compressor.inlet.m_dot": {"value": 80.0, "unit": "kg/s"},
        "compressor.inlet.p": {"value": 101.325, "unit": "kPa"},
        "compressor.inlet.T": {"value": 300.0, "unit": "K"},
        "compressor.shaft.omega": 314.1592653589793
      }
    }
  ],
  "calibrations": [{
    "id": "baseline_fit",
    "parameters": [{
      "id": "eta",
      "scope": "component",
      "targets": ["components.compressor.parameters.eta_is"],
      "bounds": {"lower": 0.75, "upper": 0.95}
    }],
    "observations": [{
      "id": "baseline_power",
      "case": "baseline",
      "target": "compressor.shaft.W_dot",
      "measured": {"value": )json"
          << baseline_power_w << R"json(, "unit": "W"},
      "sigma": {"value": 10000.0, "unit": "W"}
    }]
  }]
})json";
    return model.str();
}

std::string mapped_independent_study_model(
    double baseline_power_w) {
    std::ostringstream model;
    model << std::setprecision(17) << R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "mapped_independent_study",
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
        "reference_temperature": {"value": 300.0, "unit": "K"},
        "efficiency_scale": 0.8
      },
      "media": {"inlet": "air", "outlet": "air"}
    }],
    "connections": []
  },
  "cases": [
    {
      "id": "baseline",
      "mode": "steady_state_design",
      "fixed_values": {
        "compressor.inlet.m_dot": {"value": 100.0, "unit": "kg/s"},
        "compressor.inlet.p": {"value": 101.325, "unit": "kPa"},
        "compressor.inlet.T": {"value": 300.0, "unit": "K"},
        "compressor.shaft.omega": 314.1592653589793
      }
    },
    {
      "id": "validation",
      "mode": "steady_state_off_design",
      "fixed_values": {
        "compressor.inlet.m_dot": {"value": 80.0, "unit": "kg/s"},
        "compressor.inlet.p": {"value": 101.325, "unit": "kPa"},
        "compressor.inlet.T": {"value": 300.0, "unit": "K"},
        "compressor.shaft.omega": 314.1592653589793
      }
    }
  ],
  "calibrations": [{
    "id": "map_correction_fit",
    "parameters": [{
      "id": "compressor_efficiency_scale",
      "scope": "component",
      "targets": [
        "components.compressor.parameters.efficiency_scale"
      ],
      "bounds": {"lower": 0.7, "upper": 1.1}
    }],
    "observations": [{
      "id": "baseline_power",
      "case": "baseline",
      "target": "compressor.shaft.W_dot",
      "measured": {"value": )json"
          << baseline_power_w << R"json(, "unit": "W"},
      "sigma": {"value": 10000.0, "unit": "W"}
    }]
  }]
})json";
    return model.str();
}

void test_engineering_study_freezes_before_prediction() {
    constexpr double gamma = 1.4;
    constexpr double cp = 1004.5;
    constexpr double true_eta = 0.88;
    const double isentropic_delta_h =
        cp * 300.0 *
        (std::pow(
             10.0, (gamma - 1.0) / gamma) -
         1.0);
    const double baseline_power =
        100.0 * isentropic_delta_h / true_eta;
    const double validation_power =
        0.8 * baseline_power;

    thermox::service::SimulationService service;
    thermox::service::EngineeringStudyRequest request;
    request.model_json =
        independent_study_model(baseline_power);
    request.calibration_id = "baseline_fit";
    request.calibration_solver.max_iterations = 20;
    request.prediction_cases = {{
        "validation",
        {{
            "validation_power",
            "compressor.shaft.W_dot",
            "power",
            validation_power,
            10000.0,
        }},
    }};
    const auto response =
        service.run_engineering_study(request);
    require(
        response.succeeded(),
        "engineering study must calibrate then predict: " +
            response.error.message);
    require(
        response.calibration.succeeded() &&
            response.predictions.size() == 1 &&
            response.predictions.front()
                    .simulation.succeeded() &&
            response.diagnostics.prediction_case_count == 1 &&
            response.diagnostics.observation_count == 1,
        "study response must preserve calibration and prediction results");
    require(
        std::abs(
            response.predictions.front()
                .observations.front().residual_si) <
            5000.0,
        "frozen baseline efficiency must predict independent load");
    const auto json =
        thermox::service::
            serialize_engineering_study_response_json(
                response);
    require(
        json.find("\"calibration\": {") !=
                std::string::npos &&
            json.find("\"predictions\": [") !=
                std::string::npos &&
            json.find("\"weighted_sum_squares\":") !=
                std::string::npos,
        "study JSON must expose calibration, predictions, and "
        "aggregate diagnostics");

    auto changed_measurement = request;
    changed_measurement.prediction_cases.front()
        .observations.front().measured_si += 5.0e6;
    const auto changed =
        service.run_engineering_study(
            changed_measurement);
    require(
        changed.succeeded() &&
            changed.predictions.front()
                    .observations.front().predicted_si ==
                response.predictions.front()
                    .observations.front().predicted_si,
        "prediction observations must not influence the solve");

    auto leaked = request;
    leaked.prediction_cases.front().case_id = "baseline";
    const auto rejected =
        service.run_engineering_study(leaked);
    require(
        !rejected.succeeded() &&
            rejected.error.code ==
                "invalid_study_predictions",
        "calibration cases must not be reused as independent predictions");
}

void test_map_correction_is_calibrated_then_frozen() {
    constexpr double gamma = 1.4;
    constexpr double cp = 1004.5;
    constexpr double map_efficiency = 0.85;
    const double isentropic_delta_h =
        cp * 300.0 *
        (std::pow(
             10.0, (gamma - 1.0) / gamma) -
         1.0);
    const double baseline_power =
        100.0 * isentropic_delta_h / map_efficiency;
    const double validation_power = 0.8 * baseline_power;

    thermox::service::SimulationService service;
    thermox::service::EngineeringStudyRequest request;
    request.model_json =
        mapped_independent_study_model(baseline_power);
    request.calibration_id = "map_correction_fit";
    request.calibration_solver.max_iterations = 24;
    request.artifacts.performance_maps.push_back(
        compressor_map());
    request.prediction_cases = {{
        "validation",
        {{
            "validation_power",
            "compressor.shaft.W_dot",
            "power",
            validation_power,
            10000.0,
        }},
    }};

    const auto response =
        service.run_engineering_study(request);
    require(
        response.succeeded(),
        "map correction study must calibrate then predict: " +
            response.error.message);
    require(
        response.calibration.parameters.size() == 1 &&
            std::abs(
                response.calibration.parameters.front()
                    .fitted_value_si -
                1.0) <
                2.0e-3,
        "ordinary calibration must fit a component-owned map "
        "efficiency correction");
    require(
        std::abs(
            response.predictions.front()
                .observations.front().residual_si) <
            5000.0,
        "the fitted map correction must remain frozen for the "
        "independent operating point");
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
    components.register_connector_domain({
        "thermal_bus",
        "example.connector.thermal_bus/v1",
        "thermal_bus_link",
        {
            {"potential", 300.0, 100.0, "temperature", false},
            {"flow", 0.0, 1000000.0, "power", false},
        },
    });
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
    thermox::platform::ComponentModelDescriptor bridge;
    bridge.kind = "bridge.thermal_bus.custom";
    bridge.version = "0.1.0";
    bridge.ports = {
        {"emit", "thermal_bus", "out"},
        {"receive", "thermal_bus", "in"},
    };
    components.register_model(
        std::make_shared<
            thermox::platform::MetadataComponentModel>(
            bridge));
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
    const auto connector = std::find_if(
        catalog.connector_domains.begin(),
        catalog.connector_domains.end(),
        [](const auto& domain) {
            return domain.domain == "thermal_bus";
        });
    require(
        connector != catalog.connector_domains.end() &&
            connector->contract_version ==
                "example.connector.thermal_bus/v1" &&
            connector->connection_kind ==
                "thermal_bus_link" &&
            connector->variables.size() == 2U,
        "custom connector domain must reach service catalog");
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
      },
      {
        "id": "bridge",
        "kind": "bridge.thermal_bus.custom"
      }
    ],
    "connections": [
      {
        "id": "thermal_bus_loop",
        "from": "bridge.emit",
        "to": "bridge.receive",
        "kind": "thermal_bus_link",
        "contract_version": "example.connector.thermal_bus/v1"
      }
    ]
  },
  "cases": [
    {
      "id": "design",
      "mode": "steady_state_design",
      "fixed_values": {
        "sensor.signal.value": 42.0,
        "sensor.command.value": 0.5,
        "bridge.emit.potential": {
          "value": 350.0,
          "unit": "K"
        },
        "bridge.emit.flow": {
          "value": 2.0,
          "unit": "MW"
        }
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
                .domain == "control" &&
            require_port_result(
                solved.graph, "bridge", "emit")
                .domain == "thermal_bus" &&
            require_result_value(
                require_port_result(
                    solved.graph, "bridge", "receive")
                    .primary_values,
                "flow").value_si == 2.0e6,
        "custom runtime graph must compile and solve injected "
        "connector semantics");
}

void test_expression_component_flows_through_service_runtime() {
    auto components =
        thermox::platform::make_default_component_registry();
    thermox::platform::ExpressionComponentDefinition definition;
    definition.descriptor.kind = "custom.signal.gain";
    definition.descriptor.version = "1.0.0";
    definition.descriptor.ports = {
        {"input", "signal", "in"},
        {"output", "signal", "out"},
    };
    definition.descriptor.parameters = {
        {
            "gain", "dimensionless", true, std::nullopt,
            0.0, 100.0, true, true,
        },
    };
    definition.equations = {
        {
            "gain_law",
            "output.value - parameter.gain * input.value",
            1.0,
        },
    };
    thermox::platform::register_expression_component(
        components, std::move(definition));
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
                return component.kind == "custom.signal.gain";
            }),
        "safe expression component must appear in runtime catalog");

    thermox::service::SteadySimulationRequest request;
    request.case_id = "design";
    request.model_json = R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "safe_expression_service",
    "media": [],
    "components": [{
      "id": "gain",
      "kind": "custom.signal.gain",
      "parameters": {"gain": 2.5}
    }],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {"gain.input.value": 4.0}
  }]
})json";
    const auto solved = service.run_steady(request);
    require(
        solved.succeeded(),
        "safe expression component must solve through service: " +
            solved.error.message);
    require(
        std::abs(
            require_result_value(
                require_port_result(
                    solved.graph, "gain", "output")
                    .primary_values,
                "value").value_si -
            10.0) < 1.0e-12,
        "safe expression result must retain graph identity");
    require(
        solved.metadata.catalog_fingerprint ==
            catalog.fingerprint,
        "safe expression implementation identity must participate "
        "in execution provenance");
}

void test_expression_component_is_request_scoped() {
    thermox::service::SimulationService service;
    const auto base_catalog = service.get_catalog();

    thermox::service::SteadySimulationRequest request;
    request.case_id = "design";
    request.model_json = R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "request_scoped_expression",
    "media": [],
    "components": [{
      "id": "gain",
      "kind": "custom.signal.request_gain",
      "parameters": {"gain": 3.0}
    }],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {"gain.input.value": 4.0}
  }]
})json";
    thermox::service::ExpressionComponentInput component;
    component.kind = "custom.signal.request_gain";
    component.version = "1.0.0";
    component.ports = {
        {"input", "signal", "in", 1},
        {"output", "signal", "out", 1},
    };
    component.parameters = {
        {
            "gain", "dimensionless", true, std::nullopt,
            0.0, 100.0, true, true,
        },
    };
    component.equations = {
        {
            "gain_law",
            "output.value - parameter.gain * input.value",
            1.0,
        },
    };
    request.components.expression_components.push_back(component);

    const auto solved = service.run_steady(request);
    require(
        solved.succeeded(),
        "request-scoped expression component must solve: " +
            solved.error.message);
    require(
        std::abs(
            require_result_value(
                require_port_result(
                    solved.graph, "gain", "output")
                    .primary_values,
                "value").value_si -
            12.0) < 1.0e-12,
        "request-scoped component must contribute its equation");
    require(
        solved.metadata.catalog_fingerprint !=
            base_catalog.fingerprint,
        "request-scoped component identity must change execution "
        "provenance");

    const auto catalog_after = service.get_catalog();
    require(
        catalog_after.fingerprint == base_catalog.fingerprint &&
            std::none_of(
                catalog_after.components.begin(),
                catalog_after.components.end(),
                [](const auto& entry) {
                    return entry.kind ==
                        "custom.signal.request_gain";
                }),
        "request-scoped definitions must not mutate the shared "
        "runtime catalog");

    request.components.expression_components.front()
        .equations.front().expression = "system(\"unsafe\")";
    const auto rejected = service.run_steady(request);
    require(
        rejected.status ==
                thermox::service::OperationStatus::invalid_request &&
            rejected.error.code == "invalid_components" &&
            rejected.error.stage == "components",
        "unsafe request-scoped expressions must fail before model "
        "compilation");
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
    require(
        std::abs(require_result_value(
                     compressor.metrics,
                     "net_mass_flow")
                     .value_si) < 1.0e-9 &&
            std::abs(require_result_value(
                         compressor.metrics,
                         "net_energy_flow")
                         .value_si) < 1.0e-5,
        "component metrics must expose compressor mass and "
        "energy closure");
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

void test_steady_continuation_service() {
    thermox::service::SimulationService service;
    thermox::service::SteadySimulationRequest request;
    request.model_json =
        read_source_file("core/examples/air_compressor.json");
    request.case_id = "design";
    request.solver.continuation_enabled = true;
    request.solver.continuation_initial_step = 0.2;
    const auto response = service.run_steady(request);
    require(
        response.succeeded(),
        "continued steady execution failed: " +
            response.error.message);
    require(
        response.continuation.enabled &&
            response.continuation.converged &&
            response.continuation.used_informed_path &&
            response.continuation.reached_parameter == 1.0 &&
            response.continuation.accepted_stages > 1 &&
            !response.continuation.stages.empty(),
        "steady service must expose successful continuation "
        "stages");
    require(
        std::any_of(
            response.metadata.solver.settings.begin(),
            response.metadata.solver.settings.end(),
            [](const auto& setting) {
                return setting.name ==
                           "continuation_enabled" &&
                       setting.value == 1.0;
            }),
        "steady provenance must record continuation settings");
    require(
        response.metadata.solver.contract_version ==
            "thermox.newton-continuation/v1",
        "continued solve must identify its solver contract");
    const auto json =
        thermox::service::serialize_steady_response_json(
            response);
    require(
        json.find("\"continuation\": {\"enabled\": true") !=
                std::string::npos &&
            json.find("\"used_informed_path\": true") !=
                std::string::npos &&
            json.find("\"target_parameter\": 1") !=
                std::string::npos,
        "steady JSON must expose continuation diagnostics");
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

thermox::service::GraphResult projection_graph(double kpi_value) {
    thermox::service::GraphResult graph;
    graph.system_balances = {
        {"energy_residual", "power", 0.25},
    };
    graph.kpis = {
        {"net_efficiency", "dimensionless", kpi_value},
    };
    thermox::service::ComponentResult component;
    component.component_id = "turbine";
    component.kind = "expander";
    component.metrics = {
        {"shaft_power", "power", 12.0},
    };
    component.internal_values = {
        {"pressure_ratio", "dimensionless", 8.0},
    };
    thermox::service::PortResult outlet;
    outlet.port_name = "outlet";
    outlet.domain = "fluid";
    outlet.primary_values = {
        {"temperature", "temperature", 720.0},
    };
    outlet.derived_values = {
        {"enthalpy", "specific_energy", 900000.0},
    };
    component.ports.push_back(std::move(outlet));
    graph.components.push_back(std::move(component));
    return graph;
}

void test_system_agnostic_result_projection() {
    using thermox::service::ResultAggregation;
    using thermox::service::ResultProjection;
    using thermox::service::ResultValueScope;

    const std::vector<ResultProjection> steady_projections{
        {
            "cycle_efficiency",
            ResultValueScope::kpi,
            {},
            {},
            "net_efficiency",
            "dimensionless",
            ResultAggregation::final,
        },
        {
            "turbine_outlet_temperature",
            ResultValueScope::port_primary,
            "turbine",
            "outlet",
            "temperature",
            "temperature",
            ResultAggregation::final,
        },
    };
    const auto steady = thermox::service::project_steady_result(
        projection_graph(0.41), steady_projections);
    require(
        steady.schema_version ==
                thermox::service::result_summary_schema_v1 &&
            steady.mode == "steady" &&
            steady.values.size() == 2U &&
            steady.values[0].value_si == 0.41 &&
            steady.values[1].value_si == 720.0,
        "steady summaries must select exact unit-tagged graph "
        "values without system-specific logic");
    const auto steady_json =
        thermox::service::serialize_result_summary_json(steady);
    require(
        steady_json.find(
            "\"schema_version\": "
            "\"thermox.result_summary/v1\"") !=
                std::string::npos &&
            steady_json.find(
                "\"id\": \"cycle_efficiency\"") !=
                std::string::npos &&
            steady_json.find("\"sample_time\": null") !=
                std::string::npos,
        "result summaries must have a stable serialized "
        "service contract");

    std::vector<thermox::service::StateSample> trajectory{
        {0.0, projection_graph(0.40)},
        {1.0, projection_graph(0.25)},
        {2.0, projection_graph(0.35)},
    };
    const std::vector<ResultProjection> transient_projections{
        {
            "minimum_efficiency",
            ResultValueScope::kpi,
            {},
            {},
            "net_efficiency",
            "dimensionless",
            ResultAggregation::minimum,
        },
        {
            "maximum_efficiency",
            ResultValueScope::kpi,
            {},
            {},
            "net_efficiency",
            "dimensionless",
            ResultAggregation::maximum,
        },
        {
            "final_efficiency",
            ResultValueScope::kpi,
            {},
            {},
            "net_efficiency",
            "dimensionless",
            ResultAggregation::final,
        },
    };
    const auto transient =
        thermox::service::project_transient_result(
            trajectory, transient_projections);
    require(
        transient.values.size() == 3U &&
            transient.values[0].value_si == 0.25 &&
            transient.values[0].sample_time == 1.0 &&
            transient.values[1].value_si == 0.40 &&
            transient.values[1].sample_time == 0.0 &&
            transient.values[2].value_si == 0.35 &&
            transient.values[2].sample_time == 2.0,
        "transient summaries must apply explicit reductions and "
        "retain the selected sample time");

    auto wrong_dimension = steady_projections;
    wrong_dimension.front().dimension = "power";
    bool rejected = false;
    try {
        (void)thermox::service::project_steady_result(
            projection_graph(0.41), wrong_dimension);
    } catch (const thermox::service::ResultProjectionError&) {
        rejected = true;
    }
    require(
        rejected,
        "result projection must reject dimension mismatches");
}

}  // namespace

int main() {
    try {
        test_request_contract_validation();
        test_request_scoped_performance_map_artifacts();
        test_resolved_performance_map_artifacts();
        test_catalog_discovery();
        test_validation_and_canonicalization();
#ifdef THERMOX_TEST_HAS_CANTERA
        test_cantera_brayton_integration_benchmark();
#endif
        test_compile_aware_validation_diagnostics();
        test_structurally_singular_validation_diagnostic();
        test_calibration_observation_contract_validation();
        test_bounded_calibration_service();
        test_engineering_study_freezes_before_prediction();
        test_map_correction_is_calibrated_then_frozen();
        test_component_version_is_enforced();
        test_property_and_connector_versions_are_enforced();
        test_connection_contract_diagnostic();
        test_injectable_native_runtime();
        test_expression_component_flows_through_service_runtime();
        test_expression_component_is_request_scoped();
        test_steady_service();
        test_steady_continuation_service();
        test_explicit_system_boundary_balance();
        test_transient_service();
        test_structured_compilation_failure();
        test_invalid_solver_settings();
        test_system_agnostic_result_projection();
        std::cout << "thermox service tests passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "thermox service tests failed: " << ex.what()
                  << "\n";
        return 1;
    }
}
