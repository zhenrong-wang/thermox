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
3. Implement unit normalization to canonical SI. ✅ *(Generic schema scalars support canonical
   fields plus `{value, unit}` inputs.)*
4. Implement component registry and base C++ `ComponentModel` interface. ✅ *(The registry validates port contracts, resolves medium property packages, and compiles generic graph/connection structure with ideal-gas and real-fluid compressor/turbine residuals.)*
5. Implement variable registry and residual registry. ✅
6. Implement sparse nonlinear system assembly with named residuals, explicit sparse Jacobian partials, bounds, and scaling metadata. ✅
7. Implement damped Newton solver with scaling, line search, finite-difference Jacobian fallback, and a sparse linear-solver abstraction. ✅
8. Integrate a sparse linear algebra backend behind the existing sparse solver hooks. ✅ *(The current backend is a built-in CSR sparse direct solver; Eigen/SuiteSparse-style external backends can be added behind the same interface later.)*
9. Implement basic property packages/adapters:
   - unified SI-unit C++ property-package contract ✅
   - simple ideal gas in C++ ✅ *(used by the compiled compressor/turbine residual slices)*
   - CO2 Span-Wagner adapter with transport properties ✅
   - water/steam IF97 adapter with transport properties ✅
   - steady Newton and transient DAE integration tests for property calls ✅
   - schema-selected property registry and checked component evaluation path ✅
   - compile-time component/backend capability validation ✅
10. Implement minimal compiled components:
   - source/sink
   - pump ✅ *(property-aware PH/PS isentropic-efficiency model, including IF97 regression)*
   - turbine ✅ *(property-aware mass continuity, pressure ratio, isentropic enthalpy change, and
     shaft-power residuals; fixed-fluid and composition-aware material variants are available)*
   - compressor ✅ *(property-aware mass continuity, pressure ratio, isentropic enthalpy change,
     and shaft-power residuals; fixed-fluid and composition-aware material variants are
     available)*
   - combustor ✅ *(backend-agnostic adiabatic equilibrium model with composition-aware air,
     fuel, and exhaust material ports)*
   - heat exchanger simplified
   - condenser
   - mixer/splitter ✅ *(two-inlet enthalpy-flow mixer and two-outlet splitter)*
11. Add examples:
   - simple Rankine cycle
   - simple Brayton cycle ✅ *(public Cantera-backed material/shaft/electrical platform graph)*

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
   - bounded second-order central finite differences with recoverable one-sided physical-domain
     fallback, shared by Newton assembly and analytic-Jacobian verification. ✅
4. Recoverable/fatal physics-evaluation status and bounds-aware line-search recovery. ✅
   - named maximum normalized equation residuals for returned steady states and converged
     transient implicit stages, propagated through service and UI diagnostics. ✅
5. Duplicate-name validation and fixed-pattern structural matching. ✅
6. Index-1 DAE contract, consistent initial conditions, implicit adaptive integration, and events. ✅
   - per-differential-state absolute error weighting through declared physical scales, retained
     relative control, algebraic constraint exclusion, and limiting-state/error diagnostics. ✅
7. Steady, transient, sanitizer, platform, and isolated example regression coverage. ✅
8. Reusable UMFPACK factorization lifecycle with fixed-pattern symbolic reuse across steady Newton
   iterations, consistent DAE initialization, and transient stages. ✅

Production follow-ons:

- optional IDA-class integration backend with mature order 1-5 and Newton/Krylov support; the
  dependency-free native backend now provides adaptive variable-step BDF1/BDF2 with safe order
  restart;
- graph/block tearing and richer rank/conditioning diagnostics.

Runtime extensibility now includes the safe `thermox.expression_component/v3` contract for
index-1 transient residuals, internal algebraic/differential states, state rates, time, bounds,
scales, and sparse analytic DAE Jacobians. Constrained property functions, thermochemistry,
species-expanded variables, and discrete events remain follow-ons.

## System-agnostic transient platform milestone

Goal: bridge registered dynamic components into the cycle-independent DAE kernel. ✅

Delivered:

1. Generic `DaeEquationSystemBuilder` with state and derivative partials, scaling, bounds, checked
   evaluation, square-system validation, and fixed sparse Jacobian assembly. ✅
2. Component metadata for steady/transient support, dynamic port variables, and internal
   differential/algebraic variables. ✅
3. Generic transient graph compiler for topology equations, fixed algebraic boundaries, initial
   states, component accumulation equations, and property capability validation. ✅
4. Registered lumped thermal-storage component and end-to-end transient CLI example. ✅
5. Consistent initialization, adaptive integration, release, sanitizer, and CLI regression
   coverage. ✅

## Reusable dynamic equipment primitives

Goal: make stored wall energy, rotating inertia, and normalized control dynamics available as
ordinary registered graph components. ✅

Delivered:

