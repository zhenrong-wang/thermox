#!/usr/bin/env python3
"""Build the generic closed-loop Thermox replay of NASA's T-MATS example."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from build_tmats_simple_gas_turbine_open_loop import build


def value(number: float, unit: str) -> dict:
    return {"value": number, "unit": unit}


def build_closed_loop(reference: dict) -> dict:
    declaration = build(reference)
    model = declaration["model"]
    model["id"] = "nasa_tmats_simple_gas_turbine_closed_loop"
    model["name"] = (
        "Generic Thermox closed-loop replay of NASA T-MATS simple gas turbine"
    )

    fuel = next(component for component in model["components"] if component["id"] == "fuel")
    fuel["kind"] = "source.material.controlled_fixed_composition"
    fuel["parameters"]["reference_mass_flow"] = value(1.0, "lbm/s")
    model["components"].extend([
        {
            "id": "shaft_speed_pickup",
            "kind": "sensor.shaft.normalized_speed",
            "version": "1.0.0",
            "parameters": {"reference_speed": value(1.0, "rpm")},
        },
        {
            "id": "speed_sensor",
            "kind": "sensor.first_order_lag.normalized",
            "version": "1.0.0",
            "parameters": {
                "gain": 1.0,
                "time_constant": value(0.05, "s"),
            },
        },
        {
            "id": "speed_setpoint",
            "kind": "source.signal.boundary",
            "version": "1.0.0",
        },
        {
            "id": "speed_controller",
            "kind": "control.pi_bounded.normalized",
            "version": "1.0.0",
            "parameters": {
                "proportional_gain": 0.025,
                "integral_time": value(0.5, "s"),
                "tracking_time": value(1.0, "s"),
                "bias": 0.0,
                "lower_limit": -1000.0,
                "upper_limit": 1000.0,
            },
        },
    ])

    model["connections"] = [
        connection
        for connection in model["connections"]
        if connection["id"] != "turbine_power"
    ]
    model["connections"].extend([
        {
            "id": "turbine_to_speed_pickup",
            "kind": "shaft_link",
            "from": "turbine.shaft",
            "to": "shaft_speed_pickup.inlet",
        },
        {
            "id": "speed_pickup_to_shaft",
            "kind": "shaft_link",
            "from": "shaft_speed_pickup.outlet",
            "to": "shaft.driver",
        },
        {
            "id": "true_speed_signal",
            "kind": "signal_link",
            "from": "shaft_speed_pickup.measurement",
            "to": "speed_sensor.command",
        },
        {
            "id": "sensed_speed_signal",
            "kind": "signal_link",
            "from": "speed_sensor.response",
            "to": "speed_controller.measurement",
        },
        {
            "id": "speed_demand_signal",
            "kind": "signal_link",
            "from": "speed_setpoint.outlet",
            "to": "speed_controller.setpoint",
        },
        {
            "id": "fuel_command_signal",
            "kind": "signal_link",
            "from": "speed_controller.command",
            "to": "fuel.command",
        },
    ])

    case = declaration["cases"][0]
    case["id"] = "nasa_speed_control_replay"
    reference_times = [
        sample["time"]
        for signal in reference["signals"]
        if signal["id"] == "shaft_speed"
        for sample in signal["samples"]
    ]
    if len(reference_times) != 701:
        raise ValueError("reference must contain 701 shaft-speed timestamps")
    demand = lambda time: (
        10000.0
        if time <= 5.0
        else 10000.0 - 200.0 * (time - 5.0)
        if time <= 10.0
        else 9000.0
    )
    case["input_schedules"] = {
        "speed_setpoint.outlet.value": {
            "interpolation": "linear",
            "points": [
                {"time": value(time, "s"), "value": demand(time)}
                for time in reference_times
            ],
        }
    }
    guesses = case["initial_guesses"]
    guesses.update({
        "fuel.command.value": 3.0,
        "shaft_speed_pickup.inlet.W_dot": value(22.0, "MW"),
        "shaft_speed_pickup.inlet.omega": value(10000.0, "rpm"),
        "shaft_speed_pickup.outlet.W_dot": value(22.0, "MW"),
        "shaft_speed_pickup.outlet.omega": value(10000.0, "rpm"),
        "shaft_speed_pickup.measurement.value": 10000.0,
        "speed_sensor.command.value": 10000.0,
        "speed_sensor.response.value": 10000.0,
        "speed_setpoint.outlet.value": 10000.0,
        "speed_controller.setpoint.value": 10000.0,
        "speed_controller.measurement.value": 10000.0,
        "speed_controller.command.value": 3.0,
        "speed_controller.integral_state": 3.0,
    })
    return declaration


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    reference = json.loads(args.reference.read_text(encoding="utf-8"))
    declaration = build_closed_loop(reference)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(declaration, indent=2) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
