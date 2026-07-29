#include "thermox/service/simulation_jobs.hpp"

#include "thermox/service/serialization.hpp"

#include <algorithm>
#include <iomanip>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <limits>
#include <sstream>
#include <string_view>
#include <utility>

namespace thermox::service {

namespace {

std::string fnv1a64(std::string_view value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto character : value) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ULL;
    }
    std::ostringstream encoded;
    encoded << "fnv1a64:" << std::hex << std::setfill('0')
            << std::setw(16) << hash;
    return encoded.str();
}

void append_steady_settings(
    std::ostringstream& stream,
    const SteadySolverSettings& settings) {
    stream << settings.max_iterations << '|'
           << settings.residual_tolerance << '|'
           << settings.step_tolerance << '|'
           << settings.finite_difference_epsilon << '|'
           << settings.min_damping << '|'
           << settings.damping_reduction << '|'
           << settings.sufficient_decrease << '|'
           << settings.max_line_search_steps;
}

void append_string(
    std::ostringstream& stream,
    const std::string& value) {
    stream << value.size() << ':' << value << '|';
}

void append_map_payload(
    std::ostringstream& stream,
    const PerformanceMapPayloadInput& map) {
    append_string(stream, map.primary_variable.name);
    append_string(stream, map.primary_variable.dimension);
    append_string(stream, map.family_variable.name);
    append_string(stream, map.family_variable.dimension);
    stream << map.output_variables.size() << '|';
    for (const auto& output : map.output_variables) {
        append_string(stream, output.name);
        append_string(stream, output.dimension);
    }
    stream << map.curves.size() << '|';
    for (const auto& curve : map.curves) {
        stream << curve.family_coordinate << '|'
               << curve.samples.size() << '|';
        for (const auto& sample : curve.samples) {
            stream << sample.coordinate << '|'
                   << sample.outputs.size() << '|';
            for (const double output : sample.outputs) {
                stream << output << '|';
            }
        }
    }
    append_string(stream, map.primary_extrapolation);
    append_string(stream, map.family_extrapolation);
}

void append_artifacts(
    std::ostringstream& stream,
    const SimulationArtifactBundle& artifacts) {
    stream << artifacts.performance_maps.size() << '|';
    for (const auto& artifact : artifacts.performance_maps) {
        append_string(stream, artifact.id);
        append_string(stream, artifact.schema_version);
        append_string(stream, artifact.revision);
        append_string(stream, artifact.checksum_sha256);
        stream << artifact.map.has_value() << '|';
        if (artifact.map) {
            append_map_payload(stream, *artifact.map);
        }
        stream << artifact.condition_variable.has_value() << '|';
        if (artifact.condition_variable) {
            append_string(stream, artifact.condition_variable->name);
            append_string(
                stream, artifact.condition_variable->dimension);
        }
        stream << artifact.layers.size() << '|';
        for (const auto& layer : artifact.layers) {
            stream << layer.condition_coordinate << '|';
            append_map_payload(stream, layer.map);
        }
        append_string(stream, artifact.condition_extrapolation);
    }
    stream << artifacts.references.size() << '|';
    for (const auto& reference : artifacts.references) {
        append_string(stream, reference.id);
        append_string(stream, reference.artifact_type);
        append_string(stream, reference.schema_version);
        append_string(stream, reference.revision);
        append_string(stream, reference.checksum_sha256);
    }
}

std::string request_fingerprint(
    const SimulationJobRequest& request) {
    std::ostringstream stream;
    stream << std::setprecision(
        std::numeric_limits<double>::max_digits10);
    stream << request.schema_version << '|'
           << to_string(request.mode) << '|'
           << request.case_id.size() << ':' << request.case_id << '|'
           << request.model_json.size() << ':' << request.model_json << '|';
    stream << request.source_revisions.has_value() << '|';
    if (request.source_revisions) {
        append_string(
            stream, request.source_revisions->project_id);
        append_string(
            stream,
            request.source_revisions->model_revision_id);
        append_string(
            stream, request.source_revisions->model_checksum);
        append_string(
            stream,
            request.source_revisions->case_revision_id);
        append_string(
            stream, request.source_revisions->case_checksum);
        append_string(
            stream,
            request.source_revisions
                ->run_configuration_revision_id);
        append_string(
            stream,
            request.source_revisions
                ->run_configuration_checksum);
    }
    append_steady_settings(stream, request.steady_solver);
    stream << '|'
           << request.transient_solver.start_time << '|'
           << request.transient_solver.end_time << '|'
           << request.transient_solver.initial_step << '|'
           << request.transient_solver.min_step << '|'
           << request.transient_solver.max_step << '|'
           << request.transient_solver.absolute_tolerance << '|'
           << request.transient_solver.relative_tolerance << '|'
           << request.transient_solver.max_steps << '|'
           << request.transient_solver.max_consecutive_rejections << '|'
           << request.transient_solver
                  .compute_consistent_initial_conditions
           << '|';
    append_steady_settings(
        stream, request.transient_solver.nonlinear_solver);
    stream << '|';
    append_artifacts(stream, request.artifacts);
    stream << '|' << request.result_projections.size() << '|';
    for (const auto& projection : request.result_projections) {
        append_string(stream, projection.id);
        append_string(stream, to_string(projection.scope));
        append_string(stream, projection.component_id);
        append_string(stream, projection.port_name);
        append_string(stream, projection.value_name);
        append_string(stream, projection.dimension);
        append_string(stream, to_string(projection.aggregation));
    }
    return fnv1a64(stream.str());
}

