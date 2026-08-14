#include "thermox/platform/expression_component.hpp"
#include "thermox/service/in_memory_artifacts.hpp"
#include "thermox/service/engineering_study.hpp"
#include "thermox/service/native_runtime.hpp"
#include "thermox/service/performance_test.hpp"
#include "thermox/service/result_projection.hpp"
#include "thermox/service/serialization.hpp"
#include "thermox/service/simulation_service.hpp"
#include "thermox/service/thermal_feasibility.hpp"
#include "thermox/service/validation_evidence.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
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
    map.output_constraints = {
        {"pressure_ratio", 1.0, std::nullopt, false, true},
        {"isentropic_efficiency", 0.0, 1.0, false, true},
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

thermox::service::PerformanceMapArtifactInput conditioned_compressor_map() {
    using namespace thermox::service;
    auto ordinary = compressor_map();
    PerformanceMapArtifactInput artifact;
    artifact.id = "request-variable-geometry-map";
    artifact.schema_version = "thermox.performance_map/v2";
    artifact.revision = "service-test-1";
    artifact.checksum_sha256 = std::string(64, 'b');
    artifact.condition_variable = {
        "geometry_setting", "angle"};
    auto lower = *ordinary.map;
    auto upper = *ordinary.map;
    lower.primary_extrapolation = "linear";
    upper.primary_extrapolation = "linear";
    artifact.layers = {
        {0.0, std::move(lower)},
        {1.0, std::move(upper)},
    };
    artifact.condition_extrapolation = "linear";
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

thermox::service::CorrelationArtifactInput bend_correlation() {
    thermox::service::CorrelationArtifactInput artifact;
    artifact.id = "request-bend-correlation";
    artifact.schema_version = "thermox.correlation/v2";
    artifact.revision = "service-correlation-1";
    artifact.checksum_sha256 = std::string(64, 'c');
    artifact.inputs = {
        {"mass_flow", "mass_flow"},
        {"density", "density"},
        {"area", "area"},
    };
    artifact.output = {"pressure_loss", "pressure"};
    artifact.candidates = {
        {"default", "general", 0, {{"loss_coefficient", 1.5}},
         "loss_coefficient * mass_flow * abs(mass_flow) / "
         "(2 * density * area * area)", {}}};
    return artifact;
}

thermox::service::CorrelationArtifactInput bend_correlation_family() {
    thermox::service::CorrelationArtifactInput artifact;
    artifact.id = "request-bend-correlation";
    artifact.schema_version = "thermox.correlation/v2";
    artifact.revision = "service-family-1";
    artifact.checksum_sha256 = std::string(64, 'f');
    artifact.inputs = {
        {"mass_flow", "mass_flow"},
        {"density", "density"},
        {"area", "area"},
    };
    artifact.output = {"pressure_loss", "pressure"};
    artifact.candidates = {
        {"low_flow", "low-flow", 10, {{"coefficient", 1.0}},
         "coefficient * mass_flow * abs(mass_flow) / "
         "(2 * density * area * area)",
         {{"mass_flow", 0.0, 2.0, true, true}}},
        {"high_flow", "high-flow", 20, {{"coefficient", 1.5}},
         "coefficient * mass_flow * abs(mass_flow) / "
         "(2 * density * area * area)",
         {{"mass_flow", 2.0, 20.0, true, true}}},
    };
    return artifact;
}

std::string correlated_bend_model() {
    return R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "service_correlated_bend",
    "media": [{"id": "air", "backend": "ideal_gas_mixture", "substance": "Air"}],
    "components": [{
      "id": "bend",
      "kind": "fitting.fluid.return_bend.correlation",
      "parameters": {"inner_diameter": {"value": 0.5, "unit": "m"}},
      "artifacts": {"pressure_loss_correlation": "request-bend-correlation"},
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
})json";
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

std::string variable_geometry_compressor_model() {
    auto model = mapped_compressor_model();
    const auto replace = [&](
        const std::string& from, const std::string& to) {
        const auto position = model.find(from);
        if (position == std::string::npos) {
            throw std::runtime_error(
                "test model substitution target not found: " + from);
        }
        model.replace(position, from.size(), to);
    };
    replace(
        "compressor.fluid.performance_map",
        "compressor.fluid.variable_geometry_map");
    replace(
        "request-compressor-map",
        "request-variable-geometry-map");
    replace(
        "\"reference_temperature\": {\"value\": 300.0, \"unit\": \"K\"}",
        "\"reference_temperature\": {\"value\": 300.0, \"unit\": \"K\"},\n"
        "        \"geometry_setting\": {\"value\": 0.5, \"unit\": \"rad\"}");
    return model;
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
        valid.succeeded() && valid.readiness.calculatable &&
            valid.performance_map_quality.size() == 1U &&
            !valid.performance_map_quality.front().conditioned &&
            valid.performance_map_quality.front().layers.size() == 1U &&
            valid.performance_map_quality.front().layers.front()
                    .curve_count == 2U &&
            valid.performance_map_quality.front().layers.front()
                    .sample_count == 4U &&
            valid.performance_map_quality.front().layers.front()
                    .outputs.at(1).constraint_maximum == 1.0 &&
            valid.performance_map_quality.front().layers.front()
                    .outputs.at(1).minimum_upper_margin.has_value() &&
            std::abs(*valid.performance_map_quality.front().layers.front()
                          .outputs.at(1).minimum_upper_margin -
                     0.15) < 1.0e-12 &&
            valid.performance_map_quality.front().advisory_codes.empty(),
        "validation must resolve and report quality for a request-scoped "
        "performance map");
    const auto valid_json =
        thermox::service::serialize_validate_response_json(valid);
    require(
        valid_json.find("\"performance_map_quality\": [") !=
                std::string::npos &&
            valid_json.find(
                "\"schema_version\": "
                "\"thermox.performance_map_quality/v1\"") !=
                std::string::npos &&
            valid_json.find("\"declared_constraint\": {") !=
                std::string::npos,
        "validation JSON must expose structured performance-map quality");

    auto physically_invalid_validation = validation;
    physically_invalid_validation.artifacts.performance_maps.front()
        .map->output_constraints.at(1).maximum = 0.80;
    const auto physically_invalid =
        service.validate_model(physically_invalid_validation);
    require(
        physically_invalid.status ==
                thermox::service::OperationStatus::invalid_request &&
            !physically_invalid.readiness.calculatable &&
            physically_invalid.diagnostics.size() == 1U &&
            physically_invalid.diagnostics.front().code ==
                "invalid_artifacts" &&
            physically_invalid.diagnostics.front().stage == "physical",
        "map samples outside declared physical constraints must block "
        "artifact readiness before compilation");

    auto extrapolating_validation = validation;
    auto& extrapolating_map =
        *extrapolating_validation.artifacts.performance_maps.front().map;
    extrapolating_map.primary_extrapolation = "linear";
    extrapolating_map.family_extrapolation = "linear";
    const auto extrapolating =
        service.validate_model(extrapolating_validation);
    require(
        extrapolating.succeeded() &&
            extrapolating.readiness.calculatable &&
            extrapolating.performance_map_quality.front()
                    .advisory_codes.size() == 2U &&
            extrapolating.diagnostics.size() == 2U &&
            extrapolating.diagnostics.front().severity ==
                thermox::service::DiagnosticSeverity::warning &&
            extrapolating.diagnostics.front().stage == "physical" &&
            extrapolating.diagnostics.front().json_path ==
                "/artifacts/performance_maps/0/map",
        "map quality advisories must be non-blocking physical warnings "
        "attributed to the exact artifact payload");

    thermox::service::ValidateModelRequest conditioned_validation;
    conditioned_validation.model_json =
        variable_geometry_compressor_model();
    conditioned_validation.case_id = "operating_point";
    conditioned_validation.artifacts.performance_maps.push_back(
        conditioned_compressor_map());
    const auto conditioned =
        service.validate_model(conditioned_validation);
    const auto& conditioned_quality =
        conditioned.performance_map_quality.at(0);
    require(
        conditioned.succeeded() && conditioned.readiness.calculatable &&
            conditioned_quality.conditioned &&
            conditioned_quality.layers.size() == 2U &&
            conditioned_quality.layers.front()
                    .has_condition_coordinate &&
            conditioned_quality.layers.front()
                    .advisory_codes.size() == 1U &&
            conditioned_quality.advisory_codes.size() == 1U &&
            conditioned_quality.outputs.size() == 2U &&
            conditioned.diagnostics.size() == 3U &&
            conditioned.diagnostics.front().json_path ==
                "/artifacts/performance_maps/0/layers/0/map",
        "conditioned-map quality must expose per-layer and cross-layer "
        "metrics without blocking readiness");

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
    auto restricted = request;
    restricted.artifacts.performance_maps.front()
        .operating_envelope = {{
        "corrected_mass_flow", "mass_flow",
        110.0, 120.0, true, true,
    }};
    const auto outside_envelope = service.run_steady(restricted);
    require(
        !outside_envelope.succeeded() &&
            outside_envelope.error.code ==
                "artifact_operating_envelope_violation" &&
            outside_envelope.error.message.find("operating envelope") !=
                std::string::npos,
        "request-scoped operating envelopes must reject a converged "
        "physical point outside Study policy");
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

void test_request_scoped_correlation_artifacts() {
    thermox::service::SimulationService service;
    thermox::service::SteadySimulationRequest request;
    request.model_json = correlated_bend_model();
    request.case_id = "design";
    request.artifacts.correlations.push_back(
        bend_correlation());
    const auto response = service.run_steady(request);
    require(
        response.succeeded(),
        "steady execution must use a request-scoped correlation: " +
            response.error.message);
    require(
        response.metadata.artifacts.size() == 1U &&
            response.metadata.artifacts.front().artifact_type ==
                "thermox.correlation" &&
            response.metadata.artifacts.front().id ==
                "request-bend-correlation",
        "correlation execution must retain immutable provenance");
    auto restricted = request;
    restricted.artifacts.correlations.front().operating_envelope = {{
        "mass_flow", "mass_flow", 0.0, 1.0, true, true,
    }};
    const auto outside_envelope = service.run_steady(restricted);
    require(
        !outside_envelope.succeeded() &&
            outside_envelope.error.code ==
                "artifact_operating_envelope_violation" &&
            outside_envelope.error.message.find("mass_flow") !=
                std::string::npos,
        "correlation operating-envelope violations must reach the "
        "stable service error contract: " +
            outside_envelope.error.code + ": " +
            outside_envelope.error.message);
}

void test_correlation_applicability_reaches_component_diagnostics() {
    thermox::service::SimulationService service;
    thermox::service::SteadySimulationRequest request;
    request.model_json = correlated_bend_model();
    request.case_id = "design";
    auto artifact = bend_correlation();
    artifact.candidates.front().applicability = {
        {"mass_flow", 0.0, 1.0, true, true},
    };
    request.artifacts.correlations.push_back(std::move(artifact));
    const auto response = service.run_steady(request);
    require(
        !response.succeeded() &&
            response.error.message.find(
                "mass_flow=2") != std::string::npos &&
            response.error.message.find(
                "[0, 1]") != std::string::npos,
        "component evaluation must surface the exact correlation "
        "applicability violation");
}

void test_request_scoped_correlation_family_selects_candidate() {
    thermox::service::SimulationService service;
    thermox::service::SteadySimulationRequest request;
    request.model_json = correlated_bend_model();
    request.case_id = "design";
    request.artifacts.correlations.push_back(
        bend_correlation_family());
    const auto response = service.run_steady(request);
    require(
        response.succeeded(),
        "a v2 correlation family must execute through an unchanged "
        "component artifact role: " + response.error.message);
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
            thermox::service::catalog_schema_v10,
        "catalog contract must be versioned");
    require(
        !response.fingerprint.empty(),
        "catalog must have a deterministic fingerprint");
    require(
        response.components.size() == 76,
        "service must expose the complete component registry");
    const auto efficient_combustor = std::find_if(
        response.components.begin(), response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "combustor.material.equilibrium_heat_release_efficiency";
        });
    require(
        efficient_combustor != response.components.end() &&
            efficient_combustor->supports_steady &&
            !efficient_combustor->supports_transient,
        "catalog must expose the heat-release-efficiency combustor");
    const auto material_regulator = std::find_if(
        response.components.begin(), response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "regulator.material.isenthalpic_network_pressure";
        });
    require(
        material_regulator != response.components.end() &&
            material_regulator->supports_steady &&
            !material_regulator->supports_transient,
        "catalog must expose the material pressure regulator");
    const auto iso_compressor = std::find_if(
        response.components.begin(), response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "compressor.material.iso2314_equivalent_cooling";
        });
    require(
        iso_compressor != response.components.end() &&
            iso_compressor->supports_steady &&
            !iso_compressor->supports_transient,
        "catalog must expose the graph-native ISO 2314 compressor");
    const auto shaft_combiner = std::find_if(
        response.components.begin(), response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "shaft.combiner.two_driver";
        });
    require(
        shaft_combiner != response.components.end() &&
            shaft_combiner->supports_steady &&
            shaft_combiner->ports.size() == 3,
        "catalog must expose the multi-stage shaft combiner");
    const auto dynamic_cell = std::find_if(
        response.components.begin(), response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "heat_exchanger.fluid.dynamic_cell";
        });
    require(
        dynamic_cell != response.components.end() &&
            dynamic_cell->supports_steady &&
            dynamic_cell->supports_transient &&
            dynamic_cell->internal_variables.size() == 5,
        "catalog must expose steady/transient heat-exchanger cells");
    const auto material_fluid_cell = std::find_if(
        response.components.begin(), response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "heat_exchanger.material_fluid.dynamic_cell";
        });
    require(
        material_fluid_cell != response.components.end() &&
            material_fluid_cell->supports_steady &&
            material_fluid_cell->supports_transient &&
            material_fluid_cell->internal_variables.size() == 3,
        "catalog must expose the composition-aware dynamic "
        "material-to-fluid cell");
    const auto two_phase_cell = std::find_if(
        response.components.begin(), response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "heat_exchanger.material_fluid.equilibrium_two_phase_cell";
        });
    require(
        two_phase_cell != response.components.end() &&
            !two_phase_cell->supports_steady &&
            two_phase_cell->supports_transient &&
            two_phase_cell->internal_variables.size() == 5,
        "catalog must expose the equilibrium two-phase inventory "
        "cell");
    const auto two_phase_loss = std::find_if(
        response.components.begin(), response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "restriction.fluid.local_loss.homogeneous_two_phase";
        });
    require(
        two_phase_loss != response.components.end() &&
            two_phase_loss->supports_steady &&
            two_phase_loss->supports_transient &&
            two_phase_loss->parameters.size() == 2,
        "catalog must expose the homogeneous two-phase hydraulic "
        "impedance contract");
    const auto correlated_pipe = std::find_if(
        response.components.begin(), response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "pipe.fluid.correlated_two_phase_pressure_drop";
        });
    require(
        correlated_pipe != response.components.end() &&
            correlated_pipe->supports_steady &&
            correlated_pipe->supports_transient &&
            correlated_pipe->artifacts.size() == 3 &&
            std::any_of(
                correlated_pipe->artifacts.begin(),
                correlated_pipe->artifacts.end(),
                [](const auto& artifact) {
                    return artifact.role ==
                        "friction_pressure_gradient_correlation" &&
                        artifact.required;
                }) &&
            std::any_of(
                correlated_pipe->artifacts.begin(),
                correlated_pipe->artifacts.end(),
                [](const auto& artifact) {
                    return artifact.role == "friction_regime_map" &&
                        artifact.artifact_type ==
                            "thermox.regime_map" &&
                        !artifact.required;
                }),
        "catalog must expose independent required two-phase void and "
        "friction correlation roles plus optional regime selection");
    const auto correlated_inventory = std::find_if(
        response.components.begin(), response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "volume.fluid.equilibrium_two_phase_correlated_outlet";
        });
    require(
        correlated_inventory != response.components.end() &&
            !correlated_inventory->supports_steady &&
            correlated_inventory->supports_transient &&
            correlated_inventory->parameters.size() == 2 &&
            correlated_inventory->internal_variables.size() == 6 &&
            correlated_inventory->artifacts.size() == 1 &&
            correlated_inventory->artifacts.front().role ==
                "void_fraction_correlation" &&
            correlated_inventory->artifacts.front().artifact_type ==
                "thermox.correlation",
        "catalog must expose correlated two-phase inventory and "
        "outlet-slip contracts");
    const auto hydraulic_inertance = std::find_if(
        response.components.begin(), response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "pipe.fluid.hydraulic_inertance";
        });
    require(
        hydraulic_inertance != response.components.end() &&
            !hydraulic_inertance->supports_steady &&
            hydraulic_inertance->supports_transient &&
            hydraulic_inertance->parameters.size() == 2,
        "catalog must expose lumped hydraulic momentum storage");
    const auto gravity_pipe = std::find_if(
        response.components.begin(), response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "pipe.fluid.homogeneous_equilibrium_local_loss";
        });
    require(
        gravity_pipe != response.components.end() &&
            gravity_pipe->supports_steady &&
            gravity_pipe->supports_transient &&
            gravity_pipe->parameters.size() == 3,
        "catalog must expose bidirectional homogeneous-equilibrium "
        "gravity and local-loss closure");
    const auto fluid_pump = std::find_if(
        response.components.begin(), response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "pump.fluid.isentropic_efficiency";
        });
    const auto fluid_mixer = std::find_if(
        response.components.begin(), response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "junction.fluid.mixer.two_inlet";
        });
    require(
        fluid_pump != response.components.end() &&
            fluid_pump->supports_steady &&
            fluid_pump->supports_transient &&
            fluid_mixer != response.components.end() &&
            fluid_mixer->supports_steady &&
            fluid_mixer->supports_transient,
        "catalog must expose quasi-steady pump and mixer models "
        "inside transient networks");
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
    const auto pipe = std::find_if(
        response.components.begin(),
        response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "pipe.fluid.darcy_weisbach_heat_transfer";
        });
    require(
        pipe != response.components.end() &&
            pipe->template_kind == "pipe.fluid" &&
            pipe->category == "Fluid transport" &&
            pipe->ports.size() == 3 &&
            pipe->required_property_capabilities ==
                std::vector<std::string>{"state_ph", "transport"},
        "catalog must expose the property-backed pipe and its "
        "explicit ambient heat boundary");
    const auto gas_orifice = std::find_if(
        response.components.begin(),
        response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "restriction.fluid.orifice.perfect_gas";
        });
    require(
        gas_orifice != response.components.end() &&
            gas_orifice->template_kind ==
                "restriction.fluid.orifice" &&
            gas_orifice->category == "Fluid control" &&
            gas_orifice->parameters.size() == 2,
        "catalog must expose the generic choking-orifice model");
    const auto flash_separator = std::find_if(
        response.components.begin(),
        response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "separator.fluid.equilibrium_flash";
        });
    require(
        flash_separator != response.components.end() &&
            flash_separator->template_kind == "separator.fluid" &&
            flash_separator->category == "Phase separation" &&
            flash_separator->ports.size() == 3 &&
            flash_separator->required_property_capabilities ==
                std::vector<std::string>{"saturation_p"},
        "catalog must expose the equilibrium flash separator");
    const auto drum = std::find_if(
        response.components.begin(),
        response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "drum.fluid.equilibrium_two_phase";
        });
    require(
        drum != response.components.end() &&
            drum->template_kind == "drum.fluid" &&
            drum->category == "Fluid inventory" &&
            !drum->supports_steady && drum->supports_transient &&
            drum->ports.size() == 5 &&
            drum->internal_variables.size() == 5 &&
            drum->required_property_capabilities ==
                std::vector<std::string>{"saturation_p"},
        "catalog must expose dynamic equilibrium drum states and ports");
    const auto actuated_valve = std::find_if(
        response.components.begin(),
        response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "valve.fluid.actuated_nonflashing_liquid";
        });
    require(
        actuated_valve != response.components.end() &&
            actuated_valve->template_kind == "valve.fluid" &&
            actuated_valve->category == "Fluid control" &&
            actuated_valve->supports_steady &&
            actuated_valve->supports_transient &&
            actuated_valve->ports.size() == 3 &&
            actuated_valve->required_property_capabilities ==
                std::vector<std::string>{"state_ph", "saturation_p"},
        "catalog must expose the normalized actuated liquid valve");
    const auto bounded_pi = std::find_if(
        response.components.begin(),
        response.components.end(),
        [](const auto& component) {
            return component.kind ==
                "control.pi_bounded.normalized";
        });
    require(
        bounded_pi != response.components.end() &&
            bounded_pi->template_kind == "control.pi" &&
            bounded_pi->category == "Control" &&
            !bounded_pi->supports_steady &&
            bounded_pi->supports_transient &&
            bounded_pi->ports.size() == 3 &&
            bounded_pi->internal_variables.size() == 1,
        "catalog must expose bounded PI anti-windup state and ports");
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
                "saturation_p") != if97->capabilities.end() &&
            std::find(
                if97->capabilities.begin(),
                if97->capabilities.end(),
                "surface_tension") != if97->capabilities.end(),
        "catalog must expose property compatibility metadata");
    const auto water_heos = std::find_if(
        response.property_backends.begin(),
        response.property_backends.end(),
        [](const auto& backend) {
            return backend.backend == "coolprop_heos";
        });
    require(
        water_heos != response.property_backends.end() &&
            water_heos->implementation_name ==
                "coolprop-heos-water" &&
            std::find(
                water_heos->capabilities.begin(),
                water_heos->capabilities.end(),
                "state_ph_derivatives") !=
                water_heos->capabilities.end(),
        "catalog must expose regime-spanning HEOS water metadata");
    require(
        response.connector_domains.size() == 7,
        "catalog must expose connector contracts");
    const auto zuber_findlay = std::find_if(
        response.correlation_templates.begin(),
        response.correlation_templates.end(),
        [](const auto& descriptor) {
            return descriptor.id ==
                "zuber_findlay_kinematic_void_fraction";
        });
    require(
        zuber_findlay != response.correlation_templates.end() &&
            zuber_findlay->coefficients.size() == 2 &&
            zuber_findlay->reference.find("10.1115/1.3689137") !=
                std::string::npos,
        "catalog must expose the referenced, parameterized drift-flux template");
    const auto chisholm = std::find_if(
        response.correlation_templates.begin(),
        response.correlation_templates.end(),
        [](const auto& descriptor) {
            return descriptor.id ==
                "chisholm_turbulent_turbulent_friction_gradient";
        });
    require(
        response.correlation_templates.size() == 5 &&
            chisholm != response.correlation_templates.end() &&
            chisholm->coefficients.size() == 5 &&
            chisholm->reference.find(
                "10.1016/0017-9310(67)90047-6") !=
                std::string::npos,
        "catalog must expose all referenced Chisholm regime templates");
    const auto chisholm_family = std::find_if(
        response.correlation_family_templates.begin(),
        response.correlation_family_templates.end(),
        [](const auto& descriptor) {
            return descriptor.id ==
                "chisholm_smooth_pipe_friction_family";
        });
    require(
        chisholm_family !=
                response.correlation_family_templates.end() &&
            chisholm_family->bindings.size() == 4U &&
            chisholm_family->bindings.front()
                .fallback_for_unmapped_flow_regime &&
            chisholm_family->scope.find("does not claim") !=
                std::string::npos,
        "catalog must expose the packaged general Chisholm family and "
        "its physical scope");
    const auto mishima_ishii = std::find_if(
        response.regime_map_templates.begin(),
        response.regime_map_templates.end(),
        [](const auto& descriptor) {
            return descriptor.id ==
                "mishima_ishii_vertical_upflow_annular_entrainment";
        });
    require(
        response.regime_map_templates.size() == 4 &&
            mishima_ishii != response.regime_map_templates.end() &&
            mishima_ishii->regions.size() == 2 &&
            mishima_ishii->regions.front().branches.size() == 1 &&
            mishima_ishii->regions.front().branches.front().id ==
                "wave_entrainment" &&
            mishima_ishii->scope.find("vertical upward") !=
                std::string::npos,
        "catalog must expose cited regime-map templates with explicit scope");
    const auto composite_map = std::find_if(
        response.regime_map_templates.begin(),
        response.regime_map_templates.end(),
        [](const auto& descriptor) {
            return descriptor.id ==
                "mishima_ishii_vertical_upflow_composite";
        });
    require(
        composite_map != response.regime_map_templates.end() &&
            composite_map->regions.size() == 4U &&
            composite_map->regions.back().regime == "annular" &&
            composite_map->regions.back().branches.size() == 2U,
        "catalog must preserve composite regimes and alternative "
        "physical mechanisms");
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
        json.find("\"schema_version\": \"thermox.catalog/v10\"") !=
            std::string::npos,
        "catalog JSON must expose its schema");
    require(
        json.find("\"unit_dimensions\": [") !=
            std::string::npos,
        "catalog JSON must serialize unit dimensions");
    require(
        json.find("\"correlation_templates\": [") !=
                std::string::npos &&
            json.find("10.1115/1.3689137") != std::string::npos,
        "catalog JSON must serialize correlation templates and provenance");
    require(
        json.find("\"correlation_family_templates\": [") !=
                std::string::npos &&
            json.find("chisholm_smooth_pipe_friction_family") !=
                std::string::npos,
        "catalog JSON must serialize reusable correlation families");
    require(
        json.find("\"regime_map_templates\": [") !=
                std::string::npos &&
            json.find("10.1016/0017-9310(84)90142-X") !=
                std::string::npos,
        "catalog JSON must serialize regime-map templates and provenance");
}

