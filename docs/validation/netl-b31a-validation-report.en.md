# Thermox Technical Validation Report

## NETL Case B31A Natural-Gas Combined Cycle

**Document status:** Engineering review draft<br>
**Revision:** 2<br>
**Date:** 11 August 2026<br>
**Thermox validation baseline:** commit `fc87fa6`<br>
**Language:** English

## 1. Executive summary

This report documents the current validation of Thermox against the public
U.S. DOE/NETL Case B31A natural-gas combined-cycle reference. Its purpose is to
give thermal engineers enough technical detail to evaluate both the results and
the limits of the validation.

The work supports the following conclusion:

> Thermox has a credible steady-state, design-point numerical and physical
> foundation. Water/steam and exhaust-gas properties, graph compilation,
> nonlinear solution, mass and energy conservation, and a connected HP/IP/LP
> steam-turbine train have been validated. The work does not yet constitute a
> fully detailed or independently validated combined-cycle prediction model.

The distinction is important. The B31A publication contains sufficient data to
verify overall plant balances, major stream states, aggregate HRSG recovery,
and the steam-turbine train at its design point. It does not publish enough
independent equipment information to identify every gas-turbine map, HRSG coil,
drum, attemperator, leakage path, or off-design characteristic.

Principal results are:

| Verification item | Thermox result | Published reference | Assessment |
|---|---:|---:|---|
| Maximum IF97 steam-state enthalpy difference | 2.143 kJ/kg | Stream-table values | Good agreement |
| HRSG gas-side heat recovery | 2491.929 GJ/h | Derived from streams 3–4 | Reproduced |
| HRSG water/steam-side recovery | 2465.894 GJ/h | Derived from streams 5–7, 9, 11 | Reproduced |
| Water-side-driven stack temperature | 84.536 °C | 79 °C | +5.536 K |
| Gas-enthalpy-driven stack temperature | 78.173 °C | 79 °C | −0.827 K |
| Connected steam-train generator output | 271.736 MWe | 272 MWe | −0.264 MWe |
| Condenser duty | 1461.403 GJ/h | 1461 GJ/h | +0.403 GJ/h |
| Net HHV efficiency | 53.447% | 53.4% | Rounding agreement |
| Net HHV heat rate | 6735.63 kJ/kWh | 6736 kJ/kWh | Rounding agreement |
| Representative normalized nonlinear residual | 4.3×10⁻¹⁴ | Acceptance limit 1×10⁻¹⁰ | Converged |

The 5.536 K stack-temperature difference is not a nonlinear solver residual.
It is primarily the consequence of a 26.035 GJ/h difference between the
published gas-side and reconstructed water/steam-side HRSG balances, together
with property-method and displayed-data precision. Thermox records numerical
closure and engineering agreement as separate quantities.

## 2. Reference case and source

The benchmark is based on:

- U.S. Department of Energy, National Energy Technology Laboratory,
  *Cost and Performance Baseline for Fossil Energy Plants, Volume 1:
  Bituminous Coal and Natural Gas to Electricity*, May 2025;
- Case B31A, NGCC without CO₂ capture;
- Exhibits 4-7 through 4-16, particularly the process diagram, stream table,
  performance summary, power summary, and energy balance.

Official sources:

