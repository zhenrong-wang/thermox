#include "thermox/service/in_memory_projects.hpp"
#include "thermox/service/projects.hpp"
#include "thermox/service/simulation_runtime.hpp"
#include "thermox/service/simulation_service.hpp"
#include "thermox/platform/model_document.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

const thermox::service::IdentityContext team_a{
    "user-a", "team-a", "project-test"};
const thermox::service::IdentityContext team_b{
    "user-b", "team-b", "project-test"};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string read_source_file(const std::string& relative_path) {
    std::ifstream input(
        std::string(THERMOX_SOURCE_DIR) + "/" + relative_path);
    if (!input) {
        throw std::runtime_error(
            "could not open source file: " + relative_path);
    }
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

void test_projects_are_team_scoped_logical_partitions() {
    thermox::service::ProjectService service{
        thermox::service::make_in_memory_project_repository()};
    const auto project = service.create_project({
        team_a,
        "  Gas turbine study  ",
        "  OEM comparison  ",
    });
    require(
        project.team_id == "team-a" &&
            project.created_by_user_id == "user-a" &&
            project.name == "Gas turbine study" &&
            project.description == "OEM comparison",
        "project creation must retain Team ownership and actor "
        "audit metadata");
    require(
        service.get_project(team_a, project.project_id)
            .has_value() &&
            service.list_projects(team_a).size() == 1U,
        "the owning Team must be able to read its project");
    require(
        !service.get_project(team_b, project.project_id)
             .has_value() &&
            service.list_projects(team_b).empty(),
        "cross-Team reads must not reveal project existence");
}

void test_model_revisions_are_immutable_and_scoped() {
    thermox::service::ProjectService service{
        thermox::service::make_in_memory_project_repository()};
    const auto project = service.create_project({
        team_a, "Cycle model", {},
    });
    const auto model =
        read_source_file(
            "core/examples/air_compressor.topology.json");
    const auto first = service.create_model_revision({
        team_a, project.project_id, {}, model,
    });
    const auto second = service.create_model_revision({
        team_a,
        project.project_id,
        first.model_revision_id,
        model,
    });
    require(
        first.revision_number == 1U &&
            second.revision_number == 2U &&
            second.parent_model_revision_id ==
                first.model_revision_id &&
            first.checksum == second.checksum &&
            first.checksum.starts_with("sha256:") &&
            first.canonical_model_json.find(
                "\"schema_version\": "
                "\"thermox.topology/v1\"") !=
                std::string::npos,
        "model revisions must be ordered, parent-linked, "
        "canonical, and content checksummed");
    const auto revisions = service.list_model_revisions(
        team_a, project.project_id);
    require(
        revisions.size() == 2U &&
            revisions.front().model_revision_id ==
                first.model_revision_id,
        "model revision history must be deterministic");
    require(
        !service.get_model_revision(
                    team_b,
                    project.project_id,
                    first.model_revision_id)
             .has_value() &&
            service
                .list_model_revisions(
                    team_b, project.project_id)
                .empty(),
        "cross-Team model revision reads must not reveal "
        "resource existence");

    bool hidden_parent = false;
    try {
        const auto other_project = service.create_project({
            team_a, "Other model", {},
        });
        (void)service.create_model_revision({
            team_a,
            other_project.project_id,
            first.model_revision_id,
            model,
        });
    } catch (const thermox::service::ProjectStateError&) {
        hidden_parent = true;
    }
    require(
        hidden_parent,
        "a parent revision must belong to the same project");
}

void test_assembly_hierarchy_survives_model_revision_persistence() {
    thermox::service::ProjectService service{
        thermox::service::make_in_memory_project_repository()};
    const auto project = service.create_project({
        team_a, "Assembly persistence", {},
    });
    const auto revision = service.create_model_revision({
        team_a,
        project.project_id,
        {},
        R"json({
          "schema_version": "thermox.topology/v1",
          "model": {
            "id": "stage_assembly",
            "media": [{
              "id": "air", "backend": "ideal_gas_mixture",
              "substance": "Air"
            }],
            "components": [],
            "assemblies": [{
              "id": "compressor",
              "ports": [
                {"name": "inlet", "endpoint": "stage.inlet"},
                {"name": "outlet", "endpoint": "stage.outlet"}
              ],
              "parameters": [{
                "name": "pressure_ratio",
                "target": "stage.pressure_ratio"
              }],
              "components": [{
                "id": "stage",
                "kind": "compressor.fluid.isentropic_efficiency",
                "parameters": {"pressure_ratio": 2.0, "eta_is": 0.9},
                "media": {"inlet": "air", "outlet": "air"}
              }],
              "connections": []
            }],
            "connections": []
          }
        })json",
    });
    require(
        revision.canonical_model_json.find("\"assemblies\"") !=
            std::string::npos,
        "canonical topology must retain assembly hierarchy");
    const auto reparsed =
        thermox::platform::parse_topology_document_text(
            revision.canonical_model_json);
    require(
        reparsed.assemblies.size() == 1U &&
            reparsed.assemblies.front().components.size() == 1U &&
            reparsed.assemblies.front().parameters.size() == 1U &&
            thermox::platform::flatten_model_document(reparsed)
                    .components.front().id == "compressor/stage",
        "persisted assembly must round-trip and expand deterministically");
}

void test_invalid_input_is_rejected_before_persistence() {
    thermox::service::ProjectService service{
        thermox::service::make_in_memory_project_repository()};
    bool invalid_name = false;
    try {
        (void)service.create_project({team_a, "  ", {}});
    } catch (const thermox::service::ProjectRequestError&) {
        invalid_name = true;
    }
    require(
        invalid_name,
        "empty project names must be rejected");

    const auto project =
        service.create_project({team_a, "Valid", {}});
    bool invalid_model = false;
    try {
        (void)service.create_model_revision({
            team_a, project.project_id, {}, "{}",
        });
    } catch (const thermox::service::ProjectRequestError&) {
        invalid_model = true;
    }
    require(
        invalid_model,
        "invalid model documents must not create revisions");
    require(
        service
            .list_model_revisions(
                team_a, project.project_id)
            .empty(),
        "failed revision creation must not mutate history");
}

