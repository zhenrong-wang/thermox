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

For a structurally nonsingular incidence graph, the same analysis forms the directed dependency
graph induced by the equation-variable matching, collapses its strongly connected components, and
returns irreducible square blocks in deterministic dependency-first order. The graph compiler
retains this report before choosing a fixed or hybrid Jacobian representation, and validation
exposes every block plus the largest block size. This is analysis-only today: Newton still solves
the complete coupled system, while future block solves and tearing can consume the established
ordering without changing component equations.

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
factorization through `SolverOptions`. For structurally decomposed solves, an exact-pattern
resolver retains one factorization per distinct CSR structure. Equal block patterns share an
instance, while alternating unlike patterns do not evict one another's symbolic analysis. The
resolver cache and each factorization serialize access. Continuation and DAE orchestration install
this resolver automatically when block execution is enabled; monolithic sparse paths retain one
shared factorization across stages. An explicit factorization or custom one-shot solver remains
authoritative. Fixed-pattern DAE initialization derives its mixed Jacobian directly from the DAE
callback: differential columns use `dF/d(y_dot)` and algebraic columns use `dF/dy`.

Diagnostics identify the selected linear backend and distinguish symbolic and numeric
factorization counts in native and service results. Every backend result is also checked against
the exact dimensionless Newton system using the normalized backward error
`||A x - b||inf / (||A||inf ||x||inf + ||b||inf)`. A non-finite result or an error above the
configured linear tolerance rejects that Newton solve instead of allowing an inaccurate step to
contaminate nonlinear convergence. Diagnostics retain the last and worst accepted linear errors;
continuation and transient integration propagate the worst error across all internal solves.

For a structurally nonsingular fixed sparse pattern, the default `automatic` policy selects
dependency-ordered block execution only when the system is reducible, the provider exposes both
residual-row and fixed-CSR-value subset callbacks, and the compiler explicitly asserts that block
execution is root-equivalent to the monolithic formulation. The native equation builders make
that assertion only when every assembled equation is linear; sparsity alone cannot prove root
uniqueness for nonlinear thermal models. If an automatic block attempt nevertheless fails, Newton
retries the monolithic problem from the original initial state and retains the attempted work in
diagnostics. Callers may force `monolithic` or `blocks` for controlled comparisons and custom
providers. The solver restricts each irreducible block to its matched equations and variables,
retains the provider's exact sparse Jacobian values, and solves blocks serially in triangular
order. Upstream state is fixed while downstream blocks solve. A final full-model residual check is
mandatory, so an incomplete declared dependency pattern cannot produce a false convergence.
Diagnostics report the number of block solves, the largest linear system actually factorized, and
the first failed block. A single irreducible block automatically stays on the monolithic path.
`NonlinearProblem` also exposes optional residual-row and fixed-CSR-value subset callbacks. The
steady and DAE equation-system builders publish these callbacks automatically from their
per-equation functions, so native compiled graphs evaluate only the equations owned by each block.
Custom providers may omit them; automatic policy then stays monolithic, while forced block mode
retains the correct full-callback fallback.
Steady diagnostics also report the largest absolute normalized residual and its registered
equation name at the returned state. Transient diagnostics retain the largest such residual among
converged consistent-initialization and implicit-stage solves. These values make the conservation,
property, or closure equation limiting numerical acceptance explicit instead of hiding it inside
one aggregate norm.
For a fixed-pattern UMFPACK solve, one symbolic factorization per distinct retained pattern and one
numeric factorization per Newton matrix are expected. The one-shot `solve_sparse_linear_system`
and custom dense/sparse hooks remain available for isolated calls and backend testing.

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
  well-determined regions and exposes dependency-ordered irreducible blocks. Fixed-pattern
  dependency-ordered execution is opt-in; automatic tearing inside an irreducible block is not yet
  enabled.
- Custom problems without row-selective callbacks invoke their full residual and fixed-pattern
  value callbacks before restricting the returned rows. Informed-continuation Jacobian transforms
  also currently use that fallback. Block mode therefore does not guarantee a speedup for every
  provider or continuation policy.
- Component-informed homotopy paths, an optional IDA-class transient backend, and broader
  sparse-backend performance/conditioning diagnostics remain future integrations.