void test_correlation_template_instantiation() {
    thermox::service::SimulationService service;
    thermox::service::InstantiateCorrelationRequest request;
    request.artifact_id = "smooth-pipe-two-phase-friction";
    request.revision = "engineering-baseline-1";
    request.bindings = {
        {
            "chisholm_laminar_laminar_friction_gradient",
            {}, "laminar_laminar", 0, {}, true,
        },
        {
            "chisholm_laminar_turbulent_friction_gradient",
            {}, "laminar_turbulent", 0, {}, true,
        },
        {
            "chisholm_turbulent_laminar_friction_gradient",
            {}, "turbulent_laminar", 0, {}, true,
        },
        {
            "chisholm_turbulent_turbulent_friction_gradient",
            {}, "turbulent_turbulent", 0, {}, true,
        },
    };
    const auto response = service.instantiate_correlation(request);
    require(
        response.succeeded() &&
            response.schema_version ==
                thermox::service::
                    correlation_instantiation_schema_v1 &&
            response.artifact.id == request.artifact_id &&
            response.artifact.schema_version ==
                "thermox.correlation/v2" &&
            response.artifact.candidates.size() == 4U &&
            response.artifact.candidates.front()
                .fallback_for_unmapped_flow_regime &&
            response.canonical_payload_json.find(
                "fallback_for_unmapped_flow_regime") !=
                std::string::npos &&
            response.artifact.checksum_sha256.size() == 64U &&
            !response.canonical_payload_json.empty() &&
            !response.catalog_fingerprint.empty(),
        "service must instantiate a content-addressed correlation "
        "family from catalog templates");

    const auto repeated = service.instantiate_correlation(request);
    require(
        repeated.succeeded() &&
            repeated.artifact.checksum_sha256 ==
                response.artifact.checksum_sha256 &&
            repeated.canonical_payload_json ==
                response.canonical_payload_json,
        "correlation instantiation must be deterministic");

    thermox::service::InstantiateCorrelationRequest packaged;
    packaged.artifact_id = "packaged-smooth-pipe-friction";
    packaged.revision = "engineering-baseline-1";
    packaged.family_template_id =
        "chisholm_smooth_pipe_friction_family";
    const auto packaged_response =
        service.instantiate_correlation(packaged);
    require(
        packaged_response.succeeded() &&
            packaged_response.artifact.candidates.size() == 4U &&
            packaged_response.artifact.candidates.back()
                .fallback_for_unmapped_flow_regime,
        "service must instantiate a complete registered family by ID");

    auto conflicting = request;
    conflicting.family_template_id =
        "chisholm_smooth_pipe_friction_family";
    const auto conflicting_response =
        service.instantiate_correlation(conflicting);
    require(
        !conflicting_response.succeeded() &&
            conflicting_response.error.code ==
                "invalid_correlation_bindings",
        "service must reject ambiguous family-ID plus explicit bindings");

    auto incompatible = request;
    incompatible.bindings.push_back({
        "zuber_findlay_kinematic_void_fraction", {}, "void", 0,
    });
    const auto rejected =
        service.instantiate_correlation(incompatible);
    require(
        !rejected.succeeded() &&
            rejected.error.code ==
                "correlation_instantiation_failed",
        "service must reject templates with incompatible contracts");
}

void test_regime_map_template_instantiation() {
    thermox::service::SimulationService service;
    thermox::service::InstantiateRegimeMapRequest request;
    request.artifact_id = "vertical-upflow-annular-boundary";
    request.revision = "engineering-baseline-1";
    request.template_id =
        "mishima_ishii_vertical_upflow_annular_entrainment";
    const auto response = service.instantiate_regime_map(request);
    require(
        response.succeeded() &&
            response.schema_version ==
                thermox::service::
                    regime_map_instantiation_schema_v1 &&
            response.artifact.id == request.artifact_id &&
            response.artifact.schema_version ==
                "thermox.regime_map/v2" &&
            response.artifact.regions.size() == 2U &&
            response.artifact.checksum_sha256.size() == 64U &&
            !response.canonical_payload_json.empty() &&
            !response.catalog_fingerprint.empty(),
        "service must instantiate a content-addressed regime map "
        "from a catalog template");
    const auto repeated = service.instantiate_regime_map(request);
    require(
        repeated.succeeded() &&
            repeated.artifact.checksum_sha256 ==
                response.artifact.checksum_sha256 &&
            repeated.canonical_payload_json ==
                response.canonical_payload_json,
        "regime-map instantiation must be deterministic");

    auto unknown = request;
    unknown.template_id = "unknown";
    const auto rejected = service.instantiate_regime_map(unknown);
    require(
        !rejected.succeeded() &&
            rejected.error.code ==
                "regime_map_instantiation_failed",
        "service must reject unknown regime-map templates");
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
                response.compilation.equation_count &&
            !response.compilation.structural_blocks.empty() &&
            response.compilation.largest_structural_block_size > 0 &&
            std::accumulate(
                response.compilation.structural_blocks.begin(),
                response.compilation.structural_blocks.end(),
                std::size_t{0},
                [](std::size_t total, const auto& block) {
                    return total + block.variable_names.size();
                }) == response.compilation.variable_count,
        "validation must compile and structurally analyze the model");
    require(
        response.readiness.calculatable &&
            response.readiness.layers.size() == 6 &&
            std::all_of(
                response.readiness.layers.begin(),
                response.readiness.layers.end(),
                [](const auto& layer) {
                    return layer.state ==
                        thermox::service::ReadinessState::ready;
                }) &&
            std::all_of(
                response.readiness.entities.begin(),
                response.readiness.entities.end(),
                [](const auto& entity) {
                    return entity.state ==
                        thermox::service::ReadinessState::ready;
                }),
        "successful validation must authorize the exact revision "
        "set through every readiness layer and entity");
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
    const auto validation_json =
        thermox::service::serialize_validate_response_json(response);
    require(
        validation_json.find("\"structural_blocks\": [") !=
                std::string::npos &&
            validation_json.find(
                "\"largest_structural_block_size\":") !=
                std::string::npos &&
            validation_json.find(
                "\"suggested_tear_variable_names\":") !=
                std::string::npos &&
            validation_json.find(
                "\"acyclic_after_suggested_tears\": true") !=
                std::string::npos &&
            validation_json.find(
                "\"suggested_inner_nonzero_count\":") !=
                std::string::npos &&
            validation_json.find(
                "\"suggested_dense_schur_entry_count\":") !=
                std::string::npos,
        "validation JSON must expose block and tearing structure");

    thermox::service::ValidateModelRequest round_trip;
    round_trip.model_json = response.canonical_model_json;
    const auto reparsed = service.validate_model(round_trip);
    require(
        reparsed.succeeded(),
        "canonical service model must be parseable");
    require(
        reparsed.model.model_id == response.model.model_id,
        "canonical round trip must preserve model identity");

    auto unknown_case = request;
    unknown_case.case_id = "not-declared";
    const auto study_blocked = service.validate_model(unknown_case);
    const auto blocked_study_layer = std::find_if(
        study_blocked.readiness.layers.begin(),
        study_blocked.readiness.layers.end(),
        [](const auto& layer) {
            return layer.id == "study";
        });
    require(
        !study_blocked.readiness.calculatable &&
            blocked_study_layer !=
                study_blocked.readiness.layers.end() &&
            blocked_study_layer->state ==
                thermox::service::ReadinessState::blocked &&
            !study_blocked.diagnostics.empty() &&
            study_blocked.diagnostics.front().code ==
                "unknown_case" &&
            study_blocked.diagnostics.front().stage == "study",
        "an unknown operating case must block study readiness");

    thermox::service::ValidateModelRequest malformed;
    malformed.model_json = "{not-json";
    const auto draft_blocked = service.validate_model(malformed);
    const auto blocked_draft_layer = std::find_if(
        draft_blocked.readiness.layers.begin(),
        draft_blocked.readiness.layers.end(),
        [](const auto& layer) {
            return layer.id == "draft";
        });
    require(
        !draft_blocked.readiness.calculatable &&
            blocked_draft_layer !=
                draft_blocked.readiness.layers.end() &&
            blocked_draft_layer->state ==
                thermox::service::ReadinessState::blocked &&
            std::count_if(
                draft_blocked.readiness.layers.begin(),
                draft_blocked.readiness.layers.end(),
                [](const auto& layer) {
                    return layer.state == thermox::service::
                        ReadinessState::not_evaluated;
                }) == 5,
        "a malformed document must block draft readiness without "
        "claiming that downstream layers were evaluated");

    thermox::service::SteadySimulationRequest simulation;
    simulation.model_json = response.canonical_model_json;
    simulation.case_id = "design";
    const auto solved = service.run_steady(simulation);
    require(
        solved.succeeded(),
        "canonical model must preserve dimensional solve semantics: " +
            solved.error.message);
}