void test_public_json_omits_model_from_history() {
    thermox::service::ProjectService service{
        thermox::service::make_in_memory_project_repository()};
    const auto project =
        service.create_project({team_a, "Serialization", {}});
    const auto revision = service.create_model_revision({
        team_a,
        project.project_id,
        {},
        read_source_file(
            "core/examples/air_compressor.topology.json"),
    });
    const auto detail =
        thermox::service::serialize_model_revision_json(revision);
    const auto history =
        thermox::service::serialize_model_revisions_json(
            {revision});
    require(
        detail.find("\"model\": {") !=
                std::string::npos &&
            history.find("\"model\": {") ==
                std::string::npos &&
            history.find(revision.checksum) !=
                std::string::npos,
        "revision detail must carry content while history "
        "returns metadata only");
}

void test_case_revisions_bind_exact_model_revisions() {
    thermox::service::ProjectService service{
        thermox::service::make_in_memory_project_repository()};
    const auto project =
        service.create_project({team_a, "Case history", {}});
    const auto model = service.create_model_revision({
        team_a,
        project.project_id,
        {},
        read_source_file(
            "core/examples/air_compressor.topology.json"),
    });
    const auto case_json = read_source_file(
        "core/examples/air_compressor.design.case.json");
    const auto first = service.create_case_revision({
        team_a,
        project.project_id,
        model.model_revision_id,
        {},
        case_json,
    });
    const auto second = service.create_case_revision({
        team_a,
        project.project_id,
        model.model_revision_id,
        first.case_revision_id,
        case_json,
    });
    require(
        first.case_id == "design" &&
            first.mode == "steady_state_design" &&
            first.revision_number == 1U &&
            second.revision_number == 2U &&
            second.parent_case_revision_id ==
                first.case_revision_id &&
            first.model_revision_id ==
                model.model_revision_id &&
            first.checksum == second.checksum &&
            first.checksum.starts_with("sha256:"),
        "case revisions must be canonical, immutable, ordered, "
        "and bound to an exact model revision");
    require(
        service
                .list_case_revisions(
                    team_a,
                    project.project_id,
                    model.model_revision_id)
                .size() == 2U &&
            !service
                 .get_case_revision(
                     team_b,
                     project.project_id,
                     model.model_revision_id,
                     first.case_revision_id)
                 .has_value(),
        "case history must be Team scoped");

    const auto detail =
        thermox::service::serialize_case_revision_json(first);
    const auto history =
        thermox::service::serialize_case_revisions_json({first});
    require(
        detail.find("\"case_document\": {") !=
                std::string::npos &&
            history.find("\"case_document\": {") ==
                std::string::npos,
        "case detail must carry its document while history "
        "returns metadata only");

    const auto resolved = service.resolve_model_case(
        team_a,
        project.project_id,
        model.model_revision_id,
        first.case_revision_id);
    require(
        resolved &&
            resolved->model_checksum == model.checksum &&
            resolved->case_checksum == first.checksum &&
            resolved->executable_model_json.find(
                "\"schema_version\": \"thermox.model/v2\"") !=
                std::string::npos,
        "an exact topology/case pair must compose into a "
        "provenance-pinned executable model");
    thermox::service::SteadySimulationRequest run;
    run.model_json = resolved->executable_model_json;
    run.case_id = resolved->case_id;
    require(
        thermox::service::SimulationService{}
            .run_steady(run)
            .succeeded(),
        "the composed persisted topology/case pair must execute "
        "through the ordinary simulation service");
}

std::string performance_map_payload() {
    return R"json({
  "primary_variable": {
    "name": "corrected_mass_flow",
    "dimension": "mass_flow"
  },
  "family_variable": {
    "name": "corrected_speed",
    "dimension": "angular_speed"
  },
  "output_variables": [
    {"name": "pressure_ratio", "dimension": "dimensionless"},
    {"name": "isentropic_efficiency", "dimension": "dimensionless"}
  ],
  "curves": [
    {
      "family_coordinate": 250.0,
      "samples": [
        {"coordinate": 70.0, "outputs": [10.0, 0.85]},
        {"coordinate": 120.0, "outputs": [10.0, 0.85]}
      ]
    },
    {
      "family_coordinate": 400.0,
      "samples": [
        {"coordinate": 70.0, "outputs": [10.0, 0.85]},
        {"coordinate": 120.0, "outputs": [10.0, 0.85]}
      ]
    }
  ]
})json";
}

std::string expression_component_payload(
    const std::string& version = "1.0.0") {
    auto payload = std::string{R"json({
  "kind": "custom.signal.persisted_gain",
  "version": "1.0.0",
  "template_kind": "custom.signal.gain",
  "display_name": "Signal gain",
  "category": "Project components",
  "model_name": "Algebraic gain",
  "ports": [
    {"name": "input", "domain": "signal", "direction": "in"},
    {"name": "output", "domain": "signal", "direction": "out"}
  ],
  "parameters": [
    {
      "name": "gain",
      "dimension": "dimensionless",
      "required": true,
      "lower_bound": 0.0,
      "upper_bound": 100.0
    }
  ],
  "equations": [
    {
      "name": "gain_law",
      "expression":
        "output.value - parameter.gain * input.value",
      "residual_scale": 1.0
    }
  ]
})json"};
    payload.replace(
        payload.find("\"version\": \"1.0.0\""),
        std::string{"\"version\": \"1.0.0\""}.size(),
        "\"version\": \"" + version + "\"");
    return payload;
}

std::string correlation_payload() {
    return R"json({
  "inputs": [
    {"name": "mass_flow", "dimension": "mass_flow"},
    {"name": "density", "dimension": "density"},
    {"name": "area", "dimension": "area"}
  ],
  "output": {
    "name": "pressure_loss",
    "dimension": "pressure"
  },
  "candidates": [{
    "id": "default",
    "regime": "general",
    "priority": 0,
    "coefficients": {"loss_coefficient": 1.5},
    "expression": "loss_coefficient * mass_flow * abs(mass_flow) / (2 * density * area * area)",
    "applicability": [{
      "input": "mass_flow",
      "minimum": 0.0,
      "maximum": 25.0,
      "minimum_inclusive": true,
      "maximum_inclusive": false
    }]
  }]
})json";
}

