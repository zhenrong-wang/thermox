#!/usr/bin/env python3
"""Build the declaration-only AGTF30 mechanically coupled low spool."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


CASES = [
    {
        "id": "sea_level_static_1000",
        "n2_rpm": 3100.0000432125,
        "map_coordinates": (1.64069469, 1.85675355),
        "enthalpies": (-10055.4366, -6702.9867, -6702.9867,
                       25892.5460, 523522.5880, 353092.2924,
                       185755.6587),
        "fan": (952.4527216472, 518.6700144600, 14.6225200000,
                524.6498883364, 15.1543226875, 10146.0098505703),
        "lpc": (29.4739326359, 524.6498883364, 15.1321639840,
                582.6838354522, 20.8871224494, 983.8223485839),
        "lpt": (24.6515224266, 1433.7670620815, 38.5591407289,
                25.1483235749, 1151.0231330851, 14.9677381289,
                4299.7260121151),
        "cooling": (0.4968011483, 480.0102044774, 43.0985941800),
    },
    {
        "id": "sea_level_mach_03_1500",
        "n2_rpm": 4812.1454878870,
        "map_coordinates": (1.74935262, 1.57212729),
        "enthalpies": (10595.1861, 19476.6238, 19476.6238,
                       99302.7223, 831801.6541, 457417.1538,
                       319971.7489),
        "fan": (1486.5592910209, 555.4727026221, 15.5959388828,
                571.2765314183, 17.1003657680, 26992.8957404502),
        "lpc": (44.3335423899, 571.2765314183, 17.0502467900,
                712.6083875065, 34.2933896482, 2330.4984504638),
        "lpt": (44.2490042836, 1922.2569753797, 80.5007777020,
                45.1356751314, 1325.2888976205, 15.7062233148,
                11149.3779403394),
        "cooling": (0.8866708478, 608.2898486714, 81.2923815900),
    },
    {
        "id": "alt_10000_mach_05_1750",
        "n2_rpm": 5364.6286788317,
        "map_coordinates": (1.74925103, 1.65749007),
        "enthalpies": (-16488.0412, -5115.8948, -5115.8948,
                       86195.7227, 844233.7204, 406299.8275,
                       308192.7846),
        "fan": (1362.1732328680, 507.1903453549, 11.9662568707,
                527.4801300339, 13.6409494280, 28447.8271699601),
        "lpc": (44.0276527003, 527.4801300339, 13.5817551190,
                689.4957044996, 32.1343832806, 2376.2453719256),
        "lpt": (43.9715927247, 1941.4925756653, 80.7994692145,
                44.8521457787, 1240.3710330419, 11.4749285219,
                11669.6604187030),
        "cooling": (0.8805530540, 597.1568202739, 79.3414883700),
    },
    {
        "id": "alt_25000_mach_06_2000",
        "n2_rpm": 5774.2733312021,
        "map_coordinates": (1.75076720, 1.82375606),
        "enthalpies": (-48538.2462, -34720.0266, -34720.0266,
                       60848.7713, 836574.4909, 359687.9707,
                       278372.0627),
        "fan": (933.1420345382, 449.8859451757, 6.9515950704,
                474.6139527632, 8.3275416857, 22044.1677525214),
        "lpc": (33.3034466511, 474.6139527632, 8.2752884722,
                644.6920761831, 22.4726344734, 1749.7967677598),
        "lpt": (33.2783129227, 1929.6451557496, 61.3445174228,
                33.9443818557, 1162.1512948641, 6.8332506957,
                8950.3218416294),
        "cooling": (0.6660689330, 568.8598572209, 58.3332508800),
    },
    {
        "id": "alt_30000_mach_072_2000",
        "n2_rpm": 5804.5640118102,
        "map_coordinates": (1.75636101, 1.77597500),
        "enthalpies": (-45895.1333, -31948.8979, -31948.8979,
                       66448.3051, 842797.6361, 356603.1711,
                       283751.6175),
        "fan": (823.7870551985, 454.6183483264, 6.1639786109,
                479.5690376878, 7.3819365350, 19534.9050804272),
        "lpc": (29.1460880549, 479.5690376878, 7.3364349934,
                654.6017474960, 20.2235148517, 1568.1691019346),
        "lpt": (29.1265429133, 1939.2720389069, 53.8425624667,
                29.7094646744, 1156.9484849263, 5.6850295910,
                7949.2438262781),
        "cooling": (0.5829217611, 573.9762053671, 51.6127416500),
    },
]


def quantity(value: float, unit: str) -> dict[str, object]:
    return {"value": value, "unit": unit}


def species_guesses(
    target: dict[str, object], prefix: str, flow_lbm_s: float
) -> None:
    target[f"{prefix}.m_dot[N2]"] = quantity(
        flow_lbm_s * 0.768, "lbm/s"
    )
    target[f"{prefix}.m_dot[O2]"] = quantity(
        flow_lbm_s * 0.232, "lbm/s"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("benchmarks/nasa_tmats/agtf30_low_spool.json"),
    )
    args = parser.parse_args()
    repository = args.repository.resolve()

    material = {
        "id": "dry_air",
        "backend": "cantera",
        "mechanism": "gri30.yaml",
        "phase": "gri30",
        "package_version": "3.2.0",
        "species": ["N2", "O2"],
    }
    fixed_source = lambda component_id: {
        "id": component_id,
        "kind": "source.material.fixed_composition",
        "version": "1.0.0",
        "parameters": {
            "mass_fraction[N2]": 0.768,
            "mass_fraction[O2]": 0.232,
        },
        "materials": {"outlet": "dry_air"},
    }
    sink = lambda component_id: {
        "id": component_id,
        "kind": "sink.material.boundary",
        "version": "1.0.0",
        "materials": {"inlet": "dry_air"},
    }
    model = {
        "id": "nasa_agtf30_coupled_low_spool",
        "name": "NASA AGTF30 mechanically coupled fan-LPC-LPT low spool",
        "revision": "1",
        "media": [],
        "materials": [material],
        "components": [
            fixed_source("station_2"),
            {
                "id": "fan",
                "kind": "compressor.material.coordinate_map",
                "version": "1.0.0",
                "parameters": {
                    "reference_pressure": quantity(14.696, "psia"),
                    "reference_temperature": quantity(518.67, "degR"),
                    "flow_capacity_scale": 0.768426,
                    "pressure_ratio_scale": 0.769231,
                    "efficiency_scale": 1.036257,
                },
                "artifacts": {"performance_map": "nasa-agtf30-fan-map"},
                "materials": {"inlet": "dry_air", "outlet": "dry_air"},
            },
            sink("station_21"),
            fixed_source("station_23"),
            {
                "id": "lpc",
                "kind": "compressor.material.coordinate_map",
                "version": "1.0.0",
                "parameters": {
                    "reference_pressure": quantity(14.696, "psia"),
                    "reference_temperature": quantity(518.67, "degR"),
                    "flow_capacity_scale": 0.300704,
                    "pressure_ratio_scale": 4.374453,
                    "efficiency_scale": 1.019486,
                },
                "artifacts": {"performance_map": "nasa-agtf30-lpc-map"},
                "materials": {"inlet": "dry_air", "outlet": "dry_air"},
            },
            sink("station_24a"),
            fixed_source("station_48"),
            fixed_source("hpc_bleed_1"),
            {
                "id": "lpt",
                "kind": "turbine.material.coordinate_map.cooling_injections",
                "version": "1.0.0",
                "port_counts": {"cooling": 1},
                "parameters": {
                    "reference_pressure": quantity(14.696, "psia"),
                    "reference_temperature": quantity(518.67, "degR"),
                    "flow_capacity_scale": 1.0,
                    "pressure_ratio_scale": 1.0,
                    "efficiency_scale": 1.0,
                    "cooling_position[1]": 1.0,
                },
                "artifacts": {"performance_map": "nasa-agtf30-lpt-map"},
                "materials": {
                    "inlet": "dry_air",
                    "outlet": "dry_air",
                    "cooling_1": "dry_air",
                },
            },
            sink("station_5"),
            {
                "id": "lp_shaft",
                "kind": "shaft.train.geared_two_load",
                "version": "1.0.0",
                "parameters": {
                    "speed_ratio": 3.1,
                    "shaft_efficiency": 0.99,
                    "gearbox_efficiency": 1.0,
                    "fixed_loss": quantity(0.0, "W"),
                },
            },
        ],
        "connections": [
            {"id": "station2_fan", "from": "station_2.outlet", "to": "fan.inlet", "kind": "material_link"},
            {"id": "fan_station21", "from": "fan.outlet", "to": "station_21.inlet", "kind": "material_link"},
            {"id": "station23_lpc", "from": "station_23.outlet", "to": "lpc.inlet", "kind": "material_link"},
            {"id": "lpc_station24a", "from": "lpc.outlet", "to": "station_24a.inlet", "kind": "material_link"},
            {"id": "station48_lpt", "from": "station_48.outlet", "to": "lpt.inlet", "kind": "material_link"},
            {"id": "bleed1_lpt", "from": "hpc_bleed_1.outlet", "to": "lpt.cooling_1", "kind": "material_link"},
            {"id": "lpt_station5", "from": "lpt.outlet", "to": "station_5.inlet", "kind": "material_link"},
            {"id": "lpt_shaft", "from": "lpt.shaft", "to": "lp_shaft.driver", "kind": "shaft_link"},
            {"id": "shaft_lpc", "from": "lp_shaft.direct_load", "to": "lpc.shaft", "kind": "shaft_link"},
            {"id": "shaft_fan", "from": "lp_shaft.geared_load", "to": "fan.shaft", "kind": "shaft_link"},
        ],
    }

    cases = []
    for data in CASES:
        case_id = data["id"]
        n2_rpm = data["n2_rpm"]
        fan_w, fan_tin, fan_pin, fan_tout, fan_pout, fan_torque = data["fan"]
        lpc_w, lpc_tin, lpc_pin, lpc_tout, lpc_pout, lpc_torque = data["lpc"]
        (lpt_w, lpt_tin, lpt_pin, lpt_wout, lpt_tout,
         lpt_pout, lpt_torque) = data["lpt"]
        cooling_w, cooling_t, cooling_p = data["cooling"]
        fan_coordinate, lpc_coordinate = data["map_coordinates"]
        (fan_hin, fan_hout, lpc_hin, lpc_hout, lpt_hin,
         lpt_hout, cooling_h) = data["enthalpies"]
        omega = n2_rpm * 3.141592653589793 / 30.0
        fan_omega = omega / 3.1
        fan_power = fan_torque * 1.3558179483314004 * fan_omega
        lpc_power = lpc_torque * 1.3558179483314004 * omega
        lpt_power = lpt_torque * 1.3558179483314004 * omega
        fixed = {
            "station_2.outlet.p": quantity(fan_pin, "psia"),
            "station_2.outlet.T": quantity(fan_tin, "degR"),
            "station_23.outlet.p": quantity(lpc_pin, "psia"),
            "station_23.outlet.T": quantity(lpc_tin, "degR"),
            "station_23.outlet.m_dot[N2]": quantity(lpc_w * 0.768, "lbm/s"),
            "station_48.outlet.p": quantity(lpt_pin, "psia"),
            "station_48.outlet.T": quantity(lpt_tin, "degR"),
            "hpc_bleed_1.outlet.p": quantity(cooling_p, "psia"),
            "hpc_bleed_1.outlet.T": quantity(cooling_t, "K"),
            "hpc_bleed_1.outlet.m_dot[N2]": quantity(cooling_w * 0.768, "lbm/s"),
            "lpc.shaft.omega": quantity(n2_rpm, "rpm"),
            "fan.map_coordinate.value": fan_coordinate,
        }
        guesses = {
            "station_2.outlet.h": quantity(fan_hin, "J/kg"),
            "fan.inlet.p": quantity(fan_pin, "psia"),
            "fan.inlet.h": quantity(fan_hin, "J/kg"),
            "fan.outlet.p": quantity(fan_pout, "psia"),
            "fan.outlet.h": quantity(fan_hout, "J/kg"),
            "fan.shaft.W_dot": quantity(fan_power, "W"),
            "fan.shaft.omega": quantity(n2_rpm / 3.1, "rpm"),
            "station_23.outlet.h": quantity(lpc_hin, "J/kg"),
            "lpc.inlet.p": quantity(lpc_pin, "psia"),
            "lpc.inlet.h": quantity(lpc_hin, "J/kg"),
            "lpc.outlet.p": quantity(lpc_pout, "psia"),
            "lpc.outlet.h": quantity(lpc_hout, "J/kg"),
            "lpc.map_coordinate.value": lpc_coordinate + 1.0e-4,
            "lpc.shaft.W_dot": quantity(lpc_power, "W"),
            "station_48.outlet.h": quantity(lpt_hin, "J/kg"),
            "lpt.inlet.p": quantity(lpt_pin, "psia"),
            "lpt.inlet.h": quantity(lpt_hin, "J/kg"),
            "lpt.outlet.p": quantity(lpt_pout, "psia"),
            "lpt.outlet.h": quantity(lpt_hout, "J/kg"),
            "lpt.stage_outlet_enthalpy": quantity(lpt_hout, "J/kg"),
            "lpt.map_coordinate.value": (lpt_pin / lpt_pout) + 1.0e-4,
            "lpt.shaft.W_dot": quantity(lpt_power, "W"),
            "lpt.shaft.omega": quantity(n2_rpm, "rpm"),
            "hpc_bleed_1.outlet.h": quantity(cooling_h, "J/kg"),
            "lpt.cooling_1.p": quantity(cooling_p, "psia"),
            "lpt.cooling_1.h": quantity(cooling_h, "J/kg"),
            "lp_shaft.driver.W_dot": quantity(lpt_power, "W"),
            "lp_shaft.driver.omega": quantity(n2_rpm, "rpm"),
            "lp_shaft.direct_load.W_dot": quantity(lpc_power, "W"),
            "lp_shaft.direct_load.omega": quantity(n2_rpm, "rpm"),
            "lp_shaft.geared_load.W_dot": quantity(fan_power, "W"),
            "lp_shaft.geared_load.omega": quantity(n2_rpm / 3.1, "rpm"),
        }
        for prefix, flow in (
            ("station_2.outlet", fan_w), ("fan.inlet", fan_w),
            ("fan.outlet", fan_w), ("station_21.inlet", fan_w),
            ("station_23.outlet", lpc_w), ("lpc.inlet", lpc_w),
            ("lpc.outlet", lpc_w), ("station_24a.inlet", lpc_w),
            ("station_48.outlet", lpt_w), ("lpt.inlet", lpt_w),
            ("lpt.outlet", lpt_wout), ("station_5.inlet", lpt_wout),
            ("hpc_bleed_1.outlet", cooling_w),
            ("lpt.cooling_1", cooling_w),
        ):
            species_guesses(guesses, prefix, flow)
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
