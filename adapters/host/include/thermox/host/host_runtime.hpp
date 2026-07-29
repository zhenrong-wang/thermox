#pragma once

#include "thermox/service/simulation_jobs.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace thermox::host {

struct PersistenceConfiguration {
    std::string postgres_url;
    std::string object_store_driver;
    std::string object_key_prefix{"results"};
    std::string s3_endpoint;
    std::string s3_region{"us-east-1"};
    std::string s3_bucket;
    std::string s3_access_key;
    std::string s3_secret_key;
    std::string s3_addressing_style{"path"};

    [[nodiscard]] bool durable() const;
};

struct WorkerConfiguration {
    std::string worker_id;
    service::SimulationWorkerSettings lease;
    std::chrono::milliseconds poll_interval{250};
    std::uint32_t library_threads{1};
};

PersistenceConfiguration persistence_from_environment();
WorkerConfiguration worker_from_environment();

void require_durable(
    const PersistenceConfiguration& configuration);
void configure_library_thread_limit(std::uint32_t threads);

std::shared_ptr<service::SimulationJobRepository>
make_job_repository(
    const PersistenceConfiguration& configuration);
std::shared_ptr<service::ResultArtifactStore>
make_result_artifact_store(
    const PersistenceConfiguration& configuration);

std::string persistence_description(
    const PersistenceConfiguration& configuration);

}  // namespace thermox::host
