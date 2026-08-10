#include "thermox/service/result_projection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace thermox::service {
namespace {

const ResultValue& named_value(
    const std::vector<ResultValue>& values,
    const ResultProjection& projection) {
    const auto found = std::find_if(
        values.begin(),
        values.end(),
        [&](const auto& value) {
            return value.name == projection.value_name;
        });
    if (found == values.end()) {
        throw ResultProjectionError(
            "result projection '" + projection.id +
            "' did not match value '" + projection.value_name +
            "'");
    }
    if (found->dimension != projection.dimension) {
        throw ResultProjectionError(
            "result projection '" + projection.id +
            "' expected dimension '" + projection.dimension +
            "' but matched '" + found->dimension + "'");
    }
    if (!std::isfinite(found->value_si)) {
        throw ResultProjectionError(
            "result projection '" + projection.id +
            "' matched a non-finite value");
    }
    return *found;
}

const ComponentResult& component(
    const GraphResult& graph,
    const ResultProjection& projection) {
    const auto found = std::find_if(
        graph.components.begin(),
        graph.components.end(),
        [&](const auto& value) {
            return value.component_id == projection.component_id;
        });
    if (found == graph.components.end()) {
        throw ResultProjectionError(
            "result projection '" + projection.id +
            "' did not match component '" +
            projection.component_id + "'");
    }
    return *found;
}

const PortResult& port(
    const ComponentResult& component_result,
    const ResultProjection& projection) {
    const auto found = std::find_if(
        component_result.ports.begin(),
        component_result.ports.end(),
        [&](const auto& value) {
            return value.port_name == projection.port_name;
        });
    if (found == component_result.ports.end()) {
        throw ResultProjectionError(
            "result projection '" + projection.id +
            "' did not match port '" + projection.port_name +
            "' on component '" + projection.component_id + "'");
    }
    return *found;
}

const ResultValue& resolve(
    const GraphResult& graph,
    const ResultProjection& projection) {
    switch (projection.scope) {
        case ResultValueScope::system_balance:
            return named_value(
                graph.system_balances, projection);
        case ResultValueScope::kpi:
            return named_value(graph.kpis, projection);
        case ResultValueScope::component_metric:
            return named_value(
                component(graph, projection).metrics,
                projection);
        case ResultValueScope::component_internal:
            return named_value(
                component(graph, projection).internal_values,
                projection);
        case ResultValueScope::port_primary:
            return named_value(
                port(component(graph, projection), projection)
                    .primary_values,
                projection);
        case ResultValueScope::port_derived:
            return named_value(
                port(component(graph, projection), projection)
                    .derived_values,
                projection);
    }
    throw ResultProjectionError(
        "result projection has an unsupported value scope");
}

bool requires_component(ResultValueScope scope) {
    return scope == ResultValueScope::component_metric ||
        scope == ResultValueScope::component_internal ||
        scope == ResultValueScope::port_primary ||
        scope == ResultValueScope::port_derived;
}

bool requires_port(ResultValueScope scope) {
    return scope == ResultValueScope::port_primary ||
        scope == ResultValueScope::port_derived;
}

}  // namespace

std::string to_string(ResultValueScope scope) {
    switch (scope) {
        case ResultValueScope::system_balance:
            return "system_balance";
        case ResultValueScope::kpi:
            return "kpi";
        case ResultValueScope::component_metric:
            return "component_metric";
        case ResultValueScope::component_internal:
            return "component_internal";
        case ResultValueScope::port_primary:
            return "port_primary";
        case ResultValueScope::port_derived:
            return "port_derived";
    }
    return "unknown";
}

ResultValueScope result_value_scope_from_string(
    const std::string& value) {
    if (value == "system_balance") {
        return ResultValueScope::system_balance;
    }
    if (value == "kpi") {
        return ResultValueScope::kpi;
    }
    if (value == "component_metric") {
        return ResultValueScope::component_metric;
    }
    if (value == "component_internal") {
        return ResultValueScope::component_internal;
    }
    if (value == "port_primary") {
        return ResultValueScope::port_primary;
    }
    if (value == "port_derived") {
        return ResultValueScope::port_derived;
    }
    throw ResultProjectionError(
        "unsupported result projection scope: " + value);
}

