#include "thermox/nonlinear_solver.hpp"
#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/expression_component.hpp"
#include "thermox/platform/model_document.hpp"
#include "thermox/physics/property_registry.hpp"
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

void test_expression_component_uses_port_bound_properties() {
    auto registry =
        thermox::platform::make_default_component_registry();
    auto definition = pressure_loss_definition(
        "outlet.p - inlet.p + "
        "parameter.loss_coefficient * inlet.m_dot * "
        "abs(inlet.m_dot) / (2 * "
        "property.density_ph(inlet.p, inlet.h) * "
        "parameter.flow_area * parameter.flow_area)");
    definition.descriptor.kind =
        "custom.fluid.property_pressure_loss";
    definition.descriptor.parameters.clear();
    definition.descriptor.parameters.push_back({
        "loss_coefficient", "dimensionless", false, 2.0,
        0.0, 100.0, true, true});
    definition.descriptor.parameters.push_back({
        "flow_area", "area", false, 0.5,
        1.0e-6, 1000.0, false, true});
    thermox::platform::register_expression_component(
        registry, std::move(definition));
    const auto& model = registry.require_model(
        "custom.fluid.property_pressure_loss");
    require(
        model.descriptor().required_property_capabilities ==
            std::vector<thermox::physics::PropertyCapability>{
                thermox::physics::PropertyCapability::state_ph} &&
            model.requires_property_capability_on_port(
                thermox::physics::PropertyCapability::state_ph,
                "inlet") &&
            !model.requires_property_capability_on_port(
                thermox::physics::PropertyCapability::state_ph,
                "outlet"),
        "safe property calls derive the required p-h capability");

    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "property_expression",
    "media": [{
      "id": "air",
      "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "components": [{
      "id": "loss",
      "kind": "custom.fluid.property_pressure_loss",
      "media": {"inlet": "air", "outlet": "air"}
    }],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "loss.inlet.m_dot": {"value": 12.0, "unit": "kg/s"},
      "loss.inlet.p": {"value": 2.0, "unit": "bar"},
      "loss.inlet.h": {"value": 300.0, "unit": "kJ/kg"}
    }
  }]
})json");
    const auto graph = thermox::platform::compile_model_graph(
        document, registry, "design");
    const auto derivative_check =
        thermox::verify_problem_jacobian(graph.problem);
    require(
        derivative_check.passed,
        "property functions chain p-h derivatives into the sparse Jacobian");
    const auto solved = thermox::solve_newton(graph.problem);
    require(
        solved.diagnostics.converged,
        "property-backed custom pressure loss converges");
}

void test_expression_component_supports_isentropic_closure() {
    auto registry =
        thermox::platform::make_default_component_registry();
    auto definition = pressure_loss_definition(
        "outlet.p - inlet.p * parameter.pressure_ratio");
    definition.descriptor.kind =
        "custom.fluid.isentropic_expander";
    definition.descriptor.template_kind =
        "custom.fluid.isentropic_expander";
    definition.equations[2] = {
        "isentropic_closure",
        "property.entropy_ph(outlet.p, outlet.h) - "
        "property.entropy_ph(inlet.p, inlet.h)",
        1000.0,
    };
    thermox::platform::register_expression_component(
        registry, std::move(definition));

    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "custom_isentropic_expander",
    "media": [{
      "id": "air",
      "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "components": [{
      "id": "expander",
      "kind": "custom.fluid.isentropic_expander",
      "media": {"inlet": "air", "outlet": "air"},
      "parameters": {"pressure_ratio": 0.5}
    }],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "expander.inlet.m_dot": {"value": 12.0, "unit": "kg/s"},
      "expander.inlet.p": {"value": 2.0, "unit": "bar"},
      "expander.inlet.h": {"value": 300.0, "unit": "kJ/kg"}
    }
  }]
})json");
    const auto graph = thermox::platform::compile_model_graph(
        document, registry, "design");
    const auto derivative_check =
        thermox::verify_problem_jacobian(graph.problem);
    require(
        derivative_check.passed,
        "entropy p-h functions must chain provider derivatives into "
        "the sparse Jacobian");
    const auto solved = thermox::solve_newton(graph.problem);
    require(
        solved.diagnostics.converged,
        "custom isentropic entropy closure must converge");
    const auto outlet_enthalpy = [&]() {
        for (std::size_t index = 0;
             index < graph.problem.variable_names.size(); ++index) {
            if (graph.problem.variable_names[index] ==
                "expander.outlet.h") {
                return solved.x.at(index);
            }
        }
        throw std::runtime_error(
            "missing custom expander outlet enthalpy");
    }();
    constexpr double cp = 1004.5;
    constexpr double gas_constant = 287.0;
    const double expected = 300000.0 * std::pow(
        0.5, gas_constant / cp);
    require(
        std::abs(outlet_enthalpy - expected) < 1.0e-5,
        "entropy closure must reproduce the ideal-gas isentropic "
        "enthalpy relation: solved=" +
            std::to_string(outlet_enthalpy) +
            ", expected=" + std::to_string(expected));
}

