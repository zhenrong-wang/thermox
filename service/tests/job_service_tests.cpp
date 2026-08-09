#include "thermox/service/in_memory_jobs.hpp"
#include "thermox/service/in_memory_artifacts.hpp"
#include "thermox/service/serialization.hpp"
#include "thermox/service/simulation_jobs.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
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
    request.source_revisions =
        thermox::service::RevisionProvenance{
            "project-job-test",
            "model-revision-job-test",
            "sha256:" + std::string(64, '1'),
            "case-revision-job-test",
            "sha256:" + std::string(64, '2'),
        };
    return request;
}

thermox::service::SimulationJobRequest expression_request(
    std::string idempotency_key) {
    auto request = steady_request(std::move(idempotency_key));
    request.model_json = R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "job_expression",
    "media": [],
    "components": [{
      "id": "gain",
      "kind": "custom.signal.job_gain",
      "parameters": {"gain": 2.0}
    }],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {"gain.input.value": 5.0}
  }]
})json";
    request.case_id = "design";
    thermox::service::ExpressionComponentInput component;
    component.kind = "custom.signal.job_gain";
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
    request.components.expression_components.push_back(
        std::move(component));
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

thermox::service::CorrelationArtifactInput unused_correlation_family() {
    thermox::service::CorrelationArtifactInput artifact;
    artifact.id = "job-correlation-family";
    artifact.schema_version = "thermox.correlation/v2";
    artifact.revision = "job-family-1";
    artifact.checksum_sha256 = std::string(64, 'd');
    artifact.inputs = {{"x", "dimensionless"}};
    artifact.output = {"y", "dimensionless"};
    artifact.candidates = {
        {"normal", "normal", 10, {{"factor", 1.0}},
         "factor * x", {{"x", 0.0, 1.0, true, true}}},
    };
    return artifact;
}

thermox::service::RegimeMapArtifactInput unused_regime_map() {
    thermox::service::RegimeMapArtifactInput artifact;
    artifact.id = "job-regime-map";
    artifact.schema_version = "thermox.regime_map/v2";
    artifact.revision = "job-regime-1";
    artifact.checksum_sha256 = std::string(64, 'e');
    artifact.inputs = {{"vapor_weber_number", "dimensionless"}};
    artifact.regions = {
        {"low", "stratified", 10,
         {{"weber", 0,
           {{"vapor_weber_number", "dimensionless", std::nullopt,
             20.0, true, true}}}}},
        {"high", "annular", 10,
         {{"weber", 0,
           {{"vapor_weber_number", "dimensionless", 20.0,
             std::nullopt, false, true}}}}},
    };
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
    const auto engineering_artifact = unused_test_map();
    auto alternate_artifact = engineering_artifact;
    alternate_artifact.id = "job-map-alternate";
    alternate_artifact.checksum_sha256 = std::string(64, 'c');
    const auto engineering_resolver = thermox::service::
        make_in_memory_engineering_artifact_resolver(
            {engineering_artifact, alternate_artifact});
    thermox::service::SimulationJobService service(
        thermox::service::make_default_simulation_runtime(),
        engineering_resolver, jobs, artifacts);

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

    auto constrained = steady_request("map-constraint-submission");
    constrained.artifacts.performance_maps.push_back(
        unused_test_map());
    constrained.artifacts.performance_maps.front()
        .map->output_constraints.push_back(
            {"efficiency", 0.0, 1.0, false, true});
    (void)service.submit(constrained);
    constrained.artifacts.performance_maps.front()
        .map->output_constraints.front().maximum = 0.95;
    conflict = false;
    try {
        (void)service.submit(constrained);
    } catch (const thermox::service::JobConflictError&) {
        conflict = true;
    }
    require(
        conflict,
        "declared map output constraints must participate in the job "
        "idempotency fingerprint");

    auto family_request = steady_request("correlation-family-submission");
    family_request.artifacts.correlations.push_back(
        unused_correlation_family());
    (void)service.submit(family_request);
    family_request.artifacts.correlations.front()
        .candidates.front().priority = 11;
    conflict = false;
    try {
        (void)service.submit(family_request);
    } catch (const thermox::service::JobConflictError&) {
        conflict = true;
    }
    require(
        conflict,
        "correlation candidates and selection metadata must "
        "participate in the idempotency fingerprint");

    auto regime_request = steady_request("regime-map-submission");
    regime_request.artifacts.regime_maps.push_back(
        unused_regime_map());
    (void)service.submit(regime_request);
    regime_request.artifacts.regime_maps.front()
        .regions.front().branches.front().criteria.front().maximum = 21.0;
    conflict = false;
    try {
        (void)service.submit(regime_request);
    } catch (const thermox::service::JobConflictError&) {
        conflict = true;
    }
    require(
        conflict,
        "regime-map regions and criteria must participate in the "
        "idempotency fingerprint");

    auto referenced = steady_request("reference-submission");
    referenced.artifacts.references.push_back(
        map_reference(engineering_artifact));
    (void)service.submit(referenced);
    referenced.artifacts.references.front() =
        map_reference(alternate_artifact);
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

    auto custom = expression_request("component-submission");
    (void)service.submit(custom);
    custom.components.expression_components.front()
        .equations.front().expression =
        "output.value - 3.0 * input.value";
    conflict = false;
    try {
        (void)service.submit(custom);
    } catch (const thermox::service::JobConflictError&) {
        conflict = true;
    }
    require(
        conflict,
        "request-scoped component definitions must participate "
        "in the idempotency fingerprint");

    auto metadata = expression_request("component-metadata");
    (void)service.submit(metadata);
    metadata.components.expression_components.front().display_name =
        "Revised signal gain";
    conflict = false;
    try {
        (void)service.submit(metadata);
    } catch (const thermox::service::JobConflictError&) {
        conflict = true;
    }
    require(
        conflict,
        "physical-template metadata must participate in the "
        "idempotency fingerprint");
}

