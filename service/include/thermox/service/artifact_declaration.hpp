#pragma once

#include "thermox/service/simulation_service.hpp"

#include <stdexcept>
#include <string>

namespace thermox::service {

class ArtifactDeclarationError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

PerformanceMapArtifactInput
parse_performance_map_artifact_declaration_json(
    const std::string& text);

}  // namespace thermox::service