void test_expression_component_supports_two_phase_quality_closure() {
    auto registry =
        thermox::platform::make_default_component_registry();
    auto definition = pressure_loss_definition(
        "outlet.p - inlet.p * parameter.pressure_ratio");
    definition.descriptor.kind =
        "custom.fluid.quality_target";
    definition.descriptor.template_kind =
        "custom.fluid.quality_target";
    definition.descriptor.parameters.push_back({
        "target_quality", "dimensionless", true,
        std::nullopt, 0.0, 1.0, true, true});
    definition.equations[2] = {
        "quality_closure",
        "property.vapor_quality_ph(outlet.p, outlet.h) - "
        "parameter.target_quality",
        1.0,
    };
    thermox::platform::register_expression_component(
        registry, std::move(definition));

    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "custom_quality_target",
    "media": [{
      "id": "water",
      "backend": "water_steam_if97",
      "substance": "Water"
    }],
    "components": [{
      "id": "target",
      "kind": "custom.fluid.quality_target",
      "media": {"inlet": "water", "outlet": "water"},
      "parameters": {
        "pressure_ratio": 1.0,
        "target_quality": 0.4
      }
    }],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "target.inlet.m_dot": {"value": 5.0, "unit": "kg/s"},
      "target.inlet.p": {"value": 1.0, "unit": "MPa"},
      "target.inlet.h": {"value": 500.0, "unit": "kJ/kg"}
    },
    "initial_guesses": {
      "target.outlet.h": {"value": 1500.0, "unit": "kJ/kg"}
    }
  }]
})json");
    const auto graph = thermox::platform::compile_model_graph(
        document, registry, "design");
    const auto derivative_check =
        thermox::verify_problem_jacobian(graph.problem);
    require(
        derivative_check.passed,
        "two-phase quality functions must chain bounded provider "
        "derivatives into the sparse Jacobian");
    const auto solved = thermox::solve_newton(graph.problem);
    require(
        solved.diagnostics.converged,
        "custom two-phase quality closure must converge");
    const auto outlet_enthalpy = [&]() {
        for (std::size_t index = 0;
             index < graph.problem.variable_names.size(); ++index) {
            if (graph.problem.variable_names[index] ==
                "target.outlet.h") {
                return solved.x.at(index);
            }
        }
        throw std::runtime_error(
            "missing quality-target outlet enthalpy");
    }();
    auto properties =
        thermox::physics::make_default_property_package_registry();
    const auto water = properties.create(
        "water_steam_if97", "Water");
    const auto saturation = water->saturation_p(1.0e6);
    require(saturation.ok(), "quality-target saturation reference");
    const double expected =
        saturation.liquid.enthalpy_j_kg + 0.4 *
        (saturation.vapor.enthalpy_j_kg -
         saturation.liquid.enthalpy_j_kg);
    require(
        std::abs(outlet_enthalpy - expected) < 1.0e-4,
        "quality closure must reproduce the saturation-mixture "
        "enthalpy relation");
    auto liquid_trial = graph.problem.initial_guess;
    for (std::size_t index = 0;
         index < graph.problem.variable_names.size(); ++index) {
        if (graph.problem.variable_names[index] ==
            "target.outlet.h") {
            liquid_trial[index] = 100000.0;
        }
    }
    std::vector<double> residual(
        graph.problem.residual_names.size(), 0.0);
    const auto liquid_status = graph.problem.checked_residual(
        liquid_trial, residual);
    require(
        liquid_status.code ==
                thermox::EvaluationStatusCode::recoverable_failure &&
            liquid_status.message.find(
                "requires a two-phase p-h state") !=
                std::string::npos,
        "vapor-quality expressions must reject single-phase trials as "
        "recoverable physical evaluations: " +
            liquid_status.message);
}