void test_homogeneous_two_phase_local_loss() {
    thermox::service::SimulationService service;
    thermox::service::SteadySimulationRequest request;
    request.model_json = R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "two_phase_local_loss",
    "media": [{
      "id": "water",
      "backend": "water_steam_if97",
      "substance": "Water"
    }],
    "components": [{
      "id": "source",
      "kind": "source.fluid.boundary",
      "media": {"outlet": "water"}
    }, {
      "id": "loss",
      "kind": "restriction.fluid.local_loss.homogeneous_two_phase",
      "parameters": {
        "flow_diameter": {"value": 50.0, "unit": "mm"},
        "loss_coefficient": 42.0
      },
      "media": {"inlet": "water", "outlet": "water"}
    }, {
      "id": "sink",
      "kind": "sink.fluid.boundary",
      "media": {"inlet": "water"}
    }],
    "connections": [{
      "id": "source_to_loss",
      "kind": "fluid_link",
      "from": "source.outlet",
      "to": "loss.inlet"
    }, {
      "id": "loss_to_sink",
      "kind": "fluid_link",
      "from": "loss.outlet",
      "to": "sink.inlet"
    }]
  },
  "cases": [{
    "id": "forward",
    "mode": "steady_state_design",
    "fixed_values": {
      "source.outlet.m_dot": {"value": 0.05, "unit": "kg/s"},
      "source.outlet.p": {"value": 2.0, "unit": "bar"},
      "source.outlet.h": {"value": 1500.0, "unit": "kJ/kg"}
    },
    "initial_guesses": {
      "loss.inlet.m_dot": {"value": 0.05, "unit": "kg/s"},
      "loss.inlet.p": {"value": 2.0, "unit": "bar"},
      "loss.inlet.h": {"value": 1500.0, "unit": "kJ/kg"},
      "loss.outlet.m_dot": {"value": 0.05, "unit": "kg/s"},
      "loss.outlet.p": {"value": 1.95, "unit": "bar"},
      "loss.outlet.h": {"value": 1500.0, "unit": "kJ/kg"}
    }
  }]
})json";
    request.case_id = "forward";
    const auto response = service.run_steady(request);
    require(
        response.succeeded() && response.diagnostics.converged,
        "homogeneous two-phase local loss must solve: " +
            response.error.message);
    const auto& inlet = require_port_result(
        response.graph, "loss", "inlet");
    const auto& outlet = require_port_result(
        response.graph, "loss", "outlet");
    require(
        inlet.phase == "two_phase" && outlet.phase == "two_phase" &&
            require_result_value(
                inlet.primary_values, "p").value_si >
                require_result_value(
                    outlet.primary_values, "p").value_si &&
            std::abs(require_result_value(
                inlet.primary_values, "m_dot").value_si -
                require_result_value(
                    outlet.primary_values, "m_dot").value_si) <
                1.0e-12 &&
            std::abs(require_result_value(
                inlet.primary_values, "h").value_si -
                require_result_value(
                    outlet.primary_values, "h").value_si) <
                1.0e-9,
        "two-phase local loss must conserve mass and enthalpy while "
        "producing a positive pressure drop");
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

    double combustor_total_mass_flow = 0.0;
    for (const auto& value : combustor_outlet.primary_values) {
        if (value.name.starts_with("m_dot[")) {
            combustor_total_mass_flow += value.value_si;
        }
    }
    require(
        std::abs(
            require_result_value(
                combustor_outlet.derived_values, "m_dot_total")
                .value_si -
            combustor_total_mass_flow) < 1.0e-10,
        "material result must expose total mass flow");
    double mass_fraction_sum = 0.0;
    for (const auto& value : combustor_outlet.derived_values) {
        if (value.name.starts_with("mass_fraction[")) {
            mass_fraction_sum += value.value_si;
        }
    }
    require(
        std::abs(mass_fraction_sum - 1.0) < 1.0e-12,
        "material result species mass fractions must sum to one");

    require(
        std::abs(
            require_result_value(
                compressor_shaft.primary_values, "W_dot")
                    .value_si -
            34.80152099e6) < 100.0,
        "Brayton compressor power must match the standalone "
        "Cantera reference");
    require(
        std::abs(
            require_result_value(
                turbine_shaft.primary_values, "W_dot")
                    .value_si -
            69.86066885e6) < 100.0,
        "Brayton turbine power must match the standalone "
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
        "standalone Cantera reference");
    require(
        std::abs(
            require_result_value(
                turbine_outlet.derived_values, "T")
                    .value_si -
            864.300347) < 1.0e-3,
        "Brayton exhaust temperature must match the standalone "
        "Cantera reference");
    const auto benchmark_summary =
        thermox::service::project_steady_result(
            response.graph,
            {
                {
                    "net_electric_power",
                    thermox::service::ResultValueScope::port_primary,
                    "generator",
                    "electrical",
                    "P",
                    "power",
                    thermox::service::ResultAggregation::final,
                },
                {
                    "turbine_inlet_temperature",
                    thermox::service::ResultValueScope::port_derived,
                    "combustor",
                    "outlet",
                    "T",
                    "temperature",
                    thermox::service::ResultAggregation::final,
                },
                {
                    "turbine_exhaust_temperature",
                    thermox::service::ResultValueScope::port_derived,
                    "turbine",
                    "outlet",
                    "T",
                    "temperature",
                    thermox::service::ResultAggregation::final,
                },
            });
    auto benchmark_observations = thermox::service::
        validation_observations_from_result_summary(
            benchmark_summary);
    benchmark_observations.push_back({
        "normalized_residual",
        "dimensionless",
        response.diagnostics.final_residual_norm,
    });
    const auto evidence =
        thermox::service::evaluate_validation_evidence(
            benchmark_observations,
            {
                {
                    "brayton_net_power_reference",
                    "net_electric_power",
                    thermox::service::ValidationEvidenceLayer::system,
                    thermox::service::ValidationEvidenceBasis::
                        derived_reference,
                    "power",
                    33.84513305e6,
                    100.0,
                    0.0,
                    "Standalone direct Cantera calculation",
                    "Uses the same thermochemistry mechanism as the "
                    "registered property path.",
                },
                {
                    "brayton_firing_temperature_reference",
                    "turbine_inlet_temperature",
                    thermox::service::ValidationEvidenceLayer::component,
                    thermox::service::ValidationEvidenceBasis::
                        derived_reference,
                    "temperature",
                    1418.696978,
                    1.0e-3,
                    0.0,
                    "Standalone direct Cantera calculation",
                    "Verifies graph integration rather than an external "
                    "OEM combustor test.",
                },
                {
                    "brayton_exhaust_temperature_reference",
                    "turbine_exhaust_temperature",
                    thermox::service::ValidationEvidenceLayer::component,
                    thermox::service::ValidationEvidenceBasis::
                        derived_reference,
                    "temperature",
                    864.300347,
                    1.0e-3,
                    0.0,
                    "Standalone direct Cantera calculation",
                    "Verifies component composition and property calls.",
                },
                {
                    "brayton_numerical_closure",
                    "normalized_residual",
                    thermox::service::ValidationEvidenceLayer::numerical,
                    thermox::service::ValidationEvidenceBasis::
                        internal_consistency,
                    "dimensionless",
                    0.0,
                    1.0e-10,
                    0.0,
                    "Thermox scaled equation system",
                    "Closure is not external cycle validation.",
                },
            },
            {
                "The reference uses the same Cantera mechanism and is "
                "not an independent property source.",
                "The design point specifies component pressure ratios "
                "and efficiencies rather than OEM maps.",
            });
    require(
        evidence.passed && evidence.passed_count == 4U &&
            evidence.classes.front().passed_count == 0U &&
            evidence.classes[3].passed_count == 3U &&
            evidence.classes[4].passed_count == 1U,
        "Brayton evidence must remain derived integration evidence, "
        "not be promoted to independent OEM validation");
}

void test_netl_b31a_hrsg_boundary_benchmark() {
    thermox::service::SimulationService service;
    thermox::service::SteadySimulationRequest request;
    request.model_json = read_source_file(
        "benchmarks/netl_b31a/hrsg_boundary.json");
    request.case_id = "published_boundary";
    request.solver.continuation_enabled = true;
    const auto response = service.run_steady(request);

    require(
        response.succeeded(),
        "NETL B31A HRSG boundary benchmark must solve: " +
            response.error.message);
    require(
        response.diagnostics.converged &&
            response.diagnostics.final_residual_norm < 1.0e-10,
        "NETL B31A numerical equations must close independently "
        "of the published-data discrepancy");
    require(
        response.continuation.converged &&
            response.continuation.reached_parameter == 1.0,
        "NETL B31A benchmark must reach the full published "
        "steam-side heat duty");

    const auto& inlet = require_port_result(
        response.graph, "aggregate_hrsg", "inlet");
    const auto& outlet = require_port_result(
        response.graph, "aggregate_hrsg", "outlet");
    const double inlet_temperature = require_result_value(
        inlet.derived_values, "T").value_si;
    const double outlet_temperature = require_result_value(
        outlet.derived_values, "T").value_si;
    require(
        std::abs(inlet_temperature - 925.15) < 1.0e-3,
        "NETL B31A exhaust inlet boundary must retain 652 degC");
    require(
        std::abs(
            require_result_value(
                inlet.derived_values, "rho").value_si -
            0.4) < 0.01 &&
            std::abs(
                require_result_value(
                    inlet.derived_values,
                    "mean_molecular_weight").value_si -
                0.028357) < 1.0e-6,
        "Cantera exhaust density and molecular weight must agree "
        "with the published stream table at displayed precision");
    require(
        std::abs(outlet_temperature - 352.15) < 7.0,
        "steam-side heat recovery must predict the published 79 "
        "degC stack temperature within the declared 7 K boundary "
        "audit tolerance");
    const auto projected = thermox::service::project_steady_result(
        response.graph,
        {{
            "stack_temperature",
            thermox::service::ResultValueScope::port_derived,
            "aggregate_hrsg",
            "outlet",
            "T",
            "temperature",
            thermox::service::ResultAggregation::final,
        }});
    auto observations = thermox::service::
        validation_observations_from_result_summary(projected);
    observations.push_back({
        "normalized_residual",
        "dimensionless",
        response.diagnostics.final_residual_norm,
    });
    const auto evidence =
        thermox::service::evaluate_validation_evidence(
            observations,
            {
                {
                    "stack_temperature_boundary_agreement",
                    "stack_temperature",
                    thermox::service::ValidationEvidenceLayer::system,
                    thermox::service::ValidationEvidenceBasis::
                        boundary_constrained,
                    "temperature",
                    352.15,
                    7.0,
                    0.0,
                    "NETL B31A Exhibit 4-8 stack temperature",
                    "The calculation is driven by the published "
                    "design-point steam-side heat duty.",
                },
                {
                    "scaled_equation_closure",
                    "normalized_residual",
                    thermox::service::ValidationEvidenceLayer::numerical,
                    thermox::service::ValidationEvidenceBasis::
                        internal_consistency,
                    "dimensionless",
                    0.0,
                    1.0e-10,
                    0.0,
                    "Thermox scaled equation system",
                    "Numerical closure is separate from external "
                    "engineering agreement.",
                },
            },
            {
                "The published gas-side and steam-side HRSG duties "
                "differ by 26.035 GJ/h.",
                "No detailed HRSG geometry or off-design correlations "
                "are published.",
            });
    require(
        evidence.passed && evidence.passed_count == 2U &&
            evidence.classes.front().passed_count == 0U &&
            evidence.classes[1].passed_count == 1U &&
            evidence.classes[4].passed_count == 1U,
        "B31A HRSG evidence must pass without being mislabeled as "
        "independent predictive validation");

    constexpr const char* species[] = {
        "m_dot[O2]", "m_dot[H2O]", "m_dot[CO2]",
        "m_dot[N2]", "m_dot[AR]"};
    double mass_flow = 0.0;
    for (const auto* name : species) {
        const double inlet_flow = require_result_value(
            inlet.primary_values, name).value_si;
        const double outlet_flow = require_result_value(
            outlet.primary_values, name).value_si;
        require(
            std::abs(inlet_flow - outlet_flow) < 1.0e-9,
            "aggregate HRSG must conserve each exhaust species");
        mass_flow += inlet_flow;
    }
    require(
        std::abs(mass_flow - 3859805.0 / 3600.0) < 1.0e-9,
        "NETL B31A exhaust flow boundary must retain the published "
        "total flow");
    const double recovered_heat = mass_flow * (
        require_result_value(inlet.primary_values, "h").value_si -
        require_result_value(outlet.primary_values, "h").value_si);
    require(
        std::abs(recovered_heat - 684970477.475) < 1.0,
        "aggregate HRSG must conserve the independently derived "
        "water/steam-side heat duty");

    request.case_id = "published_gas_enthalpy";
    const auto gas_side = service.run_steady(request);
    require(
        gas_side.succeeded() && gas_side.diagnostics.converged &&
            gas_side.diagnostics.final_residual_norm < 1.0e-10,
        "NETL B31A gas-side enthalpy benchmark must solve");
    const auto& gas_side_outlet = require_port_result(
        gas_side.graph, "aggregate_hrsg", "outlet");
    require(
        std::abs(
            require_result_value(
                gas_side_outlet.derived_values, "T").value_si -
            352.15) < 1.0,
        "Cantera must reproduce the published stack temperature "
        "within 1 K when driven by the published Aspen enthalpy "
        "difference");
    require(
        std::abs(
            require_result_value(
                gas_side_outlet.derived_values, "rho").value_si -
            1.0) < 0.04,
        "Cantera stack density must agree with the published "
        "stream table at displayed precision");
}

void test_netl_b31a_segmented_triple_pressure_hrsg() {
    thermox::service::SimulationService service;
    thermox::service::SteadySimulationRequest request;
    request.model_json = read_source_file(
        "benchmarks/netl_b31a/segmented_hrsg.json");
    request.case_id = "published_boundary_decomposition";
    request.solver.continuation_enabled = true;
    const auto response = service.run_steady(request);

    require(
        response.succeeded() && response.diagnostics.converged &&
            response.continuation.converged &&
            response.continuation.reached_parameter == 1.0,
        "segmented triple-pressure reheat HRSG must solve: " +
            response.error.message);
    require(
        response.diagnostics.final_residual_norm < 1.0e-10,
        "segmented HRSG equations must close independently of its "
        "engineering evidence class");

    const auto& exhaust = require_port_result(
        response.graph, "exhaust_inlet", "outlet");
    const auto& stack = require_port_result(
        response.graph, "stack", "inlet");
    const auto temperature = [](const auto& port) {
        return require_result_value(
            port.derived_values, "T").value_si;
    };
    std::vector<double> gas_temperatures = {temperature(exhaust)};
    for (const auto* component : {
             "hp_superheater", "reheater", "ip_superheater",
             "lp_superheater", "hp_evaporator", "ip_evaporator",
             "lp_evaporator", "hp_economizer", "ip_economizer",
             "lp_economizer"}) {
        gas_temperatures.push_back(temperature(require_port_result(
            response.graph, component, "hot_out")));
    }
    require(
        std::adjacent_find(
            gas_temperatures.begin(), gas_temperatures.end(),
            [](double upstream, double downstream) {
                return upstream <= downstream;
            }) == gas_temperatures.end(),
        "every declared HRSG surface must cool the serial exhaust "
        "path");
    for (const auto* species : {
             "m_dot[O2]", "m_dot[H2O]", "m_dot[CO2]",
             "m_dot[N2]", "m_dot[AR]"}) {
        require(
            std::abs(require_result_value(
                exhaust.primary_values, species).value_si -
                require_result_value(
                    stack.primary_values, species).value_si) <
                1.0e-9,
            "segmented HRSG must conserve every exhaust species");
    }

    const auto& hp_economizer_out = require_port_result(
        response.graph, "hp_economizer", "cold_out");
    const auto& hp_evaporator_out = require_port_result(
        response.graph, "hp_evaporator", "cold_out");
    const auto& ip_evaporator_out = require_port_result(
        response.graph, "ip_evaporator", "cold_out");
    const auto& lp_evaporator_out = require_port_result(
        response.graph, "lp_evaporator", "cold_out");
    require(
        hp_economizer_out.phase == "liquid" &&
            hp_evaporator_out.phase == "vapor" &&
            require_result_value(
                ip_evaporator_out.derived_values,
                "vapor_quality").value_si > 0.99 &&
            require_result_value(
                lp_evaporator_out.derived_values,
                "vapor_quality").value_si > 0.99,
        "all three pressure circuits must traverse their declared "
        "economizer and evaporation state progression");

    const auto& main_steam = require_port_result(
        response.graph, "main_steam", "inlet");
    const auto& hot_reheat = require_port_result(
        response.graph, "hot_reheat", "inlet");
    const auto& lp_steam = require_port_result(
        response.graph, "lp_steam", "inlet");
    const auto primary = [](const auto& port, const char* name) {
        return require_result_value(
            port.primary_values, name).value_si;
    };
    require(
        std::abs(primary(main_steam, "h") - 3520.51e3) < 1.0e-3,
        "segmented HRSG must reproduce main-steam enthalpy");
    require(
        std::abs(primary(hot_reheat, "h") - 3641.17e3) < 1.0e-3,
        "segmented HRSG must reproduce hot-reheat enthalpy");
    require(
        std::abs(primary(lp_steam, "h") - 3071.95e3) < 1.0e-3,
        "segmented HRSG must reproduce LP-steam enthalpy");
    require(
        std::abs(
            primary(hot_reheat, "m_dot") -
            159.865277777778) < 1.0e-9,
        "IP make-up and cold reheat flows must mix exactly");

    const double exhaust_mass =
        primary(exhaust, "m_dot[O2]") +
        primary(exhaust, "m_dot[H2O]") +
        primary(exhaust, "m_dot[CO2]") +
        primary(exhaust, "m_dot[N2]") +
        primary(exhaust, "m_dot[AR]");
    const double gas_duty = exhaust_mass *
        (primary(exhaust, "h") - primary(stack, "h"));
    require(
        std::abs(gas_duty - 684.970522136112e6) < 1.0,
        "segmented gas-side duty must equal all declared surfaces");
    require(
        std::abs(require_result_value(
            response.graph.system_balances,
            "net_boundary_mass_flow").value_si) < 1.0e-9 &&
            std::abs(require_result_value(
                response.graph.system_balances,
                "net_boundary_energy_flow").value_si) < 1.0,
        "segmented HRSG must close whole-system steady mass and "
        "energy balances");

    const auto& thermal_feasibility = response.thermal_feasibility;
    const auto approach_of = [&](const char* component_id) {
        const auto found = std::find_if(
            thermal_feasibility.counterflow_approaches.begin(),
            thermal_feasibility.counterflow_approaches.end(),
            [&](const auto& result) {
                return result.component_id == component_id;
            });
        require(
            found != thermal_feasibility.counterflow_approaches.end(),
            std::string("missing thermal approach result for ") +
                component_id);
        return *found;
    };
    require(
        !thermal_feasibility.passed &&
            thermal_feasibility.scope == "steady" &&
            thermal_feasibility.checked_count == 10U &&
            thermal_feasibility.failed_count > 0U &&
            approach_of("hp_superheater").passed &&
            !approach_of("reheater").passed &&
            !approach_of("hp_economizer").passed,
        "generic terminal-approach audit must reject the unpublished "
        "surface ordering even though its equations close");
    const auto thermal_json =
        thermox::service::serialize_thermal_feasibility_summary_json(
            thermal_feasibility);
    require(
        thermal_json.find("thermox.thermal_feasibility/v1") !=
                std::string::npos &&
            thermal_json.find("hp_economizer") !=
                std::string::npos &&
            thermal_json.find("\"passed\": false") !=
                std::string::npos,
        "thermal-feasibility evidence must serialize its failed "
        "component verdicts");
    const auto durable_response_json =
        thermox::service::serialize_steady_response_json(response);
    require(
        durable_response_json.find("\"thermal_feasibility\"") !=
                std::string::npos &&
            durable_response_json.find(
                "counterflow_minimum_approach") !=
                std::string::npos,
        "durable steady result must contain both aggregate physical "
        "evidence and projectable component metrics");
    const auto reheater_acceptance_projection =
        thermox::service::project_steady_result(
            response.graph,
            {{
                "reheater_minimum_approach",
                thermox::service::ResultValueScope::component_metric,
                "reheater",
                {},
                "counterflow_minimum_approach",
                "temperature_difference",
                thermox::service::ResultAggregation::final,
            }});
    const auto reheater_acceptance =
        thermox::service::evaluate_engineering_acceptance(
            reheater_acceptance_projection,
            {{
                "positive_reheater_terminal_approach",
                "reheater_minimum_approach",
                "temperature_difference",
                0.0,
                std::nullopt,
                true,
                true,
            }});
    require(
        !reheater_acceptance.passed &&
            reheater_acceptance.failed_count == 1U &&
            reheater_acceptance.criteria.front().actual_value_si < 0.0,
        "ordinary Study engineering acceptance must be able to "
        "block a thermally inadmissible exchanger result");
    auto tampered_feasibility = thermal_feasibility;
    tampered_feasibility.failed_count = 0U;
    bool tampered_feasibility_rejected = false;
    try {
        thermox::service::validate_thermal_feasibility_summary(
            tampered_feasibility);
    } catch (const thermox::service::ThermalFeasibilityError&) {
        tampered_feasibility_rejected = true;
    }
    require(
        tampered_feasibility_rejected,
        "durable thermal-feasibility evidence must reject tampered "
        "aggregate counts");

    const auto evidence =
        thermox::service::evaluate_validation_evidence(
            {
                {"stack_temperature", "temperature", temperature(stack)},
                {"main_steam_enthalpy", "specific_enthalpy",
                 primary(main_steam, "h")},
                {"hot_reheat_enthalpy", "specific_enthalpy",
                 primary(hot_reheat, "h")},
                {"lp_steam_enthalpy", "specific_enthalpy",
                 primary(lp_steam, "h")},
                {"normalized_residual", "dimensionless",
                 response.diagnostics.final_residual_norm},
            },
            {
                {
                    "stack_temperature_boundary_agreement",
                    "stack_temperature",
                    thermox::service::ValidationEvidenceLayer::system,
                    thermox::service::ValidationEvidenceBasis::
                        boundary_constrained,
                    "temperature", 352.15, 7.0, 0.0,
                    "NETL B31A Exhibit 4-8 stack temperature",
                    "All segment duties sum to the published "
                    "steam-side HRSG boundary duty.",
                },
                {
                    "main_steam_calibrated_reproduction",
                    "main_steam_enthalpy",
                    thermox::service::ValidationEvidenceLayer::system,
                    thermox::service::ValidationEvidenceBasis::
                        calibrated_reproduction,
                    "specific_enthalpy", 3520.51e3, 1.0, 0.0,
                    "NETL B31A Exhibit 4-8 stream 5",
                    "The fixed segment duties were partitioned to "
                    "reproduce this boundary state.",
                },
                {
                    "hot_reheat_calibrated_reproduction",
                    "hot_reheat_enthalpy",
                    thermox::service::ValidationEvidenceLayer::system,
                    thermox::service::ValidationEvidenceBasis::
                        calibrated_reproduction,
                    "specific_enthalpy", 3641.17e3, 1.0, 0.0,
                    "NETL B31A Exhibit 4-8 stream 7",
                    "The reheater duty was derived from this boundary "
                    "state and the declared mixer balance.",
                },
                {
                    "lp_steam_calibrated_reproduction",
                    "lp_steam_enthalpy",
                    thermox::service::ValidationEvidenceLayer::system,
                    thermox::service::ValidationEvidenceBasis::
                        calibrated_reproduction,
                    "specific_enthalpy", 3071.95e3, 1.0, 0.0,
                    "NETL B31A Exhibit 4-8 stream 9",
                    "The fixed segment duties were partitioned to "
                    "reproduce this boundary state.",
                },
                {
                    "scaled_equation_closure",
                    "normalized_residual",
                    thermox::service::ValidationEvidenceLayer::numerical,
                    thermox::service::ValidationEvidenceBasis::
                        internal_consistency,
                    "dimensionless", 0.0, 1.0e-10, 0.0,
                    "Thermox scaled equation system",
                    "Numerical closure is separate from external "
                    "engineering validation.",
                },
            },
            {
                "The source does not publish coil-by-coil duties, UA, "
                "geometry, pinch, approach, or pressure losses.",
                "Intermediate HP/IP/LP enthalpy targets and gas-path "
                "surface ordering are explicit topology assumptions.",
                "Feed pumps, drums, circulation, blowdown, attemperation, "
                "ambient loss, and the 1 kg/h source rounding imbalance "
                "remain outside this steady boundary decomposition.",
            });
    require(
        evidence.passed && evidence.passed_count == 5U &&
            evidence.classes.front().passed_count == 0U &&
            evidence.classes[1].passed_count == 1U &&
            evidence.classes[2].passed_count == 3U &&
            evidence.classes[4].passed_count == 1U,
        "segmented HRSG must not promote calibrated topology "
        "reproduction to independent predictive validation");
}