1. Two-sided lumped wall with steady heat balance and transient thermal-capacity accumulation. ✅
2. Two-port shaft inertia with kinetic-energy state, speed closure, mechanical efficiency, and
   explicit fixed loss. ✅
3. Normalized proportional controller and first-order control lag with steady/transient forms. ✅
4. Shaft, signal, and control source/sink boundaries for independently composable graphs. ✅
5. Registry-owned time and moment-of-inertia units plus fixed-sparse graph-level integration
   regressions. ✅

## Thermofluid connector foundation

Goal: establish non-redundant, property-independent stream semantics before expanding the
component library. ✅

Delivered:

1. Fluid links carry primary conserved variables `m_dot`, `p`, and `h`; thermodynamic quantities
   are derived results, while natural `(p,T)` boundaries compile to PH property equations without
   adding a temperature unknown. ✅
2. Property-aware turbomachinery uses PH/PS closure for ideal gas and real fluids. ✅
3. Platform result evaluation reconstructs temperature, density, entropy, phase, and quality for
   every solved fluid port. ✅
4. Compile-time degree-of-freedom diagnostics reject under- and over-specified graphs before
   Newton and report incidence-matched variable/equation candidates. ✅
5. Physical IF97 pump plus generic two-inlet mixer and two-outlet splitter establish the first
   reusable fluid-network component set. ✅

## Reusable transport and inventory components

Goal: establish the first steady transport elements and a property-backed dynamic fluid
inventory without introducing cycle-specific assumptions. ✅

Delivered:

1. Isenthalpic pressure-ratio valve with mass, pressure, and enthalpy conservation equations. ✅
2. Two-stream fixed-duty heat exchanger with independent hot/cold media and configurable
   fractional pressure losses. ✅
3. Transient rigid adiabatic fluid volume with mass and total-energy accumulation plus PH
   density/internal-energy closure. ✅
4. Transient fluid boundaries and fixed sparse DAE assembly for a connected source-volume-sink
   network. ✅
5. SI normalization for volume, mass, and energy component/case values. ✅
6. Generic single-phase Darcy-Weisbach pipe models with laminar/transitional/turbulent friction,
   roughness, local losses, elevation head, registered transport properties, and an optional
   explicit ambient heat boundary. ✅
7. Generic flow-area restrictions for non-flashing liquids and perfect gases, including exact
   critical-pressure-ratio choking and an explicit saturation-boundary rejection for liquid
   flashing. ✅

## Heat transfer and phase-change components

Goal: add property-backed heat-transfer equipment and prove a steam-cycle graph without embedding
Rankine-specific behavior in the platform. ✅

Delivered:

1. Counterflow two-stream heat exchanger using PH-derived terminal temperatures, UA/LMTD heat
   transfer, energy conservation, and independent side pressure losses/media. ✅
2. Generic quality-target evaporator and condenser with typed heat ports, positive duty
   magnitudes, pressure loss, and IF97 two-phase PH closure. ✅
3. Registered steady/transient heat sink boundary and thermal-conductance unit normalization. ✅
4. Simple closed-loop IF97 pump–evaporator–turbine–condenser Rankine graph with power, heat,
   pressure, quality, efficiency, and exact stream-closure regressions. ✅
5. CLI and clean-build regression coverage for the Rankine model document. ✅
6. Composition-aware material-to-fluid fixed-duty, design-point energy-balance, and counterflow
   UA exchangers with independent pressure losses and thermochemistry/property contracts. ✅
7. Property-backed equilibrium flash separator with saturated liquid/vapor outlets, pressure
   loss, saturation-dome validation, and exact lever-rule mass/energy closure. ✅
8. Dynamic rigid two-phase equilibrium drum with differential mass/internal-energy inventory,
   algebraic pressure/quality/level closure, phase-specific outlets, and an explicit heat port. ✅
9. Normalized drum level measurement plus a steady/transient actuated non-flashing liquid valve
   that composes with registered controller and actuator-lag blocks. ✅
10. Transient bounded normalized PI controller with explicit setpoint/measurement ports, command
    saturation, integral state, and tracking back-calculation anti-windup. ✅
11. Declared closed-loop dynamic reference joining property-backed fluid flow, two-phase inventory,
    level sensing, bounded PI control, actuator dynamics, and consistent DAE initialization. ✅
12. Generic steady/transient mixed heat-exchanger cell with independent hot/cold property packages,
    constant fluid-energy holdups, wall capacitance, and flow-dependent pressure loss. ✅
13. Declared two-cell counterflow gas-to-water exchanger proving distributed cell composition,
    cumulative pressure loss, steady duty closure, and transient whole-system energy closure. ✅
14. Composition-aware steady/transient material-to-fluid cell coupling Cantera exhaust to an IF97
    fluid inventory, with species conservation, wall storage, pressure loss, and DAE energy
    closure. ✅
