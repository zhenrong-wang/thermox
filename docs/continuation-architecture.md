# Continuation Architecture

## Purpose

Thermox continuation is a cycle-independent initialization strategy for square steady nonlinear
systems. It does not know about compressors, reactors, Rankine cycles, fluids, or component
parameters. It wraps the compiled numeric problem and progressively moves from a trivial anchored
system to the real residual equations.

For target or model-informed residuals `F(x, x0, lambda)`, initial state `x0`, variable scales
`sx`, residual scales `sF`, and continuation parameter `lambda`, the staged residual is:

```text
H_i(x, lambda) =
    lambda F_i(x, x0, lambda)
    + (1 - lambda) sF_i (x_i - x0_i) / sx_i
```

At `lambda = 0`, `x0` is an exact solution. At `lambda = 1`, `H` is exactly the target problem.
Without an informed callback, `F(x, x0, lambda) = F(x)` and this reduces to ordinary residual
homotopy. Intermediate solutions warm-start the next stage.

## Adaptive path

`solve_continuation` starts with a configured step in `lambda`. A converged stage advances the
path and grows the next step. A failed stage retains the last accepted state and reduces the step.
The solve fails explicitly if the reduced step falls below its minimum or the maximum stage count
is reached.

Every attempt records:

- start and target continuation parameters;
- acceptance or rejection;
- nonlinear iterations and final residual norm;
- the underlying Newton diagnostic.

Aggregate diagnostics retain all function, Jacobian, linear-solve, symbolic-factorization, and
numeric-factorization work. Fixed sparse target patterns are extended once with the anchor
diagonal, and the same factorization object is shared across stages. `used_informed_path` records
whether the compiled problem supplied model-aware callbacks; it is not inferred from component
names.

## Component extension contract

`NonlinearProblem` may carry parameterized residual and dense, sparse, fixed-pattern, or hybrid
Jacobian callbacks. Each receives the current state, the immutable initial anchor, and `lambda`.
At `lambda = 1` it must reproduce the ordinary target equation exactly. If an informed residual
does not provide a matching derivative path, continuation safely falls back to finite
differences rather than applying an inconsistent target Jacobian.

`EquationSystemBuilder` exposes checked, sparse, and target-linear continuation equation methods.
Native or built-in `ComponentModel` implementations use those methods while compiling their
ordinary equations; the service, topology document, and numerical driver do not require
component-specific branches. Target-linear continuation equations retain their linear-dependence
metadata, so graph degree-of-freedom reduction is unchanged.

The first built-in policy covers fixed-efficiency fluid and material compressors and turbines.
Their staged pressure ratio begins at the ratio implied by the case initial state and ends at the
declared component ratio. This is deliberately anchor-aware: a good user or OEM initial guess is
never moved back to an arbitrary unity-ratio state.

## Derivative preservation

The wrapper preserves the target derivative path:

- fixed sparse value callbacks receive a stable union of the target pattern and anchor diagonal;
- dynamic sparse and hybrid analytic rows receive the scaled target entries and applicable anchor
  diagonal entries;
- dense analytic Jacobians are blended directly;
- problems without analytic derivatives continue to use bounded finite differences.

Scaling is essential: the anchor equations use existing variable and residual scales, so the
homotopy does not add quantities with incompatible numerical magnitudes.

## Platform contract

Continuation is an opt-in member of `SteadySolverSettings`. The service publishes it as
`thermox.newton-continuation/v1`, persists the settings with run configurations and jobs, and
returns structured stage diagnostics. HTTP, CLI, and web interfaces only select settings and
render results; they do not own continuation logic.

The CLI shorthand is:

```sh
./build/thermox_cli solve \
  --model core/examples/air_compressor.json \
  --case design \
  --continuation \
  --format json
```

## Boundaries

The generic fallback remains residual homotopy, and a component hook does not guarantee that every
nonlinear branch connects smoothly from the anchor to the desired physical solution. Components
without a hook must still be evaluable at the first positive continuation stage.

Performance-map coordinate seeding, heat-duty staging, and reaction/equilibrium introduction are
the next component policies. They belong behind this same extension contract; they must not become
cycle-specific solver logic.
