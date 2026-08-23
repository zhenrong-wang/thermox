#include "thermox/nonlinear_solver.hpp"
#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/model_document.hpp"
#include "thermox/transient_solver.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(
    double actual,
    double expected,
    double tolerance,
    const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(
            message + ": expected " + std::to_string(expected) +
            ", received " + std::to_string(actual));
    }
}

std::size_t variable_index(
    const std::vector<std::string>& names,
    const std::string& name) {
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (names[index] == name) return index;
    }
    throw std::runtime_error("missing variable: " + name);
}

void test_two_sided_wall_accumulates_net_heat() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "wall_storage",
    "media": [],
    "components": [
      {"id": "heater", "kind": "source.heat.boundary"},
      {
        "id": "wall",
        "kind": "storage.thermal.wall_two_sided",
        "parameters": {
          "thermal_capacity": {"value": 2.0, "unit": "MJ/K"}
        }
      },
      {"id": "cooler", "kind": "sink.heat.boundary"}
    ],
    "connections": [
      {
        "id": "heat_in",
        "from": "heater.outlet",
        "to": "wall.hot_side",
        "kind": "heat_link"
      },
      {
        "id": "heat_out",
        "from": "wall.cold_side",
        "to": "cooler.inlet",
        "kind": "heat_link"
      }
    ]
  },
  "cases": [
    {
      "id": "steady",
      "mode": "steady_state_design",
      "fixed_values": {
        "heater.outlet.Q_dot": {"value": 1.0, "unit": "MW"},
        "heater.outlet.T": {"value": 350.0, "unit": "K"}
      }
    },
    {
      "id": "transient",
      "mode": "dynamic_transient",
      "fixed_values": {
        "heater.outlet.Q_dot": {"value": 1.0, "unit": "MW"},
        "cooler.inlet.Q_dot": {"value": 0.2, "unit": "MW"}
      },
      "initial_guesses": {
        "wall.temperature": {"value": 300.0, "unit": "K"}
      }
    }
  ]
})json");
    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto steady =
        thermox::platform::compile_model_graph(
            document, registry, "steady");
    const auto steady_result =
        thermox::solve_newton(steady.problem);
    require(
        steady_result.diagnostics.converged,
        steady_result.diagnostics.message);
    require_near(
        steady_result.x.at(variable_index(
            steady.problem.variable_names,
            "cooler.inlet.Q_dot")),
        1.0e6,
        1.0e-7,
        "steady wall passes heat without accumulation");

    const auto graph =
        thermox::platform::compile_transient_model_graph(
            document, registry, "transient");
    const auto temperature =
        variable_index(
            graph.problem.variable_names, "wall.temperature");
    const auto initialized =
        thermox::make_consistent_initial_conditions(
            graph.problem, 0.0);
    require(
        initialized.diagnostics.converged,
        initialized.diagnostics.message);
    require_near(
        initialized.derivative.at(temperature),
        0.4,
        1.0e-10,
        "wall temperature derivative follows net heat");

    thermox::TimeIntegrationOptions options;
    options.end_time = 10.0;
    options.initial_step = 1.0;
    options.max_step = 2.0;
    const auto result =
        thermox::integrate_dae(graph.problem, options);
    require(result.diagnostics.success, result.diagnostics.message);
    require_near(
        result.trajectory.back().state.at(temperature),
        304.0,
        1.0e-7,
        "wall integrates net heat flow");
}