std::string correlation_family_payload() {
    return R"json({
  "inputs": [{"name": "mass_flow", "dimension": "mass_flow"}],
  "output": {"name": "pressure_loss", "dimension": "pressure"},
  "candidates": [
    {
      "id": "low_flow",
      "regime": "low-flow",
      "priority": 10,
      "coefficients": {"factor": 1.0},
      "expression": "factor * mass_flow * abs(mass_flow)",
      "applicability": [{"input": "mass_flow", "minimum": 0.0, "maximum": 2.0}]
    },
    {
      "id": "high_flow",
      "regime": "high-flow",
      "priority": 20,
      "coefficients": {"factor": 2.0},
      "expression": "factor * mass_flow * abs(mass_flow)",
      "applicability": [{"input": "mass_flow", "minimum": 2.0, "maximum": 20.0}]
    }
  ]
})json";
}

void test_correlation_artifact_is_executable_input() {
    thermox::service::ProjectService projects{
        thermox::service::make_in_memory_project_repository()};
    const auto project = projects.create_project({
        team_a, "Correlation artifact", {},
    });
    const auto revision = projects.create_artifact_revision({
        team_a,
        project.project_id,
        "return-bend-pressure-loss",
        {},
        "thermox.correlation",
        "thermox.correlation/v1",
        correlation_payload(),
    });
    const auto resolved = projects.resolve_artifact_revisions(
        team_a, project.project_id,
        {revision.artifact_revision_id});
    require(
        resolved && resolved->snapshot.correlations.size() == 1U &&
            resolved->snapshot.correlations.front().id ==
                "return-bend-pressure-loss" &&
            resolved->snapshot.correlations.front().candidates.front()
                    .expression.find(
                "mass_flow") != std::string::npos &&
            resolved->snapshot.correlations.front()
                    .candidates.front().coefficients.at(
                        "loss_coefficient") == 1.5 &&
            resolved->snapshot.correlations.front()
                    .candidates.front().applicability.size() == 1U &&
            resolved->snapshot.correlations.front()
                    .candidates.front().applicability.front().maximum ==
                25.0 &&
            !resolved->snapshot.correlations.front()
                    .candidates.front().applicability.front()
                    .maximum_inclusive,
        "correlation revisions must resolve into an immutable "
        "executable artifact snapshot");

    bool unsafe_rejected = false;
    try {
        (void)projects.create_artifact_revision({
            team_a,
            project.project_id,
            "unsafe-correlation",
            {},
            "thermox.correlation",
            "thermox.correlation/v1",
            R"json({
              "inputs": [{"name": "x", "dimension": "dimensionless"}],
              "output": {"name": "y", "dimension": "dimensionless"},
              "candidates": [{
                "id": "default", "regime": "general", "priority": 0,
                "coefficients": {}, "expression": "system(x)"
              }]
            })json",
        });
    } catch (const thermox::service::ProjectRequestError&) {
        unsafe_rejected = true;
    }
    require(unsafe_rejected,
            "unsafe correlation expressions must be rejected before "
            "persistence");

    bool removed_shape_rejected = false;
    try {
        (void)projects.create_artifact_revision({
            team_a,
            project.project_id,
            "removed-correlation-shape",
            {},
            "thermox.correlation",
            "thermox.correlation/v1",
            R"json({
              "inputs": [{"name": "x", "dimension": "dimensionless"}],
              "output": {"name": "y", "dimension": "dimensionless"},
              "coefficients": {},
              "expression": "x"
            })json",
        });
    } catch (const thermox::service::ProjectRequestError&) {
        removed_shape_rejected = true;
    }
    require(
        removed_shape_rejected,
        "the removed single-law payload shape must not be adapted");

    bool invalid_envelope_rejected = false;
    try {
        (void)projects.create_artifact_revision({
            team_a,
            project.project_id,
            "invalid-envelope-correlation",
            {},
            "thermox.correlation",
            "thermox.correlation/v1",
            R"json({
              "inputs": [{"name": "x", "dimension": "dimensionless"}],
              "output": {"name": "y", "dimension": "dimensionless"},
              "candidates": [{
                "id": "default", "regime": "general", "priority": 0,
                "coefficients": {}, "expression": "x",
                "applicability": [{
                  "input": "unknown",
                  "minimum": 0.0,
                  "maximum": 1.0
                }]
              }]
            })json",
        });
    } catch (const thermox::service::ProjectRequestError&) {
        invalid_envelope_rejected = true;
    }
    require(
        invalid_envelope_rejected,
        "invalid applicability envelopes must be rejected before "
        "persistence");

    const auto family_revision = projects.create_artifact_revision({
        team_a,
        project.project_id,
        "return-bend-family",
        {},
        "thermox.correlation",
        "thermox.correlation/v1",
        correlation_family_payload(),
    });
    const auto resolved_family = projects.resolve_artifact_revisions(
        team_a, project.project_id,
        {family_revision.artifact_revision_id});
    require(
        resolved_family &&
            resolved_family->snapshot.correlations.size() == 1U &&
            resolved_family->snapshot.correlations.front()
                    .schema_version == "thermox.correlation/v1" &&
            resolved_family->snapshot.correlations.front()
                    .candidates.size() == 2U &&
            resolved_family->snapshot.correlations.front()
                    .candidates.back().regime == "high-flow" &&
            resolved_family->snapshot.correlations.front()
                    .candidates.back().priority == 20,
        "correlation families must preserve ordered candidates, "
        "regimes, priorities, and qualified ranges");
}

