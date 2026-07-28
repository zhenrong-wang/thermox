# Thermox Numeric Core Contracts

_Status: implemented foundation, 2026-07-27_

## Scope

The numeric core is independent of thermal-cycle physics. Physics code compiles components,
connections, property relations, and boundary conditions into one of two mathematical forms:

- steady nonlinear algebraic systems: `F(x) = 0`
- transient differential-algebraic systems: `F(t, y, y_dot) = 0`

Gas turbines, steam cycles, nuclear secondary systems, refrigeration systems, and other physical
domains reuse these contracts without adding domain concepts to the numeric module.

`EquationSystemBuilder` classifies linear rows as independent, redundant, or inconsistent using an
incrementally maintained sparse row basis. The builder does not silently remove rows; the platform
compiler uses this information only for generated connection equations at closed-loop boundaries.

## Steady nonlinear contract

`NonlinearProblem` carries names, initial values, physical bounds, variable/residual scales, and
residual/Jacobian callbacks. `solve_newton` solves in dimensionless coordinates:

```text
z = x / x_scale
r = F / residual_scale
J_scaled = diag(1 / residual_scale) J diag(x_scale)
```

Available derivative paths:

1. fixed `SparsePattern` plus value-only updates;
2. complete dynamic sparse triplets;
3. hybrid analytic rows plus finite differences for missing rows;
4. dense analytic Jacobian;
5. full finite-difference fallback.

Checked residual callbacks can report success, recoverable domain failure, or fatal failure.
Recoverable failures during line search cause damping instead of terminating the solve. This is
the intended path for property-domain and map-boundary failures.

`analyze_problem_structure` validates names and dimensions and performs bipartite structural
matching when a fixed Jacobian pattern is available.

## Transient DAE contract

`DaeProblem` identifies differential and algebraic variables and supplies:

```text
F(t, y, y_dot) = 0
```

An analytic DAE Jacobian callback assembles:

```text
dF/dy + alpha dF/d(y_dot)
```

where the time integrator supplies `alpha`. Both dense and sparse forms are supported, including a
fixed sparse pattern with value-only updates.

`DaeEquationSystemBuilder` is the assembly boundary above `DaeProblem`. It registers named
differential and algebraic variables, initial states and derivatives, independent state/derivative
scales, bounds, residual scales, checked equations, and sparse partials with respect to both `y` and
`y_dot`. It combines those partials into `dF/dy + alpha dF/d(y_dot)` and rejects non-square systems
before integration.

`make_consistent_initial_conditions` holds differential states fixed while solving their initial
derivatives, and solves algebraic states while holding their supplied derivatives fixed. This
targets index-1 systems.

`integrate_dae` currently provides:

- fully implicit backward Euler;
- adaptive error control using one full step versus two half steps;
- bounded Newton solves at every implicit stage;
- rejected-step recovery;
- trajectory and nonlinear-work diagnostics;
- rising, falling, and direction-independent events;
- terminal events with an interpolated terminal state.

The first-order integrator is the dependency-free reference backend. The DAE callback and metadata
contract is designed so a higher-order BDF or IDA-style backend can be added without changing
physics components.

## Sparse and linear-solver policy

The built-in dense and CSR direct solvers provide small-model capability and deterministic tests.
Their pivot tests are relative to matrix magnitude. They are not the intended large-plant
factorization backend.

When configured with `-DTHERMOX_ENABLE_UMFPACK=ON`, the default sparse solve uses SuiteSparse
UMFPACK with column-oriented sparse LU factorization. This backend is optional: standalone and
dependency-minimal builds retain the reference CSR solver. The nonlinear and physics contracts do
not change with the selected factorization backend.

Compiled models should declare fixed Jacobian patterns wherever possible. A production sparse
backend should reuse symbolic analysis/factorization metadata across Newton iterations and time
steps.

## Physics-layer responsibilities

The physics/model compiler remains responsible for:

- selecting state variables and residual equations;
- assigning physically meaningful scales and bounds;
- declaring stable Jacobian structure and derivatives;
- converting property failures into recoverable or fatal evaluation statuses;
- supplying consistent topology, conservation laws, and component equations;
- interpreting solved variables as engineering results.

The numeric core does not know about fluids, phases, turbines, reactors, or units.

## Current limits

- Newton systems must be square.
- The native transient backend is first-order and intended for index-1 DAEs.
- Bounds use projected trial steps, not a full constrained optimization method.
- Structural matching requires a declared fixed sparse pattern.
- Reusing symbolic sparse-factorization analysis across Newton iterations and adding a higher-order
  transient backend remain future integrations.
