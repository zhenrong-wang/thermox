#!/usr/bin/env python3
"""Add a map-driven variable bleed valve to the nozzle-closed AGTF30 graph."""

from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path


PSIA_TO_PA = 6894.757293168
REFERENCE_PRESSURE_PSIA = 14.696
REFERENCE_TEMPERATURE_DEG_R = 518.67
DEG_R_TO_K = 5.0 / 9.0
SPECIES = ["CH4", "O2", "N2", "CO2", "H2O", "CO", "H2"]


def _copy_port(guesses: dict, target: str, source: str) -> None:
    for variable in ("p", "h", *(f"m_dot[{name}]" for name in SPECIES)):
        source_key = f"{source}.{variable}"
        if source_key in guesses:
            guesses[f"{target}.{variable}"] = copy.deepcopy(
                guesses[source_key]
            )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument(
        "--output", type=Path,
        default=Path(
            "benchmarks/nasa_tmats/agtf30_vbv_nozzle_twin_spool.json"
        ),
    )
    args = parser.parse_args()
    repository = args.repository.resolve()
    declaration = json.loads(
        (repository /
         "benchmarks/nasa_tmats/agtf30_nozzle_closed_twin_spool.json")
        .read_text(encoding="utf-8")
    )
    model = declaration["model"]
    model["id"] = "nasa_agtf30_vbv_nozzle_twin_spool"
    model["name"] = "NASA AGTF30 nozzle-closed twin spool with VBV"
    model["revision"] = "1"
    model["components"].append({
        "id": "vbv",
        "kind": "junction.material.cross_bleed.performance_map",
        "version": "1.0.0",
        "parameters": {
            "reference_pressure":
                REFERENCE_PRESSURE_PSIA * PSIA_TO_PA,
            "reference_temperature":
                REFERENCE_TEMPERATURE_DEG_R * DEG_R_TO_K,
            "flow_capacity_scale": 1.0,
        },
        "artifacts": {"flow_characteristic": "nasa-agtf30-vbv-map"},
        "materials": {
            "donor_inlet": "reacting_gas",
            "donor_outlet": "reacting_gas",
            "receiver_inlet": "reacting_gas",
            "receiver_outlet": "reacting_gas",
        },
    })

    removed = {"split_duct17", "lpc_duct25"}
    model["connections"] = [
        connection for connection in model["connections"]
        if connection["id"] not in removed
    ]
    model["connections"].extend([
        {"id": "split_vbv_receiver", "from": "fan_splitter.outlet_a",
         "to": "vbv.receiver_inlet", "kind": "material_link"},
        {"id": "lpc_vbv_donor", "from": "lpc.outlet",
         "to": "vbv.donor_inlet", "kind": "material_link"},
        {"id": "vbv_receiver_duct17", "from": "vbv.receiver_outlet",
         "to": "duct_17.inlet", "kind": "material_link"},
        {"id": "vbv_donor_duct25", "from": "vbv.donor_outlet",
         "to": "duct_25.inlet", "kind": "material_link"},
    ])

    for case in declaration["cases"]:
        fixed = case["fixed_values"]
        guesses = case["initial_guesses"]
        fixed["vbv.position.value"] = 0.0
        guesses["vbv.bleed_mass_flow"] = {
            "value": 0.0, "unit": "kg/s"
        }
        _copy_port(guesses, "vbv.receiver_inlet", "fan_splitter.outlet_a")
        _copy_port(guesses, "vbv.receiver_outlet", "fan_splitter.outlet_a")
        _copy_port(guesses, "vbv.donor_inlet", "lpc.outlet")
        _copy_port(guesses, "vbv.donor_outlet", "lpc.outlet")

    destination = (repository / args.output).resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(
        json.dumps(declaration, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
