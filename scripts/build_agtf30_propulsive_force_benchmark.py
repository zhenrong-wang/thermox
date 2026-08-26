#!/usr/bin/env python3
"""Add generic freestream momentum and net-force closure to AGTF30."""

from __future__ import annotations

import argparse
import copy
import json
import math
from pathlib import Path


GAMMA = 1.4
AIR_GAS_CONSTANT_J_KG_K = 287.05287
RANKINE_TO_KELVIN = 5.0 / 9.0
FLIGHT_CONDITIONS = {
    "sea_level_mach_03_1500": (0.3, 518.67 + 27.0),
    "alt_10000_mach_05_1750": (0.5, 483.03),
    "alt_25000_mach_06_2000": (0.6, 429.62 - 10.0),
    "alt_30000_mach_072_2000": (0.72, 411.84),
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(
            "benchmarks/nasa_tmats/agtf30_propulsive_force.json"
        ),
    )
    args = parser.parse_args()
    repository = args.repository.resolve()
    declaration = json.loads((
        repository /
        "benchmarks/nasa_tmats/agtf30_vbv_nozzle_twin_spool.json"
    ).read_text(encoding="utf-8"))
    reference = json.loads((
        repository /
        "benchmarks/nasa_tmats/agtf30_propulsion_reference.json"
    ).read_text(encoding="utf-8"))
    reference_cases = {
        case["case_id"]: case for case in reference["cases"]
    }
    model = declaration["model"]
    model["id"] = "nasa_agtf30_propulsive_force"
    model["name"] = (
        "NASA AGTF30 gas path with generic propulsive force accounting"
    )
    model["revision"] = "1"
    model["components"].extend([{
        "id": "freestream_momentum",
        "kind": "transport.material.freestream_momentum",
        "version": "1.0.0",
        "parameters": {
            "freestream_velocity": {"value": 0.0, "unit": "m/s"},
            "momentum_coefficient": 1.0,
        },
        "materials": {"inlet": "reacting_gas", "outlet": "reacting_gas"},
    }, {
        "id": "propulsive_force",
        "kind": "balance.force.propulsive",
        "version": "1.0.0",
        "port_counts": {"gross": 2},
    }])
    inlet_connection = next(
        connection for connection in model["connections"]
        if connection["from"] == "station_2.outlet"
        and connection["to"] == "fan.inlet"
    )
    inlet_connection["to"] = "freestream_momentum.inlet"
    model["connections"].extend([{
        "id": "freestream_to_fan",
        "from": "freestream_momentum.outlet",
        "to": "fan.inlet",
        "kind": "material_link",
    }, {
        "id": "bypass_gross_force",
        "from": "bypass_exit.thrust",
        "to": "propulsive_force.gross_1",
        "kind": "force_link",
    }, {
        "id": "core_gross_force",
        "from": "core_exit.thrust",
        "to": "propulsive_force.gross_2",
        "kind": "force_link",
    }, {
        "id": "freestream_drag_force",
        "from": "freestream_momentum.drag",
        "to": "propulsive_force.drag",
        "kind": "force_link",
    }])

    for case in declaration["cases"]:
        case_id = case["id"]
        mach, static_temperature_rankine = FLIGHT_CONDITIONS[case_id]
        velocity = mach * math.sqrt(
            GAMMA * AIR_GAS_CONSTANT_J_KG_K *
            static_temperature_rankine * RANKINE_TO_KELVIN
        )
        case.setdefault("parameter_overrides", {})[
            "components.freestream_momentum.parameters.freestream_velocity"
        ] = {"value": velocity, "unit": "m/s"}
        guesses = case["initial_guesses"]
        for variable in ("p", "h"):
            station_key = f"station_2.outlet.{variable}"
            station_value = (
                guesses[station_key] if station_key in guesses
                else case["fixed_values"][station_key]
            )
            guesses[f"freestream_momentum.inlet.{variable}"] = copy.deepcopy(
                station_value
            )
            guesses[f"freestream_momentum.outlet.{variable}"] = copy.deepcopy(
                guesses[f"fan.inlet.{variable}"]
            )
        species = model["materials"][0]["species"]
        for name in species:
            variable = f"m_dot[{name}]"
            guesses[f"freestream_momentum.inlet.{variable}"] = copy.deepcopy(
                guesses[f"station_2.outlet.{variable}"]
            )
            guesses[f"freestream_momentum.outlet.{variable}"] = copy.deepcopy(
                guesses[f"fan.inlet.{variable}"]
            )
        source = reference_cases[case_id]["outputs"]
        guesses["freestream_momentum.drag.F"] = source[
            "ram_drag"
        ]["value_si"]
        guesses["propulsive_force.drag.F"] = source[
            "ram_drag"
        ]["value_si"]
        guesses["propulsive_force.gross_1.F"] = guesses[
            "bypass_exit.thrust.F"
        ]
        guesses["propulsive_force.gross_2.F"] = guesses[
            "core_exit.thrust.F"
        ]
        guesses["propulsive_force.net.F"] = source[
            "net_thrust"
        ]["value_si"]

    output = args.output
    if not output.is_absolute():
        output = repository / output
    output.write_text(
        json.dumps(declaration, indent=2) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
