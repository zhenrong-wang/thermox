#!/usr/bin/env python3
"""Build the AGTF30 continuous twin-spool graph with source-declared ducts."""

from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path


IN2_TO_M2 = 0.00064516
SPECIES = ["CH4", "O2", "N2", "CO2", "H2O", "CO", "H2"]

DUCTS = {
    "duct_2": (286.9, 0.45, 0.010),
    "duct_25": (115.6, 0.45, 0.015),
    "duct_17": (6917.7, 0.45, 0.015),
    "duct_45": (66.3, 0.30, 0.005),
    "duct_5": (945.0, 0.35, 0.010),
}


def _duct(component_id: str, data: tuple[float, float, float]) -> dict:
    area_in2, design_mach, loss_fraction = data
    return {
        "id": component_id,
        "kind": "transport.material.perfect_gas_mach_scaled_loss",
        "version": "1.0.0",
        "parameters": {
            "flow_area": area_in2 * IN2_TO_M2,
            "design_mach": design_mach,
            "design_pressure_loss_fraction": loss_fraction,
            "heat_capacity_ratio": 1.4,
        },
        "materials": {"inlet": "reacting_gas", "outlet": "reacting_gas"},
    }


def _replace_connection(
    connections: list[dict], connection_id: str,
    first: dict, second: dict,
) -> None:
    index = next(
        i for i, connection in enumerate(connections)
        if connection["id"] == connection_id
    )
    connections[index:index + 1] = [first, second]


def _copy_port(
    guesses: dict, target: str, source: str,
) -> None:
    for variable in ("p", "h", *(f"m_dot[{name}]" for name in SPECIES)):
        key = f"{source}.{variable}"
        if key in guesses:
            guesses[f"{target}.{variable}"] = copy.deepcopy(guesses[key])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument(
        "--output", type=Path,
        default=Path(
            "benchmarks/nasa_tmats/agtf30_ducted_twin_spool.json"
        ),
    )
    args = parser.parse_args()
    repository = args.repository.resolve()
    source = json.loads(
        (repository /
         "benchmarks/nasa_tmats/agtf30_continuous_twin_spool.json")
        .read_text(encoding="utf-8")
    )
    declaration = copy.deepcopy(source)
    model = declaration["model"]
    model["id"] = "nasa_agtf30_ducted_twin_spool"
    model["name"] = "NASA AGTF30 continuous twin-spool gas path with ducts"
    model["revision"] = "1"
    model["components"].extend(
        _duct(component_id, data)
        for component_id, data in DUCTS.items()
    )
    connections = model["connections"]
    _replace_connection(
        connections, "split_core",
        {"id": "split_duct2", "from": "fan_splitter.outlet_b",
         "to": "duct_2.inlet", "kind": "material_link"},
        {"id": "duct2_lpc", "from": "duct_2.outlet",
         "to": "lpc.inlet", "kind": "material_link"},
    )
    _replace_connection(
        connections, "split_bypass",
        {"id": "split_duct17", "from": "fan_splitter.outlet_a",
         "to": "duct_17.inlet", "kind": "material_link"},
        {"id": "duct17_bypass", "from": "duct_17.outlet",
         "to": "bypass_exit.inlet", "kind": "material_link"},
    )
    _replace_connection(
        connections, "lpc_hpc",
        {"id": "lpc_duct25", "from": "lpc.outlet",
         "to": "duct_25.inlet", "kind": "material_link"},
        {"id": "duct25_hpc", "from": "duct_25.outlet",
         "to": "hpc.inlet", "kind": "material_link"},
    )
    _replace_connection(
        connections, "hpt_lpt",
        {"id": "hpt_duct45", "from": "hpt.outlet",
         "to": "duct_45.inlet", "kind": "material_link"},
        {"id": "duct45_lpt", "from": "duct_45.outlet",
         "to": "lpt.inlet", "kind": "material_link"},
    )
    _replace_connection(
        connections, "lpt_core_exit",
        {"id": "lpt_duct5", "from": "lpt.outlet",
         "to": "duct_5.inlet", "kind": "material_link"},
        {"id": "duct5_core_exit", "from": "duct_5.outlet",
         "to": "core_exit.inlet", "kind": "material_link"},
    )

    port_guesses = {
        "duct_2": ("fan_splitter.outlet_b", 0.45),
        "duct_17": ("fan_splitter.outlet_a", 0.10),
        "duct_25": ("lpc.outlet", 0.45),
        "duct_45": ("hpt.outlet", 0.30),
        "duct_5": ("lpt.outlet", 0.35),
    }
    for case in declaration["cases"]:
        guesses = case["initial_guesses"]
        for duct_id, (upstream, mach) in port_guesses.items():
            _copy_port(guesses, f"{duct_id}.inlet", upstream)
            _copy_port(guesses, f"{duct_id}.outlet", upstream)
            guesses[f"{duct_id}.mach"] = mach

    destination = (repository / args.output).resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(
        json.dumps(declaration, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
