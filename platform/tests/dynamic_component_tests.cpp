#include "thermox/nonlinear_solver.hpp"
#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/model_document.hpp"
#include "thermox/transient_solver.hpp"

#include <cmath>
#include <iostream>
#include <memory>
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

class QuasiSteadySignalGainModel final
    : public thermox::platform::ComponentModel {
public:
    explicit QuasiSteadySignalGainModel(bool invalid_state = false) {
        descriptor_.kind = invalid_state
            ? "test.quasi_steady.invalid"
            : "test.quasi_steady.signal_gain";
        descriptor_.version = "1.0.0";
        descriptor_.ports = {
            {"input", "signal", "in"},
            {"output", "signal", "out"},
        };
        descriptor_.supports_steady = true;
        descriptor_.supports_transient = true;
        descriptor_.uses_quasi_steady_transient_equations = true;
        if (invalid_state) {
            descriptor_.internal_variables = {{
                "stored_value", thermox::DaeVariableKind::differential,
                0.0, 1.0, 0.0, 1.0, -1.0, 1.0,
                "dimensionless"}};
        }
    }

    const thermox::platform::ComponentModelDescriptor&
    descriptor() const override {
        return descriptor_;
    }

    void add_equations(
        const thermox::platform::ComponentCompileContext& context,
        thermox::EquationSystemBuilder& system) const override {
        const auto input = context.port_variables.at("input.value");
        const auto output = context.port_variables.at("output.value");
        system.add_linear_equation(
            "component." + context.component.id + ".gain",
            {{output, 1.0}, {input, -2.0}}, 0.0, 1.0);
    }

private:
    thermox::platform::ComponentModelDescriptor descriptor_;
};

void test_quasi_steady_equations_lift_into_transient_dae() {
    auto registry =
        thermox::platform::make_default_component_registry();
    registry.register_model(
        std::make_shared<QuasiSteadySignalGainModel>());
    bool rejected_differential_state = false;
    try {
        registry.register_model(
            std::make_shared<QuasiSteadySignalGainModel>(true));
    } catch (const std::invalid_argument&) {
        rejected_differential_state = true;
    }
    require(
        rejected_differential_state,
        "quasi-steady registration must reject differential state");

    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "quasi_steady_lift",
    "media": [],
    "components": [
      {"id": "source", "kind": "source.signal.boundary"},
      {"id": "gain", "kind": "test.quasi_steady.signal_gain"},
      {"id": "sink", "kind": "sink.signal.boundary"}
    ],
    "connections": [
      {"id": "input", "from": "source.outlet", "to": "gain.input", "kind": "signal_link"},
      {"id": "output", "from": "gain.output", "to": "sink.inlet", "kind": "signal_link"}
    ]
  },
  "cases": [{
    "id": "transient", "mode": "dynamic_transient",
    "fixed_values": {"source.outlet.value": 0.25}
  }]
})json");
    const auto graph =
        thermox::platform::compile_transient_model_graph(
            document, registry, "transient");
    const auto initialized =
        thermox::make_consistent_initial_conditions(
            graph.problem, 0.0);
    require(
        initialized.diagnostics.converged,
        initialized.diagnostics.message);
    require_near(
        initialized.state.at(variable_index(
            graph.problem.variable_names, "sink.inlet.value")),
        0.5, 1.0e-12,
        "quasi-steady equation is retained in transient DAE");
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

    auto invalid_reset_map = document;
    thermox::platform::StateEventDefinition event;
    event.id = "invalid_speed_to_energy_reset";
    event.target = "rotor.rotational_energy";
    event.threshold = {6.0e5, "J", "energy"};
    event.direction = "rising";
    thermox::platform::StateEventDefinition::Action action;
    action.type = "set_state";
    action.target = "rotor.rotational_energy";
    action.source = "rotor.omega";
    event.actions.push_back(std::move(action));
    invalid_reset_map.cases.back().state_events.push_back(
        std::move(event));
    bool rejected_dimension_mismatch = false;
    try {
        (void)thermox::platform::compile_transient_model_graph(
            invalid_reset_map, registry, "runup");
    } catch (const std::invalid_argument&) {
        rejected_dimension_mismatch = true;
    }
    require(
        rejected_dimension_mismatch,
        "cross-component reset sources must match target dimensions");
}

