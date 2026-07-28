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

The next service slice will execute bounded, multi-case estimation by repeatedly invoking the
ordinary simulation workflow. The numeric solver and component models will remain unaware of
calibration campaigns.