void test_dynamic_cantera_if97_hrsg_cell() {
    thermox::service::SimulationService service;
    thermox::service::TransientSimulationRequest request;
    request.model_json = read_source_file(
        "core/examples/dynamic_exhaust_water_hrsg_cell.json");
    request.case_id = "heat_up";
    request.solver.end_time = 0.1;
    request.solver.max_step = 0.05;
    const auto response = service.run_transient(request);

    require(
        response.succeeded(),
        "dynamic Cantera-to-IF97 HRSG cell must solve: " +
            response.error.message);
    require(
        response.diagnostics.success &&
            response.diagnostics.accepted_steps > 0 &&
            !response.trajectory.empty(),
        "dynamic material-to-fluid cell must complete adaptive DAE "
        "integration");

    const auto& graph = response.trajectory.back().graph;
    const auto& hot_in = require_port_result(
        graph, "hrsg_cell", "hot_in");
    const auto& hot_out = require_port_result(
        graph, "hrsg_cell", "hot_out");
    for (const auto* species : {
             "m_dot[N2]", "m_dot[O2]", "m_dot[H2O]",
             "m_dot[CO2]"}) {
        require(
            std::abs(
                require_result_value(
                    hot_in.primary_values, species).value_si -
                require_result_value(
                    hot_out.primary_values, species).value_si) <
                1.0e-10,
            "dynamic HRSG cell must conserve each exhaust species");
    }
    require(
        require_result_value(
            hot_out.primary_values, "h").value_si <
            require_result_value(
                hot_in.primary_values, "h").value_si,
        "dynamic HRSG cell must cool the exhaust stream");

    const auto& cold_in = require_port_result(
        graph, "hrsg_cell", "cold_in");
    const auto& cold_out = require_port_result(
        graph, "hrsg_cell", "cold_out");
    require(
        require_result_value(
            cold_out.primary_values, "h").value_si >
            require_result_value(
                cold_in.primary_values, "h").value_si,
        "dynamic HRSG cell must heat the IF97 water stream");
}

void test_distributed_cantera_if97_counterflow_exchanger() {
    thermox::service::SimulationService service;
    const auto model = read_source_file(
        "core/examples/two_cell_counterflow_exhaust_water.json");

    thermox::service::SteadySimulationRequest steady_request;
    steady_request.model_json = model;
    steady_request.case_id = "steady";
    const auto steady = service.run_steady(steady_request);
    require(
        steady.succeeded() && steady.diagnostics.converged,
        "distributed Cantera-to-IF97 exchanger must solve: " +
            steady.error.message);

    const auto& source = require_port_result(
        steady.graph, "exhaust_source", "outlet");
    const auto& cell_1_in = require_port_result(
        steady.graph, "cell_1", "hot_in");
    const auto& cell_1_out = require_port_result(
        steady.graph, "cell_1", "hot_out");
    const auto& cell_2_in = require_port_result(
        steady.graph, "cell_2", "hot_in");
    const auto& cell_2_out = require_port_result(
        steady.graph, "cell_2", "hot_out");
    const auto& stack = require_port_result(
        steady.graph, "stack", "inlet");
    for (const auto* species : {
             "m_dot[N2]", "m_dot[O2]", "m_dot[H2O]",
             "m_dot[CO2]"}) {
        const double expected = require_result_value(
            source.primary_values, species).value_si;
        for (const auto* port : {
                 &cell_1_in, &cell_1_out, &cell_2_in,
                 &cell_2_out, &stack}) {
            require(
                std::abs(require_result_value(
                    port->primary_values, species).value_si -
                    expected) < 1.0e-10,
                "distributed exchanger must conserve every exhaust "
                "species through every cell");
        }
    }
    require(
        require_result_value(
            source.primary_values, "p").value_si >
            require_result_value(
                cell_1_out.primary_values, "p").value_si &&
        require_result_value(
            cell_1_out.primary_values, "p").value_si >
            require_result_value(
                cell_2_out.primary_values, "p").value_si,
        "distributed exchanger hot pressure must fall through both "
        "cells");

    const auto& water_source = require_port_result(
        steady.graph, "water_source", "outlet");
    const auto& cell_2_cold_out = require_port_result(
        steady.graph, "cell_2", "cold_out");
    const auto& water_sink = require_port_result(
        steady.graph, "water_sink", "inlet");
    const double hot_duty =
        require_result_value(
            source.primary_values, "h").value_si -
        require_result_value(
            stack.primary_values, "h").value_si;
    const double cold_duty = require_result_value(
        water_source.primary_values, "m_dot").value_si * (
        require_result_value(
            water_sink.primary_values, "h").value_si -
        require_result_value(
            water_source.primary_values, "h").value_si);
    require(
        hot_duty > 0.0 && std::abs(hot_duty - cold_duty) < 1.0e-4,
        "distributed exchanger must close steady hot/cold duty");
    require(
        require_result_value(
            water_source.primary_values, "p").value_si >
            require_result_value(
                cell_2_cold_out.primary_values, "p").value_si &&
        require_result_value(
            cell_2_cold_out.primary_values, "p").value_si >
            require_result_value(
                water_sink.primary_values, "p").value_si,
        "counterflow water pressure must fall through both cells");

    thermox::service::TransientSimulationRequest transient_request;
    transient_request.model_json = model;
    transient_request.case_id = "heat_up";
    transient_request.solver.end_time = 0.1;
    transient_request.solver.max_step = 0.05;
    const auto transient = service.run_transient(transient_request);
    require(
        transient.succeeded() && transient.diagnostics.success &&
            !transient.trajectory.empty(),
        "distributed Cantera-to-IF97 transient must solve: " +
            transient.error.message);
    const auto& final = transient.trajectory.back().graph;
    const auto& final_stack = require_port_result(
        final, "stack", "inlet");
    require(
        std::isfinite(require_result_value(
            final_stack.derived_values, "T").value_si),
        "transient material ports must expose thermochemistry-derived "
        "state values");
    double stored_energy_rate = 0.0;
    constexpr double wall_capacity = 50000.0;
    for (const auto* cell_id : {"cell_1", "cell_2"}) {
        const auto& cell = require_component_result(final, cell_id);
        const auto& cold_energy = require_result_value(
            cell.internal_values, "cold_total_energy");
        const auto& wall_temperature = require_result_value(
            cell.internal_values, "wall_temperature");
        require(
            cold_energy.has_derivative &&
                wall_temperature.has_derivative,
            "distributed exchanger must expose storage derivatives");
        stored_energy_rate += cold_energy.derivative_si_s +
            wall_capacity * wall_temperature.derivative_si_s;
    }
    const double boundary_energy_rate = require_result_value(
        final.system_balances,
        "net_boundary_energy_flow").value_si;
    require(
        std::abs(stored_energy_rate - boundary_energy_rate) < 1.0e-4,
        "distributed transient must close whole-system stored-energy "
        "rate against boundary enthalpy flow");
    const auto& final_water_source = require_port_result(
        final, "water_source", "outlet");
    const auto& final_water_sink = require_port_result(
        final, "water_sink", "inlet");
    require(
        require_result_value(
            final_water_sink.primary_values, "h").value_si >
            require_result_value(
                final_water_source.primary_values, "h").value_si,
        "distributed transient must heat the counterflow water path");
}

void test_dynamic_equilibrium_two_phase_material_fluid_cell() {
    thermox::service::SimulationService service;
    thermox::service::TransientSimulationRequest request;
    request.model_json = read_source_file(
        "core/examples/dynamic_two_phase_rigid_volume_cell.json");
    request.case_id = "boil";
    request.solver.end_time = 1.0;
    request.solver.max_step = 0.1;
    const auto response = service.run_transient(request);
    require(
        response.succeeded() && response.diagnostics.success &&
            response.trajectory.size() > 1,
        "dynamic equilibrium two-phase cell must solve: " +
            response.error.message);
    require(
        response.thermal_feasibility.scope == "trajectory" &&
            response.thermal_feasibility.checked_count == 1U &&
            std::all_of(
                response.thermal_feasibility.counterflow_approaches.begin(),
                response.thermal_feasibility.counterflow_approaches.end(),
                [](const auto& result) {
                    return result.has_sample_time &&
                        std::isfinite(result.sample_time);
                }),
        "transient heat-transfer result must retain the worst terminal "
        "approach and sample time for every exchanger");
    const auto transient_approach_projection =
        thermox::service::project_transient_result(
            response.trajectory,
            {{
                "minimum_evaporator_approach",
                thermox::service::ResultValueScope::component_metric,
                "evaporating_cell",
                {},
                "counterflow_minimum_approach",
                "temperature_difference",
                thermox::service::ResultAggregation::minimum,
            }});
    require(
        transient_approach_projection.values.size() == 1U &&
            std::abs(
                transient_approach_projection.values.front().value_si -
                response.thermal_feasibility.counterflow_approaches
                    .front().minimum_approach_k) < 1.0e-10 &&
            transient_approach_projection.values.front().has_sample_time,
        "Study projections must expose the trajectory-wide limiting "
        "counterflow approach and its sample time");
    const auto durable_transient_json =
        thermox::service::serialize_transient_response_json(response);
    require(
        durable_transient_json.find("\"scope\": \"trajectory\"") !=
                std::string::npos &&
            durable_transient_json.find("\"sample_time\":") !=
                std::string::npos,
        "durable transient result must retain limiting physical "
        "evidence with time attribution");

    const auto& initial_graph = response.trajectory.front().graph;
    const auto& final_graph = response.trajectory.back().graph;
    const auto& initial_cell = require_component_result(
        initial_graph, "evaporating_cell");
    const auto& final_cell = require_component_result(
        final_graph, "evaporating_cell");
    const auto& initial_mass = require_result_value(
        initial_cell.internal_values, "fluid_mass");
    const auto& final_mass = require_result_value(
        final_cell.internal_values, "fluid_mass");
    const auto& initial_quality = require_result_value(
        initial_cell.internal_values, "vapor_quality");
    const auto& final_quality = require_result_value(
        final_cell.internal_values, "vapor_quality");
    const auto& initial_pressure = require_result_value(
        initial_cell.internal_values, "fluid_pressure");
    const auto& final_pressure = require_result_value(
        final_cell.internal_values, "fluid_pressure");
    require(
        std::abs(final_mass.value_si - initial_mass.value_si -
                 1.0e-4) < 1.0e-9 &&
            final_mass.has_derivative &&
            std::abs(final_mass.derivative_si_s - 1.0e-4) < 1.0e-12,
        "two-phase inventory mass must integrate inlet minus outlet "
        "flow");
    require(
        initial_quality.value_si > 0.0 &&
            final_quality.value_si < 1.0 &&
            final_quality.value_si > initial_quality.value_si,
        "heated equilibrium inventory must remain inside the dome "
        "and increase vapor quality");
    require(
        final_pressure.value_si > initial_pressure.value_si,
        "heated rigid two-phase inventory must develop pressure");

    const auto& initial_outlet = require_port_result(
        initial_graph, "evaporating_cell", "cold_out");
    const auto& final_outlet = require_port_result(
        final_graph, "evaporating_cell", "cold_out");
    require(
        initial_outlet.phase == "two_phase" &&
            final_outlet.phase == "two_phase" &&
            std::abs(require_result_value(
                final_outlet.derived_values,
                "vapor_quality").value_si -
                final_quality.value_si) < 1.0e-10,
        "two-phase outlet result must expose the equilibrium quality");
    require(
        std::abs(final_mass.value_si /
            require_result_value(
                final_outlet.derived_values, "rho").value_si -
            1.0) < 1.0e-10,
        "two-phase mass and density must close the declared rigid "
        "volume");

    const auto& hot_in = require_port_result(
        final_graph, "evaporating_cell", "hot_in");
    const auto& hot_out = require_port_result(
        final_graph, "evaporating_cell", "hot_out");
    for (const auto* species : {
             "m_dot[N2]", "m_dot[O2]", "m_dot[H2O]",
             "m_dot[CO2]"}) {
        require(
            std::abs(require_result_value(
                hot_in.primary_values, species).value_si -
                require_result_value(
                    hot_out.primary_values, species).value_si) <
                1.0e-10,
            "two-phase cell must conserve each hot-gas species");
    }

    const auto& fluid_energy = require_result_value(
        final_cell.internal_values, "fluid_total_energy");
    const auto& wall_temperature = require_result_value(
        final_cell.internal_values, "wall_temperature");
    require(
        fluid_energy.has_derivative &&
            wall_temperature.has_derivative,
        "two-phase cell must expose fluid and wall storage rates");
    const double stored_energy_rate =
        fluid_energy.derivative_si_s +
        50000.0 * wall_temperature.derivative_si_s;
    const double boundary_energy_rate = require_result_value(
        final_graph.system_balances,
        "net_boundary_energy_flow").value_si;
    const double boundary_mass_rate = require_result_value(
        final_graph.system_balances,
        "net_boundary_mass_flow").value_si;
    require(
        std::abs(stored_energy_rate - boundary_energy_rate) < 1.0e-3,
        "two-phase fluid and wall storage must close against net "
        "boundary energy flow");
    require(
        std::abs(boundary_mass_rate -
                 final_mass.derivative_si_s) < 1.0e-12,
        "two-phase inventory accumulation must close against net "
        "boundary mass flow");
}

void test_composed_dynamic_single_pressure_hrsg() {
    thermox::service::SimulationService service;
    thermox::service::TransientSimulationRequest request;
    request.model_json = read_source_file(
        "core/examples/dynamic_single_pressure_hrsg.json");
    request.case_id = "startup_segment";
    request.solver.end_time = 0.1;
    request.solver.max_step = 0.05;
    const auto response = service.run_transient(request);
    require(
        response.succeeded() && response.diagnostics.success &&
            response.trajectory.size() > 1,
        "composed single-pressure HRSG must solve: " +
            response.error.message);

    const auto& final = response.trajectory.back().graph;
    const auto& source = require_port_result(
        final, "exhaust_source", "outlet");
    const auto& superheater_hot_out = require_port_result(
        final, "superheater", "hot_out");
    const auto& evaporator_hot_out = require_port_result(
        final, "evaporator", "hot_out");
    const auto& stack = require_port_result(
        final, "stack", "inlet");
    const auto temperature = [](const auto& port) {
        return require_result_value(
            port.derived_values, "T").value_si;
    };
    require(
        temperature(source) > temperature(superheater_hot_out) &&
            temperature(superheater_hot_out) >
                temperature(evaporator_hot_out) &&
            temperature(evaporator_hot_out) > temperature(stack),
        "HRSG exhaust temperature must fall through superheater, "
        "evaporator, and economizer in gas-flow order");
    for (const auto* species : {
             "m_dot[N2]", "m_dot[O2]", "m_dot[H2O]",
             "m_dot[CO2]"}) {
        require(
            std::abs(require_result_value(
                source.primary_values, species).value_si -
                require_result_value(
                    stack.primary_values, species).value_si) <
                1.0e-10,
            "composed HRSG must conserve every exhaust species");
    }

    const auto& feedwater = require_port_result(
        final, "feedwater", "outlet");
    const auto& evaporator_outlet = require_port_result(
        final, "evaporator", "cold_out");
    const auto& restriction_outlet = require_port_result(
        final, "evaporator_outlet_loss", "outlet");
    const auto& drum_vapor = require_port_result(
        final, "separator_drum", "vapor_outlet");
    const auto& steam = require_port_result(
        final, "superheater", "cold_out");
    require(
        feedwater.phase == "liquid" &&
            evaporator_outlet.phase == "two_phase" &&
            restriction_outlet.phase == "two_phase" &&
            drum_vapor.phase == "two_phase" &&
            require_result_value(
                drum_vapor.derived_values,
                "vapor_quality").value_si > 1.0 - 1.0e-10 &&
            steam.phase == "vapor" &&
            temperature(steam) > temperature(drum_vapor),
        "HRSG water path must progress through liquid, two-phase, "
        "separated vapor, and superheated-vapor states");
    require(
        require_result_value(
            evaporator_outlet.primary_values, "p").value_si >
            require_result_value(
                restriction_outlet.primary_values, "p").value_si &&
            require_result_value(
                restriction_outlet.primary_values,
                "m_dot").value_si > 0.0,
        "two-phase hydraulic impedance must sustain positive flow "
        "and a positive pressure drop");

    const auto& evaporator = require_component_result(
        final, "evaporator");
    const auto& drum = require_component_result(
        final, "separator_drum");
    const double stored_mass_rate = require_result_value(
        evaporator.internal_values,
        "fluid_mass").derivative_si_s + require_result_value(
        drum.internal_values, "total_mass").derivative_si_s;
    const double boundary_mass_rate = require_result_value(
        final.system_balances,
        "net_boundary_mass_flow").value_si;
    require(
        std::abs(stored_mass_rate - boundary_mass_rate) < 1.0e-10,
        "coupled evaporator and drum inventories must close the "
        "whole-system mass rate");

    const auto& superheater = require_component_result(
        final, "superheater");
    const auto& economizer = require_component_result(
        final, "economizer");
    const auto storage_rate = [](const auto& component,
                                 const char* energy,
                                 double wall_capacity) {
        return require_result_value(
            component.internal_values, energy).derivative_si_s +
            wall_capacity * require_result_value(
                component.internal_values,
                "wall_temperature").derivative_si_s;
    };
    const double stored_energy_rate =
        storage_rate(
            superheater, "cold_total_energy", 300000.0) +
        storage_rate(
            evaporator, "fluid_total_energy", 50000.0) +
        storage_rate(
            economizer, "cold_total_energy", 30000.0) +
        require_result_value(
            drum.internal_values,
            "total_internal_energy").derivative_si_s;
    const double boundary_energy_rate = require_result_value(
        final.system_balances,
        "net_boundary_energy_flow").value_si;
    require(
        std::abs(stored_energy_rate - boundary_energy_rate) < 1.0e-2,
        "all HRSG fluid, drum, and wall storage rates must close "
        "against boundary enthalpy flow");
}