15. Declared two-cell counterflow Cantera-exhaust/IF97-water exchanger proving mixed-domain spatial
    composition, exact species transport, cumulative pressure loss, and steady/transient energy
    closure. ✅
16. Transient material-to-fluid equilibrium two-phase cell with rigid-volume mass/energy storage,
    pressure-quality saturation closure, wall capacitance, and graph-audited conservation. ✅
17. Homogeneous-equilibrium two-phase hydraulic impedance plus a declaration-only dynamic
    single-pressure HRSG combining economizer, evaporator, separator drum, and superheater with
    whole-system mass/energy and exhaust-species audits. ✅
18. Generic hydraulic-inertance momentum storage, quasi-steady fixed-efficiency fluid
    turbomachinery and junctions in transient graphs, plus a declaration-only forced-circulation
    drum/evaporator loop with hydraulic, phase, species, mass, and energy audits. ✅
19. Signed homogeneous-equilibrium gravity/local-loss transport plus a declaration-only pump-free
    natural-circulation drum/evaporator loop with density-head, reverse-flow contract, momentum,
    phase, species, mass, and energy verification. ✅
20. Constant-slip two-phase gravity/local-loss transport plus a declaration-only distributed
    two-cell boiling riser with independent inventory states, inter-cell flow, phase, species,
    mass, and energy verification. ✅
21. Correlation-driven two-phase void-fraction transport using immutable engineering artifacts,
    dimension-checked live phase/flow/geometry inputs, physical output guards, and shared
    steady/transient component contracts. ✅
22. General rigid PH inventory with optional heat transfer, differential-state-only adaptive error
    control, and an end-to-end IF97 two-phase-to-vapor transition regression. Bidirectional
    liquid/two-phase traversal remains an explicit follow-up. ✅
23. Registered HEOS water/steam backend with analytic single-phase and bounded two-phase PH
    derivatives, plus four conserved bidirectional liquid/two-phase/vapor transition regressions.
    ✅
24. Correlation-driven transient two-phase inventory separating thermodynamic holdup quality,
    observable void fraction, and transported outlet quality while retaining exact mass and
    internal-energy accumulation. ✅
25. Immutable correlation applicability envelopes with typed per-input SI limits, inclusive or
    exclusive bounds, publication validation, durable execution persistence, authoring support,
    and explicit component-level out-of-range diagnostics. ✅
26. Unified correlation artifacts with named regime candidates, deterministic applicability/priority
    selection, explicit coverage-gap and ambiguity diagnostics, mass-flux inputs for two-phase
    consumers, immutable project publication, and durable job persistence. ✅
27. Correlation-driven two-phase pipe pressure drop with independent void-fraction and distributed
    friction-gradient artifacts, typed phase/flow/geometry inputs, signed reverse-flow assembly,
    friction zero-flow handling, and shared steady/transient compilation. ✅
28. Constant-area separated-flow acceleration pressure drop from independently evaluated inlet and
    outlet momentum flux, using property-backed endpoint qualities/densities and the bound
    void-fraction closure, with forward/reverse and steady/transient verification. ✅
29. Referenced Lockhart–Martinelli/Chisholm smooth-pipe friction templates for all four phase
    Reynolds-number combinations, plus generic compatible-template family instantiation,
    deterministic regime boundaries, immutable constants, and connected pipe verification. ✅

## Component catalog and modularization foundation

Goal: make registered component contracts authoritative and begin separating the component library
from graph compilation. ✅

Delivered:

1. Public parameter descriptors covering SI dimension, requiredness, defaults, lower/upper bounds,
   and inclusive/exclusive bound semantics. ✅
2. Registration-time descriptor validation and steady/transient compile-time value validation for
   missing, unknown, dimensionally incompatible, non-finite, and out-of-range parameters. ✅
3. Queryable, kind-ordered component descriptor catalog for future API and frontend consumers. ✅
4. Catalog-owned optional defaults consumed directly by physical models. ✅
5. Separate boundary and storage component-family registrar modules plus a dedicated catalog
   validation translation unit, establishing the split pattern for remaining families. ✅
6. Removal of obsolete component-level ideal-gas `cp` and `gamma`; property packages remain the
   sole fluid-property authority. ✅
7. Independent turbomachinery registrar for compressor, pump, and turbine variants, preserving
   shared PH/PS equation behavior across ideal-gas, IF97, and CO₂ backends. ✅
8. Independent transport registrar for mixer, splitter, and isenthalpic valve models. ✅
9. Shared internal component-model support utilities plus an exact 18-kind catalog-equivalence
   regression. ✅
10. Independent heat-transfer/phase-change and rigid fluid-inventory registrars; the central
    registry source now contains registry mechanics and graph compilation rather than concrete
    physical models. ✅
11. Explicit saturation-pair capability in the C and C++ property contracts, implemented for CO₂
    and IF97, with exact saturated-liquid/vapor phase-change targets. ✅
