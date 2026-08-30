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
- `pump.fluid.semi_physical_positive_displacement` now provides the more appropriate generic
  diaphragm/positive-displacement closure for this rig: ideal displacement capacity minus
  pressure-driven leakage, with property-backed isentropic heating and shaft input. Three blocked
  experiment-family folds inside cases 1--68 give mass-flow MAPE of 0.34--0.80% and maximum errors
  of 0.57--1.99%; pump outlet-temperature MAE is 0.15--0.27 K. A full cases 1--68 fit gives an
  effective displacement of 4.283 mL/rev, conditional leakage area of 1.141e-8 m2 at fixed
  `Cd=0.8`, and aggregate PT-derived isentropic efficiency of 0.362. Cases 69--77 are diagnostic
  only and give 1.07% mass-flow MAPE and 0.30 K outlet-temperature MAE. This is strong
  component-blocked evidence, not external-boundary whole-cycle validation: measured pump pressure
  rise is still an input, and only `Cd*A` is identifiable.
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
  reach 28.64--47.77% at low-power points (28--58 W maximum absolute error). The original six-
  parameter formulation returned rank 5/6 in the full fit and every blocked fold, independently
  driving proportional mechanical loss to zero. Fixing that unsupported term at zero produces a
  clean five-parameter formulation; all fits are now full rank 5/5 and the objectives and
  predictions are unchanged to numerical precision. This removes redundancy without a prior or
  held-out-data tuning. Because the six-parameter fold results had already been inspected, the
  reduced blocked-fold rerun is confirmatory evidence of the rank correction, not a new untouched
  model-selection validation.
- The fitted leakage area is an effective conditional value: discharge coefficient is fixed at
  0.8, so these data identify the coefficient-area product rather than physical clearance area
  independently. The 300 rad/s mechanical-loss reference speed is only a parameterization
  normalization. Both assumptions are explicit in the result artifact and are not hardware facts.
- A frozen five-parameter fit on cases 1--68 was also evaluated diagnostically on consumed cases
  69--77. This is explicitly not independent validation. It gives mass-flow MAPE 10.44% with
  -10.44% bias, outlet-temperature MAE 1.01 K, and shaft-power MAPE 16.47% with -16.47% bias. The
  coherent underprediction shows that the model does not extrapolate without systematic bias to
  the changed source/sink-flow regime. Fresh hardware data are still required for a new holdout.
- The first complete external-boundary graph is now executable for case 1. It composes the generic
  semi-physical pump and expander with two-cell counterflow finite-volume evaporator/condenser
  assemblies, a rigid receiver, and the fixed-charge balance. From only utility inlet boundaries,
  pump/expander speeds, charge, and ambient temperature, it converges in 10 Newton iterations with
  maximum scaled residual 5.33e-12. Flow is -0.69% from measurement and utility outlet
  temperatures differ by +0.57/-0.08 K. This is topology and solver feasibility, **not validation**:
  expander inlet temperature differs by -7.74 K, condenser outlet by +6.12 K, pump discharge
  pressure by -14.59%, and expander power by -32.01%. The exchanger conductances and volumes are
  preliminary, receiver volume was adjusted for case-1 charge closure, hydraulic losses are near
  zero because effective flow areas are unknown, and case 1 participated in pump/expander fitting.
  The frozen result is retained in `external_boundary_case1_feasibility_results.json` so those
  limitations cannot be hidden by later calibration.
- External-boundary predictive validation therefore remains open. It requires training-only
  identification or traceable geometry for exchanger conductance, hydraulic loss, refrigerant
  holdup, receiver behavior, and connecting lines. The dataset's charge alone cannot independently
  identify every individual volume; the calibration must expose rank and profile diagnostics and
  then be frozen before any new holdout is evaluated.
- `thermox_orc_1kw_external_boundary_sweep` provides a bounded, single-process multi-case
  diagnostic path. It reconstructs utility mass flows with real-fluid inlet density, applies each
  case's external boundaries, charge, speed, and ambient temperature, and uses measured component
  endpoints only as nonlinear initial guesses—not as equations. Neighboring cases first use the
  previous converged root and retry once from their own measured-endpoint seed if needed.
