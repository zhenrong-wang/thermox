# Thermox Implementation Roadmap

_Date: 2026-07-27_

This roadmap turns the research/design foundation into executable engineering work.

## Phase 0 — Architecture baseline

Deliverables:

- `docs/research-and-architecture.md`
- `docs/schema-design.md`
- `docs/roadmap.md`
- initial benchmark list and acceptance criteria

Exit criteria:

- Agreement on equation-oriented architecture.
- Agreement on model/case/result schema direction.
- Agreement that combined cycle is the first full target, with Rankine and Brayton cycles as stepping stones.

## Phase 1 — C++ steady-state core MVP

Goal: prove graph → compiled equation system → solve → results for small thermal cycles using the long-term production core architecture from day one.

Work items:

1. Create C++ core package under `core/` with CMake, unit tests, and a small CLI runner. ✅
2. Implement language-neutral schema loading/validation for media, components, ports, connections, and cases. ✅
3. Implement unit normalization to canonical SI. ✅ *(Generic schema scalars and Brayton example inputs now support canonical fields plus `{value, unit}` inputs.)*
4. Implement component registry and base C++ `ComponentModel` interface. ✅ *(The registry validates port contracts and compiles generic graph/connection structure; specialized physical residuals have started with the ideal-gas compressor and turbine.)*
5. Implement variable registry and residual registry. ✅
6. Implement sparse nonlinear system assembly with named residuals, explicit sparse Jacobian partials, bounds, and scaling metadata. ✅
7. Implement damped Newton solver with scaling, line search, finite-difference Jacobian fallback, and a sparse linear-solver abstraction. ✅
8. Integrate a sparse linear algebra backend behind the existing sparse solver hooks. ✅ *(The current backend is a built-in CSR sparse direct solver; Eigen/SuiteSparse-style external backends can be added behind the same interface later.)*
9. Implement basic property packages/adapters:
   - simple ideal gas in C++ ✅ *(used by the compiled compressor/turbine residual slices)*
   - CoolProp/IF97-style water/steam adapter when dependency policy is set
10. Implement minimal compiled components:
   - source/sink
   - pump
   - turbine ✅ *(ideal-gas isentropic-efficiency residuals: mass continuity, pressure ratio, inlet/outlet enthalpy, outlet temperature, and shaft power)*
   - compressor ✅ *(ideal-gas isentropic-efficiency residuals: mass continuity, pressure ratio, inlet/outlet enthalpy, outlet temperature, and shaft power)*
   - combustor simplified
   - heat exchanger simplified
   - condenser
   - mixer/splitter
11. Add examples:
   - simple Rankine cycle
   - simple Brayton cycle

Exit criteria:

- `scripts/verify.sh` configures/builds the C++ core and runs unit tests plus example solves.
- Rankine and Brayton examples converge from documented initial guesses.
- Solver returns structured diagnostics and result tables.

## Numerical foundation milestone

Goal: stabilize the cycle-independent numeric kernel before expanding the physics library. ✅

Delivered:

1. Dimensionless variable/residual scaling across Newton residuals, Jacobian rows, and Jacobian
   columns. ✅
2. Relative-pivot dense and CSR reference linear solvers. ✅
3. Fixed sparse patterns with value-only updates and hybrid analytic/finite-difference rows. ✅
4. Recoverable/fatal physics-evaluation status and bounds-aware line-search recovery. ✅
5. Duplicate-name validation and fixed-pattern structural matching. ✅
6. Index-1 DAE contract, consistent initial conditions, implicit adaptive integration, and events. ✅
7. Steady, transient, sanitizer, and example regression coverage. ✅

Production follow-ons:

- external sparse factorization with symbolic reuse;
- higher-order BDF/IDA-style integration backend;
- graph/block tearing, continuation, and richer rank/conditioning diagnostics.

## Phase 2 — Combined-cycle prototype

Goal: produce a credible single-pressure combined-cycle heat balance.

Work items:

1. Add HRSG component models:
   - economizer
   - evaporator/drum simplified
   - superheater
   - gas-side pressure drop
   - pinch/approach constraints
2. Add gas turbine off-design placeholders:
   - compressor/turbine maps
   - map interpolation
   - extrapolation policies
3. Add steam turbine stage/extraction support.
4. Add design/off-design case handling.
5. Add heat balance report generation.
6. Add benchmark regression cases.

Exit criteria:

- Single-pressure CCGT model converges.
- Heat and mass balance closure errors are below configured tolerance.
- Result report includes gross/net power, thermal efficiency, heat rate, HRSG pinch/approach, exhaust stack temperature, and major stream table.

## Phase 3 — Backend API and job service

Goal: expose the core through a production-shaped API.

Work items:

1. Create API service.
2. Endpoints:
   - `POST /models/validate`
   - `POST /models/compile`
   - `POST /simulations`
   - `GET /simulations/{id}`
   - `GET /simulations/{id}/results`
   - `GET /component-types`
3. Add job state model and run persistence.
4. Add structured logging and run IDs.
5. Add result serialization.

Exit criteria:

- API can validate and run Rankine/Brayton examples.
- Failed runs return actionable diagnostics.
- Model revision, case data, component versions, property backend versions, and solver options are recorded.

## Phase 4 — Frontend graph layer

Goal: let users build models visually and inspect results.

Work items:

1. Create React/TypeScript web app.
2. Implement graph canvas with typed ports.
3. Component palette generated from backend component registry.
4. Parameter forms with units and validation.
5. Case editor for fixed values and initial guesses.
6. Run monitor and result overlay on graph.
7. Stream/result table views.

Exit criteria:

- User can build a simple Rankine cycle in the UI, run it, and inspect stream results.
- Invalid connections are rejected before submission.

## Phase 5 — Production hardening

Goal: robustness, extensibility, and safe user customization.

Work items:

1. Analytic or automatic differentiation Jacobians for core components.
2. Homotopy/continuation initialization strategies.
3. Tearing/decomposition and better structural diagnostics.
4. Safe custom equation DSL; no arbitrary user Python in API process.
5. Map validation and interpolation quality controls.
6. Multi-user auth, authorization, project isolation, audit logs.
7. Scalable job workers and deployment automation.
8. Extended benchmark suite against published/reference examples.

Exit criteria:

- Large combined-cycle benchmark converges reliably.
- Regression suite catches numerical drift.
- User customization is sandboxed or constrained.
- System is deployable with documented security and operations practices.

## First recommended implementation sprint

Duration: 1–2 weeks.

Scope:

1. Scaffold C++ core package with CMake, tests, and `scripts/verify.sh`.
2. Implement minimal schema structs and JSON/YAML ingestion boundary.
3. Implement variable/residual registry and sparse pattern representation.
4. Implement damped Newton for small systems with dense fallback first, behind a sparse-solver abstraction.
5. Implement ideal-gas Brayton example without external property dependency.
6. Add README instructions and CLI output for diagnostics.

Why this first: it proves the most important architectural path without locking the project into a scripting runtime. The first solver may be minimal, but the ABI, data ownership, diagnostics, and build structure should already match the long-term C/C++ numerical core direction.
