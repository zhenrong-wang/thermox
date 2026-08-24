#pragma once

#include "thermox/service/simulation_service.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace thermox::service {

inline constexpr char result_summary_schema_v4[] =
    "thermox.result_summary/v4";

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
    mean,
    root_mean_square,
};

std::string to_string(ResultAggregation aggregation);
ResultAggregation result_aggregation_from_string(
    const std::string& value);

enum class ResultWindowAnchor {
    simulation,
    event,
};

std::string to_string(ResultWindowAnchor anchor);
ResultWindowAnchor result_window_anchor_from_string(
    const std::string& value);

struct ResultWindow {
    ResultWindowAnchor anchor{ResultWindowAnchor::simulation};
    double start_time{0.0};
    double end_time{0.0};
    std::string event_name;
    std::size_t event_occurrence{0};
    bool operator==(const ResultWindow&) const = default;
};

struct ResultProjection {
    std::string id;
    ResultValueScope scope{ResultValueScope::kpi};
    std::string component_id;
    std::string port_name;
    std::string value_name;
    std::string dimension;
    ResultAggregation aggregation{ResultAggregation::final};
    std::optional<ResultWindow> window;
};

struct ProjectedResultValue {
    std::string id;
    std::string dimension;
    double value_si{0.0};
    ResultAggregation aggregation{ResultAggregation::final};
    bool has_sample_time{false};
    double sample_time{0.0};
    bool has_window{false};
    double window_start_time{0.0};
    double window_end_time{0.0};
    std::string window_anchor_event_name;
    std::size_t window_anchor_event_occurrence{0};
};

struct EngineeringAcceptanceCriterion {
    std::string id;
    std::string projection_id;
    std::string dimension;
    std::optional<double> lower_bound_si;
    std::optional<double> upper_bound_si;
    bool lower_inclusive{true};
    bool upper_inclusive{true};
};

struct EngineeringAcceptanceResult {
    std::string criterion_id;
    std::string projection_id;
    std::string dimension;
    double actual_value_si{0.0};
    std::optional<double> lower_bound_si;
    std::optional<double> upper_bound_si;
    bool lower_inclusive{true};
    bool upper_inclusive{true};
    std::optional<double> lower_margin_si;
    std::optional<double> upper_margin_si;
    double limiting_margin_si{0.0};
    std::string limiting_bound;
    bool passed{false};
};

struct EngineeringAcceptanceSummary {
    bool passed{false};
    std::size_t passed_count{0};
    std::size_t failed_count{0};
    std::vector<EngineeringAcceptanceResult> criteria;
};

struct ResultSummary {
    std::string schema_version{result_summary_schema_v4};
    std::string mode;
    std::vector<ProjectedResultValue> values;
    std::optional<EngineeringAcceptanceSummary>
        engineering_acceptance;
};

class ResultProjectionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void validate_result_projections(
    const std::vector<ResultProjection>& projections);

void validate_engineering_acceptance_criteria(
    const std::vector<EngineeringAcceptanceCriterion>& criteria,
    const std::vector<ResultProjection>& projections);

EngineeringAcceptanceSummary evaluate_engineering_acceptance(
    const ResultSummary& summary,
    const std::vector<EngineeringAcceptanceCriterion>& criteria);

void validate_engineering_acceptance_summary(
    const EngineeringAcceptanceSummary& summary);

ResultSummary project_steady_result(
    const GraphResult& graph,
    const std::vector<ResultProjection>& projections);

ResultSummary project_transient_result(
    const std::vector<StateSample>& trajectory,
    const std::vector<EventValue>& events,
    const std::vector<ResultProjection>& projections);

}  // namespace thermox::service
