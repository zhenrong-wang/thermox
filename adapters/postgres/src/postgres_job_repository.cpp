#include "thermox/postgres/postgres_job_repository.hpp"

#include "job_codec.hpp"

#include <libpq-fe.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace thermox::postgres {

namespace {

using service::SimulationJobRecord;
using service::SimulationJobState;

struct ConnectionDeleter {
    void operator()(PGconn* connection) const {
        if (connection != nullptr) {
            PQfinish(connection);
        }
    }
};

struct ResultDeleter {
    void operator()(PGresult* result) const {
        if (result != nullptr) {
            PQclear(result);
        }
    }
};

using Connection = std::unique_ptr<PGconn, ConnectionDeleter>;
using Result = std::unique_ptr<PGresult, ResultDeleter>;

Connection connect(const std::string& connection_string) {
    Connection connection{
        PQconnectdb(connection_string.c_str())};
    if (!connection ||
        PQstatus(connection.get()) != CONNECTION_OK) {
        const std::string message = connection
            ? PQerrorMessage(connection.get())
            : "allocation failed";
        throw std::runtime_error(
            "could not connect to PostgreSQL: " + message);
    }
    return connection;
}

Result execute(
    PGconn* connection,
    const char* sql,
    const std::vector<const char*>& values,
    ExecStatusType expected = PGRES_TUPLES_OK) {
    Result result{PQexecParams(
        connection,
        sql,
        static_cast<int>(values.size()),
        nullptr,
        values.empty() ? nullptr : values.data(),
        nullptr,
        nullptr,
        0)};
    if (!result || PQresultStatus(result.get()) != expected) {
        const std::string message = result
            ? PQresultErrorMessage(result.get())
            : PQerrorMessage(connection);
        throw std::runtime_error(
            "PostgreSQL job repository query failed: " +
            message);
    }
    return result;
}

std::string field(
    const PGresult* result,
    int row,
    int column) {
    if (PQgetisnull(result, row, column)) {
        throw std::runtime_error(
            "PostgreSQL job row contains an unexpected null");
    }
    return PQgetvalue(result, row, column);
}

std::optional<std::string> optional_field(
    const PGresult* result,
    int row,
    int column) {
    if (PQgetisnull(result, row, column)) {
        return std::nullopt;
    }
    return std::string(PQgetvalue(result, row, column));
}

SimulationJobState decode_state(const std::string& state) {
    if (state == "queued") {
        return SimulationJobState::queued;
    }
    if (state == "running") {
        return SimulationJobState::running;
    }
    if (state == "succeeded") {
        return SimulationJobState::succeeded;
    }
    if (state == "failed") {
        return SimulationJobState::failed;
    }
    if (state == "cancelled") {
        return SimulationJobState::cancelled;
    }
    throw std::runtime_error(
        "invalid persisted simulation job state: " + state);
}

SimulationJobRecord decode_record(
    const PGresult* result,
    int row = 0) {
    SimulationJobRecord record;
    record.job_id = field(result, row, 0);
    record.team_id = field(result, row, 1);
    record.submitted_by_user_id = field(result, row, 2);
    record.revision =
        std::stoull(field(result, row, 3));
    record.state =
        decode_state(field(result, row, 4));
    record.request =
        detail::decode_request(field(result, row, 5));
    record.schema_version = record.request.schema_version;
    if (record.request.identity.team_id != record.team_id ||
        record.request.identity.user_id !=
            record.submitted_by_user_id) {
        throw std::runtime_error(
            "persisted simulation job ownership does not match "
            "its immutable request");
    }
    record.request_fingerprint = field(result, row, 6);
    record.worker_id =
        optional_field(result, row, 7).value_or("");
    if (const auto payload = optional_field(result, row, 8)) {
        record.execution =
            detail::decode_execution(*payload);
    }
    if (const auto payload = optional_field(result, row, 9)) {
        record.error = detail::decode_error(*payload);
    }
    if (const auto payload = optional_field(result, row, 10)) {
        record.result_artifact =
            detail::decode_result_artifact(*payload);
    }
    return record;
}

void diagnose_terminal_update(
    PGconn* connection,
    const std::string& job_id,
    std::uint64_t expected_revision) {
    const auto result = execute(
        connection,
        "SELECT revision, state "
        "FROM thermox_simulation_jobs WHERE job_id = $1",
        {job_id.c_str()});
    if (PQntuples(result.get()) == 0) {
        throw service::JobStateError(
            "simulation job does not exist");
    }
    const auto actual_revision =
        std::stoull(field(result.get(), 0, 0));
    if (actual_revision != expected_revision) {
        throw service::JobConflictError(
            "simulation job revision conflict");
    }
    throw service::JobStateError(
        "terminal result may only be published for a "
        "running job");
}

class PostgresJobRepository final
    : public service::SimulationJobRepository {
public:
    explicit PostgresJobRepository(
        std::string connection_string)
        : connection_string_(std::move(connection_string)) {
        if (connection_string_.empty()) {
            throw std::invalid_argument(
                "PostgreSQL connection string must not be empty");
        }
        auto connection = connect(connection_string_);
        const auto result = execute(
            connection.get(),
            "SELECT to_regclass("
            "'thermox_simulation_jobs')::text",
            {});
        if (PQgetisnull(result.get(), 0, 0)) {
            throw std::runtime_error(
                "PostgreSQL Thermox schema is not installed; "
                "apply migrations before creating the repository");
        }
    }

    SimulationJobRecord create_or_get(
        const service::SimulationJobRequest& request,
        const std::string& request_fingerprint) override {
        auto connection = connect(connection_string_);
        const auto payload = detail::encode_request(request);
        const auto inserted = execute(
            connection.get(),
            "INSERT INTO thermox_simulation_jobs ("
            "team_id, submitted_by_user_id, idempotency_key, "
            "request_fingerprint, request_payload"
            ") VALUES ($1, $2, $3, $4, $5::jsonb) "
            "ON CONFLICT (team_id, idempotency_key) DO NOTHING "
            "RETURNING "
            "job_id, team_id, submitted_by_user_id, revision, "
            "state, request_payload::text, request_fingerprint, "
            "worker_id, execution_payload::text, "
            "error_payload::text, result_artifact_payload::text",
            {
                request.identity.team_id.c_str(),
                request.identity.user_id.c_str(),
                request.idempotency_key.c_str(),
                request_fingerprint.c_str(),
                payload.c_str(),
            });
        if (PQntuples(inserted.get()) == 1) {
            return decode_record(inserted.get());
        }

        const auto existing = execute(
            connection.get(),
            "SELECT "
            "job_id, team_id, submitted_by_user_id, revision, "
            "state, request_payload::text, request_fingerprint, "
            "worker_id, execution_payload::text, "
            "error_payload::text, result_artifact_payload::text "
            "FROM thermox_simulation_jobs "
            "WHERE team_id = $1 AND idempotency_key = $2",
            {
                request.identity.team_id.c_str(),
                request.idempotency_key.c_str(),
            });
        if (PQntuples(existing.get()) != 1) {
            throw std::runtime_error(
                "idempotent simulation job disappeared");
        }
        auto record = decode_record(existing.get());
        if (record.request_fingerprint != request_fingerprint) {
            throw service::JobConflictError(
                "idempotency key is already bound to a "
                "different simulation request");
        }
        return record;
    }

    std::optional<SimulationJobRecord> get(
        const std::string& team_id,
        const std::string& job_id) const override {
        auto connection = connect(connection_string_);
        const auto result = execute(
            connection.get(),
            "SELECT "
            "job_id, team_id, submitted_by_user_id, revision, "
            "state, request_payload::text, request_fingerprint, "
            "worker_id, execution_payload::text, "
            "error_payload::text, result_artifact_payload::text "
            "FROM thermox_simulation_jobs "
            "WHERE team_id = $1 AND job_id = $2",
            {team_id.c_str(), job_id.c_str()});
        if (PQntuples(result.get()) == 0) {
            return std::nullopt;
        }
        return decode_record(result.get());
    }

    std::optional<SimulationJobRecord> claim_next(
        const std::string& worker_id) override {
        auto connection = connect(connection_string_);
        const auto result = execute(
            connection.get(),
            "WITH candidate AS ("
            "  SELECT job_id FROM thermox_simulation_jobs "
            "  WHERE state = 'queued' "
            "  ORDER BY queue_sequence "
            "  FOR UPDATE SKIP LOCKED LIMIT 1"
            ") "
            "UPDATE thermox_simulation_jobs AS jobs "
            "SET state = 'running', worker_id = $1, "
            "revision = jobs.revision + 1, "
            "updated_at = clock_timestamp() "
            "FROM candidate "
            "WHERE jobs.job_id = candidate.job_id "
            "RETURNING "
            "jobs.job_id, jobs.team_id, "
            "jobs.submitted_by_user_id, jobs.revision, "
            "jobs.state, jobs.request_payload::text, "
            "jobs.request_fingerprint, jobs.worker_id, "
            "jobs.execution_payload::text, "
            "jobs.error_payload::text, "
            "jobs.result_artifact_payload::text",
            {worker_id.c_str()});
        if (PQntuples(result.get()) == 0) {
            return std::nullopt;
        }
        return decode_record(result.get());
    }

    SimulationJobRecord publish_success(
        const std::string& job_id,
        std::uint64_t expected_revision,
        const service::ExecutionMetadata& execution,
        const service::ResultArtifactManifest&
            result_artifact) override {
        if (result_artifact.artifact_id.empty()) {
            throw service::JobStateError(
                "cannot publish success without a result artifact");
        }
        auto connection = connect(connection_string_);
        const std::string revision =
            std::to_string(expected_revision);
        const auto execution_payload =
            detail::encode_execution(execution);
        const auto artifact_payload =
            detail::encode_result_artifact(result_artifact);
        const auto result = execute(
            connection.get(),
            "UPDATE thermox_simulation_jobs "
            "SET state = 'succeeded', revision = revision + 1, "
            "execution_payload = $3::jsonb, "
            "result_artifact_payload = $4::jsonb, "
            "error_payload = NULL, "
            "updated_at = clock_timestamp() "
            "WHERE job_id = $1 AND revision = $2::bigint "
            "AND state = 'running' "
            "RETURNING "
            "job_id, team_id, submitted_by_user_id, revision, "
            "state, request_payload::text, request_fingerprint, "
            "worker_id, execution_payload::text, "
            "error_payload::text, result_artifact_payload::text",
            {
                job_id.c_str(),
                revision.c_str(),
                execution_payload.c_str(),
                artifact_payload.c_str(),
            });
        if (PQntuples(result.get()) == 0) {
            diagnose_terminal_update(
                connection.get(), job_id, expected_revision);
        }
        return decode_record(result.get());
    }

    SimulationJobRecord publish_failure(
        const std::string& job_id,
        std::uint64_t expected_revision,
        const service::ServiceError& error,
        const std::optional<service::ExecutionMetadata>&
            execution) override {
        auto connection = connect(connection_string_);
        const std::string revision =
            std::to_string(expected_revision);
        const auto error_payload = detail::encode_error(error);
        const auto execution_payload = execution
            ? detail::encode_execution(*execution)
            : std::string{};
        const char* encoded_execution = execution
            ? execution_payload.c_str()
            : nullptr;
        const auto result = execute(
            connection.get(),
            "UPDATE thermox_simulation_jobs "
            "SET state = 'failed', revision = revision + 1, "
            "execution_payload = $3::jsonb, "
            "error_payload = $4::jsonb, "
            "result_artifact_payload = NULL, "
            "updated_at = clock_timestamp() "
            "WHERE job_id = $1 AND revision = $2::bigint "
            "AND state = 'running' "
            "RETURNING "
            "job_id, team_id, submitted_by_user_id, revision, "
            "state, request_payload::text, request_fingerprint, "
            "worker_id, execution_payload::text, "
            "error_payload::text, result_artifact_payload::text",
            {
                job_id.c_str(),
                revision.c_str(),
                encoded_execution,
                error_payload.c_str(),
            });
        if (PQntuples(result.get()) == 0) {
            diagnose_terminal_update(
                connection.get(), job_id, expected_revision);
        }
        return decode_record(result.get());
    }

    SimulationJobRecord cancel(
        const std::string& team_id,
        const std::string& job_id,
        std::uint64_t expected_revision) override {
        auto connection = connect(connection_string_);
        const std::string revision =
            std::to_string(expected_revision);
        const auto result = execute(
            connection.get(),
            "UPDATE thermox_simulation_jobs "
            "SET state = 'cancelled', revision = revision + 1, "
            "updated_at = clock_timestamp() "
            "WHERE team_id = $1 AND job_id = $2 "
            "AND revision = $3::bigint AND state = 'queued' "
            "RETURNING "
            "job_id, team_id, submitted_by_user_id, revision, "
            "state, request_payload::text, request_fingerprint, "
            "worker_id, execution_payload::text, "
            "error_payload::text, result_artifact_payload::text",
            {
                team_id.c_str(),
                job_id.c_str(),
                revision.c_str(),
            });
        if (PQntuples(result.get()) == 1) {
            return decode_record(result.get());
        }

        const auto current = execute(
            connection.get(),
            "SELECT revision, state "
            "FROM thermox_simulation_jobs "
            "WHERE team_id = $1 AND job_id = $2",
            {team_id.c_str(), job_id.c_str()});
        if (PQntuples(current.get()) == 0) {
            throw service::JobStateError(
                "simulation job does not exist");
        }
        if (std::stoull(field(current.get(), 0, 0)) !=
            expected_revision) {
            throw service::JobConflictError(
                "simulation job revision conflict");
        }
        throw service::JobStateError(
            "only queued jobs may be cancelled");
    }

private:
    std::string connection_string_;
};

}  // namespace

std::shared_ptr<service::SimulationJobRepository>
make_postgres_job_repository(std::string connection_string) {
    return std::make_shared<PostgresJobRepository>(
        std::move(connection_string));
}

}  // namespace thermox::postgres
