#pragma once

#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/safe_expression.hpp"

#include <memory>
#include <string>
#include <vector>

namespace thermox::platform {

inline constexpr char expression_component_schema_v5[] =
    "thermox.expression_component/v5";
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

struct ExpressionComponentEventActionDefinition {
    // set_state targets a local differential port/internal symbol and uses
    // expression. set_mode uses mode and targets the owning component.
    std::string type;
    std::string target;
    std::string expression;
    std::string mode;
};

struct ExpressionComponentEventDefinition {
    std::string name;
    std::string expression;
    std::string dimension{"dimensionless"};
    std::string direction{"any"};
    bool terminal{false};
    int priority{0};
    double hysteresis_si{0.0};
    std::vector<ExpressionComponentEventActionDefinition> actions;
};

struct ExpressionComponentDefinition {
    std::string schema_version{expression_component_schema_v5};
    ComponentModelDescriptor descriptor;
    std::vector<AlgebraicExpressionEquation> equations;
    // Version 5 transient residuals may reference ordinary port symbols,
    // internal.<name>, derivative.<port>.<variable>,
    // derivative.internal.<name>, parameters, and time.
    std::vector<AlgebraicExpressionEquation> transient_equations;
    // Mode-aware definitions replace the top-level equation
    // arrays with one fixed-structure equation set per named mode.
    std::vector<ExpressionComponentModeDefinition> modes;
    // Component-owned zero-crossing surfaces and atomic accepted-event
    // actions. Event/reset expressions use the transient state namespace but
    // cannot reference state rates.
    std::vector<ExpressionComponentEventDefinition> events;
};

std::shared_ptr<const ComponentModel>
make_expression_component_model(
    const ComponentRegistry& registry,
    ExpressionComponentDefinition definition);

void register_expression_component(
    ComponentRegistry& registry,
    ExpressionComponentDefinition definition);

}  // namespace thermox::platform
