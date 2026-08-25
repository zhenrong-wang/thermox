#!/usr/bin/env python3
"""Convert a generated T-MATS turbine map into a Thermox artifact.

The generated AGTF-style C model stores turbine maps on pressure-ratio and
corrected-speed axes.  This importer preserves that orientation and applies
the declared T-MATS component scalers and unit conversions exactly once.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
import re


POUND_MASS_TO_KILOGRAM = 0.45359237
PSIA_STANDARD = 14.696
RANKINE_STANDARD = 518.67
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
    reference_pressure_psia: float = PSIA_STANDARD,
    reference_temperature_deg_r: float = RANKINE_STANDARD,
) -> dict:
    raw = source.read_bytes()
    text = raw.decode("utf-8")
    speeds = _array(text, f"{prefix}_Y_T_NcVec")
    raw_pressure_ratios = _array(text, f"{prefix}_X_T_PRVec")
    raw_flows = _array(text, f"{prefix}_T_T_Map_WcArray")
    raw_efficiencies = _array(text, f"{prefix}_T_T_Map_EffArray")
    expected = len(speeds) * len(raw_pressure_ratios)
    if len(raw_flows) != expected or len(raw_efficiencies) != expected:
        raise ValueError(
            "turbine map arrays must have speed_count * pressure_ratio_count "
            f"entries; expected {expected}, observed flow={len(raw_flows)}, "
            f"efficiency={len(raw_efficiencies)}"
        )

    speed_scale = _scalar(text, f"{prefix}_s_T_Nc")
    flow_scale = _scalar(text, f"{prefix}_s_T_Wc")
    pressure_ratio_scale = _scalar(text, f"{prefix}_s_T_PR")
    efficiency_scale = _scalar(text, f"{prefix}_s_T_Eff")
    scalers = {
        "corrected_speed": speed_scale,
        "corrected_mass_flow": flow_scale,
        "pressure_ratio": pressure_ratio_scale,
        "isentropic_efficiency": efficiency_scale,
    }
    if any(not math.isfinite(value) or value <= 0.0 for value in scalers.values()):
        raise ValueError("T-MATS turbine scalers must be positive and finite")
    if reference_pressure_psia <= 0.0 or reference_temperature_deg_r <= 0.0:
        raise ValueError("reference pressure and temperature must be positive")

    flow_to_si_standard = (
        POUND_MASS_TO_KILOGRAM
        * reference_pressure_psia
        / math.sqrt(reference_temperature_deg_r)
    )
    speed_to_si_standard = (
        math.sqrt(reference_temperature_deg_r)
        * RPM_TO_RADIAN_PER_SECOND
    )
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
                # Preserve the zero-expansion boundary present in canonical
                # T-MATS tables. Component equations still select a
                # physically admissible operating point above this boundary.
                "minimum_inclusive": True,
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
    actual_pressure_ratios = [
        1.0 + pressure_ratio_scale * (value - 1.0)
        for value in raw_pressure_ratios
    ]
    for speed_index, speed in enumerate(speeds):
        samples = []
        for pressure_index, pressure_ratio in enumerate(actual_pressure_ratios):
            # T-MATS passes these C arrays to interp2Ac with pressure ratio
            # as X and corrected speed as Y. The generated storage is
            # pressure-major: all speed values for one pressure coordinate.
            flat_index = pressure_index * len(speeds) + speed_index
            samples.append(
                {
                    "coordinate": pressure_ratio,
                    "outputs": [
                        raw_flows[flat_index] * flow_scale * flow_to_si_standard,
                        pressure_ratio,
                        raw_efficiencies[flat_index] * efficiency_scale,
                    ],
                }
            )
        payload["curves"].append(
            {
                "family_coordinate": (
                    speed * speed_scale * speed_to_si_standard
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
            "source_format": "T-MATS generated C turbine structure",
            "source_prefix": prefix,
            "source_coordinates": {
                "primary": "X_T_PRVec transformed by s_T_PR",
                "family": "Y_T_NcVec transformed by s_T_Nc",
            },
            "source_component_scales": scalers,
            "reference_pressure_psia": reference_pressure_psia,
            "reference_temperature_degR": reference_temperature_deg_r,
            "unit_conversions": {
                "corrected_mass_flow": (
                    "lbm/s*sqrt(degR)/psia to kg/s at declared reference state"
                ),
                "corrected_speed": (
                    "rpm/sqrt(degR) to rad/s at declared reference state"
                ),
            },
            "curve_count": len(speeds),
            "samples_per_curve": len(raw_pressure_ratios),
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--prefix", default="GTF_hpt")
    parser.add_argument("--id", default="nasa-agtf30-hpt-map")
    parser.add_argument("--revision", default="agtf30-17051fc")
    parser.add_argument("--reference-pressure-psia", type=float, default=PSIA_STANDARD)
    parser.add_argument(
        "--reference-temperature-deg-r", type=float, default=RANKINE_STANDARD
    )
    args = parser.parse_args()
    artifact = convert(
        args.source,
        args.prefix,
        args.id,
        args.revision,
        args.reference_pressure_psia,
        args.reference_temperature_deg_r,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(artifact, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
