# 1 kW R245fa ORC hardware validation

This benchmark is Thermox's first publicly reproducible, multi-operating-point whole-system
hardware validation candidate. It contains 77 complete steady operating points from a physical
R245fa organic Rankine-cycle testbed: diaphragm pump, brazed-plate evaporator, scroll expander,
brazed-plate condenser, and passive liquid receiver.

The source workbook is CC BY 4.0 and is identified exactly in `source_manifest.json`. The original
file remains under ignored `tmp/` so source acquisition and derived evidence stay visibly
separate. The benchmark must never silently turn measured internal states into predictive inputs.

`validation_contract.json` freezes the evidence ladder and the first train/holdout partition
before model fitting. Cases 1--68 contain structured charge, cooling-temperature, and
expander-speed sweeps. Cases 69--77 vary source/sink flow and combined boundaries and therefore
form the primary extrapolative holdout. Randomly mixing those rows would exaggerate confidence.

## Current status

- Source provenance, license, checksum, sheets, units, completeness, and join-key integrity are
  audited.
- Thermox's `coolprop_heos` registry is now an open-substance pure-fluid provider; `R245fa` is
  runtime validated by CoolProp rather than hard-coded into a component or benchmark.
- PT/PH/PS round trips, analytic PH derivatives, transport derivatives, saturation states, and a
  real-fluid transient volume are covered for R245fa by the normal property/platform tests.
- Measured-state thermodynamic accounting is executable across all 77 cases. Using Thermox's
  pinned CoolProp 8.0.0 path, the closed refrigerant-loop energy residual is 0.763% mean absolute
  and 1.552% maximum absolute, passing the preregistered 1%/2% accounting gate. The evaporator
  refrigerant duty is 3.580% lower than the water-side duty on average; condenser refrigerant duty
  is 3.046% higher on average. Those external-side differences remain diagnostics until water-flow
  metrology, ambient losses, and instrument uncertainty are recovered from the papers.
- Case 73 produces a slightly nonpositive pump fluid-power estimate from its measured PT pair and
  is flagged rather than corrected or removed.
- A generic steady/transient inventory accounting contract is now available: rigid volumes expose
  property-backed holdup and the steady-only `balance.fluid.fixed_total_charge` imposes a
  system-level charge. Transient graphs conserve charge through their differential mass balances.
  This removes the need to invent a loop pressure once the physical volumes are known.
- Constant-holdup heat-exchanger cells expose both side inventories; composition-aware cells,
  variable-mass two-phase cells, correlated volumes, and equilibrium drums expose their applicable
  fluid inventory. Distributed graphs can therefore report and constrain charge without reaching
  into component internals.
- A steady/transient finite-volume exchanger cell now computes each side's property-backed mass
  from its own geometry and mixed state. Its transient form conserves differential mass, fluid
  internal energy, and wall energy during filling or draining. Inventory links are medium-aware,
  preventing utility-fluid mass from entering a refrigerant charge balance. Rigid volumes provide
  the corresponding generic receiver/accumulator closure.
- The same generic exchanger DAE is regression-tested across a two-phase-to-vapor saturation
  boundary with no model switch or rejected integration step. This validates homogeneous-
  equilibrium regime traversal; it does not substitute for equipment-specific boiling,
  condensation, void-fraction, or dryout correlations.
- A correlation-driven finite-volume variant accepts independent, versioned hot/cold conductance
  artifacts with state, flow, transport, quality, saturation, and transient wall-superheat inputs.
  A saturation-crossing transient regression exercises the wall-dependent path and total-energy
  closure. This supplies the generic binding point for future ORC evaporator/condenser correlations
  without embedding R245fa or this dataset in platform code; it is not evidence for a particular
  boiling or condensation law until such an artifact is sourced and validated.
- A generic real-fluid pump performance-map component now closes pressure rise, isentropic outlet
  enthalpy, and shaft power from mass flow and shaft speed. Its synthetic R245fa regression proves
  the platform contract and explicitly rejects gas-path corrected maps. The 77 measurements do not
  themselves define a defensible non-rectangular pump map, so no hardware-validation claim is made
  until the paper, OEM data, or a preregistered training-only model supplies its form.
