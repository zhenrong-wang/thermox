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

Run the case with:

```sh
./build/thermox_cli solve \
  --model benchmarks/netl_b31a/hrsg_boundary.json \
  --case published_boundary --continuation --format text
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
