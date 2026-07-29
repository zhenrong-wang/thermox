#pragma once

#include "thermox/service/simulation_jobs.hpp"

#include <optional>
#include <string>

namespace thermox::postgres::detail {

std::string encode_request(
    const service::SimulationJobRequest& request);
service::SimulationJobRequest decode_request(
    const std::string& payload);

std::string encode_execution(
    const service::ExecutionMetadata& execution);
service::ExecutionMetadata decode_execution(
    const std::string& payload);

std::string encode_error(const service::ServiceError& error);
service::ServiceError decode_error(const std::string& payload);

std::string encode_result_artifact(
    const service::ResultArtifactManifest& artifact);
service::ResultArtifactManifest decode_result_artifact(
    const std::string& payload);

std::string encode_result_summary(
    const service::ResultSummary& summary);
service::ResultSummary decode_result_summary(
    const std::string& payload);

}  // namespace thermox::postgres::detail