std::string to_string(ResultAggregation aggregation) {
    switch (aggregation) {
        case ResultAggregation::final:
            return "final";
        case ResultAggregation::minimum:
            return "minimum";
        case ResultAggregation::maximum:
            return "maximum";
    }
    return "unknown";
}

ResultAggregation result_aggregation_from_string(
    const std::string& value) {
    if (value == "final") {
        return ResultAggregation::final;
    }
    if (value == "minimum") {
        return ResultAggregation::minimum;
    }
    if (value == "maximum") {
        return ResultAggregation::maximum;
    }
    throw ResultProjectionError(
        "unsupported result projection aggregation: " + value);
}

void validate_result_projections(
    const std::vector<ResultProjection>& projections) {
    if (projections.size() > 256U) {
        throw ResultProjectionError(
            "a run configuration may define at most 256 result "
            "projections");
    }
    std::set<std::string> ids;
    for (const auto& projection : projections) {
        if (projection.id.empty() ||
            projection.value_name.empty() ||
            projection.dimension.empty()) {
            throw ResultProjectionError(
                "result projection ID, value name, and dimension "
                "must not be empty");
        }
        if (!ids.insert(projection.id).second) {
            throw ResultProjectionError(
                "result projection IDs must be unique");
        }
        const bool component_required =
            requires_component(projection.scope);
        const bool port_required = requires_port(projection.scope);
        if (projection.component_id.empty() ==
            component_required) {
            throw ResultProjectionError(
                "result projection '" + projection.id +
                "' has an invalid component selector");
        }
        if (projection.port_name.empty() == port_required) {
            throw ResultProjectionError(
                "result projection '" + projection.id +
                "' has an invalid port selector");
        }
    }
}

void validate_engineering_acceptance_criteria(
    const std::vector<EngineeringAcceptanceCriterion>& criteria,
    const std::vector<ResultProjection>& projections) {
    if (criteria.size() > 256U) {
        throw ResultProjectionError(
            "a Study may define at most 256 engineering "
            "acceptance criteria");
    }
    std::set<std::string> ids;
    for (const auto& criterion : criteria) {
        if (criterion.id.empty() ||
            criterion.projection_id.empty() ||
            criterion.dimension.empty()) {
            throw ResultProjectionError(
                "acceptance criterion ID, projection ID, and "
                "dimension must not be empty");
        }
        if (!ids.insert(criterion.id).second) {
            throw ResultProjectionError(
                "engineering acceptance criterion IDs must be unique");
        }
        const auto projection = std::find_if(
            projections.begin(), projections.end(),
            [&](const ResultProjection& candidate) {
                return candidate.id == criterion.projection_id;
            });
        if (projection == projections.end()) {
            throw ResultProjectionError(
                "acceptance criterion '" + criterion.id +
                "' references unknown result projection '" +
                criterion.projection_id + "'");
        }
        if (projection->dimension != criterion.dimension) {
            throw ResultProjectionError(
                "acceptance criterion '" + criterion.id +
                "' dimension does not match its result projection");
        }
        if (!criterion.lower_bound_si &&
            !criterion.upper_bound_si) {
            throw ResultProjectionError(
                "acceptance criterion '" + criterion.id +
                "' must define at least one bound");
        }
        if ((criterion.lower_bound_si &&
             !std::isfinite(*criterion.lower_bound_si)) ||
            (criterion.upper_bound_si &&
             !std::isfinite(*criterion.upper_bound_si))) {
            throw ResultProjectionError(
                "engineering acceptance bounds must be finite");
        }
        if (criterion.lower_bound_si &&
            criterion.upper_bound_si &&
            (*criterion.lower_bound_si >
                 *criterion.upper_bound_si ||
             (*criterion.lower_bound_si ==
                  *criterion.upper_bound_si &&
              (!criterion.lower_inclusive ||
               !criterion.upper_inclusive)))) {
            throw ResultProjectionError(
                "acceptance criterion '" + criterion.id +
                "' has an empty bound interval");
        }
    }
}

