#include "thermox/object_store/result_artifact_store.hpp"
#include "thermox/object_store/s3_compatible_object_store.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
}

}  // namespace

int main() {
    const auto endpoint =
        environment("THERMOX_TEST_S3_ENDPOINT");
    const auto bucket =
        environment("THERMOX_TEST_S3_BUCKET");
    const auto access_key =
        environment("THERMOX_TEST_S3_ACCESS_KEY");
    const auto secret_key =
        environment("THERMOX_TEST_S3_SECRET_KEY");
    if (endpoint.empty() || bucket.empty() ||
        access_key.empty() || secret_key.empty()) {
        std::cout
            << "S3 integration environment is incomplete; "
               "skipping S3-compatible object-store tests\n";
        return 77;
    }

    try {
        auto objects =
            thermox::object_store::
                make_s3_compatible_object_store({
                    .endpoint = endpoint,
                    .region = environment(
                        "THERMOX_TEST_S3_REGION")
                        .empty()
                        ? "us-east-1"
                        : environment(
                              "THERMOX_TEST_S3_REGION"),
                    .bucket = bucket,
                    .access_key = access_key,
                    .secret_key = secret_key,
                });
        auto writer =
            thermox::object_store::
                make_object_result_artifact_store(
                    objects, "integration/results");
        const std::string content =
            R"({"schema_version":"thermox.result/v4","provider":"s3-compatible"})";
        const auto manifest = writer->put_json(
            "integration-job",
            thermox::service::result_schema_v4,
            content);

        auto second_client =
            thermox::object_store::
                make_s3_compatible_object_store({
                    .endpoint = endpoint,
                    .region = environment(
                        "THERMOX_TEST_S3_REGION")
                        .empty()
                        ? "us-east-1"
                        : environment(
                              "THERMOX_TEST_S3_REGION"),
                    .bucket = bucket,
                    .access_key = access_key,
                    .secret_key = secret_key,
                });
        auto reader =
            thermox::object_store::
                make_object_result_artifact_store(
                    second_client, "integration/results");
        const auto loaded = reader->get(manifest);
        require(
            loaded && *loaded == content &&
                manifest.checksum.starts_with("sha256:"),
            "S3-compatible result must survive independent "
            "writer and reader instances");
        auto missing = manifest;
        missing.artifact_id =
            "integration/results/missing/object.json";
        require(
            !reader->get(missing).has_value(),
            "S3-compatible driver must map a missing key to "
            "not found");
        std::cout
            << "thermox S3-compatible object-store tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "thermox S3-compatible object-store tests failed: "
            << error.what() << "\n";
        return 1;
    }
}