void test_expression_component_artifact_is_executable() {
    auto projects =
        std::make_shared<thermox::service::ProjectService>(
            thermox::service::
                make_in_memory_project_repository());
    const auto project = projects->create_project({
        team_a, "Custom component", {},
    });
    const auto revision = projects->create_artifact_revision({
        team_a,
        project.project_id,
        "persisted-gain",
        {},
        "thermox.expression_component",
        "thermox.expression_component/v2",
        expression_component_payload(),
    });
    const auto resolved = projects->resolve_artifact_revisions(
        team_a,
        project.project_id,
        {revision.artifact_revision_id});
    require(
        resolved &&
            resolved->components.expression_components.size() ==
                1U &&
            resolved->components.expression_components.front()
                    .kind ==
                "custom.signal.persisted_gain" &&
            resolved->components.expression_components.front()
                    .template_kind == "custom.signal.gain" &&
            resolved->components.expression_components.front()
                    .display_name == "Signal gain" &&
            resolved->components.expression_components.front()
                    .model_name == "Algebraic gain" &&
            resolved->snapshot.references.size() == 1U &&
            resolved->snapshot.references.front().revision ==
                revision.artifact_revision_id,
        "expression-component revisions must resolve into an "
        "immutable executable bundle with artifact provenance");
    thermox::service::ProjectComponentCatalogService
        component_catalog{
            projects,
            thermox::service::
                make_default_simulation_runtime()};
    const auto discovered =
        component_catalog.get(team_a, project.project_id);
    require(
        discovered.schema_version ==
                "thermox.project_component_catalog/v1" &&
            discovered.components.size() == 1U &&
            discovered.components.front().source
                    .artifact_revision_id ==
                revision.artifact_revision_id &&
            discovered.components.front().component.kind ==
                "custom.signal.persisted_gain" &&
            discovered.components.front().component.template_kind ==
                "custom.signal.gain" &&
            discovered.components.front().component.display_name ==
                "Signal gain" &&
            discovered.components.front().component.model_name ==
                "Algebraic gain" &&
            discovered.components.front().definition.kind ==
                "custom.signal.persisted_gain" &&
            discovered.components.front().definition.equations
                    .size() == 1U &&
            !discovered.components.front()
                 .catalog_fingerprint.empty(),
        "project component discovery must pair its editable "
        "definition and runtime descriptor with the exact "
        "immutable source revision");
    const auto latest_revision =
        projects->create_artifact_revision({
            team_a,
            project.project_id,
            "persisted-gain",
            revision.artifact_revision_id,
            "thermox.expression_component",
            "thermox.expression_component/v2",
            expression_component_payload("1.0.1"),
        });
    const auto refreshed =
        component_catalog.get(team_a, project.project_id);
    require(
        refreshed.components.size() == 2U &&
            refreshed.components.front().source
                    .artifact_revision_id ==
                latest_revision.artifact_revision_id &&
            refreshed.components.front().definition.version ==
                "1.0.1",
        "project component discovery must expose the latest "
        "immutable revision of each logical definition");
    bool duplicate_version_rejected = false;
    try {
        (void)projects->create_artifact_revision({
            team_a,
            project.project_id,
            "persisted-gain",
            latest_revision.artifact_revision_id,
            "thermox.expression_component",
            "thermox.expression_component/v2",
            expression_component_payload("1.0.1"),
        });
    } catch (const thermox::service::ProjectRequestError&) {
        duplicate_version_rejected = true;
    }
    bool duplicate_kind_rejected = false;
    try {
        (void)projects->create_artifact_revision({
            team_a,
            project.project_id,
            "second-gain-artifact",
            {},
            "thermox.expression_component",
            "thermox.expression_component/v2",
            expression_component_payload("2.0.0"),
        });
    } catch (const thermox::service::ProjectRequestError&) {
        duplicate_kind_rejected = true;
    }
    require(
        duplicate_version_rejected &&
            duplicate_kind_rejected,
        "component authoring must reserve one logical artifact "
        "per kind and one immutable revision per kind/version");

    const auto model = projects->create_model_revision({
        team_a,
        project.project_id,
        {},
        R"json({
  "schema_version": "thermox.topology/v1",
  "model": {
    "id": "persisted_component",
    "media": [],
    "components": [{
      "id": "gain",
      "kind": "custom.signal.persisted_gain",
      "parameters": {"gain": 2.0}
    }],
    "connections": []
  }
})json",
    });
    const auto simulation_case =
        projects->create_case_revision({
            team_a,
            project.project_id,
            model.model_revision_id,
            {},
            R"json({
  "schema_version": "thermox.case/v1",
  "case": {
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {"gain.input.value": 5.0}
  }
})json",
        });
    thermox::service::CreateStudyRevisionRequest study_request;
    study_request.identity = team_a;
    study_request.project_id = project.project_id;
    study_request.study_id = "custom-study";
    study_request.model_revision_id = model.model_revision_id;
    study_request.case_revision_id = simulation_case.case_revision_id;
    study_request.intent = simulation_case.mode;
    study_request.artifact_revision_ids = {
        revision.artifact_revision_id,
    };
    const auto study = projects->create_study_revision(study_request);
    thermox::service::CreateRunConfigurationRevisionRequest
        configuration;
    configuration.identity = team_a;
    configuration.project_id = project.project_id;
    configuration.run_configuration_id = "custom-run";
    configuration.study_revision_id = study.study_revision_id;
    const auto run =
        projects->create_run_configuration_revision(
            configuration);
    const auto resolved_run =
        projects->resolve_run_configuration(
            team_a,
            project.project_id,
            run.run_configuration_revision_id);
    require(
        resolved_run &&
            resolved_run->artifacts.components
                    .expression_components.size() == 1U,
        "run configurations must pin and resolve component "
        "definition revisions");

    thermox::service::ProjectModelValidationService validator{
        projects,
        thermox::service::make_default_simulation_runtime()};
    const auto validation = validator.validate({
        team_a,
        project.project_id,
        model.model_revision_id,
        simulation_case.case_revision_id,
        {revision.artifact_revision_id},
    });
    require(
        validation.validation.succeeded(),
        "revision-backed validation must compile persisted "
        "models against selected component definitions");

    thermox::service::SteadySimulationRequest request;
    request.model_json =
        resolved_run->model_case.executable_model_json;
    request.case_id = resolved_run->model_case.case_id;
    request.artifacts = resolved_run->artifacts.snapshot;
    request.components = resolved_run->artifacts.components;
    const auto solved =
        thermox::service::SimulationService{}.run_steady(request);
    require(
        solved.succeeded() &&
            solved.metadata.artifacts.size() == 1U &&
            solved.metadata.artifacts.front().revision ==
                revision.artifact_revision_id &&
            solved.metadata.artifacts.front().artifact_type ==
                "thermox.expression_component",
        "a persisted expression component must execute through "
        "the standard solver and retain revision provenance");

    bool rejected = false;
    auto unsafe = expression_component_payload();
    const auto location = unsafe.find(
        "output.value - parameter.gain * input.value");
    unsafe.replace(
        location,
        std::string(
            "output.value - parameter.gain * input.value").size(),
        "system(\"unsafe\")");
    try {
        (void)projects->create_artifact_revision({
            team_a,
            project.project_id,
            "unsafe-component",
            {},
            "thermox.expression_component",
            "thermox.expression_component/v2",
            unsafe,
        });
    } catch (const thermox::service::ProjectRequestError&) {
        rejected = true;
    }
    require(
        rejected &&
            projects
                    ->list_artifact_revisions(
                        team_a, project.project_id)
                    .size() == 2U,
        "unsafe component definitions must be rejected before "
        "an immutable revision is published");

    bool hidden = false;
    try {
        (void)component_catalog.get(team_b, project.project_id);
    } catch (const thermox::service::ProjectStateError&) {
        hidden = true;
    }
    require(
        hidden,
        "project component discovery must hide cross-Team "
        "project existence");
}

