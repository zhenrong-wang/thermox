# Performance Map Architecture

_Decision date: 2026-07-28_

## Boundary

Performance maps are immutable, validated engineering data used by registered components. They are
not embedded in the nonlinear solver and do not create a gas-turbine-specific execution path.

The separation is deliberate:

- a registered component **type** declares ports, scalar parameters, required artifact roles, and
  the equations that interpret those inputs;
- a model-document component **instance** supplies its medium bindings, scalar parameters, and
  artifact IDs;
- a simulation request carries project/run-scoped immutable artifact payloads or exact
  schema/revision/checksum-pinned references, while a native host may also install deployment-wide
  artifacts in its immutable runtime;
- the service builds a private overlay for compilation and never mutates or leaks data into another
  request.

Consequently, Thermox may ship a generic compressor model without embedding any manufacturer's
compressor. A user creates a real compressor instance by binding a particular performance-map
artifact and the remaining machine data required by that model. The same component type can be
instantiated repeatedly with different maps.

Component artifact bindings are generic:

```json
{
  "id": "gt1.compressor",
  "kind": "compressor.fluid.performance_map",
  "artifacts": {
    "performance_map": "gt1-compressor-oem-map"
  }
}
```

The component descriptor declares that `performance_map` expects artifact type
`thermox.performance_map`. Compilation rejects missing roles, unknown roles, and artifact IDs that
are absent from the effective request/runtime registry. A request cannot silently replace an
artifact with the same ID in the deployment runtime.

The transport-neutral `SimulationArtifactBundle` accepts v1 ordinary maps and v2 conditioned maps
using standard C++ DTOs, either inline or through `EngineeringArtifactReference`.
`EngineeringArtifactResolver` is an injectable service port; its in-memory adapter demonstrates
the contract for deployment-installed references and embedded callers. Production project
artifacts instead use immutable PostgreSQL revision metadata plus provider-neutral object content,
then become verified inline snapshots at job submission. Validation, steady, transient,
calibration, and queued-job execution all consume the same bundle. The service rejects missing
artifacts and any mismatch between the resolved type, schema, revision, or checksum.
Artifact identity is recorded in result provenance. Complete inline payloads and complete reference
identities participate in job idempotency fingerprints.

`thermox::platform::PerformanceMap` represents a two-coordinate family of piecewise-linear curves.
The individual curves may have different primary-coordinate samples, matching common compressor
and turbine speed-line data without forcing it onto a rectangular grid.

`thermox.performance_map/v2` adds a third, conditioned coordinate. Each strictly ordered condition
layer contains a complete v1-style non-rectangular map with identical axes, outputs, and
extrapolation policies. Evaluation resolves corrected flow and corrected speed in the two
neighboring layers, then interpolates their outputs and derivatives along the condition axis.

The Definition workspace exposes ordinary `thermox.performance_map/v1` artifacts as their own
Engineering Data Registry domain. The typed form declares both axes, any number of outputs,
non-rectangular family curves, and the two extrapolation policies. It previews the first output
across all family curves before publication. Creation and revision are explicit separate actions;
revision authoring retrieves and integrity-checks the exact immutable parent payload before
preloading the form. Conditioned `v2` maps remain supported by the platform and service runtime,
but their higher-dimensional authoring workflow is intentionally separate from the v1 editor.
For production-sized ordinary maps, the browser can import comma-, tab-, or semicolon-delimited
long-form tables and explicitly map columns to the family axis, primary axis, and declared outputs.
The import adapter handles quoted fields, rejects non-finite mapped values and duplicate operating
points, and normalizes rows into ordered non-rectangular curves locally. The service still receives
the same declaration JSON, so CSV is an interface concern rather than a platform-core format.

Every axis and output has a stable name and physical dimension. Evaluation returns:

- all interpolated outputs;
- piecewise-analytic derivatives with respect to both coordinates;
- explicit primary- and family-axis extrapolation flags.

## Extrapolation

Each axis independently selects one policy:

- `reject` — fail outside the declared data domain; this is the default;
- `clamp` — use the nearest boundary value and a zero derivative outside the domain;
- `linear` — continue the boundary segment and retain its derivative.

Components must not silently change these policies. A rejected evaluation will later map to the
numeric kernel's recoverable model-domain failure. Successful clamp or linear extrapolation must
remain visible in diagnostics and result provenance.

## Gas-turbine use

