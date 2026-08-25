#!/usr/bin/env python3
"""Convert a generated T-MATS compressor map into a Thermox artifact.

Generated T-MATS C models store compressor maps on R-line and normalized
corrected-speed axes. This importer preserves R-line as the primary
coordinate and records every component scaler without fitting output data.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
import re


POUND_MASS_TO_KILOGRAM = 0.45359237
RPM_TO_RADIAN_PER_SECOND = 2.0 * math.pi / 60.0


def _numbers(body: str) -> list[float]:
    body = re.sub(r"/\*.*?\*/", " ", body, flags=re.DOTALL)
    body = re.sub(r"//[^\n]*", " ", body)
    return [
        float(value)
        for value in re.findall(
            r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[Ee][-+]?\d+)?",
            body,
        )
    ]


def _array(text: str, name: str) -> list[float]:
    match = re.search(
        rf"\bdouble\s+{re.escape(name)}\s*\[[^]]+\]\s*=\s*"
        r"\{(.*?)\}\s*;",
        text,
        flags=re.DOTALL,
    )
    if match is None:
        raise ValueError(f"missing C array {name}")
    values = _numbers(match.group(1))
    if not values:
        raise ValueError(f"C array {name} is empty")
    return values


def _scalar(text: str, name: str) -> float:
    match = re.search(
        rf"\bdouble\s+{re.escape(name)}\s*=\s*"
        r"([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[Ee][-+]?\d+)?)\s*;",
        text,
    )
    if match is None:
        raise ValueError(f"missing C scalar {name}")
    return float(match.group(1))


def convert(
    source: pathlib.Path,
    prefix: str,
    artifact_id: str,
    revision: str,
) -> dict:
    raw = source.read_bytes()
    text = raw.decode("utf-8")
    speeds = _array(text, f"{prefix}_Y_C_Map_NcVec")
    coordinates = _array(text, f"{prefix}_X_C_RlineVec")
    raw_flows = _array(text, f"{prefix}_T_C_Map_WcArray")
    raw_pressure_ratios = _array(text, f"{prefix}_T_C_Map_PRArray")
    raw_efficiencies = _array(text, f"{prefix}_T_C_Map_EffArray")
    expected = len(speeds) * len(coordinates)
    for name, values in (
        ("corrected flow", raw_flows),
        ("pressure ratio", raw_pressure_ratios),
        ("efficiency", raw_efficiencies),
    ):
        if len(values) != expected:
            raise ValueError(
                f"{name} array must contain speed_count * R-line_count "
                f"entries; expected {expected}, observed {len(values)}"
            )

    speed_scale = _scalar(text, f"{prefix}_s_C_Nc")
    component_scales = {
        "flow_capacity_scale": _scalar(text, f"{prefix}_s_C_Wc"),
        "pressure_ratio_scale": _scalar(text, f"{prefix}_s_C_PR"),
        "efficiency_scale": _scalar(text, f"{prefix}_s_C_Eff"),
    }
    if not math.isfinite(speed_scale) or speed_scale <= 0.0:
        raise ValueError("T-MATS compressor speed scale must be positive")
    if any(
        not math.isfinite(value) or value <= 0.0
        for value in component_scales.values()
    ):
        raise ValueError("T-MATS compressor scalers must be positive")

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
                # T-MATS maps legitimately include the zero-loading PR=1
                # boundary.  Keeping it makes the imported artifact faithful
                # and leaves operating-domain selection to the component.
                "minimum_inclusive": True,
            },
            {
                "output": "isentropic_efficiency",
                "minimum": 0.0,
                "maximum": 1.0,
                # Some source maps use zero-valued padding at an unused
                # boundary. Preserve that audited source datum; the solver's
                # admissible operating point still requires positive
                # efficiency.
                "minimum_inclusive": True,
                "maximum_inclusive": True,
            },
        ],
        "curves": [],
    }
    for speed_index, normalized_speed in enumerate(speeds):
        samples = []
        for coordinate_index, coordinate in enumerate(coordinates):
            # interp2Ac receives R-line as X and speed as Y. Generated C
            # storage is R-line-major: all speeds for one R-line value.
            flat_index = coordinate_index * len(speeds) + speed_index
            samples.append(
                {
                    "coordinate": coordinate,
                    "outputs": [
                        raw_flows[flat_index] * POUND_MASS_TO_KILOGRAM,
                        raw_pressure_ratios[flat_index],
                        raw_efficiencies[flat_index],
                    ],
                }
            )
        payload["curves"].append(
            {
                "family_coordinate": (
                    normalized_speed
                    * speed_scale
                    * RPM_TO_RADIAN_PER_SECOND
                ),
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
            "source_format": "T-MATS generated C compressor structure",
            "source_prefix": prefix,
            "source_coordinates": {
                "primary": "X_C_RlineVec",
                "family": "Y_C_Map_NcVec * s_C_Nc",
            },
            "source_component_scales": component_scales,
            "source_speed_scale_rpm": speed_scale,
            "unit_conversions": {
                "corrected_mass_flow": "lbm/s to kg/s",
                "corrected_speed": "rpm to rad/s",
            },
            "storage_order": "R-line-major, speed-minor",
            "curve_count": len(speeds),
            "samples_per_curve": len(coordinates),
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--id", required=True)
    parser.add_argument("--revision", default="agtf30-17051fc")
    args = parser.parse_args()
    artifact = convert(args.source, args.prefix, args.id, args.revision)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(artifact, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