void test_multi_load_rotors_accumulate_net_power() {
    const auto document =
        thermox::platform::parse_model_document_text(R"json({
  "schema_version": "thermox.model/v2",
  "model": {
    "id": "multi_load_shaft_inertia",
    "media": [],
    "components": [
      {"id": "driver_a", "kind": "source.shaft.boundary"},
      {
        "id": "rotor_a", "kind": "shaft.inertia.multi_load",
        "port_counts": {"load": 2},
        "parameters": {
          "moment_of_inertia": {"value": 10.0, "unit": "kg*m2"},
          "mechanical_efficiency": 0.9
        }
      },
      {"id": "load_a1", "kind": "sink.shaft.boundary"},
      {"id": "load_a2", "kind": "sink.shaft.boundary"},
      {"id": "driver_b", "kind": "source.shaft.boundary"},
      {
        "id": "rotor_b", "kind": "shaft.inertia.multi_load",
        "port_counts": {"load": 2},
        "parameters": {
          "moment_of_inertia": {"value": 20.0, "unit": "kg*m2"},
          "mechanical_efficiency": 0.95
        }
      },
      {
        "id": "gearbox_b", "kind": "gearbox.shaft.fixed_ratio",
        "parameters": {
          "speed_ratio": 2.0,
          "mechanical_efficiency": 0.8
        }
      },
      {"id": "load_b1", "kind": "sink.shaft.boundary"},
      {"id": "load_b2", "kind": "sink.shaft.boundary"}
    ],
    "connections": [
      {"id": "drive_a", "from": "driver_a.outlet", "to": "rotor_a.driver", "kind": "shaft_link"},
      {"id": "load_a_1", "from": "rotor_a.load_1", "to": "load_a1.inlet", "kind": "shaft_link"},
      {"id": "load_a_2", "from": "rotor_a.load_2", "to": "load_a2.inlet", "kind": "shaft_link"},
      {"id": "drive_b", "from": "driver_b.outlet", "to": "rotor_b.driver", "kind": "shaft_link"},
      {"id": "load_b_1", "from": "rotor_b.load_1", "to": "load_b1.inlet", "kind": "shaft_link"},
      {"id": "gear_b", "from": "rotor_b.load_2", "to": "gearbox_b.driver", "kind": "shaft_link"},
      {"id": "load_b_2", "from": "gearbox_b.load", "to": "load_b2.inlet", "kind": "shaft_link"}
    ]
  },
  "cases": [{
    "id": "runup", "mode": "dynamic_transient",
    "fixed_values": {
      "driver_a.outlet.W_dot": {"value": 1.0, "unit": "MW"},
      "load_a1.inlet.W_dot": {"value": 0.3, "unit": "MW"},
      "load_a2.inlet.W_dot": {"value": 0.1, "unit": "MW"},
      "driver_b.outlet.W_dot": {"value": 1.0, "unit": "MW"},
      "load_b1.inlet.W_dot": {"value": 0.3, "unit": "MW"},
      "load_b2.inlet.W_dot": {"value": 0.2, "unit": "MW"}
    },
    "initial_guesses": {
      "rotor_a.rotational_energy": {"value": 50.0, "unit": "kJ"},
      "rotor_b.rotational_energy": {"value": 100.0, "unit": "kJ"}
    }
  }]
})json");
    const auto graph =
        thermox::platform::compile_transient_model_graph(
            document,
            thermox::platform::make_default_component_registry(),
            "runup");
    const auto initialized =
        thermox::make_consistent_initial_conditions(graph.problem, 0.0);
    require(
        initialized.diagnostics.converged,
        initialized.diagnostics.message);
    const auto index = [&](const std::string& name) {
        return variable_index(graph.problem.variable_names, name);
    };
    require_near(
        initialized.state.at(index("rotor_a.omega")), 100.0, 1.0e-8,
        "two-load rotor recovers speed from kinetic energy");
    require_near(
        initialized.derivative.at(
            index("rotor_a.rotational_energy")),
        5.0e5, 1.0e-6,
        "two-load rotor accumulates efficiency-adjusted net power");
    require_near(
        initialized.state.at(index("load_a1.inlet.omega")),
        100.0, 1.0e-8,
        "two-load rotor enforces common speed");
    require_near(
        initialized.state.at(index("rotor_b.omega")), 100.0, 1.0e-8,
        "geared rotor recovers speed from kinetic energy");
    require_near(
        initialized.state.at(index("load_b2.inlet.omega")),
        50.0, 1.0e-8,
        "geared rotor enforces its speed ratio");
    require_near(
        initialized.derivative.at(
            index("rotor_b.rotational_energy")),
        4.0e5, 1.0e-6,
        "geared rotor accumulates shaft and gearbox adjusted net power");
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
      {"id": "consumer", "kind": "sink.control.boundary"},
      {"id": "observer_source", "kind": "source.control.boundary"},
      {
        "id": "observer",
        "kind": "control.first_order_lag.normalized",
        "parameters": {
          "gain": 1.0,
          "time_constant": {"value": 1.0, "unit": "s"}
        }
      },
      {"id": "observer_sink", "kind": "sink.control.boundary"}
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
      },
      {
        "id": "observer_command",
        "from": "observer_source.outlet",
        "to": "observer.command",
        "kind": "signal_link"
      },
      {
        "id": "observer_response",
        "from": "observer.response",
        "to": "observer_sink.inlet",
        "kind": "signal_link"
      }
    ]
  },
  "cases": [
    {
      "id": "steady",
      "mode": "steady_state_design",
      "fixed_values": {
        "sensor.outlet.value": 1.0,
        "observer_source.outlet.value": 0.8
      }
    },
    {
      "id": "transient",
      "mode": "dynamic_transient",
      "fixed_values": {
        "sensor.outlet.value": 1.0,
        "observer_source.outlet.value": 0.8
      },
      "initial_guesses": {
        "actuator.response.value": 0.0,
        "observer.response.value": 0.0
      }
    },
    {
      "id": "mode_switch",
      "mode": "dynamic_transient",
      "fixed_values": {
        "sensor.outlet.value": 1.0,
        "observer_source.outlet.value": 0.8
      },
      "component_modes": {"actuator": "tracking"},
      "initial_guesses": {
        "actuator.response.value": 0.0,
        "observer.response.value": 0.0
      },
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
        }, {
          "type": "set_state",
          "target": "observer.response.value",
          "value": 0.05
        }, {
          "type": "set_state",
          "target": "actuator.response.value",
          "source": "observer.response.value"
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
    const auto observer_response = variable_index(
        switched.problem.variable_names,
        "observer.response.value");
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
        "nonterminal mode/state event must transition exactly once");
    require_near(
        switched_result.events.front().time,
        -2.0 * std::log(0.8),
        2.0e-3,
        "mode transition occurs at the declared state threshold");
    require_near(
        switched_result.events.front().state.at(switched_response),
        0.8 * (1.0 - std::exp(
            -switched_result.events.front().time)),
        2.0e-3,
        "system event must copy a cross-component graph value into "
        "the reset state from the common pre-event snapshot");
    require_near(
        switched_result.events.front().state.at(observer_response),
        0.05,
        1.0e-10,
        "source-state reset commits only after all reset sources are read");
    require_near(
        switched_result.trajectory.back().state.at(switched_response),
        switched_result.events.front().state.at(switched_response) *
            std::exp(
            -(options.end_time -
              switched_result.events.front().time) / 2.0),
        2.0e-3,
        "post-event state follows the reset value and decay equation");

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

    auto invalid_reset = document;
    thermox::platform::StateEventDefinition::Action reset_action;
    reset_action.type = "set_state";
    reset_action.target = "sensor.outlet.value";
    reset_action.value = {0.0, "dimensionless", "dimensionless"};
    invalid_reset.cases.back().state_events.front().actions.push_back(
        std::move(reset_action));
    bool rejected_algebraic_reset = false;
    try {
        (void)thermox::platform::compile_transient_model_graph(
            invalid_reset, registry, "mode_switch");
    } catch (const std::invalid_argument&) {
        rejected_algebraic_reset = true;
    }
    require(
        rejected_algebraic_reset,
        "set_state must reject algebraic graph variables");

    auto invalid_source = document;
    invalid_source.cases.back().state_events.front()
        .actions.back().source = "missing.output.value";
    bool rejected_unknown_source = false;
    try {
        (void)thermox::platform::compile_transient_model_graph(
            invalid_source, registry, "mode_switch");
    } catch (const std::invalid_argument&) {
        rejected_unknown_source = true;
    }
    require(
        rejected_unknown_source,
        "cross-component reset maps must reject unknown graph sources");

    auto ambiguous_reset = document;
    ambiguous_reset.cases.back().state_events.front()
        .actions.back().value =
            thermox::platform::ScalarValue{
                0.0, "dimensionless", "dimensionless"};
    bool rejected_ambiguous_reset = false;
    try {
        (void)thermox::platform::compile_transient_model_graph(
            ambiguous_reset, registry, "mode_switch");
    } catch (const std::invalid_argument&) {
        rejected_ambiguous_reset = true;
    }
    require(
        rejected_ambiguous_reset,
        "reset maps must declare exactly one constant or graph source");
}

}  // namespace

int main() {
    try {
        test_quasi_steady_equations_lift_into_transient_dae();
        test_two_sided_wall_accumulates_net_heat();
        test_rotor_integrates_kinetic_energy();
        test_multi_load_rotors_accumulate_net_power();
        test_normalized_control_chain_steady_and_transient();
        std::cout << "dynamic component tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "dynamic component tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