void test_worker_executes_request_scoped_component() {
    auto jobs =
        thermox::service::make_in_memory_job_repository();
    auto artifacts =
        thermox::service::make_in_memory_result_artifact_store();
    thermox::service::SimulationJobService service(jobs, artifacts);

    const auto queued =
        service.submit(expression_request("component-run"));
    const auto completed = service.run_next("component-worker");
    require(
        completed &&
            completed->job_id == queued.job_id &&
            completed->state ==
                thermox::service::SimulationJobState::succeeded &&
            completed->execution &&
            !completed->execution->catalog_fingerprint.empty(),
        "worker must reconstruct and execute the request-scoped "
        "component runtime");
    const auto result =
        service.get_result(team_a, queued.job_id);
    require(
        result &&
            result->content.find(
                "\"value_si\": 10") != std::string::npos,
        "request-scoped component job must publish its calculated "
        "result");
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
    request.acceptance_criteria = {
        {
            "outlet_temperature_operating_band",
            "compressor_outlet_temperature",
            "temperature",
            300.0,
            1000.0,
            true,
            true,
        },
        {
            "outlet_temperature_intentionally_failing",
            "compressor_outlet_temperature",
            "temperature",
            1000.0,
            std::nullopt,
            true,
            true,
        },
    };
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
            completed->execution->source_revisions.has_value() &&
            completed->result_artifact.has_value() &&
            completed->result_summary.has_value() &&
            !completed->error.has_value(),
        "successful job must retain worker, provenance, and "
        "artifact metadata");
    require(
        completed->execution->source_revisions
                ->model_revision_id ==
            "model-revision-job-test",
        "worker execution metadata must retain source revision "
        "provenance");
    require(
        completed->execution->artifacts.size() == 1 &&
            completed->execution->artifacts.front().id == "job-map",
        "workers must propagate request artifacts and provenance");
    require(
        completed->result_summary->values.size() == 1U &&
            completed->result_summary->values.front().id ==
                "compressor_outlet_temperature" &&
            completed->result_summary->values.front().dimension ==
                "temperature",
        "workers must materialize configured result projections");
    require(
        completed->result_summary->engineering_acceptance &&
            !completed->result_summary->engineering_acceptance->passed &&
            completed->result_summary->engineering_acceptance
                    ->passed_count == 1U &&
            completed->result_summary->engineering_acceptance
                    ->failed_count == 1U &&
            completed->state ==
                thermox::service::SimulationJobState::succeeded,
        "engineering acceptance must report pass/fail criteria "
        "without rewriting successful numerical execution state");

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
                  "\"thermox.job/v14\"") != std::string::npos &&
            json.find("\"state\": \"succeeded\"") !=
                std::string::npos &&
            json.find("\"result_artifact\": {") !=
                std::string::npos &&
            json.find("\"result_summary\": {") !=
                std::string::npos &&
            json.find("\"engineering_acceptance\": {") !=
                std::string::npos &&
            json.find("\"failed_count\": 1") !=
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

