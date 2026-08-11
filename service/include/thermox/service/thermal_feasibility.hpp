#pragma once

#include "thermox/service/simulation_service.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace thermox::service {

inline constexpr const char* thermal_feasibility_schema =
    "thermox.thermal_feasibility/v1";

struct CounterflowApproachResult {
    std::string component_id;
    std::string component_kind;
    double hot_in_minus_cold_out_k{0.0};
    double hot_out_minus_cold_in_k{0.0};
    double minimum_approach_k{0.0};
    bool passed{false};
};

struct ThermalFeasibilitySummary {
    std::string schema_version{thermal_feasibility_schema};
    double required_minimum_approach_k{0.0};
    bool passed{false};
    std::size_t checked_count{0};
    std::size_t passed_count{0};
    std::size_t failed_count{0};
    std::vector<CounterflowApproachResult> counterflow_approaches;
};

class ThermalFeasibilityError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Audits every result component exposing hot_in, hot_out, cold_in, and
// cold_out ports. For counterflow equipment the two terminal approaches are
// T_hot,in - T_cold,out and T_hot,out - T_cold,in.
ThermalFeasibilitySummary audit_counterflow_thermal_feasibility(
    const GraphResult& graph,
    double required_minimum_approach_k = 0.0);

void validate_thermal_feasibility_summary(
    const ThermalFeasibilitySummary& summary);

}  // namespace thermox::service
