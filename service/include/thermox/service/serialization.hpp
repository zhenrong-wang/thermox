#pragma once

#include "thermox/service/result_projection.hpp"
#include "thermox/service/simulation_jobs.hpp"

#include <string>

namespace thermox::service {

std::string serialize_catalog_response_json(
    const CatalogResponse& response);
std::string serialize_validate_response_json(
    const ValidateModelResponse& response);
std::string serialize_steady_response_json(
    const SteadySimulationResponse& response);
std::string serialize_calibration_response_json(
    const CalibrationResponse& response);
std::string serialize_engineering_study_response_json(
    const EngineeringStudyResponse& response);
std::string serialize_transient_response_json(
    const TransientSimulationResponse& response);
std::string serialize_result_summary_json(
    const ResultSummary& summary);
std::string serialize_job_record_json(
    const SimulationJobRecord& record);
std::string serialize_job_page_json(
    const SimulationJobPage& page,
    const std::string& next_cursor);

}  // namespace thermox::service
