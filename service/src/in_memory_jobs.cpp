#include "thermox/service/in_memory_jobs.hpp"

#include <algorithm>
#include <chrono>
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
        const std::string scoped_key =
            request.identity.team_id + '\0' +
            request.idempotency_key;
        const auto existing_key =
            jobs_by_key_.find(scoped_key);
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
        record.team_id = request.identity.team_id;
        record.submitted_by_user_id =
            request.identity.user_id;
        record.revision = 1;
        record.created_at =
            std::chrono::system_clock::time_point{
                std::chrono::duration_cast<
                    std::chrono::microseconds>(
                    std::chrono::system_clock::now()
                        .time_since_epoch())};
        record.request = request;
        record.request_fingerprint = request_fingerprint;
        jobs_by_key_.emplace(
            scoped_key, record.job_id);
        queue_order_.emplace(next_queue_sequence_++, record.job_id);
        jobs_.emplace(record.job_id, record);
        return record;
    }

    std::optional<SimulationJobRecord> get(
        const std::string& team_id,
        const std::string& job_id) const override {
        std::lock_guard lock(mutex_);
        const auto found = jobs_.find(job_id);
        if (found == jobs_.end() ||
            found->second.team_id != team_id) {
            return std::nullopt;
        }
        return found->second;
    }

    SimulationJobPage list(
        const std::string& team_id,
        const SimulationJobQuery& query) const override {
        if (query.limit == 0 || query.limit > 200) {
            throw std::invalid_argument(
                "simulation history limit must be between 1 "
                "and 200");
        }
        std::lock_guard lock(mutex_);
        std::vector<SimulationJobRecord> matches;
        matches.reserve(jobs_.size());
        for (const auto& [job_id, record] : jobs_) {
            (void)job_id;
            if (record.team_id != team_id ||
                (query.state && record.state != *query.state)) {
                continue;
            }
            const auto& source = record.request.source_revisions;
            if (!query.project_id.empty() &&
                (!source ||
                 source->project_id != query.project_id)) {
                continue;
            }
            if (!query.run_configuration_revision_id.empty() &&
                (!source ||
                 source->run_configuration_revision_id !=
                     query.run_configuration_revision_id)) {
                continue;
            }
            if (query.before &&
                !(record.created_at < query.before->created_at ||
                  (record.created_at == query.before->created_at &&
                   record.job_id < query.before->job_id))) {
                continue;
            }
            matches.push_back(record);
        }
        std::sort(
            matches.begin(),
            matches.end(),
            [](const auto& left, const auto& right) {
                if (left.created_at != right.created_at) {
                    return left.created_at > right.created_at;
                }
                return left.job_id > right.job_id;
            });
        SimulationJobPage page;
        if (matches.size() > query.limit) {
            matches.resize(query.limit);
            const auto& last = matches.back();
            page.next = SimulationJobCursor{
                last.created_at,
                last.job_id,
            };
        }
        page.jobs = std::move(matches);
        return page;
    }

    std::optional<SimulationJobRecord> claim_next(
        const std::string& worker_id,
        std::chrono::milliseconds lease_duration) override {
        if (worker_id.empty() || lease_duration.count() <= 0) {
            throw std::invalid_argument(
                "worker ID and lease duration must be valid");
        }
        std::lock_guard lock(mutex_);
        for (const auto& [sequence, job_id] : queue_order_) {
            (void)sequence;
            auto& record = jobs_.at(job_id);
            if (record.state != SimulationJobState::queued) {
                continue;
            }
            record.state = SimulationJobState::running;
            record.worker_id = worker_id;
            ++record.attempt;
            record.lease_expires_at =
                std::chrono::system_clock::now() +
                lease_duration;
            ++record.revision;
            return record;
        }
        return std::nullopt;
    }

    bool renew_lease(
        const std::string& job_id,
        std::uint64_t expected_revision,
        const std::string& worker_id,
        std::chrono::milliseconds lease_duration) override {
        if (worker_id.empty() || lease_duration.count() <= 0) {
            throw std::invalid_argument(
                "worker ID and lease duration must be valid");
        }
        std::lock_guard lock(mutex_);
        const auto found = jobs_.find(job_id);
        if (found == jobs_.end()) {
            return false;
        }
        auto& record = found->second;
        const auto now = std::chrono::system_clock::now();
        if (record.revision != expected_revision ||
            record.state != SimulationJobState::running ||
            record.worker_id != worker_id ||
            !record.lease_expires_at ||
            *record.lease_expires_at <= now) {
            return false;
        }
        record.lease_expires_at = now + lease_duration;
        return true;
    }

    std::size_t recover_expired(
        std::uint32_t maximum_attempts,
        const ServiceError& exhausted_error) override {
        if (maximum_attempts == 0) {
            throw std::invalid_argument(
                "maximum worker attempts must be positive");
        }
        std::lock_guard lock(mutex_);
        const auto now = std::chrono::system_clock::now();
        std::size_t recovered = 0;
        for (auto& [job_id, record] : jobs_) {
            (void)job_id;
            if (record.state != SimulationJobState::running ||
                !record.lease_expires_at ||
                *record.lease_expires_at > now) {
                continue;
            }
            record.lease_expires_at.reset();
            record.result_summary.reset();
            ++record.revision;
            ++recovered;
            if (record.attempt < maximum_attempts) {
                record.state = SimulationJobState::queued;
                record.worker_id.clear();
            } else {
                record.state = SimulationJobState::failed;
                record.error = exhausted_error;
                record.execution.reset();
                record.result_artifact.reset();
            }
        }
        return recovered;
    }

    SimulationJobRecord publish_success(
        const std::string& job_id,
        std::uint64_t expected_revision,
        const ExecutionMetadata& execution,
        const ResultArtifactManifest& result_artifact,
        const std::optional<ResultSummary>&
            result_summary) override {
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
        record.result_summary = result_summary;
        record.error.reset();
        record.lease_expires_at.reset();
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
        record.result_summary.reset();
        record.lease_expires_at.reset();
        ++record.revision;
        return record;
    }

    SimulationJobRecord cancel(
        const std::string& team_id,
        const std::string& job_id,
        std::uint64_t expected_revision) override {
        std::lock_guard lock(mutex_);
        auto& record = require(job_id);
        if (record.team_id != team_id) {
            throw JobStateError("simulation job does not exist");
        }
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
        if (!record.lease_expires_at ||
            *record.lease_expires_at <=
                std::chrono::system_clock::now()) {
            throw JobStateError(
                "terminal result requires a live worker lease");
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
        const ResultArtifactManifest& manifest) const override {
        std::lock_guard lock(mutex_);
        const auto found = artifacts_.find(manifest.artifact_id);
        if (found == artifacts_.end()) {
            return std::nullopt;
        }
        if (found->second.size() != manifest.byte_size ||
            checksum_for(found->second) != manifest.checksum) {
            throw std::runtime_error(
                "in-memory result artifact failed manifest "
                "verification");
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