- The first deliberately small charge-sweep diagnostic is frozen in
  `external_boundary_sweep_1_3_results.json`. Case 1 converges, while cases 2 and 3 fail from both
  initialization paths. Their best limiting scaled residuals are 4.21% in condenser hot-side
  inventory and 1.69% in the expander ambient heat-loss diagnostic. These adjacent cases change
  charge from 3.5 to 4.0 and 4.5 kg. This falsifies the assumption that the preliminary case-1
  inventory/exchanger parameterization can be reused unchanged across the charge sweep. It does
  **not** falsify the nonlinear kernel and is not an accuracy validation. A 68-case run is withheld
  until training-only volume, conductance, loss, and receiver parameters are identifiable; blindly
  spending CPU on the known-inadequate parameterization would add no evidence.
- A training-only inventory screen now tests whether the 68 measured boundary states and total
  charge values can identify three nonnegative lumped refrigerant volumes. The full linear system
  has numerical rank 3/3, but its QR reciprocal pivot ratio is only 3.32e-4 and the unconstrained
  optimum requires +700 L condenser volume and -352 L receiver volume. Exact active-set
  nonnegative least squares collapses to a single 7.88 L evaporator volume, yet still leaves
  0.742 kg charge RMSE and 1.989 kg maximum error. These are impossible for the documented 1 kW
  apparatus and decisively reject boundary-density-only charge allocation. The result is frozen in
  `inventory_identifiability_results.json`; numerical rank must not be misreported as physical
  identifiability.
- Whole-cycle inventory calibration therefore needs at least apparatus internal-volume data and a
  two-phase holdup/void-fraction model (or measured inventory/void fraction at selected operating
  points). Connecting-line, plate-channel, receiver, and expander trapped volumes cannot be inferred
  credibly from this dataset's total charge and boundary temperatures/pressures alone.
- The generic core can now use `volume.fluid.equilibrium_two_phase_correlated_outlet` in both steady
  and transient graphs. Its steady equations retain distinct thermodynamic holdup quality,
  correlation-derived void fraction, and transported outlet quality; a fixed-total-charge balance
  is regression-tested to solve the corresponding saturation pressure. Connecting-line inventory
  is represented compositionally by this volume plus a selected `pipe.fluid.*` hydraulic model,
  rather than by an ORC-specific line component. This closes the platform capability gap, but it
  does not manufacture the missing apparatus volumes or correlation applicability data.
- Finite-volume exchanger sides can now bind optional `hot_side_void_fraction_correlation` and
  `cold_side_void_fraction_correlation` artifacts. The closure invokes the artifact only in a
  two-phase PH state and falls back to the property-package density in liquid, vapor, supercritical,
  or other single-phase regimes. The same contract is verified in steady and transient execution,
  including a two-phase-to-vapor path.
- An explicit constant-slip sensitivity was run on real cases 1--10 with slip ratio 2; this ratio is
  a declared diagnostic assumption, not an identified hardware parameter. Both repeated 3.5 kg and
  4.0 kg charge blocks converge, while both 4.5 kg cases stop at 0.55--0.58% condenser-inventory
  residual and 5.0--6.5 kg cases fail primarily on total charge. The repeatability supports a
  missing-volume/holdup-capacity diagnosis. Across the four converged cases mass-flow MAPE is 1.31%
  (1.64% maximum), but expander-power MAPE is 44.56% (62.15% maximum). Thus Thermox is credibly
  executing a real external-boundary graph and predicting flow in its feasible subset, but this
  parameterization decisively fails a system power-accuracy claim. The frozen evidence is
  `external_boundary_slip_sensitivity_1_10_results.json`.