void test_artifact_revisions_are_snapshotted_and_scoped() {
    thermox::service::ProjectService service{
        thermox::service::make_in_memory_project_repository()};
    const auto project =
        service.create_project({team_a, "Artifact history", {}});
    const auto first = service.create_artifact_revision({
        team_a,
        project.project_id,
        "oem-compressor-map",
        {},
        "thermox.performance_map",
        "thermox.performance_map/v1",
        performance_map_payload(),
    });
    const auto second = service.create_artifact_revision({
        team_a,
        project.project_id,
        "oem-compressor-map",
        first.artifact_revision_id,
        "thermox.performance_map",
        "thermox.performance_map/v1",
        performance_map_payload(),
    });
    require(
        first.revision_number == 1U &&
            second.revision_number == 2U &&
            second.parent_artifact_revision_id ==
                first.artifact_revision_id &&
            first.content.checksum ==
                second.content.checksum &&
            first.content.checksum.starts_with("sha256:") &&
            service
                    .list_artifact_revisions(
                        team_a, project.project_id)
                    .size() == 2U &&
            !service
                 .get_artifact_revision(
                     team_b,
                     project.project_id,
                     first.artifact_revision_id)
                 .has_value(),
        "artifact revisions must be immutable, ordered, "
        "content-addressed, and Team scoped");
    const auto resolved = service.resolve_artifact_revisions(
        team_a,
        project.project_id,
        {first.artifact_revision_id});
    require(
        resolved &&
            resolved->snapshot.performance_maps.size() == 1U &&
            resolved->snapshot.performance_maps.front().id ==
                "oem-compressor-map" &&
            resolved->snapshot.performance_maps.front().revision ==
                first.artifact_revision_id &&
            resolved->snapshot.performance_maps.front()
                    .checksum_sha256 ==
                first.content.checksum.substr(7),
        "artifact resolution must produce an immutable "
        "execution snapshot with persisted provenance");
    const auto content = service.get_artifact_revision_content(
        team_a,
        project.project_id,
        first.artifact_revision_id);
    require(
        content &&
            content->revision.artifact_revision_id ==
                first.artifact_revision_id &&
            content->canonical_artifact_json.find(
                "corrected_mass_flow") != std::string::npos &&
            !service
                 .get_artifact_revision_content(
                     team_b,
                     project.project_id,
                     first.artifact_revision_id)
                 .has_value(),
        "artifact content reads must return the exact canonical "
        "payload and remain Team scoped");
    const auto serialized =
        thermox::service::serialize_artifact_revision_json(first);
    const auto serialized_content =
        thermox::service::serialize_artifact_revision_content_json(
            *content);
    require(
        serialized.find(first.content.checksum) !=
                std::string::npos &&
            serialized.find(first.content.object_key) ==
                std::string::npos &&
            serialized_content.find("\"artifact\"") !=
                std::string::npos &&
            serialized_content.find(first.content.object_key) ==
                std::string::npos,
        "public artifact reads must publish integrity and payload "
        "but hide provider object keys");
}

void test_run_configurations_bind_complete_execution_intent() {
    thermox::service::ProjectService service{
        thermox::service::make_in_memory_project_repository()};
    const auto project =
        service.create_project({team_a, "Run history", {}});
    const auto model = service.create_model_revision({
        team_a,
        project.project_id,
        {},
        read_source_file(
            "core/examples/air_compressor.topology.json"),
    });
    const auto simulation_case = service.create_case_revision({
        team_a,
        project.project_id,
        model.model_revision_id,
        {},
        read_source_file(
            "core/examples/air_compressor.design.case.json"),
    });
    const auto artifact = service.create_artifact_revision({
        team_a,
        project.project_id,
        "run-map",
        {},
        "thermox.performance_map",
        "thermox.performance_map/v1",
        performance_map_payload(),
    });
    thermox::service::CreateStudyRevisionRequest study_request;
    study_request.identity = team_a;
    study_request.project_id = project.project_id;
    study_request.study_id = "design-study";
    study_request.model_revision_id = model.model_revision_id;
    study_request.case_revision_id = simulation_case.case_revision_id;
    study_request.intent = simulation_case.mode;
    study_request.artifact_revision_ids = {
        artifact.artifact_revision_id,
    };
    study_request.result_projections = {
        {
            "compressor_outlet_temperature",
            thermox::service::ResultValueScope::port_derived,
            "compressor",
            "outlet",
            "T",
            "temperature",
            thermox::service::ResultAggregation::final,
        },
    };
    const auto study = service.create_study_revision(study_request);
    thermox::service::CreateRunConfigurationRevisionRequest
        request;
    request.identity = team_a;
    request.project_id = project.project_id;
    request.run_configuration_id = "design-run";
    request.study_revision_id = study.study_revision_id;
    request.steady_solver.max_iterations = 37;
    const auto first =
        service.create_run_configuration_revision(request);
    request.parent_run_configuration_revision_id =
        first.run_configuration_revision_id;
    const auto second =
        service.create_run_configuration_revision(request);
    const auto resolved = service.resolve_run_configuration(
        team_a,
        project.project_id,
        first.run_configuration_revision_id);
    require(
        first.study_revision_id == study.study_revision_id &&
            first.revision_number == 1U &&
            second.revision_number == 2U &&
            second.parent_run_configuration_revision_id ==
                first.run_configuration_revision_id &&
            first.checksum == second.checksum &&
            first.checksum.starts_with("sha256:") &&
            first.steady_solver.max_iterations == 37 &&
            resolved &&
            resolved->study.result_projections.size() == 1U &&
            resolved->model_case.model_revision_id ==
                model.model_revision_id &&
            resolved->artifacts.snapshot.performance_maps
                    .size() == 1U,
        "run configurations must immutably bind the complete "
        "execution intent and resolve its snapshots");
    require(
        service
                .list_run_configuration_revisions(
                    team_a, project.project_id)
                .size() == 2U &&
            !service
                 .get_run_configuration_revision(
                     team_b,
                     project.project_id,
                     first.run_configuration_revision_id)
                 .has_value(),
        "run configuration history must be Team scoped");
    const auto serialized = thermox::service::
        serialize_run_configuration_revision_json(first);
    require(
        serialized.find(first.run_configuration_revision_id) !=
                std::string::npos &&
            serialized.find("\"max_iterations\": 37") !=
                std::string::npos &&
            serialized.find(study.study_revision_id) !=
                std::string::npos,
        "run configuration JSON must publish its Study binding "
        "and solver policy");
}

