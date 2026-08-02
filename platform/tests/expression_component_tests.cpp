#include "thermox/nonlinear_solver.hpp"
#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/expression_component.hpp"
#include "thermox/platform/model_document.hpp"

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

}  // namespace

int main() {
    try {
        test_expression_component_compiles_and_solves();
        test_expression_contract_rejects_unsafe_or_unknown_inputs();
        test_expression_implementation_identity_covers_equations();
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "thermox_expression_component_tests passed\n";
    return 0;
}
