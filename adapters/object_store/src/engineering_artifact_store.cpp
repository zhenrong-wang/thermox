#include "thermox/object_store/engineering_artifact_store.hpp"

#include <openssl/evp.h>

#include <cctype>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
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
            "could not initialize artifact SHA-256 checksum");
    }
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int digest_size = 0;
    if (EVP_DigestFinal_ex(
            context.get(), digest, &digest_size) != 1) {
        throw std::runtime_error(
            "could not finalize artifact SHA-256 checksum");
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

std::string component(std::string_view value) {
    if (value.empty()) {
        throw std::invalid_argument(
            "artifact object key component must not be empty");
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

class ObjectEngineeringArtifactContentStore final
    : public service::EngineeringArtifactContentStore {
public:
    ObjectEngineeringArtifactContentStore(
        std::shared_ptr<ObjectStore> objects,
        std::string prefix)
        : objects_(std::move(objects)),
          prefix_(component(prefix)) {
        if (!objects_) {
            throw std::invalid_argument(
                "object store must not be null");
        }
    }

    service::ArtifactContentManifest put_json(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& artifact_id,
        const std::string& artifact_schema_version,
        const std::string& canonical_json) override {
        const auto checksum = sha256(canonical_json);
        const auto key =
            prefix_ + "/" + component(team_id) + "/" +
            component(project_id) + "/" +
            component(artifact_id) + "/" +
            component(artifact_schema_version) + "/" +
            checksum.substr(7) + ".json";
        Object object;
        object.content = canonical_json;
        object.media_type = "application/json";
        object.metadata = {
            {"thermox-checksum", checksum},
            {"thermox-byte-size",
             std::to_string(canonical_json.size())},
            {"thermox-artifact-schema",
             artifact_schema_version},
        };
        objects_->put(key, object);
        return {
            key,
            object.media_type,
            static_cast<std::uint64_t>(
                canonical_json.size()),
            checksum,
        };
    }

    std::optional<std::string> get(
        const service::ArtifactContentManifest&
            manifest) const override {
        if (!manifest.object_key.starts_with(prefix_ + "/")) {
            throw std::runtime_error(
                "engineering artifact is outside the "
                "configured object prefix");
        }
        const auto object = objects_->get(manifest.object_key);
        if (!object) {
            return std::nullopt;
        }
        if (object->media_type != manifest.media_type ||
            object->content.size() != manifest.byte_size ||
            sha256(object->content) != manifest.checksum) {
            throw std::runtime_error(
                "engineering artifact object failed manifest "
                "verification");
        }
        return object->content;
    }

private:
    std::shared_ptr<ObjectStore> objects_;
    std::string prefix_;
};

}  // namespace

std::shared_ptr<service::EngineeringArtifactContentStore>
make_object_engineering_artifact_content_store(
    std::shared_ptr<ObjectStore> objects,
    std::string key_prefix) {
    return std::make_shared<
        ObjectEngineeringArtifactContentStore>(
        std::move(objects), std::move(key_prefix));
}

}  // namespace thermox::object_store