- [NETL report PDF](https://netl.doe.gov/projects/VueConnection/download.aspx?filename=CostandPerformanceBaselineFossilEnergyPlantsVolume1BituminousCoalNaturalGastoElectricity_052125.pdf&id=7859ed18-9465-4fbb-86d4-d7ae3358ac12)
- [OSTI bibliographic record](https://www.osti.gov/biblio/2580491)

The report describes a nominal 2×1 F-class combined cycle consisting of two
combustion turbines, two triple-pressure reheat HRSGs, and one condensing,
triple-admission steam-turbine generator. The published stream-table flow rates
represent both gas-turbine/HRSG trains in total.

The source PDF is not committed to the Thermox repository. Only the minimum
public values required for reproducible calculations are tracked.

## 3. Terminology and evidence classification

The following classifications are used throughout this report.

| Classification | Meaning |
|---|---|
| Published | Printed directly in the NETL report |
| Derived | Arithmetic reconstruction using published values only |
| Calculated | Produced by Thermox equations and registered property packages |
| Calibrated | A model parameter estimated from a published design-point observation |
| Assumed | An interpretation required because an internal state or characteristic is not published |

A calibrated result is not treated as independent predictive validation. For
example, the HP/IP/LP isentropic efficiencies reproduce the design-point outlet
states used to derive them. The connected topology, mixing, leakage routing,
mass balance, property evaluation, and power aggregation remain meaningful
tests, but off-design prediction requires independent data.

The executable regressions encode these distinctions through
`thermox.validation_evidence/v1`. Each dimensioned comparison carries a layer,
source reference, tolerance, explicit limitations, and one evidence basis:
independent reference, boundary constrained, calibrated reproduction, derived
reference, internal consistency, or assumption dependent. Classification does
not change a numeric verdict, and a passing comparison cannot promote itself to
a stronger evidence class. In this report, the IF97 enthalpy comparisons are
independent published-output checks, the aggregate HRSG temperature comparison
is boundary constrained, the steam-train power comparison is calibrated, and
scaled equation closure is internal consistency.

## 4. Platform elements under test

The benchmark exercises normal, reusable Thermox capabilities. No B31A-specific
equation is compiled into the core.

### 4.1 Numerical and graph platform

- versioned declarative model documents;
- typed fluid and material connectors;
- structural degree-of-freedom analysis;
- SI unit normalization;
- sparse Newton solution and continuation;
- property-informed initialization;
- component and system result projection;
- separation of numerical diagnostics from engineering acceptance criteria.

### 4.2 Property packages

- CoolProp IF97 for water and steam;
- Cantera 3.2.0 with `gri30.yaml` for exhaust-gas mixtures;
- composition carried as conserved species mass flows;
- pressure–enthalpy state recovery and derived temperature, density, entropy,
  quality, heat capacity, and molecular weight.

### 4.3 Generic physical components

- material and fluid boundaries;
- splitters and mixers;
- isenthalpic pressure-equalization valves;
- HP, IP, and LP isentropic-efficiency turbine models;
- composition-aware material coolers;
- fixed-duty, UA/LMTD, and design-point energy-balance material-to-fluid heat
  exchangers;
- component-scoped calibration declarations.

## 5. Published design-point boundaries

### 5.1 Gas path and plant performance

| Quantity | Published value |
|---|---:|
| Ambient air flow | 3,764,363 kg/h |
| Natural-gas flow | 95,442 kg/h |
| Turbine exhaust flow | 3,859,805 kg/h |
| Turbine exhaust temperature | 652 °C |
| Stack temperature | 79 °C |
| Combustion-turbine generator power | 484 MWe |
| Steam-turbine generator power | 272 MWe |
| Total gross power | 755 MWe |
| Auxiliary load | 14 MWe |
| Net power | 741 MWe |
| Net HHV efficiency | 53.4% |
| Net LHV efficiency | 59.2% |
| Net HHV heat rate | 6736 kJ/kWh |
| Condenser duty | 1461 GJ/h |

The gas-path mass balance closes exactly at displayed precision:

```text
3,764,363 + 95,442 = 3,859,805 kg/h
```

### 5.2 Principal water/steam streams

| Stream | Interpretation | Flow (kg/h) | Pressure (MPa abs) | Temperature (°C) | Published h (kJ/kg) |
|---|---|---:|---:|---:|---:|
| 5 | Main steam | 542,286 | 17.30 | 585 | 3520.51 |
| 6 | Cold reheat | 526,630 | 4.13 | 364 | 3124.08 |
| 7 | Hot reheat | 575,515 | 3.84 | 585 | 3641.17 |
| 8 | Steam stream 8 | 590,534 | 0.52 | 300 | 3062.57 |
| 9 | Steam stream 9 / LP admission | 69,218 | 0.51 | 304 | 3071.95 |
| 10 | LP exhaust | 659,752 | 0.01 rounded; 1 psia in diagram | 38 | 2375.86 |
| 11 | Condensate | 660,390 | 0.01 rounded | 38 | 160.78 |

The HRSG water/steam boundary closes within 1 kg/h at displayed precision:

```text
inputs  = m11 + m6             = 1,187,020 kg/h
outputs = m5 + m7 + m9         = 1,187,019 kg/h
difference                       = 1 kg/h
```

## 6. Verification methodology

The validation is divided into independent layers so that a good result in one
layer cannot conceal a weakness in another.

### 6.1 Source consistency checks

Published flows, enthalpies, powers, efficiencies, and heat rates are first
recombined arithmetically without invoking the Thermox solver. This identifies
rounding and internal source discrepancies before model comparison.

### 6.2 Property verification

For streams 5–9 and 11, published flow, pressure, and temperature are specified;
enthalpy and density are calculated independently using the registered IF97
backend. Stream 10 is wet steam, so pressure and enthalpy form the independent
pair and temperature and vapor quality are calculated.

Exhaust-gas composition, pressure, and temperature are evaluated through
Cantera. Molecular weight, density, and enthalpy differences are compared with
the NETL table.

### 6.3 Aggregate HRSG boundary check

The water/steam-side duty is reconstructed as:

```text
Qsteam = (m5 h5 + m7 h7 + m9 h9 − m6 h6 − m11 h11) / 3600
       = 684.970477 MW
       = 2465.894 GJ/h
```

Thermox applies this independently reconstructed duty to the published exhaust
inlet flow, composition, pressure, and temperature. Stack temperature is a
calculated result and is compared with 79 °C.

A second calculation uses the gas-side enthalpy difference printed by NETL:

```text
Qgas = m3 (h3 − h4)
     = 2491.929 GJ/h
```

This separates gas-property agreement from the cross-boundary energy
discrepancy.

### 6.4 Connected steam-turbine train

The steam train is solved as a connected component graph:

```text
main steam -> gland split -> HP turbine -> cold-reheat / HP-leak split
hot reheat + throttled HP leak -> IP mixer -> IP turbine
IP exhaust + LP admission -> LP mixer -> LP turbine -> condenser
```

The component efficiencies are calibrated from design-point outlet states and
recorded as component-level parameters with bounds and observation uncertainty.
The graph then independently enforces pressure ratios, flow routing, mixing
enthalpy, shaft power, and aggregate mass and energy balances.

### 6.5 Numerical acceptance

The principal numerical requirement is a normalized final residual below
`1×10⁻¹⁰`. This is intentionally much stricter than engineering agreement
tolerances. A converged numerical model may still be physically inaccurate;
conversely, rounded public data should not be forced to machine-precision
agreement through hidden corrections.

## 7. Results

### 7.1 IF97 water/steam properties

| Stream | Published h (kJ/kg) | IF97 h (kJ/kg) | Difference (kJ/kg) | Published density (kg/m³) | IF97 density (kg/m³) |
|---|---:|---:|---:|---:|---:|
| 5 | 3520.51 | 3522.653 | +2.143 | 48.1 | 48.038 |
| 6 | 3124.08 | 3125.122 | +1.042 | 15.1 | 15.119 |
| 7 | 3641.17 | 3641.737 | +0.567 | 9.9 | 9.887 |
| 8 | 3062.57 | 3064.092 | +1.522 | 2.0 | 1.991 |
| 9 | 3071.95 | 3072.613 | +0.663 | 1.9 | 1.938 |
| 11 | 160.78 | 159.114 | −1.666 | 992.8 | 992.933 |

All listed enthalpy differences are below 2.15 kJ/kg. The largest relative
difference among the high-enthalpy steam states is approximately 0.061%.
Differences include property-reference convention and published input rounding.

For stream 10, the detailed diagram's 1 psia value is preferred over the metric
table's rounded 0.01 MPa. At 1 psia and 2375.86 kJ/kg, IF97 calculates:

- temperature: 38.719 °C;
- vapor quality: 0.9189.

These agree with the diagram's displayed 101 °F / 38 °C exhaust. Treating the
rounded metric pressure as exact would instead produce 45.808 °C and an
artificially high inferred LP-stage efficiency.

### 7.2 Exhaust-gas properties and HRSG boundary

At the turbine exhaust boundary, Cantera calculates:

- mixture molecular weight: 28.357846 kg/kmol, versus 28.357 published;
- density: 0.4055 kg/m³, versus 0.4 published;
- inlet temperature: 652 °C by boundary specification.

With the independently reconstructed water/steam duty, the predicted stack
temperature is 84.536 °C, 5.536 K above the published value.

With the published gas-side enthalpy drop, the predicted stack temperature is
78.173 °C, 0.827 K below the published value. This indicates that Cantera's
exhaust sensible-enthalpy change is close to the NETL/Aspen result; most of the
larger water-side-driven discrepancy comes from the published cross-boundary
balance rather than the nonlinear solver or gas property bridge.

The gas-side minus water/steam-side recovery is:

```text
2491.929 − 2465.894 = 26.035 GJ/h
```

This is 1.04% of gas-side recovery. It may include pump work embedded in the
chosen water boundary, heat loss, property-reference differences, and displayed
stream rounding. Exhibit 4-16 also reports plant-wide ambient loss and
unaccounted energy; therefore zero discrepancy should not be manufactured.

### 7.3 Connected HP/IP/LP steam train

| Stage | Calibrated ηis | Thermox shaft power (MW) |
|---|---:|---:|
| HP | 0.89802 | 59.812 |
| IP | 0.90396 | 92.600 |
| LP | 0.91687 | 126.292 |
| Total | — | 278.704 |

Applying the published 97.5% steam-turbine generator efficiency gives:

```text
278.704 × 0.975 = 271.736 MWe
```

The published result is 272 MWe, a difference of −0.264 MWe, or approximately
−0.10%. The graph simultaneously reproduces the cold-reheat flow and the final
condenser flow within the displayed source precision.

The fitted efficiencies are plausible design-point values, but they are not
independent evidence of off-design performance. The important independent
checks in this model are connected routing, pressure equalization, mixing,
leakage accounting, IF97 state evaluation, shaft-power calculation, and total
flow/energy closure.

### 7.4 Other independently reconstructed plant quantities

| Quantity | Reconstructed | Published | Difference |
|---|---:|---:|---:|
| Steam thermodynamic shaft power | 279.054 MW | Not directly tabulated | — |
| Steam-turbine generator-terminal power | 272.077 MWe | 272 MWe | +0.077 MWe |
| Condenser duty | 1461.403 GJ/h | 1461 GJ/h | +0.403 GJ/h |
| Net HHV efficiency | 53.447% | 53.4% | +0.047 percentage point |
| Net HHV heat rate | 6735.63 kJ/kWh | 6736 kJ/kWh | −0.37 kJ/kWh |

The 272.077 MWe arithmetic reconstruction uses published stream enthalpies
directly. The 271.736 MWe connected-model result uses IF97 states, explicit
gland routing, and the leakage interpretation described below.

## 8. Explicit assumptions and interpretations

The following choices are visible in the benchmark adapter and are not hidden
inside core equations.

1. The 1,405 lb/h gland stream is split before the HP turbine at main-steam
   enthalpy, consistent with the detailed diagram.
2. The 33,112 lb/h HP leakage stream is assigned HP-exhaust enthalpy and then
   throttled to hot-reheat pressure because no independent leakage state is
   published.
3. The detailed 1 psia LP exhaust pressure is used instead of treating the
   rounded 0.01 MPa conversion as exact.
4. Exhaust composition is treated as frozen through the HRSG boundary.
5. The aggregate HRSG model uses a prescribed recovered duty; it does not claim
   to identify individual coil geometry or UA.
6. Stage efficiencies are design-point calibration parameters, not universal
   constants or OEM map replacements.
7. Displayed source values are not adjusted merely to force exact external
   agreement.

Thermal engineers are specifically requested to review assumptions 1 and 2,
because alternate leakage extraction locations or enthalpies will redistribute
stage power while leaving the overall published boundary nearly unchanged.

## 9. What has and has not been validated

### 9.1 Supported claims

- Thermox can compile and solve a generic connected steady-state thermal graph.
- Numerical equations close tightly for the tested models.
- IF97 and Cantera are correctly integrated through the platform property
  registries.
- Major B31A mass, energy, steam-state, condenser, and efficiency arithmetic is
  reproducible.
- The HP/IP/LP steam train can be represented with reusable components and
  component-scoped calibration.
- Aggregate combined-cycle/HRSG boundaries are consistent within quantified
  engineering tolerances.

### 9.2 Unsupported claims

- A complete coil-by-coil triple-pressure HRSG has been validated.
- The gas turbine can predict unseen loads or ambient conditions.
- The steam turbine can predict off-design swallowing capacity or wet-stage
  performance.
- The complete combined cycle has been solved as one independently predictive
  graph.
- Startup, shutdown, control response, or other combined-cycle transients have
  been validated.
- The calibrated design point proves OEM-equivalent component performance.

## 10. Hard limits imposed by the available benchmark

The primary limit is parameter identifiability, not nonlinear convergence.
Multiple internal equipment models can reproduce the same published plant
inlet/outlet states and total power.

### 10.1 HRSG information not published sufficiently

- coil areas and clean/fouled UAs;
- detailed gas and steam states at every coil boundary;
- gas-side and water-side pressure-loss distribution;
- HP/IP/LP drum pressure and level-control assumptions;
- evaporator circulation and blowdown;
- pinch and approach targets by pressure level;
- attemperator spray states and control laws;
- casing/radiation loss allocation.

The public diagram shows coil ordering and selected intermediate labels, but it
does not provide enough independent values to identify all section parameters.
Assigning arbitrary values could produce a converged model without producing a
validated one.

### 10.2 Gas-turbine information not published sufficiently

- compressor and turbine maps;
- corrected speed and corrected flow conventions;
- variable-geometry and control schedules;
- compressor bleed and turbine cooling flows;
- combustor efficiency and pressure-loss behavior;
- firing-temperature and exhaust-temperature control logic;
- mechanical loss variation and auxiliary boundaries.

### 10.3 Steam-turbine off-design information not published sufficiently

- stage-group flow-capacity or Stodola-type coefficients;
- efficiency maps versus flow, pressure ratio, speed, and quality;
- leakage variation with pressure/load;
- admission/extraction control behavior;
- moisture separation and wet-stage loss correlations;
- condenser-pressure performance curves.

### 10.4 Transient information not published

- drum, vessel, and piping inventories;
- metal masses and thermal capacitances;
- valve/actuator dynamics;
- control loops and set-point schedules;
- transport delays;
- time-series startup, load-change, trip, or shutdown observations.

## 11. Data requested for the next validation stage

The most valuable additions would be:

1. At least two independent operating points not used for calibration,
   preferably including different load and ambient conditions.
2. HRSG coil-by-coil gas inlet/outlet temperatures, water/steam states, duties,
   pressure losses, and UA or geometry information.
3. HP/IP/LP drum pressures, circulation ratios, blowdown, and attemperator spray
   flow/state.
4. Gas-turbine compressor/turbine maps or an OEM performance deck with bleed,
   cooling, and control definitions.
5. Steam-turbine stage-group flow/efficiency characteristics and leakage states.
6. Measurement uncertainty, instrument location, averaging period, and whether
   values are measured, corrected, or calculated.
7. For transient validation, synchronized time histories and equipment
   inventory/metal-mass data.

One independent off-design point is more valuable for predictive validation
than many additional parameters fitted to the same design point.

## 12. Questions for thermal-engineering review

Reviewers are requested to comment specifically on the following items.

1. Is the interpretation of streams 8 and 9 and their admission into the LP
   train consistent with the NETL balance diagram?
2. Is splitting the gland stream before the HP expansion correct for the stated
   enthalpy, or should a different extraction state be used?
3. What thermodynamic state should be assigned to the 33,112 lb/h HP leakage
   entering the IP section?
4. Is 1 psia the correct design condenser boundary, and should backpressure be
   inferred from saturation temperature instead?
5. Are the derived HP/IP/LP efficiencies reasonable for the stated F-class
   combined-cycle steam turbine?
6. Which portion of the 26.035 GJ/h HRSG boundary discrepancy should be assigned
   to pumps, HRSG ambient loss, source rounding, or property-method differences?
7. Which HRSG internal states in Exhibit 4-15 are sufficiently authoritative to
   use as constraints rather than initialization or calibration observations?
8. What minimum equipment data would the reviewers consider sufficient for a
   credible off-design acceptance test?

## 13. Reproduction

From the Thermox repository root:

```sh
cmake --build build --parallel 2

./build/thermox_cli solve \
  --model benchmarks/netl_b31a/hrsg_boundary.json \
  --case published_boundary --continuation --format text

./build/thermox_cli solve \
  --model benchmarks/netl_b31a/hrsg_boundary.json \
  --case published_gas_enthalpy --continuation --format text

./build/thermox_cli solve \
  --model benchmarks/netl_b31a/steam_stream_states.json \
  --case published_states --format text

./build/thermox_cli solve \
  --model benchmarks/netl_b31a/steam_turbine_train.json \
  --case published_design --format text

ctest --test-dir build -j 1 --output-on-failure
```

The serial test setting is intentional for the current development host.

Tracked benchmark files:

- `benchmarks/netl_b31a/hrsg_boundary.json`
- `benchmarks/netl_b31a/steam_stream_states.json`
- `benchmarks/netl_b31a/steam_turbine_train.json`
- `benchmarks/netl_b31a/README.md`

## 14. Final assessment

The present evidence is sufficient to proceed confidently with Thermox as a
generic steady-state thermal-system platform and with further component-model
development. It is also sufficient to present the B31A work as a detailed
design-point steam-train validation plus an aggregate combined-cycle/HRSG
boundary validation.

The evidence is not sufficient to present Thermox as a validated off-design or
transient combined-cycle predictor. Reaching that level requires more internal
equipment information and, most importantly, independent operating points.