void test_dynamic_forced_circulation_evaporator() {
    thermox::service::SimulationService service;
    thermox::service::TransientSimulationRequest request;
    request.model_json = read_source_file(
        "core/examples/dynamic_forced_circulation_evaporator.json");
    request.case_id = "circulation_segment";
    request.solver.end_time = 0.1;
    request.solver.max_step = 0.05;
    const auto response = service.run_transient(request);
    require(
        response.succeeded() && response.diagnostics.success &&
            response.trajectory.size() > 1,
        "forced-circulation evaporator must solve: " +
            response.error.message);

    const auto& final = response.trajectory.back().graph;
    const auto& source = require_port_result(
        final, "exhaust_source", "outlet");
    const auto& stack = require_port_result(
        final, "stack", "inlet");
    require(
        require_result_value(
            source.derived_values, "T").value_si >
            require_result_value(
                stack.derived_values, "T").value_si,
        "circulation evaporator must cool the exhaust stream");
    for (const auto* species : {
             "m_dot[N2]", "m_dot[O2]", "m_dot[H2O]",
             "m_dot[CO2]"}) {
        require(
            std::abs(require_result_value(
                source.primary_values, species).value_si -
                require_result_value(
                    stack.primary_values, species).value_si) <
                1.0e-10,
            "circulation evaporator must conserve each exhaust "
            "species");
    }

    const auto& pump_in = require_port_result(
        final, "circulation_pump", "inlet");
    const auto& pump_out = require_port_result(
        final, "circulation_pump", "outlet");
    const auto& inertance_in = require_port_result(
        final, "circulation_inertance", "inlet");
    const auto& inertance_out = require_port_result(
        final, "circulation_inertance", "outlet");
    const auto& riser_in = require_port_result(
        final, "riser_loss", "inlet");
    const auto& riser_out = require_port_result(
        final, "riser_loss", "outlet");
    const auto& flow = require_result_value(
        inertance_out.primary_values, "m_dot");
    require(
        require_result_value(
            pump_out.primary_values, "p").value_si >
            require_result_value(
                pump_in.primary_values, "p").value_si &&
            require_result_value(
                inertance_in.primary_values, "p").value_si >
            require_result_value(
                inertance_out.primary_values, "p").value_si &&
            require_result_value(
                riser_in.primary_values, "p").value_si >
            require_result_value(
                riser_out.primary_values, "p").value_si &&
            flow.value_si > 0.0 && flow.has_derivative &&
            flow.derivative_si_s > 0.0,
        "pump head must drive an accelerating positive loop flow "
        "through momentum storage and two-phase resistance");
    require(
        pump_in.phase == "two_phase" || pump_in.phase == "liquid",
        "drum liquid return must remain on the liquid saturation "
        "boundary");
    require(
        riser_in.phase == "two_phase" &&
            riser_out.phase == "two_phase",
        "the evaporator riser must transport a two-phase mixture");

    const auto& evaporator = require_component_result(
        final, "evaporator");
    const auto& drum = require_component_result(
        final, "separator_drum");
    const double stored_mass_rate = require_result_value(
        evaporator.internal_values,
        "fluid_mass").derivative_si_s + require_result_value(
        drum.internal_values, "total_mass").derivative_si_s;
    require(
        std::abs(stored_mass_rate) < 2.0e-10 &&
            std::abs(require_result_value(
                final.system_balances,
                "net_boundary_mass_flow").value_si) < 1.0e-12,
        "closed circulation loop must conserve combined water mass");
    const double stored_energy_rate = require_result_value(
        evaporator.internal_values,
        "fluid_total_energy").derivative_si_s +
        100000.0 * require_result_value(
            evaporator.internal_values,
            "wall_temperature").derivative_si_s +
        require_result_value(
            drum.internal_values,
            "total_internal_energy").derivative_si_s;
    const double boundary_energy_rate = require_result_value(
        final.system_balances,
        "net_boundary_energy_flow").value_si;
    require(
        std::abs(stored_energy_rate - boundary_energy_rate) < 1.0e-2,
        "circulation-loop fluid, drum, and wall storage must close "
        "against exhaust heat and pump-work boundaries");
}

void test_dynamic_natural_circulation_evaporator() {
    thermox::service::SimulationService service;
    thermox::service::TransientSimulationRequest request;
    request.model_json = read_source_file(
        "core/examples/dynamic_natural_circulation_evaporator.json");
    request.case_id = "buoyancy_segment";
    request.solver.end_time = 0.1;
    request.solver.max_step = 0.05;
    const auto response = service.run_transient(request);
    require(
        response.succeeded() && response.diagnostics.success &&
            response.trajectory.size() > 1,
        "natural-circulation evaporator must solve: " +
            response.error.message);

    const auto& final = response.trajectory.back().graph;
    require(
        std::none_of(
            final.components.begin(), final.components.end(),
            [](const auto& component) {
                return component.kind.starts_with("pump.");
            }),
        "natural-circulation reference must contain no pump");
    const auto& down_in = require_port_result(
        final, "downcomer_gravity_loss", "inlet");
    const auto& down_out = require_port_result(
        final, "downcomer_gravity_loss", "outlet");
    const auto& riser_in = require_port_result(
        final, "riser_gravity_loss", "inlet");
    const auto& riser_out = require_port_result(
        final, "riser_gravity_loss", "outlet");
    const auto& inertance_out = require_port_result(
        final, "downcomer_inertance", "outlet");
    const auto& circulation_flow = require_result_value(
        inertance_out.primary_values, "m_dot");
    const double down_density = require_result_value(
        down_in.derived_values, "rho").value_si;
    const double down_quality = require_result_value(
        down_in.derived_values, "vapor_quality").value_si;
    const double riser_density = require_result_value(
        riser_in.derived_values, "rho").value_si;
    require(
        (down_in.phase == "liquid" ||
         (down_in.phase == "two_phase" &&
          down_quality >= 0.0 && down_quality < 1.0e-10)) &&
            riser_in.phase == "two_phase" &&
            riser_out.phase == "two_phase" &&
            down_density > 100.0 * riser_density,
        "natural-circulation loop must resolve a dense liquid "
        "downcomer and low-density two-phase riser");
    require(
        require_result_value(
            down_out.primary_values, "p").value_si >
            require_result_value(
                down_in.primary_values, "p").value_si &&
            require_result_value(
                riser_in.primary_values, "p").value_si >
            require_result_value(
                riser_out.primary_values, "p").value_si &&
            circulation_flow.value_si > 0.10 &&
            circulation_flow.has_derivative &&
            circulation_flow.derivative_si_s > 0.0,
        "density-head imbalance must raise downcomer pressure and "
        "accelerate positive circulation without mechanical drive");

    const auto& source = require_port_result(
        final, "exhaust_source", "outlet");
    const auto& stack = require_port_result(
        final, "stack", "inlet");
    require(
        require_result_value(source.derived_values, "T").value_si >
            require_result_value(
                stack.derived_values, "T").value_si,
        "natural-circulation evaporator must cool the exhaust");
    for (const auto* species : {
             "m_dot[N2]", "m_dot[O2]", "m_dot[H2O]",
             "m_dot[CO2]"}) {
        require(
            std::abs(require_result_value(
                source.primary_values, species).value_si -
                require_result_value(
                    stack.primary_values, species).value_si) <
                1.0e-10,
            "natural-circulation evaporator must conserve each "
            "exhaust species");
    }

    const auto& evaporator = require_component_result(
        final, "evaporator");
    const auto& drum = require_component_result(
        final, "separator_drum");
    const double stored_mass_rate = require_result_value(
        evaporator.internal_values,
        "fluid_mass").derivative_si_s + require_result_value(
        drum.internal_values, "total_mass").derivative_si_s;
    require(
        std::abs(stored_mass_rate) < 2.0e-10 &&
            std::abs(require_result_value(
                final.system_balances,
                "net_boundary_mass_flow").value_si) < 1.0e-12,
        "natural-circulation inventories must conserve combined "
        "water mass");
    const double stored_energy_rate = require_result_value(
        evaporator.internal_values,
        "fluid_total_energy").derivative_si_s +
        100000.0 * require_result_value(
            evaporator.internal_values,
            "wall_temperature").derivative_si_s +
        require_result_value(
            drum.internal_values,
            "total_internal_energy").derivative_si_s;
    const double boundary_energy_rate = require_result_value(
        final.system_balances,
        "net_boundary_energy_flow").value_si;
    require(
        std::abs(stored_energy_rate - boundary_energy_rate) < 1.0e-2,
        "natural-circulation storage must close against the exhaust "
        "heat boundary without hidden pump work");
}
#endif

void test_netl_b31a_steam_stream_property_benchmark() {
    thermox::service::SimulationService service;
    thermox::service::SteadySimulationRequest request;
    request.model_json = read_source_file(
        "benchmarks/netl_b31a/steam_stream_states.json");
    request.case_id = "published_states";
    const auto response = service.run_steady(request);
    require(
        response.succeeded(),
        "NETL B31A steam-state benchmark must solve: " +
            response.error.message);
    require(
        response.diagnostics.converged &&
            response.diagnostics.final_residual_norm < 1.0e-10,
        "NETL B31A independent IF97 state equations must close");
    require(
        response.diagnostics.iterations <= 1,
        "fixed pressure/temperature states must use property-informed "
        "enthalpy initialization");

    struct PublishedState {
        const char* id;
        double enthalpy_j_kg;
        double density_kg_m3;
        double density_tolerance;
    };
    constexpr PublishedState states[] = {
        {"stream_5", 3520.51e3, 48.1, 0.1},
        {"stream_6", 3124.08e3, 15.1, 0.1},
        {"stream_7", 3641.17e3, 9.9, 0.1},
        {"stream_8", 3062.57e3, 2.0, 0.1},
        {"stream_9", 3071.95e3, 1.9, 0.1},
        {"stream_11", 160.78e3, 992.8, 0.3},
    };
    std::vector<thermox::service::ResultProjection>
        enthalpy_projections;
    std::vector<thermox::service::ValidationEvidenceCriterion>
        enthalpy_criteria;
    for (const auto& expected : states) {
        const auto& port = require_port_result(
            response.graph, expected.id, "outlet");
        require(
            std::abs(
                require_result_value(
                    port.primary_values, "h").value_si -
                expected.enthalpy_j_kg) < 2500.0,
            std::string(expected.id) +
                " IF97 enthalpy must agree with the published "
                "steam-table value within 2.5 kJ/kg");
        require(
            std::abs(
                require_result_value(
                    port.derived_values, "rho").value_si -
                expected.density_kg_m3) <
                    expected.density_tolerance,
            std::string(expected.id) +
                " IF97 density must agree at published precision");
        const std::string observation_id =
            std::string(expected.id) + "_enthalpy";
        enthalpy_projections.push_back({
            observation_id,
            thermox::service::ResultValueScope::port_primary,
            expected.id,
            "outlet",
            "h",
            "specific_enthalpy",
            thermox::service::ResultAggregation::final,
        });
        enthalpy_criteria.push_back({
            observation_id + "_agreement",
            observation_id,
            thermox::service::ValidationEvidenceLayer::property,
            thermox::service::ValidationEvidenceBasis::
                independent_reference,
            "specific_enthalpy",
            expected.enthalpy_j_kg,
            2500.0,
            0.0,
            "NETL B31A published steam-stream table",
            "Published enthalpy is not imposed on the PT state solve.",
        });
    }

    const auto property_summary =
        thermox::service::project_steady_result(
            response.graph, enthalpy_projections);
    auto property_observations = thermox::service::
        validation_observations_from_result_summary(property_summary);
    property_observations.push_back({
        "normalized_residual",
        "dimensionless",
        response.diagnostics.final_residual_norm,
    });
    enthalpy_criteria.push_back({
        "property_state_numerical_closure",
        "normalized_residual",
        thermox::service::ValidationEvidenceLayer::numerical,
        thermox::service::ValidationEvidenceBasis::internal_consistency,
        "dimensionless",
        0.0,
        1.0e-10,
        0.0,
        "Thermox scaled equation system",
        "Closure does not substitute for property agreement.",
    });
    const auto property_evidence =
        thermox::service::evaluate_validation_evidence(
            property_observations,
            enthalpy_criteria,
            {
                "Published pressures and temperatures have limited "
                "display precision.",
                "Steam-table reference conventions may differ from IF97.",
            });
    require(
        property_evidence.passed &&
            property_evidence.passed_count == 7U &&
            property_evidence.classes.front().passed_count == 6U &&
            property_evidence.classes[4].passed_count == 1U,
        "B31A steam-property evidence must distinguish independent "
        "published outputs from numerical closure");

    const auto& exhaust = require_port_result(
        response.graph, "stream_10", "outlet");
    const double exhaust_temperature = require_result_value(
        exhaust.derived_values, "T").value_si;
    const double exhaust_quality = require_result_value(
        exhaust.derived_values, "vapor_quality").value_si;
    require(
        std::abs(exhaust_temperature - 311.869076244) < 1.0e-3 &&
            exhaust_quality > 0.90 && exhaust_quality < 0.93,
        "the detailed 1 psia LP exhaust boundary must resolve as "
        "wet steam near the published temperature with explicit "
        "quality");
}

void test_netl_b31a_decomposed_steam_turbine_train() {
    thermox::service::SimulationService service;
    thermox::service::SteadySimulationRequest request;
    request.model_json = read_source_file(
        "benchmarks/netl_b31a/steam_turbine_train.json");
    request.case_id = "published_design";
    const auto response = service.run_steady(request);
    require(
        response.succeeded(),
        "NETL B31A decomposed steam train must solve: " +
            response.error.message);
    require(
        response.diagnostics.converged &&
            response.diagnostics.final_residual_norm < 1.0e-10,
        "decomposed steam-train equations must close independently "
        "of published-data tolerances");

    struct StageTarget {
        const char* id;
        double outlet_pressure_pa;
        double outlet_enthalpy_j_kg;
        double shaft_power_w;
    };
    constexpr StageTarget targets[] = {
        {"hp_turbine", 4.13e6, 3125122.33267645,
         59811657.3037643},
        {"ip_turbine", 0.52e6, 3064091.52889555,
         92600048.3263533},
        {"lp_turbine", 6894.757293168, 2375860.0,
         126292155.644046},
    };
    double total_shaft_power = 0.0;
    std::vector<thermox::service::ResultProjection>
        stage_power_projections;
    std::vector<thermox::service::ValidationEvidenceCriterion>
        stage_power_criteria;
    for (const auto& target : targets) {
        const auto& outlet = require_port_result(
            response.graph, target.id, "outlet");
        const auto& shaft = require_port_result(
            response.graph, target.id, "shaft");
        require(
            std::abs(
                require_result_value(
                    outlet.primary_values, "p").value_si -
                target.outlet_pressure_pa) < 1.0e-3 &&
            std::abs(
                require_result_value(
                    outlet.primary_values, "h").value_si -
                target.outlet_enthalpy_j_kg) < 1.0e-3,
            std::string(target.id) +
                " must reproduce its independently calibrated "
                "design outlet state");
        const double power = require_result_value(
            shaft.primary_values, "W_dot").value_si;
        require(
            std::abs(power - target.shaft_power_w) < 1.0,
            std::string(target.id) +
                " shaft balance must remain reproducible");
        total_shaft_power += power;
        const std::string observation_id =
            std::string(target.id) + "_shaft_power";
        stage_power_projections.push_back({
            observation_id,
            thermox::service::ResultValueScope::port_primary,
            target.id,
            "shaft",
            "W_dot",
            "power",
            thermox::service::ResultAggregation::final,
        });
        stage_power_criteria.push_back({
            observation_id + "_reproduction",
            observation_id,
            thermox::service::ValidationEvidenceLayer::component,
            thermox::service::ValidationEvidenceBasis::
                calibrated_reproduction,
            "power",
            target.shaft_power_w,
            1.0,
            0.0,
            "NETL B31A stage states and calibrated efficiency",
            "The stage efficiency is fitted from this design point.",
        });
    }
    require(
        std::abs(total_shaft_power - 278703861.274164) < 1.0,
        "decomposed HP/IP/LP shaft powers must aggregate exactly");
    require(
        std::abs(total_shaft_power * 0.975 / 1.0e6 - 272.0) < 0.4,
        "decomposed train must reproduce published generator power "
        "within source/property precision");
    const auto stage_power_summary =
        thermox::service::project_steady_result(
            response.graph, stage_power_projections);
    auto stage_power_observations = thermox::service::
        validation_observations_from_result_summary(
            stage_power_summary);
    stage_power_observations.push_back({
        "generator_power",
        "power",
        total_shaft_power * 0.975,
    });
    stage_power_observations.push_back({
        "normalized_residual",
        "dimensionless",
        response.diagnostics.final_residual_norm,
    });
    stage_power_criteria.push_back({
        "steam_generator_power_reproduction",
        "generator_power",
        thermox::service::ValidationEvidenceLayer::system,
        thermox::service::ValidationEvidenceBasis::
            calibrated_reproduction,
        "power",
        272.0e6,
        0.4e6,
        0.0,
        "NETL B31A steam-turbine generator power",
        "The same design point determines stage efficiencies.",
    });
    stage_power_criteria.push_back({
        "steam_train_numerical_closure",
        "normalized_residual",
        thermox::service::ValidationEvidenceLayer::numerical,
        thermox::service::ValidationEvidenceBasis::internal_consistency,
        "dimensionless",
        0.0,
        1.0e-10,
        0.0,
        "Thermox scaled equation system",
        "Closure validates execution, not off-design performance.",
    });
    const auto train_evidence =
        thermox::service::evaluate_validation_evidence(
            stage_power_observations,
            stage_power_criteria,
            {
                "Stage efficiencies are calibrated at the published "
                "design point.",
                "The HP leakage state is not independently published.",
                "No off-design stage-group maps are available.",
            });
    require(
        train_evidence.passed && train_evidence.passed_count == 5U &&
            train_evidence.classes.front().passed_count == 0U &&
            train_evidence.classes[2].passed_count == 4U &&
            train_evidence.classes[4].passed_count == 1U,
        "B31A steam-train evidence must remain explicitly calibrated "
        "rather than being promoted to predictive validation");

    const auto& cold_reheat = require_port_result(
        response.graph, "cold_reheat", "inlet");
    const auto& lp_exhaust = require_port_result(
        response.graph, "condenser_inlet", "inlet");
    require(
        std::abs(
            require_result_value(
                cold_reheat.primary_values, "m_dot").value_si -
            526630.0 / 3600.0) < 1.0e-9,
        "HP split must preserve the published cold-reheat flow");
    require(
        std::abs(
            require_result_value(
                lp_exhaust.primary_values, "m_dot").value_si -
            659752.0 / 3600.0) < 1.0e-3,
        "split, leakage, reheat, and LP admission routing must "
        "reproduce condenser flow within source rounding");
}

