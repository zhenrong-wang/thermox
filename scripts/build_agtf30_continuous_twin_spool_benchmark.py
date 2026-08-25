#!/usr/bin/env python3
"""Build the declaration-only AGTF30 continuous twin-spool gas path."""

from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path


BYPASS_RATIOS = {
    "sea_level_mach_03_1500": 32.5312545,
    "alt_10000_mach_05_1750": 29.9390383,
    "alt_25000_mach_06_2000": 27.0193832,
    "alt_30000_mach_072_2000": 27.2640694,
}

SPECIES = ["CH4", "O2", "N2", "CO2", "H2O", "CO", "H2"]


def _component(document: dict, component_id: str) -> dict:
    for component in document["model"]["components"]:
        if component["id"] == component_id:
            return copy.deepcopy(component)
    raise KeyError(component_id)


def _case(document: dict, case_id: str) -> dict:
    for item in document["cases"]:
        if item["id"] == case_id:
            return item
    raise KeyError(case_id)


def _bind_material(component: dict) -> dict:
    for port in component.get("materials", {}):
        component["materials"][port] = "reacting_gas"
    return component


def _copy_guesses(source: dict, target: dict, prefixes: tuple[str, ...]) -> None:
    for name, value in source["initial_guesses"].items():
        if name.startswith(prefixes):
            target[name] = copy.deepcopy(value)


