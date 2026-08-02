#pragma once

#include "thermox/service/simulation_service.hpp"

#include <memory>
#include <vector>

namespace thermox::service {

std::shared_ptr<const EngineeringArtifactResolver>
make_in_memory_engineering_artifact_resolver(
    std::vector<PerformanceMapArtifactInput> performance_maps,
    std::vector<CorrelationArtifactInput> correlations = {});

}  // namespace thermox::service