12. Incremental linear-relation analysis plus deterministic reduction of compiler-generated
    connection rows that are proven redundant; the Rankine example now runs as a true closed
    loop. ✅

Completed structural slices:

- versioned validation, steady-simulation, and transient-simulation service workflows; ✅
- transport-neutral command, result, error, diagnostics, and provenance contracts; ✅
- canonical model/result JSON serialization owned by the service; ✅
- thin CLI adapter with no compiler, property, or solver orchestration; ✅
- immutable injectable simulation runtime and deterministic catalog fingerprint; ✅
- versioned component/property/connector catalog discovery contract; ✅
- compile-aware steady/transient validation summary and structured diagnostic envelope; ✅
- breaking `thermox.model/v2` schema with registry-derived ports and fluid-only medium bindings; ✅
- domain-compatible connection kinds, direction checks, and explicit port cardinality; ✅
- optional exact component/property/connector version pins plus complete execution
  provenance, including the platform build and effective solver settings; ✅
- graph-native `thermox.result/v3` steady/transient/event values for every connector domain,
  fluid-derived properties, internal states, and derivatives; ✅
- registered source/sink semantics plus topology-aware steady system-boundary mass and energy
  accounting across fluid, material, heat, shaft, and electrical domains; ✅
- generic steady component net-flow metrics that attribute mass closure and energy conversion
  losses through registered port directions; ✅
- transport-neutral `thermox.job/v16` lifecycle with Team-scoped idempotency, leased worker claims,
  optimistic revisions, terminal publication, and checksummed result artifacts; ✅
- optional PostgreSQL job-metadata adapter with Team-scoped uniqueness, durable immutable requests,
  `SKIP LOCKED` claims, optimistic terminal transitions, migration, Compose service, and isolated
  integration contract coverage; ✅
- renewable worker leases, attempt counters, fencing revisions, atomic expired-job requeue,
  bounded-attempt failure, PostgreSQL migration, and hard-restart recovery verification; ✅
- provider-neutral object-storage port, SHA-256 result adapter, S3-compatible Signature V4 driver,
  pinned local MinIO Compose stack, restart-safe HTTP composition, and live integration coverage;
  ✅
- separate durable API and calculation-worker process roles, request-process execution guard,
  graceful worker stop, and bounded numerical-library thread defaults; ✅
- Team-owned logical projects, trusted per-Team membership roles, immutable parent-linked
  canonical model revisions, SHA-256 integrity metadata, PostgreSQL composite tenant constraints,
  and thin project/revision HTTP routes; ✅
- independent `thermox.topology/v1` and `thermox.case/v1` persistence contracts, immutable
  model-bound case history, exact topology/case composition into the established compiler input,
  PostgreSQL migration, and thin case-revision HTTP routes; ✅
- production revision-backed simulation submission with Team-scoped source resolution, immutable
  composed job snapshots, and project/topology/case IDs plus both SHA-256 source identities in job
  and result provenance; ✅
- Team/project-scoped immutable engineering-artifact revisions, PostgreSQL metadata and parent
  constraints, provider-neutral content-addressed object payloads, exact revision selection,
  verified inline job snapshots, and artifact revision/checksum execution provenance; ✅
- immutable Study revisions binding exact topology, case, artifacts, intent, and outputs, plus
  run-configuration v3 history binding one Study to complete steady/transient solver policy;
  normalized PostgreSQL constraints, thin HTTP routes, run-backed job submission, and checksum
  provenance; ✅

## Persistence boundary decision

Goal: place future database work without coupling persistence to simulation internals. ✅

Decision:

1. Numeric, physics, component, and compiler libraries remain database-free. ✅
2. The service layer owns application workflows; its next slice owns repository interfaces and
   job transactions. ✅
3. PostgreSQL stores projects, immutable revisions, run metadata, provenance, diagnostics, result
   manifests, and searchable summaries. ✅
4. Large trajectories and reports use checksummed object storage rather than database blobs. ✅
5. Database implementation starts after versioned serialization, application workflows, job
   states, and provenance contracts are stable. ✅

See `docs/persistence-architecture.md`.

## Calibration and validation foundation

Goal: support component-owned and system-shared calibration without hiding physical ownership or
coupling estimation to component equations.

Delivered:

1. Top-level calibration campaigns remain separate from model topology and operating cases. ✅
2. Typed adjustable parameters reference explicit component/connection parameter owners. ✅
3. Component-local and system-shared scopes coexist, including multi-case sharing. ✅
4. SI-normalized bounds, priors, measured observations, and uncertainty validation. ✅
5. Canonical service serialization preserves calibration contracts. ✅

Next:

1. Resolve observation targets against registered graph result variables and dimensions. ✅
2. Derive thermodynamic results, including temperature, for composition-aware material ports. ✅
3. Add an estimation service command that evaluates bounded multi-case objectives through the
   ordinary simulation workflow. ✅
