#!/usr/bin/env python3
"""Summarize exact-boundary T-MATS combustor and nozzle diagnostics."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


LBM_S_TO_KG_S = 0.45359237
LBF_TO_N = 4.4482216152605


def component(result: dict, component_id: str) -> dict:
    return next(
        item for item in result["graph"]["components"]
        if item["component_id"] == component_id
    )


def port_values(result: dict, component_id: str, port_name: str) -> dict:
    port = next(
        item for item in component(result, component_id)["ports"]
        if item["port_name"] == port_name
    )
    return {
        item["name"]: item["value_si"]
        for item in port["primary_values"] + port["derived_values"]
    }


def load(path: Path) -> tuple[dict, str]:
    raw = path.read_bytes()
    result = json.loads(raw)
    if result.get("status") != "succeeded":
        raise ValueError(f"slice did not succeed: {path}")
    return result, hashlib.sha256(raw).hexdigest()


def relative_percent(actual: float, reference: float) -> float:
    return 100.0 * (actual / reference - 1.0)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("combustor_result", type=Path)
    parser.add_argument("nozzle_capacity_result", type=Path)
    parser.add_argument("nozzle_area_result", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    combustor, combustor_hash = load(args.combustor_result)
    capacity, capacity_hash = load(args.nozzle_capacity_result)
    area, area_hash = load(args.nozzle_area_result)

    reference_temperature_k = 3077.9857342195064 * 5.0 / 9.0
    reference_flow_kg_s = 102.99996409294292 * LBM_S_TO_KG_S
    reference_thrust_n = 10665.721779868387 * LBF_TO_N
    combustor_temperature = port_values(
        combustor, "combustor", "outlet")["T"]
    capacity_flow = port_values(capacity, "gas", "outlet")["m_dot_total"]
    capacity_nozzle = component(capacity, "nozzle")
    capacity_thrust = port_values(capacity, "nozzle", "thrust")["F"]
    area_ratio = port_values(area, "nozzle", "area_ratio")["value"]
    area_thrust = port_values(area, "nozzle", "thrust")["F"]

    summary = {
        "schema_version": "thermox.component_boundary_comparison/v1",
        "id": "nasa_tmats_simple_gas_turbine_component_boundary_slices",
        "classification": "public_source_exact_boundary_cross_code_diagnostic",
        "inputs": {
            "combustor_result_sha256": combustor_hash,
            "nozzle_capacity_result_sha256": capacity_hash,
            "nozzle_area_result_sha256": area_hash,
        },
        "combustor": {
            "boundary": "exact NASA station 3 plus 3 lbm/s methane",
            "reference_outlet_temperature_k": reference_temperature_k,
            "thermox_outlet_temperature_k": combustor_temperature,
            "relative_error_percent": relative_percent(
                combustor_temperature, reference_temperature_k
            ),
        },
        "nozzle_fixed_oem_area": {
            "reference_flow_kg_s": reference_flow_kg_s,
            "thermox_flow_kg_s": capacity_flow,
            "flow_relative_error_percent": relative_percent(
                capacity_flow, reference_flow_kg_s
            ),
            "reference_thrust_n": reference_thrust_n,
            "thermox_thrust_n": capacity_thrust,
            "thrust_relative_error_percent": relative_percent(
                capacity_thrust, reference_thrust_n
            ),
            "mach": next(
                item["value_si"]
                for item in capacity_nozzle["internal_values"]
                if item["name"] == "mach"
            ),
        },
        "nozzle_fixed_measured_flow": {
            "required_area_ratio": area_ratio,
            "area_difference_percent": 100.0 * (area_ratio - 1.0),
            "thermox_thrust_n": area_thrust,
            "thrust_relative_error_percent": relative_percent(
                area_thrust, reference_thrust_n
            ),
        },
        "interpretation": (
            "Declared-LHV alignment removes the dominant combustor bias; "
            "remaining temperature and capacity differences are attributable "
            "to empirical FAR-table versus equilibrium-species properties."
        ),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