void test_expression_component_supports_heat_capacity_rate() {
    auto registry =
        thermox::platform::make_default_component_registry();
    auto definition = pressure_loss_definition(
        "outlet.p - inlet.p");
    definition.descriptor.kind =
        "custom.fluid.constant_cp_heater";
    definition.descriptor.template_kind =
        "custom.fluid.heater";
    definition.descriptor.ports.push_back(
        {"heat", "heat", "in"});
    definition.equations[2] = {
        "heat_capacity_rate",
        "heat.Q_dot - inlet.m_dot * "
        "property.cp_ph(inlet.p, inlet.h) * "
        "(property.temperature_ph(outlet.p, outlet.h) - "
        "property.temperature_ph(inlet.p, inlet.h))",
        1.0e6,
    };
    thermox::platform::register_expression_component(
        registry, std::move(definition));

    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "custom_constant_cp_heater",
    "media": [{
      "id": "air",
      "backend": "ideal_gas_mixture",
      "substance": "Air"
    }],
    "components": [{
      "id": "heater",
      "kind": "custom.fluid.constant_cp_heater",
      "media": {"inlet": "air", "outlet": "air"},
      "parameters": {"pressure_ratio": 1.0}
    }],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "heater.inlet.m_dot": {"value": 10.0, "unit": "kg/s"},
      "heater.inlet.p": {"value": 2.0, "unit": "bar"},
      "heater.inlet.h": {"value": 300.0, "unit": "kJ/kg"},
      "heater.heat.Q_dot": {"value": 1.0, "unit": "MW"},
      "heater.heat.T": {"value": 500.0, "unit": "K"}
    }
  }]
})json");
    const auto graph = thermox::platform::compile_model_graph(
        document, registry, "design");
    const auto derivative_check =
        thermox::verify_problem_jacobian(graph.problem);
    require(
        derivative_check.passed,
        "heat-capacity p-h functions must provide a consistent sparse "
        "Jacobian");
    const auto solved = thermox::solve_newton(graph.problem);
    require(
        solved.diagnostics.converged,
        "custom heat-capacity-rate equation must converge");
    const auto outlet_enthalpy = [&]() {
        for (std::size_t index = 0;
             index < graph.problem.variable_names.size(); ++index) {
            if (graph.problem.variable_names[index] ==
                "heater.outlet.h") {
                return solved.x.at(index);
            }
        }
        throw std::runtime_error(
            "missing constant-cp heater outlet enthalpy");
    }();
    require(
        std::abs(outlet_enthalpy - 400000.0) < 1.0e-5,
        "constant-cp heat-capacity-rate closure must reproduce its "
        "analytical outlet enthalpy");
}