4. Report fitted parameters, per-observation residuals, fitted canonical models, and complete
   provenance. ✅
5. Add covariance and identifiability diagnostics behind a replaceable least-squares optimizer.
6. Add topology-aware/continuation initialization for large reacting graphs so calibration
   candidates begin from component-consistent neighboring states. ✅ *(connector-domain
   propagation now removes duplicate port guesses; component-owned nonlinear seed policies remain
   explicit continuation hooks)*
7. Calibrate only designated baseline cases, freeze the fitted canonical model, and predict
   independent cases through a leakage-guarded engineering-study service. ✅
8. Add composition-preserving material split/mix routing for compressor bleed and cooling-air
   reinjection without introducing gas-turbine-specific graph semantics. ✅
9. Extend performance-map compressor/turbine contracts to composition-aware material ports so
   reacting gas paths can run frozen-parameter off-design predictions. ✅
10. Add dimensioned, bounds-checked per-case component parameter overrides so controls and
    operating configuration can vary while topology and calibrated global parameters remain
    shared. ✅
11. Add conditioned three-coordinate performance-map artifacts and variable-geometry
    fluid/material compressor and turbine components for case-selected IGV/guide-vane angles. ✅
12. Standardize component-owned flow-capacity, pressure-ratio, and efficiency map corrections,
    and prove ordinary calibration can fit and freeze them for independent prediction. ✅
13. Add species-keyed parameter templates and a fixed-composition material source that leaves
    total flow available for downstream map/pressure closure. ✅
14. Separate weighted parameter calibration from square hard-equality data reconciliation; expose
    case-owned adjustable boundaries, bounded Newton sensitivities, strict equality convergence,
    and semantically separate inferred, constrained, and held-out results. ✅
15. Add overdetermined weighted reconciliation with sensitivity rank, local identifiability,
    covariance, parameter correlation, reduced chi-square, and propagated independent measurement
    uncertainty while preserving exact hard-equality mode. ✅
16. Replace weighted-reconciliation normal equations with a rank-revealing rectangular
    column-pivoted QR solve and derive the local covariance kernel from its triangular factor. ✅
17. Add canonical pairwise measurement correlations, positive-definite covariance validation,
    Cholesky-whitened calibration/reconciliation objectives and sensitivities, and explicit joint
    chi-square diagnostics. ✅
18. Add active-set Gauss-Newton convergence at finite bounds, free-subspace covariance, null
    symmetric uncertainty for bound-active parameters, and explicit uncertainty interpretation. ✅
19. Add opt-in bounded profile-likelihood intervals with nuisance-parameter re-optimization,
    adaptive threshold bracketing, explicit bound truncation, evaluation budgets, provenance, and
    thin CLI access. ✅
20. Carry data reconciliation through the common `thermox.job/v16` idempotent queue, leased
    worker, execution-provenance, and checksummed result-artifact boundary without moving
    reconciliation logic out of the service. ✅
21. Add a distinct immutable reconciliation revision with disjoint constraint/held-out Study
    bindings, exact artifact snapshots, solver/profile policy, in-memory and PostgreSQL
    persistence, executable resolution, and reconciliation-specific job provenance. ✅
22. Add thin production HTTP and web Study authoring/submission for immutable reconciliation
    revisions; never accept an unrevisioned production model body. ✅
23. Add simultaneous multidimensional or sampling-based confidence regions where one-dimensional
    profiling is insufficient; allow a sparse QR/SVD backend for very large rectangular
    sensitivities.

See `docs/calibration-architecture.md`.

## Phase 2 — Combined-cycle prototype

Goal: produce a credible single-pressure combined-cycle heat balance.

Work items:

1. Add HRSG component models:
   - generic gas/material-to-water design-point energy-balance section; ✅
   - economizer ✅ *(generic dynamic material-to-fluid cell)*
   - evaporator/drum simplified ✅ *(equilibrium inventories and two-phase hydraulic impedance)*
   - superheater ✅ *(generic dynamic material-to-fluid cell)*
   - gas-side pressure drop ✅ *(composition-aware cell local-loss equations)*
   - pinch/approach constraints
2. Add gas turbine off-design placeholders:
   - generic dimensioned, non-rectangular map interpolation kernel; ✅
   - explicit reject/clamp/linear extrapolation policies and derivatives; ✅
   - versioned map artifacts, generic component artifact bindings, and runtime registry; ✅
   - request-scoped artifact bundles, job propagation, idempotency, and result provenance; ✅
   - map-based compressor graph component; ✅
   - map-based turbine graph component; ✅
   - standardized calibratable map corrections without mutating source artifacts; ✅
   - two-load shaft train with mechanical losses and an electrical generator component; ✅
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

1. Map HTTP/RPC endpoints onto the existing application service and runtime catalog.
   - transport-neutral operation and wire-response mapping; ✅
   - framework-neutral concrete HTTP application adapter for catalog, validation, steady, and
     transient operations; ✅
   - dependency-light local/integration network host; ✅
   - Team-scoped asynchronous submission/status/result routes; ✅
   - production-hardened listener.
