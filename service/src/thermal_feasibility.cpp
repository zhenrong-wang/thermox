#include "thermox/service/thermal_feasibility.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_set>
#include <utility>

namespace thermox::service {

namespace {

constexpr std::size_t maximum_checked_components = 512U;

const PortResult* find_port(
    const ComponentResult& component,
    const char* name) {
    const auto found = std::find_if(
        component.ports.begin(), component.ports.end(),
        [&](const auto& port) { return port.port_name == name; });
    return found == component.ports.end() ? nullptr : &*found;
}

double temperature_of(
    const ComponentResult& component,
    const PortResult& port) {
    const auto found = std::find_if(
        port.derived_values.begin(), port.derived_values.end(),
        [](const auto& value) { return value.name == "T"; });
    if (found == port.derived_values.end() ||
        found->dimension != "temperature" ||
        !std::isfinite(found->value_si)) {
        throw ThermalFeasibilityError(
            "component '" + component.component_id + "' port '" +
            port.port_name +
            "' lacks a finite derived temperature");
    }
    return found->value_si;
}

bool close(double left, double right) {
    const double scale = std::max(
        {1.0, std::abs(left), std::abs(right)});
    return std::abs(left - right) <=
        64.0 * std::numeric_limits<double>::epsilon() * scale;
}

}  // namespace

ThermalFeasibilitySummary audit_counterflow_thermal_feasibility(
    const GraphResult& graph,
    double required_minimum_approach_k) {
    if (!std::isfinite(required_minimum_approach_k) ||
        required_minimum_approach_k < 0.0) {
        throw ThermalFeasibilityError(
            "required minimum counterflow approach must be finite "
            "and nonnegative");
    }

    ThermalFeasibilitySummary summary;
    summary.scope = "steady";
    summary.required_minimum_approach_k =
        required_minimum_approach_k;
    for (const auto& component : graph.components) {
        const auto* hot_in = find_port(component, "hot_in");
        const auto* hot_out = find_port(component, "hot_out");
        const auto* cold_in = find_port(component, "cold_in");
        const auto* cold_out = find_port(component, "cold_out");
        const std::size_t matching_ports =
            static_cast<std::size_t>(hot_in != nullptr) +
            static_cast<std::size_t>(hot_out != nullptr) +
            static_cast<std::size_t>(cold_in != nullptr) +
            static_cast<std::size_t>(cold_out != nullptr);
        if (matching_ports != 4U) continue;
        if (summary.counterflow_approaches.size() >=
            maximum_checked_components) {
            throw ThermalFeasibilityError(
                "counterflow thermal-feasibility audit exceeds 512 "
                "components");
        }

        CounterflowApproachResult result;
        result.component_id = component.component_id;
        result.component_kind = component.kind;
        result.hot_in_minus_cold_out_k =
            temperature_of(component, *hot_in) -
            temperature_of(component, *cold_out);
        result.hot_out_minus_cold_in_k =
            temperature_of(component, *hot_out) -
            temperature_of(component, *cold_in);
        result.minimum_approach_k = std::min(
            result.hot_in_minus_cold_out_k,
            result.hot_out_minus_cold_in_k);
        result.passed =
            result.minimum_approach_k >= required_minimum_approach_k;
        summary.counterflow_approaches.push_back(std::move(result));
    }

    summary.checked_count = summary.counterflow_approaches.size();
    summary.passed_count = static_cast<std::size_t>(std::count_if(
        summary.counterflow_approaches.begin(),
        summary.counterflow_approaches.end(),
        [](const auto& result) { return result.passed; }));
    summary.failed_count = summary.checked_count - summary.passed_count;
    summary.passed = summary.failed_count == 0U;
    validate_thermal_feasibility_summary(summary);
    return summary;
}

ThermalFeasibilitySummary audit_counterflow_thermal_feasibility(
    const std::vector<StateSample>& trajectory,
    double required_minimum_approach_k) {
    if (trajectory.empty()) {
        throw ThermalFeasibilityError(
            "counterflow trajectory audit requires at least one sample");
    }
    ThermalFeasibilitySummary summary;
    summary.scope = "trajectory";
    summary.required_minimum_approach_k =
        required_minimum_approach_k;
    std::map<std::string, CounterflowApproachResult> worst;
    for (const auto& sample : trajectory) {
        if (!std::isfinite(sample.time)) {
            throw ThermalFeasibilityError(
                "counterflow trajectory sample time must be finite");
        }
        const auto snapshot = audit_counterflow_thermal_feasibility(
            sample.graph, required_minimum_approach_k);
        for (auto result : snapshot.counterflow_approaches) {
            result.has_sample_time = true;
            result.sample_time = sample.time;
            const auto found = worst.find(result.component_id);
            if (found == worst.end()) {
                worst.emplace(result.component_id, std::move(result));
            } else {
                if (found->second.component_kind != result.component_kind) {
                    throw ThermalFeasibilityError(
                        "counterflow component kind changes across "
                        "trajectory for '" + result.component_id + "'");
                }
                if (result.minimum_approach_k <
                    found->second.minimum_approach_k) {
                    found->second = std::move(result);
                }
            }
        }
    }
    summary.counterflow_approaches.reserve(worst.size());
    for (auto& [component_id, result] : worst) {
        (void)component_id;
        summary.counterflow_approaches.push_back(std::move(result));
    }
    summary.checked_count = summary.counterflow_approaches.size();
    summary.passed_count = static_cast<std::size_t>(std::count_if(
        summary.counterflow_approaches.begin(),
        summary.counterflow_approaches.end(),
        [](const auto& result) { return result.passed; }));
    summary.failed_count = summary.checked_count - summary.passed_count;
    summary.passed = summary.failed_count == 0U;
    validate_thermal_feasibility_summary(summary);
    return summary;
}

void validate_thermal_feasibility_summary(
    const ThermalFeasibilitySummary& summary) {
    if (summary.schema_version != thermal_feasibility_schema_v1) {
        throw ThermalFeasibilityError(
            "unsupported thermal-feasibility schema: " +
            summary.schema_version);
    }
    if (summary.scope != "steady" && summary.scope != "trajectory") {
        throw ThermalFeasibilityError(
            "thermal-feasibility scope must be steady or trajectory");
    }
    if (!std::isfinite(summary.required_minimum_approach_k) ||
        summary.required_minimum_approach_k < 0.0) {
        throw ThermalFeasibilityError(
            "thermal-feasibility minimum approach is invalid");
    }
    if (summary.counterflow_approaches.size() >
        maximum_checked_components ||
        summary.checked_count !=
            summary.counterflow_approaches.size() ||
        summary.passed_count + summary.failed_count !=
            summary.checked_count ||
        summary.passed != (summary.failed_count == 0U)) {
        throw ThermalFeasibilityError(
            "thermal-feasibility aggregate counts are inconsistent");
    }

    std::unordered_set<std::string> identities;
    std::size_t passed_count = 0U;
    for (const auto& result : summary.counterflow_approaches) {
        if (result.component_id.empty() || result.component_kind.empty() ||
            !identities.insert(result.component_id).second) {
            throw ThermalFeasibilityError(
                "thermal-feasibility component identity is invalid");
        }
        if (!std::isfinite(result.hot_in_minus_cold_out_k) ||
            !std::isfinite(result.hot_out_minus_cold_in_k) ||
            !std::isfinite(result.minimum_approach_k) ||
            !close(
                result.minimum_approach_k,
                std::min(result.hot_in_minus_cold_out_k,
                         result.hot_out_minus_cold_in_k)) ||
            result.passed !=
                (result.minimum_approach_k >=
                 summary.required_minimum_approach_k)) {
            throw ThermalFeasibilityError(
                "thermal-feasibility result for component '" +
                result.component_id + "' is inconsistent");
        }
        if (result.has_sample_time != (summary.scope == "trajectory") ||
            (result.has_sample_time &&
             !std::isfinite(result.sample_time))) {
            throw ThermalFeasibilityError(
                "thermal-feasibility sample attribution for component '" +
                result.component_id + "' is inconsistent");
        }
        if (result.passed) ++passed_count;
    }
    if (passed_count != summary.passed_count) {
        throw ThermalFeasibilityError(
            "thermal-feasibility verdict distribution is inconsistent");
    }
}

void append_counterflow_thermal_metrics(
    GraphResult& graph,
    const ThermalFeasibilitySummary& summary) {
    validate_thermal_feasibility_summary(summary);
    if (summary.scope != "steady") {
        throw ThermalFeasibilityError(
            "component thermal metrics require a steady snapshot "
            "summary");
    }
    for (const auto& result : summary.counterflow_approaches) {
        const auto component = std::find_if(
            graph.components.begin(), graph.components.end(),
            [&](const auto& candidate) {
                return candidate.component_id == result.component_id;
            });
        if (component == graph.components.end() ||
            component->kind != result.component_kind) {
            throw ThermalFeasibilityError(
                "thermal-feasibility result references an unknown "
                "component: " + result.component_id);
        }
        for (const auto* name : {
                 "counterflow_hot_in_minus_cold_out",
                 "counterflow_hot_out_minus_cold_in",
                 "counterflow_minimum_approach"}) {
            if (std::any_of(
                    component->metrics.begin(), component->metrics.end(),
                    [&](const auto& metric) {
                        return metric.name == name;
                    })) {
                throw ThermalFeasibilityError(
                    "component '" + result.component_id +
                    "' already owns thermal-feasibility metric '" +
                    name + "'");
            }
        }
        component->metrics.push_back({
            "counterflow_hot_in_minus_cold_out",
            "temperature_difference",
            result.hot_in_minus_cold_out_k});
        component->metrics.push_back({
            "counterflow_hot_out_minus_cold_in",
            "temperature_difference",
            result.hot_out_minus_cold_in_k});
        component->metrics.push_back({
            "counterflow_minimum_approach", "temperature_difference",
            result.minimum_approach_k});
    }
}

}  // namespace thermox::service