void test_netl_b31a_connected_hrsg_and_steam_cycle() {
    thermox::service::SimulationService service;
    thermox::service::SteadySimulationRequest request;
    request.model_json = read_source_file(
        "benchmarks/netl_b31a/connected_steam_cycle.json");
    request.case_id = "published_connected_cycle";
    request.solver.continuation_enabled = true;
    const auto response = service.run_steady(request);

    require(
        response.succeeded() && response.diagnostics.converged &&
            response.continuation.converged &&
            response.continuation.reached_parameter == 1.0,
        "connected B31A HRSG and steam cycle must solve: " +
            response.error.message);
    require(
        response.graph.components.size() == 28U &&
            response.diagnostics.final_residual_norm < 1.0e-10,
        "the connected benchmark must remain one closed 28-component "
        "nonlinear system");

    const auto primary = [](const auto& port, const char* name) {
        return require_result_value(
            port.primary_values, name).value_si;
    };
    const auto& main_steam = require_port_result(
        response.graph, "hp_superheater", "cold_out");
    const auto& hp_inlet = require_port_result(
        response.graph, "hp_turbine", "inlet");
    const auto& hot_reheat = require_port_result(
        response.graph, "reheater", "cold_out");
    const auto& ip_reheat_admission = require_port_result(
        response.graph, "ip_inlet_mixer", "inlet_a");
    const auto& lp_steam = require_port_result(
        response.graph, "lp_superheater", "cold_out");
    const auto& lp_admission = require_port_result(
        response.graph, "lp_inlet_mixer", "inlet_b");
    for (const auto* name : {"p", "h"}) {
        require(
            std::abs(
                primary(main_steam, name) -
                primary(hp_inlet, name)) < 1.0e-6 &&
                std::abs(
                    primary(hot_reheat, name) -
                    primary(ip_reheat_admission, name)) < 1.0e-6 &&
                std::abs(
                    primary(lp_steam, name) -
                    primary(lp_admission, name)) < 1.0e-6,
            std::string("connected HRSG/turbine interface must preserve ") +
                name);
    }
    require(
        std::abs(
            primary(main_steam, "m_dot") -
            primary(hp_inlet, "m_dot") -
            primary(require_port_result(
                response.graph, "gland_loss", "inlet"),
                "m_dot")) < 1.0e-9 &&
            std::abs(
                primary(hot_reheat, "m_dot") -
                primary(ip_reheat_admission, "m_dot")) < 1.0e-9 &&
            std::abs(
                primary(lp_steam, "m_dot") -
                primary(lp_admission, "m_dot")) < 1.0e-9,
        "connected interfaces must preserve flow including the explicit "
        "main-steam gland split");

    double total_shaft_power = 0.0;
    for (const auto* stage : {
             "hp_turbine", "ip_turbine", "lp_turbine"}) {
        const double power = primary(
            require_port_result(response.graph, stage, "shaft"),
            "W_dot");
        require(
            power > 50.0e6,
            std::string(stage) +
                " must produce positive utility-scale shaft power");
        total_shaft_power += power;
    }
    require(
        std::abs(total_shaft_power - 278508416.752932) < 1.0,
        "connected HP/IP/LP shaft power must remain reproducible");
    require(
        std::abs(total_shaft_power * 0.975 - 272.0e6) < 0.5e6,
        "connected steam cycle must reproduce published generator "
        "power within declared source and property precision");
    require(
        std::abs(require_result_value(
            response.graph.system_balances,
            "net_boundary_mass_flow").value_si) < 1.0e-9 &&
            std::abs(require_result_value(
                response.graph.system_balances,
                "net_boundary_energy_flow").value_si) < 1.0,
        "the connected system must close external mass and energy "
        "balances without coordinator arithmetic");

    require(
        !response.thermal_feasibility.passed &&
            response.thermal_feasibility.checked_count == 10U &&
            response.thermal_feasibility.failed_count == 4U,
        "the connected solve must preserve the HRSG ordering's known "
        "thermal infeasibility instead of promoting convergence to "
        "physical validation");

    const auto evidence =
        thermox::service::evaluate_validation_evidence(
            {
                {"generator_power", "power",
                 total_shaft_power * 0.975},
                {"normalized_residual", "dimensionless",
                 response.diagnostics.final_residual_norm},
            },
            {
                {
                    "connected_generator_power_reproduction",
                    "generator_power",
                    thermox::service::ValidationEvidenceLayer::system,
                    thermox::service::ValidationEvidenceBasis::
                        calibrated_reproduction,
                    "power", 272.0e6, 0.5e6, 0.0,
                    "NETL B31A steam-turbine generator power",
                    "Stage efficiencies and HRSG segment duties are "
                    "design-point calibration inputs.",
                },
                {
                    "connected_cycle_numerical_closure",
                    "normalized_residual",
                    thermox::service::ValidationEvidenceLayer::numerical,
                    thermox::service::ValidationEvidenceBasis::
                        internal_consistency,
                    "dimensionless", 0.0, 1.0e-10, 0.0,
                    "Thermox connected equation system",
                    "Closure validates execution, not off-design "
                    "prediction or HRSG coil ordering.",
                },
            },
            {
                "Segment duties and stage efficiencies are calibrated.",
                "Four assumed counterflow segment approaches are negative.",
                "Gas-turbine, condenser, and feedwater return equipment "
                "are outside this declaration.",
            });
    require(
        evidence.passed && evidence.passed_count == 2U,
        "connected benchmark evidence must pass only with its "
        "calibrated and internal-consistency qualifications");
}

void test_netl_b31a_published_balance_consistency() {
    constexpr double air_kg_h = 3764363.0;
    constexpr double fuel_kg_h = 95442.0;
    constexpr double exhaust_kg_h = 3859805.0;
    require(
        air_kg_h + fuel_kg_h == exhaust_kg_h,
        "NETL B31A gas-path mass balance must close exactly at "
        "published precision");

    constexpr double m5 = 542286.0;
    constexpr double m6 = 526630.0;
    constexpr double m7 = 575515.0;
    constexpr double m9 = 69218.0;
    constexpr double m10 = 659752.0;
    constexpr double m11 = 660390.0;
    require(
        std::abs((m11 + m6) - (m5 + m7 + m9)) <= 1.0,
        "NETL B31A HRSG water/steam mass balance must close within "
        "one kg/h of displayed rounding");

    constexpr double h3 = 872.29;
    constexpr double h4 = 226.68;
    constexpr double h5 = 3520.51;
    constexpr double h6 = 3124.08;
    constexpr double h7 = 3641.17;
    constexpr double h9 = 3071.95;
    constexpr double h10 = 2375.86;
    constexpr double h11 = 160.78;
    const double gas_recovery_gj_h =
        exhaust_kg_h * (h3 - h4) / 1.0e6;
    const double steam_recovery_gj_h =
        (m5 * h5 + m7 * h7 + m9 * h9 -
         m6 * h6 - m11 * h11) / 1.0e6;
    const double steam_shaft_mw =
        (m5 * h5 + m7 * h7 + m9 * h9 -
         m6 * h6 - m10 * h10) / 3.6e6;
    const double condenser_gj_h =
        m10 * (h10 - h11) / 1.0e6;
    require(
        std::abs(gas_recovery_gj_h - 2491.92870605) < 1.0e-6 &&
            std::abs(steam_recovery_gj_h - 2465.89371891) <
                1.0e-6,
        "NETL B31A detailed HRSG duties must remain reproducible");
    require(
        std::abs(
            (gas_recovery_gj_h - steam_recovery_gj_h) /
                gas_recovery_gj_h -
            0.010447725522) < 1.0e-9,
        "NETL B31A gas/steam HRSG discrepancy must stay distinct "
        "from the numerical residual");
    require(
        std::abs(steam_shaft_mw * 0.975 - 272.0) < 0.1,
        "NETL B31A stream enthalpies must reproduce published "
        "steam-generator terminal power");
    require(
        std::abs(condenser_gj_h - 1461.0) < 0.5,
        "NETL B31A stream enthalpies must reproduce published "
        "condenser duty");
    require(
        std::abs(741.0 / 1386.417 * 100.0 - 53.4) < 0.1 &&
            std::abs(1386.417 / 741.0 * 3600.0 - 6736.0) < 1.0,
        "NETL B31A net efficiency and heat rate must be internally "
        "consistent at published precision");
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
            response.diagnostics.front().stage == "physical" &&
            response.diagnostics.front().component_id == "custom" &&
            response.diagnostics.front().json_path ==
                "/model/components/0",
        "validation must return a stable catalog diagnostic");
    const auto physical = std::find_if(
        response.readiness.layers.begin(),
        response.readiness.layers.end(),
        [](const auto& layer) {
            return layer.id == "physical";
        });
    const auto component = std::find_if(
        response.readiness.entities.begin(),
        response.readiness.entities.end(),
        [](const auto& entity) {
            return entity.entity_type == "component" &&
                entity.entity_id == "custom";
        });
    require(
        !response.readiness.calculatable &&
            physical != response.readiness.layers.end() &&
            physical->state ==
                thermox::service::ReadinessState::blocked &&
            physical->diagnostic_codes ==
                std::vector<std::string>{"unknown_component_type"} &&
            component != response.readiness.entities.end() &&
            component->state ==
                thermox::service::ReadinessState::blocked,
        "readiness must block the physical layer and identify the "
        "affected component");
    const auto json =
        thermox::service::serialize_validate_response_json(response);
    require(
        json.find("\"readiness\": {\"calculatable\": false") !=
                std::string::npos &&
            json.find("\"id\": \"physical\", \"state\": \"blocked\"") !=
                std::string::npos &&
            json.find("\"entity_id\": \"custom\"") !=
                std::string::npos,
        "validation JSON must expose the authoritative readiness contract");
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

void test_hard_constraint_data_reconciliation_service() {
    thermox::service::SimulationService service;
    thermox::service::DataReconciliationRequest request;
    request.model_json =
        read_source_file("examples/data_reconciliation.json");
    request.reconciliation_id = "fixed_power_relaxed_airflow";
    request.solver.max_iterations = 6;
    request.solver.constraint_tolerance = 1.0e-8;
    request.held_out_cases = {{
        "test_point",
        {{
            "held_out_discharge_temperature",
            "compressor.outlet.T",
            "temperature",
            660.67570109108158,
            1.0,
        }},
    }};

    const auto response = service.run_data_reconciliation(request);
    require(
        response.succeeded(),
        "hard-constraint reconciliation must succeed: " +
            response.error.message);
    require(
        response.intent == thermox::service::
            CalculationIntent::data_reconciliation &&
            response.diagnostics.converged,
        "reconciliation must expose its distinct calculation intent and "
        "hard-constraint convergence");
    require(
        response.inferred_parameters.size() == 1 &&
            response.hard_constraints.size() == 1 &&
            response.held_out_results.size() == 1,
        "reconciliation must separate inferred quantities, hard "
        "constraints, and held-out results");
    require(
        std::abs(
            response.inferred_parameters.front().fitted_value_si -
            100.0) <= 1.0e-6,
        "fixed measured power should infer the originating airflow");
    require(
        std::abs(
            response.hard_constraints.front()
                .normalized_residual) <= 1.0e-8,
        "hard equality must satisfy the declared normalized tolerance");
    require(
        response.diagnostics
            .sensitivity_factorization_quality_available &&
            response.diagnostics
                .minimum_sensitivity_reciprocal_pivot_ratio > 0.0,
        "reconciliation must expose sensitivity factorization quality "
        "without mislabeling it as a matrix condition number");
    require(
        std::abs(
            response.held_out_results.front()
                .observations.front().predicted_si -
            660.67570109108158) <= 1.0e-7,
        "held-out output must be evaluated only after reconciliation");
    const auto json = thermox::service::
        serialize_data_reconciliation_response_json(response);
    require(
        json.find(
            "\"calculation_intent\": \"data_reconciliation\"") !=
                std::string::npos &&
            json.find("\"inferred_parameters\":") !=
                std::string::npos &&
            json.find("\"hard_constraints\":") !=
                std::string::npos &&
            json.find("\"held_out_results\":") !=
                std::string::npos,
        "reconciliation JSON must preserve semantic result separation");
}

void test_hard_reconciliation_reports_local_bound_limitation() {
    auto model = read_source_file("examples/data_reconciliation.json");
    const std::string feasible =
        "\"value\": 36.229874174599141, \"unit\": \"MW\"";
    const auto position = model.find(feasible);
    require(position != std::string::npos,
            "hard reconciliation fixture contains measured power");
    model.replace(
        position, feasible.size(),
        "\"value\": 60.0, \"unit\": \"MW\"");

    thermox::service::SimulationService service;
    thermox::service::DataReconciliationRequest request;
    request.model_json = std::move(model);
    request.reconciliation_id = "fixed_power_relaxed_airflow";
    request.solver.max_iterations = 6;
    const auto response = service.run_data_reconciliation(request);

    require(
        response.status ==
                thermox::service::OperationStatus::solver_failed &&
            response.error.code ==
                "reconciliation_locally_bound_limited" &&
            response.diagnostics.locally_bound_limited &&
            response.diagnostics.active_bound_count == 1 &&
            response.diagnostics.active_bounds.size() == 1 &&
            response.diagnostics.active_bounds.front().parameter_id ==
                "inferred_airflow" &&
            response.diagnostics.active_bounds.front().side == "upper" &&
            response.diagnostics.active_bounds.front().limits_local_step &&
            response.inferred_parameters.size() == 1 &&
            std::abs(
                response.inferred_parameters.front().fitted_value_si -
                120.0) < 1.0e-10 &&
            !response.hard_constraints.empty() &&
            std::abs(response.hard_constraints.front()
                         .normalized_residual) > 1.0,
        "hard reconciliation must distinguish physical-bound "
        "infeasibility from numerical or identifiability failure");
    const auto json = thermox::service::
        serialize_data_reconciliation_response_json(response);
    require(
        json.find("\"locally_bound_limited\": true") !=
                std::string::npos &&
            json.find(
                "\"parameter_id\": \"inferred_airflow\", "
                "\"side\": \"upper\"") !=
                std::string::npos &&
            json.find("\"limits_local_step\": true") !=
                std::string::npos,
        "bound-limited diagnostics must survive serialization");
}

void test_overdetermined_weighted_reconciliation_service() {
    thermox::service::SimulationService service;
    thermox::service::DataReconciliationRequest request;
    request.model_json =
        read_source_file("examples/data_reconciliation.json");
    request.reconciliation_id = "weighted_repeated_power";
    request.mode = thermox::service::
        ReconciliationMode::weighted_measurements;
    request.solver.max_iterations = 6;

    const auto response = service.run_data_reconciliation(request);
    require(
        response.succeeded(),
        "overdetermined weighted reconciliation must succeed: " +
            response.error.message);
    require(
        response.mode == thermox::service::
                ReconciliationMode::weighted_measurements &&
            response.hard_constraints.empty() &&
            response.weighted_measurements.size() == 2,
        "weighted measurements must remain distinct from hard "
        "equalities");
    require(
        std::abs(
            response.inferred_parameters.front().fitted_value_si -
            100.138007655668) <= 1.0e-6,
        "weighted reconciliation should infer the uncertainty-weighted "
        "airflow");
    require(
        response.diagnostics.adjustable_quantity_count == 1 &&
            response.diagnostics.measurement_count == 2 &&
            response.diagnostics.degrees_of_freedom == 1 &&
            response.diagnostics.sensitivity_rank == 1 &&
            response.diagnostics.locally_identifiable &&
            response.diagnostics
                .sensitivity_factorization_quality_method ==
                "reference-householder-cpqr-r-diagonal-ratio" &&
            response.diagnostics.reduced_chi_square_available &&
            std::abs(
                response.diagnostics.weighted_sum_squares - 0.5) <=
                1.0e-8 &&
            std::abs(
                response.diagnostics.reduced_chi_square - 0.5) <=
                1.0e-8,
        "weighted reconciliation must report CPQR rank, redundancy, and "
        "measurement consistency statistics");
    require(
            response.parameter_uncertainties.size() == 1 &&
            response.parameter_uncertainties.front()
                .standard_uncertainty_si.has_value() &&
            std::abs(
                *response.parameter_uncertainties.front()
                     .standard_uncertainty_si -
                0.195172298358) <= 1.0e-6 &&
            !response.parameter_uncertainties.front().bound_active,
        "known measurement uncertainties must propagate through the "
        "local sensitivity matrix");
    const auto json = thermox::service::
        serialize_data_reconciliation_response_json(response);
    require(
        json.find(
            "\"reconciliation_mode\": \"weighted_measurements\"") !=
                std::string::npos &&
            json.find("\"weighted_measurements\":") !=
                std::string::npos &&
            json.find("\"parameter_uncertainties\":") !=
                std::string::npos,
        "weighted reconciliation JSON must expose statistical meaning");
}

