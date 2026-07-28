#include "thermox/service/in_memory_jobs.hpp"

#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace thermox::service {

namespace {

class InMemoryJobRepository final
    : public SimulationJobRepository {
public:
    SimulationJobRecord create_or_get(
        const SimulationJobRequest& request,
        const std::string& request_fingerprint) override {
        std::lock_guard lock(mutex_);
        const auto existing_key =
            jobs_by_key_.find(request.idempotency_key);
        if (existing_key != jobs_by_key_.end()) {
            const auto& existing = jobs_.at(existing_key->second);
            if (existing.request_fingerprint !=
                request_fingerprint) {
                throw JobConflictError(
                    "idempotency key is already bound to a "
                    "different simulation request");
            }
            return existing;
        }

        std::ostringstream id;
        id << "job-" << std::setfill('0') << std::setw(8)
           << next_id_++;
        SimulationJobRecord record;
        record.job_id = id.str();
        record.revision = 1;
        record.request = request;
        record.request_fingerprint = request_fingerprint;
        jobs_by_key_.emplace(
            request.idempotency_key, record.job_id);
        queue_order_.emplace(next_queue_sequence_++, record.job_id);
        jobs_.emplace(record.job_id, record);
        return record;
    }

    std::optional<SimulationJobRecord> get(
        const std::string& job_id) const override {
        std::lock_guard lock(mutex_);
        const auto found = jobs_.find(job_id);
        if (found == jobs_.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    std::optional<SimulationJobRecord> claim_next(
        const std::string& worker_id) override {
        std::lock_guard lock(mutex_);
        for (const auto& [sequence, job_id] : queue_order_) {
            (void)sequence;
            auto& record = jobs_.at(job_id);
            if (record.state != SimulationJobState::queued) {
                continue;
            }
            record.state = SimulationJobState::running;
            record.worker_id = worker_id;
            ++record.revision;
            return record;
        }
        return std::nullopt;
    }

    SimulationJobRecord publish_success(
        const std::string& job_id,
        std::uint64_t expected_revision,
        const ExecutionMetadata& execution,
        const ResultArtifactManifest& result_artifact) override {
        std::lock_guard lock(mutex_);
        auto& record = require_running(
            job_id, expected_revision);
        if (result_artifact.artifact_id.empty()) {
            throw JobStateError(
                "cannot publish success without a result artifact");
        }
        record.state = SimulationJobState::succeeded;
        record.execution = execution;
        record.result_artifact = result_artifact;
        record.error.reset();
        ++record.revision;
        return record;
    }

    SimulationJobRecord publish_failure(
        const std::string& job_id,
        std::uint64_t expected_revision,
        const ServiceError& error,
        const std::optional<ExecutionMetadata>& execution) override {
        std::lock_guard lock(mutex_);
        auto& record = require_running(
            job_id, expected_revision);
        record.state = SimulationJobState::failed;
        record.execution = execution;
        record.error = error;
        record.result_artifact.reset();
        ++record.revision;
        return record;
    }

    SimulationJobRecord cancel(
        const std::string& job_id,
        std::uint64_t expected_revision) override {
        std::lock_guard lock(mutex_);
        auto& record = require(job_id);
        require_revision(record, expected_revision);
        if (record.state != SimulationJobState::queued) {
            throw JobStateError(
                "only queued jobs may be cancelled");
        }
        record.state = SimulationJobState::cancelled;
        ++record.revision;
        return record;
    }

private:
    SimulationJobRecord& require(const std::string& job_id) {
        const auto found = jobs_.find(job_id);
        if (found == jobs_.end()) {
            throw JobStateError("simulation job does not exist");
        }
        return found->second;
    }

    static void require_revision(
        const SimulationJobRecord& record,
        std::uint64_t expected_revision) {
        if (record.revision != expected_revision) {
            throw JobConflictError(
                "simulation job revision conflict");
        }
    }

    SimulationJobRecord& require_running(
        const std::string& job_id,
        std::uint64_t expected_revision) {
        auto& record = require(job_id);
        require_revision(record, expected_revision);
        if (record.state != SimulationJobState::running) {
            throw JobStateError(
                "terminal result may only be published for a "
                "running job");
        }
        return record;
    }

    mutable std::mutex mutex_;
    std::uint64_t next_id_{1};
    std::uint64_t next_queue_sequence_{1};
    std::unordered_map<std::string, SimulationJobRecord> jobs_;
    std::unordered_map<std::string, std::string> jobs_by_key_;
    std::map<std::uint64_t, std::string> queue_order_;
};

class InMemoryResultArtifactStore final
    : public ResultArtifactStore {
public:
    ResultArtifactManifest put_json(
        const std::string& job_id,
        const std::string& schema_version,
        const std::string& content) override {
        std::lock_guard lock(mutex_);
        const auto checksum = checksum_for(content);
        const auto artifact_id = job_id + "/" + checksum;
        artifacts_.emplace(artifact_id, content);
        return {
            artifact_id,
            "application/json",
            schema_version,
            static_cast<std::uint64_t>(content.size()),
            checksum,
        };
    }

    std::optional<std::string> get(
        const std::string& artifact_id) const override {
        std::lock_guard lock(mutex_);
        const auto found = artifacts_.find(artifact_id);
        if (found == artifacts_.end()) {
            return std::nullopt;
        }
        return found->second;
    }

private:
    static std::string checksum_for(const std::string& content) {
        std::uint64_t hash = 14695981039346656037ULL;
        for (const auto character : content) {
            hash ^= static_cast<unsigned char>(character);
            hash *= 1099511628211ULL;
        }
        std::ostringstream encoded;
        encoded << "fnv1a64:" << std::hex << std::setfill('0')
                << std::setw(16) << hash;
        return encoded.str();
    }

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> artifacts_;
};

}  // namespace

std::shared_ptr<SimulationJobRepository>
make_in_memory_job_repository() {
    return std::make_shared<InMemoryJobRepository>();
}

std::shared_ptr<ResultArtifactStore>
make_in_memory_result_artifact_store() {
    return std::make_shared<InMemoryResultArtifactStore>();
}

}  // namespace thermox::service
