#include "thermox/host/host_runtime.hpp"

#include "thermox/service/in_memory_jobs.hpp"
#include "thermox/service/in_memory_projects.hpp"

#ifdef THERMOX_HAS_POSTGRES_JOBS
#include "thermox/postgres/postgres_job_repository.hpp"
#include "thermox/postgres/postgres_project_repository.hpp"
#endif

#ifdef THERMOX_HAS_OBJECT_ARTIFACTS
#include "thermox/object_store/engineering_artifact_store.hpp"
#include "thermox/object_store/result_artifact_store.hpp"
#endif

#ifdef THERMOX_HAS_S3_OBJECT_STORE
#include "thermox/object_store/s3_compatible_object_store.hpp"
#endif

#include <charconv>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace thermox::host {

namespace {

std::string environment(
    const char* name,
    std::string fallback = {}) {
    const char* value = std::getenv(name);
    return value == nullptr
        ? std::move(fallback)
        : std::string(value);
}

template <typename Integer>
Integer environment_integer(
    const char* name,
    Integer fallback,
    Integer minimum,
    Integer maximum) {
    const auto text =
        environment(name, std::to_string(fallback));
    Integer value{};
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size() ||
        value < minimum || value > maximum) {
        throw std::invalid_argument(
            std::string(name) +
            " is outside its supported range");
    }
    return value;
}

}  // namespace

bool PersistenceConfiguration::durable() const {
    return !postgres_url.empty() &&
        !object_store_driver.empty();
}

PersistenceConfiguration persistence_from_environment() {
    PersistenceConfiguration configuration;
    configuration.postgres_url =
        environment("THERMOX_POSTGRES_URL");
    configuration.object_store_driver =
        environment("THERMOX_OBJECT_STORE_DRIVER");
    configuration.object_key_prefix =
        environment("THERMOX_OBJECT_KEY_PREFIX", "results");
    configuration.artifact_key_prefix = environment(
        "THERMOX_ARTIFACT_KEY_PREFIX",
        "engineering-artifacts");
    configuration.s3_endpoint =
        environment("THERMOX_S3_ENDPOINT");
    configuration.s3_region =
        environment("THERMOX_S3_REGION", "us-east-1");
    configuration.s3_bucket =
        environment("THERMOX_S3_BUCKET");
    configuration.s3_access_key =
        environment("THERMOX_S3_ACCESS_KEY");
    configuration.s3_secret_key =
        environment("THERMOX_S3_SECRET_KEY");
    configuration.s3_addressing_style =
        environment("THERMOX_S3_ADDRESSING_STYLE", "path");
    if (!configuration.object_store_driver.empty() &&
        configuration.object_store_driver !=
            "s3-compatible") {
        throw std::invalid_argument(
            "unsupported object store driver: " +
            configuration.object_store_driver);
    }
    return configuration;
}

WorkerConfiguration worker_from_environment() {
    WorkerConfiguration configuration;
    configuration.worker_id =
        environment("THERMOX_WORKER_ID");
    configuration.lease.lease_duration =
        std::chrono::milliseconds{
            environment_integer<long long>(
                "THERMOX_WORKER_LEASE_MS",
                30000,
                1,
                std::numeric_limits<long long>::max())};
    configuration.lease.heartbeat_interval =
        std::chrono::milliseconds{
            environment_integer<long long>(
                "THERMOX_WORKER_HEARTBEAT_MS",
                10000,
                1,
                std::numeric_limits<long long>::max())};
    configuration.lease.maximum_attempts =
        environment_integer<std::uint32_t>(
            "THERMOX_WORKER_MAX_ATTEMPTS",
            3,
            1,
            std::numeric_limits<std::uint32_t>::max());
    configuration.poll_interval =
        std::chrono::milliseconds{
            environment_integer<long long>(
                "THERMOX_WORKER_POLL_MS",
                250,
                1,
                std::numeric_limits<long long>::max())};
    configuration.library_threads =
        environment_integer<std::uint32_t>(
            "THERMOX_LIBRARY_THREADS",
            1,
            1,
            std::numeric_limits<std::uint32_t>::max());
    if (configuration.lease.heartbeat_interval >=
        configuration.lease.lease_duration) {
        throw std::invalid_argument(
            "worker heartbeat interval must be shorter than "
            "the worker lease");
    }
    return configuration;
}

void require_durable(
    const PersistenceConfiguration& configuration) {
    if (!configuration.durable()) {
        throw std::invalid_argument(
            "separate API and worker roles require "
            "THERMOX_POSTGRES_URL and "
            "THERMOX_OBJECT_STORE_DRIVER");
    }
    if (configuration.object_key_prefix.empty() ||
        configuration.artifact_key_prefix.empty() ||
        configuration.s3_endpoint.empty() ||
        configuration.s3_region.empty() ||
        configuration.s3_bucket.empty() ||
        configuration.s3_access_key.empty() ||
        configuration.s3_secret_key.empty()) {
        throw std::invalid_argument(
            "durable S3-compatible storage requires "
            "THERMOX_OBJECT_KEY_PREFIX, "
            "THERMOX_ARTIFACT_KEY_PREFIX, THERMOX_S3_ENDPOINT, "
            "THERMOX_S3_REGION, THERMOX_S3_BUCKET, "
            "THERMOX_S3_ACCESS_KEY, and "
            "THERMOX_S3_SECRET_KEY");
    }
}

