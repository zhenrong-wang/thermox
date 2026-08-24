#include "thermox/nonlinear_solver.hpp"
#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/expression_component.hpp"
#include "thermox/platform/model_document.hpp"
#include "thermox/transient_solver.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Callback>
void require_invalid(
    Callback&& callback,
    const std::string& expected) {
    try {
        callback();
    } catch (const std::invalid_argument& error) {
        require(
            std::string{error.what()}.find(expected) !=
                std::string::npos,
            "unexpected validation message: " +
                std::string{error.what()});
        return;
    }
    throw std::runtime_error(
        "expected invalid_argument containing: " + expected);
}

thermox::platform::ExpressionComponentDefinition
pressure_loss_definition(std::string pressure_expression) {
    thermox::platform::ExpressionComponentDefinition definition;
    definition.descriptor.kind =
        "custom.fluid.pressure_loss";
    definition.descriptor.version = "1.0.0";
    definition.descriptor.template_kind =
        "custom.fluid.pressure_loss";
    definition.descriptor.display_name = "Custom pressure loss";
    definition.descriptor.category = "Project components";
    definition.descriptor.model_name = "Custom expression";
    definition.descriptor.ports = {
        {"inlet", "fluid", "in"},
        {"outlet", "fluid", "out"},
    };
    definition.descriptor.parameters = {
        {
            "pressure_ratio",
            "dimensionless",
            true,
            std::nullopt,
            0.0,
            1.0,
            false,
            true,
        },
    };
    definition.equations = {
        {
            "mass_balance",
            "outlet.m_dot - inlet.m_dot",
            10.0,
        },
        {
            "pressure_law",
            std::move(pressure_expression),
            100000.0,
        },
        {
            "enthalpy_balance",
            "outlet.h - inlet.h",
            100000.0,
        },
    };
    return definition;
}

void test_expression_component_compiles_and_solves() {
    auto registry =
        thermox::platform::make_default_component_registry();
    thermox::platform::register_expression_component(
        registry,
        pressure_loss_definition(
            "outlet.p - inlet.p * "
            "parameter.pressure_ratio"));

    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "custom_pressure_loss",
    "media": [{
      "id": "air",
      "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "components": [{
      "id": "duct",
      "kind": "custom.fluid.pressure_loss",
      "media": {
        "inlet": "air",
        "outlet": "air"
      },
      "parameters": {
        "pressure_ratio": 0.9
      }
    }],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "duct.inlet.m_dot": {
        "value": 12.0,
        "unit": "kg/s"
      },
      "duct.inlet.p": {
        "value": 2.0,
        "unit": "bar"
      },
      "duct.inlet.h": {
        "value": 300.0,
        "unit": "kJ/kg"
      }
    }
  }]
})json");
    const auto graph =
        thermox::platform::compile_model_graph(
            document, registry, "design");
    require(
        graph.problem.sparse_jacobian_pattern.has_value(),
        "custom equations declare a fixed sparse pattern");
    const auto derivative_check =
        thermox::verify_problem_jacobian(graph.problem);
    require(
        derivative_check.passed,
        "safe expressions provide consistent analytic derivatives");

    const auto solved = thermox::solve_newton(graph.problem);
    require(
        solved.diagnostics.converged,
        "custom pressure-loss component converges");
    const auto index_of = [&](const std::string& name) {
        for (std::size_t index = 0;
             index < graph.problem.variable_names.size();
             ++index) {
            if (graph.problem.variable_names[index] == name) {
                return index;
            }
        }
        throw std::runtime_error(
            "missing solved variable: " + name);
    };
    require(
        std::abs(
            solved.x.at(index_of("duct.outlet.m_dot")) -
            12.0) < 1.0e-10,
        "custom mass equation is solved");
    require(
        std::abs(
            solved.x.at(index_of("duct.outlet.p")) -
            180000.0) < 1.0e-6,
        "custom pressure equation is solved");
    require(
        std::abs(
            solved.x.at(index_of("duct.outlet.h")) -
            300000.0) < 1.0e-6,
        "custom enthalpy equation is solved");
}