void validate_request(const SimulationJobRequest& request) {
    if (request.schema_version != job_schema_v5) {
        throw JobRequestError(
            "unsupported job schema version: " +
            request.schema_version);
    }
    if (request.idempotency_key.empty()) {
        throw JobRequestError("idempotency key must not be empty");
    }
    if (request.identity.user_id.empty()) {
        throw JobRequestError("identity user ID must not be empty");
    }
    if (request.identity.team_id.empty()) {
        throw JobRequestError("identity team ID must not be empty");
    }
    if (request.model_json.empty()) {
        throw JobRequestError("model JSON must not be empty");
    }
    if (request.source_revisions) {
        const auto& source = *request.source_revisions;
        if (source.project_id.empty() ||
            source.model_revision_id.empty() ||
            source.model_checksum.empty() ||
            source.case_revision_id.empty() ||
            source.case_checksum.empty()) {
            throw JobRequestError(
                "revision-backed jobs require complete source "
                "revision provenance");
        }
        if (source.run_configuration_revision_id.empty() !=
            source.run_configuration_checksum.empty()) {
            throw JobRequestError(
                "run configuration revision provenance "
                "requires both revision ID and checksum");
        }
    }
    try {
        validate_result_projections(request.result_projections);
    } catch (const ResultProjectionError& error) {
        throw JobRequestError(error.what());
    }
    if (request.mode == SimulationJobMode::steady &&
        std::any_of(
            request.result_projections.begin(),
            request.result_projections.end(),
            [](const auto& projection) {
                return projection.aggregation !=
                    ResultAggregation::final;
            })) {
        throw JobRequestError(
            "steady jobs only support final result projection "
            "aggregation");
    }
}

void validate_identity(const IdentityContext& identity) {
    if (identity.user_id.empty()) {
        throw JobRequestError("identity user ID must not be empty");
    }
    if (identity.team_id.empty()) {
        throw JobRequestError("identity team ID must not be empty");
    }
}

ServiceError unhandled_worker_error(const std::exception& error) {
    return {
        error_schema_v1,
        "worker_execution_failed",
        "worker",
        error.what(),
    };
}

}  // namespace

std::string to_string(SimulationJobMode mode) {
    switch (mode) {
        case SimulationJobMode::steady:
            return "steady";
        case SimulationJobMode::transient:
            return "transient";
    }
    return "unknown";
}

std::string to_string(SimulationJobState state) {
    switch (state) {
        case SimulationJobState::queued:
            return "queued";
        case SimulationJobState::running:
            return "running";
        case SimulationJobState::succeeded:
            return "succeeded";
        case SimulationJobState::failed:
            return "failed";
        case SimulationJobState::cancelled:
            return "cancelled";
    }
    return "unknown";
}

bool is_terminal(SimulationJobState state) {
    return state == SimulationJobState::succeeded ||
        state == SimulationJobState::failed ||
        state == SimulationJobState::cancelled;
}

struct SimulationJobService::Impl {
    Impl(
        std::shared_ptr<const SimulationRuntime> runtime,
        std::shared_ptr<const EngineeringArtifactResolver>
            engineering_artifacts,
        std::shared_ptr<SimulationJobRepository> job_repository,
        std::shared_ptr<ResultArtifactStore> artifact_store)
        : simulation(
              std::move(runtime),
              std::move(engineering_artifacts)),
          jobs(std::move(job_repository)),
          artifacts(std::move(artifact_store)) {
        if (!jobs) {
            throw std::invalid_argument(
                "job repository must not be null");
        }
        if (!artifacts) {
            throw std::invalid_argument(
                "result artifact store must not be null");
        }
    }

