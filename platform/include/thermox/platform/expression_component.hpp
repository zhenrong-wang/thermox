#pragma once

#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/safe_expression.hpp"

#include <memory>
#include <string>
#include <vector>

namespace thermox::platform {

inline constexpr char expression_component_schema_v4[] =
    "thermox.expression_component/v4";
inline constexpr char expression_component_artifact_type[] =
    "thermox.expression_component";

struct AlgebraicExpressionEquation {
    std::string name;
    std::string expression;
    double residual_scale{1.0};
};

struct ExpressionComponentModeDefinition {
    std::string name;
    std::vector<AlgebraicExpressionEquation> equations;
    std::vector<AlgebraicExpressionEquation> transient_equations;
};

struct ExpressionComponentDefinition {
    std::string schema_version{expression_component_schema_v4};
    ComponentModelDescriptor descriptor;
    std::vector<AlgebraicExpressionEquation> equations;
    // Version 4 transient residuals may reference ordinary port symbols,
    // internal.<name>, derivative.<port>.<variable>,
    // derivative.internal.<name>, parameters, and time.
    std::vector<AlgebraicExpressionEquation> transient_equations;
    // Mode-aware definitions replace the top-level equation
    // arrays with one fixed-structure equation set per named mode.
    std::vector<ExpressionComponentModeDefinition> modes;
};

std::shared_ptr<const ComponentModel>
make_expression_component_model(
    const ComponentRegistry& registry,
    ExpressionComponentDefinition definition);

void register_expression_component(
    ComponentRegistry& registry,
    ExpressionComponentDefinition definition);

}  // namespace thermox::platform