void test_studies_bind_immutable_engineering_intent() {
    thermox::service::ProjectService service{
        thermox::service::make_in_memory_project_repository()};
    const auto project =
        service.create_project({team_a, "Study history", {}});
    const auto model = service.create_model_revision({
        team_a,
        project.project_id,
        {},
        read_source_file(
            "core/examples/air_compressor.topology.json"),
    });
    const auto simulation_case = service.create_case_revision({
        team_a,
        project.project_id,
        model.model_revision_id,
        {},
        read_source_file(
            "core/examples/air_compressor.design.case.json"),
    });
    thermox::service::CreateStudyRevisionRequest request;
    request.identity = team_a;
    request.project_id = project.project_id;
    request.study_id = "compressor-design";
    request.model_revision_id = model.model_revision_id;
    request.case_revision_id = simulation_case.case_revision_id;
    request.intent = simulation_case.mode;
    request.result_projections = {{
        "outlet_temperature",
        thermox::service::ResultValueScope::port_derived,
        "compressor",
        "outlet",
        "T",
        "temperature",
        thermox::service::ResultAggregation::final,
    }};
    request.acceptance_criteria = {{
        "outlet_temperature_band",
        "outlet_temperature",
        "temperature",
        300.0,
        700.0,
        true,
        true,
    }};
    const auto first = service.create_study_revision(request);
    request.parent_study_revision_id = first.study_revision_id;
    const auto second = service.create_study_revision(request);
    const auto history = service.list_study_revisions(
        team_a, project.project_id);
    require(
        first.revision_number == 1U &&
            second.revision_number == 2U &&
            history.size() == 2U &&
            first.checksum == second.checksum &&
            first.acceptance_criteria.size() == 1U &&
            !service.get_study_revision(
                team_b, project.project_id,
                first.study_revision_id),
        "studies must be immutable, revisioned, deterministic, "
        "and Team scoped");
    require(
        thermox::service::serialize_study_revision_json(first)
                    .find("\"intent\": \"steady_state_design\"") !=
                std::string::npos &&
            thermox::service::serialize_study_revision_json(first)
                    .find("\"acceptance_criteria\": [") !=
                std::string::npos,
        "study serialization must expose durable intent and "
        "engineering acceptance criteria");

    auto invalid_criterion = request;
    invalid_criterion.parent_study_revision_id.clear();
    invalid_criterion.study_id = "invalid-acceptance";
    invalid_criterion.acceptance_criteria.front().dimension = "power";
    bool invalid_criterion_rejected = false;
    try {
        (void)service.create_study_revision(invalid_criterion);
    } catch (const thermox::service::ProjectRequestError&) {
        invalid_criterion_rejected = true;
    }
    require(
        invalid_criterion_rejected,
        "Study publication must reject acceptance criteria whose "
        "dimension differs from the selected result projection");

    request.intent = "steady_state_off_design";
    bool mismatch_rejected = false;
    try {
        (void)service.create_study_revision(request);
    } catch (const thermox::service::ProjectRequestError&) {
        mismatch_rejected = true;
    }
    require(
        mismatch_rejected,
        "study intent must agree with its exact operating case");
}

