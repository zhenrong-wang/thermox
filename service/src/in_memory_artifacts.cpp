#include "thermox/service/in_memory_artifacts.hpp"

#include <map>
#include <stdexcept>
#include <utility>

namespace thermox::service {

namespace {

class InMemoryEngineeringArtifactResolver final
    : public EngineeringArtifactResolver {
public:
    explicit InMemoryEngineeringArtifactResolver(
        std::vector<PerformanceMapArtifactInput> performance_maps) {
        for (auto& artifact : performance_maps) {
            if (artifact.id.empty()) {
                throw std::invalid_argument(
                    "engineering artifact id must not be empty");
            }
            const auto id = artifact.id;
            if (!performance_maps_
                     .emplace(id, std::move(artifact))
                     .second) {
                throw std::invalid_argument(
                    "duplicate engineering artifact id: " + id);
            }
        }
    }

    std::optional<PerformanceMapArtifactInput>
    resolve_performance_map(
        const std::string& artifact_id) const override {
        const auto found = performance_maps_.find(artifact_id);
        if (found == performance_maps_.end()) {
            return std::nullopt;
        }
        return found->second;
    }

private:
    std::map<std::string, PerformanceMapArtifactInput>
        performance_maps_;
};

}  // namespace

std::shared_ptr<const EngineeringArtifactResolver>
make_in_memory_engineering_artifact_resolver(
    std::vector<PerformanceMapArtifactInput> performance_maps) {
    return std::make_shared<
        const InMemoryEngineeringArtifactResolver>(
        std::move(performance_maps));
}

}  // namespace thermox::service
