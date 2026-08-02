#pragma once

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace thermox::platform {

struct PerformanceMapArtifact;

// Provider-neutral identity and validation contract for immutable engineering
// data consumed by component models. Concrete artifact types own their payload
// schema and validation; the platform registry only enforces identity and type.
class EngineeringArtifact {
public:
    virtual ~EngineeringArtifact() = default;

    std::string id;
    std::string schema_version;
    std::string revision;
    std::string checksum_sha256;

    [[nodiscard]] virtual std::string_view artifact_type()
        const noexcept = 0;
    virtual void validate() const = 0;
};

class EngineeringArtifactRegistry {
public:
    void register_artifact(
        std::shared_ptr<const EngineeringArtifact> artifact);
    void register_artifact(PerformanceMapArtifact artifact);

    template <typename Artifact>
    void register_artifact(Artifact artifact) {
        static_assert(
            std::is_base_of_v<EngineeringArtifact, Artifact>,
            "registered artifact must implement EngineeringArtifact");
        register_artifact(
            std::shared_ptr<const EngineeringArtifact>(
                std::make_shared<const Artifact>(
                    std::move(artifact))));
    }

    [[nodiscard]] std::shared_ptr<const EngineeringArtifact>
    require_artifact(
        const std::string& id,
        std::string_view expected_type = {}) const;

    template <typename Artifact>
    [[nodiscard]] std::shared_ptr<const Artifact> require_as(
        const std::string& id,
        std::string_view expected_type) const {
        const auto artifact = require_artifact(id, expected_type);
        const auto typed =
            std::dynamic_pointer_cast<const Artifact>(artifact);
        if (!typed) {
            throw std::logic_error(
                "engineering artifact '" + id +
                "' has an incompatible runtime payload type");
        }
        return typed;
    }

    [[nodiscard]] bool contains(const std::string& id) const;
    [[nodiscard]] std::vector<std::string> ids() const;

private:
    std::map<
        std::string,
        std::shared_ptr<const EngineeringArtifact>>
        artifacts_;
};

}  // namespace thermox::platform