def _complete_species_guesses(guesses: dict) -> None:
    prefixes = {
        name.rsplit(".m_dot[", 1)[0]
        for name in guesses
        if ".m_dot[" in name
    }
    for prefix in sorted(prefixes):
        for species in SPECIES:
            guesses.setdefault(
                f"{prefix}.m_dot[{species}]",
                {"value": 0.0, "unit": "kg/s"},
            )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(
            "benchmarks/nasa_tmats/agtf30_continuous_twin_spool.json"
        ),
    )
    args = parser.parse_args()
    repository = args.repository.resolve()
    low = json.loads(
        (repository / "benchmarks/nasa_tmats/agtf30_low_spool.json")
        .read_text(encoding="utf-8")
    )
    high = json.loads(
        (repository / "benchmarks/nasa_tmats/agtf30_high_spool.json")
        .read_text(encoding="utf-8")
    )

    station_2 = _bind_material(_component(low, "station_2"))
    fan = _bind_material(_component(low, "fan"))
    lpc = _bind_material(_component(low, "lpc"))
    lpt = _bind_material(_component(low, "lpt"))
    hpc = _component(high, "hpc")
    fuel = _component(high, "fuel")
    combustor = _component(high, "combustor")
    hpt = _component(high, "hpt")
    lp_shaft = _component(low, "lp_shaft")
    fan_gearbox = _component(low, "fan_gearbox")
    hp_shaft = _component(high, "hp_shaft")
    hp_extraction = _component(high, "hp_extraction")

    splitter = {
        "id": "fan_splitter",
        "kind": "junction.material.splitter.controlled_fraction",
        "version": "1.0.0",
        "materials": {
            "inlet": "reacting_gas",
            "outlet_a": "reacting_gas",
            "outlet_b": "reacting_gas",
        },
    }
    bypass_sink = {
        "id": "bypass_exit",
        "kind": "sink.material.boundary",
        "version": "1.0.0",
        "materials": {"inlet": "reacting_gas"},
    }
    core_sink = {
        "id": "core_exit",
        "kind": "sink.material.boundary",
        "version": "1.0.0",
        "materials": {"inlet": "reacting_gas"},
    }
    model = {
        "id": "nasa_agtf30_continuous_twin_spool",
        "name": "NASA AGTF30 continuous fan-to-LPT twin-spool gas path",
        "revision": "1",
        "media": [],
        "materials": copy.deepcopy(high["model"]["materials"]),
        "components": [
            station_2,
            fan,
            splitter,
            bypass_sink,
            lpc,
            hpc,
            fuel,
            combustor,
            hpt,
            lpt,
            core_sink,
            lp_shaft,
            fan_gearbox,
            hp_shaft,
            hp_extraction,
        ],
        "connections": [
            {"id": "station2_fan", "from": "station_2.outlet", "to": "fan.inlet", "kind": "material_link"},
            {"id": "fan_split", "from": "fan.outlet", "to": "fan_splitter.inlet", "kind": "material_link"},
            {"id": "split_bypass", "from": "fan_splitter.outlet_a", "to": "bypass_exit.inlet", "kind": "material_link"},
            {"id": "split_core", "from": "fan_splitter.outlet_b", "to": "lpc.inlet", "kind": "material_link"},
            {"id": "lpc_hpc", "from": "lpc.outlet", "to": "hpc.inlet", "kind": "material_link"},
            {"id": "hpc_combustor", "from": "hpc.outlet", "to": "combustor.air_inlet", "kind": "material_link"},
            {"id": "fuel_combustor", "from": "fuel.outlet", "to": "combustor.fuel_inlet", "kind": "material_link"},
            {"id": "combustor_hpt", "from": "combustor.outlet", "to": "hpt.inlet", "kind": "material_link"},
            {"id": "hpc_bleed2_hpt_exit", "from": "hpc.bleed_2", "to": "hpt.cooling_1", "kind": "material_link"},
            {"id": "hpc_bleed3_hpt_front", "from": "hpc.bleed_3", "to": "hpt.cooling_2", "kind": "material_link"},
            {"id": "hpt_lpt", "from": "hpt.outlet", "to": "lpt.inlet", "kind": "material_link"},
            {"id": "hpc_bleed1_lpt_exit", "from": "hpc.bleed_1", "to": "lpt.cooling_1", "kind": "material_link"},
            {"id": "lpt_core_exit", "from": "lpt.outlet", "to": "core_exit.inlet", "kind": "material_link"},
            {"id": "lpt_lp_shaft", "from": "lpt.shaft", "to": "lp_shaft.driver", "kind": "shaft_link"},
            {"id": "lp_shaft_lpc", "from": "lp_shaft.load_1", "to": "lpc.shaft", "kind": "shaft_link"},
            {"id": "lp_shaft_fan_gearbox", "from": "lp_shaft.load_2", "to": "fan_gearbox.driver", "kind": "shaft_link"},
            {"id": "fan_gearbox_fan", "from": "fan_gearbox.load", "to": "fan.shaft", "kind": "shaft_link"},
            {"id": "hpt_hp_shaft", "from": "hpt.shaft", "to": "hp_shaft.driver", "kind": "shaft_link"},
            {"id": "hp_shaft_hpc", "from": "hp_shaft.load_1", "to": "hpc.shaft", "kind": "shaft_link"},
            {"id": "hp_shaft_extraction", "from": "hp_shaft.load_2", "to": "hp_extraction.inlet", "kind": "shaft_link"},
        ],
    }

    cases = []
    for case_id, bypass_ratio in BYPASS_RATIOS.items():
        low_case = _case(low, case_id)
        high_case = _case(high, case_id)
        fixed = {
            "station_2.outlet.p": copy.deepcopy(
                low_case["fixed_values"]["station_2.outlet.p"]
            ),
            "station_2.outlet.T": copy.deepcopy(
                low_case["fixed_values"]["station_2.outlet.T"]
            ),
            "fan_splitter.fraction.value":
                bypass_ratio / (1.0 + bypass_ratio),
            "lpc.shaft.omega": copy.deepcopy(
                low_case["fixed_values"]["lpc.shaft.omega"]
            ),
            "hpc.shaft.omega": copy.deepcopy(
                high_case["fixed_values"]["hpc.shaft.omega"]
            ),
            "fuel.outlet.T": copy.deepcopy(
                high_case["fixed_values"]["fuel.outlet.T"]
            ),
            "hp_extraction.inlet.W_dot": copy.deepcopy(
                high_case["fixed_values"]["hp_extraction.inlet.W_dot"]
            ),
        }
        guesses: dict[str, object] = {}
        _copy_guesses(
            low_case,
            guesses,
            ("station_2.", "fan.", "lpc.", "lp_shaft.",
             "fan_gearbox.", "lpt."),
        )
        _copy_guesses(
            high_case,
            guesses,
            (
                "hpc.", "fuel.", "combustor.", "hpt.",
                "hp_shaft.", "hp_extraction.",
            ),
        )
        guesses["fan.map_coordinate.value"] = copy.deepcopy(
            low_case["fixed_values"]["fan.map_coordinate.value"]
        )
        fan_pressure = copy.deepcopy(guesses["fan.outlet.p"])
        fan_enthalpy = copy.deepcopy(guesses["fan.outlet.h"])
        guesses["fan_splitter.inlet.p"] = fan_pressure
        guesses["fan_splitter.inlet.h"] = fan_enthalpy
        for port in ("outlet_a", "outlet_b"):
            guesses[f"fan_splitter.{port}.p"] = copy.deepcopy(fan_pressure)
            guesses[f"fan_splitter.{port}.h"] = copy.deepcopy(fan_enthalpy)
        guesses["bypass_exit.inlet.p"] = copy.deepcopy(fan_pressure)
        guesses["bypass_exit.inlet.h"] = copy.deepcopy(fan_enthalpy)
        guesses["core_exit.inlet.p"] = copy.deepcopy(
            guesses["lpt.outlet.p"]
        )
        guesses["core_exit.inlet.h"] = copy.deepcopy(
            guesses["lpt.outlet.h"]
        )

        for species in SPECIES:
            fan_flow = guesses.get(
                f"fan.outlet.m_dot[{species}]",
                {"value": 0.0, "unit": "kg/s"},
            )
            value = float(fan_flow["value"])
            unit = str(fan_flow["unit"])
            guesses[f"fan_splitter.inlet.m_dot[{species}]"] = copy.deepcopy(
                fan_flow
            )
            guesses[f"fan_splitter.outlet_a.m_dot[{species}]"] = {
                "value": value * bypass_ratio / (1.0 + bypass_ratio),
                "unit": unit,
            }
            guesses[f"fan_splitter.outlet_b.m_dot[{species}]"] = {
                "value": value / (1.0 + bypass_ratio),
                "unit": unit,
            }
            guesses[f"bypass_exit.inlet.m_dot[{species}]"] = copy.deepcopy(
                guesses[f"fan_splitter.outlet_a.m_dot[{species}]"]
            )
            hpt_flow = guesses.get(
                f"hpt.outlet.m_dot[{species}]",
                {"value": 0.0, "unit": "kg/s"},
            )
            bleed_flow = guesses.get(
                f"hpc.bleed_1.m_dot[{species}]",
                {"value": 0.0, "unit": "kg/s"},
            )
            if hpt_flow["unit"] == bleed_flow["unit"]:
                lpt_outlet_flow = {
                    "value": float(hpt_flow["value"]) +
                        float(bleed_flow["value"]),
                    "unit": hpt_flow["unit"],
                }
            else:
                lpt_outlet_flow = copy.deepcopy(hpt_flow)
            guesses[f"lpt.inlet.m_dot[{species}]"] = copy.deepcopy(hpt_flow)
            guesses[f"lpt.outlet.m_dot[{species}]"] = lpt_outlet_flow
            guesses[f"core_exit.inlet.m_dot[{species}]"] = copy.deepcopy(
                lpt_outlet_flow
            )
        _complete_species_guesses(guesses)
        cases.append({
            "id": case_id,
            "mode": "steady_state_off_design",
            "fixed_values": fixed,
            "initial_guesses": guesses,
        })

    declaration = {
        "schema_version": "thermox.model/v2",
        "model": model,
        "cases": cases,
    }
    destination = (repository / args.output).resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(
        json.dumps(declaration, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