void test_calibration_jobs_use_the_worker_artifact_boundary() {
    auto jobs = thermox::service::make_in_memory_job_repository();
    auto artifacts =
        thermox::service::make_in_memory_result_artifact_store();
    thermox::service::SimulationJobService service(jobs, artifacts);
    auto request = steady_request("calibration-run");
    request.mode = thermox::service::SimulationJobMode::calibration;
    request.case_id.clear();
    request.calibration_id = "acceptance_fit";
    request.source_revisions->case_revision_id.clear();
    request.source_revisions->case_checksum.clear();
    request.source_revisions->calibration_revision_id =
        "calibration-revision-job-test";
    request.source_revisions->calibration_checksum =
        "sha256:" + std::string(64, '3');
    const auto queued = service.submit(request);
    const auto completed = service.run_next("calibration-worker");
    require(
        completed && completed->job_id == queued.job_id &&
            completed->state ==
                thermox::service::SimulationJobState::succeeded &&
            completed->result_artifact.has_value() &&
            !completed->result_summary.has_value() &&
            completed->execution->source_revisions
                    ->calibration_revision_id ==
                "calibration-revision-job-test",
        "calibration jobs must execute through the leased worker and "
        "retain exact calibration provenance");
    const auto result = service.get_result(team_a, queued.job_id);
    require(
        result &&
            result->content.find("\"calibration_id\": ") !=
                std::string::npos &&
            result->content.find("acceptance_fit") !=
                std::string::npos,
        "calibration jobs must publish a durable calibration result");
}

void test_non_ready_submission_is_rejected() {
    auto jobs =
        thermox::service::make_in_memory_job_repository();
    auto artifacts =
        thermox::service::make_in_memory_result_artifact_store();
    thermox::service::SimulationJobService service(jobs, artifacts);

    auto request = steady_request("failed-run");
    request.model_json = "{not valid JSON";
    bool rejected = false;
    try {
        (void)service.submit(request);
    } catch (const thermox::service::JobRequestError& error) {
        rejected = std::string(error.what()).find(
            "not calculation-ready") != std::string::npos;
    }
    require(
        rejected,
        "a malformed model must be rejected by the authoritative "
        "readiness gate before it enters the durable queue");
    require(
        !service.run_next("worker-b").has_value(),
        "a rejected calculation must not create queued work");
}

