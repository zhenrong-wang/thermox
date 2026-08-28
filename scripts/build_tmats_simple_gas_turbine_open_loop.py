#!/usr/bin/env python3
"""Build the generic Thermox plant for NASA's dynamic gas-turbine example.

The first validation slice replays the independently exported NASA fuel-flow
history. This deliberately excludes NASA's sensor and PI controller so plant
physics differences can be diagnosed before closing the speed-control loop.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


REFERENCE_ID = "nasa_tmats_simple_gas_turbine_nonlinear_dynamic"
AIR_FRACTIONS = {"N2": 0.768, "O2": 0.232}
PRODUCT_FLOWS_LBM_S = {
    "CH4": 0.0,
    "O2": 11.2,
    "N2": 76.8,
    "CO2": 8.25,
    "H2O": 6.75,
    "CO": 0.0,
    "H2": 0.0,
}
SPECIES = tuple(PRODUCT_FLOWS_LBM_S)


def _signal(artifact: dict, signal_id: str) -> dict:
    matches = [item for item in artifact["signals"] if item["id"] == signal_id]
    if len(matches) != 1:
        raise ValueError(f"reference must contain one {signal_id!r} signal")
    return matches[0]


def _value(value: float, unit: str) -> dict:
    return {"value": value, "unit": unit}


def _flow_guesses(prefix: str, flows: dict[str, float]) -> dict:
    return {
        f"{prefix}.m_dot[{species}]": _value(flows.get(species, 0.0), "lbm/s")
        for species in SPECIES
    }


def build(reference: dict) -> dict:
    if reference.get("id") != REFERENCE_ID:
        raise ValueError("unexpected nonlinear reference identity")
    fuel = _signal(reference, "fuel_flow")
    if fuel.get("unit") != "lbm/s" or len(fuel["samples"]) != 701:
        raise ValueError("fuel reference must contain 701 lbm/s samples")

    air_flows = {
        species: 100.0 * fraction for species, fraction in AIR_FRACTIONS.items()
    }
    components = [
        {
            "id": "ambient",
            "kind": "source.material.fixed_composition",
            "version": "1.0.0",
            "parameters": {
                f"mass_fraction[{name}]": fraction
                for name, fraction in AIR_FRACTIONS.items()
            },
            "materials": {"outlet": "reacting_gas"},
        },
        {
            "id": "inlet",
            "kind": "transport.material.frozen_pressure_ratio",
            "version": "1.0.0",
            "parameters": {"pressure_ratio": 0.98},
            "materials": {"inlet": "reacting_gas", "outlet": "reacting_gas"},
        },
        {
            "id": "compressor",
            "kind": "compressor.material.coordinate_map",
            "version": "1.0.0",
            "parameters": {
                "reference_pressure": _value(14.696, "psia"),
                "reference_temperature": _value(518.67, "degR"),
                "flow_capacity_scale": 0.495342,
                "pressure_ratio_scale": 0.863640,
                "efficiency_scale": 0.997653,
            },
            "artifacts": {"performance_map": "nasa-tmats-example-hpc-map"},
            "materials": {"inlet": "reacting_gas", "outlet": "reacting_gas"},
        },
        {
            "id": "fuel",
            "kind": "source.material.fixed_composition",
            "version": "1.0.0",
            "parameters": {"mass_fraction[CH4]": 1.0},
            "materials": {"outlet": "reacting_gas"},
        },
        {
            "id": "combustor",
            "kind": "combustor.material.equilibrium_declared_lhv",
            "version": "1.0.0",
            "parameters": {
                "pressure_ratio": 0.95,
                "combustion_efficiency": 1.0,
                "fuel_lower_heating_value": _value(42.79846824, "MJ/kg"),
            },
            "materials": {
                "air_inlet": "reacting_gas",
                "fuel_inlet": "reacting_gas",
                "outlet": "reacting_gas",
            },
        },
        {
            "id": "turbine",
            "kind": "turbine.material.coordinate_map",
            "version": "1.0.0",
            "parameters": {
                "reference_pressure": _value(14.696, "psia"),
                "reference_temperature": _value(518.67, "degR"),
                "flow_capacity_scale": 0.444192,
                "pressure_ratio_scale": 0.511630,
                "efficiency_scale": 0.998914,
            },
            "artifacts": {"performance_map": "nasa-tmats-example-hpt-map"},
            "materials": {"inlet": "reacting_gas", "outlet": "reacting_gas"},
        },
        {
            "id": "duct",
            "kind": "transport.material.frozen_pressure_ratio",
            "version": "1.0.0",
            "parameters": {"pressure_ratio": 0.99},
            "materials": {"inlet": "reacting_gas", "outlet": "reacting_gas"},
        },
        {
            "id": "nozzle_area",
            "kind": "source.signal.boundary",
            "version": "1.0.0",
        },
        {
            "id": "ambient_pressure_ratio",
            "kind": "source.signal.boundary",
            "version": "1.0.0",
        },
        {
            "id": "nozzle",
            "kind": "terminal.material.convergent_nozzle",
            "version": "1.0.0",
            "parameters": {
                "reference_throat_area": _value(110.868590, "in2"),
                "reference_back_pressure": _value(14.696, "psia"),
                "discharge_coefficient": 1.0,
                "velocity_coefficient": 0.99,
            },
            "materials": {"inlet": "reacting_gas"},
        },
        {
            "id": "thrust_sink",
            "kind": "sink.force.boundary",
            "version": "1.0.0",
        },
        {
            "id": "shaft",
            "kind": "shaft.inertia.two_port",
            "version": "1.0.0",
            "parameters": {
                "moment_of_inertia": _value(30.0 * 1.3558179483314004, "kg*m2"),
                "mechanical_efficiency": 1.0,
                "fixed_loss": _value(0.0, "W"),
            },
        },
    ]
    connections = [
        {"id": "s0", "kind": "material_link", "from": "ambient.outlet", "to": "inlet.inlet"},
        {"id": "s2", "kind": "material_link", "from": "inlet.outlet", "to": "compressor.inlet"},
        {"id": "s3", "kind": "material_link", "from": "compressor.outlet", "to": "combustor.air_inlet"},
        {"id": "fuel_link", "kind": "material_link", "from": "fuel.outlet", "to": "combustor.fuel_inlet"},
        {"id": "s4", "kind": "material_link", "from": "combustor.outlet", "to": "turbine.inlet"},
        {"id": "s5", "kind": "material_link", "from": "turbine.outlet", "to": "duct.inlet"},
        {"id": "s7", "kind": "material_link", "from": "duct.outlet", "to": "nozzle.inlet"},
        {"id": "area", "kind": "signal_link", "from": "nozzle_area.outlet", "to": "nozzle.area_ratio"},
        {"id": "back_pressure", "kind": "signal_link", "from": "ambient_pressure_ratio.outlet", "to": "nozzle.back_pressure_ratio"},
        {"id": "thrust", "kind": "force_link", "from": "nozzle.thrust", "to": "thrust_sink.inlet"},
        {"id": "turbine_power", "kind": "shaft_link", "from": "turbine.shaft", "to": "shaft.driver"},
        {"id": "compressor_power", "kind": "shaft_link", "from": "shaft.load", "to": "compressor.shaft"},
    ]

    fixed = {
        "ambient.outlet.p": _value(14.696, "psia"),
        "ambient.outlet.T": _value(518.67, "degR"),
        "fuel.outlet.T": _value(300.0, "K"),
        "nozzle_area.outlet.value": 1.0,
        "ambient_pressure_ratio.outlet.value": 1.0,
    }
    guesses = {
        **_flow_guesses("ambient.outlet", air_flows),
        **_flow_guesses("inlet.inlet", air_flows),
        **_flow_guesses("inlet.outlet", air_flows),
        **_flow_guesses("compressor.inlet", air_flows),
        **_flow_guesses("compressor.outlet", air_flows),
        **_flow_guesses("combustor.air_inlet", air_flows),
        **_flow_guesses("fuel.outlet", {"CH4": 3.0}),
        **_flow_guesses("combustor.fuel_inlet", {"CH4": 3.0}),
        **_flow_guesses("combustor.outlet", PRODUCT_FLOWS_LBM_S),
        **_flow_guesses("turbine.inlet", PRODUCT_FLOWS_LBM_S),
        **_flow_guesses("turbine.outlet", PRODUCT_FLOWS_LBM_S),
        **_flow_guesses("duct.inlet", PRODUCT_FLOWS_LBM_S),
        **_flow_guesses("duct.outlet", PRODUCT_FLOWS_LBM_S),
        **_flow_guesses("nozzle.inlet", PRODUCT_FLOWS_LBM_S),
        "ambient.outlet.h": _value(-10.0, "kJ/kg"),
        "inlet.inlet.p": _value(14.696, "psia"),
        "inlet.inlet.h": _value(-10.0, "kJ/kg"),
        "inlet.outlet.p": _value(14.40208, "psia"),
        "inlet.outlet.h": _value(-10.0, "kJ/kg"),
        "compressor.inlet.p": _value(14.40208, "psia"),
        "compressor.inlet.h": _value(-10.0, "kJ/kg"),
        "compressor.outlet.p": _value(288.0418126, "psia"),
        "compressor.outlet.h": _value(460.0, "kJ/kg"),
        "compressor.map_coordinate.value": 2.0,
        "compressor.shaft.W_dot": _value(22.0, "MW"),
        "compressor.shaft.omega": _value(10000.0, "rpm"),
        "fuel.outlet.p": _value(288.0418126, "psia"),
        "fuel.outlet.h": _value(-4.65, "MJ/kg"),
        "combustor.air_inlet.p": _value(288.0418126, "psia"),
        "combustor.air_inlet.h": _value(460.0, "kJ/kg"),
        "combustor.fuel_inlet.p": _value(288.0418126, "psia"),
        "combustor.fuel_inlet.h": _value(-4.65, "MJ/kg"),
        "combustor.outlet.p": _value(273.6397220, "psia"),
        "combustor.outlet.h": _value(1500.0, "kJ/kg"),
        "turbine.inlet.p": _value(273.6397220, "psia"),
        "turbine.inlet.h": _value(1500.0, "kJ/kg"),
        "turbine.outlet.p": _value(89.8203867, "psia"),
        "turbine.outlet.h": _value(900.0, "kJ/kg"),
        "turbine.map_coordinate.value": 5.0,
        "turbine.shaft.W_dot": _value(22.0, "MW"),
        "turbine.shaft.omega": _value(10000.0, "rpm"),
        "duct.inlet.p": _value(89.8203867, "psia"),
        "duct.inlet.h": _value(900.0, "kJ/kg"),
        "duct.outlet.p": _value(88.92218283, "psia"),
        "duct.outlet.h": _value(900.0, "kJ/kg"),
        "nozzle.inlet.p": _value(88.92218283, "psia"),
        "nozzle.inlet.h": _value(900.0, "kJ/kg"),
        "nozzle_area.outlet.value": 1.0,
        "nozzle.area_ratio.value": 1.0,
        "ambient_pressure_ratio.outlet.value": 1.0,
        "nozzle.back_pressure_ratio.value": 1.0,
        "nozzle.mach": 1.0,
        "nozzle.thrust.F": _value(10665.72178, "lbf"),
        "thrust_sink.inlet.F": _value(10665.72178, "lbf"),
        "shaft.driver.W_dot": _value(22.0, "MW"),
        "shaft.driver.omega": _value(10000.0, "rpm"),
        "shaft.load.W_dot": _value(22.0, "MW"),
        "shaft.load.omega": _value(10000.0, "rpm"),
        "shaft.omega": _value(10000.0, "rpm"),
        "shaft.rotational_energy": _value(
            0.5
            * 30.0
            * 1.3558179483314004
            * (10000.0 * 2.0 * math.pi / 60.0) ** 2,
            "J",
        ),
    }
    schedule = {
        "fuel.outlet.m_dot[CH4]": {
            "interpolation": "linear",
            "points": [
                {
                    "time": _value(sample["time"], "s"),
                    "value": _value(sample["value"], "lbm/s"),
                }
                for sample in fuel["samples"]
            ],
        }
    }
    return {
        "schema_version": "thermox.model/v2",
        "model": {
            "id": "nasa_tmats_simple_gas_turbine_open_loop",
            "name": "Generic Thermox replay of NASA T-MATS simple gas-turbine plant",
            "revision": "1",
            "media": [],
            "materials": [
                {
                    "id": "reacting_gas",
                    "backend": "cantera",
                    "mechanism": "benchmarks/nasa_tmats/agtf30_major_products.yaml",
                    "phase": "gas",
                    "package_version": "3.2.0",
                    "species": list(SPECIES),
                }
            ],
            "components": components,
            "connections": connections,
        },
        "cases": [
            {
                "id": "nasa_fuel_history_replay",
                "mode": "dynamic_transient",
                "fixed_values": fixed,
                "initial_guesses": guesses,
                "input_schedules": schedule,
                "solver_options": {"boundary_continuation": 1.0},
            }
        ],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    reference = json.loads(args.reference.read_text(encoding="utf-8"))
    model = build(reference)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(model, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
