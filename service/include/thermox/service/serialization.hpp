#pragma once

#include "thermox/service/simulation_service.hpp"

#include <string>

namespace thermox::service {

std::string serialize_catalog_response_json(
    const CatalogResponse& response);
std::string serialize_validate_response_json(
    const ValidateModelResponse& response);
std::string serialize_steady_response_json(
    const SteadySimulationResponse& response);
std::string serialize_transient_response_json(
    const TransientSimulationResponse& response);

}  // namespace thermox::service
