#pragma once

#include "thermox/platform/performance_map.hpp"
#include "thermox/platform/correlation.hpp"
#include "thermox/platform/regime_map.hpp"
#include "thermox/service/simulation_service.hpp"

#include <string>

namespace thermox::service::detail {

platform::PerformanceMapArtifact performance_map_artifact(
    const PerformanceMapArtifactInput& input);

platform::CorrelationArtifact correlation_artifact(
    const CorrelationArtifactInput& input);

platform::RegimeMapArtifact regime_map_artifact(
    const RegimeMapArtifactInput& input);

PerformanceMapQualitySummary performance_map_quality_summary(
    const platform::PerformanceMapArtifact& artifact);

std::string canonicalize_performance_map_payload(
    const std::string& schema_version,
    const std::string& payload_json);

PerformanceMapArtifactInput performance_map_from_payload(
    const std::string& artifact_id,
    const std::string& schema_version,
    const std::string& revision,
    const std::string& checksum,
    const std::string& payload_json);

std::string canonicalize_correlation_payload(
    const std::string& schema_version,
    const std::string& payload_json);

CorrelationArtifactInput correlation_from_payload(
    const std::string& artifact_id,
    const std::string& schema_version,
    const std::string& revision,
    const std::string& checksum,
    const std::string& payload_json);

std::string correlation_payload_json(
    const CorrelationArtifactInput& artifact);

std::string canonicalize_regime_map_payload(
    const std::string& schema_version,
    const std::string& payload_json);

RegimeMapArtifactInput regime_map_from_payload(
    const std::string& artifact_id,
    const std::string& schema_version,
    const std::string& revision,
    const std::string& checksum,
    const std::string& payload_json);

std::string regime_map_payload_json(
    const RegimeMapArtifactInput& artifact);

std::string canonicalize_expression_component_payload(
    const std::string& schema_version,
    const std::string& payload_json);

ExpressionComponentInput expression_component_from_payload(
    const std::string& schema_version,
    const std::string& payload_json);

std::string canonicalize_assembly_template_payload(
    const std::string& schema_version,
    const std::string& payload_json);

}  // namespace thermox::service::detail
