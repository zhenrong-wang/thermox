#include "thermox/service/in_memory_jobs.hpp"
#include "thermox/service/in_memory_artifacts.hpp"
#include "thermox/service/serialization.hpp"
#include "thermox/service/simulation_jobs.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

const thermox::service::IdentityContext team_a{
    "user-a", "team-a", "request-test"};
const thermox::service::IdentityContext team_b{
    "user-b", "team-b", "request-test"};

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

thermox::service::SimulationJobRequest steady_request(
    std::string idempotency_key) {
    thermox::service::SimulationJobRequest request;
    request.identity = team_a;
    request.idempotency_key = std::move(idempotency_key);
    request.model_json =
        read_source_file("core/examples/air_compressor.json");
    return request;
}

thermox::service::PerformanceMapArtifactInput unused_test_map() {
    thermox::service::PerformanceMapArtifactInput artifact;
    artifact.id = "job-map";
    artifact.schema_version = "thermox.performance_map/v1";
    artifact.revision = "job-test-1";
    artifact.checksum_sha256 = std::string(64, 'b');
    thermox::service::PerformanceMapPayloadInput map;
    map.primary_variable = {"flow", "mass_flow"};
    map.family_variable = {"speed", "angular_speed"};
    map.output_variables = {{"efficiency", "dimensionless"}};
    map.curves = {
        {1.0, {{1.0, {0.8}}, {2.0, {0.9}}}},
        {2.0, {{1.0, {0.82}}, {2.0, {0.92}}}},
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

void test_submission_is_idempotent_and_conflict_safe() {
    auto jobs =
        thermox::service::make_in_memory_job_repository();
    auto artifacts =
        thermox::service::make_in_memory_result_artifact_store();
    thermox::service::SimulationJobService service(jobs, artifacts);

    auto request = steady_request("submission-1");
    request.artifacts.performance_maps.push_back(
        unused_test_map());
    const auto first = service.submit(request);
    const auto repeated = service.submit(request);
    require(
        first.job_id == repeated.job_id &&
            first.revision == repeated.revision,
        "an identical idempotent submission must return the "
        "existing job");
    require(
        first.state ==
                thermox::service::SimulationJobState::queued &&
            first.revision == 1,
        "a new job must start queued at revision one");

    auto changed = request;
    changed.artifacts.performance_maps.front()
        .map->curves.front().samples.front().outputs.front() = 0.7;
    bool conflict = false;
    try {
        (void)service.submit(changed);
    } catch (const thermox::service::JobConflictError&) {
        conflict = true;
    }
    require(
        conflict,
        "artifact payload must participate in the idempotency "
        "fingerprint and reusing its key "
        "must fail");

    auto referenced = steady_request("reference-submission");
    referenced.artifacts.references.push_back(
        map_reference(unused_test_map()));
    (void)service.submit(referenced);
    referenced.artifacts.references.front().checksum_sha256 =
        std::string(64, 'c');
    conflict = false;
    try {
        (void)service.submit(referenced);
    } catch (const thermox::service::JobConflictError&) {
        conflict = true;
    }
    require(
        conflict,
        "artifact references must participate in the job "
        "idempotency fingerprint");
}

void test_success_publishes_a_readable_artifact() {
    auto jobs =
        thermox::service::make_in_memory_job_repository();
    auto artifacts =
        thermox::service::make_in_memory_result_artifact_store();
    const auto engineering_artifact = unused_test_map();
    const auto engineering_resolver =
        thermox::service::
            make_in_memory_engineering_artifact_resolver(
                {engineering_artifact});
    thermox::service::SimulationJobService service(
        thermox::service::make_default_simulation_runtime(),
        engineering_resolver,
        jobs,
        artifacts);

    auto request = steady_request("successful-run");
    request.artifacts.references.push_back(
        map_reference(engineering_artifact));
    const auto queued = service.submit(request);
    bool unavailable = false;
    try {
        (void)service.get_result(team_a, queued.job_id);
    } catch (const thermox::service::JobStateError&) {
        unavailable = true;
    }
    require(
        unavailable,
        "queued jobs must not expose a result");

    const auto completed = service.run_next("worker-a");
    require(completed.has_value(), "worker must claim queued job");
    require(
        completed->job_id == queued.job_id &&
            completed->state ==
                thermox::service::SimulationJobState::succeeded &&
            completed->revision == 3,
        "successful execution must atomically publish a terminal "
        "revision");
    require(
        completed->worker_id == "worker-a" &&
            completed->execution.has_value() &&
            completed->result_artifact.has_value() &&
            !completed->error.has_value(),
        "successful job must retain worker, provenance, and "
        "artifact metadata");
    require(
        completed->execution->artifacts.size() == 1 &&
            completed->execution->artifacts.front().id == "job-map",
        "workers must propagate request artifacts and provenance");

    const auto& manifest = *completed->result_artifact;
    require(
        manifest.schema_version ==
                thermox::service::result_schema_v3 &&
            manifest.media_type == "application/json" &&
            manifest.byte_size > 0 &&
            manifest.checksum.starts_with("fnv1a64:"),
        "artifact manifest must be versioned and checksummed");
    const auto result =
        service.get_result(team_a, completed->job_id);
    require(
        result.has_value() &&
            result->manifest.artifact_id ==
                manifest.artifact_id &&
            result->content.size() == manifest.byte_size &&
            result->content.find("\"schema_version\": "
                                 "\"thermox.result/v3\"") !=
                std::string::npos,
        "the application service must retrieve a published "
        "artifact through its manifest");
    require(
        !service.get_result(team_a, "missing-job").has_value(),
        "result retrieval must distinguish a missing job");

    const auto json =
        thermox::service::serialize_job_record_json(*completed);
    require(
        json.find("\"schema_version\": "
                  "\"thermox.job/v2\"") != std::string::npos &&
            json.find("\"state\": \"succeeded\"") !=
                std::string::npos &&
            json.find("\"result_artifact\": {") !=
                std::string::npos &&
            json.find("\"execution\": {") !=
                std::string::npos,
        "succeeded job JSON must expose state, provenance, and "
        "the result manifest");
    require(
        json.find(request.model_json) == std::string::npos &&
            json.find(request.idempotency_key) ==
                std::string::npos,
        "job status JSON must not echo the model body or "
        "idempotency key");
    require(
        !service.run_next("worker-a").has_value(),
        "completed jobs must not be claimed again");
}

void test_solver_failure_is_a_terminal_job_failure() {
    auto jobs =
        thermox::service::make_in_memory_job_repository();
    auto artifacts =
        thermox::service::make_in_memory_result_artifact_store();
    thermox::service::SimulationJobService service(jobs, artifacts);

    auto request = steady_request("failed-run");
    request.model_json = "{not valid JSON";
    const auto queued = service.submit(request);
    const auto completed = service.run_next("worker-b");
    require(
        completed.has_value() &&
            completed->job_id == queued.job_id &&
            completed->state ==
                thermox::service::SimulationJobState::failed &&
            completed->error.has_value() &&
            !completed->result_artifact.has_value(),
        "simulation errors must publish failure without an "
        "artifact");
    require(
        completed->error->code == "invalid_model" &&
            completed->error->stage == "validation",
        "job failure must preserve the simulation error");
    const auto json =
        thermox::service::serialize_job_record_json(*completed);
    require(
        json.find("\"state\": \"failed\"") !=
                std::string::npos &&
            json.find("\"code\": \"invalid_model\"") !=
                std::string::npos &&
            json.find("\"result_artifact\": null") !=
                std::string::npos,
        "failed job JSON must expose its structured error "
        "without a result manifest");
}

void test_transient_jobs_use_the_same_artifact_boundary() {
    auto jobs =
        thermox::service::make_in_memory_job_repository();
    auto artifacts =
        thermox::service::make_in_memory_result_artifact_store();
    thermox::service::SimulationJobService service(jobs, artifacts);

    thermox::service::SimulationJobRequest request;
    request.identity = team_a;
    request.idempotency_key = "transient-run";
    request.mode = thermox::service::SimulationJobMode::transient;
    request.model_json = read_source_file(
        "core/examples/lumped_thermal_storage.json");
    request.case_id = "charge";
    request.transient_solver.end_time = 0.2;
    request.transient_solver.max_step = 0.05;
    (void)service.submit(request);

    const auto completed = service.run_next("worker-transient");
    require(
        completed.has_value() &&
            completed->state ==
                thermox::service::SimulationJobState::succeeded &&
            completed->execution.has_value() &&
            completed->execution->operation == "transient" &&
            completed->result_artifact.has_value(),
        "transient execution must publish through the common job "
        "contract");
    const auto content = artifacts->get(
        *completed->result_artifact);
    require(
        content.has_value() &&
            content->find("\"trajectory\": [") !=
                std::string::npos,
        "transient job artifact must retain its trajectory");
}

void test_cancel_and_optimistic_revision_rules() {
    auto jobs =
        thermox::service::make_in_memory_job_repository();
    auto artifacts =
        thermox::service::make_in_memory_result_artifact_store();
    thermox::service::SimulationJobService service(jobs, artifacts);

    const auto queued =
        service.submit(steady_request("cancelled-run"));
    bool conflict = false;
    try {
        (void)service.cancel(
            team_a, queued.job_id, queued.revision + 1);
    } catch (const thermox::service::JobConflictError&) {
        conflict = true;
    }
    require(conflict, "stale revisions must be rejected");

    const auto cancelled =
        service.cancel(team_a, queued.job_id, queued.revision);
    require(
        cancelled.state ==
                thermox::service::SimulationJobState::cancelled &&
            cancelled.revision == queued.revision + 1 &&
            thermox::service::is_terminal(cancelled.state),
        "queued jobs must support an optimistic cancellation");
    require(
        !service.run_next("worker-c").has_value(),
        "cancelled jobs must not be claimable");
}

void test_claim_is_atomic() {
    auto jobs =
        thermox::service::make_in_memory_job_repository();
    auto artifacts =
        thermox::service::make_in_memory_result_artifact_store();
    thermox::service::SimulationJobService service(jobs, artifacts);
    (void)service.submit(steady_request("atomic-claim"));

    std::optional<thermox::service::SimulationJobRecord> first;
    std::optional<thermox::service::SimulationJobRecord> second;
    std::thread one([&] { first = jobs->claim_next("worker-one"); });
    std::thread two([&] { second = jobs->claim_next("worker-two"); });
    one.join();
    two.join();
    require(
        first.has_value() != second.has_value(),
        "exactly one concurrent worker may claim a queued job");
}

void test_request_validation() {
    auto jobs =
        thermox::service::make_in_memory_job_repository();
    auto artifacts =
        thermox::service::make_in_memory_result_artifact_store();
    thermox::service::SimulationJobService service(jobs, artifacts);
    auto request = steady_request("");
    bool rejected = false;
    try {
        (void)service.submit(request);
    } catch (const thermox::service::JobRequestError&) {
        rejected = true;
    }
    require(
        rejected,
        "job submission must require an idempotency key");

    request = steady_request("missing-identity");
    request.identity = {};
    rejected = false;
    try {
        (void)service.submit(request);
    } catch (const thermox::service::JobRequestError&) {
        rejected = true;
    }
    require(
        rejected,
        "job submission must require a trusted identity scope");
}

void test_team_scope_isolation() {
    auto jobs =
        thermox::service::make_in_memory_job_repository();
    auto artifacts =
        thermox::service::make_in_memory_result_artifact_store();
    thermox::service::SimulationJobService service(jobs, artifacts);

    const auto owned =
        service.submit(steady_request("shared-key"));
    auto other_request = steady_request("shared-key");
    other_request.identity = team_b;
    const auto other = service.submit(other_request);
    require(
        owned.job_id != other.job_id,
        "idempotency keys must be scoped by team");
    require(
        !service.get(team_b, owned.job_id).has_value() &&
            service.get(team_a, owned.job_id).has_value(),
        "cross-team job lookup must not reveal existence");

    bool hidden = false;
    try {
        (void)service.cancel(
            team_b, owned.job_id, owned.revision);
    } catch (const thermox::service::JobStateError&) {
        hidden = true;
    }
    require(
        hidden,
        "cross-team cancellation must be rejected as not found");
}

}  // namespace

int main() {
    try {
        test_submission_is_idempotent_and_conflict_safe();
        test_success_publishes_a_readable_artifact();
        test_solver_failure_is_a_terminal_job_failure();
        test_transient_jobs_use_the_same_artifact_boundary();
        test_cancel_and_optimistic_revision_rules();
        test_claim_is_atomic();
        test_request_validation();
        test_team_scope_isolation();
        std::cout << "thermox job service tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "thermox job service tests failed: "
                  << error.what() << "\n";
        return 1;
    }
}