void test_rotor_integrates_kinetic_energy() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "shaft_inertia",
    "media": [],
    "components": [
      {"id": "driver", "kind": "source.shaft.boundary"},
      {
        "id": "rotor",
        "kind": "shaft.inertia.two_port",
        "parameters": {
          "moment_of_inertia": {
            "value": 100.0,
            "unit": "kg*m2"
          }
        }
      },
      {"id": "load", "kind": "sink.shaft.boundary"}
    ],
    "connections": [
      {
        "id": "drive",
        "from": "driver.outlet",
        "to": "rotor.driver",
        "kind": "shaft_link"
      },
      {
        "id": "load",
        "from": "rotor.load",
        "to": "load.inlet",
        "kind": "shaft_link"
      }
    ]
  },
  "cases": [
    {
      "id": "steady",
      "mode": "steady_state_design",
      "fixed_values": {
        "driver.outlet.W_dot": {"value": 1.0, "unit": "MW"},
        "driver.outlet.omega": {
          "value": 100.0,
          "unit": "rad/s"
        }
      }
    },
    {
      "id": "runup",
      "mode": "dynamic_transient",
      "fixed_values": {
        "driver.outlet.W_dot": {"value": 1.0, "unit": "MW"},
        "load.inlet.W_dot": {"value": 0.2, "unit": "MW"}
      },
      "initial_guesses": {
        "rotor.rotational_energy": {
          "value": 0.5,
          "unit": "MJ"
        }
      }
    }
  ]
})json");
    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto steady =
        thermox::platform::compile_model_graph(
            document, registry, "steady");
    const auto steady_result =
        thermox::solve_newton(steady.problem);
    require(
        steady_result.diagnostics.converged,
        steady_result.diagnostics.message);
    require_near(
        steady_result.x.at(variable_index(
            steady.problem.variable_names,
            "load.inlet.W_dot")),
        1.0e6,
        1.0e-7,
        "steady rotor passes balanced shaft power");

    const auto graph =
        thermox::platform::compile_transient_model_graph(
            document, registry, "runup");
    const auto energy = variable_index(
        graph.problem.variable_names,
        "rotor.rotational_energy");
    const auto speed = variable_index(
        graph.problem.variable_names, "rotor.omega");
    const auto initialized =
        thermox::make_consistent_initial_conditions(
            graph.problem, 0.0);
    require(
        initialized.diagnostics.converged,
        initialized.diagnostics.message);
    require_near(
        initialized.state.at(speed),
        100.0,
        1.0e-8,
        "kinetic energy closure establishes rotor speed");
    require_near(
        initialized.derivative.at(energy),
        8.0e5,
        1.0e-6,
        "rotor energy derivative follows net shaft power");

    thermox::TimeIntegrationOptions options;
    options.end_time = 1.0;
    options.initial_step = 0.1;
    options.max_step = 0.25;
    const auto result =
        thermox::integrate_dae(graph.problem, options);
    require(result.diagnostics.success, result.diagnostics.message);
    require_near(
        result.trajectory.back().state.at(energy),
        1.3e6,
        1.0e-4,
        "rotor integrates kinetic energy");
    require_near(
        result.trajectory.back().state.at(speed),
        std::sqrt(26000.0),
        1.0e-5,
        "rotor speed follows kinetic energy");
}