EngineeringAcceptanceSummary evaluate_engineering_acceptance(
    const ResultSummary& summary,
    const std::vector<EngineeringAcceptanceCriterion>& criteria) {
    EngineeringAcceptanceSummary acceptance;
    acceptance.passed = true;
    acceptance.criteria.reserve(criteria.size());
    for (const auto& criterion : criteria) {
        const auto value = std::find_if(
            summary.values.begin(), summary.values.end(),
            [&](const ProjectedResultValue& candidate) {
                return candidate.id == criterion.projection_id;
            });
        if (value == summary.values.end()) {
            throw ResultProjectionError(
                "acceptance criterion '" + criterion.id +
                "' cannot find projected result '" +
                criterion.projection_id + "'");
        }
        if (value->dimension != criterion.dimension) {
            throw ResultProjectionError(
                "acceptance criterion '" + criterion.id +
                "' matched a result with a different dimension");
        }
        const bool lower_passed = !criterion.lower_bound_si ||
            (criterion.lower_inclusive
                 ? value->value_si >= *criterion.lower_bound_si
                 : value->value_si > *criterion.lower_bound_si);
        const bool upper_passed = !criterion.upper_bound_si ||
            (criterion.upper_inclusive
                 ? value->value_si <= *criterion.upper_bound_si
                 : value->value_si < *criterion.upper_bound_si);
        const bool passed = lower_passed && upper_passed;
        const std::optional<double> lower_margin =
            criterion.lower_bound_si
            ? std::optional<double>{
                  value->value_si - *criterion.lower_bound_si}
            : std::nullopt;
        const std::optional<double> upper_margin =
            criterion.upper_bound_si
            ? std::optional<double>{
                  *criterion.upper_bound_si - value->value_si}
            : std::nullopt;
        const bool lower_limits = lower_margin &&
            (!upper_margin || *lower_margin <= *upper_margin);
        acceptance.criteria.push_back({
            criterion.id,
            criterion.projection_id,
            criterion.dimension,
            value->value_si,
            criterion.lower_bound_si,
            criterion.upper_bound_si,
            criterion.lower_inclusive,
            criterion.upper_inclusive,
            lower_margin,
            upper_margin,
            lower_limits ? *lower_margin : *upper_margin,
            lower_limits ? "lower" : "upper",
            passed,
        });
        acceptance.passed_count += passed ? 1U : 0U;
        acceptance.failed_count += passed ? 0U : 1U;
        acceptance.passed = acceptance.passed && passed;
    }
    validate_engineering_acceptance_summary(acceptance);
    return acceptance;
}