void test_expression_contract_rejects_unsafe_or_unknown_inputs() {
    auto registry =
        thermox::platform::make_default_component_registry();
    auto missing_template = pressure_loss_definition(
        "outlet.p - inlet.p");
    missing_template.descriptor.template_kind.clear();
    require_invalid(
        [&]() {
            thermox::platform::register_expression_component(
                registry, std::move(missing_template));
        },
        "requires physical template kind");
    require_invalid(
        [&]() {
            thermox::platform::register_expression_component(
                registry,
                pressure_loss_definition(
                    "system(outlet.p)"));
        },
        "unknown function 'system'");
    require_invalid(
        [&]() {
            thermox::platform::register_expression_component(
                registry,
                pressure_loss_definition(
                    "outlet.p - secret.value"));
        },
        "unknown symbol: secret.value");
    require_invalid(
        [&]() {
            thermox::platform::register_expression_component(
                registry,
                pressure_loss_definition(
                    "outlet.p - inlet.m_dot"));
        },
        "addition and subtraction require compatible dimensions");
    require_invalid(
        [&]() {
            thermox::platform::register_expression_component(
                registry,
                pressure_loss_definition("exp(inlet.p)"));
        },
        "exp and log require a dimensionless argument");
    require_invalid(
        [&]() {
            thermox::platform::register_expression_component(
                registry,
                pressure_loss_definition(
                    "pow(inlet.p, parameter.pressure_ratio)"));
        },
        "pow of a dimensioned value requires a constant exponent");
    require_invalid(
        [&]() {
            thermox::platform::register_expression_component(
                registry,
                pressure_loss_definition(
                    "outlet.p + " + std::string(4096, '1')));
        },
        "exceeds the 4096-character limit");

    auto material =
        pressure_loss_definition(
            "outlet.p - inlet.p");
    material.descriptor.kind = "custom.material.unsupported";
    material.descriptor.ports = {
        {"inlet", "material", "in"},
        {"outlet", "material", "out"},
    };
    require_invalid(
        [&]() {
            thermox::platform::register_expression_component(
                registry, std::move(material));
        },
        "does not support species-expanded connector variables");

    auto shaft_power = pressure_loss_definition(
        "outlet.p - inlet.p");
    shaft_power.descriptor.kind = "custom.fluid.shaft_power";
    shaft_power.descriptor.ports.push_back(
        {"shaft", "shaft", "out"});
    shaft_power.equations.push_back({
        "power_balance",
        "shaft.W_dot - inlet.m_dot * inlet.h", 1.0});
    thermox::platform::register_expression_component(
        registry, std::move(shaft_power));

    auto zero_pressure = pressure_loss_definition(
        "outlet.p - 0");
    zero_pressure.descriptor.kind = "custom.fluid.zero_pressure";
    thermox::platform::register_expression_component(
        registry, std::move(zero_pressure));
}

void test_expression_implementation_identity_covers_equations() {
    auto registry =
        thermox::platform::make_default_component_registry();
    const auto first = thermox::platform::
        make_expression_component_model(
            registry,
            pressure_loss_definition(
                "outlet.p - inlet.p * "
                "parameter.pressure_ratio"));
    const auto second = thermox::platform::
        make_expression_component_model(
            registry,
            pressure_loss_definition(
                "outlet.p / inlet.p - "
                "parameter.pressure_ratio"));
    require(
        !first->implementation_fingerprint().empty() &&
            first->implementation_fingerprint() !=
                second->implementation_fingerprint(),
        "equation content participates in implementation identity");
}

void test_transient_expression_component_integrates_internal_state() {
    auto registry = thermox::platform::make_default_component_registry();
    thermox::platform::ExpressionComponentDefinition definition;
    definition.schema_version =
        thermox::platform::expression_component_schema_v4;
    definition.descriptor.kind = "custom.signal.first_order_lag";
    definition.descriptor.version = "1.0.0";
    definition.descriptor.template_kind = "control.first_order_lag";
    definition.descriptor.display_name = "First-order lag";
    definition.descriptor.category = "Project controls";
    definition.descriptor.model_name = "Safe transient expression";
    definition.descriptor.supports_steady = false;
    definition.descriptor.supports_transient = true;
    definition.descriptor.ports = {
        {"input", "signal", "in"},
        {"output", "signal", "out"},
    };
    definition.descriptor.parameters = {
        {"time_constant", "time", true, std::nullopt, 0.0,
         std::numeric_limits<double>::infinity(), false, true},
    };
    definition.descriptor.internal_variables = {
        {"filtered", thermox::DaeVariableKind::differential,
         0.0, 1.0, 0.0, 1.0,
         -std::numeric_limits<double>::infinity(),
         std::numeric_limits<double>::infinity(), "dimensionless"},
    };
    definition.transient_equations = {
        {"state_balance",
         "parameter.time_constant * derivative.internal.filtered + "
         "internal.filtered - input.value", 1.0},
        {"output", "output.value - internal.filtered", 1.0},
    };
    thermox::platform::register_expression_component(
        registry, std::move(definition));

    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "transient_expression",
    "media": [],
    "components": [{
      "id": "lag",
      "kind": "custom.signal.first_order_lag",
      "parameters": {"time_constant": {"value": 2.0, "unit": "s"}}
    }],
    "connections": []
  },
  "cases": [{
    "id": "step",
    "mode": "dynamic_transient",
    "fixed_values": {"lag.input.value": 1.0},
    "initial_guesses": {"lag.filtered": 0.0}
  }]
})json");
    const auto graph = thermox::platform::compile_transient_model_graph(
        document, registry, "step");
    require(graph.problem.sparse_jacobian_pattern.has_value(),
            "transient expressions declare a fixed sparse DAE pattern");

    thermox::TimeIntegrationOptions options;
    options.end_time = 2.0;
    options.initial_step = 0.1;
    options.max_step = 0.2;
    const auto result = thermox::integrate_dae(graph.problem, options);
    require(result.diagnostics.success, result.diagnostics.message);
    const auto filtered = [&]() {
        for (std::size_t i = 0; i < graph.problem.variable_names.size(); ++i) {
            if (graph.problem.variable_names[i] == "lag.filtered") return i;
        }
        throw std::runtime_error("missing lag.filtered");
    }();
    const double expected = 1.0 - std::exp(-1.0);
    require(std::abs(result.trajectory.back().state.at(filtered) - expected) <
                2.0e-2,
            "safe transient expression integrates its declared state");
}

