#pragma once

#include "thermox/platform/correlation.hpp"
#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/regime_map.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace thermox::platform::component_model_support {

template <typename Result, typename Evaluate>
std::function<EvaluationStatus(const std::vector<double>&, Result&)>
memoize_component_evaluation(
    Evaluate evaluate,
    std::vector<std::size_t> dependencies,
    std::size_t capacity = 16U) {
    // Multi-output physical closures are lowered to several scalar graph
    // equations. A small exact-key cache prevents each row and each sparse
    // finite-difference column from repeating the same property calculation.
    struct Entry {
        std::vector<double> key;
        EvaluationStatus status;
        Result result;
    };
    struct Cache {
        std::mutex mutex;
        std::vector<Entry> entries;
    };
    auto cache = std::make_shared<Cache>();
    return [evaluate = std::move(evaluate),
            dependencies = std::move(dependencies), cache,
            capacity](const std::vector<double>& x, Result& result) {
        std::vector<double> key;
        key.reserve(dependencies.size());
        for (const auto dependency : dependencies)
            key.push_back(x.at(dependency));
        {
            const std::lock_guard lock{cache->mutex};
            const auto found = std::find_if(
                cache->entries.begin(), cache->entries.end(),
                [&](const Entry& entry) { return entry.key == key; });
            if (found != cache->entries.end()) {
                result = found->result;
                return found->status;
            }
        }
        Result evaluated_result;
        const auto status = evaluate(x, evaluated_result);
        {
            const std::lock_guard lock{cache->mutex};
            if (capacity > 0U) {
                if (cache->entries.size() >= capacity)
                    cache->entries.erase(cache->entries.begin());
                cache->entries.push_back(
                    {std::move(key), status, evaluated_result});
            }
        }
        result = std::move(evaluated_result);
        return status;
    };
}

inline std::size_t add_numeric_checked_sparse_equation(
    EquationSystemBuilder& system,
    std::string name,
    CheckedEquationCallback evaluate,
    std::vector<std::size_t> variables,
    double scale) {
    // Preserve recoverable domain failures while declaring the real graph
    // incidence instead of forcing the global solver to assume a dense row.
    std::sort(variables.begin(), variables.end());
    variables.erase(
        std::unique(variables.begin(), variables.end()), variables.end());
    const auto assemble =
        [evaluate, variables](
            const std::vector<double>& x,
            std::vector<EquationPartial>& jacobian) {
            double base = 0.0;
            const auto base_status = evaluate(x, base);
            if (!base_status.ok())
                throw std::runtime_error(base_status.message);
            for (const auto variable : variables) {
                const double step = 1.0e-6 *
                    std::max(std::abs(x.at(variable)), 1.0);
                auto perturbed = x;
                perturbed.at(variable) += step;
                double shifted = 0.0;
                auto status = evaluate(perturbed, shifted);
                double derivative = 0.0;
                if (status.ok()) {
                    derivative = (shifted - base) / step;
                } else {
                    perturbed.at(variable) = x.at(variable) - step;
                    status = evaluate(perturbed, shifted);
                    if (!status.ok())
                        throw std::runtime_error(status.message);
                    derivative = (base - shifted) / step;
                }
                jacobian.push_back({variable, derivative});
            }
            return base;
        };
    return system.add_checked_sparse_equation(
        std::move(name), std::move(evaluate), std::move(variables),
        assemble, scale);
}

inline double required_parameter(
    const ComponentDefinition& component,
    const std::string& name) {
    const auto it = component.parameters.find(name);
    if (it == component.parameters.end()) {
        throw std::invalid_argument(
            "component '" + component.id +
            "' is missing required parameter: " + name);
    }
    return it->second.value_si;
}

inline double parameter_or(
    const ComponentDefinition& component,
    const std::string& name,
    double default_value) {
    const auto it = component.parameters.find(name);
    return it == component.parameters.end()
        ? default_value
        : it->second.value_si;
}

inline std::size_t require_port_variable(
    const ComponentCompileContext& context,
    const std::string& key) {
    const auto it = context.port_variables.find(key);
    if (it == context.port_variables.end()) {
        throw std::logic_error(
            "compiled component variable missing: " +
            context.component.id + "." + key);
    }
    return it->second;
}

inline std::size_t require_internal_variable(
    const ComponentCompileContext& context,
    const std::string& name) {
    const auto it = context.internal_variables.find(name);
    if (it == context.internal_variables.end()) {
        throw std::logic_error(
            "compiled component internal variable missing: " +
            context.component.id + "." + name);
    }
    return it->second;
}

