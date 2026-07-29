#include "thermox/service/in_memory_projects.hpp"
#include "thermox/service/projects.hpp"

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
        read_source_file("core/examples/air_compressor.json");
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
                "\"schema_version\": \"thermox.model/v2\"") !=
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
        read_source_file("core/examples/air_compressor.json"),
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

}  // namespace

int main() {
    try {
        test_projects_are_team_scoped_logical_partitions();
        test_model_revisions_are_immutable_and_scoped();
        test_invalid_input_is_rejected_before_persistence();
        test_public_json_omits_model_from_history();
        std::cout << "project service tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "project service test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
