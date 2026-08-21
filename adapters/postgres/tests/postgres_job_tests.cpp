#include "thermox/postgres/postgres_job_repository.hpp"
#include "thermox/postgres/postgres_project_repository.hpp"

#include "thermox/service/projects.hpp"

#include <libpq-fe.h>

#include <cmath>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <limits>
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
             "009_request_component_bundles.sql",
             "010_study_revisions.sql",
             "011_run_configurations_bind_studies.sql",
             "012_job_study_provenance.sql",
             "013_calibration_revisions.sql",
             "014_calibration_jobs.sql",
             "015_study_acceptance_criteria.sql",
             "016_performance_map_quality_reviews.sql",
             "017_study_artifact_qualifications.sql",
             "018_study_operating_envelopes.sql",
             "019_reconciliation_revisions.sql",
             "020_reconciliation_jobs.sql",
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
            {}, {}, {}, {}, {}, {}, {}, {},
        };
    value.steady_solver.max_iterations = 17;
    value.steady_solver.structural_decomposition_policy =
        thermox::service::StructuralDecompositionPolicy::blocks;
    value.transient_solver.end_time = 12.5;
    value.transient_solver.required_output_times = {1.25, 7.5};

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
    payload.output_constraints = {
        {"efficiency", 0.0, 1.0, false, true}};
    payload.curves = {
        {1.0, {{2.0, {0.91}}, {3.0, {0.92}}}},
    };
    map.map = payload;
    map.operating_envelope = {{
        "flow", "mass_flow", 2.0, 3.0, true, true,
    }};
    value.artifacts.performance_maps.push_back(std::move(map));
    thermox::service::CorrelationArtifactInput correlation;
    correlation.id = "bend-correlation";
    correlation.schema_version = "thermox.correlation/v2";
    correlation.revision = "vendor-2";
    correlation.checksum_sha256 = std::string(64, 'c');
    correlation.inputs = {
        {"mass_flow", "mass_flow"},
        {"density", "density"},
    };
    correlation.output = {"pressure_loss", "pressure"};
    correlation.candidates = {
        {"normal_flow", "normal-flow", 10,
         {{"coefficient", 1.5}},
         "coefficient * mass_flow * abs(mass_flow) / density",
         {{"mass_flow", 0.0, 20.0, true, false}}},
    };
    correlation.operating_envelope = {{
        "mass_flow", "mass_flow", 1.0, 5.0, true, true,
    }};
    value.artifacts.correlations.push_back(
        std::move(correlation));
    thermox::service::RegimeMapArtifactInput regime_map;
    regime_map.id = "flow-pattern-map";
    regime_map.schema_version = "thermox.regime_map/v2";
    regime_map.revision = "vendor-3";
    regime_map.checksum_sha256 = std::string(64, 'd');
    regime_map.inputs = {
        {"vapor_weber_number", "dimensionless"},
    };
    regime_map.regions = {
        {"annular", "annular", 10,
         {{"weber", 0,
           {{"vapor_weber_number", "dimensionless", 20.0,
             std::nullopt, false, true}}}}},
    };
    regime_map.operating_envelope = {{
        "vapor_weber_number", "dimensionless", 0.0, 50.0,
        true, true,
    }};
    value.artifacts.regime_maps.push_back(std::move(regime_map));
    value.artifacts.references.push_back({
        "fuel-spec",
        "thermox.material",
        "thermox.material/v1",
        "lab-3",
        std::string(64, 'b'),
    });
    thermox::service::ExpressionComponentInput component;
    component.schema_version = "thermox.expression_component/v3";
    component.kind = "custom.signal.persisted_gain";
    component.version = "1.0.0";
    component.template_kind = "custom.signal.gain";
    component.display_name = "Signal gain";
    component.category = "Project components";
    component.model_name = "Algebraic gain";
    component.supports_transient = true;
    component.ports = {
        {"input", "signal", "in", 1},
        {"output", "signal", "out", 1},
    };
    component.parameters = {
        {
            "gain", "dimensionless", true, std::nullopt,
            -std::numeric_limits<double>::infinity(),
            100.0, true, true,
        },
    };
    component.equations = {
        {
            "gain_law",
            "output.value - parameter.gain * input.value",
            1.0,
        },
    };
    component.internal_variables = {{
        "filtered", "differential", 0.0, 1.0, 0.0, 1.0,
        -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(), "dimensionless"}};
    component.transient_equations = {
        {"state_balance",
         "derivative.internal.filtered + internal.filtered - input.value",
         1.0},
        {"transient_output", "output.value - internal.filtered", 1.0},
    };
    value.components.expression_components.push_back(
        std::move(component));
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
            {}, {}, {}, {}, {}, {}, {}, {},
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
            repeated.request.steady_solver
                .structural_decomposition_policy ==
                thermox::service::StructuralDecompositionPolicy::blocks &&
            repeated.request.artifacts.performance_maps.size() == 1 &&
            repeated.request.artifacts.performance_maps.front().map
                    ->output_constraints.front().maximum == 1.0 &&
            repeated.request.artifacts.performance_maps.front()
                    .operating_envelope.front().minimum == 2.0 &&
            repeated.request.artifacts.correlations.size() == 1 &&
            repeated.request.artifacts.correlations.front()
                    .operating_envelope.front().maximum == 5.0 &&
            repeated.request.artifacts.correlations.front()
                    .candidates.size() == 1 &&
            repeated.request.artifacts.correlations.front()
                    .candidates.front().regime == "normal-flow" &&
            repeated.request.artifacts.correlations.front()
                    .candidates.front().coefficients.at("coefficient") ==
                1.5 &&
            repeated.request.artifacts.correlations.front()
                    .candidates.front().applicability.front().maximum ==
                20.0 &&
            !repeated.request.artifacts.correlations.front()
                    .candidates.front().applicability.front()
                    .maximum_inclusive &&
            repeated.request.artifacts.regime_maps.size() == 1 &&
            repeated.request.artifacts.regime_maps.front()
                    .operating_envelope.front().maximum == 50.0 &&
            repeated.request.artifacts.regime_maps.front()
                    .regions.front().regime == "annular" &&
            repeated.request.artifacts.regime_maps.front()
                    .regions.front().branches.front()
                    .criteria.front().minimum == 20.0 &&
            !repeated.request.artifacts.regime_maps.front()
                    .regions.front().branches.front().criteria.front()
                    .minimum_inclusive &&
            repeated.request.components.expression_components
                    .size() == 1 &&
            repeated.request.components.expression_components
                    .front().template_kind ==
                "custom.signal.gain" &&
            repeated.request.components.expression_components
                    .front().display_name == "Signal gain" &&
            repeated.request.components.expression_components
                    .front().model_name == "Algebraic gain" &&
            repeated.request.components.expression_components
                    .front().equations.front().expression ==
                "output.value - parameter.gain * input.value" &&
            repeated.request.components.expression_components
                    .front().supports_transient &&
            repeated.request.components.expression_components
                    .front().internal_variables.front().kind ==
                "differential" &&
            repeated.request.components.expression_components
                    .front().transient_equations.front().name ==
                "state_balance" &&
            repeated.request.transient_solver.required_output_times ==
                std::vector<double>{1.25, 7.5} &&
            std::isinf(
                repeated.request.components.expression_components
                    .front().parameters.front().lower_bound) &&
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
    first_request.source_revisions->study_revision_id =
        "study-postgres-a";
    first_request.source_revisions->study_checksum =
        "sha256:" + std::string(64, '5');
    const auto first = jobs->create_or_get(
        first_request, "history-fingerprint-a");
    auto second_request = request("team-a", "history-b");
    second_request.source_revisions
        ->run_configuration_revision_id = "run-postgres-b";
    second_request.source_revisions
        ->run_configuration_checksum =
        "sha256:" + std::string(64, '4');
    second_request.source_revisions->study_revision_id =
        "study-postgres-b";
    second_request.source_revisions->study_checksum =
        "sha256:" + std::string(64, '6');
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
            page.jobs.front().request.source_revisions &&
            page.jobs.front().request.source_revisions
                    ->study_revision_id ==
                "study-postgres-b" &&
            page.jobs.front().request.source_revisions
                    ->study_checksum ==
                "sha256:" + std::string(64, '6') &&
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
    auto reconciliation_request =
        request("team-a", "history-reconciliation");
    reconciliation_request.mode =
        thermox::service::SimulationJobMode::reconciliation;
    reconciliation_request.case_id.clear();
    reconciliation_request.reconciliation_id = "balance-postgres-a";
    reconciliation_request.source_revisions->case_revision_id.clear();
    reconciliation_request.source_revisions->case_checksum.clear();
    reconciliation_request.source_revisions
        ->reconciliation_revision_id = "reconciliation-postgres-a";
    reconciliation_request.source_revisions->reconciliation_checksum =
        "sha256:" + std::string(64, '7');
    const auto reconciliation_job = jobs->create_or_get(
        reconciliation_request, "history-fingerprint-reconciliation");
    query = {};
    query.reconciliation_revision_id = "reconciliation-postgres-a";
    const auto reconciliation_filtered = jobs->list("team-a", query);
    require(
        reconciliation_filtered.jobs.size() == 1U &&
            reconciliation_filtered.jobs.front().job_id ==
                reconciliation_job.job_id,
        "PostgreSQL reconciliation history must use immutable "
        "revision provenance");
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
    thermox::service::EngineeringAcceptanceResult criterion;
    criterion.criterion_id = "minimum_net_power";
    criterion.projection_id = "net_power";
    criterion.dimension = "power";
    criterion.actual_value_si = 42.0;
    criterion.lower_bound_si = 50.0;
    criterion.lower_margin_si = -8.0;
    criterion.limiting_margin_si = -8.0;
    criterion.limiting_bound = "lower";
    criterion.passed = false;
    summary.engineering_acceptance =
        thermox::service::EngineeringAcceptanceSummary{
            false, 0U, 1U, {criterion}};
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
            succeeded.result_summary->engineering_acceptance &&
            succeeded.result_summary->engineering_acceptance
                    ->criteria.front().lower_margin_si == -8.0 &&
            succeeded.result_summary->engineering_acceptance
                    ->criteria.front().limiting_margin_si == -8.0 &&
            succeeded.result_summary->engineering_acceptance
                    ->criteria.front().limiting_bound == "lower" &&
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
    const auto quality_review =
        projects.create_performance_map_quality_review({
            team_a,
            project.project_id,
            artifact.artifact_revision_id,
            {},
            thermox::service::EngineeringReviewDisposition::approved,
            "Corrected flow 70-120 kg/s and speed 250-400 rad/s",
            "Qualified for interpolation inside the measured envelope.",
        });
    const auto quality_reviews =
        projects.list_performance_map_quality_reviews(
            team_a, project.project_id,
            artifact.artifact_revision_id);
    require(
        quality_reviews.size() == 1U &&
            quality_review.artifact_checksum ==
                artifact.content.checksum &&
            quality_review.quality_snapshot_checksum.starts_with(
                "sha256:") &&
            projects.list_performance_map_quality_reviews(
                team_b, project.project_id,
                artifact.artifact_revision_id).empty(),
        "PostgreSQL quality reviews must preserve the exact artifact "
        "and assessed snapshot under Team isolation");

    thermox::service::CreateStudyRevisionRequest study_request;
    study_request.identity = team_a;
    study_request.project_id = project.project_id;
    study_request.study_id = "postgres-design-study";
    study_request.model_revision_id = first.model_revision_id;
    study_request.case_revision_id = first_case.case_revision_id;
    study_request.intent = first_case.mode;
    study_request.artifact_revision_ids = {
        artifact.artifact_revision_id,
    };
    study_request.artifact_qualification_requirements = {{
        artifact.artifact_revision_id,
        quality_review.review_id,
        {thermox::service::EngineeringReviewDisposition::approved},
    }};
    study_request.artifact_operating_envelopes = {{
        artifact.artifact_revision_id,
        {{
            "corrected_mass_flow", "mass_flow",
            70.0, 120.0, true, true,
        }},
    }};
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
    const auto study =
        projects.create_study_revision(study_request);
    const auto loaded_study = projects.get_study_revision(
        team_a, project.project_id, study.study_revision_id);
    require(
        loaded_study &&
            loaded_study->artifact_revision_ids ==
                study_request.artifact_revision_ids &&
            loaded_study->artifact_qualification_requirements.size() ==
                1U &&
            loaded_study->artifact_qualification_requirements.front()
                    .review_id == quality_review.review_id &&
            loaded_study->artifact_operating_envelopes.size() == 1U &&
            loaded_study->intent == "steady_state_design" &&
            projects
                    .list_study_revisions(
                        team_a, project.project_id)
                    .size() == 1U &&
            !projects
                 .get_study_revision(
                     team_b,
                     project.project_id,
                     study.study_revision_id)
                 .has_value(),
        "PostgreSQL studies must preserve exact bindings and "
        "Team isolation");

    thermox::service::CreateCalibrationRevisionRequest
        calibration_request;
    calibration_request.identity = team_a;
    calibration_request.project_id = project.project_id;
    calibration_request.calibration_id = "postgres-acceptance-fit";
    calibration_request.model_revision_id = first.model_revision_id;
    calibration_request.training_study_revision_ids = {
        study.study_revision_id,
    };
    calibration_request.definition_json = R"json({
      "schema_version": "thermox.calibration/v1",
      "calibration": {
        "id": "postgres-acceptance-fit",
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
        }]
      }
    })json";
    calibration_request.solver.max_iterations = 13;
    calibration_request.solver.transient_simulation_solver.end_time =
        42.0;
    calibration_request.solver.transient_simulation_solver.max_step =
        0.25;
    calibration_request.solver.transient_simulation_solver
        .required_output_times = {2.0, 8.0};
    const auto calibration =
        projects.create_calibration_revision(calibration_request);
    const auto loaded_calibration = projects.get_calibration_revision(
        team_a, project.project_id,
        calibration.calibration_revision_id);
    require(
        loaded_calibration &&
            loaded_calibration->training_study_revision_ids ==
                calibration_request.training_study_revision_ids &&
            loaded_calibration->solver.max_iterations == 13 &&
            loaded_calibration->solver
                    .transient_simulation_solver.end_time == 42.0 &&
            loaded_calibration->solver
                    .transient_simulation_solver.max_step == 0.25 &&
            loaded_calibration->solver.transient_simulation_solver
                    .required_output_times ==
                std::vector<double>{2.0, 8.0} &&
            projects.list_calibration_revisions(
                team_a, project.project_id).size() == 1U &&
            !projects.get_calibration_revision(
                team_b, project.project_id,
                calibration.calibration_revision_id),
        "PostgreSQL calibrations must preserve Study bindings, "
        "solver policy, and Team isolation");

    thermox::service::CreateReconciliationRevisionRequest
        reconciliation_request;
    reconciliation_request.identity = team_a;
    reconciliation_request.project_id = project.project_id;
    reconciliation_request.reconciliation_id =
        "postgres-power-reconciliation";
    reconciliation_request.model_revision_id = first.model_revision_id;
    reconciliation_request.constraint_study_revision_ids = {
        study.study_revision_id,
    };
    reconciliation_request.definition_json = R"json({
      "schema_version":"thermox.calibration/v1",
      "calibration":{
        "id":"postgres-power-reconciliation",
        "parameters":[{
          "id":"efficiency","scope":"component",
          "targets":["components.compressor.parameters.eta_is"],
          "cases":["design"],
          "bounds":{"lower":0.75,"upper":0.95}
        }],
        "observations":[{
          "id":"required-power","case":"design",
          "target":"compressor.shaft.W_dot",
          "measured":{"value":36.229874174599141,"unit":"MW"},
          "sigma":{"value":0.1,"unit":"MW"}
        }]
      }
    })json";
    reconciliation_request.solver.max_iterations = 9;
    reconciliation_request.profile_likelihood.enabled = true;
    reconciliation_request.profile_likelihood.parameter_ids = {
        "efficiency",
    };
    reconciliation_request.mode = thermox::service::
        ReconciliationMode::weighted_measurements;
    reconciliation_request.joint_confidence_region.enabled = true;
    reconciliation_request.joint_confidence_region.objective_increase =
        1.0;
    reconciliation_request.joint_confidence_region.parameter_ids = {
        "efficiency",
    };
    const auto reconciliation =
        projects.create_reconciliation_revision(
            reconciliation_request);
    const auto loaded_reconciliation =
        projects.get_reconciliation_revision(
            team_a, project.project_id,
            reconciliation.reconciliation_revision_id);
    const auto resolved_reconciliation =
        projects.resolve_reconciliation(
            team_a, project.project_id,
            reconciliation.reconciliation_revision_id);
    require(
        loaded_reconciliation && resolved_reconciliation &&
            loaded_reconciliation->constraint_study_revision_ids ==
                reconciliation_request.constraint_study_revision_ids &&
            loaded_reconciliation->solver.max_iterations == 9 &&
            loaded_reconciliation->profile_likelihood.enabled &&
            loaded_reconciliation->joint_confidence_region.enabled &&
            loaded_reconciliation->joint_confidence_region.parameter_ids ==
                std::vector<std::string>({"efficiency"}) &&
            projects.list_reconciliation_revisions(
                team_a, project.project_id).size() == 1U &&
            !projects.get_reconciliation_revision(
                team_b, project.project_id,
                reconciliation.reconciliation_revision_id),
        "PostgreSQL reconciliations must preserve Study bindings, "
        "solver/profile policy, composition, and Team isolation");

    thermox::service::CreateRunConfigurationRevisionRequest
        run_request;
    run_request.identity = team_a;
    run_request.project_id = project.project_id;
    run_request.run_configuration_id = "postgres-design-run";
    run_request.study_revision_id = study.study_revision_id;
    run_request.steady_solver.max_iterations = 41;
    run_request.steady_solver.structural_decomposition_policy =
        thermox::service::StructuralDecompositionPolicy::blocks;
    run_request.transient_solver.required_output_times = {0.2, 0.8};
    const auto run =
        projects.create_run_configuration_revision(run_request);
    const auto loaded =
        projects.get_run_configuration_revision(
            team_a,
            project.project_id,
            run.run_configuration_revision_id);
    require(
        loaded &&
            loaded->study_revision_id == study.study_revision_id &&
            loaded->steady_solver.max_iterations == 41 &&
            loaded->steady_solver
                .structural_decomposition_policy ==
                thermox::service::StructuralDecompositionPolicy::blocks &&
            loaded->transient_solver.required_output_times ==
                std::vector<double>{0.2, 0.8} &&
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
