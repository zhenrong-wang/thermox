#!/usr/bin/env python3
"""Build exact-boundary component slices for the NASA T-MATS dynamic example.

The slices diagnose model-form differences without coupling them to compressor,
turbine, shaft, or controller errors.  They remain ordinary Thermox declarative
models and exercise only public registry components.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


MECHANISM = "benchmarks/nasa_tmats/agtf30_major_products.yaml"
SPECIES = ("CH4", "O2", "N2", "CO2", "H2O", "CO", "H2")
AIR_FRACTIONS = {"O2": 0.232, "N2": 0.768}
PRODUCT_FLOWS = {
    "CH4": 0.0,
    "O2": 11.2,
    "N2": 76.8,
    "CO2": 8.25,
    "H2O": 6.75,
    "CO": 0.0,
    "H2": 0.0,
}


def value(number: float, unit: str) -> dict:
    return {"value": number, "unit": unit}


def material() -> dict:
    return {
        "id": "reacting_gas",
        "backend": "cantera",
        "mechanism": MECHANISM,
        "phase": "gas",
        "package_version": "3.2.0",
        "species": list(SPECIES),
    }


def flow_guesses(prefix: str, flows: dict[str, float]) -> dict:
    return {
        f"{prefix}.m_dot[{species}]": value(flows.get(species, 0.0), "lbm/s")
        for species in SPECIES
    }


def combustor_model() -> dict:
    air_flow = 99.99996409294292
    air_flows = {
        species: air_flow * fraction
        for species, fraction in AIR_FRACTIONS.items()
    }
    return {
        "schema_version": "thermox.model/v2",
        "model": {
            "id": "nasa_tmats_combustor_boundary_slice",
            "name": "NASA T-MATS combustor exact-boundary diagnostic",
            "revision": "1",
            "media": [],
            "materials": [material()],
            "components": [
                {
                    "id": "air",
                    "kind": "source.material.fixed_composition",
                    "version": "1.0.0",
                    "parameters": {
                        f"mass_fraction[{name}]": fraction
                        for name, fraction in AIR_FRACTIONS.items()
                    },
                    "materials": {"outlet": "reacting_gas"},
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
                        "fuel_lower_heating_value": value(42.79846824, "MJ/kg"),
                    },
                    "materials": {
                        "air_inlet": "reacting_gas",
                        "fuel_inlet": "reacting_gas",
                        "outlet": "reacting_gas",
                    },
                },
                {
                    "id": "exhaust",
                    "kind": "sink.material.boundary",
                    "version": "1.0.0",
                    "materials": {"inlet": "reacting_gas"},
                },
            ],
            "connections": [
                {"id": "s3", "kind": "material_link", "from": "air.outlet", "to": "combustor.air_inlet"},
                {"id": "fuel_link", "kind": "material_link", "from": "fuel.outlet", "to": "combustor.fuel_inlet"},
                {"id": "s4", "kind": "material_link", "from": "combustor.outlet", "to": "exhaust.inlet"},
            ],
        },
        "cases": [{
            "id": "exact_station_3_and_fuel",
            "mode": "steady_state_off_design",
            "fixed_values": {
                "air.outlet.p": value(288.041812601952, "psia"),
                "air.outlet.T": value(1316.1339585749665, "degR"),
                "air.outlet.m_dot[N2]": value(air_flows["N2"], "lbm/s"),
                "fuel.outlet.T": value(300.0, "K"),
                "fuel.outlet.m_dot[CH4]": value(3.0, "lbm/s"),
            },
            "initial_guesses": {
                **flow_guesses("air.outlet", air_flows),
                **flow_guesses("combustor.air_inlet", air_flows),
                **flow_guesses("fuel.outlet", {"CH4": 3.0}),
                **flow_guesses("combustor.fuel_inlet", {"CH4": 3.0}),
                **flow_guesses("combustor.outlet", PRODUCT_FLOWS),
                **flow_guesses("exhaust.inlet", PRODUCT_FLOWS),
                "air.outlet.h": value(460.0, "kJ/kg"),
                "combustor.air_inlet.p": value(288.041812601952, "psia"),
                "combustor.air_inlet.h": value(460.0, "kJ/kg"),
                "fuel.outlet.p": value(288.041812601952, "psia"),
                "fuel.outlet.h": value(-4.65, "MJ/kg"),
                "combustor.fuel_inlet.p": value(288.041812601952, "psia"),
                "combustor.fuel_inlet.h": value(-4.65, "MJ/kg"),
                "combustor.outlet.p": value(273.6397219718544, "psia"),
                "combustor.outlet.h": value(1.5, "MJ/kg"),
                "exhaust.inlet.p": value(273.6397219718544, "psia"),
                "exhaust.inlet.h": value(1.5, "MJ/kg"),
            },
        }],
    }


def nozzle_model() -> dict:
    total_product_flow = sum(PRODUCT_FLOWS.values())
    fractions = {
        species: flow / total_product_flow
        for species, flow in PRODUCT_FLOWS.items()
    }
    base_fixed = {
        "gas.outlet.p": value(88.9221828301095, "psia"),
        "gas.outlet.T": value(2453.1832204022294, "degR"),
        "back_pressure.outlet.value": 1.0,
    }
    guesses = {
        **flow_guesses("gas.outlet", PRODUCT_FLOWS),
        **flow_guesses("nozzle.inlet", PRODUCT_FLOWS),
        "gas.outlet.h": value(900.0, "kJ/kg"),
        "nozzle.inlet.p": value(88.9221828301095, "psia"),
        "nozzle.inlet.h": value(900.0, "kJ/kg"),
        "area.outlet.value": 1.0,
        "nozzle.area_ratio.value": 1.0,
        "back_pressure.outlet.value": 1.0,
        "nozzle.back_pressure_ratio.value": 1.0,
        "nozzle.mach": 1.0,
        "nozzle.thrust.F": value(10665.721779868387, "lbf"),
        "thrust_sink.inlet.F": value(10665.721779868387, "lbf"),
    }
    return {
        "schema_version": "thermox.model/v2",
        "model": {
            "id": "nasa_tmats_nozzle_boundary_slice",
            "name": "NASA T-MATS nozzle exact-boundary diagnostics",
            "revision": "1",
            "media": [],
            "materials": [material()],
            "components": [
                {
                    "id": "gas",
                    "kind": "source.material.fixed_composition",
                    "version": "1.0.0",
                    "parameters": {
                        f"mass_fraction[{name}]": fraction
                        for name, fraction in fractions.items()
                    },
                    "materials": {"outlet": "reacting_gas"},
                },
                {"id": "area", "kind": "source.signal.boundary", "version": "1.0.0"},
                {"id": "back_pressure", "kind": "source.signal.boundary", "version": "1.0.0"},
                {
                    "id": "nozzle",
                    "kind": "terminal.material.convergent_nozzle",
                    "version": "1.0.0",
                    "parameters": {
                        "reference_throat_area": value(110.868590, "in2"),
                        "reference_back_pressure": value(14.696, "psia"),
                        "discharge_coefficient": 1.0,
                        "velocity_coefficient": 0.99,
                    },
                    "materials": {"inlet": "reacting_gas"},
                },
                {"id": "thrust_sink", "kind": "sink.force.boundary", "version": "1.0.0"},
            ],
            "connections": [
                {"id": "s7", "kind": "material_link", "from": "gas.outlet", "to": "nozzle.inlet"},
                {"id": "area_link", "kind": "signal_link", "from": "area.outlet", "to": "nozzle.area_ratio"},
                {"id": "back_pressure_link", "kind": "signal_link", "from": "back_pressure.outlet", "to": "nozzle.back_pressure_ratio"},
                {"id": "thrust", "kind": "force_link", "from": "nozzle.thrust", "to": "thrust_sink.inlet"},
            ],
        },
        "cases": [
            {
                "id": "fixed_oem_area_predict_flow",
                "mode": "steady_state_off_design",
                "fixed_values": {**base_fixed, "area.outlet.value": 1.0},
                "initial_guesses": guesses,
            },
            {
                "id": "fixed_measured_flow_predict_effective_area",
                "mode": "steady_state_off_design",
                "fixed_values": {
                    **base_fixed,
                    "gas.outlet.m_dot[N2]": value(
                        102.99996409294292 * fractions["N2"], "lbm/s"
                    ),
                },
                "initial_guesses": guesses,
            },
        ],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output_directory", type=Path)
    args = parser.parse_args()
    args.output_directory.mkdir(parents=True, exist_ok=True)
    outputs = {
        "tmats_combustor_boundary_slice.json": combustor_model(),
        "tmats_nozzle_boundary_slice.json": nozzle_model(),
    }
    for name, model in outputs.items():
        (args.output_directory / name).write_text(
            json.dumps(model, indent=2) + "\n", encoding="utf-8"
        )


if __name__ == "__main__":
    main()