void test_transient_expression_validation_rejects_unknown_symbols() {
    auto registry = thermox::platform::make_default_component_registry();
    thermox::platform::ExpressionComponentDefinition definition;
    definition.schema_version =
        thermox::platform::expression_component_schema_v4;
    definition.descriptor.kind = "custom.signal.invalid_dynamic";
    definition.descriptor.version = "1.0.0";
    definition.descriptor.template_kind = "control.invalid";
    definition.descriptor.display_name = "Invalid dynamic component";
    definition.descriptor.category = "Project controls";
    definition.descriptor.model_name = "Safe transient expression";
    definition.descriptor.supports_steady = false;
    definition.descriptor.supports_transient = true;
    definition.descriptor.ports = {{"output", "signal", "out"}};
    definition.transient_equations = {
        {"invalid", "output.value - secret.state", 1.0}};
    auto algebraic_rate = definition;
    require_invalid(
        [&]() {
            thermox::platform::register_expression_component(
                registry, std::move(definition));
        },
        "references unknown symbol: secret.state");

    algebraic_rate.descriptor.kind =
        "custom.signal.invalid_algebraic_rate";
    algebraic_rate.descriptor.internal_variables = {{
        "state", thermox::DaeVariableKind::algebraic,
        0.0, 1.0, 0.0, 1.0,
        -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(), "dimensionless"}};
    algebraic_rate.transient_equations = {{
        "invalid_rate", "output.value - derivative.internal.state", 1.0}};
    auto dimension_mismatch = algebraic_rate;
    dimension_mismatch.descriptor.kind =
        "custom.signal.invalid_rate_dimension";
    dimension_mismatch.descriptor.internal_variables.front().kind =
        thermox::DaeVariableKind::differential;
    dimension_mismatch.transient_equations = {{
        "invalid_dimension",
        "derivative.internal.state + output.value", 1.0}};
    require_invalid(
        [&]() {
            thermox::platform::register_expression_component(
                registry, std::move(algebraic_rate));
        },
        "references unknown symbol: derivative.internal.state");
    require_invalid(
        [&]() {
            thermox::platform::register_expression_component(
                registry, std::move(dimension_mismatch));
        },
        "addition and subtraction require compatible dimensions");
}

