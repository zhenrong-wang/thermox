#pragma once

#include "thermox/platform/component_registry.hpp"

#include <memory>
#include <string>
#include <vector>

namespace thermox::platform {

struct ResultValue {
    std::string name;
    std::string dimension;
    double value_si{0.0};
    bool has_derivative{false};
    double derivative_si_s{0.0};
};

struct PortResult {
    std::string port_name;
    std::string domain;
    std::string medium_id;
    std::string phase;
    std::vector<ResultValue> primary_values;
    std::vector<ResultValue> derived_values;
};

struct ComponentResult {
    std::string component_id;
    std::string kind;
    std::vector<PortResult> ports;
    std::vector<ResultValue> internal_values;
    std::vector<ResultValue> metrics;
};

struct GraphResult {
    std::vector<ComponentResult> components;
    std::vector<ResultValue> system_balances;
    std::vector<ResultValue> kpis;
};

class GraphResultEvaluator {
public:
    GraphResultEvaluator(
        const ModelDocument& document,
        const CompiledModelGraph& graph,
        const physics::PropertyPackageRegistry& property_registry);
    GraphResultEvaluator(
        const ModelDocument& document,
        const CompiledTransientModelGraph& graph,
        const physics::PropertyPackageRegistry& property_registry);
    ~GraphResultEvaluator();
    GraphResultEvaluator(GraphResultEvaluator&&) noexcept;
    GraphResultEvaluator& operator=(GraphResultEvaluator&&) noexcept;
    GraphResultEvaluator(const GraphResultEvaluator&) = delete;
    GraphResultEvaluator& operator=(const GraphResultEvaluator&) = delete;

    [[nodiscard]] GraphResult evaluate(
        const std::vector<double>& state,
        const std::vector<double>& derivative = {}) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace thermox::platform
