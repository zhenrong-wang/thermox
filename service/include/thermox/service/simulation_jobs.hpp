#pragma once

#include "thermox/service/simulation_service.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace thermox::service {

inline constexpr char job_schema_v1[] = "thermox.job/v1";

enum class SimulationJobMode {
    steady,
    transient,
};

std::string to_string(SimulationJobMode mode);

enum class SimulationJobState {
    queued,
    running,
    succeeded,
    failed,
    cancelled,
};

std::string to_string(SimulationJobState state);
bool is_terminal(SimulationJobState state);

struct SimulationJobRequest {
    std::string schema_version{job_schema_v1};
    std::string idempotency_key;
    SimulationJobMode mode{SimulationJobMode::steady};
    std::string model_json;
    std::string case_id;
    SteadySolverSettings steady_solver;
    TransientSolverSettings transient_solver;
    SimulationArtifactBundle artifacts;
};

struct ResultArtifactManifest {
    std::string artifact_id;
    std::string media_type{"application/json"};
    std::string schema_version{result_schema_v3};
    std::uint64_t byte_size{0};
    std::string checksum;
};

struct ResultArtifact {
    ResultArtifactManifest manifest;
    std::string content;
};

struct SimulationJobRecord {
    std::string schema_version{job_schema_v1};
    std::string job_id;
    std::uint64_t revision{0};
    SimulationJobState state{SimulationJobState::queued};
    SimulationJobRequest request;
    std::string request_fingerprint;
    std::string worker_id;
    std::optional<ExecutionMetadata> execution;
    std::optional<ServiceError> error;
    std::optional<ResultArtifactManifest> result_artifact;
};

class JobRequestError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

class JobConflictError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class JobStateError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class SimulationJobRepository {
public:
    virtual ~SimulationJobRepository() = default;

    // Must atomically create a queued job, or return the existing job when
    // both the idempotency key and request fingerprint match.
    virtual SimulationJobRecord create_or_get(
        const SimulationJobRequest& request,
        const std::string& request_fingerprint) = 0;

    virtual std::optional<SimulationJobRecord> get(
        const std::string& job_id) const = 0;

    // Must atomically select one queued job and transition it to running.
    virtual std::optional<SimulationJobRecord> claim_next(
        const std::string& worker_id) = 0;

    // Terminal publication operations are compare-and-swap updates.
    virtual SimulationJobRecord publish_success(
        const std::string& job_id,
        std::uint64_t expected_revision,
        const ExecutionMetadata& execution,
        const ResultArtifactManifest& result_artifact) = 0;
    virtual SimulationJobRecord publish_failure(
        const std::string& job_id,
        std::uint64_t expected_revision,
        const ServiceError& error,
        const std::optional<ExecutionMetadata>& execution) = 0;
    virtual SimulationJobRecord cancel(
        const std::string& job_id,
        std::uint64_t expected_revision) = 0;
};

class ResultArtifactStore {
public:
    virtual ~ResultArtifactStore() = default;

    // Implementations should make repeated writes for the same job and
    // content idempotent. A successful return guarantees the artifact is
    // readable before its manifest is published on a job.
    virtual ResultArtifactManifest put_json(
        const std::string& job_id,
        const std::string& schema_version,
        const std::string& content) = 0;
    // Implementations must return only content that satisfies the stored
    // artifact's integrity metadata.
    virtual std::optional<std::string> get(
        const std::string& artifact_id) const = 0;
};

class SimulationJobService {
public:
    SimulationJobService(
        std::shared_ptr<SimulationJobRepository> jobs,
        std::shared_ptr<ResultArtifactStore> artifacts);
    SimulationJobService(
        std::shared_ptr<const SimulationRuntime> runtime,
        std::shared_ptr<SimulationJobRepository> jobs,
        std::shared_ptr<ResultArtifactStore> artifacts);
    SimulationJobService(
        std::shared_ptr<const SimulationRuntime> runtime,
        std::shared_ptr<const EngineeringArtifactResolver>
            engineering_artifacts,
        std::shared_ptr<SimulationJobRepository> jobs,
        std::shared_ptr<ResultArtifactStore> result_artifacts);
    ~SimulationJobService();
    SimulationJobService(SimulationJobService&&) noexcept;
    SimulationJobService& operator=(SimulationJobService&&) noexcept;
    SimulationJobService(const SimulationJobService&) = delete;
    SimulationJobService& operator=(const SimulationJobService&) = delete;

    [[nodiscard]] SimulationJobRecord submit(
        const SimulationJobRequest& request);
    [[nodiscard]] std::optional<SimulationJobRecord> get(
        const std::string& job_id) const;
    [[nodiscard]] std::optional<ResultArtifact> get_result(
        const std::string& job_id) const;
    [[nodiscard]] std::optional<SimulationJobRecord> run_next(
        const std::string& worker_id);
    [[nodiscard]] SimulationJobRecord cancel(
        const std::string& job_id,
        std::uint64_t expected_revision);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace thermox::service
