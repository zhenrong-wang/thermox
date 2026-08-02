#pragma once

#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/safe_expression.hpp"

#include <memory>
#include <string>
#include <vector>

namespace thermox::platform {

inline constexpr char expression_component_schema_v2[] =
    "thermox.expression_component/v2";
inline constexpr char expression_component_artifact_type[] =
    "thermox.expression_component";

struct AlgebraicExpressionEquation {
    std::string name;
    std::string expression;
    double residual_scale{1.0};
};

struct ExpressionComponentDefinition {
    std::string schema_version{expression_component_schema_v2};
    ComponentModelDescriptor descriptor;
    std::vector<AlgebraicExpressionEquation> equations;
};

std::shared_ptr<const ComponentModel>
make_expression_component_model(
    const ComponentRegistry& registry,
    ExpressionComponentDefinition definition);

void register_expression_component(
    ComponentRegistry& registry,
    ExpressionComponentDefinition definition);

}  // namespace thermox::platform
