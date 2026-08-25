#!/usr/bin/env python3
"""Build the declaration-only AGTF30 coupled high-spool benchmark."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


FUEL_GUESSES_LBM_S = {
    "sea_level_static_1000": 0.30826616,
    "sea_level_mach_03_1500": 0.802132742,
    "alt_10000_mach_05_1750": 0.824493078,
    "alt_25000_mach_06_2000": 0.640935205,
    "alt_30000_mach_072_2000": 0.56337662,
}

HPC_TORQUE_LBF_FT = {
    "sea_level_static_1000": 1413.118438,
    "sea_level_mach_03_1500": 3031.727355,
    "alt_10000_mach_05_1750": 3050.602968,
    "alt_25000_mach_06_2000": 2314.083100,
    "alt_30000_mach_072_2000": 2017.266793,
}

HPT_OUTLET_PRESSURE_PSIA = {
    "sea_level_static_1000": 38.98232159,
    "sea_level_mach_03_1500": 81.37677935,
    "alt_10000_mach_05_1750": 81.66776981,
    "alt_25000_mach_06_2000": 61.99335406,
    "alt_30000_mach_072_2000": 54.41163179,
}


def quantity(value: float, unit: str) -> dict[str, object]:
    return {"value": value, "unit": unit}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("benchmarks/nasa_tmats/agtf30_high_spool.json"),
    )
    args = parser.parse_args()
    repository = args.repository.resolve()

    hpc = json.loads(
        (repository / "benchmarks/nasa_tmats/agtf30_hpc_off_design.json")
        .read_text(encoding="utf-8")
    )
    species = ["CH4", "O2", "N2", "CO2", "H2O", "CO", "H2"]

    model = {
        "id": "nasa_agtf30_coupled_high_spool",
        "name": "NASA AGTF30 coupled HPC-combustor-cooled-HPT high spool",
        "revision": "1",
        "media": [],
        "materials": [
            {
                "id": "reacting_gas",
                "backend": "cantera",
                "mechanism": "benchmarks/nasa_tmats/agtf30_major_products.yaml",
                "phase": "gas",
                "package_version": "3.2.0",
                "species": species,
            }
        ],
        "components": [
            {
                "id": "station_25",
                "kind": "source.material.fixed_composition",
                "version": "1.0.0",
                "parameters": {
                    "mass_fraction[N2]": 0.768,
                    "mass_fraction[O2]": 0.232,
                },
                "materials": {"outlet": "reacting_gas"},
            },
            {
                "id": "hpc",
                "kind": "compressor.material.coordinate_map.fractional_bleeds",
                "version": "1.0.0",
                "port_counts": {"bleed": 3},
                "parameters": {
                    "reference_pressure": quantity(14.696, "psia"),
                    "reference_temperature": quantity(518.67, "degR"),
                    "flow_capacity_scale": 0.1328,
                    "pressure_ratio_scale": 0.595594,
                    "efficiency_scale": 0.994014,
                    "bleed_fraction[1]": 0.02,
                    "bleed_pressure_fraction[1]": 0.146498,
                    "bleed_enthalpy_fraction[1]": 0.4997,
                    "bleed_fraction[2]": 0.0693,
                    "bleed_pressure_fraction[2]": 1.0,
                    "bleed_enthalpy_fraction[2]": 1.0,
                    "bleed_fraction[3]": 0.0625,
                    "bleed_pressure_fraction[3]": 1.0,
                    "bleed_enthalpy_fraction[3]": 1.0,
                },
                "artifacts": {"performance_map": "nasa-agtf30-hpc-map"},
                "materials": {
                    "inlet": "reacting_gas",
                    "outlet": "reacting_gas",
                    "bleed_1": "reacting_gas",
                    "bleed_2": "reacting_gas",
                    "bleed_3": "reacting_gas",
                },
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
                "kind": "combustor.material.equilibrium_heat_release_efficiency",
                "version": "1.0.0",
                "parameters": {
                    "pressure_ratio": 0.96,
                    "combustion_efficiency": 0.999,
                    # 18,400 Btu/lbm converted explicitly because the model
                    # schema intentionally accepts only its canonical
                    # specific-enthalpy engineering units.
                    "fuel_lower_heating_value": quantity(42.79846824, "MJ/kg"),
                },
                "materials": {
                    "air_inlet": "reacting_gas",
                    "fuel_inlet": "reacting_gas",
                    "outlet": "reacting_gas",
                },
            },
            {
                "id": "hpt",
                "kind": "turbine.material.coordinate_map.cooling_injections",
                "version": "1.0.0",
                "port_counts": {"cooling": 2},
                "parameters": {
                    "reference_pressure": quantity(14.696, "psia"),
                    "reference_temperature": quantity(518.67, "degR"),
                    "flow_capacity_scale": 1.0,
                    "pressure_ratio_scale": 1.0,
                    "efficiency_scale": 1.0,
                    "cooling_position[1]": 1.0,
                    "cooling_position[2]": 0.0,
                },
                "artifacts": {"performance_map": "nasa-agtf30-hpt-map"},
                "materials": {
                    "inlet": "reacting_gas",
                    "outlet": "reacting_gas",
                    "cooling_1": "reacting_gas",
                    "cooling_2": "reacting_gas",
                },
            },
            {
                "id": "station_45",
                "kind": "sink.material.boundary",
                "version": "1.0.0",
                "materials": {"inlet": "reacting_gas"},
            },
            {
                "id": "hp_shaft",
                "kind": "shaft.train.multi_load",
                "version": "1.0.0",
                "port_counts": {"load": 2},
                "parameters": {
                    "mechanical_efficiency": 1.0,
                    "fixed_loss": quantity(0.0, "W"),
                },
            },
            {
                "id": "hp_extraction",
                "kind": "sink.shaft.boundary",
                "version": "1.0.0",
            },
        ],
        "connections": [
            {"id": "station25_hpc", "from": "station_25.outlet", "to": "hpc.inlet", "kind": "material_link"},
            {"id": "hpc_combustor", "from": "hpc.outlet", "to": "combustor.air_inlet", "kind": "material_link"},
            {"id": "fuel_combustor", "from": "fuel.outlet", "to": "combustor.fuel_inlet", "kind": "material_link"},
            {"id": "combustor_hpt", "from": "combustor.outlet", "to": "hpt.inlet", "kind": "material_link"},
            {"id": "hpc_bleed2_hpt_exit", "from": "hpc.bleed_2", "to": "hpt.cooling_1", "kind": "material_link"},
            {"id": "hpc_bleed3_hpt_front", "from": "hpc.bleed_3", "to": "hpt.cooling_2", "kind": "material_link"},
            {"id": "hpt_station45", "from": "hpt.outlet", "to": "station_45.inlet", "kind": "material_link"},
            {"id": "hpt_hp_shaft", "from": "hpt.shaft", "to": "hp_shaft.driver", "kind": "shaft_link"},
            {"id": "hp_shaft_hpc", "from": "hp_shaft.load_1", "to": "hpc.shaft", "kind": "shaft_link"},
            {"id": "hp_shaft_extraction", "from": "hp_shaft.load_2", "to": "hp_extraction.inlet", "kind": "shaft_link"},
        ],
    }

    cases = []
    for source_case in hpc["cases"]:
        case_id = source_case["id"]
        source_fixed = source_case["fixed_values"]
        source_guess = source_case["initial_guesses"]
        inlet_n2 = source_fixed["compressor.inlet.m_dot[N2]"]["value"]
        inlet_o2 = source_fixed["compressor.inlet.m_dot[O2]"]["value"]
        # The NASA fuel command is retained separately as the validation
        # reference. Start the inverse solve slightly leaner because the
        # reduced equilibrium chemistry releases more sensible turbine-inlet
        # enthalpy than T-MATS' station-level burner correlation.
        fuel_guess = FUEL_GUESSES_LBM_S[case_id] * 0.84
        omega_rpm = source_fixed["compressor.shaft.omega"]["value"]
        hpc_power_w = (
            HPC_TORQUE_LBF_FT[case_id]
            * 1.3558179483314004
            * omega_rpm
            * 3.141592653589793
            / 30.0
        ) * 1.002
        hpt_power_w = hpc_power_w + 260997.475
        retained = 1.0 - 0.02 - 0.0693 - 0.0625
        burned_n2 = inlet_n2 * retained
        burned_o2 = max(inlet_o2 * retained - 4.0 * fuel_guess, 0.01)
        burned_co2 = 2.744 * fuel_guess
        burned_h2o = 2.246 * fuel_guess
        exit_n2 = inlet_n2 * (1.0 - 0.02)
        exit_o2 = max(
            inlet_o2 * (1.0 - 0.02) - 4.0 * fuel_guess,
            0.01,
        )
        air_flow_lbm_s = (inlet_n2 + inlet_o2) * retained
        inlet_flow_kg_s = (inlet_n2 + inlet_o2) * 0.45359237
        bleed_1_flow_kg_s = inlet_flow_kg_s * 0.02
        compressor_outlet_h_kj_kg = (
            source_guess["compressor.inlet.h"]["value"]
            + hpc_power_w /
            (inlet_flow_kg_s - bleed_1_flow_kg_s * (1.0 - 0.4997)) /
            1000.0
        )
        combustor_h_kj_kg = (
            air_flow_lbm_s * compressor_outlet_h_kj_kg
            + fuel_guess * (-4645.8569 - 42.7985)
        ) / (air_flow_lbm_s + fuel_guess)
        main_flow_kg_s = (air_flow_lbm_s + fuel_guess) * 0.45359237
        front_flow_kg_s = (
            (inlet_n2 + inlet_o2) * 0.0625 * 0.45359237
        )
        rear_flow_kg_s = (
            (inlet_n2 + inlet_o2) * 0.0693 * 0.45359237
        )
        stage_flow_kg_s = main_flow_kg_s + front_flow_kg_s
        stage_h_j_kg = (
            main_flow_kg_s * combustor_h_kj_kg * 1000.0
            + front_flow_kg_s * compressor_outlet_h_kj_kg * 1000.0
            - hpt_power_w
        ) / stage_flow_kg_s
        outlet_h_j_kg = (
            stage_flow_kg_s * stage_h_j_kg
            + rear_flow_kg_s * compressor_outlet_h_kj_kg * 1000.0
        ) / (stage_flow_kg_s + rear_flow_kg_s)
        fixed = {
            "station_25.outlet.p": source_fixed["compressor.inlet.p"],
            "station_25.outlet.T": source_fixed["compressor.inlet.T"],
            "station_25.outlet.m_dot[N2]": source_fixed["compressor.inlet.m_dot[N2]"],
            "hpc.shaft.omega": source_fixed["compressor.shaft.omega"],
            "fuel.outlet.T": quantity(300.0, "K"),
            "hp_extraction.inlet.W_dot": quantity(260.997475, "kW"),
        }
        guesses = {
            "station_25.outlet.h": source_guess["compressor.inlet.h"],
            "station_25.outlet.m_dot[O2]": source_fixed["compressor.inlet.m_dot[O2]"],
            "hpc.inlet.p": source_fixed["compressor.inlet.p"],
            "hpc.inlet.h": source_guess["compressor.inlet.h"],
            "hpc.outlet.p": source_guess["compressor.outlet.p"],
            "hpc.outlet.h": quantity(
                compressor_outlet_h_kj_kg, "kJ/kg"
            ),
            "hpc.shaft.W_dot": quantity(hpc_power_w, "W"),
            "hpc.map_coordinate.value": source_guess["compressor.map_coordinate.value"],
            "fuel.outlet.p": source_guess["compressor.outlet.p"],
            "fuel.outlet.h": quantity(-4.65, "MJ/kg"),
            "fuel.outlet.m_dot[CH4]": quantity(fuel_guess, "lbm/s"),
            "combustor.air_inlet.p": source_guess["compressor.outlet.p"],
            "combustor.air_inlet.h": quantity(
                compressor_outlet_h_kj_kg, "kJ/kg"
            ),
            "combustor.fuel_inlet.p": source_guess["compressor.outlet.p"],
            "combustor.fuel_inlet.h": quantity(-4.65, "MJ/kg"),
            "combustor.fuel_inlet.m_dot[CH4]": quantity(fuel_guess, "lbm/s"),
            "combustor.outlet.p": quantity(
                source_guess["compressor.outlet.p"]["value"] * 0.96,
                "psia",
            ),
            "combustor.outlet.h": quantity(
                combustor_h_kj_kg, "kJ/kg"
            ),
            "hpt.inlet.p": quantity(
                source_guess["compressor.outlet.p"]["value"] * 0.96,
                "psia",
            ),
            "hpt.inlet.h": quantity(combustor_h_kj_kg, "kJ/kg"),
            "hpt.outlet.p": quantity(
                HPT_OUTLET_PRESSURE_PSIA[case_id], "psia"
            ),
            "hpt.outlet.h": quantity(outlet_h_j_kg, "J/kg"),
            "hpt.stage_outlet_enthalpy": quantity(
                stage_h_j_kg, "J/kg"
            ),
            "station_45.inlet.p": quantity(
                HPT_OUTLET_PRESSURE_PSIA[case_id], "psia"
            ),
            "station_45.inlet.h": quantity(outlet_h_j_kg, "J/kg"),
            "hpt.shaft.W_dot": quantity(hpt_power_w, "W"),
            "hpt.shaft.omega": source_fixed["compressor.shaft.omega"],
            "hp_shaft.driver.omega": source_fixed["compressor.shaft.omega"],
            "hp_shaft.load_1.omega": source_fixed["compressor.shaft.omega"],
            "hp_shaft.load_2.omega": source_fixed["compressor.shaft.omega"],
            "hp_extraction.inlet.omega": source_fixed["compressor.shaft.omega"],
            "hp_shaft.driver.W_dot": quantity(hpt_power_w, "W"),
            "hp_shaft.load_1.W_dot": quantity(hpc_power_w, "W"),
            "hp_shaft.load_2.W_dot": quantity(260.997475, "kW"),
            "hpt.map_coordinate.value": 4.2,
        }
        for number, fraction in ((1, 0.02), (2, 0.0693), (3, 0.0625)):
            if number == 1:
                inlet_p = source_fixed["compressor.inlet.p"]["value"]
                outlet_p = source_guess["compressor.outlet.p"]["value"]
                inlet_h = source_guess["compressor.inlet.h"]["value"]
                outlet_h = compressor_outlet_h_kj_kg
                guesses["hpc.bleed_1.p"] = quantity(
                    inlet_p * (1.0 - 0.146498) +
                    outlet_p * 0.146498,
                    "psia",
                )
                guesses["hpc.bleed_1.h"] = quantity(
                    inlet_h * (1.0 - 0.4997) +
                    outlet_h * 0.4997,
                    "kJ/kg",
                )
            else:
                guesses[f"hpc.bleed_{number}.p"] = source_guess[
                    "compressor.outlet.p"
                ]
                guesses[f"hpc.bleed_{number}.h"] = quantity(
                    compressor_outlet_h_kj_kg, "kJ/kg"
                )
            guesses[f"hpc.bleed_{number}.m_dot[N2]"] = quantity(
                inlet_n2 * fraction, "lbm/s"
            )
            guesses[f"hpc.bleed_{number}.m_dot[O2]"] = quantity(
                inlet_o2 * fraction, "lbm/s"
            )
        for number, fraction in ((1, 0.0693), (2, 0.0625)):
            guesses[f"hpt.cooling_{number}.p"] = source_guess[
                "compressor.outlet.p"
            ]
            guesses[f"hpt.cooling_{number}.h"] = quantity(
                compressor_outlet_h_kj_kg, "kJ/kg"
            )
            guesses[f"hpt.cooling_{number}.m_dot[N2]"] = quantity(
                inlet_n2 * fraction, "lbm/s"
            )
            guesses[f"hpt.cooling_{number}.m_dot[O2]"] = quantity(
                inlet_o2 * fraction, "lbm/s"
            )
        for component in ("hpc.outlet", "combustor.air_inlet"):
            guesses[f"{component}.m_dot[N2]"] = quantity(
                burned_n2, "lbm/s"
            )
            guesses[f"{component}.m_dot[O2]"] = quantity(
                inlet_o2 * retained, "lbm/s"
            )
        for component in ("combustor.outlet", "hpt.inlet"):
            guesses[f"{component}.m_dot[N2]"] = quantity(
                burned_n2, "lbm/s"
            )
            guesses[f"{component}.m_dot[O2]"] = quantity(
                burned_o2, "lbm/s"
            )
            guesses[f"{component}.m_dot[CO2]"] = quantity(
                burned_co2, "lbm/s"
            )
            guesses[f"{component}.m_dot[H2O]"] = quantity(
                burned_h2o, "lbm/s"
            )
        for component in ("hpt.outlet", "station_45.inlet"):
            guesses[f"{component}.m_dot[N2]"] = quantity(
                exit_n2, "lbm/s"
            )
            guesses[f"{component}.m_dot[O2]"] = quantity(
                exit_o2, "lbm/s"
            )
            guesses[f"{component}.m_dot[CO2]"] = quantity(
                burned_co2, "lbm/s"
            )
            guesses[f"{component}.m_dot[H2O]"] = quantity(
                burned_h2o, "lbm/s"
            )
        cases.append(
            {
                "id": case_id,
                "mode": "steady_state_off_design",
                "fixed_values": fixed,
                "initial_guesses": guesses,
            }
        )

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