2. Endpoints:
   - `POST /models/validate`
   - `POST /models/compile`
   - `POST /simulations`
   - `GET /simulations/{id}`
   - `GET /simulations/{id}/results`
   - `GET /component-types`
3. Add job state model and run persistence.
   - transport-neutral job state, repository ports, and in-memory contract adapter; ✅
   - checksum-pinned engineering-artifact resolver port and in-memory adapter; ✅
   - PostgreSQL and object-storage production adapters.
4. Add structured logging and run IDs.
5. Publish the existing versioned result serialization through the API.
   - job status and service-owned result retrieval boundary; ✅
   - concrete network publication. ✅

Exit criteria:

- API can validate and run Rankine/Brayton examples.
- Failed runs return actionable diagnostics.
- Model revision, case data, component versions, property backend versions, and solver options are recorded.

## Phase 4 — Frontend graph layer

Goal: let users build models visually and inspect results.

Work items:

1. Provide a transport-neutral, Team-scoped graph authoring service with typed atomic edit batches
   against immutable base revisions. ✅
2. Provide revision-backed, compile-aware validation for an exact topology, case, and engineering
   artifact set. ✅
3. Provide typed, SI-normalized case authoring for metadata, parameter overrides, fixed values,
   initial guesses, and solver options against immutable base revisions. ✅
4. Create React/TypeScript web app. ✅
5. Implement graph canvas with typed ports.
   - immutable topology visualization and revision selection; ✅
   - catalog-driven component creation and typed connection authoring through immutable edit
     batches; ✅
   - selection-driven inspection, identity-preserving update, and guarded removal. ✅
6. Component palette generated from backend component registry. ✅
7. Parameter forms with units and validation.
   - catalog-driven SI component creation forms with service-authoritative validation; ✅
   - catalog-driven instance editing; ✅
   - property-registry fluid creation and typed project-artifact binding; ✅
   - persistent SI/engineering display profiles with reversible component input conversion,
     normalized case display, and graph/result presentation; ✅
   - platform-owned extensible unit registry, service catalog descriptors, and catalog-driven
     browser conversions and authoring hints; ✅
8. Case editor using the revision-backed authoring service.
   - topology-scoped creation and exact immutable revision browsing; ✅
   - metadata, parameter override, fixed value, initial guess, and solver option editing with
     service-authoritative SI normalization; ✅
   - exact artifact-revision selection, compile-aware validation, provenance, and structured
     diagnostics. ✅
9. Run configuration and execution workspace.
   - exact topology/case/artifact bindings, complete solver policies, generic result projections,
     and immutable configuration history; ✅
   - idempotent durable job submission, state-filtered cursor history, active-job-only polling,
     optimistic cancellation, structured errors, and projected-result summaries; ✅
   - result-summary overlay on the graph; ✅
10. Graph-native steady/transient result inspection with system balance, KPI, component,
    internal-state, and port/stream tables. ✅
    - unified identity/scope filtering; ✅
    - canonical-SI visible-sample and complete-trajectory CSV export; ✅
    - selectable catalog-unit-aware transient signal plotting; ✅

Exit criteria:

- User can build a simple Rankine cycle in the UI, run it, and inspect stream results.
- Invalid connections are rejected before submission.

## Phase 5 — Production hardening

Goal: robustness, extensibility, and safe user customization.

Work items:

1. Provide validated injectable connector-domain registration and prove a custom domain/component
   through catalog, compilation, solve, and graph-native results. ✅
2. Package the existing native component/property composition boundaries as a documented extension
   SDK and out-of-tree conformance kit. ✅
3. Verify provider-owned Jacobian rows against bounded finite differences with named mismatch
   diagnostics. ✅
4. Expand analytic or automatic differentiation Jacobians across property-heavy core components.
   - backend-neutral PH derivative contract with explicit analytic/fallback provenance; ✅
   - analytic ideal-gas and HEOS CO2 derivatives plus centralized bounded IF97 fallback; ✅
   - rigid-volume and performance-map component migration to the shared contract; ✅
5. Homotopy/continuation initialization strategies.
   - adaptive, scale-aware residual homotopy with sparse derivative preservation and warm starts; ✅
   - service, durable configuration, HTTP, CLI, and web settings plus stage diagnostics; ✅
   - anchor-aware component extension contract plus fixed fluid/material turbomachinery
     pressure-ratio staging; ✅
   - fluid performance-map coordinate/geometry seeding with continuation-only linear extension
     and strict target-domain enforcement; ✅
   - fixed-duty and counterflow-UA heat-exchanger staging, including recovery from reversed
     terminal-temperature initial guesses with strict target LMTD enforcement; ✅
   - adiabatic-equilibrium combustor pressure, enthalpy, product-composition, and
     continuation-only reactant-domain staging with strict target chemistry enforcement; ✅
   - composition-coupled material-map initialization and future finite-rate/multi-zone reactor
     policies remain follow-ons.
