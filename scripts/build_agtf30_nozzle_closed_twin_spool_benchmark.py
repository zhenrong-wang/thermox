#!/usr/bin/env python3
"""Build AGTF30 twin-spool gas paths closed by convergent nozzles."""

from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path


IN2_TO_M2 = 0.00064516
PSIA_TO_PA = 6894.757293168
REFERENCE_AMBIENT_PSIA = 14.696
SPECIES = ["CH4", "O2", "N2", "CO2", "H2O", "CO", "H2"]

CASE_COMMANDS = {
    "sea_level_mach_03_1500": (5487.0, 14.696),
    "alt_10000_mach_05_1750": (4775.0, 10.108),
    "alt_25000_mach_06_2000": (4681.0, 5.461),
    "alt_30000_mach_072_2000": (4543.8, 4.373),
}


def _nozzle(
    component_id: str, reference_area_in2: float,
    thrust_coefficient: float,
) -> dict:
    return {
        "id": component_id,
        "kind": "terminal.material.convergent_nozzle",
        "version": "1.0.0",
        "parameters": {
            "reference_throat_area": reference_area_in2 * IN2_TO_M2,
            "reference_back_pressure":
                REFERENCE_AMBIENT_PSIA * PSIA_TO_PA,
            "discharge_coefficient": 1.0,
            "velocity_coefficient": thrust_coefficient,
        },
        "materials": {"inlet": "reacting_gas"},
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument(
        "--output", type=Path,
        default=Path(
            "benchmarks/nasa_tmats/agtf30_nozzle_closed_twin_spool.json"
        ),
    )
    parser.add_argument(
        "--warm-start-directory", type=Path,
        help=(
            "directory containing converged ducted_<case>.json results; "
            "used only to condition initial guesses"
        ),
    )
    args = parser.parse_args()
    repository = args.repository.resolve()
    declaration = json.loads(
        (repository / "benchmarks/nasa_tmats/agtf30_ducted_twin_spool.json")
        .read_text(encoding="utf-8")
    )
    model = declaration["model"]
    model["id"] = "nasa_agtf30_nozzle_closed_twin_spool"
    model["name"] = "NASA AGTF30 nozzle-closed twin-spool gas paths"
    model["revision"] = "1"
    replacements = {
        "bypass_exit": _nozzle("bypass_exit", 1.0, 0.9975),
        "core_exit": _nozzle("core_exit", 393.43, 0.9999),
    }
    model["components"] = [
        replacements.get(component["id"], component)
        for component in model["components"]
    ]

    for case in declaration["cases"]:
        vafn_in2, ambient_psia = CASE_COMMANDS[case["id"]]
        fixed = case["fixed_values"]
        guesses = case["initial_guesses"]
        guesses["fan_splitter.fraction.value"] = fixed.pop(
            "fan_splitter.fraction.value"
        )
        guesses["hpc.shaft.omega"] = fixed.pop("hpc.shaft.omega")
        fixed.update({
            "bypass_exit.area_ratio.value": vafn_in2,
            "bypass_exit.back_pressure_ratio.value":
                ambient_psia / REFERENCE_AMBIENT_PSIA,
            "core_exit.area_ratio.value": 1.0,
            "core_exit.back_pressure_ratio.value":
                ambient_psia / REFERENCE_AMBIENT_PSIA,
        })
        guesses.update({
            "bypass_exit.mach": 1.0,
            "bypass_exit.thrust.F": 500000.0,
            "core_exit.mach": 1.0,
            "core_exit.thrust.F": 50000.0,
        })
        # The parent benchmark assembled its initial state from independently
        # validated spool slices. Normalize every connected material endpoint
        # to its upstream guess before releasing the two nozzle constraints;
        # this changes initialization only, never an equation or boundary.
        for _ in range(3):
            for connection in model["connections"]:
                if connection["kind"] != "material_link":
                    continue
                source = connection["from"]
                target = connection["to"]
                for variable in (
                    "p", "h", *(f"m_dot[{name}]" for name in SPECIES)
                ):
                    source_key = f"{source}.{variable}"
                    target_key = f"{target}.{variable}"
                    if source_key in guesses and target_key not in fixed:
                        guesses[target_key] = copy.deepcopy(
                            guesses[source_key]
                        )
        if args.warm_start_directory is not None:
            result_path = (
                args.warm_start_directory /
                f"ducted_{case['id']}.json"
            ).resolve()
            result = json.loads(result_path.read_text(encoding="utf-8"))
            if result.get("status") != "succeeded":
                raise ValueError(f"warm-start result did not succeed: {result_path}")
            for component in result["graph"]["components"]:
                component_id = component["component_id"]
                for port in component["ports"]:
                    for value in port["primary_values"]:
                        key = (
                            f"{component_id}.{port['port_name']}."
                            f"{value['name']}"
                        )
                        if key not in fixed:
                            guesses[key] = value["value_si"]
                for value in component["internal_values"]:
                    key = f"{component_id}.{value['name']}"
                    if key not in fixed:
                        guesses[key] = value["value_si"]

    destination = (repository / args.output).resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(
        json.dumps(declaration, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
