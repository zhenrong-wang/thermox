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
If staging exhausts the minimum step or maximum stage count, the driver makes one explicit target
solve from the last accepted state. This protects callers from a homotopy-path failure when the
target equations are nevertheless directly solvable. Diagnostics label that fallback and retain
every rejected stage; the solve still fails if the target fallback also fails.

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

Fluid performance-map compressors and turbines also provide an informed path for ordinary and
conditioned maps. Compilation finds a corrected-flow/speed seed that is evaluable across adjacent
map curves (and adjacent geometry layers when present). Intermediate stages blend from that seed
to the actual corrected coordinates and from the anchor-implied pressure ratio to the selected
map output. The analytic pressure-ratio row includes both continuation chain-rule factors.

For maps whose declared extrapolation policy is `reject`, intermediate stages use a temporary
piecewise-linear extension of the immutable source surface. This removes the domain barrier while
the operating variables move toward the map. The extension exists only inside the informed path:
the original map and its original reject/clamp/linear policy are always evaluated at `lambda = 1`.
Consequently continuation cannot turn an out-of-domain final operating point into a successful
solution.

Fixed-duty heat exchangers stage each side from the duty implied by its initial mass flow and
enthalpy difference to the declared duty. Counterflow-UA exchangers stage the initial hot/cold
energy imbalance to conservation and the anchor-implied hot-side duty to `UA * LMTD`. During
intermediate stages, terminal temperature differences move from positive anchor-derived seeds to
the evaluated differences. This permits recovery from reversed outlet-temperature guesses without
weakening the target: `lambda = 1` still evaluates the original strict positive-terminal-difference
LMTD equation.

The adiabatic-equilibrium material combustor stages its pressure ratio from the anchor-implied
ratio, and stages outlet enthalpy and each product-species flow from the anchor values to the
thermochemistry backend result. Intermediate equilibrium calls project finite negative reactant
flow guesses to a vanishing positive floor and protect pressure with a positive floor while the
compiled boundary and connection equations move the operating state into its physical domain.
These projections are continuation-only: `lambda = 1` passes the actual pressure, enthalpy, and
reactant composition to the unmodified equilibrium backend.

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
`thermox.newton-continuation/v3`, persists the settings with run configurations and jobs, and
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

Composition-coupled material-map initialization and continuation for future finite-rate or
multi-zone reactor models remain component policies. They belong behind this same extension
contract; they must not become cycle-specific solver logic.