6. Structural diagnostics and graph decomposition.
   - rectangular compile-time incidence matching with bounded unmatched variable/equation
     candidate diagnostics; ✅
   - equal-count structural-singularity rejection with unmatched equation/variable candidates
     and a stable service diagnostic; ✅
   - shared Dulmage-Mendelsohn under/over/well-determined region localization with compiler and
     service diagnostics; ✅
   - deterministic matched-incidence SCC decomposition into dependency-ordered irreducible square
     blocks, retained by steady/transient graph compilation and exposed through validation; ✅
   - backend-independent normalized backward-error verification for every scaled Newton linear
     solve, with configurable acceptance and steady/continuation/transient diagnostics; ✅
   - bounded full-system iterative refinement for finite but inaccurate backend steps, with
     independently recomputed acceptance and steady/continuation/transient recovery telemetry; ✅
   - dependency-ordered execution of fixed-pattern irreducible blocks, preserving analytic
     sparse values and enforcing a final whole-system residual check; ✅
   - optional residual-row and fixed-CSR-offset evaluation contracts, automatically assembled by
     steady and DAE equation builders and consumed by block Newton; ✅
   - exact-CSR-pattern factorization resolver with symbolic reuse across structural blocks,
     continuation stages, and transient initialization/full/half stages; ✅
   - conservative automatic selection for compiler-certified root-equivalent reducible systems
     with block-local callbacks, automatic monolithic retry on block failure, and explicit
     monolithic/block overrides through native, service, persistence, HTTP, CLI, and web
     contracts; ✅
   - informed-continuation residual-row and fixed-CSR-offset subsets, automatically assembled by
     the equation builder and preserved through homotopy anchor transforms; ✅
   - deterministic feedback-variable suggestions identify a structurally verified acyclic cut
     inside each irreducible block and are exposed through validation metadata; ✅
   - explicit exact Schur-complement tearing of Newton linearizations, with reconstructed
     whole-system backward-error verification, numerical-rank fallback, and separate structural
     incidence versus fixed-CSR contracts; ✅
   - reusable partial-pivoting dense LU and batched inner right-hand-side solves so Schur assembly
     performs one inner factorization per Newton linearization; ✅
   - sparse multi-right-hand-side factorization contract and CSR inner Schur elimination, retaining
     symbolic reuse across Newton, continuation, and DAE stages while limiting dense work to the
     reduced tear system; ✅
   - raw suggested-partition cost metrics plus steady, continuation, and transient runtime tearing
     success/fallback telemetry, without treating graph shape as numerical conditioning evidence
     or enabling tearing automatically; ✅
   - backend-sourced LU pivot-quality evidence, aggregated across steady, continuation, and DAE
     solves with explicit method/availability semantics and no condition-number overclaim; ✅
   - explicit factory-neutral structural-policy audit runner with a monolithic baseline, normalized
     root-equivalence checks, complete per-policy diagnostics, and no noisy timing gate or automatic
     policy mutation; ✅
   - nested nonlinear tearing remains a follow-on only if profiling demonstrates a benefit and an
     explicit branch-selection/convergence contract can be justified.
7. Safe custom equation DSL; no arbitrary user Python in API process.
   - bounded steady algebraic expression grammar, symbol validation, analytic sparse
     differentiation, implementation fingerprints, trusted composition-root registration,
     immutable request overlays, and restart-safe job snapshots; ✅
   - Team/project-owned content-addressed revisions, generic HTTP artifact authoring,
     run-configuration binding, integrity-checked resolution, job reconstruction, and execution
     provenance; ✅
   - project-scoped descriptor discovery, canvas-library integration, and automatic exact
     definition-revision binding during validation and run authoring; ✅
   - approval policy, compound dimension analysis, property functions, and transient-state
     contracts remain follow-ons.
8. Referenced, provider-extensible correlation templates.
   - packaged parameterized Zuber–Findlay kinematic void-fraction relation, strict coefficient
     bounds, physical limiting tests, runtime-catalog discovery, and connected two-phase pipe
     verification; ✅
   - generic multi-template family instantiation through the platform, service, and thin HTTP
     adapter, returning a deterministic canonical payload, runtime-catalog fingerprint, and
     content SHA-256 suitable for separate Team/project publication; ✅
   - classical four-regime Lockhart–Martinelli/Chisholm smooth-pipe friction family with live
     phase flux/Reynolds inputs and connected two-phase transport verification; ✅
   - reusable catalog family preset with explicit general flow-pattern fallback semantics,
     one-ID platform/service/HTTP instantiation, fingerprinted scope, and connected IF97 pipe
     verification against the cited composite regime map; ✅
   - geometry/regime-specific coefficient correlations and flow-pattern maps remain follow-ons.
