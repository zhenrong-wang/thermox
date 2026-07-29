#pragma once

#include "thermox/service/simulation_service.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace thermox::service {

inline constexpr char result_summary_schema_v1[] =
    "thermox.result_summary/v1";

enum class ResultValueScope {
    system_balance,
    kpi,
    component_metric,
    component_internal,
    port_primary,
    port_derived,
};

std::string to_string(ResultValueScope scope);
ResultValueScope result_value_scope_from_string(
    const std::string& value);

enum class ResultAggregation {
    final,
    minimum,
    maximum,
};

std::string to_string(ResultAggregation aggregation);
ResultAggregation result_aggregation_from_string(
    const std::string& value);

struct ResultProjection {
    std::string id;
    ResultValueScope scope{ResultValueScope::kpi};
    std::string component_id;
    std::string port_name;
    std::string value_name;
    std::string dimension;
    ResultAggregation aggregation{ResultAggregation::final};
};

struct ProjectedResultValue {
    std::string id;
    std::string dimension;
    double value_si{0.0};
    ResultAggregation aggregation{ResultAggregation::final};
    bool has_sample_time{false};
    double sample_time{0.0};
};

struct ResultSummary {
    std::string schema_version{result_summary_schema_v1};
    std::string mode;
    std::vector<ProjectedResultValue> values;
};

class ResultProjectionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void validate_result_projections(
    const std::vector<ResultProjection>& projections);

ResultSummary project_steady_result(
    const GraphResult& graph,
    const std::vector<ResultProjection>& projections);

ResultSummary project_transient_result(
    const std::vector<StateSample>& trajectory,
    const std::vector<ResultProjection>& projections);

}  // namespace thermox::service
