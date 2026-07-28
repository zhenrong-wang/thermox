# Performance Map Architecture

_Decision date: 2026-07-28_

## Boundary

Performance maps are immutable, validated engineering data used by registered components. They are
not embedded in the nonlinear solver and do not create a gas-turbine-specific execution path.

`thermox::platform::PerformanceMap` represents a two-coordinate family of piecewise-linear curves.
The individual curves may have different primary-coordinate samples, matching common compressor
and turbine speed-line data without forcing it onto a rectangular grid.

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

## Next integration slice

1. Define a versioned map-artifact schema and checksum identity.
2. Register immutable map artifacts in the application runtime.
3. Add a map-based compressor component that lowers map outputs and analytic derivatives into
   checked graph equations.
4. Calibrate a designated baseline point, then freeze parameters and predict independent
   off-design validation points.
