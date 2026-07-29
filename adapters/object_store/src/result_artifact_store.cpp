#include "thermox/object_store/result_artifact_store.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace thermox::object_store {

namespace {

std::string sha256(std::string_view content) {
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context{
        EVP_MD_CTX_new(), EVP_MD_CTX_free};
    if (!context ||
        EVP_DigestInit_ex(
            context.get(), EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(
            context.get(), content.data(), content.size()) != 1) {
        throw std::runtime_error(
            "could not initialize SHA-256 result checksum");
    }
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int digest_size = 0;
    if (EVP_DigestFinal_ex(
            context.get(), digest, &digest_size) != 1) {
        throw std::runtime_error(
            "could not finalize SHA-256 result checksum");
    }
    std::ostringstream encoded;
    encoded << "sha256:";
    for (unsigned int index = 0;
         index < digest_size;
         ++index) {
        encoded << std::hex << std::setfill('0')
                << std::setw(2)
                << static_cast<unsigned int>(digest[index]);
    }
    return encoded.str();
}

std::string normalize_prefix(std::string prefix) {
    while (!prefix.empty() && prefix.front() == '/') {
        prefix.erase(prefix.begin());
    }
    while (!prefix.empty() && prefix.back() == '/') {
        prefix.pop_back();
    }
    if (prefix.empty()) {
        throw std::invalid_argument(
            "object result key prefix must not be empty");
    }
    std::istringstream segments(prefix);
    std::string segment;
    while (std::getline(segments, segment, '/')) {
        if (segment.empty() || segment == "." ||
            segment == "..") {
            throw std::invalid_argument(
                "object result key prefix contains an unsafe "
                "path segment");
        }
    }
    return prefix;
}

std::string key_component(std::string_view value) {
    if (value.empty()) {
        throw std::invalid_argument(
            "result artifact job ID must not be empty");
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string encoded;
    for (const unsigned char character : value) {
        if (std::isalnum(character) || character == '-' ||
            character == '_' || character == '.') {
            encoded.push_back(static_cast<char>(character));
        } else {
            encoded.push_back('~');
            encoded.push_back(hex[character >> 4U]);
            encoded.push_back(hex[character & 0x0fU]);
        }
    }
    return encoded;
}

class ObjectResultArtifactStore final
    : public service::ResultArtifactStore {
public:
    ObjectResultArtifactStore(
        std::shared_ptr<ObjectStore> objects,
        std::string key_prefix)
        : objects_(std::move(objects)),
          key_prefix_(normalize_prefix(std::move(key_prefix))) {
        if (!objects_) {
            throw std::invalid_argument(
                "object store must not be null");
        }
    }

    service::ResultArtifactManifest put_json(
        const std::string& job_id,
        const std::string& schema_version,
        const std::string& content) override {
        if (schema_version.empty()) {
            throw std::invalid_argument(
                "result artifact schema version must not be empty");
        }
        const auto checksum = sha256(content);
        const auto digest = checksum.substr(
            std::string("sha256:").size());
        const auto artifact_id =
            key_prefix_ + "/" + key_component(job_id) + "/" +
            key_component(schema_version) + "/" +
            digest + ".json";
        Object object;
        object.content = content;
        object.media_type = "application/json";
        object.metadata = {
            {"thermox-checksum", checksum},
            {"thermox-schema-version", schema_version},
            {"thermox-byte-size", std::to_string(content.size())},
        };
        objects_->put(artifact_id, object);
        return {
            artifact_id,
            object.media_type,
            schema_version,
            static_cast<std::uint64_t>(content.size()),
            checksum,
        };
    }

    std::optional<std::string> get(
        const service::ResultArtifactManifest&
            manifest) const override {
        if (!manifest.artifact_id.starts_with(
                key_prefix_ + "/")) {
            throw std::runtime_error(
                "result artifact is outside the configured "
                "object key prefix");
        }
        const auto object = objects_->get(manifest.artifact_id);
        if (!object) {
            return std::nullopt;
        }
        const auto checksum =
            object->metadata.find("thermox-checksum");
        const auto schema = object->metadata.find(
            "thermox-schema-version");
        const auto byte_size =
            object->metadata.find("thermox-byte-size");
        if (checksum == object->metadata.end() ||
            schema == object->metadata.end() ||
            byte_size == object->metadata.end()) {
            throw std::runtime_error(
                "stored result object is missing integrity "
                "metadata");
        }
        if (checksum->second != manifest.checksum ||
            checksum->second != sha256(object->content) ||
            schema->second != manifest.schema_version ||
            byte_size->second !=
                std::to_string(manifest.byte_size) ||
            object->content.size() != manifest.byte_size ||
            object->media_type != manifest.media_type) {
            throw std::runtime_error(
                "stored result object failed manifest "
                "verification");
        }
        return object->content;
    }

private:
    std::shared_ptr<ObjectStore> objects_;
    std::string key_prefix_;
};

}  // namespace

std::shared_ptr<service::ResultArtifactStore>
make_object_result_artifact_store(
    std::shared_ptr<ObjectStore> objects,
    std::string key_prefix) {
    return std::make_shared<ObjectResultArtifactStore>(
        std::move(objects), std::move(key_prefix));
}

}  // namespace thermox::object_store