    SimulationService simulation;
    std::shared_ptr<SimulationJobRepository> jobs;
    std::shared_ptr<ResultArtifactStore> artifacts;
};

SimulationJobService::SimulationJobService(
    std::shared_ptr<SimulationJobRepository> jobs,
    std::shared_ptr<ResultArtifactStore> artifacts)
    : SimulationJobService(
          make_default_simulation_runtime(),
          nullptr,
          std::move(jobs),
          std::move(artifacts)) {}

SimulationJobService::SimulationJobService(
    std::shared_ptr<const SimulationRuntime> runtime,
    std::shared_ptr<SimulationJobRepository> jobs,
    std::shared_ptr<ResultArtifactStore> artifacts)
    : SimulationJobService(
          std::move(runtime),
          nullptr,
          std::move(jobs),
          std::move(artifacts)) {}

SimulationJobService::SimulationJobService(
    std::shared_ptr<const SimulationRuntime> runtime,
    std::shared_ptr<const EngineeringArtifactResolver>
        engineering_artifacts,
    std::shared_ptr<SimulationJobRepository> jobs,
    std::shared_ptr<ResultArtifactStore> artifacts)
    : impl_(std::make_unique<Impl>(
          std::move(runtime),
          std::move(engineering_artifacts),
          std::move(jobs),
          std::move(artifacts))) {}

SimulationJobService::~SimulationJobService() = default;
SimulationJobService::SimulationJobService(
    SimulationJobService&&) noexcept = default;
SimulationJobService& SimulationJobService::operator=(
    SimulationJobService&&) noexcept = default;

SimulationJobRecord SimulationJobService::submit(
    const SimulationJobRequest& request) {
    validate_request(request);
    return impl_->jobs->create_or_get(
        request, request_fingerprint(request));
}

std::optional<SimulationJobRecord> SimulationJobService::get(
    const IdentityContext& identity,
    const std::string& job_id) const {
    validate_identity(identity);
    if (job_id.empty()) {
        throw JobRequestError("job ID must not be empty");
    }
    return impl_->jobs->get(identity.team_id, job_id);
}

SimulationJobPage SimulationJobService::list(
    const IdentityContext& identity,
    const SimulationJobQuery& query) const {
    validate_identity(identity);
    if (query.limit == 0 || query.limit > 200) {
        throw JobRequestError(
            "simulation history limit must be between 1 and 200");
    }
    if (query.before && query.before->job_id.empty()) {
        throw JobRequestError(
            "simulation history cursor job ID must not be empty");
    }
    return impl_->jobs->list(identity.team_id, query);
}

std::optional<ResultArtifact> SimulationJobService::get_result(
    const IdentityContext& identity,
    const std::string& job_id) const {
    const auto record = get(identity, job_id);
    if (!record) {
        return std::nullopt;
    }
    if (record->state != SimulationJobState::succeeded ||
        !record->result_artifact) {
        throw JobStateError(
            "result is only available for a succeeded job");
    }
    const auto content = impl_->artifacts->get(
        *record->result_artifact);
    if (!content) {
        throw JobStateError(
            "succeeded job references a missing result artifact");
    }
    if (content->size() !=
        record->result_artifact->byte_size) {
        throw JobStateError(
            "result artifact size does not match its manifest");
    }
    return ResultArtifact{
        *record->result_artifact,
        *content,
    };
}

