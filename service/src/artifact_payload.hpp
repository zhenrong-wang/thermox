#pragma once

#include "thermox/service/simulation_service.hpp"

#include <string>

namespace thermox::service::detail {

std::string canonicalize_performance_map_payload(
    const std::string& schema_version,
    const std::string& payload_json);

PerformanceMapArtifactInput performance_map_from_payload(
    const std::string& artifact_id,
    const std::string& schema_version,
    const std::string& revision,
    const std::string& checksum,
    const std::string& payload_json);

std::string canonicalize_expression_component_payload(
    const std::string& schema_version,
    const std::string& payload_json);

ExpressionComponentInput expression_component_from_payload(
    const std::string& schema_version,
    const std::string& payload_json);

}  // namespace thermox::service::detail