9. Typed physical-regime classification.
   - provider-neutral `thermox.regime_map/v2` engineering artifact with typed inputs, bounded safe
     expression criteria, named OR branches for alternative physical mechanisms, explicit boundary
     inclusion, deterministic region/branch priority, selected-mechanism reporting, and hard
     failures for gaps or ambiguous overlaps; ✅
   - CoolProp-backed saturation surface tension as an explicit capability, authoritative N/m and
     mN/m units, and reusable phase flux, superficial velocity, Reynolds, Froude, Weber, Bond, and
     property-ratio evaluation; ✅
   - canonical request and Team/project persistence payloads, immutable job snapshots and
     fingerprints, thin HTTP publication, plus optional correlated-pipe friction-map binding with
     compile-time closure coverage and live regime selection; ✅
   - separately auditable cited Mishima–Ishii vertical-upflow bubbly/slug, slug/churn, and
     annular-entrainment transition templates, explicit evidence/applicability limits, catalog
     discovery, artifact instantiation, and native-extension registration; ✅
   - limited composite vertical-upflow map with bubbly/slug/churn/annular regions, alternative
     film-reversal and wave-entrainment annular branches, conservative global validity gates, and
     selected-mechanism verification; ✅
   - matched regime-specific closure families and additional geometries remain follow-ons.
10. Map validation and interpolation quality controls.
    - ordinary non-rectangular map quality reports, finite-derivative validation, positive
      adjacent-domain coverage for reject extrapolation, output range/slope/kink metrics, and
      explicit extrapolation/domain advisories; ✅
    - conditioned-map cross-layer family/primary coverage, condition-derivative validation,
      layer reports, extrapolation-dependency advisories, and overlaid-grid slope metrics; ✅
    - structured service validation projection with per-artifact/per-layer metrics, stable warning
      codes, exact payload attribution, and non-blocking readiness semantics; ✅
    - generic named output intervals with inclusive/exclusive bounds, publication- and
      evaluation-time enforcement, conditioned-layer consistency, quality margins, immutable
      payload/job persistence, and web declaration authoring; ✅
    - immutable Team/project-scoped quality review decisions pinned to the exact map revision and
      checksum, with server-derived quality snapshots, reviewer attribution, rationale, explicit
      supersession, PostgreSQL persistence, and thin HTTP/web-client contracts. ✅
    - immutable v2 Study qualification policies pinning exact artifact/review evidence, explicit
      acceptable approving dispositions, deterministic checksums, publication and run-resolution
      enforcement, normalized PostgreSQL constraints, and thin HTTP/web contracts. ✅
    - dimensioned v3 Study operating envelopes for performance-map coordinates, correlation
      inputs, and regime-map inputs; shared platform validation; request-scoped runtime enforcement
      overriding permissive source behavior; durable v15 job snapshots/fingerprints; and stable
      violation errors. ✅
    - structured v2 result-summary engineering evidence with signed lower/upper margins, limiting
      bound identification, transient-extremum support, durable PostgreSQL encoding, and web result
      presentation. ✅
    - dedicated engineering reviewer roles remain a follow-on.
11. Multi-user auth, authorization, project isolation, audit logs.
12. Scalable job workers and deployment automation.
13. Extended benchmark suite against published/reference examples.
   - transport-neutral classified validation-evidence contract separating independent,
     boundary-constrained, calibrated, derived, internal-consistency, and assumption-dependent
     comparisons with explicit source and limitation evidence; ✅
   - NETL 2025 Case B31A aggregate HRSG boundary audit with independently reconstructed
     water/steam duty, Cantera exhaust properties, explicit numerical/external tolerances, and a
     service-level regression test; ✅
   - NETL B31A connected HP/IP/LP steam train with gland loss, HP leakage, reheat and LP admission
     mixing, precision-aware condenser boundary, calibrated stage efficiencies, and independent
     stage/aggregate power acceptance checks; ✅
   - GB/T 14100-2016 / ISO 2314:2009 equations 28-31 equivalent turbine-cooling extraction
     calculation with detailed extraction-state, compressor-power, and manufacturer-`m_d`
     evidence paths, strict ambiguity rejection, and explicit equivalent-versus-physical flow
     semantics; ✅
   - segmented triple-pressure/reheat HRSG boundary decomposition with ten serial gas surfaces,
     three IF97 pressure circuits, cold-reheat/IP make-up mixing, whole-system conservation,
     classified boundary/calibration evidence, and generic terminal-approach rejection of the
     unsupported assumed coil order; ✅
   - durable steady/trajectory counterflow-feasibility summaries, projectable approach metrics,
     offset-safe temperature-difference units, Study acceptance integration, and web result
     presentation distinct from numerical convergence; ✅
   - a single connected 2x1 combined-cycle graph and independently parameterized off-design HRSG
     remain follow-ons.

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
