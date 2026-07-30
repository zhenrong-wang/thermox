#include "thermox/service/in_memory_projects.hpp"
#include "thermox/service/projects.hpp"
#include "thermox/service/simulation_runtime.hpp"
#include "thermox/service/simulation_service.hpp"

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
    const auto serialized =
        thermox::service::serialize_artifact_revision_json(first);
    require(
        serialized.find(first.content.checksum) !=
                std::string::npos &&
            serialized.find(first.content.object_key) ==
                std::string::npos,
        "public artifact metadata must publish integrity but "
        "hide provider object keys");
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
    thermox::service::CreateRunConfigurationRevisionRequest
        request;
    request.identity = team_a;
    request.project_id = project.project_id;
    request.run_configuration_id = "design-run";
    request.model_revision_id = model.model_revision_id;
    request.case_revision_id =
        simulation_case.case_revision_id;
    request.artifact_revision_ids = {
        artifact.artifact_revision_id,
    };
    request.steady_solver.max_iterations = 37;
    request.result_projections = {
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
        first.mode == "steady" &&
            first.revision_number == 1U &&
            second.revision_number == 2U &&
            second.parent_run_configuration_revision_id ==
                first.run_configuration_revision_id &&
            first.checksum == second.checksum &&
            first.checksum.starts_with("sha256:") &&
            first.steady_solver.max_iterations == 37 &&
            first.result_projections.size() == 1U &&
            first.result_projections.front().component_id ==
                "compressor" &&
            resolved &&
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
            serialized.find(
                "\"id\": "
                "\"compressor_outlet_temperature\"") !=
                std::string::npos,
        "run configuration JSON must publish bindings and "
        "solver and result-projection policy");
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
        test_invalid_input_is_rejected_before_persistence();
        test_public_json_omits_model_from_history();
        test_case_revisions_bind_exact_model_revisions();
        test_artifact_revisions_are_snapshotted_and_scoped();
        test_run_configurations_bind_complete_execution_intent();
        test_graph_edits_publish_valid_child_revisions();
        test_revision_backed_validation_resolves_exact_inputs();
        std::cout << "project service tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "project service test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
