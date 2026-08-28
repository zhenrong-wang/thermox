#!/usr/bin/env python3
"""Convert a public T-MATS MATLAB turbine setup into a Thermox map.

The importer intentionally accepts the ``setup_HPT.m`` data layout used by
NASA examples. It preserves the source pressure-ratio coordinate and keeps
the four T-MATS component scalers outside the immutable raw-map payload.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
import re
from typing import Sequence


POUND_MASS_TO_KILOGRAM = 0.45359237
RPM_TO_RADIAN_PER_SECOND = 2.0 * math.pi / 60.0


def _matrix(text: str, name: str) -> list[list[float]]:
    match = re.search(
        rf"MWS\.HPT\.{re.escape(name)}\s*=\s*\[(.*?)\]\s*;",
        text,
        flags=re.DOTALL,
    )
    if match is None:
        raise ValueError(f"missing MWS.HPT.{name} matrix")
    body = re.sub(r"%[^\n]*", "", match.group(1)).replace("...", " ")
    rows = []
    for row in body.split(";"):
        values = [float(value) for value in row.split()]
        if values:
            rows.append(values)
    if not rows:
        raise ValueError(f"MWS.HPT.{name} is empty")
    return rows


def _vector(text: str, name: str) -> list[float]:
    return [value for row in _matrix(text, name) for value in row]


def _scalar(text: str, name: str) -> float:
    match = re.search(
        rf"MWS\.HPT\.{re.escape(name)}\s*=\s*"
        r"([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[Ee][-+]?\d+)?)\s*;",
        text,
    )
    if match is None:
        raise ValueError(f"missing MWS.HPT.{name} scalar")
    return float(match.group(1))


def _require_shape(
    name: str,
    values: Sequence[Sequence[float]],
    rows: int,
    columns: int,
) -> None:
    if len(values) != rows or any(len(row) != columns for row in values):
        shape = [len(row) for row in values]
        raise ValueError(
            f"{name} must be {rows}x{columns}; observed row lengths {shape}"
        )


def convert(
    source: pathlib.Path,
    artifact_id: str,
    revision: str,
) -> dict:
    raw = source.read_bytes()
    text = raw.decode("utf-8")
    speeds = _vector(text, "NcVec")
    coordinates = _vector(text, "PRVec")
    flows = _matrix(text, "WcArray")
    efficiencies = _matrix(text, "EffArray")
    for name, values in (("WcArray", flows), ("EffArray", efficiencies)):
        _require_shape(name, values, len(speeds), len(coordinates))

    source_scales = {
        "flow_capacity_scale": _scalar(text, "s_Wc"),
        "pressure_ratio_scale": _scalar(text, "s_PR"),
        "efficiency_scale": _scalar(text, "s_eff"),
    }
    speed_scale = _scalar(text, "s_Nc")
    if any(
        not math.isfinite(value) or value <= 0.0
        for value in (*source_scales.values(), speed_scale)
    ):
        raise ValueError("T-MATS turbine scalers must be positive and finite")

    payload = {
        "primary_variable": {
            "name": "map_coordinate",
            "dimension": "dimensionless",
        },
        "family_variable": {
            "name": "corrected_speed",
            "dimension": "angular_speed",
        },
        "output_variables": [
            {"name": "corrected_mass_flow", "dimension": "mass_flow"},
            {"name": "pressure_ratio", "dimension": "dimensionless"},
            {
                "name": "isentropic_efficiency",
                "dimension": "dimensionless",
            },
        ],
        "output_constraints": [
            {
                "output": "corrected_mass_flow",
                "minimum": 0.0,
                "minimum_inclusive": False,
            },
            {
                "output": "pressure_ratio",
                "minimum": 1.0,
                "minimum_inclusive": False,
            },
            {
                "output": "isentropic_efficiency",
                "minimum": 0.0,
                "maximum": 1.0,
                "minimum_inclusive": False,
                "maximum_inclusive": True,
            },
        ],
        "curves": [],
    }
    for speed_index, normalized_speed in enumerate(speeds):
        samples = []
        for coordinate_index, coordinate in enumerate(coordinates):
            samples.append(
                {
                    "coordinate": coordinate,
                    "outputs": [
                        flows[speed_index][coordinate_index]
                        * POUND_MASS_TO_KILOGRAM,
                        coordinate,
                        efficiencies[speed_index][coordinate_index],
                    ],
                }
            )
        payload["curves"].append(
            {
                "family_coordinate": normalized_speed
                * speed_scale
                * RPM_TO_RADIAN_PER_SECOND,
                "samples": samples,
            }
        )

    canonical_payload = json.dumps(
        payload, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return {
        "id": artifact_id,
        "schema_version": "thermox.performance_map/v1",
        "revision": revision,
        "checksum_sha256": hashlib.sha256(canonical_payload).hexdigest(),
        "payload": payload,
        "import_audit": {
            "source_sha256": hashlib.sha256(raw).hexdigest(),
            "source_format": "T-MATS setup_HPT.m",
            "source_coordinates": {
                "primary": "PRVec",
                "family": "NcVec * s_Nc",
            },
            "unit_conversions": {
                "corrected_mass_flow": "lbm/s to kg/s",
                "corrected_speed": "rpm to rad/s",
            },
            "source_component_scales": source_scales,
            "source_speed_scale_rpm": speed_scale,
            "curve_count": len(speeds),
            "samples_per_curve": len(coordinates),
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--id", default="nasa-tmats-example-hpt-map")
    parser.add_argument("--revision", default="t-mats-ad6e4d5")
    args = parser.parse_args()
    artifact = convert(args.source, args.id, args.revision)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(artifact, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