void test_mode_aware_expression_component_switches_fixed_structure() {
    auto registry = thermox::platform::make_default_component_registry();
    thermox::platform::ExpressionComponentDefinition definition;
    definition.schema_version =
        thermox::platform::expression_component_schema_v4;
    definition.descriptor.kind = "custom.signal.mode_lag";
    definition.descriptor.version = "1.0.0";
    definition.descriptor.template_kind = "control.first_order_lag";
    definition.descriptor.display_name = "Mode-aware lag";
    definition.descriptor.category = "Project controls";
    definition.descriptor.model_name = "Safe mode equations";
    definition.descriptor.supports_steady = true;
    definition.descriptor.supports_transient = true;
    definition.descriptor.default_mode = "tracking";
    definition.descriptor.ports = {
        {"input", "signal", "in"},
        {"output", "signal", "out"},
    };
    definition.descriptor.parameters = {
        {"time_constant", "time", true, std::nullopt, 0.0,
         std::numeric_limits<double>::infinity(), false, true},
    };
    definition.descriptor.internal_variables = {{
        "filtered", thermox::DaeVariableKind::differential,
        0.0, 1.0, 0.0, 1.0,
        -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(), "dimensionless"}};
    definition.modes = {
        {
            "tracking",
            {{"steady_output", "output.value - input.value", 1.0}},
            {
                {"state_balance",
                 "parameter.time_constant * "
                 "derivative.internal.filtered + internal.filtered - "
                 "input.value", 1.0},
                {"output", "output.value - internal.filtered", 1.0},
            },
        },
        {
            "failsafe",
            {{"steady_output", "output.value - 0 * input.value", 1.0}},
            {
                {"state_balance",
                 "parameter.time_constant * "
                 "derivative.internal.filtered + internal.filtered - "
                 "0 * input.value", 1.0},
                {"output", "output.value - internal.filtered", 1.0},
            },
        },
    };
    thermox::platform::register_expression_component(
        registry, std::move(definition));

    const auto document = thermox::platform::parse_model_document_text(
        R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "mode_expression",
    "media": [],
    "components": [{
      "id": "lag",
      "kind": "custom.signal.mode_lag",
      "parameters": {"time_constant": {"value": 2.0, "unit": "s"}}
    }],
    "connections": []
  },
  "cases": [{
    "id": "steady_failsafe",
    "mode": "steady_state_design",
    "component_modes": {"lag": "failsafe"},
    "fixed_values": {"lag.input.value": 1.0}
  }, {
    "id": "transient_trip",
    "mode": "dynamic_transient",
    "component_modes": {"lag": "tracking"},
    "fixed_values": {"lag.input.value": 1.0},
    "initial_guesses": {"lag.filtered": 0.0},
    "state_events": [{
      "id": "failsafe",
      "target": "lag.filtered",
      "threshold": 0.2,
      "direction": "rising",
      "terminal": false,
      "actions": [{
        "type": "set_mode",
        "target": "lag",
        "mode": "failsafe"
      }]
    }]
  }]
})json");
    const auto steady = thermox::platform::compile_model_graph(
        document, registry, "steady_failsafe");
    const auto steady_result = thermox::solve_newton(steady.problem);
    require(steady_result.diagnostics.converged,
            steady_result.diagnostics.message);
    const auto steady_output = std::find(
        steady.problem.variable_names.begin(),
        steady.problem.variable_names.end(), "lag.output.value");
    require(
        steady_output != steady.problem.variable_names.end() &&
            std::abs(steady_result.x.at(static_cast<std::size_t>(
                steady_output - steady.problem.variable_names.begin()))) <
                1.0e-10,
        "steady compilation selects the declared expression mode");

    const auto transient =
        thermox::platform::compile_transient_model_graph(
            document, registry, "transient_trip");
    const auto filtered = std::find(
        transient.problem.variable_names.begin(),
        transient.problem.variable_names.end(), "lag.filtered");
    require(filtered != transient.problem.variable_names.end(),
            "mode-aware transient exposes its differential state");
    const auto filtered_index = static_cast<std::size_t>(
        filtered - transient.problem.variable_names.begin());
    thermox::TimeIntegrationOptions options;
    options.end_time = 2.0;
    options.initial_step = 0.05;
    options.max_step = 0.1;
    const auto result = thermox::integrate_dae(
        transient.problem, options);
    require(result.diagnostics.success, result.diagnostics.message);
    require(
        result.events.size() == 1U &&
            result.events.front().transitioned,
        "case event switches the project-defined component mode");
    const double expected = 0.2 * std::exp(
        -(options.end_time - result.events.front().time) / 2.0);
    require(
        std::abs(result.trajectory.back().state.at(filtered_index) -
                 expected) < 2.0e-2,
        "mode-aware expression follows its post-trip equation set");

    auto invalid = thermox::platform::ExpressionComponentDefinition{};
    invalid.schema_version =
        thermox::platform::expression_component_schema_v4;
    invalid.descriptor.kind = "custom.signal.invalid_modes";
    invalid.descriptor.version = "1.0.0";
    invalid.descriptor.template_kind = "control.invalid";
    invalid.descriptor.display_name = "Invalid modes";
    invalid.descriptor.category = "Project controls";
    invalid.descriptor.model_name = "Invalid incidence";
    invalid.descriptor.default_mode = "a";
    invalid.descriptor.ports = {
        {"input", "signal", "in"},
        {"output", "signal", "out"},
    };
    invalid.modes = {
        {"a", {{"law", "output.value", 1.0}}, {}},
        {"b", {{"law", "output.value + input.value", 1.0}}, {}},
    };
    require_invalid(
        [&]() {
            thermox::platform::register_expression_component(
                registry, std::move(invalid));
        },
        "preserve variable incidence");
}

}  // namespace

int main() {
    try {
        test_expression_component_compiles_and_solves();
        test_expression_contract_rejects_unsafe_or_unknown_inputs();
        test_expression_implementation_identity_covers_equations();
        test_transient_expression_component_integrates_internal_state();
        test_transient_expression_validation_rejects_unknown_symbols();
        test_mode_aware_expression_component_switches_fixed_structure();
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "thermox_expression_component_tests passed\n";
    return 0;
}
