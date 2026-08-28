#!/usr/bin/env python3
"""Import the pinned NASA T-MATS nonlinear trajectory as validation evidence."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path


SIGNALS = {
    "shaft_speed_rpm": ("shaft_speed", "angular_speed", "rpm"),
    "fuel_flow_lbm_s": ("fuel_flow", "mass_flow", "lbm/s"),
    "net_thrust_lbf": ("net_thrust", "force", "lbf"),
}
for station in ("s0", "s2", "s3", "s4", "s5", "s7"):
    SIGNALS[f"{station}_mass_flow_lbm_s"] = (
        f"{station}_mass_flow", "mass_flow", "lbm/s"
    )
    SIGNALS[f"{station}_total_temperature_degR"] = (
        f"{station}_total_temperature", "temperature", "degR"
    )
    SIGNALS[f"{station}_total_pressure_psia"] = (
        f"{station}_total_pressure", "pressure", "psia"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    raw = args.input.read_bytes()
    checksum = hashlib.sha256(raw).hexdigest()
    with args.input.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        required = {"time_s", "solver_iterations", *SIGNALS}
        if set(reader.fieldnames or ()) != required:
            raise ValueError("T-MATS export columns do not match the pinned contract")
        rows = list(reader)

    if len(rows) != 701:
        raise ValueError("T-MATS export must contain exactly 701 samples")
    times = [float(row["time_s"]) for row in rows]
    if abs(times[0]) > 1.0e-15 or abs(times[-1] - 10.5) > 1.0e-12:
        raise ValueError("T-MATS export time extent is not 0 through 10.5 s")
    for index, time in enumerate(times):
        if not math.isfinite(time) or (
            index and abs(time - times[index - 1] - 0.015) > 1.0e-12
        ):
            raise ValueError("T-MATS export time grid is not the expected 0.015 s grid")

    signals = []
    for column, (signal_id, dimension, unit) in SIGNALS.items():
        samples = []
        for time, row in zip(times, rows, strict=True):
            value = float(row[column])
            if not math.isfinite(value):
                raise ValueError(f"non-finite {column} sample")
            samples.append({"time": time, "value": value})
        signals.append({
            "id": signal_id,
            "dimension": dimension,
            "unit": unit,
            "samples": samples,
        })

    artifact = {
        "schema_version": "thermox.validation_series/v1",
        "id": "nasa_tmats_simple_gas_turbine_nonlinear_dynamic",
        "source": {
            "reference": (
                "NASA T-MATS GasTurbine_Dyn_Template.mdl at "
                "ad6e4d5d0d76c229db4eb72ca40ef58d5ddc4014; "
                "executed with MATLAB R2023a / Simulink 10.7"
            ),
            "checksum_sha256": checksum,
            "evidence_basis": "derived_reference",
            "acquisition": "computational",
            "note": (
                "Full nonlinear NASA reference trajectory on the model's "
                "native 0.015 s output grid."
            ),
            "limitations": [
                "The source is a computational T-MATS example, not hardware measurement.",
                "NASA's public maps, shaft inertia, controller, thermodynamic conventions, and solver settings define the comparison scope.",
                "Agreement qualifies cross-code implementation behavior only and cannot establish physical engine accuracy.",
            ],
        },
        "time_unit": "s",
        "signals": signals,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(artifact, indent=2) + "\n", encoding="utf-8"
    )
    print(f"validation_series={args.output}")
    print(f"source_sha256={checksum}")


if __name__ == "__main__":
    main()
