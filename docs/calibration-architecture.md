# Calibration Architecture

## Purpose

Calibration is an application workflow over a physical Thermox model. It is not a component
equation, a property-package concern, or a special execution path in the nonlinear kernel.

The platform keeps four inputs distinct:

1. the model owns component instances, topology, materials, artifacts, and physical parameters;
2. cases own operating and boundary conditions;
3. calibration definitions select adjustable model parameters and measured observations;
4. the service runs estimation and records fitted values, residuals, uncertainty, and provenance.

This separation allows the same graph to be simulated directly, calibrated against acceptance
data, and then validated against independent cases.

## Physical ownership and calibration scope

Every adjustable value references the physical parameter consumed by an equation:

```text
components.<component-id>.parameters.<parameter-name>
connections.<connection-id>.parameters.<parameter-name>
cases.<case-id>.fixed_values.<graph-variable>
cases.<case-id>.parameter_overrides.<component-parameter-path>
```

A `component`-scoped calibration parameter has exactly one target. A `system`-scoped parameter may
represent a plant-wide or shared estimate and may bind multiple dimensionally compatible physical
targets with the same initial value. Scope controls estimation and sharing; it does not move the
parameter out of its physical owner.

Case-owned fixed-value targets support inverse boundary reconstruction without inventing a
component parameter. For example, holding measured power and thermodynamic states fixed while
estimating an inlet flow uses `cases.test_point.fixed_values.inlet.outlet.m_dot`. Each independently
estimated operating-point boundary should be a separate calibration parameter. Shared physical
parameters remain component- or connection-owned and can still be fitted over those same cases.
Case-owned parameter overrides use the parallel `parameter_overrides` path when the estimated
quantity is an operating configuration such as guide-vane angle rather than a graph boundary.

Effects with a recognizable physical owner remain explicit graph elements. Shaft loss belongs to a
shaft-train component, generator loss to a generator, bleed and cooling flows to topology, duct
loss to a transport component, and auxiliary power to an auxiliary-load component. Calibration
must not introduce an unowned global correction that hides missing physics.

Map-driven compressor and turbine instances expose `flow_capacity_scale`,
`pressure_ratio_scale`, and `efficiency_scale` as bounded component parameters. They provide a
standard calibration surface while leaving the bound OEM or test map immutable. Calibration uses
the same generic parameter-target contract; no map-specific optimizer path exists.

The optional `cases` list specifies the operating cases over which a fitted value is shared. An
omitted list means that the future estimation workflow may use the parameter wherever the
calibration campaign requires it.

## Bounds, priors, and observations

Bounds and priors are typed scalar quantities. Their dimensions must match every target. Prior
standard deviations and observation uncertainties must be positive.

An observation identifies a case, a graph result target, a measured value, and its uncertainty:

```json
{
  "id": "net_power",
  "case": "baseline",
  "target": "generator.electrical.P",
  "measured": {"value": 257.5, "unit": "MW"},
  "sigma": {"value": 1.0, "unit": "MW"}
}
```

A transient-case observation additionally declares its physical sample time:

```json
{
  "id": "drum_level_30s",
  "case": "load_step",
  "target": "drum.level.value",
  "time": {"value": 30.0, "unit": "s"},
  "measured": {"value": 0.42, "unit": "m"},
  "sigma": {"value": 0.005, "unit": "m"}
}
```

Time is required for transient cases and forbidden for steady cases. It is not a sample index or
nearest-step hint. The adaptive DAE integrator treats the sorted unique observation times as
required output boundaries, derives each case horizon from its latest observation, and emits
accepted states at those exact times. The configured transient start time still defines the
trajectory origin and must precede the latest observation.

Uncertainty is part of the contract so estimation can weight unlike measurements without treating
all field values as exact.