- The negative power and charge evidence is now decomposed in
  `external_boundary_negative_evidence_1_10_results.json`. At the measured expander inlet/outlet
  states, the frozen semi-physical expander predicts shaft power with 0.92% MAPE and 1.58% maximum
  error across the four converged cases. In the external-boundary cycle, the same evaluator has
  44.56% power MAPE because the cycle predicts expander inlet pressure 9.59% low, inlet enthalpy
  3.38% low, and outlet pressure 13.58% high on average. The dominant power failure is therefore
  upstream pressure/state formation, not a missing expander output multiplier.
- Failed high-charge final iterates now expose per-component inventory. The assumed 2 L homogeneous
  receiver saturates near 2.70 kg and the complete preliminary graph reaches only 4.50--4.75 kg,
  leaving 0.50--1.87 kg of the declared 5.0--6.5 kg charge unrepresented. This quantifies the
  earlier missing-volume diagnosis and shows that the evaporator's assumed refrigerant volume
  contributes only grams in these iterates.
- The associated Energy and Applied Energy papers explicitly pair two reality-based closures: a
  passive receiver whose inventory is the residual after other component inventories, and an
  empirical evaporation/expander-inlet pressure-formation model. Thermox now provides the generic
  steady component `receiver.fluid.passive_residual_charge`; its nonnegative stored mass is solved
  by the enclosing fixed-charge balance without imposing a homogeneous vessel state. Replacing the
  preliminary rigid receiver with it makes this benchmark under-specified by exactly one equation,
  independently confirming the missing pressure-formation closure. The original feasibility model
  therefore remains unchanged until the paper's pressure equation/coefficient provenance or a
  preregistered training-only replacement is available. Fixing measured internal pressure would
  close the graph but would forfeit the external-boundary prediction classification.
- The replacement pressure-formation study was preregistered in a separate pushed commit before
  fitting. `thermox_orc_1kw_pressure_formation_study` evaluates four fixed candidate families with
  column-pivoted QR, training-fold-only normalization, three experiment-family folds, explicit
  rank checks, and no access to cases 69--77 for selection. None passes the frozen 8% MAPE / 15%
  maximum-error gate on every fold. The two compact hot-source-temperature models fail the speed
  fold at 9.02--9.22% MAPE and 20.02--22.61% maximum error. The richer regularized external-
  boundary models are rank-deficient in the charge fold and extrapolate severely in the sink and
  speed folds. The result in `pressure_formation_study_results.json` therefore rejects every
  candidate and deliberately produces no deployable closure or consumed-holdout score. This
  dataset cannot currently replace the unavailable published pressure equation with a credible
  generic correlation.

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
./build/thermox_orc_1kw_semi_physical_expander_study \
  benchmarks/orc_1kw/measurements.csv consumed-holdout-diagnostic
```

Run the positive-displacement pump study with:

```sh
./build/thermox_orc_1kw_semi_physical_pump_study \
  benchmarks/orc_1kw/measurements.csv
```

Run the case-1 external-boundary closure feasibility model with:

```sh
./build/thermox_cli solve \
  --model benchmarks/orc_1kw/external_boundary_case1_feasibility.json \
  --case case_1 --format json
```

Run a bounded external-boundary case range with:

```sh
./build/thermox_orc_1kw_external_boundary_sweep \
  benchmarks/orc_1kw/external_boundary_case1_feasibility.json \
  benchmarks/orc_1kw/measurements.csv 1 10 2.0
```

The last argument is the declared vapor/liquid velocity slip ratio. It is mandatory so a benchmark
cannot silently choose or tune a holdup law.

This evidence executable is intentionally not registered as routine CTest because real-fluid
whole-cycle solves are comparatively expensive. Keep ranges small until their parameterization is
shown feasible.

Run the low-cost training-only inventory identifiability screen with:

```sh
./build/thermox_orc_1kw_inventory_identifiability_study \
  benchmarks/orc_1kw/measurements.csv
```

Run the preregistered pressure-formation discrimination study with:

```sh
./build/thermox_orc_1kw_pressure_formation_study \
  benchmarks/orc_1kw/measurements.csv
```

The declared residual scales are engineering model-discrimination scales, not instrument standard
uncertainties. Consequently, the study does not report uncertainty-qualified parameter covariance.
