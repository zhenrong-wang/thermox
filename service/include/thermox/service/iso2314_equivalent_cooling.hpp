#pragma once

#include "thermox/physics/iso2314_equivalent_cooling.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace thermox::service {

inline constexpr char iso2314_equivalent_cooling_schema_v1[] =
    "thermox.iso2314_equivalent_cooling/v1";

struct Iso2314EquivalentCoolingRequest {
    std::string id;
    std::string standard_reference{
        "GB/T 14100-2016 / ISO 2314:2009, equations 28-31"};
    physics::Iso2314EquivalentCoolingInput calculation;
};

struct Iso2314EquivalentCoolingResponse {
    std::string schema_version{iso2314_equivalent_cooling_schema_v1};
    std::string id;
    std::string standard_reference;
    physics::Iso2314EquivalentCoolingResult calculation;
    std::string evidence_basis;
    bool actual_cooling_flow_identified{false};
    std::vector<std::string> limitations;
};

class Iso2314EquivalentCoolingRequestError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Iso2314EquivalentCoolingRequest
parse_iso2314_equivalent_cooling_request_json(const std::string& text);

Iso2314EquivalentCoolingResponse evaluate_iso2314_equivalent_cooling(
    const Iso2314EquivalentCoolingRequest& request);

Iso2314EquivalentCoolingResponse
evaluate_iso2314_equivalent_cooling_json(const std::string& text);

std::string serialize_iso2314_equivalent_cooling_response_json(
    const Iso2314EquivalentCoolingResponse& response);

}  // namespace thermox::service