Measurements that share an uncertainty source may declare pairwise `measurement_correlations` on
the calibration definition. Each entry references two observation IDs and a coefficient strictly
inside `(-1, 1)`. Unspecified pairs remain independent. Model validation assembles the complete
correlation matrix and requires it to be positive definite; individual valid-looking pairs cannot
therefore create an impossible joint uncertainty model. The service uses a Cholesky factor to
whiten normalized residuals before evaluating the calibration objective. The response reports the
declared pair count and whether covariance was applied.

## Validation boundary

Model-document parsing currently validates:

- unique calibration, parameter, and observation IDs;
- supported component/system scopes;
- target existence and dimensional compatibility;
- consistent initial values for shared targets and initial values inside calibration bounds;
- case references;
- ordered bounds;
- positive prior and observation uncertainties;
- measured-value and uncertainty dimensions;
- a non-negative time quantity on every transient observation and no time on steady observations;
- unique, known correlation pairs and a positive-definite measurement correlation matrix.

Service validation additionally resolves observation paths against the active component registry
and checks that measured dimensions match the exposed primary or derived result. Fluid and
composition-aware material ports both expose derived temperature, so measured gas-path
temperatures can participate in gas-turbine calibration without becoming connector unknowns.

The service executes bounded, multi-case estimation by repeatedly invoking the ordinary steady or
transient simulation workflow selected by each case. All observations for one transient case are
grouped into one trajectory solve, including multiple targets at the same sample time. Its
optimizer boundary is a generic scaled, bounded nonlinear least-squares
contract in the numerical core. The reference implementation uses rank-revealing Gauss-Newton
steps inside an adaptive trust region: candidate solve failures shrink the region, every
observation is weighted by its declared uncertainty, declared correlations whiten the joint
residual, and priors contribute normalized penalty residuals. Evaluations and case solves remain
sequential to bound host resource use. The numerical callback receives the accepted reference
point, allowing service-owned continuation and warm starts without exposing thermal-system state
to the optimizer.

Each accepted steady case solution is retained as a named-variable warm start. A candidate parameter move
first attempts the requested endpoint; on failure, adaptive continuation halves the move and walks
through converged neighboring states before retrying the endpoint. Continuation controls and all
underlying Newton settings are recorded in execution provenance.

The response includes initial/fitted values, bounds, per-observation physical and normalized
residuals, objective diagnostics, complete execution settings, and a canonical fitted model that
can be submitted directly for independent validation runs.

After fitting, the service evaluates a bounded finite-difference sensitivity through the same
simulation path. It reports measurement-only sensitivity rank separately from the rank after
declared parameter priors are appended. This distinction is intentional: a prior can make a local
posterior well posed, but it cannot make the plant measurements identify a parameter. For the
locally free, full-rank posterior subspace, the rank-revealing least-squares factorization returns
standard uncertainties and correlations without forming normal equations. Bound-active parameters
are labelled explicitly and receive no symmetric two-sided uncertainty. Failure of this evidence
calculation does not discard an otherwise valid fit; it produces `uncertainty_available=false`
with a diagnostic message. Covariance is likewise withheld when the reference optimizer reaches
its iteration budget without convergence, although the local sensitivity ranks remain visible.

The thin command-line adapter exposes the same service workflow for local and batch use:

```sh
thermox_cli calibrate \
  --model model.json \
  --calibration acceptance_fit \
  --max-iterations 20 \
  --format json
```

`--max-iterations` is an execution budget, not a convergence claim. A successful response can
therefore contain `diagnostics.converged=false` when the best feasible fitted model is returned
after the trust-region solver reaches that budget. Callers must retain the convergence
flag, objective history summary, and observation residuals when qualifying the result.

`EngineeringStudyRequest` formalizes that independent workflow. It runs one named calibration,
freezes the returned canonical model, executes each declared prediction case sequentially through
the ordinary steady or transient service selected by the case mode, and evaluates prediction
observations only after the solve. Transient observations carry `time_si`; their unique times are
passed to the adaptive integrator as exact required output boundaries. A case
referenced by calibration observations is rejected as a prediction case, preventing accidental
training/validation overlap. Changing a prediction measurement cannot change the predicted state.
The response preserves the complete calibration and per-case graph results while reporting
physical residuals, normalized residuals, weighted sums of squares, RMS normalized residual, and
the maximum absolute normalized residual.