void test_expression_component_supports_transport_properties() {
    auto registry =
        thermox::platform::make_default_component_registry();
    auto definition = pressure_loss_definition(
        "outlet.p - inlet.p");
    definition.descriptor.kind =
        "custom.fluid.transport_match";
    definition.descriptor.template_kind =
        "custom.fluid.transport_match";
    definition.equations[2] = {
        "transport_match",
        "property.viscosity_ph(outlet.p, outlet.h) / "
        "property.viscosity_ph(inlet.p, inlet.h) + "
        "property.thermal_conductivity_ph(outlet.p, outlet.h) / "
        "property.thermal_conductivity_ph(inlet.p, inlet.h) - 2",
        1.0,
    };
    thermox::platform::register_expression_component(
        registry, std::move(definition));

    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "custom_transport_match",
    "media": [{
      "id": "steam",
      "backend": "water_steam_if97",
      "substance": "Steam"
    }],
    "components": [{
      "id": "target",
      "kind": "custom.fluid.transport_match",
      "media": {"inlet": "steam", "outlet": "steam"},
      "parameters": {"pressure_ratio": 1.0}
    }],
    "connections": []
  },
  "cases": [{
    "id": "design",
    "mode": "steady_state_design",
    "fixed_values": {
      "target.inlet.m_dot": {"value": 10.0, "unit": "kg/s"},
      "target.inlet.p": {"value": 60.0, "unit": "bar"},
      "target.inlet.h": {"value": 3.2, "unit": "MJ/kg"}
    },
    "initial_guesses": {
      "target.outlet.m_dot": {"value": 10.0, "unit": "kg/s"},
      "target.outlet.p": {"value": 60.0, "unit": "bar"},
      "target.outlet.h": {"value": 3.2, "unit": "MJ/kg"}
    }
  }]
})json");
    const auto graph = thermox::platform::compile_model_graph(
        document, registry, "design");
    const auto derivative_check =
        thermox::verify_problem_jacobian(graph.problem);
    require(
        derivative_check.passed,
        "transport property expressions must provide a consistent "
        "sparse Jacobian");
    const auto solved = thermox::solve_newton(graph.problem);
    require(
        solved.diagnostics.converged,
        "custom transport-property equation must converge");

    auto ideal_document = document;
    ideal_document.media.front().backend =
        "ideal_gas_mixture";
    ideal_document.media.front().substance = "Air";
    require_invalid(
        [&]() {
            (void)thermox::platform::compile_model_graph(
                ideal_document, registry, "design");
        },
        "transport");
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
                    "property.density_ph(inlet.p, outlet.h)"));
        },
        "arguments must reference the same fluid port");
    require_invalid(
        [&]() {
            thermox::platform::register_expression_component(
                registry,
                pressure_loss_definition(
                    "property.density_ph(inlet.p * 1, inlet.h)"));
        },
        "requires direct <port>.p and <port>.h symbols");
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
        thermox::platform::expression_component_schema_v5;
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

void test_transient_property_expression_integrates_internal_state() {
    auto registry = thermox::platform::make_default_component_registry();
    thermox::platform::ExpressionComponentDefinition definition;
    definition.descriptor.kind =
        "custom.fluid.filtered_density";
    definition.descriptor.version = "1.0.0";
    definition.descriptor.template_kind = "fluid.state_filter";
    definition.descriptor.display_name = "Filtered density";
    definition.descriptor.category = "Project fluid controls";
    definition.descriptor.model_name = "Safe p-h density filter";
    definition.descriptor.supports_steady = false;
    definition.descriptor.supports_transient = true;
    definition.descriptor.ports = {
        {"state", "fluid", "in"},
    };
    definition.descriptor.parameters = {
        {"time_constant", "time", true, std::nullopt, 0.0,
         std::numeric_limits<double>::infinity(), false, true},
    };
    definition.descriptor.internal_variables = {{
        "filtered_density", thermox::DaeVariableKind::differential,
        1.0, 1.0, 0.0, 1.0, 0.0,
        std::numeric_limits<double>::infinity(), "density"}};
    definition.transient_equations = {{
        "density_filter",
        "parameter.time_constant * "
        "derivative.internal.filtered_density + "
        "internal.filtered_density - "
        "property.density_ph(state.p, state.h)",
        1.0}};
    thermox::platform::register_expression_component(
        registry, std::move(definition));

    const auto document = thermox::platform::parse_model_document_text(
        R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "transient_property_expression",
    "media": [{"id": "air", "backend": "ideal_gas_mixture",
               "substance": "Air"}],
    "components": [{
      "id": "probe",
      "kind": "custom.fluid.filtered_density",
      "media": {"state": "air"},
      "parameters": {"time_constant": {"value": 0.5, "unit": "s"}}
    }],
    "connections": []
  },
  "cases": [{
    "id": "hold",
    "mode": "dynamic_transient",
    "fixed_values": {
      "probe.state.m_dot": {"value": 0.0, "unit": "kg/s"},
      "probe.state.p": {"value": 1.0, "unit": "bar"},
      "probe.state.h": {"value": 300.0, "unit": "kJ/kg"}
    }
  }]
})json");
    const auto graph = thermox::platform::compile_transient_model_graph(
        document, registry, "hold");
    thermox::TimeIntegrationOptions options;
    options.end_time = 1.0;
    options.initial_step = 0.05;
    options.max_step = 0.1;
    const auto result = thermox::integrate_dae(graph.problem, options);
    require(result.diagnostics.success, result.diagnostics.message);
    const auto found = std::find(
        graph.problem.variable_names.begin(),
        graph.problem.variable_names.end(),
        "probe.filtered_density");
    require(
        found != graph.problem.variable_names.end(),
        "transient property expression must expose its internal state");
    const auto index = static_cast<std::size_t>(
        std::distance(graph.problem.variable_names.begin(), found));
    require(
        result.trajectory.back().state.at(index) > 1.02,
        "transient property expression must evolve toward p-h density");
}

