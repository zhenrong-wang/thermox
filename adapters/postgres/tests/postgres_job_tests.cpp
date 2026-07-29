#include "thermox/postgres/postgres_job_repository.hpp"

#include <libpq-fe.h>

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

    std::ifstream migration(
        std::string(THERMOX_SOURCE_DIR) +
        "/adapters/postgres/migrations/"
        "001_simulation_jobs.sql");
    require(
        static_cast<bool>(migration),
        "could not read the PostgreSQL test migration");
    std::ostringstream sql;
    sql << migration.rdbuf();
    execute_sql(
        connection.get(), sql.str(), "apply the test migration");
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
            repeated.request.artifacts.performance_maps.size() == 1,
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
            !claimed.worker_id.empty(),
        "claim must atomically publish a running revision");

    const thermox::service::ResultArtifactManifest manifest{
        "artifact-a",
        "application/json",
        thermox::service::result_schema_v3,
        42,
        "sha256:test",
    };
    const auto succeeded = jobs->publish_success(
        claimed.job_id, claimed.revision, execution(), manifest);
    require(
        succeeded.state == SimulationJobState::succeeded &&
            succeeded.revision == 3 &&
            succeeded.execution.has_value() &&
            succeeded.execution->components.size() == 1 &&
            succeeded.result_artifact.has_value() &&
            succeeded.result_artifact->byte_size == 42,
        "success publication must preserve provenance and "
        "artifact metadata");

    bool conflict = false;
    try {
        (void)jobs->publish_success(
            claimed.job_id,
            claimed.revision,
            execution(),
            manifest);
    } catch (const thermox::service::JobConflictError&) {
        conflict = true;
    }
    require(
        conflict,
        "terminal publication must reject a stale revision");
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
        test_atomic_claim_and_terminal_publication(
            jobs, connection_string);
        test_failure_and_cancellation(
            jobs, connection_string);
        jobs.reset();
        drop_test_schema(connection_string);
        std::cout << "thermox PostgreSQL job tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "thermox PostgreSQL job tests failed: "
            << error.what() << "\n";
        return 1;
    }
}
