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
```

A `component`-scoped calibration parameter has exactly one target. A `system`-scoped parameter may
represent a plant-wide or shared estimate and may bind multiple dimensionally compatible physical
targets with the same initial value. Scope controls estimation and sharing; it does not move the
parameter out of its physical owner.

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

Uncertainty is part of the contract so estimation can weight unlike measurements without treating
all field values as exact.

## Validation boundary

Model-document parsing currently validates:

- unique calibration, parameter, and observation IDs;
- supported component/system scopes;
- target existence and dimensional compatibility;
- consistent initial values for shared targets and initial values inside calibration bounds;
- case references;
- ordered bounds;
- positive prior and observation uncertainties;
- measured-value and uncertainty dimensions.

Service validation additionally resolves observation paths against the active component registry
and checks that measured dimensions match the exposed primary or derived result. Fluid and
composition-aware material ports both expose derived temperature, so measured gas-path
temperatures can participate in gas-turbine calibration without becoming connector unknowns.

The service executes bounded, multi-case estimation by repeatedly invoking the ordinary steady
simulation workflow. Its dependency-free reference optimizer is a deterministic coordinate search:
candidate solve failures are rejected, every observation is weighted by its declared uncertainty,
and priors contribute normalized penalty residuals. Evaluations and case solves remain sequential
to bound host resource use.

Each accepted case solution is retained as a named-variable warm start. A candidate parameter move
first attempts the requested endpoint; on failure, adaptive continuation halves the move and walks
through converged neighboring states before retrying the endpoint. Continuation controls and all
underlying Newton settings are recorded in execution provenance.

The response includes initial/fitted values, bounds, per-observation physical and normalized
residuals, objective diagnostics, complete execution settings, and a canonical fitted model that
can be submitted directly for independent validation runs.

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
after the bounded coordinate search reaches that budget. Callers must retain the convergence
flag, objective history summary, and observation residuals when qualifying the result.

`EngineeringStudyRequest` formalizes that independent workflow. It runs one named calibration,
freezes the returned canonical model, executes each declared prediction case sequentially through
the ordinary steady service, and evaluates prediction observations only after the solve. A case
referenced by calibration observations is rejected as a prediction case, preventing accidental
training/validation overlap. Changing a prediction measurement cannot change the predicted state.
The response preserves the complete calibration and per-case graph results while reporting
physical residuals, normalized residuals, weighted sums of squares, RMS normalized residual, and
the maximum absolute normalized residual.

Coordinate search is intended for a small number of engineering calibration parameters. A later
optimizer interface can add trust-region least squares and covariance/identifiability analysis
without changing model, component, property, or observation contracts. Transient estimation is
also a later workflow; the first implementation intentionally accepts steady cases only. The
numeric solver and component models remain unaware of calibration campaigns.

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
