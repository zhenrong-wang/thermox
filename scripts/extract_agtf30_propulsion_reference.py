#!/usr/bin/env python3
"""Extract traceable AGTF30 propulsion outputs from NASA outputs.mat.

This utility intentionally depends on scipy rather than carrying a private MAT
reader. It verifies the pinned public source checksum before interpreting the
published Y-vector positions declared by MEX_engine_model.c.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


SOURCE_SHA256 = (
    "d5a62c4d59825b5521f28d6a6f125e21f2f170ab0b7081d2dd536525f7f10f42"
)
SOURCE_REVISION = "17051fc00960fc8d03e0c8813c42535ab6cffce1"
LBF_TO_N = 4.4482216152605
SELECTED_POINTS = {
    0: "sea_level_static_1000",
    1: "sea_level_mach_03_1500",
    2: "alt_10000_mach_05_1750",
    3: "alt_25000_mach_06_2000",
    5: "alt_30000_mach_072_2000",
}
OUTPUTS = {
    "ram_drag": (55, "lbf"),
    "gross_thrust": (56, "lbf"),
    "net_thrust": (57, "lbf"),
    "bypass_gross_thrust": (58, "lbf"),
    "core_gross_thrust": (59, "lbf"),
    "tsfc": (60, "lbm/(h*lbf)"),
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    digest = hashlib.sha256(args.source.read_bytes()).hexdigest()
    if digest != SOURCE_SHA256:
        raise ValueError(
            f"unexpected AGTF30 outputs.mat checksum: {digest}"
        )
    try:
        import numpy as np
        from scipy.io import loadmat
    except ImportError as error:
        raise SystemExit(
            "numpy and scipy are required to extract MATLAB v5 data"
        ) from error

    source = loadmat(
        args.source, squeeze_me=True, struct_as_record=False
    )["outputs"]
    cases = []
    for index, case_id in SELECTED_POINTS.items():
        point = source[index]
        values = np.atleast_1d(point.Y)
        outputs = {}
        for name, (position, unit) in OUTPUTS.items():
            value = float(values[position])
            outputs[name] = {"source_value": value, "source_unit": unit}
            if unit == "lbf":
                outputs[name]["value_si"] = value * LBF_TO_N
                outputs[name]["dimension"] = "force"
                outputs[name]["unit_si"] = "N"
        gross = outputs["gross_thrust"]["source_value"]
        component_gross = (
            outputs["bypass_gross_thrust"]["source_value"]
            + outputs["core_gross_thrust"]["source_value"]
        )
        net = outputs["net_thrust"]["source_value"]
        reconstructed_net = (
            gross - outputs["ram_drag"]["source_value"]
        )
        if abs(gross - component_gross) > 1.0e-9:
            raise ValueError(
                f"gross-thrust source identity failed at index {index}"
            )
        if abs(net - reconstructed_net) > 1.0e-9:
            raise ValueError(
                f"net-thrust source identity failed at index {index}"
            )
        cases.append({
            "case_id": case_id,
            "source_array_index_zero_based": index,
            "coordinates": {
                "altitude_ft": float(point.altitude),
                "mach": float(point.mach_number),
                "corrected_fan_speed_rpm": float(point.N1c),
            },
            "outputs": outputs,
        })

    artifact = {
        "schema_version": "thermox.validation-reference/v1",
        "id": "nasa-agtf30-propulsion-outputs",
        "classification": "authoritative_public_cross_code_reference",
        "source": {
            "repository_revision": SOURCE_REVISION,
            "path": "outputs.mat",
            "sha256": SOURCE_SHA256,
            "field": "outputs[].Y",
            "index_evidence": "engine_model/MEX_engine_model.c:1806-1811",
        },
        "conversion_constants": {"lbf_to_n": LBF_TO_N},
        "cases": cases,
        "limitations": [
            "The values are outputs of NASA's public AGTF30/T-MATS code, not hardware measurements.",
            "TSFC remains in its published source unit; no SI conversion is asserted by this artifact.",
        ],
    }
    args.output.write_text(
        json.dumps(artifact, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