void test_projection_failure_is_structured() {
    auto jobs =
        thermox::service::make_in_memory_job_repository();
    auto artifacts =
        thermox::service::make_in_memory_result_artifact_store();
    thermox::service::SimulationJobService service(jobs, artifacts);

    auto request = steady_request("missing-projection-value");
    request.result_projections = {
        {
            "missing_output",
            thermox::service::ResultValueScope::kpi,
            {},
            {},
            "not_a_real_kpi",
            "dimensionless",
            thermox::service::ResultAggregation::final,
        },
    };
    (void)service.submit(request);
    const auto completed = service.run_next("projection-worker");
    require(
        completed &&
            completed->state ==
                thermox::service::SimulationJobState::failed &&
            completed->error &&
            completed->error->code ==
                "result_projection_failed" &&
            completed->error->stage == "result" &&
            !completed->result_artifact &&
            !completed->result_summary,
        "an unresolved result projection must fail the job with "
        "a structured result-stage error");
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

void test_expired_attempt_is_requeued_and_fenced() {
    using namespace std::chrono_literals;
    auto jobs =
        thermox::service::make_in_memory_job_repository();
    auto artifacts =
        thermox::service::make_in_memory_result_artifact_store();
    thermox::service::SimulationJobService service(jobs, artifacts);
    const auto queued =
        service.submit(steady_request("in-memory-lease"));
    const auto claimed =
        jobs->claim_next("abandoned-worker", 10ms);
    require(
        claimed && claimed->job_id == queued.job_id &&
            claimed->attempt == 1 &&
            claimed->lease_expires_at,
        "in-memory claims must carry an attempt and lease");
    std::this_thread::sleep_for(15ms);
    const thermox::service::ServiceError exhausted{
        thermox::service::error_schema_v1,
        "worker_attempts_exhausted",
        "worker",
        "attempt limit reached",
    };
    require(
        jobs->recover_expired(2, exhausted) == 1,
        "an expired in-memory attempt must be recovered");
    const auto requeued = service.get(team_a, queued.job_id);
    require(
        requeued &&
            requeued->state ==
                thermox::service::SimulationJobState::queued &&
            requeued->revision == claimed->revision + 1 &&
            requeued->worker_id.empty() &&
            !requeued->lease_expires_at,
        "recovery must requeue with a new fencing revision");
    bool fenced = false;
    try {
        (void)jobs->publish_failure(
            claimed->job_id,
            claimed->revision,
            exhausted,
            std::nullopt);
    } catch (const thermox::service::JobConflictError&) {
        fenced = true;
    }
    require(
        fenced,
        "the abandoned worker revision must be fenced");
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

    request = steady_request("missing-study-provenance");
    request.source_revisions->run_configuration_revision_id =
        "run-revision";
    request.source_revisions->run_configuration_checksum =
        "sha256:" + std::string(64, '3');
    rejected = false;
    try {
        (void)service.submit(request);
    } catch (const thermox::service::JobRequestError&) {
        rejected = true;
    }
    require(
        rejected,
        "run-backed jobs must require exact Study provenance");
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

void test_team_scoped_history_filters_and_paginates() {
    auto jobs =
        thermox::service::make_in_memory_job_repository();
    auto artifacts =
        thermox::service::make_in_memory_result_artifact_store();
    thermox::service::SimulationJobService service(jobs, artifacts);

    auto first_request = steady_request("history-1");
    first_request.source_revisions
        ->run_configuration_revision_id = "run-a";
    first_request.source_revisions
        ->run_configuration_checksum =
        "sha256:" + std::string(64, '3');
    first_request.source_revisions->study_revision_id = "study-a";
    first_request.source_revisions->study_checksum =
        "sha256:" + std::string(64, '5');
    const auto first = service.submit(first_request);
    auto second_request = steady_request("history-2");
    second_request.source_revisions
        ->run_configuration_revision_id = "run-b";
    second_request.source_revisions
        ->run_configuration_checksum =
        "sha256:" + std::string(64, '4');
    second_request.source_revisions->study_revision_id = "study-b";
    second_request.source_revisions->study_checksum =
        "sha256:" + std::string(64, '6');
    const auto second = service.submit(second_request);
    auto third_request = steady_request("history-3");
    third_request.source_revisions
        ->run_configuration_revision_id = "run-a";
    third_request.source_revisions
        ->run_configuration_checksum =
        "sha256:" + std::string(64, '3');
    third_request.source_revisions->study_revision_id = "study-a";
    third_request.source_revisions->study_checksum =
        "sha256:" + std::string(64, '5');
    const auto third = service.submit(third_request);

    auto other_request = first_request;
    other_request.identity = team_b;
    other_request.idempotency_key = "history-other-team";
    (void)service.submit(other_request);

    thermox::service::SimulationJobQuery query;
    query.limit = 2;
    const auto first_page = service.list(team_a, query);
    require(
        first_page.jobs.size() == 2U &&
            first_page.jobs[0].job_id == third.job_id &&
            first_page.jobs[1].job_id == second.job_id &&
            first_page.next.has_value(),
        "history must be newest-first, bounded, and expose a "
        "continuation cursor");
    query.before = first_page.next;
    const auto second_page = service.list(team_a, query);
    require(
        second_page.jobs.size() == 1U &&
            second_page.jobs.front().job_id == first.job_id &&
            !second_page.next.has_value(),
        "history cursor must resume without gaps or duplicates");

    (void)service.cancel(team_a, first.job_id, first.revision);
    query = {};
    query.run_configuration_revision_id = "run-a";
    const auto filtered = service.list(team_a, query);
    require(
        filtered.jobs.size() == 2U &&
            filtered.jobs[0].job_id == third.job_id &&
            filtered.jobs[1].job_id == first.job_id,
        "history must filter by immutable run configuration "
        "provenance");
    query = {};
    query.state =
        thermox::service::SimulationJobState::cancelled;
    const auto cancelled = service.list(team_a, query);
    require(
        cancelled.jobs.size() == 1U &&
            cancelled.jobs.front().job_id == first.job_id,
        "history must filter by current execution state");
    require(
        service.list(team_b, {}).jobs.size() == 1U,
        "history must never cross Team ownership boundaries");

    const auto json =
        thermox::service::serialize_job_page_json(
            first_page, "opaque-test-cursor");
    require(
        json.find("\"schema_version\": "
                  "\"thermox.job_list/v1\"") !=
                std::string::npos &&
            json.find("\"next_cursor\": "
                      "\"opaque-test-cursor\"") !=
                std::string::npos &&
            json.find("\"created_at_unix_ms\":") !=
                std::string::npos,
        "history JSON must expose a versioned page, timestamps, "
        "and an opaque continuation cursor");
}

void test_completed_study_jobs_compare_by_projected_identity() {
    auto jobs = thermox::service::make_in_memory_job_repository();
    auto artifacts =
        thermox::service::make_in_memory_result_artifact_store();
    thermox::service::SimulationJobService service(jobs, artifacts);

    auto baseline_request = steady_request("comparison-baseline");
    baseline_request.source_revisions->study_revision_id = "study-base";
    baseline_request.source_revisions->study_checksum =
        "sha256:" + std::string(64, '3');
    auto candidate_request = steady_request("comparison-candidate");
    candidate_request.source_revisions->study_revision_id =
        "study-candidate";
    candidate_request.source_revisions->study_checksum =
        "sha256:" + std::string(64, '4');
    const auto baseline = jobs->create_or_get(
        baseline_request, "comparison-baseline-fingerprint");
    const auto candidate = jobs->create_or_get(
        candidate_request, "comparison-candidate-fingerprint");

    thermox::service::ResultSummary baseline_summary;
    baseline_summary.mode = "steady";
    baseline_summary.values = {
        {"net_power", "power", 100.0,
         thermox::service::ResultAggregation::final, false, 0.0},
        {"outlet_temperature", "temperature", 300.0,
         thermox::service::ResultAggregation::final, false, 0.0},
        {"baseline_only", "pressure", 1.0e5,
         thermox::service::ResultAggregation::final, false, 0.0},
    };
    baseline_summary.engineering_acceptance =
        thermox::service::EngineeringAcceptanceSummary{
            false, 0, 1, {}};
    thermox::service::ResultSummary candidate_summary;
    candidate_summary.mode = "steady";
    candidate_summary.values = {
        {"net_power", "power", 110.0,
         thermox::service::ResultAggregation::final, false, 0.0},
        {"outlet_temperature", "power", 310.0,
         thermox::service::ResultAggregation::final, false, 0.0},
        {"candidate_only", "mass_flow", 25.0,
         thermox::service::ResultAggregation::final, false, 0.0},
    };
    candidate_summary.engineering_acceptance =
        thermox::service::EngineeringAcceptanceSummary{
            true, 1, 0, {}};
    const thermox::service::ResultArtifactManifest manifest{
        "comparison-artifact", "application/json",
        thermox::service::result_schema_v3, 2, "checksum"};
    const auto claimed_baseline = jobs->claim_next("comparison-worker");
    require(
        claimed_baseline && claimed_baseline->job_id == baseline.job_id,
        "comparison fixture must claim the baseline job first");
    (void)jobs->publish_success(
        baseline.job_id, claimed_baseline->revision, {}, manifest,
        baseline_summary);
    const auto claimed_candidate = jobs->claim_next("comparison-worker");
    require(
        claimed_candidate && claimed_candidate->job_id == candidate.job_id,
        "comparison fixture must claim the candidate job second");
    (void)jobs->publish_success(
        candidate.job_id, claimed_candidate->revision, {}, manifest,
        candidate_summary);

    const auto comparison = service.compare(
        team_a, baseline.job_id, candidate.job_id);
    require(
        comparison && comparison->matched_count == 1U &&
            comparison->incompatible_count == 1U &&
            comparison->baseline_only_count == 1U &&
            comparison->candidate_only_count == 1U &&
            comparison->values.front().id == "baseline_only" &&
            comparison->engineering_acceptance.transition ==
                "not_accepted_to_accepted",
        "comparison must align outputs by stable projection ID and "
        "report coverage and acceptance transitions");
    const auto matched = std::find_if(
        comparison->values.begin(), comparison->values.end(),
        [](const auto& value) { return value.id == "net_power"; });
    require(
        matched != comparison->values.end() &&
            matched->absolute_delta_si == 10.0 &&
            matched->relative_delta &&
            std::abs(*matched->relative_delta - 0.1) < 1.0e-12,
        "matched outputs must expose candidate-minus-baseline SI and "
        "relative deltas");
    require(
        !service.compare(team_b, baseline.job_id, candidate.job_id),
        "comparison must not disclose jobs across Team boundaries");
    const auto json =
        thermox::service::serialize_job_comparison_json(*comparison);
    require(
        json.find("\"schema_version\": "
                  "\"thermox.job_comparison/v1\"") !=
                std::string::npos &&
            json.find("\"relative_delta\": 0.10000000000000001") !=
                std::string::npos &&
            json.find("\"dimension_mismatch\"") !=
                std::string::npos,
        "comparison JSON must retain versioned deltas and explicit "
        "incompatibility evidence");
}

}  // namespace

int main() {
    try {
        test_submission_is_idempotent_and_conflict_safe();
        test_worker_executes_request_scoped_component();
        test_success_publishes_a_readable_artifact();
        test_calibration_jobs_use_the_worker_artifact_boundary();
        test_non_ready_submission_is_rejected();
        test_projection_failure_is_structured();
        test_transient_jobs_use_the_same_artifact_boundary();
        test_cancel_and_optimistic_revision_rules();
        test_claim_is_atomic();
        test_expired_attempt_is_requeued_and_fenced();
        test_request_validation();
        test_team_scope_isolation();
        test_team_scoped_history_filters_and_paginates();
        test_completed_study_jobs_compare_by_projected_identity();
        std::cout << "thermox job service tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "thermox job service tests failed: "
                  << error.what() << "\n";
        return 1;
    }
}