void test_weighted_reconciliation_rejects_rank_deficiency() {
    thermox::service::SimulationService service;
    thermox::service::DataReconciliationRequest request;
    request.model_json =
        read_source_file("examples/data_reconciliation.json");
    request.reconciliation_id = "unidentifiable_power_only";
    request.mode = thermox::service::
        ReconciliationMode::weighted_measurements;

    const auto response = service.run_data_reconciliation(request);
    require(
        response.status == thermox::service::
                OperationStatus::solver_failed &&
            response.error.code == "reconciliation_unidentifiable" &&
            response.diagnostics.sensitivity_rank == 1 &&
            response.diagnostics.adjustable_quantity_count == 2 &&
            !response.diagnostics.locally_identifiable,
        "two repeated measurements of one output must not identify two "
        "adjustable quantities");
}

void test_weighted_reconciliation_reports_parameter_correlation() {
    thermox::service::SimulationService service;
    thermox::service::DataReconciliationRequest request;
    request.model_json =
        read_source_file("examples/data_reconciliation.json");
    request.reconciliation_id = "weighted_airflow_efficiency";
    request.mode = thermox::service::
        ReconciliationMode::weighted_measurements;

    const auto response = service.run_data_reconciliation(request);
    require(
        response.succeeded() &&
            response.diagnostics.sensitivity_rank == 2 &&
            response.diagnostics.locally_identifiable &&
            response.parameter_uncertainties.size() == 2 &&
            response.parameter_correlations.size() == 1 &&
            response.parameter_correlations.front().correlation > 0.8 &&
            response.parameter_correlations.front().correlation < 0.9,
        "two identifiable inferred quantities must expose their local "
        "uncertainties and material parameter correlation");
}

void test_weighted_reconciliation_applies_measurement_correlation() {
    thermox::service::SimulationService service;
    thermox::service::DataReconciliationRequest request;
    request.model_json =
        read_source_file("examples/data_reconciliation.json");
    request.reconciliation_id = "weighted_correlated_power";
    request.mode = thermox::service::
        ReconciliationMode::weighted_measurements;
    request.solver.max_iterations = 6;
    request.profile_likelihood.enabled = true;
    request.profile_likelihood.objective_increase = 1.0;
    request.profile_likelihood.maximum_bracket_steps = 6;
    request.profile_likelihood.maximum_bisection_steps = 8;
    request.profile_likelihood.maximum_nuisance_iterations = 3;
    request.profile_likelihood.parameter_ids = {
        "shared_inferred_airflow"};

    const auto response = service.run_data_reconciliation(request);
    require(
        response.succeeded(),
        "correlated weighted reconciliation must succeed: " +
            response.error.message);
    require(
        response.diagnostics.measurement_correlation_count == 1 &&
            response.diagnostics.measurement_covariance_applied &&
            std::abs(
                response.inferred_parameters.front().fitted_value_si -
                100.138007655668) <= 1.0e-6 &&
            std::abs(
                response.diagnostics.weighted_sum_squares - 1.0) <=
                1.0e-8 &&
            response.parameter_uncertainties.size() == 1 &&
            response.parameter_uncertainties.front()
                .standard_uncertainty_si.has_value() &&
            std::abs(
                *response.parameter_uncertainties.front()
                     .standard_uncertainty_si -
                0.239036278783) <= 1.0e-6,
        "declared positive correlation must preserve the symmetric "
        "estimate while changing Mahalanobis chi-square and propagated "
        "uncertainty");
    require(
        response.profile_likelihood_intervals.size() == 1 &&
            response.profile_likelihood_intervals.front().succeeded &&
            response.profile_likelihood_intervals.front()
                .lower.threshold_reached &&
            response.profile_likelihood_intervals.front()
                .upper.threshold_reached &&
            !response.profile_likelihood_intervals.front()
                 .lower.bound_truncated &&
            !response.profile_likelihood_intervals.front()
                 .upper.bound_truncated &&
            std::abs(
                response.profile_likelihood_intervals.front()
                    .lower.value_si -
                (100.138007655668 - 0.239036278783)) <= 5.0e-3 &&
            std::abs(
                response.profile_likelihood_intervals.front()
                    .upper.value_si -
                (100.138007655668 + 0.239036278783)) <= 5.0e-3,
        "one-unit likelihood profile must agree with the local linear "
        "uncertainty for the public linear interior case");
    const auto json = thermox::service::
        serialize_data_reconciliation_response_json(response);
    require(
        json.find("\"measurement_covariance_applied\": true") !=
                std::string::npos &&
            json.find("\"profile_likelihood_intervals\": [") !=
                std::string::npos &&
            json.find("\"threshold_reached\": true") !=
                std::string::npos &&
            response.reconciled_model_json.find(
                "\"measurement_correlations\"") !=
                std::string::npos,
        "reconciliation JSON must disclose covariance whitening");

    thermox::service::CalibrationRequest calibration;
    calibration.model_json = request.model_json;
    calibration.calibration_id = "weighted_correlated_power";
    calibration.solver.max_iterations = 4;
    const auto calibration_response =
        service.run_calibration(calibration);
    require(
        calibration_response.succeeded() &&
            calibration_response.diagnostics
                    .measurement_correlation_count == 1 &&
            calibration_response.diagnostics
                .measurement_covariance_applied &&
            calibration_response.diagnostics.final_objective <
                calibration_response.diagnostics.initial_objective,
        "ordinary calibration must use and disclose the same correlated "
        "measurement objective");
}

void test_model_rejects_invalid_measurement_correlation() {
    auto model = read_source_file("examples/data_reconciliation.json");
    const std::string valid = "\"correlation\": 0.5";
    const auto position = model.find(valid);
    require(position != std::string::npos,
            "correlated reconciliation fixture contains coefficient");
    model.replace(position, valid.size(), "\"correlation\": 1.0");

    thermox::service::SimulationService service;
    thermox::service::DataReconciliationRequest request;
    request.model_json = std::move(model);
    request.reconciliation_id = "weighted_correlated_power";
    request.mode = thermox::service::
        ReconciliationMode::weighted_measurements;
    const auto response = service.run_data_reconciliation(request);
    require(
        response.status ==
                thermox::service::OperationStatus::invalid_model &&
            response.error.message.find(
                "strictly between -1 and 1") != std::string::npos,
        "model validation must reject an invalid correlation "
        "coefficient before reconciliation");
}

void test_weighted_reconciliation_reports_active_bound_uncertainty() {
    thermox::service::SimulationService service;
    thermox::service::DataReconciliationRequest request;
    request.model_json =
        read_source_file("examples/data_reconciliation.json");
    request.reconciliation_id = "weighted_active_upper_bound";
    request.mode = thermox::service::
        ReconciliationMode::weighted_measurements;
    request.solver.max_iterations = 6;
    request.profile_likelihood.enabled = true;
    request.profile_likelihood.objective_increase = 1.0;
    request.profile_likelihood.maximum_bracket_steps = 6;
    request.profile_likelihood.maximum_bisection_steps = 8;
    request.profile_likelihood.maximum_nuisance_iterations = 3;
    request.profile_likelihood.parameter_ids = {"bounded_airflow"};

    const auto response = service.run_data_reconciliation(request);
    require(
        response.succeeded(),
        "active-bound weighted reconciliation must converge: " +
            response.error.message);
    require(
        response.inferred_parameters.size() == 1 &&
            std::abs(
                response.inferred_parameters.front().fitted_value_si -
                120.0) <= 1.0e-10 &&
            response.diagnostics.active_bound_count == 1 &&
            response.diagnostics.free_uncertainty_parameter_count == 0 &&
            response.parameter_uncertainties.size() == 1 &&
            response.parameter_uncertainties.front().bound_active &&
            !response.parameter_uncertainties.front()
                 .standard_uncertainty_si.has_value() &&
            response.parameter_uncertainties.front().interpretation ==
                "bound_active_one_sided_not_estimated" &&
            response.parameter_correlations.empty(),
        "a constrained optimum must not receive an unconstrained "
        "symmetric standard uncertainty");
    require(
        response.profile_likelihood_intervals.size() == 1 &&
            response.profile_likelihood_intervals.front().succeeded &&
            response.profile_likelihood_intervals.front()
                .lower.threshold_reached &&
            response.profile_likelihood_intervals.front()
                .upper.bound_truncated &&
            !response.profile_likelihood_intervals.front()
                 .upper.threshold_reached &&
            response.profile_likelihood_intervals.front()
                    .upper.value_si == 120.0,
        "profile likelihood must expose the one-sided physical-bound "
        "truncation that local covariance cannot quantify");
    const auto json = thermox::service::
        serialize_data_reconciliation_response_json(response);
    require(
        json.find("\"standard_uncertainty_si\": null") !=
                std::string::npos &&
            json.find("\"active_bound_count\": 1") !=
                std::string::npos,
        "active-bound uncertainty semantics must survive serialization");
}

void test_profile_likelihood_rejects_invalid_intent() {
    thermox::service::SimulationService service;
    thermox::service::DataReconciliationRequest request;
    request.model_json =
        read_source_file("examples/data_reconciliation.json");
    request.reconciliation_id = "fixed_power_relaxed_airflow";
    request.profile_likelihood.enabled = true;
    const auto response = service.run_data_reconciliation(request);
    require(
        response.status ==
                thermox::service::OperationStatus::invalid_model &&
            response.error.message.find(
                "requires weighted-measurements") != std::string::npos,
        "profile likelihood must reject hard-equality intent rather "
        "than invent a measurement likelihood");
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

    const std::string declared =
        std::string{"{\"schema_version\":\"thermox.engineering_study/v1\","}
        + "\"model_document\":" +
        independent_study_model(baseline_power) +
        R"json(,"calibration_id":"baseline_fit",
          "calibration_solver":{"max_iterations":20},
          "prediction_cases":[{"case_id":"validation",
            "observations":[{"id":"validation_power",
              "target":"compressor.shaft.W_dot","dimension":"power",
              "measured_si":)json" +
        std::to_string(validation_power) +
        R"json(,"sigma_si":10000.0}]}]})json";
    const auto parsed = thermox::service::
        parse_engineering_study_request_json(declared);
    require(
        parsed.calibration_id == "baseline_fit" &&
            parsed.calibration_solver.max_iterations == 20 &&
            parsed.prediction_cases.size() == 1 &&
            parsed.prediction_cases.front().observations.size() == 1,
        "declarative engineering-study input must preserve the frozen "
        "calibration/prediction split");
    const auto declared_response = thermox::service::
        evaluate_engineering_study_json(declared);
    require(
        declared_response.succeeded() &&
            std::abs(
                declared_response.predictions.front()
                    .observations.front().residual_si) < 5000.0,
        "declarative engineering-study execution must use the same "
        "service path as direct callers");
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
        {}, thermox::platform::
                make_default_correlation_template_registry(),
        thermox::platform::
                make_default_regime_map_template_registry(),
        std::move(chemistry));
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
    definition.descriptor.template_kind = "custom.signal.gain";
    definition.descriptor.display_name = "Signal gain";
    definition.descriptor.category = "Project components";
    definition.descriptor.model_name = "Algebraic gain";
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
    component.template_kind = "custom.signal.gain";
    component.display_name = "Signal gain";
    component.category = "Project components";
    component.model_name = "Algebraic gain";
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

void test_transient_expression_component_flows_through_service() {
    thermox::service::SimulationService service;
    thermox::service::TransientSimulationRequest request;
    request.case_id = "step";
    request.model_json = R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "request_dynamic_expression",
    "media": [],
    "components": [{
      "id": "lag",
      "kind": "custom.signal.request_lag",
      "parameters": {"tau": {"value": 2.0, "unit": "s"}}
    }],
    "connections": []
  },
  "cases": [{
    "id": "step",
    "mode": "dynamic_transient",
    "fixed_values": {"lag.input.value": 1.0},
    "initial_guesses": {"lag.filtered": 0.0}
  }]
})json";
    thermox::service::ExpressionComponentInput component;
    component.schema_version = "thermox.expression_component/v3";
    component.kind = "custom.signal.request_lag";
    component.version = "1.0.0";
    component.template_kind = "control.first_order_lag";
    component.display_name = "First-order lag";
    component.category = "Project controls";
    component.model_name = "Safe transient expression";
    component.supports_steady = false;
    component.supports_transient = true;
    component.ports = {
        {"input", "signal", "in", 1},
        {"output", "signal", "out", 1},
    };
    component.parameters = {{
        "tau", "time", true, std::nullopt, 0.0,
        std::numeric_limits<double>::infinity(), false, true}};
    component.internal_variables = {{
        "filtered", "differential", 0.0, 1.0, 0.0, 1.0,
        -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(), "dimensionless"}};
    component.transient_equations = {
        {"state_balance",
         "parameter.tau * derivative.internal.filtered + "
         "internal.filtered - input.value", 1.0},
        {"output", "output.value - internal.filtered", 1.0},
    };
    request.components.expression_components.push_back(component);
    request.solver.end_time = 1.0;
    request.solver.initial_step = 0.1;
    request.solver.max_step = 0.2;

    const auto response = service.run_transient(request);
    require(response.succeeded(),
            "request-scoped transient expression must solve: " +
                response.error.message);
    const auto& lag = require_component_result(
        response.trajectory.back().graph, "lag");
    require(lag.internal_values.size() == 1 &&
                lag.internal_values.front().name == "filtered" &&
                lag.internal_values.front().value_si > 0.35 &&
                lag.internal_values.front().value_si < 0.45,
            "service must expose the integrated declared internal state");
}

void test_steady_service() {
    thermox::service::SimulationService service;
    require(
        thermox::service::structural_decomposition_policy_from_string(
            "tearing") ==
                thermox::service::StructuralDecompositionPolicy::tearing &&
            thermox::service::to_string(
                thermox::service::StructuralDecompositionPolicy::tearing) ==
                "tearing",
        "structural tearing policy must round-trip through transport and persistence text");
    thermox::service::SteadySimulationRequest request;
    request.model_json =
        read_source_file("core/examples/air_compressor.json");
    request.case_id = "design";
    request.solver.structural_decomposition_policy =
        thermox::service::StructuralDecompositionPolicy::blocks;
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
            "thermox.newton/v11" &&
            !response.metadata.solver.settings.empty(),
        "steady result must record solver contract");
    require(
        response.diagnostics
                .final_maximum_absolute_normalized_residual <=
            response.diagnostics.final_residual_norm &&
            !response.diagnostics.limiting_residual.empty() &&
            response.diagnostics.maximum_linear_backward_error <=
                request.solver.linear_residual_tolerance &&
            response.diagnostics.structural_block_solves == 0 &&
            response.diagnostics.largest_linear_system_size > 0,
        "steady result must expose residual and structural diagnostics: " +
            std::to_string(
                response.diagnostics.structural_block_solves) +
            " blocks, largest=" +
            std::to_string(
                response.diagnostics.largest_linear_system_size) +
            ", residual=" +
            std::to_string(response.diagnostics.final_residual_norm) +
            ", linear=" +
            std::to_string(
                response.diagnostics.maximum_linear_backward_error));
    require(
        std::any_of(
            response.metadata.solver.settings.begin(),
            response.metadata.solver.settings.end(),
            [&request](const auto& setting) {
                return setting.name ==
                           "linear_residual_tolerance" &&
                       setting.value ==
                           request.solver.linear_residual_tolerance;
            }),
        "steady provenance must record the linear accuracy contract");

    request.solver.structural_decomposition_policy =
        thermox::service::StructuralDecompositionPolicy::tearing;
    const auto tearing_response = service.run_steady(request);
    require(
        tearing_response.succeeded() &&
            tearing_response.diagnostics.linear_solver_backend.starts_with(
                "structural-schur-") &&
            tearing_response.diagnostics.structural_tearing_attempts > 0 &&
            tearing_response.diagnostics.structural_tearing_successes +
                    tearing_response.diagnostics.structural_tearing_fallbacks ==
                tearing_response.diagnostics.structural_tearing_attempts &&
            tearing_response.diagnostics
                    .factorization_quality_observations > 0 &&
            !tearing_response.diagnostics
                    .factorization_quality_method.empty() &&
            tearing_response.diagnostics
                    .accepted_pivot_count_at_minimum_ratio <=
                tearing_response.diagnostics
                    .factorization_size_at_minimum_ratio &&
            tearing_response.metadata.solver.contract_version ==
                "thermox.newton/v11",
        "service must expose the exact structural tearing policy with a safe fallback");
    require(
        std::any_of(
            response.metadata.solver.settings.begin(),
            response.metadata.solver.settings.end(),
            [](const auto& setting) {
                return setting.name ==
                           "structural_decomposition_policy" &&
                       setting.value == 2.0;
            }),
        "steady provenance must record structural solve policy");
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
            json.find("\"settings\": {") != std::string::npos &&
            json.find("\"limiting_residual\":") !=
                std::string::npos &&
            json.find("\"structural_tearing_attempts\":") !=
                std::string::npos &&
            json.find("\"last_structural_tearing_fallback\":") !=
                std::string::npos &&
            json.find("\"minimum_reciprocal_pivot_ratio\":") !=
                std::string::npos &&
            json.find("\"factorization_quality_method\":") !=
                std::string::npos &&
            json.find("\"maximum_linear_backward_error\":") !=
                std::string::npos &&
            json.find("\"linear_refinement_attempts\":") !=
                std::string::npos &&
            json.find("\"structural_block_solves\":") !=
                std::string::npos,
        "steady JSON must serialize complete execution provenance");
}

