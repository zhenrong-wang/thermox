#!/usr/bin/env python3
"""Build the generic nonlinear AGTF30 fuel-step transient declaration."""

from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path


DEFAULT_STEP_FRACTION = 0.01
DEFAULT_STEP_TIME_S = 0.001
REFERENCE_SAMPLE_TIMES_S = (0.002, 0.006, 0.014, 0.030, 0.050)
NASA_A_PER_S = (
    (-2.58828958, 1.11503138),
    (0.78936733, -1.88527248),
)
NASA_FUEL_B_RPM_S_PER_LBM_S = (4405.37064, 8570.82043)
KG_PER_POUND_MASS = 0.45359237
SOURCE_SHA256 = (
    "d5a62c4d59825b5521f28d6a6f125e21f2f170ab0b7081d2dd536525f7f10f42"
)


def nasa_linear_step_response(
    delta_fuel_lbm_s: float, sample_times_s: tuple[float, ...]
) -> list[dict[str, object]]:
    """Integrate the published two-state NASA A/B model deterministically."""

    state = [0.0, 0.0]
    time = 0.0
    integration_step = 1.0e-5

    def rate(value: list[float]) -> list[float]:
        return [
            NASA_A_PER_S[row][0] * value[0]
            + NASA_A_PER_S[row][1] * value[1]
            + NASA_FUEL_B_RPM_S_PER_LBM_S[row] * delta_fuel_lbm_s
            for row in range(2)
        ]

    def shifted(
        value: list[float], slope: list[float], scale: float
    ) -> list[float]:
        return [
            value[index] + scale * slope[index] for index in range(2)
        ]

    result: list[dict[str, object]] = []
    for target in sample_times_s:
        while time < target - 1.0e-15:
            step = min(integration_step, target - time)
            k1 = rate(state)
            k2 = rate(shifted(state, k1, 0.5 * step))
            k3 = rate(shifted(state, k2, 0.5 * step))
            k4 = rate(shifted(state, k3, step))
            state = [
                state[index]
                + step
                * (k1[index] + 2.0 * k2[index]
                   + 2.0 * k3[index] + k4[index])
                / 6.0
                for index in range(2)
            ]
            time += step
        result.append({
            "elapsed_time_s": target,
            "delta_speed_rpm": {"lp": state[0], "hp": state[1]},
        })
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(
            "benchmarks/nasa_tmats/agtf30_fuel_step_transient.json"
        ),
    )
    parser.add_argument(
        "--reference-output",
        type=Path,
        default=Path(
            "benchmarks/nasa_tmats/agtf30_fuel_step_linear_reference.json"
        ),
    )
    parser.add_argument(
        "--step-fraction", type=float, default=DEFAULT_STEP_FRACTION
    )
    parser.add_argument(
        "--step-time", type=float, default=DEFAULT_STEP_TIME_S
    )
    args = parser.parse_args()
    if not 0.0 < args.step_fraction <= 0.05:
        parser.error("--step-fraction must be in (0, 0.05]")
    if args.step_time <= 0.0:
        parser.error("--step-time must be positive")

    repository = args.repository.resolve()
    declaration = json.loads((
        repository /
        "benchmarks/nasa_tmats/agtf30_vbv_nozzle_transient.json"
    ).read_text(encoding="utf-8"))
    declaration["model"]["id"] = "nasa_agtf30_fuel_step_transient"
    declaration["model"]["name"] = (
        "NASA AGTF30 nonlinear open-loop fuel-step transient"
    )
    declaration["model"]["revision"] = "1"

    simulation_case = declaration["cases"][0]
    simulation_case["id"] = "sea_level_static_fuel_step_1pct"
    fixed_values = simulation_case["fixed_values"]
    nominal_fuel = fixed_values.pop("fuel.outlet.m_dot[CH4]")
    stepped_fuel = copy.deepcopy(nominal_fuel)
    if isinstance(stepped_fuel, dict):
        stepped_fuel["value"] *= 1.0 + args.step_fraction
    else:
        stepped_fuel *= 1.0 + args.step_fraction
    simulation_case["input_schedules"] = {
        "fuel.outlet.m_dot[CH4]": {
            "interpolation": "previous",
            "points": [
                {
                    "time": {"value": 0.0, "unit": "s"},
                    "value": nominal_fuel,
                },
                {
                    "time": {
                        "value": args.step_time,
                        "unit": "s",
                    },
                    "value": stepped_fuel,
                },
            ],
        }
    }

    nominal_fuel_kg_s = (
        nominal_fuel["value"]
        if isinstance(nominal_fuel, dict)
        else nominal_fuel
    )
    delta_fuel_kg_s = nominal_fuel_kg_s * args.step_fraction
    reference = {
        "schema_version": "thermox.validation.transient_reference/v1",
        "id": "nasa_agtf30_open_loop_fuel_step_linear_reference",
        "source": {
            "artifact": "NASA AGTF30 outputs.mat",
            "sha256": SOURCE_SHA256,
            "evidence_basis": "authoritative_public_cross_code",
            "limitations": [
                "The response is derived from NASA's local A/B model, "
                "not a nonlinear time history or hardware measurement.",
                "Thermox and NASA perturbations are compared about their "
                "respective initialized baselines."
            ],
        },
        "input": {
            "name": "fuel_flow",
            "step_time_s": args.step_time,
            "step_fraction_of_thermox_baseline": args.step_fraction,
            "delta_kg_s": delta_fuel_kg_s,
            "delta_lbm_s": delta_fuel_kg_s / KG_PER_POUND_MASS,
        },
        "linear_model": {
            "states": ["lp_speed_rpm", "hp_speed_rpm"],
            "A_per_s": NASA_A_PER_S,
            "fuel_B_rpm_s_per_lbm_s": NASA_FUEL_B_RPM_S_PER_LBM_S,
            "integration": "classical_rk4_fixed_1e-5_s",
        },
        "samples": nasa_linear_step_response(
            delta_fuel_kg_s / KG_PER_POUND_MASS,
            REFERENCE_SAMPLE_TIMES_S,
        ),
    }

    output = args.output
    if not output.is_absolute():
        output = repository / output
    output.write_text(
        json.dumps(declaration, indent=2) + "\n", encoding="utf-8"
    )
    reference_output = args.reference_output
    if not reference_output.is_absolute():
        reference_output = repository / reference_output
    reference_output.write_text(
        json.dumps(reference, indent=2) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
