#include "thermox/platform/engineering_artifact.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace thermox::platform {

void EngineeringArtifactRegistry::register_artifact(
    std::shared_ptr<const EngineeringArtifact> artifact) {
    if (!artifact) {
        throw std::invalid_argument(
            "engineering artifact must not be null");
    }
    if (artifact->id.empty()) {
        throw std::invalid_argument(
            "engineering artifact id must not be empty");
    }
    if (artifact->artifact_type().empty()) {
        throw std::invalid_argument(
            "engineering artifact '" + artifact->id +
            "' must declare a type");
    }
    if (artifact->schema_version.empty()) {
        throw std::invalid_argument(
            "engineering artifact '" + artifact->id +
            "' must declare a schema version");
    }
    if (artifact->revision.empty()) {
        throw std::invalid_argument(
            "engineering artifact '" + artifact->id +
            "' must declare a revision");
    }
    const bool valid_checksum =
        artifact->checksum_sha256.size() == 64 &&
        std::all_of(
            artifact->checksum_sha256.begin(),
            artifact->checksum_sha256.end(),
            [](unsigned char character) {
                return std::isxdigit(character) != 0;
            });
    if (!valid_checksum) {
        throw std::invalid_argument(
            "engineering artifact '" + artifact->id +
            "' must declare a 64-character SHA-256 checksum");
    }
    artifact->validate();

    const auto id = artifact->id;
    if (!artifacts_.emplace(id, std::move(artifact)).second) {
        throw std::invalid_argument(
            "duplicate engineering artifact id: " + id);
    }
}

std::shared_ptr<const EngineeringArtifact>
EngineeringArtifactRegistry::require_artifact(
    const std::string& id,
    std::string_view expected_type) const {
    const auto found = artifacts_.find(id);
    if (found == artifacts_.end()) {
        throw std::invalid_argument(
            "no engineering artifact registered for id: " + id);
    }
    if (!expected_type.empty() &&
        found->second->artifact_type() != expected_type) {
        throw std::invalid_argument(
            "engineering artifact '" + id + "' has type '" +
            std::string(found->second->artifact_type()) +
            "', expected '" + std::string(expected_type) + "'");
    }
    return found->second;
}

bool EngineeringArtifactRegistry::contains(
    const std::string& id) const {
    return artifacts_.find(id) != artifacts_.end();
}

std::vector<std::string> EngineeringArtifactRegistry::ids() const {
    std::vector<std::string> result;
    result.reserve(artifacts_.size());
    for (const auto& [id, _] : artifacts_) {
        result.push_back(id);
    }
    return result;
}

}  // namespace thermox::platform