void test_transient_expression_validation_rejects_unknown_symbols() {
    auto registry = thermox::platform::make_default_component_registry();
    thermox::platform::ExpressionComponentDefinition definition;
    definition.schema_version =
        thermox::platform::expression_component_schema_v5;
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
        thermox::platform::expression_component_schema_v5;
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
        thermox::platform::expression_component_schema_v5;
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

void test_component_owned_event_resets_state_and_mode_atomically() {
    auto registry = thermox::platform::make_default_component_registry();
    const auto make_definition = [] {
        thermox::platform::ExpressionComponentDefinition definition;
        definition.descriptor.kind = "custom.signal.autonomous_trip";
        definition.descriptor.version = "1.0.0";
        definition.descriptor.template_kind = "control.autonomous_trip";
        definition.descriptor.display_name = "Autonomous trip";
        definition.descriptor.category = "Project controls";
        definition.descriptor.model_name =
            "Expression event and atomic reset";
        definition.descriptor.supports_steady = false;
        definition.descriptor.supports_transient = true;
        definition.descriptor.default_mode = "tracking";
        definition.descriptor.ports = {
            {"input", "signal", "in"},
            {"output", "signal", "out"},
        };
        definition.descriptor.parameters = {
            {"time_constant", "time", true},
            {"trip_level", "dimensionless", true},
            {"reset_fraction", "dimensionless", true},
        };
        thermox::platform::InternalVariableDescriptor filtered;
        filtered.name = "filtered";
        filtered.kind = thermox::DaeVariableKind::differential;
        filtered.state_scale = 1.0;
        filtered.derivative_scale = 1.0;
        filtered.lower_bound = 0.0;
        filtered.upper_bound = 1.0;
        filtered.dimension = "dimensionless";
        thermox::platform::InternalVariableDescriptor snapshot = filtered;
        snapshot.name = "snapshot";
        definition.descriptor.internal_variables = {
            filtered, snapshot};
        definition.modes = {
            {
                "tracking", {},
                {
                    {"state_balance",
                     "parameter.time_constant * "
                     "derivative.internal.filtered + "
                     "internal.filtered - input.value", 1.0},
                    {"snapshot_hold",
                     "derivative.internal.snapshot", 1.0},
                    {"output",
                     "output.value - internal.filtered", 1.0},
                },
            },
            {
                "tripped", {},
                {
                    {"state_balance",
                     "parameter.time_constant * "
                     "derivative.internal.filtered + "
                     "internal.filtered - 0 * input.value", 1.0},
                    {"snapshot_hold",
                     "derivative.internal.snapshot", 1.0},
                    {"output",
                     "output.value - internal.filtered", 1.0},
                },
            },
        };
        thermox::platform::ExpressionComponentEventDefinition event;
        event.name = "trip";
        event.expression =
            "internal.filtered - parameter.trip_level";
        event.dimension = "dimensionless";
        event.direction = "rising";
        event.hysteresis_si = 0.01;
        event.actions = {
            {"set_state", "internal.filtered",
             "internal.filtered * parameter.reset_fraction", ""},
            {"set_state", "internal.snapshot",
             "internal.filtered", ""},
            {"set_mode", "", "", "tripped"},
        };
        definition.events.push_back(std::move(event));
        return definition;
    };

    auto definition = make_definition();
    auto invalid = definition;
    invalid.descriptor.kind = "custom.signal.invalid_event_reset";
    invalid.events.front().actions.front().expression =
        "parameter.time_constant";
    require_invalid(
        [&]() {
            thermox::platform::register_expression_component(
                registry, std::move(invalid));
        },
        "reset dimension does not match target");
    thermox::platform::register_expression_component(
        registry, std::move(definition));

    const auto document = thermox::platform::parse_model_document_text(
        R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "component_owned_event",
    "media": [],
    "components": [{
      "id": "trip",
      "kind": "custom.signal.autonomous_trip",
      "parameters": {
        "time_constant": {"value": 2.0, "unit": "s"},
        "trip_level": 0.2,
        "reset_fraction": 0.25
      }
    }],
    "connections": []
  },
  "cases": [{
    "id": "run",
    "mode": "dynamic_transient",
    "fixed_values": {"trip.input.value": 1.0},
    "initial_guesses": {
      "trip.filtered": 0.0,
      "trip.snapshot": 0.0
    }
  }]
})json");
    const auto graph =
        thermox::platform::compile_transient_model_graph(
            document, registry, "run");
    require(
        graph.problem.events.size() == 1U &&
            graph.problem.events.front().name ==
                "component.trip.event.trip",
        "component-owned event is namespaced into the DAE problem");
    const auto filtered = std::find(
        graph.problem.variable_names.begin(),
        graph.problem.variable_names.end(), "trip.filtered");
    const auto snapshot = std::find(
        graph.problem.variable_names.begin(),
        graph.problem.variable_names.end(), "trip.snapshot");
    require(
        filtered != graph.problem.variable_names.end() &&
            snapshot != graph.problem.variable_names.end(),
        "component event test exposes both differential states");
    const auto filtered_index = static_cast<std::size_t>(
        filtered - graph.problem.variable_names.begin());
    const auto snapshot_index = static_cast<std::size_t>(
        snapshot - graph.problem.variable_names.begin());

    thermox::TimeIntegrationOptions options;
    options.end_time = 2.0;
    options.initial_step = 0.05;
    options.max_step = 0.1;
    const auto result = thermox::integrate_dae(
        graph.problem, options);
    require(result.diagnostics.success, result.diagnostics.message);
    require(
        result.events.size() == 1U &&
            result.events.front().transitioned,
        "component-owned event transitions exactly once");
    require(
        std::abs(result.events.front().time + 2.0 * std::log(0.8)) <
            3.0e-3,
        "component-owned surface locates the analytical crossing");
    require(
        std::abs(result.events.front().state.at(filtered_index) - 0.05) <
                1.0e-9 &&
            std::abs(result.events.front().state.at(snapshot_index) - 0.2) <
                3.0e-3,
        "reset expressions read one pre-event state and commit atomically");
    const double expected = 0.05 * std::exp(
        -(options.end_time - result.events.front().time) / 2.0);
    require(
        std::abs(result.trajectory.back().state.at(filtered_index) -
                 expected) < 3.0e-3,
        "post-event trajectory uses the reset state and tripped mode");

    const auto repeated = thermox::integrate_dae(
        graph.problem, options);
    require(
        repeated.diagnostics.success &&
            repeated.events.size() == 1U,
        "compiled component event mode resets between executions");
}

}  // namespace

int main() {
    try {
        test_expression_component_compiles_and_solves();
        test_expression_component_uses_port_bound_properties();
        test_expression_component_supports_isentropic_closure();
        test_expression_component_supports_two_phase_quality_closure();
        test_expression_component_supports_heat_capacity_rate();
        test_expression_component_supports_transport_properties();
        test_expression_contract_rejects_unsafe_or_unknown_inputs();
        test_expression_implementation_identity_covers_equations();
        test_transient_expression_component_integrates_internal_state();
        test_transient_property_expression_integrates_internal_state();
        test_transient_expression_validation_rejects_unknown_symbols();
        test_mode_aware_expression_component_switches_fixed_structure();
        test_component_owned_event_resets_state_and_mode_atomically();
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "thermox_expression_component_tests passed\n";
    return 0;
}
