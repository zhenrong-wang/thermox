#!/usr/bin/env python3
"""Convert a T-MATS MATLAB compressor setup into a Thermox map artifact.

The converter intentionally understands the public ``setup_HPC.m`` data
layout, not arbitrary MATLAB.  It preserves R-line as the primary coordinate,
which avoids the lossy inversion of speed lines near choke where corrected
flow may repeat while pressure ratio continues to change.
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
        rf"MWS\.HPC\.{re.escape(name)}\s*=\s*\[(.*?)\]\s*;",
        text,
        flags=re.DOTALL,
    )
    if match is None:
        raise ValueError(f"missing MWS.HPC.{name} matrix")
    body = re.sub(r"%[^\n]*", "", match.group(1)).replace("...", " ")
    rows = []
    for row in body.split(";"):
        values = [float(value) for value in row.split()]
        if values:
            rows.append(values)
    if not rows:
        raise ValueError(f"MWS.HPC.{name} is empty")
    return rows


def _vector(text: str, name: str) -> list[float]:
    rows = _matrix(text, name)
    return [value for row in rows for value in row]


def _scalar(text: str, name: str) -> float:
    match = re.search(
        rf"MWS\.HPC\.{re.escape(name)}\s*=\s*"
        r"([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[Ee][-+]?\d+)?)\s*;",
        text,
    )
    if match is None:
        raise ValueError(f"missing MWS.HPC.{name} scalar")
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


def convert(source: pathlib.Path, artifact_id: str, revision: str) -> dict:
    raw = source.read_bytes()
    text = raw.decode("utf-8")
    speeds = _vector(text, "NcVec")
    coordinates = _vector(text, "RlineVec")
    flows = _matrix(text, "WcArray")
    pressure_ratios = _matrix(text, "PRArray")
    efficiencies = _matrix(text, "EffArray")
    for name, values in (
        ("WcArray", flows),
        ("PRArray", pressure_ratios),
        ("EffArray", efficiencies),
    ):
        _require_shape(name, values, len(speeds), len(coordinates))

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
    speed_scale = _scalar(text, "s_Nc")
    for speed_index, normalized_speed in enumerate(speeds):
        samples = []
        for coordinate_index, coordinate in enumerate(coordinates):
            samples.append(
                {
                    "coordinate": coordinate,
                    "outputs": [
                        flows[speed_index][coordinate_index]
                        * POUND_MASS_TO_KILOGRAM,
                        pressure_ratios[speed_index][coordinate_index],
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
            "source_format": "T-MATS setup_HPC.m",
            "source_coordinates": {
                "primary": "RlineVec",
                "family": "NcVec * s_Nc",
            },
            "unit_conversions": {
                "corrected_mass_flow": "lbm/s to kg/s",
                "corrected_speed": "rpm to rad/s",
            },
            "component_scales": {
                "flow_capacity_scale": _scalar(text, "s_Wc"),
                "pressure_ratio_scale": _scalar(text, "s_PR"),
                "efficiency_scale": _scalar(text, "s_eff"),
            },
            "curve_count": len(speeds),
            "samples_per_curve": len(coordinates),
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--id", default="nasa-tmats-example-hpc-map")
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
