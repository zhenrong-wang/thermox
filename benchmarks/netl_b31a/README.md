# NETL B31A combined-cycle benchmark

This directory contains a public, reproducible engineering benchmark for the
generic Thermox engine. It is deliberately separate from component model code:
published case values configure a normal registered topology and never alter
the solver or property implementations.

## Source

- U.S. DOE/NETL, *Cost and Performance Baseline for Fossil Energy Plants,
  Volume 1: Bituminous Coal and Natural Gas to Electricity*, May 2025,
  Case B31A, Exhibits 4-7 through 4-16.
- Official report:
  <https://netl.doe.gov/projects/VueConnection/download.aspx?filename=CostandPerformanceBaselineFossilEnergyPlantsVolume1BituminousCoalNaturalGastoElectricity_052125.pdf&id=7859ed18-9465-4fbb-86d4-d7ae3358ac12>
- OSTI record: <https://www.osti.gov/biblio/2580491>

The report is not copied into the repository. The tracked model contains only
the case values needed to reproduce the calculation.

## First validation slice

`hrsg_boundary.json` audits the aggregate gas side of the two triple-pressure,
reheat HRSGs. It is a boundary-constrained calculation, not an OEM off-design
prediction:

- exhaust flow, composition, inlet temperature, and inlet/outlet pressure come
  from Exhibit 4-8;
- recovered heat is independently reconstructed from all water/steam streams;
- the registered `cooler.material.fixed_duty` model predicts exhaust outlet
  enthalpy and Cantera predicts its temperature;
- the published 79 degC exhaust outlet is retained only as an acceptance target,
  not imposed on the solve.

The water/steam-side recovered heat is

```text
Q = (m5 h5 + m7 h7 + m9 h9 - m6 h6 - m11 h11) / 3600
  = 684.970477475 MW
```

Streams 5, 7, and 9 are the main-steam, hot-reheat, and low-pressure steam
outputs. Streams 6 and 11 are the cold-reheat and condensate/feedwater inputs.
Their total mass flows close to 1 kg/h at the report's displayed precision.

At Cantera 3.2.0 with `gri30.yaml`, Thermox predicts 357.686 K (84.536 degC),
5.536 K above the published 352.15 K (79 degC). The nonlinear equations close
below `1e-10`; the external acceptance tolerance is 7 K. These are intentionally
reported as different quantities.

For context, using the report's gas-side enthalpies gives 2491.929 GJ/h, while
the reconstructed steam-side recovery is 2465.894 GJ/h, a 26.035 GJ/h
(1.04 percent of gas-side recovery) boundary discrepancy. It includes report
rounding, property-method differences, and heat losses; it is not a numerical
residual. Exhibit 4-16 separately reports plant-wide ambient and unaccounted
energy, so exact equality should not be manufactured by calibration.

The model also includes a `published_gas_enthalpy` case. It uses the gas-side
enthalpy difference printed in Exhibit 4-8 rather than the independently
reconstructed steam-side duty. Cantera then predicts 351.323 K (78.173 degC),
only 0.827 K below the published stack temperature. This isolates most of the
5.536 K boundary discrepancy to the published gas/steam heat balance rather
than the exhaust property implementation.

## Detailed verification

`steam_stream_states.json` independently evaluates the published water and
steam states with the registered CoolProp IF97 backend. For streams 5-9 and 11,
only published flow, pressure, and temperature are specified; enthalpy is a
calculated result. The compiler obtains a property-informed initial enthalpy
from `state_pt`, so the declaration requires no hand-authored enthalpy guesses.

| Stream | Service | Published h (kJ/kg) | IF97 h (kJ/kg) | Difference (kJ/kg) | Published / IF97 density (kg/m3) |
|---|---|---:|---:|---:|---:|
| 5 | Main steam | 3520.51 | 3522.653 | +2.143 | 48.1 / 48.038 |
| 6 | Cold reheat | 3124.08 | 3125.122 | +1.042 | 15.1 / 15.119 |
| 7 | Hot reheat | 3641.17 | 3641.737 | +0.567 | 9.9 / 9.887 |
| 8 | Steam stream 8 | 3062.57 | 3064.092 | +1.522 | 2.0 / 1.991 |
| 9 | Steam stream 9 | 3071.95 | 3072.613 | +0.663 | 1.9 / 1.938 |
| 11 | Condensate | 160.78 | 159.114 | -1.666 | 992.8 / 992.933 |

The differences are all below 2.15 kJ/kg (0.061 percent for the worst
high-enthalpy state). They reflect steam-table reference and displayed-input
precision rather than numerical non-closure.

Stream 10 is wet steam, so its published pressure and enthalpy are used as the
independent pair. IF97 returns 318.958 K (45.808 degC) and quality 0.9130. The
report prints 38 degC and pressure to only two decimal places in MPa; 38 degC
saturation pressure is approximately 0.0066 MPa, which prints as 0.01 MPa.
Consequently all three displayed values cannot be imposed simultaneously.

Detailed source consistency checks also establish:

- air plus fuel equals exhaust exactly at displayed precision;
- total HRSG water/steam flow closes within 1 kg/h;
- gas-side and water/steam-side HRSG recoveries are 2491.929 and
  2465.894 GJ/h, respectively;
- the steam streams imply 279.054 MW shaft power and 272.077 MW after the
  report's 97.5 percent generator efficiency, versus 272 MWe published;
- the steam exhaust/condensate pair implies 1461.403 GJ/h condenser duty,
  versus 1461 GJ/h published;
- published fuel input and net power independently produce 53.447 percent HHV
  efficiency and 6735.63 kJ/kWh heat rate, versus 53.4 percent and 6736 kJ/kWh.

These checks are executable service regressions. Numerical residual limits,
property agreement tolerances, and source-balance tolerances are separate
assertions so one category cannot conceal another.

Run the case with:

```sh
./build/thermox_cli solve \
  --model benchmarks/netl_b31a/hrsg_boundary.json \
  --case published_boundary --continuation --format text

./build/thermox_cli solve \
  --model benchmarks/netl_b31a/steam_stream_states.json \
  --case published_states --format text
```

## Scope and next expansion

This slice validates composition-aware material transport, a prescribed heat
balance, Cantera state recovery, unit normalization, graph compilation,
continuation, and service result projection against a real combined-cycle
boundary. It does not yet validate the complete B31A plant.

The next benchmark expansion should replace the aggregate duty with segmented
HP/IP/LP economizer, evaporator, superheater, and reheater sections, then add a
multi-admission steam turbine and the gas-turbine train. Published design-point
data can validate those balances; credible off-design prediction will still
require equipment maps or equivalent OEM performance data.