std::optional<SimulationJobRecord> SimulationJobService::run_next(
    const std::string& worker_id,
    const SimulationWorkerSettings& settings) {
    if (worker_id.empty()) {
        throw JobRequestError("worker ID must not be empty");
    }
    if (settings.lease_duration.count() <= 0 ||
        settings.heartbeat_interval.count() <= 0 ||
        settings.heartbeat_interval >= settings.lease_duration ||
        settings.maximum_attempts == 0) {
        throw JobRequestError(
            "worker lease settings are invalid");
    }
    const ServiceError exhausted_error{
        error_schema_v1,
        "worker_attempts_exhausted",
        "worker",
        "simulation exceeded the maximum number of worker "
        "lease attempts",
    };
    (void)impl_->jobs->recover_expired(
        settings.maximum_attempts, exhausted_error);
    auto claimed = impl_->jobs->claim_next(
        worker_id, settings.lease_duration);
    if (!claimed) {
        return std::nullopt;
    }

    struct HeartbeatState {
        std::mutex mutex;
        std::condition_variable_any changed;
        std::atomic<bool> lost{false};
    };
    const auto heartbeat_state =
        std::make_shared<HeartbeatState>();
    std::jthread heartbeat(
        [
            jobs = impl_->jobs,
            heartbeat_state,
            job_id = claimed->job_id,
            revision = claimed->revision,
            worker_id,
            settings
        ](const std::stop_token& stop) {
            std::unique_lock lock(heartbeat_state->mutex);
            while (!stop.stop_requested()) {
                heartbeat_state->changed.wait_for(
                    lock,
                    stop,
                    settings.heartbeat_interval,
                    [] { return false; });
                if (stop.stop_requested()) {
                    break;
                }
                lock.unlock();
                bool renewed = false;
                try {
                    renewed = jobs->renew_lease(
                        job_id,
                        revision,
                        worker_id,
                        settings.lease_duration);
                } catch (...) {
                    renewed = false;
                }
                lock.lock();
                if (!renewed) {
                    heartbeat_state->lost = true;
                    break;
                }
            }
        });
    const auto require_lease = [&]() {
        if (heartbeat_state->lost.load()) {
            throw JobStateError(
                "worker lost its simulation job lease");
        }
    };

    try {
        if (claimed->request.mode == SimulationJobMode::steady) {
            SteadySimulationRequest request;
            request.model_json = claimed->request.model_json;
            request.case_id = claimed->request.case_id;
            request.solver = claimed->request.steady_solver;
            request.artifacts = claimed->request.artifacts;
            auto response = impl_->simulation.run_steady(request);
            response.metadata.source_revisions =
                claimed->request.source_revisions;
            require_lease();
            if (!response.succeeded()) {
                return impl_->jobs->publish_failure(
                    claimed->job_id,
                    claimed->revision,
                    response.error,
                    response.metadata);
            }
            const auto summary =
                claimed->request.result_projections.empty()
                ? std::optional<ResultSummary>{}
                : std::optional<ResultSummary>{
                      project_steady_result(
                          response.graph,
                          claimed->request.result_projections)};
            const auto content =
                serialize_steady_response_json(response);
            const auto manifest = impl_->artifacts->put_json(
                claimed->job_id,
                response.metadata.result_schema_version,
                content);
            require_lease();
            return impl_->jobs->publish_success(
                claimed->job_id,
                claimed->revision,
                response.metadata,
                manifest,
                summary);
        }

        TransientSimulationRequest request;
        request.model_json = claimed->request.model_json;
        request.case_id = claimed->request.case_id;
        request.solver = claimed->request.transient_solver;
        request.artifacts = claimed->request.artifacts;
        auto response =
            impl_->simulation.run_transient(request);
        response.metadata.source_revisions =
            claimed->request.source_revisions;
        require_lease();
        if (!response.succeeded()) {
            return impl_->jobs->publish_failure(
                claimed->job_id,
                claimed->revision,
                response.error,
                response.metadata);
        }
        const auto summary =
            claimed->request.result_projections.empty()
            ? std::optional<ResultSummary>{}
            : std::optional<ResultSummary>{
                  project_transient_result(
                      response.trajectory,
                      claimed->request.result_projections)};
        const auto content =
            serialize_transient_response_json(response);
        const auto manifest = impl_->artifacts->put_json(
            claimed->job_id,
            response.metadata.result_schema_version,
            content);
        require_lease();
        return impl_->jobs->publish_success(
            claimed->job_id,
            claimed->revision,
            response.metadata,
            manifest,
            summary);
    } catch (const JobConflictError&) {
        throw;
    } catch (const JobStateError&) {
        throw;
    } catch (const ResultProjectionError& error) {
        return impl_->jobs->publish_failure(
            claimed->job_id,
            claimed->revision,
            {
                error_schema_v1,
                "result_projection_failed",
                "result",
                error.what(),
            },
            std::nullopt);
    } catch (const std::exception& error) {
        return impl_->jobs->publish_failure(
            claimed->job_id,
            claimed->revision,
            unhandled_worker_error(error),
            std::nullopt);
    }
}

SimulationJobRecord SimulationJobService::cancel(
    const IdentityContext& identity,
    const std::string& job_id,
    std::uint64_t expected_revision) {
    validate_identity(identity);
    if (job_id.empty()) {
        throw JobRequestError("job ID must not be empty");
    }
    return impl_->jobs->cancel(
        identity.team_id, job_id, expected_revision);
}

}  // namespace thermox::service
