# Thermox Numeric Core Contracts

_Status: implemented foundation, 2026-07-30_

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

The builder also supports explicit linear initialization relations. Graph compilation treats case
fixed values and user initial guesses as immutable initialization anchors, then repeatedly
propagates an anchored value across connector equality relations when exactly one endpoint is not
yet informed. This initializes connected fluid, material-species, shaft, electrical, heat, signal,
and custom-domain variables without cycle-specific traversal or a dense auxiliary solve. Explicit
guesses are never overwritten. Component equations do not participate implicitly: a component
extension must deliberately publish a safe initialization relation so its continuation policy and
physical branch selection cannot be changed accidentally.

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

Finite-difference columns use second-order central perturbations whenever both bounded physical
states evaluate successfully. At a declared bound, or when one property evaluation reports a
recoverable domain failure, the same path falls back to the valid one-sided derivative. Fatal
physics failures remain fatal. The solver and analytic-Jacobian verifier share this implementation,
so derivative audits exercise the same bounded-domain behavior used during simulation.

Checked residual callbacks can report success, recoverable domain failure, or fatal failure.
Recoverable failures during line search cause damping instead of terminating the solve. This is
the intended path for property-domain and map-boundary failures.

`analyze_problem_structure` validates names and dimensions and performs bipartite structural
matching when a fixed Jacobian pattern is available.

`solve_continuation` provides an opt-in scaled residual homotopy for difficult initial guesses.
It adaptively advances from an anchored initial-state problem to the exact target residual,
warm-starting each stage and preserving dense, sparse, and hybrid derivative paths. Compiled
problems may provide anchor-aware parameterized residual and Jacobian callbacks, allowing
components to introduce their own difficult physics without adding domain knowledge to the
driver. Fixed fluid and material turbomachinery currently stage pressure ratio from the case
initialization to the declared target. Fluid performance-map machinery additionally stages
corrected coordinates from an in-domain seed while preserving the strict source-map policy at the
target stage. If an intermediate homotopy path exhausts its adaptive step, the driver attempts the
unchanged target once from the last accepted state and reports the fallback explicitly. See
[Continuation Architecture](continuation-architecture.md).

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

- fully implicit variable-step BDF1/BDF2 integration;
- backward-Euler startup and automatic BDF1 restart after an unsuccessful BDF2 trial;
- order-aware adaptive error control using one full step versus two half steps;
- dimensionally coherent weighting of each differential state by
  `absolute_tolerance * variable_scale + relative_tolerance * state_magnitude`;
- exclusion of algebraic readouts from truncation-error control while still solving their
  constraints at every implicit stage;
- bounded Newton solves at every implicit stage;
- rejected-step recovery;
- trajectory, nonlinear-work, accepted-error, and limiting-state diagnostics;
- rising, falling, and direction-independent events;
- terminal events with an interpolated terminal state.

The variable-order integrator is the dependency-free native backend. The DAE callback and metadata
contract remains independent of the stepper so an IDA-style backend with mature order 1-5 control
and Newton/Krylov facilities can be added without changing physics components.

## Sparse and linear-solver policy

The built-in dense and CSR direct solvers provide small-model capability and deterministic tests.
Their pivot tests are relative to matrix magnitude. They are not the intended large-plant
factorization backend.

When configured with `-DTHERMOX_ENABLE_UMFPACK=ON`, the default sparse solve uses SuiteSparse
UMFPACK with column-oriented sparse LU factorization. This backend is optional: standalone and
dependency-minimal builds retain the reference CSR solver. The nonlinear and physics contracts do
not change with the selected factorization backend.

`SparseFactorization` is the stateful factorization boundary. The UMFPACK implementation converts
the declared CSR structure to CSC once, retains the CSR-to-CSC value mapping, and caches symbolic
analysis while the pattern is unchanged. Each new Jacobian refreshes values and numeric LU factors.
Changing dimensions or sparsity invalidates the symbolic cache. The object serializes access, so a
shared instance cannot corrupt its cache if called concurrently.

`solve_newton` creates one default factorization per nonlinear solve. A caller may inject a shared
factorization through `SolverOptions`; the DAE integrator does this automatically for sparse
problems so consistent initialization and every full/half implicit stage share the same symbolic
analysis. Fixed-pattern DAE initialization now derives its mixed Jacobian directly from the DAE
callback: differential columns use `dF/d(y_dot)` and algebraic columns use `dF/dy`.

Diagnostics identify the selected linear backend and distinguish symbolic and numeric
factorization counts in native and service results.
For a fixed-pattern UMFPACK solve, one symbolic factorization and one numeric factorization per
Newton matrix are expected. The one-shot `solve_sparse_linear_system` and custom dense/sparse
hooks remain available for isolated calls and backend testing.

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
- The native transient backend supports BDF orders 1-2 and is intended for index-1 DAEs.
- Bounds use projected trial steps, not a full constrained optimization method.
- Structural matching requires a declared fixed sparse pattern.
- Structural incidence analysis classifies connected underdetermined, overdetermined, and
  well-determined regions; block ordering and solver tearing are not yet enabled.
- Component-informed homotopy paths, an optional IDA-class transient backend, and broader
  sparse-backend performance/conditioning diagnostics remain future integrations.