inline const std::vector<std::string>& require_port_species(
    const ComponentCompileContext& context,
    const std::string& port) {
    const auto it = context.port_species.find(port);
    if (it == context.port_species.end()) {
        throw std::logic_error(
            "compiled material species basis missing: " +
            context.component.id + "." + port);
    }
    return it->second;
}

inline std::shared_ptr<const physics::PropertyPackage>
require_property_package(
    const ComponentCompileContext& context,
    const std::string& port) {
    const auto it = context.port_properties.find(port);
    if (it == context.port_properties.end() || !it->second) {
        throw std::logic_error(
            "compiled property package missing: " +
            context.component.id + "." + port);
    }
    return it->second;
}

inline std::shared_ptr<const physics::ThermochemistryPackage>
require_thermochemistry_package(
    const ComponentCompileContext& context,
    const std::string& port) {
    const auto it = context.port_thermochemistry.find(port);
    if (it == context.port_thermochemistry.end() || !it->second) {
        throw std::logic_error(
            "compiled thermochemistry package missing: " +
            context.component.id + "." + port);
    }
    return it->second;
}

inline std::shared_ptr<const PerformanceMapArtifact>
require_performance_map(
    const ComponentCompileContext& context,
    const std::string& role) {
    const auto it = context.artifacts.find(role);
    if (it == context.artifacts.end() || !it->second) {
        throw std::logic_error(
            "compiled performance-map artifact missing: " +
            context.component.id + "." + role);
    }
    if (it->second->artifact_type() !=
        performance_map_artifact_type) {
        throw std::logic_error(
            "compiled artifact has wrong type for performance-map role: " +
            context.component.id + "." + role);
    }
    const auto map = std::dynamic_pointer_cast<
        const PerformanceMapArtifact>(it->second);
    if (!map) {
        throw std::logic_error(
            "compiled performance-map artifact has incompatible payload: " +
            context.component.id + "." + role);
    }
    return map;
}

inline std::shared_ptr<const CorrelationArtifact>
require_correlation(
    const ComponentCompileContext& context,
    const std::string& role) {
    const auto it = context.artifacts.find(role);
    if (it == context.artifacts.end() || !it->second) {
        throw std::logic_error(
            "compiled correlation artifact missing: " +
            context.component.id + "." + role);
    }
    if (it->second->artifact_type() != correlation_artifact_type) {
        throw std::logic_error(
            "compiled artifact has wrong type for correlation role: " +
            context.component.id + "." + role);
    }
    const auto correlation = std::dynamic_pointer_cast<
        const CorrelationArtifact>(it->second);
    if (!correlation) {
        throw std::logic_error(
            "compiled correlation artifact has incompatible payload: " +
            context.component.id + "." + role);
    }
    return correlation;
}

inline std::shared_ptr<const RegimeMapArtifact>
optional_regime_map(
    const ComponentCompileContext& context,
    const std::string& role) {
    const auto it = context.artifacts.find(role);
    if (it == context.artifacts.end()) return nullptr;
    if (!it->second ||
        it->second->artifact_type() != regime_map_artifact_type) {
        throw std::logic_error(
            "compiled artifact has wrong type for regime-map role: " +
            context.component.id + "." + role);
    }
    const auto map = std::dynamic_pointer_cast<
        const RegimeMapArtifact>(it->second);
    if (!map) {
        throw std::logic_error(
            "compiled regime-map artifact has incompatible payload: " +
            context.component.id + "." + role);
    }
    return map;
}

inline EvaluationStatus property_failure(
    const physics::PropertyResult& result) {
    if (result.status == physics::PropertyStatus::backend_error) {
        return EvaluationStatus::fatal(result.message);
    }
    return EvaluationStatus::recoverable(result.message);
}

inline EvaluationStatus property_failure(
    const physics::SaturationResult& result) {
    if (result.status == physics::PropertyStatus::backend_error) {
        return EvaluationStatus::fatal(result.message);
    }
    return EvaluationStatus::recoverable(result.message);
}

inline EvaluationStatus property_failure(
    const physics::PhDerivativesResult& result) {
    if (result.status == physics::PropertyStatus::backend_error) {
        return EvaluationStatus::fatal(result.message);
    }
    return EvaluationStatus::recoverable(result.message);
}

}  // namespace thermox::platform::component_model_support