#if defined(THERMOX_HAS_OBJECT_ARTIFACTS) && \
    defined(THERMOX_HAS_S3_OBJECT_STORE)
std::shared_ptr<object_store::ObjectStore> make_objects(
    const PersistenceConfiguration& configuration) {
    object_store::S3AddressingStyle style;
    if (configuration.s3_addressing_style == "path") {
        style = object_store::S3AddressingStyle::path;
    } else if (
        configuration.s3_addressing_style ==
        "virtual-hosted") {
        style =
            object_store::S3AddressingStyle::virtual_hosted;
    } else {
        throw std::invalid_argument(
            "THERMOX_S3_ADDRESSING_STYLE must be path or "
            "virtual-hosted");
    }
    return object_store::make_s3_compatible_object_store({
        .endpoint = configuration.s3_endpoint,
        .region = configuration.s3_region,
        .bucket = configuration.s3_bucket,
        .access_key = configuration.s3_access_key,
        .secret_key = configuration.s3_secret_key,
        .addressing_style = style,
    });
}
#endif

void configure_library_thread_limit(std::uint32_t threads) {
    if (threads == 0) {
        throw std::invalid_argument(
            "library thread limit must be positive");
    }
    const auto value = std::to_string(threads);
    for (const char* name : {
             "OMP_NUM_THREADS",
             "OPENBLAS_NUM_THREADS",
             "MKL_NUM_THREADS",
             "VECLIB_MAXIMUM_THREADS",
             "NUMEXPR_NUM_THREADS",
         }) {
        if (setenv(name, value.c_str(), 1) != 0) {
            throw std::runtime_error(
                "could not configure numerical library "
                "thread limits");
        }
    }
}

std::shared_ptr<service::SimulationJobRepository>
make_job_repository(
    const PersistenceConfiguration& configuration) {
    if (configuration.postgres_url.empty()) {
        return service::make_in_memory_job_repository();
    }
#ifdef THERMOX_HAS_POSTGRES_JOBS
    return postgres::make_postgres_job_repository(
        configuration.postgres_url);
#else
    throw std::runtime_error(
        "PostgreSQL was configured, but this build does not "
        "include the PostgreSQL adapter");
#endif
}

std::shared_ptr<service::ProjectRepository>
make_project_repository(
    const PersistenceConfiguration& configuration) {
    if (configuration.postgres_url.empty()) {
        return service::make_in_memory_project_repository();
    }
#ifdef THERMOX_HAS_POSTGRES_JOBS
    return postgres::make_postgres_project_repository(
        configuration.postgres_url);
#else
    throw std::runtime_error(
        "PostgreSQL was configured, but this build does not "
        "include the PostgreSQL adapter");
#endif
}

std::shared_ptr<service::ResultArtifactStore>
make_result_artifact_store(
    const PersistenceConfiguration& configuration) {
    if (configuration.object_store_driver.empty()) {
        return service::make_in_memory_result_artifact_store();
    }
#if defined(THERMOX_HAS_OBJECT_ARTIFACTS) && \
    defined(THERMOX_HAS_S3_OBJECT_STORE)
    return object_store::make_object_result_artifact_store(
        make_objects(configuration),
        configuration.object_key_prefix);
#else
    throw std::runtime_error(
        "object storage was configured, but this build does "
        "not include the S3-compatible driver");
#endif
}

std::shared_ptr<service::EngineeringArtifactContentStore>
make_engineering_artifact_content_store(
    const PersistenceConfiguration& configuration) {
    if (configuration.object_store_driver.empty()) {
        return service::
            make_in_memory_engineering_artifact_content_store();
    }
#if defined(THERMOX_HAS_OBJECT_ARTIFACTS) && \
    defined(THERMOX_HAS_S3_OBJECT_STORE)
    return object_store::
        make_object_engineering_artifact_content_store(
            make_objects(configuration),
            configuration.artifact_key_prefix);
#else
    throw std::runtime_error(
        "object storage was configured, but this build does "
        "not include the S3-compatible driver");
#endif
}

std::string persistence_description(
    const PersistenceConfiguration& configuration) {
    return std::string("job metadata: ") +
        (configuration.postgres_url.empty()
             ? "memory"
             : "postgresql") +
        ", object content: " +
        (configuration.object_store_driver.empty()
             ? "memory"
             : configuration.object_store_driver);
}

}  // namespace thermox::host