A typical compressor map will use corrected flow and corrected speed as coordinates, with pressure
ratio and efficiency as outputs. Those semantics belong to the compressor component and its data
descriptor, not to `PerformanceMap` itself. Turbines, pumps, fans, heat-exchanger correlations, and
other engineering components can use the same map kernel.

The registered fluid and composition-aware material performance-map
compressor/turbine models formalize the following map contract:

- primary axis: `corrected_mass_flow`, dimension `mass_flow`;
- family axis: `corrected_speed`, dimension `angular_speed`;
- outputs: `pressure_ratio` and `isentropic_efficiency`, both dimensionless.

Using component-instance reference pressure \(p_\mathrm{ref}\) and temperature
\(T_\mathrm{ref}\), the component evaluates:

```text
corrected_mass_flow = m_dot * sqrt(T_in / T_ref) / (p_in / p_ref)
corrected_speed     = omega / sqrt(T_in / T_ref)
```

Map-driven turbomachinery component version 2 adds three positive, dimensionless,
component-owned correction parameters. Each defaults to one:

```text
map_flow       = corrected_mass_flow / flow_capacity_scale
pressure_ratio = 1 + pressure_ratio_scale * (map_pressure_ratio - 1)
efficiency     = efficiency_scale * map_efficiency
```

The flow factor moves the operating point along the map's corrected-flow coordinate; it does not
alter the physical mass flow reported by the graph. The pressure-ratio factor scales pressure rise
or expansion ratio relative to unity, and the efficiency factor scales the selected map
efficiency. Corrected pressure ratio must remain greater than one and corrected efficiency must
remain in `(0, 1]`. The same convention applies to ordinary and variable-geometry fluid/material
compressors and turbines. Analytic pressure-ratio derivatives include the correction chain rule.

These values belong to the component instance and can therefore be bounded, calibrated on
designated baseline cases, frozen into the fitted canonical model, and evaluated on independent
off-design cases. The immutable source map is never rewritten by calibration.

The selected map pressure ratio and efficiency close outlet pressure, isentropic enthalpy change,
and shaft power using the appropriate compressor or turbine convention. Map-domain failures are
recoverable model evaluations, allowing the nonlinear solver to reject invalid trial states. The
pressure-ratio row uses map derivatives and the property package's shared PH-derivative contract
in sparse Jacobian assembly. Analytic-capable packages stay analytic through the full correction
chain; explicitly non-analytic packages use the centralized bounded fallback.

The registered `*.variable_geometry_map` variants bind a v2 artifact whose condition variable is
`geometry_setting` with dimension `angle`. The component parameter of the same name is normalized
to radians and may be overridden per operating case. This supports IGV angle, guide-vane position,
or blade pitch without changing graph topology or treating measured mass flow as a hidden map
input.

## Continuation initialization

Fluid map-driven compressors and turbines expose an anchor-aware continuation path. The compiler
selects a corrected-flow/speed coordinate inside the common domain of neighboring curves; for
conditioned maps it also selects a common adjacent-layer geometry coordinate. Intermediate solves
blend from this seed to the actual corrected component coordinates and preserve the analytic
pressure-ratio derivative chain.

A source map with `reject` boundaries receives a continuation-only piecewise-linear extension so
an out-of-domain initial guess can acquire a useful direction back toward the map. This does not
change the artifact or component's final extrapolation contract. At the exact target stage the
original map is evaluated, and an out-of-domain final operating point still fails. Composition-
coupled material map continuation remains separate work because species-flow constraints require
a composition-aware path rather than blindly reusing the fluid policy.

## Next integration slice

1. Define a versioned map-artifact schema and checksum identity. ✅
2. Resolve immutable deployment and request-scoped map artifacts through generic component
   artifact bindings during compilation. ✅
3. Add a map-based compressor component that lowers map outputs and derivatives into checked graph
   equations. ✅
4. Add the corresponding map-based turbine component with turbine pressure, efficiency, and shaft
   power conventions. ✅
5. Apply the same map contract to composition-aware material compressor and turbine ports. ✅
6. Add conditioned three-coordinate maps and variable-geometry fluid/material turbomachinery
   components. ✅
7. Add an injectable artifact resolver and immutable reference contract. ✅
8. Add persistent project artifact revisions with PostgreSQL metadata, provider-neutral object
   content, integrity-checked decoding, and immutable job snapshots. ✅
9. Calibrate a designated baseline point, then freeze parameters and predict independent
   off-design validation points. ✅
10. Add standardized component-owned flow-capacity, pressure-ratio, and efficiency map
    corrections across fluid/material and fixed/variable-geometry turbomachinery. ✅
