#include "thermox/postgres/postgres_job_repository.hpp"
#include "thermox/postgres/postgres_project_repository.hpp"

#include "thermox/service/projects.hpp"

#include <libpq-fe.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using thermox::service::SimulationJobRecord;
using thermox::service::SimulationJobRequest;
using thermox::service::SimulationJobState;

struct ConnectionDeleter {
    void operator()(PGconn* connection) const {
        if (connection != nullptr) {
            PQfinish(connection);
        }
    }
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void execute_sql(
    PGconn* connection,
    const std::string& sql,
    const std::string& description) {
    PGresult* result = PQexec(connection, sql.c_str());
    const bool succeeded =
        result != nullptr &&
        (PQresultStatus(result) == PGRES_COMMAND_OK ||
         PQresultStatus(result) == PGRES_TUPLES_OK);
    const std::string message = result
        ? PQresultErrorMessage(result)
        : PQerrorMessage(connection);
    if (result != nullptr) {
        PQclear(result);
    }
    require(
        succeeded,
        "could not " + description + ": " + message);
}

std::unique_ptr<PGconn, ConnectionDeleter> connect(
    const std::string& connection_string) {
    std::unique_ptr<PGconn, ConnectionDeleter> connection{
        PQconnectdb(connection_string.c_str())};
    require(
        connection &&
            PQstatus(connection.get()) == CONNECTION_OK,
        "could not connect to the PostgreSQL test database");
    return connection;
}

void prepare_test_schema(const std::string& connection_string) {
    require(
        setenv(
            "PGOPTIONS",
            "-c search_path=thermox_repository_contract_test",
            1) == 0,
        "could not configure the PostgreSQL test search path");
    auto connection = connect(connection_string);
    execute_sql(
        connection.get(),
        "DROP SCHEMA IF EXISTS "
        "thermox_repository_contract_test CASCADE; "
        "CREATE SCHEMA thermox_repository_contract_test; "
        "SET search_path TO thermox_repository_contract_test",
        "prepare the isolated PostgreSQL test schema");

    for (const std::string migration_name : {
             "001_simulation_jobs.sql",
             "002_worker_leases.sql",
             "003_projects_and_model_revisions.sql",
             "004_case_revisions.sql",
             "005_artifact_revisions.sql",
             "006_run_configuration_revisions.sql",
             "007_simulation_job_history.sql",
             "008_run_result_projections.sql",
         }) {
        std::ifstream migration(
            std::string(THERMOX_SOURCE_DIR) +
            "/adapters/postgres/migrations/" +
            migration_name);
        require(
            static_cast<bool>(migration),
            "could not read PostgreSQL test migration " +
                migration_name);
        std::ostringstream sql;
        sql << migration.rdbuf();
        execute_sql(
            connection.get(),
            sql.str(),
            "apply test migration " + migration_name);
    }
}

void clear_test_jobs(const std::string& connection_string) {
    auto connection = connect(connection_string);
    execute_sql(
        connection.get(),
        "TRUNCATE thermox_simulation_jobs",
        "clear the isolated PostgreSQL test jobs");
}

void drop_test_schema(const std::string& connection_string) {
    auto connection = connect(connection_string);
    execute_sql(
        connection.get(),
        "DROP SCHEMA IF EXISTS "
        "thermox_repository_contract_test CASCADE",
        "drop the isolated PostgreSQL test schema");
    if (unsetenv("PGOPTIONS") != 0) {
        throw std::runtime_error(
            "could not clear the PostgreSQL test search path");
    }
}

SimulationJobRequest request(
    std::string team_id,
    std::string idempotency_key) {
    SimulationJobRequest value;
    value.identity = {
        "postgres-test-user",
        std::move(team_id),
        "postgres-test-request",
    };
    value.idempotency_key = std::move(idempotency_key);
    value.model_json =
        R"({"schema_version":"thermox.model/v2","name":"persisted"})";
    value.case_id = "design";
    value.source_revisions =
        thermox::service::RevisionProvenance{
            "project-postgres",
            "model-revision-postgres",
            "sha256:" + std::string(64, '1'),
            "case-revision-postgres",
            "sha256:" + std::string(64, '2'),
        };
    value.steady_solver.max_iterations = 17;
    value.transient_solver.end_time = 12.5;

    thermox::service::PerformanceMapArtifactInput map;
    map.id = "compressor-map";
    map.schema_version = "thermox.performance_map/v1";
    map.revision = "oem-7";
    map.checksum_sha256 = std::string(64, 'a');
    thermox::service::PerformanceMapPayloadInput payload;
    payload.primary_variable = {"flow", "mass_flow"};
    payload.family_variable = {"speed", "angular_speed"};
    payload.output_variables = {
        {"efficiency", "dimensionless"}};
    payload.curves = {
        {1.0, {{2.0, {0.91}}, {3.0, {0.92}}}},
    };
    map.map = payload;
    value.artifacts.performance_maps.push_back(std::move(map));
    value.artifacts.references.push_back({
        "fuel-spec",
        "thermox.material",
        "thermox.material/v1",
        "lab-3",
        std::string(64, 'b'),
    });
    return value;
}

thermox::service::ExecutionMetadata execution() {
    thermox::service::ExecutionMetadata value;
    value.command_schema_version =
        thermox::service::command_schema_v1;
    value.platform_version = "postgres-test";
    value.operation = "steady";
    value.solver.contract_version = "thermox.numeric/v1";
    value.solver.settings = {{"residual_tolerance", 1.0e-9}};
    value.catalog_fingerprint = "fnv1a64:test";
    value.model = {
        "thermox.model/v2", "model-a", "revision-a", "design"};
    value.components = {
        {"compressor", "compressor", "1", "1"}};
    value.media = {
        {"air", "coolprop", "Air", "CoolProp", "7", "7"}};
    value.artifacts = {
        {"compressor-map",
         "thermox.performance_map",
         "thermox.performance_map/v1",
         "oem-7",
         std::string(64, 'a')}};
    value.connector_domains = {{"fluid", "thermox.fluid/v1"}};
    value.source_revisions =
        thermox::service::RevisionProvenance{
            "project-postgres",
            "model-revision-postgres",
            "sha256:" + std::string(64, '1'),
            "case-revision-postgres",
            "sha256:" + std::string(64, '2'),
        };
    return value;
}

void test_idempotency_and_tenant_scope(
    const std::shared_ptr<
        thermox::service::SimulationJobRepository>& jobs) {
    const auto first_request = request("team-a", "shared-key");
    const auto first =
        jobs->create_or_get(first_request, "fingerprint-a");
    const auto repeated =
        jobs->create_or_get(first_request, "fingerprint-a");
    require(
        first.job_id == repeated.job_id &&
            repeated.request.steady_solver.max_iterations == 17 &&
            repeated.request.artifacts.performance_maps.size() == 1 &&
            repeated.request.source_revisions &&
            repeated.request.source_revisions
                    ->case_revision_id ==
                "case-revision-postgres",
        "idempotent submission must return the decoded request");

    bool conflict = false;
    try {
        (void)jobs->create_or_get(
            first_request, "different-fingerprint");
    } catch (const thermox::service::JobConflictError&) {
        conflict = true;
    }
    require(
        conflict,
        "reusing a Team idempotency key with a different "
        "fingerprint must conflict");

    const auto other = jobs->create_or_get(
        request("team-b", "shared-key"), "fingerprint-b");
    require(
        other.job_id != first.job_id &&
            !jobs->get("team-b", first.job_id).has_value() &&
            jobs->get("team-a", first.job_id).has_value(),
        "job identity and reads must be Team scoped");
}

void test_indexed_history_query(
    const std::shared_ptr<
        thermox::service::SimulationJobRepository>& jobs,
    const std::string& connection_string) {
    clear_test_jobs(connection_string);
    auto first_request = request("team-a", "history-a");
    first_request.source_revisions
        ->run_configuration_revision_id = "run-postgres-a";
    first_request.source_revisions
        ->run_configuration_checksum =
        "sha256:" + std::string(64, '3');
    const auto first = jobs->create_or_get(
        first_request, "history-fingerprint-a");
    auto second_request = request("team-a", "history-b");
    second_request.source_revisions
        ->run_configuration_revision_id = "run-postgres-b";
    second_request.source_revisions
        ->run_configuration_checksum =
        "sha256:" + std::string(64, '4');
    const auto second = jobs->create_or_get(
        second_request, "history-fingerprint-b");
    (void)jobs->create_or_get(
        request("team-b", "history-other"),
        "history-fingerprint-other");

    thermox::service::SimulationJobQuery query;
    query.limit = 1;
    const auto page = jobs->list("team-a", query);
    require(
        page.jobs.size() == 1U &&
            page.jobs.front().job_id == second.job_id &&
            page.jobs.front().created_at.time_since_epoch().count() >
                0 &&
            page.next.has_value(),
        "PostgreSQL history must return a timestamped, bounded "
        "newest-first page");
    query.before = page.next;
    const auto continuation = jobs->list("team-a", query);
    require(
        continuation.jobs.size() == 1U &&
            continuation.jobs.front().job_id == first.job_id &&
            !continuation.next.has_value(),
        "PostgreSQL history cursor must resume deterministically");
    query = {};
    query.run_configuration_revision_id = "run-postgres-a";
    const auto filtered = jobs->list("team-a", query);
    require(
        filtered.jobs.size() == 1U &&
            filtered.jobs.front().job_id == first.job_id &&
            jobs->list("team-b", {}).jobs.size() == 1U,
        "PostgreSQL history filters and ownership must use "
        "indexed Team-scoped metadata");
}

void test_atomic_claim_and_terminal_publication(
    const std::shared_ptr<
        thermox::service::SimulationJobRepository>& jobs,
    const std::string& connection_string) {
    clear_test_jobs(connection_string);
    const auto queued = jobs->create_or_get(
        request("team-a", "atomic"), "fingerprint-atomic");

    std::optional<SimulationJobRecord> first;
    std::optional<SimulationJobRecord> second;
    std::thread one(
        [&] { first = jobs->claim_next("worker-one"); });
    std::thread two(
        [&] { second = jobs->claim_next("worker-two"); });
    one.join();
    two.join();
    require(
        first.has_value() != second.has_value(),
        "SKIP LOCKED claim must give one job to exactly one worker");

    const auto claimed = first ? *first : *second;
    require(
        claimed.job_id == queued.job_id &&
            claimed.state == SimulationJobState::running &&
            claimed.revision == 2 &&
            !claimed.worker_id.empty() &&
            claimed.attempt == 1 &&
            claimed.lease_expires_at.has_value(),
        "claim must atomically publish a running revision");

    const thermox::service::ResultArtifactManifest manifest{
        "artifact-a",
        "application/json",
        thermox::service::result_schema_v3,
        42,
        "sha256:test",
    };
    thermox::service::ResultSummary summary;
    summary.mode = "steady";
    summary.values = {
        {
            "net_power",
            "power",
            42.0,
            thermox::service::ResultAggregation::final,
            false,
            0.0,
        },
    };
    const auto succeeded = jobs->publish_success(
        claimed.job_id,
        claimed.revision,
        execution(),
        manifest,
        summary);
    require(
        succeeded.state == SimulationJobState::succeeded &&
            succeeded.revision == 3 &&
            succeeded.execution.has_value() &&
            succeeded.execution->components.size() == 1 &&
            succeeded.result_artifact.has_value() &&
            succeeded.result_artifact->byte_size == 42 &&
            succeeded.result_summary.has_value() &&
            succeeded.result_summary->values.size() == 1U &&
            succeeded.result_summary->values.front().value_si ==
                42.0 &&
            !succeeded.lease_expires_at.has_value(),
        "success publication must preserve provenance and "
        "artifact metadata");

    bool conflict = false;
    try {
        (void)jobs->publish_success(
            claimed.job_id,
            claimed.revision,
            execution(),
            manifest,
            summary);
    } catch (const thermox::service::JobConflictError&) {
        conflict = true;
    }
    require(
        conflict,
        "terminal publication must reject a stale revision");
}

void test_expired_lease_recovery_and_fencing(
    const std::shared_ptr<
        thermox::service::SimulationJobRepository>& jobs,
    const std::string& connection_string) {
    using namespace std::chrono_literals;
    clear_test_jobs(connection_string);
    const auto queued = jobs->create_or_get(
        request("team-a", "lease-recovery"),
        "fingerprint-lease-recovery");
    const auto first =
        jobs->claim_next("worker-dead", 20ms);
    require(
        first && first->job_id == queued.job_id &&
            first->attempt == 1,
        "first lease attempt must be claimed");
    std::this_thread::sleep_for(30ms);

    bool fenced = false;
    try {
        (void)jobs->publish_failure(
            first->job_id,
            first->revision,
            {
                thermox::service::error_schema_v1,
                "late_worker",
                "worker",
                "late",
            },
            std::nullopt);
    } catch (const thermox::service::JobStateError&) {
        fenced = true;
    }
    require(
        fenced,
        "an expired worker must be fenced before recovery");

    const thermox::service::ServiceError exhausted{
        thermox::service::error_schema_v1,
        "worker_attempts_exhausted",
        "worker",
        "attempt limit reached",
    };
    require(
        jobs->recover_expired(2, exhausted) == 1,
        "one expired first attempt must be recovered");
    const auto requeued = jobs->get("team-a", first->job_id);
    require(
        requeued &&
            requeued->state == SimulationJobState::queued &&
            requeued->revision == first->revision + 1 &&
            requeued->attempt == 1 &&
            requeued->worker_id.empty() &&
            !requeued->lease_expires_at,
        "an eligible expired attempt must be requeued with a "
        "new fencing revision");

    const auto second =
        jobs->claim_next("worker-dead-again", 20ms);
    require(
        second && second->attempt == 2 &&
            second->revision == requeued->revision + 1,
        "reclaimed jobs must increment attempt and revision");
    require(
        jobs->renew_lease(
            second->job_id,
            second->revision,
            second->worker_id,
            40ms),
        "the current worker must be able to renew a live lease");
    require(
        !jobs->renew_lease(
            second->job_id,
            second->revision,
            "other-worker",
            40ms),
        "another worker must not renew the claimed lease");
    std::this_thread::sleep_for(50ms);
    require(
        jobs->recover_expired(2, exhausted) == 1,
        "the exhausted second attempt must be recovered");
    const auto failed = jobs->get("team-a", second->job_id);
    require(
        failed &&
            failed->state == SimulationJobState::failed &&
            failed->revision == second->revision + 1 &&
            failed->error &&
            failed->error->code ==
                "worker_attempts_exhausted" &&
            !failed->lease_expires_at,
        "an exhausted job must terminate with a structured "
        "worker error");
}

void test_failure_and_cancellation(
    const std::shared_ptr<
        thermox::service::SimulationJobRepository>& jobs,
    const std::string& connection_string) {
    clear_test_jobs(connection_string);
    const auto to_fail = jobs->create_or_get(
        request("team-a", "failure"), "fingerprint-failure");
    const auto claimed = jobs->claim_next("worker-failure");
    require(
        claimed && claimed->job_id == to_fail.job_id,
        "failure test job must be claimable");
    const auto failed = jobs->publish_failure(
        claimed->job_id,
        claimed->revision,
        {
            thermox::service::error_schema_v1,
            "test_failure",
            "test",
            "intentional",
        },
        std::nullopt);
    require(
        failed.state == SimulationJobState::failed &&
            failed.error &&
            failed.error->code == "test_failure" &&
            !failed.execution,
        "failure publication must preserve a nullable execution "
        "and structured error");

    const auto queued = jobs->create_or_get(
        request("team-a", "cancel"), "fingerprint-cancel");
    bool hidden = false;
    try {
        (void)jobs->cancel(
            "team-b", queued.job_id, queued.revision);
    } catch (const thermox::service::JobStateError&) {
        hidden = true;
    }
    require(
        hidden,
        "cross-Team cancellation must behave as not found");
    const auto cancelled = jobs->cancel(
        "team-a", queued.job_id, queued.revision);
    require(
        cancelled.state == SimulationJobState::cancelled &&
            cancelled.revision == 2,
        "queued jobs must support revision-checked cancellation");
}

void test_projects_and_immutable_model_revisions(
    const std::string& connection_string) {
    thermox::service::ProjectService projects{
        thermox::postgres::make_postgres_project_repository(
            connection_string)};
    const thermox::service::IdentityContext team_a{
        "postgres-user-a", "postgres-team-a", "project-test"};
    const thermox::service::IdentityContext team_b{
        "postgres-user-b", "postgres-team-b", "project-test"};
    const auto project = projects.create_project({
        team_a, "PostgreSQL cycle", "Repository contract",
    });
    require(
        project.team_id == team_a.team_id &&
            projects.list_projects(team_a).size() == 1 &&
            projects.list_projects(team_b).empty() &&
            !projects
                 .get_project(team_b, project.project_id)
                 .has_value(),
        "PostgreSQL projects must use Team as their ownership "
        "and query boundary");

    std::ifstream model_file(
        std::string(THERMOX_SOURCE_DIR) +
        "/core/examples/air_compressor.topology.json");
    require(
        static_cast<bool>(model_file),
        "could not read model revision fixture");
    std::ostringstream model;
    model << model_file.rdbuf();
    const auto first = projects.create_model_revision({
        team_a, project.project_id, {}, model.str(),
    });
    const auto second = projects.create_model_revision({
        team_a,
        project.project_id,
        first.model_revision_id,
        model.str(),
    });
    require(
        first.revision_number == 1 &&
            second.revision_number == 2 &&
            second.parent_model_revision_id ==
                first.model_revision_id &&
            first.canonical_model_json ==
                second.canonical_model_json &&
            first.checksum == second.checksum &&
            projects
                    .list_model_revisions(
                        team_a, project.project_id)
                    .size() == 2,
        "PostgreSQL must preserve immutable canonical model "
        "bytes and ordered revision history");
    require(
        !projects
             .get_model_revision(
                 team_b,
                 project.project_id,
                 first.model_revision_id)
             .has_value(),
        "cross-Team revision lookup must not reveal existence");

    std::ifstream case_file(
        std::string(THERMOX_SOURCE_DIR) +
        "/core/examples/air_compressor.design.case.json");
    require(
        static_cast<bool>(case_file),
        "could not read case revision fixture");
    std::ostringstream simulation_case;
    simulation_case << case_file.rdbuf();
    const auto first_case = projects.create_case_revision({
        team_a,
        project.project_id,
        first.model_revision_id,
        {},
        simulation_case.str(),
    });
    const auto second_case = projects.create_case_revision({
        team_a,
        project.project_id,
        first.model_revision_id,
        first_case.case_revision_id,
        simulation_case.str(),
    });
    require(
        first_case.case_id == "design" &&
            first_case.revision_number == 1 &&
            second_case.revision_number == 2 &&
            second_case.parent_case_revision_id ==
                first_case.case_revision_id &&
            projects
                    .list_case_revisions(
                        team_a,
                        project.project_id,
                        first.model_revision_id)
                    .size() == 2 &&
            !projects
                 .get_case_revision(
                     team_b,
                     project.project_id,
                     first.model_revision_id,
                     first_case.case_revision_id)
                 .has_value(),
        "PostgreSQL case revisions must be immutable, ordered, "
        "model-bound, and Team scoped");

    const auto artifact = projects.create_artifact_revision({
        team_a,
        project.project_id,
        "postgres-compressor-map",
        {},
        "thermox.performance_map",
        "thermox.performance_map/v1",
        R"json({
          "primary_variable": {
            "name": "corrected_mass_flow",
            "dimension": "mass_flow"
          },
          "family_variable": {
            "name": "corrected_speed",
            "dimension": "angular_speed"
          },
          "output_variables": [
            {"name": "pressure_ratio",
             "dimension": "dimensionless"},
            {"name": "isentropic_efficiency",
             "dimension": "dimensionless"}
          ],
          "curves": [
            {"family_coordinate": 250.0, "samples": [
              {"coordinate": 70.0,
               "outputs": [10.0, 0.85]},
              {"coordinate": 120.0,
               "outputs": [10.0, 0.85]}
            ]},
            {"family_coordinate": 400.0, "samples": [
              {"coordinate": 70.0,
               "outputs": [10.0, 0.85]},
              {"coordinate": 120.0,
               "outputs": [10.0, 0.85]}
            ]}
          ]
        })json",
    });
    require(
        artifact.revision_number == 1U &&
            artifact.content.checksum.starts_with("sha256:") &&
            projects
                    .list_artifact_revisions(
                        team_a, project.project_id)
                    .size() == 1U &&
            !projects
                 .get_artifact_revision(
                     team_b,
                     project.project_id,
                     artifact.artifact_revision_id)
                 .has_value(),
        "PostgreSQL artifact metadata must be immutable and "
        "Team scoped");

    thermox::service::CreateRunConfigurationRevisionRequest
        run_request;
    run_request.identity = team_a;
    run_request.project_id = project.project_id;
    run_request.run_configuration_id = "postgres-design-run";
    run_request.model_revision_id = first.model_revision_id;
    run_request.case_revision_id =
        first_case.case_revision_id;
    run_request.artifact_revision_ids = {
        artifact.artifact_revision_id,
    };
    run_request.steady_solver.max_iterations = 41;
    run_request.result_projections = {
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
    const auto run =
        projects.create_run_configuration_revision(run_request);
    const auto loaded =
        projects.get_run_configuration_revision(
            team_a,
            project.project_id,
            run.run_configuration_revision_id);
    require(
        loaded &&
            loaded->artifact_revision_ids ==
                run_request.artifact_revision_ids &&
            loaded->steady_solver.max_iterations == 41 &&
            loaded->result_projections.size() == 1U &&
            loaded->result_projections.front().value_name == "T" &&
            loaded->checksum == run.checksum &&
            projects
                    .list_run_configuration_revisions(
                        team_a, project.project_id)
                    .size() == 1U &&
            !projects
                 .get_run_configuration_revision(
                     team_b,
                     project.project_id,
                     run.run_configuration_revision_id)
                 .has_value(),
        "PostgreSQL run configurations must preserve exact "
        "bindings, solver policy, and Team isolation");
}

}  // namespace

int main() {
    const char* connection_string =
        std::getenv("THERMOX_TEST_POSTGRES_URL");
    if (connection_string == nullptr ||
        std::string(connection_string).empty()) {
        std::cout
            << "THERMOX_TEST_POSTGRES_URL is unset; "
               "skipping PostgreSQL integration tests\n";
        return 77;
    }

    try {
        prepare_test_schema(connection_string);
        auto jobs =
            thermox::postgres::make_postgres_job_repository(
                connection_string);
        test_idempotency_and_tenant_scope(jobs);
        test_indexed_history_query(jobs, connection_string);
        test_atomic_claim_and_terminal_publication(
            jobs, connection_string);
        test_expired_lease_recovery_and_fencing(
            jobs, connection_string);
        test_failure_and_cancellation(
            jobs, connection_string);
        jobs.reset();
        test_projects_and_immutable_model_revisions(
            connection_string);
        drop_test_schema(connection_string);
        std::cout
            << "thermox PostgreSQL repository tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "thermox PostgreSQL job tests failed: "
            << error.what() << "\n";
        return 1;
    }
}