void test_structural_policy_audit_service() {
    thermox::service::SimulationService service;
    thermox::service::StructuralPolicyAuditRequest request;
    request.model_json =
        read_source_file("core/examples/air_compressor.json");
    request.case_id = "design";
    request.normalized_solution_tolerance = 1.0e-9;
    request.policies = {
        thermox::service::StructuralDecompositionPolicy::tearing,
        thermox::service::StructuralDecompositionPolicy::monolithic,
        thermox::service::StructuralDecompositionPolicy::tearing,
    };
    const auto response =
        service.run_structural_policy_audit(request);
    require(
        response.succeeded() &&
            response.schema_version ==
                thermox::service::structural_policy_audit_schema_v1 &&
            response.metadata.operation ==
                "structural_policy_audit" &&
            response.metadata.solver.contract_version ==
                thermox::service::structural_policy_audit_schema_v1 &&
            response.compilation.compiled &&
            response.entries.size() == 2 &&
            response.entries.front().policy ==
                thermox::service::
                    StructuralDecompositionPolicy::monolithic &&
            response.monolithic_baseline_converged &&
            response.all_policies_executed &&
            response.all_policies_converged &&
            response.all_policies_equivalent_to_monolithic,
        "service structural policy audit must retain a deterministic "
        "monolithic baseline and equivalent candidate evidence: " +
            response.error.message + "; " + response.message);
    const auto& torn = response.entries.back();
    require(
        torn.policy ==
                thermox::service::
                    StructuralDecompositionPolicy::tearing &&
            torn.comparable_to_monolithic &&
            torn.equivalent_to_monolithic &&
            torn.diagnostics.structural_tearing_attempts > 0 &&
            torn.diagnostics.factorization_quality_observations > 0,
        "audit entry must expose complete tearing and numerical evidence");
    const auto json = thermox::service::
        serialize_structural_policy_audit_response_json(response);
    require(
        json.find(
            "\"schema_version\": "
            "\"thermox.structural_policy_audit/v1\"") !=
                std::string::npos &&
            json.find("\"policy\": \"monolithic\"") !=
                std::string::npos &&
            json.find("\"policy\": \"tearing\"") !=
                std::string::npos &&
            json.find("\"minimum_reciprocal_pivot_ratio\":") !=
                std::string::npos &&
            json.find("\"linear_refinement_attempts\":") !=
                std::string::npos &&
            json.find("\"structural_blocks\": [") !=
                std::string::npos,
        "policy audit JSON must serialize provenance, structure, and "
        "complete numerical evidence");

    request.solver.continuation_enabled = true;
    const auto invalid =
        service.run_structural_policy_audit(request);
    require(
        !invalid.succeeded() &&
            invalid.error.code == "invalid_solver_settings",
        "policy audit must reject continuation rather than compare "
        "different solve algorithms implicitly");
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
            !response.continuation.stages.empty() &&
            !response.continuation.stages.back()
                 .limiting_residual.empty(),
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
            "thermox.newton-continuation/v12",
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
                std::string::npos &&
            json.find("\"limiting_residual\":") !=
                std::string::npos &&
            json.find("\"maximum_linear_backward_error\":") !=
                std::string::npos &&
            json.find("\"linear_refinement_successes\":") !=
                std::string::npos &&
            json.find("\"largest_tearing_inner_system_size\":") !=
                std::string::npos &&
            json.find("\"accepted_pivot_count_at_minimum_ratio\":") !=
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
            validation.compilation.mode == "transient" &&
            !validation.compilation.structural_blocks.empty() &&
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
            "thermox.dae-bdf/v12",
        "transient result must record solver contract");
    require(
        response.diagnostics.maximum_order_used == 2 &&
            response.diagnostics.last_error_norm <= 1.0 &&
            response.diagnostics.maximum_accepted_error_norm <= 1.0 &&
            response.diagnostics.maximum_error_ratio >= 0.0 &&
            !response.diagnostics.limiting_error_variable.empty() &&
            response.diagnostics
                    .maximum_absolute_normalized_residual <=
                1.0e-9 &&
            !response.diagnostics
                 .limiting_nonlinear_residual.empty() &&
            response.diagnostics.maximum_linear_backward_error <=
                request.solver.nonlinear_solver
                    .linear_residual_tolerance,
        "native transient service must expose scale-aware BDF order "
        "plus local-error, implicit-constraint, and linear-solve evidence");
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
    const double boundary_energy_rate = require_result_value(
        response.trajectory.back().graph.system_balances,
        "net_boundary_energy_flow").value_si;
    require(
        std::abs(boundary_energy_rate - 1.0e6) < 1.0e-6 &&
            std::abs(
                1.5e6 * store.internal_values.front()
                    .derivative_si_s -
                boundary_energy_rate) < 1.0e-3,
        "transient boundary audit must equal the differential "
        "storage rate");

    const auto json =
        thermox::service::serialize_transient_response_json(response);
    require(
        json.find("\"trajectory\": [") != std::string::npos &&
            json.find("\"internal_values\": [") !=
                std::string::npos &&
            json.find("\"derivative_si_s\":") !=
                std::string::npos &&
            json.find("\"last_error_norm\":") !=
                std::string::npos &&
            json.find("\"limiting_error_variable\":") !=
                std::string::npos &&
            json.find("\"limiting_nonlinear_residual\":") !=
                std::string::npos &&
            json.find("\"structural_tearing_fallbacks\":") !=
                std::string::npos &&
            json.find("\"linear_refinement_attempts\":") !=
                std::string::npos &&
            json.find("\"factorization_quality_observations\":") !=
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

    request.solver.end_time = 1.0;
    request.solver.nonlinear_solver.linear_residual_tolerance =
        0.0;
    const auto invalid_linear = service.run_transient(request);
    require(
        invalid_linear.status ==
                thermox::service::OperationStatus::invalid_request &&
            invalid_linear.error.code ==
                "invalid_solver_settings",
        "invalid linear accuracy settings must be rejected");
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
                thermox::service::result_summary_schema_v2 &&
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
            "\"thermox.result_summary/v2\"") !=
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

    const std::vector<thermox::service::EngineeringAcceptanceCriterion>
        criteria{
            {
                "minimum_efficiency_band",
                "minimum_efficiency",
                "dimensionless",
                0.30,
                0.50,
                true,
                true,
            },
            {
                "maximum_efficiency_ceiling",
                "maximum_efficiency",
                "dimensionless",
                std::nullopt,
                0.40,
                true,
                true,
            },
        };
    thermox::service::validate_engineering_acceptance_criteria(
        criteria, transient_projections);
    const auto acceptance =
        thermox::service::evaluate_engineering_acceptance(
            transient, criteria);
    require(
        !acceptance.passed && acceptance.failed_count == 1U &&
            acceptance.passed_count == 1U &&
            acceptance.criteria.front().lower_margin_si &&
            std::abs(*acceptance.criteria.front().lower_margin_si + 0.05) <
                1.0e-12 &&
            acceptance.criteria.front().upper_margin_si &&
            std::abs(*acceptance.criteria.front().upper_margin_si - 0.25) <
                1.0e-12 &&
            std::abs(
                acceptance.criteria.front().limiting_margin_si + 0.05) <
                1.0e-12 &&
            acceptance.criteria.front().limiting_bound == "lower" &&
            acceptance.criteria.back().upper_margin_si == 0.0 &&
            acceptance.criteria.back().limiting_margin_si == 0.0 &&
            acceptance.criteria.back().limiting_bound == "upper" &&
            acceptance.criteria.back().passed,
        "engineering acceptance must report signed margins for "
        "transient extrema and preserve inclusive boundary verdicts");
    auto inconsistent = acceptance;
    inconsistent.criteria.front().limiting_margin_si = 0.0;
    bool inconsistent_rejected = false;
    try {
        thermox::service::validate_engineering_acceptance_summary(
            inconsistent);
    } catch (const thermox::service::ResultProjectionError&) {
        inconsistent_rejected = true;
    }
    require(
        inconsistent_rejected,
        "durable engineering acceptance evidence must reject a margin "
        "that disagrees with its projected value and bounds");

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

void test_validation_evidence_contract() {
    using namespace thermox::service;
    ResultSummary projected;
    projected.mode = "steady";
    projected.values = {
        {"stack_temperature", "temperature", 357.686,
         ResultAggregation::final, false, 0.0},
        {"steam_power", "power", 271.736e6,
         ResultAggregation::final, false, 0.0},
        {"normalized_residual", "dimensionless", 4.3e-14,
         ResultAggregation::final, false, 0.0},
        {"main_steam_enthalpy", "specific_energy", 3522.653e3,
         ResultAggregation::final, false, 0.0},
        {"unpublished_loss", "power", 2.0e6,
         ResultAggregation::final, false, 0.0},
    };
    const std::vector<ValidationEvidenceCriterion> criteria{
        {
            "stack_boundary_agreement",
            "stack_temperature",
            ValidationEvidenceLayer::system,
            ValidationEvidenceBasis::boundary_constrained,
            "temperature",
            352.15,
            7.0,
            0.0,
            "NETL B31A Exhibit 4-8",
            "Steam-side duty is a published design-point boundary.",
        },
        {
            "calibrated_steam_power",
            "steam_power",
            ValidationEvidenceLayer::system,
            ValidationEvidenceBasis::calibrated_reproduction,
            "power",
            272.0e6,
            0.4e6,
            0.0,
            "NETL B31A power summary",
            "Stage efficiencies were fitted at this operating point.",
        },
        {
            "numerical_closure",
            "normalized_residual",
            ValidationEvidenceLayer::numerical,
            ValidationEvidenceBasis::internal_consistency,
            "dimensionless",
            0.0,
            1.0e-10,
            0.0,
            "Thermox scaled equation system",
            "Numerical closure is not external physical validation.",
        },
        {
            "if97_main_steam",
            "main_steam_enthalpy",
            ValidationEvidenceLayer::property,
            ValidationEvidenceBasis::independent_reference,
            "specific_energy",
            3520.51e3,
            2500.0,
            0.0,
            "NETL B31A stream 5 enthalpy",
            "Published enthalpy is not imposed on the PT state solve.",
        },
        {
            "assumed_unpublished_loss",
            "unpublished_loss",
            ValidationEvidenceLayer::component,
            ValidationEvidenceBasis::assumption_dependent,
            "power",
            0.0,
            1.0e6,
            0.0,
            "No published equipment loss allocation",
            "Deliberately fails to keep the assumption visible.",
        },
    };
    const auto evidence = evaluate_validation_evidence(
        validation_observations_from_result_summary(projected),
        criteria,
        {
            "No gas-turbine compressor or turbine maps are published.",
            "Calibrated stage efficiencies do not validate off-design prediction.",
        });
    const auto class_for = [&](ValidationEvidenceBasis basis)
        -> const ValidationEvidenceClassSummary& {
        const auto found = std::find_if(
            evidence.classes.begin(), evidence.classes.end(),
            [&](const auto& item) { return item.basis == basis; });
        require(found != evidence.classes.end(),
                "missing validation evidence class summary");
        return *found;
    };
    require(
        !evidence.passed && evidence.passed_count == 4U &&
            evidence.failed_count == 1U &&
            class_for(ValidationEvidenceBasis::independent_reference)
                    .passed_count == 1U &&
            class_for(ValidationEvidenceBasis::calibrated_reproduction)
                    .passed_count == 1U &&
            class_for(ValidationEvidenceBasis::assumption_dependent)
                    .failed_count == 1U &&
            evidence.criteria.front().signed_error_si > 5.0 &&
            evidence.criteria.front().passed &&
            evidence.limitations.size() == 2U,
        "validation evidence must preserve numeric verdicts, evidence "
        "independence, and explicit limitations");
    const auto json = serialize_validation_evidence_summary_json(
        evidence);
    require(
        json.find("\"schema_version\": "
                  "\"thermox.validation_evidence/v1\"") !=
                std::string::npos &&
            json.find("\"basis\": \"independent_reference\"") !=
                std::string::npos &&
            json.find("\"basis\": \"calibrated_reproduction\"") !=
                std::string::npos &&
            json.find("\"limitations\": [") !=
                std::string::npos,
        "validation evidence JSON must retain classification and limits");

    bool missing_rejected = false;
    try {
        (void)evaluate_validation_evidence({}, criteria);
    } catch (const ValidationEvidenceError& error) {
        missing_rejected =
            std::string(error.what()).find("missing observation") !=
            std::string::npos;
    }
    require(
        missing_rejected,
        "validation evidence must reject missing observed values");

    auto inconsistent = evidence;
    ++inconsistent.classes.front().passed_count;
    bool inconsistent_rejected = false;
    try {
        validate_validation_evidence_summary(inconsistent);
    } catch (const ValidationEvidenceError&) {
        inconsistent_rejected = true;
    }
    require(
        inconsistent_rejected,
        "durable validation evidence must reject reclassified counts");
}

void test_iso2314_gas_turbine_performance_test_reduction() {
    using namespace thermox::service;
    GasTurbinePerformanceTestRequest request;
    request.id = "public-synthetic-iso2314-campaign";
    request.equipment_id = "synthetic-gt";
    request.evidence = {
        true, true, true, true, true, true, true, true, false};

    GasTurbinePerformanceRun run;
    run.id = "run-1";
    run.duration_seconds = 1800.0;
    run.gross_generator_power_w = {100.0e6, 0.1e6};
    run.generator_losses_w = {1.0e6, 0.01e6};
    run.fuel_mass_flow_kg_s = {5.0, 0.01};
    run.fuel_lhv_j_kg = {50.0e6, 0.1e6};
    run.fuel_sensible_enthalpy_j_kg = {0.0, 0.0};
    run.output_corrections = {
        {"ambient_temperature", 1.1,
         CorrectionEvidenceBasis::manufacturer_curve,
         "public synthetic correction table", 0.001},
    };
    run.heat_rate_corrections = {
        {"ambient_temperature", 0.98,
         CorrectionEvidenceBasis::manufacturer_curve,
         "public synthetic correction table", 0.001},
    };
    run.stability = {
        {"generator_terminal_power", 0.004, 0.01, "relative",
         StabilityStatistic::maximum_deviation_from_mean},
        {"ambient_temperature", 0.5, 2.0, "temperature_difference",
         StabilityStatistic::maximum_deviation_from_mean},
    };
    request.runs = {run};

    const auto result =
        evaluate_gas_turbine_performance_test(request);
    require(
        result.iso_conformity_demonstrated &&
            result.passed_count == 9U &&
            result.not_demonstrated_count == 0U,
        "complete synthetic evidence must demonstrate the declared "
        "ISO 2314 checks");
    require(
        std::abs(
            result.runs[0].net_generator_power_w.value_si - 99.0e6) <
                1.0e-9 &&
            std::abs(
                result.runs[0].fuel_thermal_input_w.value_si -
                250.0e6) < 1.0e-9 &&
            std::abs(
                result.runs[0].heat_rate_j_per_kwh.value_si -
                3.6e6 / (99.0 / 250.0)) < 1.0e-9 &&
            std::abs(
                result.runs[0].corrected_net_generator_power_w.value_si -
                108.9e6) < 1.0e-6,
        "ISO performance reduction must compute net power, fuel input, "
        "heat rate, and corrected power from the declared boundary");
    require(
        result.runs[0].corrected_heat_rate_j_per_kwh
                .standard_uncertainty_si.has_value() &&
            result.runs[0].corrected_heat_rate_j_per_kwh
                .expanded_uncertainty_si_k2.has_value(),
        "complete Type B input and correction uncertainties must "
        "propagate to a k=2 result");
    const auto json =
        serialize_gas_turbine_performance_test_result_json(result);
    require(
        json.find("thermox.gas_turbine_performance_test/v1") !=
                std::string::npos &&
            json.find("\"iso_conformity_demonstrated\":true") !=
                std::string::npos &&
            json.find("\"basis\":\"manufacturer_curve\"") !=
                std::string::npos &&
            json.find(
                "\"statistic\":\"maximum_deviation_from_mean\"") !=
                std::string::npos,
        "performance-test result serialization must preserve schema and "
        "correction/stability evidence");

    request.id = "reported-summary-only";
    request.evidence.raw_time_series_available = false;
    request.evidence.instrument_calibrations_available = false;
    request.evidence.contemporaneous_fuel_sample_available = false;
    request.evidence.correction_curves_available = false;
    request.evidence.correction_curve_range_confirmed = false;
    request.evidence.uncertainty_analysis_available = false;
    request.evidence.deviations_documented_and_approved = true;
    request.runs[0].stability[0].statistic =
        StabilityStatistic::standard_deviation;
    const auto reported =
        evaluate_gas_turbine_performance_test(request);
    require(
        !reported.iso_conformity_demonstrated &&
            reported.accepted_deviation_count == 1U &&
            reported.not_demonstrated_count >= 4U &&
            reported.failed_count == 0U,
        "reported factors and stability summaries must reproduce results "
        "without being promoted to independently demonstrated ISO "
        "conformity");
}

}  // namespace

int main() {
    try {
        test_request_contract_validation();
        test_request_scoped_performance_map_artifacts();
        test_resolved_performance_map_artifacts();
        test_request_scoped_correlation_artifacts();
        test_correlation_applicability_reaches_component_diagnostics();
        test_request_scoped_correlation_family_selects_candidate();
        test_catalog_discovery();
        test_correlation_template_instantiation();
        test_regime_map_template_instantiation();
        test_validation_and_canonicalization();
        test_homogeneous_two_phase_local_loss();
#ifdef THERMOX_TEST_HAS_CANTERA
        test_cantera_brayton_integration_benchmark();
        test_netl_b31a_hrsg_boundary_benchmark();
        test_netl_b31a_segmented_triple_pressure_hrsg();
        test_dynamic_cantera_if97_hrsg_cell();
        test_distributed_cantera_if97_counterflow_exchanger();
        test_dynamic_equilibrium_two_phase_material_fluid_cell();
        test_composed_dynamic_single_pressure_hrsg();
        test_dynamic_forced_circulation_evaporator();
        test_dynamic_natural_circulation_evaporator();
#endif
        test_netl_b31a_steam_stream_property_benchmark();
        test_netl_b31a_decomposed_steam_turbine_train();
        test_netl_b31a_connected_hrsg_and_steam_cycle();
        test_netl_b31a_published_balance_consistency();
        test_compile_aware_validation_diagnostics();
        test_structurally_singular_validation_diagnostic();
        test_calibration_observation_contract_validation();
        test_bounded_calibration_service();
        test_hard_constraint_data_reconciliation_service();
        test_hard_reconciliation_reports_local_bound_limitation();
        test_overdetermined_weighted_reconciliation_service();
        test_weighted_reconciliation_rejects_rank_deficiency();
        test_weighted_reconciliation_reports_parameter_correlation();
        test_weighted_reconciliation_applies_measurement_correlation();
        test_model_rejects_invalid_measurement_correlation();
        test_weighted_reconciliation_reports_active_bound_uncertainty();
        test_profile_likelihood_rejects_invalid_intent();
        test_engineering_study_freezes_before_prediction();
        test_map_correction_is_calibrated_then_frozen();
        test_component_version_is_enforced();
        test_property_and_connector_versions_are_enforced();
        test_connection_contract_diagnostic();
        test_injectable_native_runtime();
        test_expression_component_flows_through_service_runtime();
        test_expression_component_is_request_scoped();
        test_transient_expression_component_flows_through_service();
        test_steady_service();
        test_structural_policy_audit_service();
        test_steady_continuation_service();
        test_explicit_system_boundary_balance();
        test_transient_service();
        test_structured_compilation_failure();
        test_invalid_solver_settings();
        test_system_agnostic_result_projection();
        test_validation_evidence_contract();
        test_iso2314_gas_turbine_performance_test_reduction();
        std::cout << "thermox service tests passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "thermox service tests failed: " << ex.what()
                  << "\n";
        return 1;
    }
}