void test_calibrations_bind_exact_training_studies() {
    thermox::service::ProjectService service{
        thermox::service::make_in_memory_project_repository()};
    const auto project =
        service.create_project({team_a, "Calibration history", {}});
    const auto model = service.create_model_revision({
        team_a, project.project_id, {},
        read_source_file("core/examples/air_compressor.topology.json"),
    });
    const auto simulation_case = service.create_case_revision({
        team_a, project.project_id, model.model_revision_id, {},
        read_source_file(
            "core/examples/air_compressor.design.case.json"),
    });
    thermox::service::CreateStudyRevisionRequest study_request;
    study_request.identity = team_a;
    study_request.project_id = project.project_id;
    study_request.study_id = "compressor-design";
    study_request.model_revision_id = model.model_revision_id;
    study_request.case_revision_id = simulation_case.case_revision_id;
    study_request.intent = simulation_case.mode;
    const auto study = service.create_study_revision(study_request);
    const auto validation_case = service.create_case_revision({
        team_a, project.project_id, model.model_revision_id, {},
        R"({"schema_version":"thermox.case/v1","case":{
          "id":"validation","mode":"steady_state_design",
          "fixed_values":{
            "compressor.inlet.m_dot":{"value":100,"unit":"kg/s"},
            "compressor.inlet.p":{"value":101.325,"unit":"kPa"},
            "compressor.inlet.T":{"value":300,"unit":"K"},
            "compressor.shaft.omega":314.1592653589793},
          "initial_guesses":{
            "compressor.outlet.p":{"value":1.2,"unit":"MPa"},
            "compressor.outlet.h":{"value":650,"unit":"kJ/kg"},
            "compressor.shaft.W_dot":{"value":35,"unit":"MW"}}
        }})",
    });
    study_request.study_id = "compressor-validation";
    study_request.case_revision_id = validation_case.case_revision_id;
    const auto validation_study =
        service.create_study_revision(study_request);

    thermox::service::CreateCalibrationRevisionRequest request;
    request.identity = team_a;
    request.project_id = project.project_id;
    request.calibration_id = "acceptance-fit";
    request.model_revision_id = model.model_revision_id;
    request.training_study_revision_ids = {study.study_revision_id};
    request.validation_study_revision_ids = {
        validation_study.study_revision_id,
    };
    request.definition_json = R"({
      "schema_version": "thermox.calibration/v1",
      "calibration": {
        "id": "acceptance-fit",
        "parameters": [{
          "id": "efficiency", "scope": "component",
          "targets": ["components.compressor.parameters.eta_is"],
          "cases": ["design"],
          "bounds": {"lower": 0.75, "upper": 0.95}
        }],
        "observations": [{
          "id": "shaft-power", "case": "design",
          "target": "compressor.shaft.W_dot",
          "measured": {"value": 35.0, "unit": "MW"},
          "sigma": {"value": 0.5, "unit": "MW"}
        }, {
          "id": "validation-shaft-power", "case": "validation",
          "target": "compressor.shaft.W_dot",
          "measured": {"value": 35.0, "unit": "MW"},
          "sigma": {"value": 0.5, "unit": "MW"}
        }]
      }
    })";
    const auto first = service.create_calibration_revision(request);
    request.parent_calibration_revision_id =
        first.calibration_revision_id;
    const auto second = service.create_calibration_revision(request);
    require(
        first.revision_number == 1U && second.revision_number == 2U &&
            first.checksum == second.checksum &&
            first.training_study_revision_ids ==
                std::vector<std::string>{study.study_revision_id} &&
            service.list_calibration_revisions(
                team_a, project.project_id).size() == 2U &&
            !service.get_calibration_revision(
                team_b, project.project_id,
                first.calibration_revision_id),
        "calibrations must be immutable and bind exact Team-scoped Studies");
    require(
        thermox::service::serialize_calibration_revision_json(first)
                .find("\"thermox.calibration/v1\"") !=
            std::string::npos,
        "calibration serialization must expose its canonical definition");
    const auto resolved = service.resolve_calibration(
        team_a, project.project_id, first.calibration_revision_id);
    require(
        resolved &&
            resolved->calibration.calibration_revision_id ==
                first.calibration_revision_id &&
            resolved->studies.size() == 2U &&
            resolved->validation_predictions.size() == 1U &&
            resolved->validation_predictions.front().observations.size() ==
                1U &&
            resolved->executable_model_json.find(
                "\"calibrations\": [") != std::string::npos,
        "calibration execution must compose exact model, Study case, "
        "definition, and artifact revisions");
    thermox::service::EngineeringStudyRequest execution;
    execution.model_json = resolved->executable_model_json;
    execution.calibration_id = first.calibration_id;
    execution.calibration_solver = first.solver;
    execution.prediction_solver = first.solver.simulation_solver;
    execution.prediction_cases = resolved->validation_predictions;
    const auto execution_result =
        thermox::service::SimulationService{
            thermox::service::make_default_simulation_runtime()}
            .run_engineering_study(execution);
    require(
        execution_result.succeeded() &&
            execution_result.predictions.size() == 1U &&
            execution_result.predictions.front().observations.size() == 1U,
        "validation observations must be evaluated after fitting and "
        "must not influence the calibration objective");

    request.parent_calibration_revision_id.clear();
    request.validation_study_revision_ids = {
        validation_study.study_revision_id,
        study.study_revision_id,
    };
    bool overlap_rejected = false;
    try {
        (void)service.create_calibration_revision(request);
    } catch (const thermox::service::ProjectRequestError&) {
        overlap_rejected = true;
    }
    require(overlap_rejected,
            "training and validation Study sets must be disjoint");
}

void test_graph_edits_publish_valid_child_revisions() {
    thermox::service::ProjectService service{
        thermox::service::make_in_memory_project_repository()};
    const auto project =
        service.create_project({team_a, "Graph edits", {}});
    const auto base = service.create_model_revision({
        team_a,
        project.project_id,
        {},
        read_source_file(
            "core/examples/air_compressor.topology.json"),
    });

    thermox::service::ApplyGraphEditsRequest edit;
    edit.identity = team_a;
    edit.project_id = project.project_id;
    edit.base_model_revision_id = base.model_revision_id;
    edit.operations = {
        {
            thermox::service::GraphEditAction::upsert,
            thermox::service::GraphEntityType::component,
            "compressor",
            R"json({
              "schema_version":
                "thermox.component_definition/v1",
              "component": {
                "id": "compressor",
                "label": "Main compressor",
                "kind":
                  "compressor.fluid.isentropic_efficiency",
                "version": "1.0.0",
                "media": {
                  "inlet": "air",
                  "outlet": "air"
                },
                "parameters": {
                  "pressure_ratio": 14.0,
                  "eta_is": 0.87
                }
              }
            })json",
            false,
        },
    };
    const auto child = service.apply_graph_edits(edit);
    require(
        child.parent_model_revision_id ==
                base.model_revision_id &&
            child.revision_number == 2U &&
            child.canonical_model_json.find(
                "\"label\": \"Main compressor\"") !=
                std::string::npos &&
            child.canonical_model_json.find(
                "\"pressure_ratio\": 14") !=
                std::string::npos,
        "a graph edit batch must publish one canonical child "
        "revision");

    auto invalid = edit;
    invalid.base_model_revision_id = child.model_revision_id;
    invalid.operations = {
        {
            thermox::service::GraphEditAction::remove,
            thermox::service::GraphEntityType::medium,
            "air",
            {},
            false,
        },
    };
    bool rejected = false;
    try {
        (void)service.apply_graph_edits(invalid);
    } catch (const thermox::service::ProjectRequestError&) {
        rejected = true;
    }
    require(
        rejected &&
            service
                    .list_model_revisions(
                        team_a, project.project_id)
                    .size() == 2U,
        "invalid graph edits must be rejected without publishing "
        "a partial revision");

    edit.identity = team_b;
    rejected = false;
    try {
        (void)service.apply_graph_edits(edit);
    } catch (const thermox::service::ProjectStateError&) {
        rejected = true;
    }
    require(
        rejected,
        "graph edits must not reveal or modify another Team's "
        "base revision");
}