For local, confidential, and CI validation campaigns the same service is available through a
versioned declaration. `model_document` is an ordinary `thermox.model/v2` document; calibration
observations remain inside that document, while held-out observations exist only in the study
request:

```json
{
  "schema_version": "thermox.engineering_study/v2",
  "model_document": {"schema_version": "thermox.model/v2", "model": {}, "cases": [], "calibrations": []},
  "calibration_id": "baseline_fit",
  "calibration_solver": {"max_iterations": 20},
  "steady_prediction_solver": {"max_iterations": 80},
  "transient_prediction_solver": {
    "initial_step": 0.01,
    "max_step": 0.5
  },
  "prediction_cases": [{
    "case_id": "held_out_transient",
    "observations": [{
      "id": "held_out_level_30s",
      "target": "drum.level.value",
      "dimension": "length",
      "measured_si": 0.42,
      "sigma_si": 0.005,
      "time_si": 30.0
    }]
  }]
}
```

```sh
thermox_cli study --input study.json --format json
```

The parser does not copy prediction observations into the model or optimizer. The service rejects
any prediction case used by a calibration observation, freezes the fitted canonical model, and
evaluates held-out residuals only after each prediction solve. The CLI is a thin adapter over this
service contract. `time_si` is required only when the referenced prediction case is transient.
Study results discriminate `steady_simulation` from `transient_simulation` and retain the complete
trajectory evidence. `sigma_si` is the declared normalization scale; callers must state whether it is
a traceable standard uncertainty, an acceptance tolerance, or an exploratory scale.

The current dense rank-revealing trust-region implementation is intended for small and moderate
engineering parameter sets. Sparse QR/SVD implementations can replace it behind the same residual
callback and evidence contracts without changing model, component, property, or observation
contracts. Transient estimation currently fits deterministic, time-point trajectory observations.
It does not yet implement process-noise models, sequential filtering, adjoint sensitivities,
event-time estimation, or trajectory warm starts across parameter candidates. Component models
remain unaware of calibration campaigns.

## Evidence classification

Calibration residuals demonstrate reconstruction of the observations used by the optimizer; they
are not independent validation. A result that holds measured boundaries fixed, calibrates internal
parameters, and compares a different measured output is a boundary-constrained check. It is
stronger than replaying the calibration observations, but it remains sensitive to the completeness
and measurement basis of those boundaries.

Predictive evidence requires cases excluded from calibration and should use
`EngineeringStudyRequest`, which enforces that separation. Off-design prediction additionally
requires the physical artifacts that govern movement between operating points, such as equipment
maps, control schedules, bleed/cooling topology, heat-loss models, and auxiliary-load definitions.
The platform must report a held-out discrepancy as missing evidence or missing physics; it must not
absorb it into an unowned plant-wide correction.

### Reduced-order discrepancy parameters

A component- or system-owned discrepancy parameter can be useful when a real test campaign does
not expose enough internal equipment data. For example, a shaft-train fixed loss can represent an
unresolved but locally stable difference between thermodynamic shaft work and the declared
generator boundary. Such a parameter is acceptable only when its ownership, units, bounds, fitted
cases, and limitations are explicit.

Agreement after fitting a discrepancy parameter supports only the operating envelope exercised by
held-out cases. It does not identify the parameter as a measured physical loss and must not be used
to hide missing cooling flows, auxiliary boundaries, maps, controls, or measurement-basis errors.
A useful qualification sequence is therefore: fit physical state parameters on training
observations, fit the minimum additional discrepancy term on training output, freeze both stages,
and evaluate separately declared prediction observations. A materially separated load or ambient
point is stronger evidence than several nearly identical repeat runs.
