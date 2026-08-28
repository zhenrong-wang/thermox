#!/usr/bin/env python3
"""Summarize a Thermox replay against the nonlinear NASA T-MATS series."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import statistics
from pathlib import Path


UNIT_TO_SI = {
    "s": 1.0,
    "rpm": 2.0 * math.pi / 60.0,
    "lbm/s": 0.45359237,
    "lbf": 4.4482216152605,
    "degR": 5.0 / 9.0,
    "psia": 6894.757293168,
}
STATIONS = {
    "s0": ("ambient", "outlet"),
    "s2": ("inlet", "outlet"),
    "s3": ("compressor", "outlet"),
    "s4": ("combustor", "outlet"),
    "s5": ("turbine", "outlet"),
    "s7": ("duct", "outlet"),
}
FIELDS = {
    "mass_flow": "m_dot_total",
    "total_temperature": "T",
    "total_pressure": "p",
}


def _component(graph: dict, component_id: str) -> dict:
    matches = [
        item
        for item in graph["components"]
        if item["component_id"] == component_id
    ]
    if len(matches) != 1:
        raise ValueError(f"graph must contain one component {component_id!r}")
    return matches[0]


def _port_value(graph: dict, component_id: str, port_name: str, name: str) -> float:
    component = _component(graph, component_id)
    ports = [item for item in component["ports"] if item["port_name"] == port_name]
    if len(ports) != 1:
        raise ValueError(f"component {component_id!r} must contain port {port_name!r}")
    values = ports[0]["primary_values"] + ports[0]["derived_values"]
    matches = [item["value_si"] for item in values if item["name"] == name]
    if len(matches) != 1:
        raise ValueError(f"port {component_id}.{port_name} must contain {name!r}")
    return matches[0]


def _prediction(graph: dict, signal_id: str) -> float:
    if signal_id == "shaft_speed":
        values = _component(graph, "shaft")["internal_values"]
        return next(item["value_si"] for item in values if item["name"] == "omega")
    if signal_id == "fuel_flow":
        return _port_value(graph, "fuel", "outlet", "m_dot_total")
    if signal_id == "net_thrust":
        return _port_value(graph, "nozzle", "thrust", "F")
    station, field = signal_id.split("_", 1)
    component_id, port_name = STATIONS[station]
    return _port_value(graph, component_id, port_name, FIELDS[field])


def analyze(reference: dict, result: dict) -> dict:
    if result.get("status") != "succeeded":
        raise ValueError("Thermox replay did not succeed")
    trajectory = {
        round(sample["time"], 12): sample["graph"]
        for sample in result["trajectory"]
    }
    summaries = []
    for signal in reference["signals"]:
        scale = UNIT_TO_SI.get(signal["unit"])
        if scale is None:
            raise ValueError(f"unsupported reference unit {signal['unit']!r}")
        relative_errors = []
        signed_errors = []
        for sample in signal["samples"]:
            graph = trajectory.get(round(sample["time"], 12))
            if graph is None:
                raise ValueError(
                    f"Thermox trajectory is missing exact time {sample['time']}"
                )
            expected = sample["value"] * scale
            predicted = _prediction(graph, signal["id"])
            signed_errors.append(predicted - expected)
            if abs(expected) > 1.0e-15:
                relative_errors.append(100.0 * (predicted / expected - 1.0))
        summaries.append(
            {
                "signal_id": signal["id"],
                "dimension": signal["dimension"],
                "sample_count": len(signed_errors),
                "minimum_relative_error_percent": min(relative_errors),
                "maximum_relative_error_percent": max(relative_errors),
                "mean_absolute_relative_error_percent": statistics.fmean(
                    abs(value) for value in relative_errors
                ),
                "final_relative_error_percent": relative_errors[-1],
                "maximum_absolute_error_si": max(abs(value) for value in signed_errors),
            }
        )

    predictive = [
        item
        for item in summaries
        if item["signal_id"]
        not in {
            "fuel_flow",
            "s0_total_temperature",
            "s0_total_pressure",
            "s2_total_temperature",
            "s2_total_pressure",
        }
    ]
    closed_loop = (
        result["metadata"]["model"]["case_id"] ==
        "nasa_speed_control_replay"
    )
    return {
        "schema_version": "thermox.cross_code_trajectory_summary/v1",
        "id": (
            "nasa_tmats_simple_gas_turbine_closed_loop_comparison"
            if closed_loop
            else "nasa_tmats_simple_gas_turbine_open_loop_aligned"
        ),
        "classification": (
            "public_source_full_nonlinear_closed_loop_cross_code_validation"
            if closed_loop
            else "public_source_full_nonlinear_open_loop_cross_code_validation"
        ),
        "reference_artifact_id": reference["id"],
        "reference_source_sha256": reference["source"]["checksum_sha256"],
        "model_id": result["metadata"]["model"]["id"],
        "case_id": result["metadata"]["model"]["case_id"],
        "simulation_diagnostics": result["diagnostics"],
        "comparison": {
            "alignment": "701 exact timestamps on the 0.015 s NASA grid",
            "forced_input": (
                "NASA speed-demand history; Thermox PI predicts fuel flow"
                if closed_loop
                else "NASA fuel-flow history with linear interpolation"
            ),
            "signal_count": len(summaries),
            "sample_comparison_count": sum(
                item["sample_count"] for item in summaries
            ),
            "predictive_signal_mean_mape_percent": statistics.fmean(
                item["mean_absolute_relative_error_percent"] for item in predictive
            ),
            "signals": summaries,
        },
        "interpretation": {
            "positive": [
                "The generic nonlinear DAE integrated the complete 10.5 s replay.",
                "All 701 NASA timestamps aligned exactly; no data resampling or fitted trajectory was used.",
                (
                    "Fuel flow is predicted by the generic sensor/PI/source control path."
                    if closed_loop
                    else "Fuel input is reproduced exactly to isolate plant physics."
                ),
                "The plant uses source-declared maps, losses, LHV, nozzle area, and shaft inertia without trajectory fitting.",
            ],
            "retained_gaps": [
                "T-MATS FAR-table properties and Thermox equilibrium species properties remain different model forms.",
                "The generic nozzle uses a thermally-perfect critical-pressure estimate rather than T-MATS' complete gamma/R tables.",
                (
                    "The open-loop replay excludes NASA's sensor and PI controller."
                    if not closed_loop
                    else "Controller coefficients reproduce the published example and are not independently identified hardware parameters."
                ),
                "The source is computational cross-code evidence, not hardware measurement.",
            ],
            "qualification": "cross_code_validation_not_hardware_qualification",
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("result", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    reference = json.loads(args.reference.read_text(encoding="utf-8"))
    raw_result = args.result.read_bytes()
    result = json.loads(raw_result)
    summary = analyze(reference, result)
    summary["thermox_result_sha256"] = hashlib.sha256(raw_result).hexdigest()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