void test_normalized_control_chain_steady_and_transient() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "control_chain",
    "media": [],
    "components": [
      {"id": "sensor", "kind": "source.signal.boundary"},
      {
        "id": "controller",
        "kind": "control.proportional.normalized",
        "parameters": {"gain": 2.0}
      },
      {
        "id": "actuator",
        "kind": "control.first_order_lag.normalized",
        "parameters": {
          "gain": 0.5,
          "time_constant": {"value": 2.0, "unit": "s"}
        }
      },
      {"id": "consumer", "kind": "sink.control.boundary"}
    ],
    "connections": [
      {
        "id": "measurement",
        "from": "sensor.outlet",
        "to": "controller.measurement",
        "kind": "signal_link"
      },
      {
        "id": "command",
        "from": "controller.command",
        "to": "actuator.command",
        "kind": "signal_link"
      },
      {
        "id": "response",
        "from": "actuator.response",
        "to": "consumer.inlet",
        "kind": "signal_link"
      }
    ]
  },
  "cases": [
    {
      "id": "steady",
      "mode": "steady_state_design",
      "fixed_values": {"sensor.outlet.value": 1.0}
    },
    {
      "id": "transient",
      "mode": "dynamic_transient",
      "fixed_values": {"sensor.outlet.value": 1.0},
      "initial_guesses": {"actuator.response.value": 0.0}
    },
    {
      "id": "mode_switch",
      "mode": "dynamic_transient",
      "fixed_values": {"sensor.outlet.value": 1.0},
      "component_modes": {"actuator": "tracking"},
      "initial_guesses": {"actuator.response.value": 0.0},
      "state_events": [{
        "id": "actuator_failsafe",
        "target": "actuator.response.value",
        "threshold": 0.2,
        "direction": "rising",
        "terminal": false,
        "actions": [{
          "type": "set_mode",
          "target": "actuator",
          "mode": "decay_to_zero"
        }]
      }]
    }
  ]
})json");
    const auto registry =
        thermox::platform::make_default_component_registry();
    const auto steady =
        thermox::platform::compile_model_graph(
            document, registry, "steady");
    const auto steady_result =
        thermox::solve_newton(steady.problem);
    require(
        steady_result.diagnostics.converged,
        steady_result.diagnostics.message);
    const auto steady_response = variable_index(
        steady.problem.variable_names,
        "actuator.response.value");
    require_near(
        steady_result.x.at(steady_response),
        1.0,
        1.0e-10,
        "steady control chain applies both gains");

    const auto transient =
        thermox::platform::compile_transient_model_graph(
            document, registry, "transient");
    const auto response = variable_index(
        transient.problem.variable_names,
        "actuator.response.value");
    require(
        transient.problem.variable_kinds.at(response) ==
            thermox::DaeVariableKind::differential,
        "first-order response is a differential state");
    const auto initialized =
        thermox::make_consistent_initial_conditions(
            transient.problem, 0.0);
    require(
        initialized.diagnostics.converged,
        initialized.diagnostics.message);
    require_near(
        initialized.derivative.at(response),
        0.5,
        1.0e-10,
        "first-order lag initializes its response rate");

    thermox::TimeIntegrationOptions options;
    options.end_time = 2.0;
    options.initial_step = 0.05;
    options.max_step = 0.1;
    options.absolute_tolerance = 1.0e-8;
    options.relative_tolerance = 1.0e-8;
    const auto result =
        thermox::integrate_dae(transient.problem, options);
    require(result.diagnostics.success, result.diagnostics.message);
    require_near(
        result.trajectory.back().state.at(response),
        1.0 - std::exp(-1.0),
        2.0e-3,
        "first-order lag follows its analytic response");

    const auto switched =
        thermox::platform::compile_transient_model_graph(
            document, registry, "mode_switch");
    const auto switched_response = variable_index(
        switched.problem.variable_names,
        "actuator.response.value");
    const auto switched_result =
        thermox::integrate_dae(switched.problem, options);
    require(
        switched_result.diagnostics.success,
        switched_result.diagnostics.message);
    require(
        switched_result.events.size() == 1U &&
            switched_result.events.front().name ==
                "actuator_failsafe" &&
            switched_result.events.front().transitioned &&
            !switched_result.events.front().terminal,
        "nonterminal set_mode event must transition exactly once");
    require_near(
        switched_result.events.front().time,
        -2.0 * std::log(0.8),
        2.0e-3,
        "mode transition occurs at the declared state threshold");
    require_near(
        switched_result.trajectory.back().state.at(switched_response),
        0.2 * std::exp(
            -(options.end_time -
              switched_result.events.front().time) / 2.0),
        2.0e-3,
        "post-event state follows the decay mode equation");

    auto invalid_mode = document;
    invalid_mode.cases.back().component_modes["actuator"] =
        "unregistered";
    bool rejected_invalid_mode = false;
    try {
        (void)thermox::platform::compile_transient_model_graph(
            invalid_mode, registry, "mode_switch");
    } catch (const std::invalid_argument&) {
        rejected_invalid_mode = true;
    }
    require(
        rejected_invalid_mode,
        "transient compilation must reject unregistered component modes");
}

}  // namespace

int main() {
    try {
        test_two_sided_wall_accumulates_net_heat();
        test_rotor_integrates_kinetic_energy();
        test_normalized_control_chain_steady_and_transient();
        std::cout << "dynamic component tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "dynamic component tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
