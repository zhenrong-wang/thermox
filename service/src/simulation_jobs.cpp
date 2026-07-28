#include "thermox/service/simulation_jobs.hpp"

#include "thermox/service/serialization.hpp"

#include <iomanip>
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

std::string request_fingerprint(
    const SimulationJobRequest& request) {
    std::ostringstream stream;
    stream << std::setprecision(
        std::numeric_limits<double>::max_digits10);
    stream << request.schema_version << '|'
           << to_string(request.mode) << '|'
           << request.case_id.size() << ':' << request.case_id << '|'
           << request.model_json.size() << ':' << request.model_json << '|';
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
    return fnv1a64(stream.str());
}

void validate_request(const SimulationJobRequest& request) {
    if (request.schema_version != job_schema_v1) {
        throw JobRequestError(
            "unsupported job schema version: " +
            request.schema_version);
    }
    if (request.idempotency_key.empty()) {
        throw JobRequestError("idempotency key must not be empty");
    }
    if (request.model_json.empty()) {
        throw JobRequestError("model JSON must not be empty");
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
        std::shared_ptr<SimulationJobRepository> job_repository,
        std::shared_ptr<ResultArtifactStore> artifact_store)
        : simulation(std::move(runtime)),
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
          std::move(jobs),
          std::move(artifacts)) {}

SimulationJobService::SimulationJobService(
    std::shared_ptr<const SimulationRuntime> runtime,
    std::shared_ptr<SimulationJobRepository> jobs,
    std::shared_ptr<ResultArtifactStore> artifacts)
    : impl_(std::make_unique<Impl>(
          std::move(runtime),
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
    const std::string& job_id) const {
    if (job_id.empty()) {
        throw JobRequestError("job ID must not be empty");
    }
    return impl_->jobs->get(job_id);
}

std::optional<ResultArtifact> SimulationJobService::get_result(
    const std::string& job_id) const {
    const auto record = get(job_id);
    if (!record) {
        return std::nullopt;
    }
    if (record->state != SimulationJobState::succeeded ||
        !record->result_artifact) {
        throw JobStateError(
            "result is only available for a succeeded job");
    }
    const auto content = impl_->artifacts->get(
        record->result_artifact->artifact_id);
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
    const std::string& worker_id) {
    if (worker_id.empty()) {
        throw JobRequestError("worker ID must not be empty");
    }
    auto claimed = impl_->jobs->claim_next(worker_id);
    if (!claimed) {
        return std::nullopt;
    }

    try {
        if (claimed->request.mode == SimulationJobMode::steady) {
            SteadySimulationRequest request;
            request.model_json = claimed->request.model_json;
            request.case_id = claimed->request.case_id;
            request.solver = claimed->request.steady_solver;
            const auto response = impl_->simulation.run_steady(request);
            if (!response.succeeded()) {
                return impl_->jobs->publish_failure(
                    claimed->job_id,
                    claimed->revision,
                    response.error,
                    response.metadata);
            }
            const auto content =
                serialize_steady_response_json(response);
            const auto manifest = impl_->artifacts->put_json(
                claimed->job_id,
                response.metadata.result_schema_version,
                content);
            return impl_->jobs->publish_success(
                claimed->job_id,
                claimed->revision,
                response.metadata,
                manifest);
        }

        TransientSimulationRequest request;
        request.model_json = claimed->request.model_json;
        request.case_id = claimed->request.case_id;
        request.solver = claimed->request.transient_solver;
        const auto response =
            impl_->simulation.run_transient(request);
        if (!response.succeeded()) {
            return impl_->jobs->publish_failure(
                claimed->job_id,
                claimed->revision,
                response.error,
                response.metadata);
        }
        const auto content =
            serialize_transient_response_json(response);
        const auto manifest = impl_->artifacts->put_json(
            claimed->job_id,
            response.metadata.result_schema_version,
            content);
        return impl_->jobs->publish_success(
            claimed->job_id,
            claimed->revision,
            response.metadata,
            manifest);
    } catch (const JobConflictError&) {
        throw;
    } catch (const JobStateError&) {
        throw;
    } catch (const std::exception& error) {
        return impl_->jobs->publish_failure(
            claimed->job_id,
            claimed->revision,
            unhandled_worker_error(error),
            std::nullopt);
    }
}

SimulationJobRecord SimulationJobService::cancel(
    const std::string& job_id,
    std::uint64_t expected_revision) {
    if (job_id.empty()) {
        throw JobRequestError("job ID must not be empty");
    }
    return impl_->jobs->cancel(job_id, expected_revision);
}

}  // namespace thermox::service