void validate_engineering_acceptance_summary(
    const EngineeringAcceptanceSummary& summary) {
    std::set<std::string> ids;
    std::size_t passed_count = 0U;
    for (const auto& result : summary.criteria) {
        if (result.criterion_id.empty() ||
            result.projection_id.empty() ||
            result.dimension.empty() ||
            !ids.insert(result.criterion_id).second ||
            !std::isfinite(result.actual_value_si) ||
            !std::isfinite(result.limiting_margin_si)) {
            throw ResultProjectionError(
                "engineering acceptance result identity or value is "
                "invalid");
        }
        const auto finite = [](const std::optional<double>& value) {
            return !value || std::isfinite(*value);
        };
        if (!finite(result.lower_bound_si) ||
            !finite(result.upper_bound_si) ||
            !finite(result.lower_margin_si) ||
            !finite(result.upper_margin_si) ||
            (!result.lower_bound_si && !result.upper_bound_si) ||
            result.lower_bound_si.has_value() !=
                result.lower_margin_si.has_value() ||
            result.upper_bound_si.has_value() !=
                result.upper_margin_si.has_value()) {
            throw ResultProjectionError(
                "engineering acceptance result bounds or margins are "
                "invalid");
        }
        const auto close = [](double left, double right) {
            const double scale = std::max(
                {1.0, std::abs(left), std::abs(right)});
            return std::abs(left - right) <=
                8.0 * std::numeric_limits<double>::epsilon() * scale;
        };
        if ((result.lower_bound_si &&
             !close(
                 *result.lower_margin_si,
                 result.actual_value_si - *result.lower_bound_si)) ||
            (result.upper_bound_si &&
             !close(
                 *result.upper_margin_si,
                 *result.upper_bound_si - result.actual_value_si))) {
            throw ResultProjectionError(
                "engineering acceptance result margin is inconsistent "
                "with its value and bound");
        }
        const bool lower_limits = result.lower_margin_si &&
            (!result.upper_margin_si ||
             *result.lower_margin_si <= *result.upper_margin_si);
        const std::string expected_bound =
            lower_limits ? "lower" : "upper";
        const double expected_margin = lower_limits
            ? *result.lower_margin_si : *result.upper_margin_si;
        const bool expected_pass =
            (!result.lower_margin_si ||
             (result.lower_inclusive
                  ? *result.lower_margin_si >= 0.0
                  : *result.lower_margin_si > 0.0)) &&
            (!result.upper_margin_si ||
             (result.upper_inclusive
                  ? *result.upper_margin_si >= 0.0
                  : *result.upper_margin_si > 0.0));
        if (result.limiting_bound != expected_bound ||
            !close(result.limiting_margin_si, expected_margin) ||
            result.passed != expected_pass) {
            throw ResultProjectionError(
                "engineering acceptance result limiting evidence or "
                "verdict is inconsistent");
        }
        passed_count += result.passed ? 1U : 0U;
    }
    const std::size_t failed_count =
        summary.criteria.size() - passed_count;
    if (summary.passed_count != passed_count ||
        summary.failed_count != failed_count ||
        summary.passed != (failed_count == 0U)) {
        throw ResultProjectionError(
            "engineering acceptance summary counts or verdict are "
            "inconsistent");
    }
}

ResultSummary project_steady_result(
    const GraphResult& graph,
    const std::vector<ResultProjection>& projections) {
    validate_result_projections(projections);
    ResultSummary summary;
    summary.mode = "steady";
    summary.values.reserve(projections.size());
    for (const auto& projection : projections) {
        if (projection.aggregation != ResultAggregation::final) {
            throw ResultProjectionError(
                "steady result projections only support final "
                "aggregation");
        }
        const auto& value = resolve(graph, projection);
        summary.values.push_back({
            projection.id,
            value.dimension,
            value.value_si,
            projection.aggregation,
            false,
            0.0,
        });
    }
    return summary;
}

ResultSummary project_transient_result(
    const std::vector<StateSample>& trajectory,
    const std::vector<ResultProjection>& projections) {
    validate_result_projections(projections);
    if (!projections.empty() && trajectory.empty()) {
        throw ResultProjectionError(
            "transient result projection requires a trajectory");
    }
    ResultSummary summary;
    summary.mode = "transient";
    summary.values.reserve(projections.size());
    for (const auto& projection : projections) {
        std::size_t selected = trajectory.size() - 1U;
        if (projection.aggregation != ResultAggregation::final) {
            for (std::size_t index = 0;
                 index < trajectory.size();
                 ++index) {
                const double candidate =
                    resolve(trajectory[index].graph, projection)
                        .value_si;
                const double current =
                    resolve(trajectory[selected].graph, projection)
                        .value_si;
                if ((projection.aggregation ==
                         ResultAggregation::minimum &&
                     candidate < current) ||
                    (projection.aggregation ==
                         ResultAggregation::maximum &&
                     candidate > current)) {
                    selected = index;
                }
            }
        }
        const auto& value =
            resolve(trajectory[selected].graph, projection);
        if (!std::isfinite(trajectory[selected].time)) {
            throw ResultProjectionError(
                "transient result projection selected a "
                "non-finite sample time");
        }
        summary.values.push_back({
            projection.id,
            value.dimension,
            value.value_si,
            projection.aggregation,
            true,
            trajectory[selected].time,
        });
    }
    return summary;
}

}  // namespace thermox::service
