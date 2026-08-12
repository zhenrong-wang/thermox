#pragma once

#include "thermox/service/simulation_service.hpp"

#include <stdexcept>

namespace thermox::service {

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

ThermalFeasibilitySummary audit_counterflow_thermal_feasibility(
    const std::vector<StateSample>& trajectory,
    double required_minimum_approach_k = 0.0);

void validate_thermal_feasibility_summary(
    const ThermalFeasibilitySummary& summary);

// Adds the three counterflow approach values to each audited component's
// metrics so ordinary Study projections and acceptance criteria can govern
// physical result admissibility.
void append_counterflow_thermal_metrics(
    GraphResult& graph,
    const ThermalFeasibilitySummary& summary);

}  // namespace thermox::service
