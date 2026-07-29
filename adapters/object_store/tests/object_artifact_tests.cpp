#include "thermox/object_store/result_artifact_store.hpp"

#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class FakeObjectStore final
    : public thermox::object_store::ObjectStore {
public:
    void put(
        const std::string& key,
        const thermox::object_store::Object& object) override {
        const auto found = objects.find(key);
        if (found != objects.end() &&
            found->second.content != object.content) {
            throw std::runtime_error(
                "immutable test object was overwritten");
        }
        objects[key] = object;
    }

    std::optional<thermox::object_store::Object> get(
        const std::string& key) const override {
        const auto found = objects.find(key);
        if (found == objects.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    std::map<
        std::string,
        thermox::object_store::Object> objects;
};

void test_provider_neutral_result_contract() {
    auto objects = std::make_shared<FakeObjectStore>();
    auto artifacts =
        thermox::object_store::make_object_result_artifact_store(
            objects, "tenant-results");
    const std::string content =
        R"({"schema_version":"thermox.result/v3","value":42})";
    const auto first = artifacts->put_json(
        "job/unsafe:id",
        thermox::service::result_schema_v3,
        content);
    const auto repeated = artifacts->put_json(
        "job/unsafe:id",
        thermox::service::result_schema_v3,
        content);
    require(
        first.artifact_id == repeated.artifact_id &&
            first.artifact_id.starts_with("tenant-results/") &&
            first.artifact_id.find("job/unsafe") ==
                std::string::npos &&
            first.checksum.starts_with("sha256:") &&
            first.checksum.size() == 71 &&
            first.byte_size == content.size(),
        "result objects must use deterministic safe keys and "
        "SHA-256 manifests");
    require(
        objects->objects.size() == 1,
        "repeated content writes must be idempotent");
    const auto loaded = artifacts->get(first);
    require(
        loaded && *loaded == content,
        "stored result content must round-trip through the "
        "provider-neutral port");
    auto mismatched_manifest = first;
    mismatched_manifest.checksum =
        "sha256:" + std::string(64, '0');
    bool rejected = false;
    try {
        (void)artifacts->get(mismatched_manifest);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(
        rejected,
        "provider bytes must be checked against the published "
        "manifest");
    auto missing = first;
    missing.artifact_id =
        "tenant-results/missing/object.json";
    require(
        !artifacts->get(missing).has_value(),
        "missing provider objects must remain distinguishable");

    objects->objects.at(first.artifact_id).content.push_back('!');
    rejected = false;
    try {
        (void)artifacts->get(first);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(
        rejected,
        "result retrieval must reject corrupted provider bytes");
}

void test_prefix_is_a_hard_boundary() {
    auto objects = std::make_shared<FakeObjectStore>();
    auto artifacts =
        thermox::object_store::make_object_result_artifact_store(
            objects, "results");
    thermox::service::ResultArtifactManifest manifest;
    manifest.artifact_id = "other/private.json";
    bool rejected = false;
    try {
        (void)artifacts->get(manifest);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(
        rejected,
        "artifact adapters must not read outside their key prefix");
}

}  // namespace

int main() {
    try {
        test_provider_neutral_result_contract();
        test_prefix_is_a_hard_boundary();
        std::cout << "thermox object artifact tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "thermox object artifact tests failed: "
                  << error.what() << "\n";
        return 1;
    }
}
