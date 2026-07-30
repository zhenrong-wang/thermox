# Continuation Architecture

## Purpose

Thermox continuation is a cycle-independent initialization strategy for square steady nonlinear
systems. It does not know about compressors, reactors, Rankine cycles, fluids, or component
parameters. It wraps the compiled numeric problem and progressively moves from a trivial anchored
system to the real residual equations.

For target residuals `F(x)`, initial state `x0`, variable scales `sx`, residual scales `sF`, and
continuation parameter `lambda`, the staged residual is:

```text
H_i(x, lambda) =
    lambda F_i(x)
    + (1 - lambda) sF_i (x_i - x0_i) / sx_i
```

At `lambda = 0`, `x0` is an exact solution. At `lambda = 1`, `H` is exactly the target problem.
Intermediate solutions warm-start the next stage.

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
diagonal, and the same factorization object is shared across stages.

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

This is residual homotopy, not component-parameter homotopy. It cannot make an unevaluable initial
state valid: property, map, or chemistry callbacks must be able to evaluate the first positive
continuation stage. It also does not guarantee that every nonlinear branch connects smoothly from
the anchor to the desired physical solution.

Future component-level homotopy hooks may provide physically informed paths—such as gradually
introducing pressure ratio, heat duty, reaction equilibrium, or map coupling—while continuing to
use the same adaptive continuation driver and diagnostics.
