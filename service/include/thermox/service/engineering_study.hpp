#pragma once

#include "thermox/service/simulation_service.hpp"

#include <stdexcept>
#include <string>

namespace thermox::service {

inline constexpr char engineering_study_request_schema_v1[] =
    "thermox.engineering_study/v1";

class EngineeringStudyRequestError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

EngineeringStudyRequest parse_engineering_study_request_json(
    const std::string& text);

EngineeringStudyResponse evaluate_engineering_study_json(
    const std::string& text);

}  // namespace thermox::service