void test_case_edits_publish_atomic_child_revisions() {
    thermox::service::ProjectService service{
        thermox::service::make_in_memory_project_repository()};
    const auto project =
        service.create_project({team_a, "Case edits", {}});
    const auto model = service.create_model_revision({
        team_a,
        project.project_id,
        {},
        read_source_file(
            "core/examples/air_compressor.topology.json"),
    });
    const auto base = service.create_case_revision({
        team_a,
        project.project_id,
        model.model_revision_id,
        {},
        read_source_file(
            "core/examples/air_compressor.design.case.json"),
    });

    thermox::service::ApplyCaseEditsRequest edit;
    edit.identity = team_a;
    edit.project_id = project.project_id;
    edit.model_revision_id = model.model_revision_id;
    edit.base_case_revision_id = base.case_revision_id;
    edit.operations = {
        {
            thermox::service::CaseEditAction::upsert,
            thermox::service::CaseEditField::label,
            {},
            "Hot-day design",
            {},
        },
        {
            thermox::service::CaseEditAction::upsert,
            thermox::service::CaseEditField::fixed_value,
            "compressor.inlet.p",
            {},
            R"json({
              "schema_version": "thermox.scalar_value/v1",
              "scalar": {"value": 2.0, "unit": "bar"}
            })json",
        },
        {
            thermox::service::CaseEditAction::remove,
            thermox::service::CaseEditField::initial_guess,
            "compressor.outlet.h",
            {},
            {},
        },
    };
    const auto child = service.apply_case_edits(edit);
    require(
        child.parent_case_revision_id ==
                base.case_revision_id &&
            child.revision_number == 2U &&
            child.case_id == base.case_id &&
            child.canonical_case_json.find(
                "\"label\": \"Hot-day design\"") !=
                std::string::npos &&
            child.canonical_case_json.find(
                "\"value\": 200000") !=
                std::string::npos &&
            child.canonical_case_json.find(
                "compressor.outlet.h") ==
                std::string::npos,
        "case edits must preserve identity, normalize units, "
        "and publish one immutable child revision");

    auto invalid = edit;
    invalid.base_case_revision_id = child.case_revision_id;
    invalid.operations = {
        {
            thermox::service::CaseEditAction::remove,
            thermox::service::CaseEditField::fixed_value,
            "missing.value",
            {},
            {},
        },
    };
    bool rejected = false;
    try {
        (void)service.apply_case_edits(invalid);
    } catch (const thermox::service::ProjectRequestError&) {
        rejected = true;
    }
    require(
        rejected &&
            service
                    .list_case_revisions(
                        team_a,
                        project.project_id,
                        model.model_revision_id)
                    .size() == 2U,
        "invalid case edits must not publish partial "
        "revisions");

    edit.identity = team_b;
    rejected = false;
    try {
        (void)service.apply_case_edits(edit);
    } catch (const thermox::service::ProjectStateError&) {
        rejected = true;
    }
    require(
        rejected,
        "case edits must hide another Team's base revision");
}

void test_revision_backed_validation_resolves_exact_inputs() {
    auto projects = std::make_shared<
        thermox::service::ProjectService>(
        thermox::service::
            make_in_memory_project_repository());
    const auto project = projects->create_project(
        {team_a, "Validation", {}});
    const auto model = projects->create_model_revision({
        team_a,
        project.project_id,
        {},
        read_source_file(
            "core/examples/air_compressor.topology.json"),
    });
    const auto simulation_case =
        projects->create_case_revision({
            team_a,
            project.project_id,
            model.model_revision_id,
            {},
            read_source_file(
                "core/examples/"
                "air_compressor.design.case.json"),
        });
    thermox::service::ProjectModelValidationService validator{
        projects,
        thermox::service::make_default_simulation_runtime(),
    };
    const auto response = validator.validate({
        team_a,
        project.project_id,
        model.model_revision_id,
        simulation_case.case_revision_id,
        {},
    });
    require(
        response.validation.succeeded() &&
            response.validation.compilation.compiled &&
            response.project_id == project.project_id &&
            response.model_revision_id ==
                model.model_revision_id &&
            response.model_checksum == model.checksum &&
            response.case_revision_id ==
                simulation_case.case_revision_id &&
            response.case_checksum == simulation_case.checksum,
        "revision-backed validation must compile the exact "
        "persisted model/case pair and publish provenance");

    bool hidden = false;
    try {
        (void)validator.validate({
            team_b,
            project.project_id,
            model.model_revision_id,
            simulation_case.case_revision_id,
            {},
        });
    } catch (const thermox::service::ProjectStateError&) {
        hidden = true;
    }
    require(
        hidden,
        "revision-backed validation must hide cross-Team "
        "revision existence");

    const auto serialized =
        thermox::service::
            serialize_project_model_validation_json(response);
    require(
        serialized.find(
            "\"schema_version\": "
            "\"thermox.project_model_validation/v1\"") !=
                std::string::npos &&
            serialized.find("\"compiled\": true") !=
                std::string::npos &&
            serialized.find(model.checksum) !=
                std::string::npos,
        "revision validation serialization must carry result "
        "and immutable provenance");
}

}  // namespace

int main() {
    try {
        test_projects_are_team_scoped_logical_partitions();
        test_model_revisions_are_immutable_and_scoped();
        test_assembly_hierarchy_survives_model_revision_persistence();
        test_invalid_input_is_rejected_before_persistence();
        test_public_json_omits_model_from_history();
        test_case_revisions_bind_exact_model_revisions();
        test_expression_component_artifact_is_executable();
        test_correlation_artifact_is_executable_input();
        test_artifact_revisions_are_snapshotted_and_scoped();
        test_run_configurations_bind_complete_execution_intent();
        test_studies_bind_immutable_engineering_intent();
        test_calibrations_bind_exact_training_studies();
        test_graph_edits_publish_valid_child_revisions();
        test_case_edits_publish_atomic_child_revisions();
        test_revision_backed_validation_resolves_exact_inputs();
        std::cout << "project service tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "project service test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