- `expander.fluid.volumetric_correlations` now supplies the generic component-blocked scroll-
  expander path. It predicts mass capacity, outlet enthalpy, shaft power, and an explicit rejected-
  heat stream from three versioned correlations. Synthetic R245fa regression covers steady and
  quasi-steady transient compilation and exact component energy closure.
- The first frozen component-blocked hardware evaluation is now executable and recorded in
  `expander_holdout_results.json`. A full quadratic response surface in pressure ratio, inlet
  pressure, and shaft speed was fitted on cases 1--68 only, then evaluated once on cases 69--77
  through the generic Thermox component. All 77 solves converged with a maximum normalized
  residual of 6.45e-10. Held-out outlet-temperature MAE is 1.90 K, but mass flow has 7.75% MAPE
  and 16.68% worst error, while shaft power has 24.41% MAPE and 48.37% worst error. The frozen
  model therefore **fails** the provisional 8% MAPE / 15% maximum-error primary-output gate. This
  is retained as negative evidence; it must not be post-hoc tuned against the exposed holdout.
- `expander.fluid.semi_physical_volumetric` is the generic physics response to that failure. It
  separates trapped displacement flow, choked leakage, built-in pressure-ratio mismatch,
  speed-dependent and proportional mechanical losses, and ambient heat rejection. Its real-fluid
  steady/transient regression closes mass and energy. A new canonical direct-evaluation API is
  shared by graph execution and optimization, avoiding a benchmark-side duplicate model.
- The semi-physical model has now been fitted and evaluated only inside cases 1--68 using three
  blocked experiment-family folds: charge sweeps, sink-temperature sweeps, and speed sweeps.
  Average validation errors are encouraging: mass-flow MAPE is 3.20--6.67%, outlet-temperature
  MAE is 0.33--1.16 K, and shaft-power MAPE is 3.82--7.97%. It nevertheless fails the provisional
  primary-output gate: speed-sweep mass flow reaches 16.84% error, and shaft-power relative maxima
  reach 28.64--47.77% at low-power points (28--58 W maximum absolute error). Every fit has local
  sensitivity rank 5 for 6 parameters, with proportional mechanical loss driven to zero. The data
  therefore do not independently identify the full loss decomposition. These are internal cross-
  validation results, not a fresh independent holdout; cases 69--77 remain consumed and excluded.
- External-boundary prediction remains open. It still requires validated scroll-expander closure
  artifacts, traceable heat-exchanger and receiver geometry, and a sourced or training-only pump
  artifact. The dataset's measured charge alone cannot identify the individual exchanger and
  receiver volumes.

## Evidence discipline

The measured-state accounting stage can validate property reconstruction and conservation, but it
cannot establish predictive accuracy. Component calibration must use only cases 1--68. The
parameters are owned by the component or system domains declared in the contract. Cases 69--77
were first inspected by Thermox only after the quadratic expander model family and acceptance
criteria were frozen. They are now a consumed holdout for that model revision. Any revised model
family needs a new benchmark revision and genuinely untouched validation data; it may use cases
69--77 for diagnosis, but not claim them again as an independent holdout.

Run the component-blocked evidence generator with:

```sh
./build/thermox_orc_1kw_expander_holdout_validation \
  benchmarks/orc_1kw/measurements.csv
```

Run the semi-physical full training fit and blocked internal cross-validation with:

```sh
./build/thermox_orc_1kw_semi_physical_expander_study \
  benchmarks/orc_1kw/measurements.csv full-fit
./build/thermox_orc_1kw_semi_physical_expander_study \
  benchmarks/orc_1kw/measurements.csv cross-validation
```

The declared residual scales are engineering model-discrimination scales, not instrument standard
uncertainties. Consequently, the study does not report uncertainty-qualified parameter covariance.
