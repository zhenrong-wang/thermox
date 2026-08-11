# Thermox

Thermox is a system-agnostic, equation-oriented thermal/fluid simulation platform. A system is
defined by registered components, registered fluid-property packages, topology, and operating
cases—not by a hard-coded cycle type. Gas turbines, steam plants, combined cycles, nuclear heat
systems, refrigeration systems, and other networks use the same platform path.

This repository contains a cycle-agnostic C++ numerical core for steady nonlinear systems and
implicit transient DAEs, a property-independent physics bridge, production CO2 and water/steam
property adapters backed by CoolProp, a generic thermal-system schema/compiler platform, and
isolated example models.

## Design documents

- [Research & Architecture Foundation](docs/research-and-architecture.md)
- [Schema Design Draft](docs/schema-design.md)
- [Implementation Roadmap](docs/roadmap.md)
- [Numeric Core Contracts](docs/numeric-core.md)
- [Property Packages](docs/property-packages.md)
- [Service Architecture](docs/service-architecture.md)
- [Graph Platform Architecture Review](docs/graph-platform-review.md)
- [Graph Authoring Architecture](docs/graph-authoring-architecture.md)
- [Native Extension Architecture](docs/extension-architecture.md)
- [Persistence Architecture](docs/persistence-architecture.md)
- [Performance Map Architecture](docs/performance-maps.md)
- [Calibration Architecture](docs/calibration-architecture.md)
- [Product Workflow Architecture](docs/product-workflow-architecture.md)
- [Calculation Readiness Architecture](docs/readiness-architecture.md)
- [Engineering Acceptance Architecture](docs/engineering-acceptance-architecture.md)
- [Study Comparison Architecture](docs/study-comparison-architecture.md)
- [Local Compose Stack](docs/local-compose.md)

## Current C++ numeric core

Implemented in this sprint:

- CMake-based C++20 equation-solver core library and CLI runner.
- Generic equation-system builder for declaring variables, residual equations, scaling, bounds, and initial guesses independent of any cycle type.
- Topology-aware initialization propagates fixed values and explicit guesses across typed
  connections without overwriting user anchors or embedding cycle-specific traversal.
- Generic DAE equation-system builder for differential/algebraic variables, accumulation equations,
  derivative scaling, bounds, checked evaluation, and fixed sparse Jacobians.
- Variable and residual registries for equation-system assembly metadata.
- Validated CSR matrix/pattern storage and replaceable dense/sparse linear-solver hooks.
- Optional SuiteSparse UMFPACK sparse LU backend for large, strongly scaled plant graphs; the
  dependency-free sparse elimination remains available for standalone/reference builds.
- Fixed sparse patterns with value-only Jacobian updates, dynamic sparse assembly, and hybrid
  analytic/finite-difference derivative rows.
- Composition-preserving material splitters and enthalpy-conserving material mixers for bleed,
  cooling, recirculation, and multistream reacting-system graphs.
- Generic single-phase pipe models use registered density and viscosity for Darcy-Weisbach
  friction, roughness, local losses, and elevation head; the heat-transfer variant exposes its
  ambient duty through a typed heat port so whole-system energy audits remain explicit. See
  [Single-phase pipe models](docs/single-phase-pipe-models.md).
- Correlation-driven two-phase pipes bind independent void-fraction and distributed-friction
  artifacts, then assemble signed friction, local loss, elevation head, and endpoint momentum-flux
  acceleration in steady or transient graphs. Correlation coefficients and applicability remain
  engineer-owned data. See
  [Engineering correlations](docs/engineering-correlations.md#two-phase-pressure-drop).
- Typed regime-map artifacts classify bounded safe-expression regions through named alternative
  mechanism branches, with deterministic boundary and two-level priority semantics. Gaps and
  ambiguous overlap remain engineering errors, and the selected mechanism is reported. No
  universal physical map is assumed. See
  [Regime-map architecture](docs/regime-map-architecture.md).
- Flow-area restriction models cover non-flashing liquids and perfect-gas subcritical/choked
  flow, while a separate homogeneous-equilibrium local-loss model supplies hydraulic impedance
  between two-phase inventories. Their validity checks reject incompatible phase states instead
  of silently applying the wrong correlation. See
  [Flow restriction models](docs/flow-restriction-models.md).
- A generic equilibrium flash separator splits an in-dome stream into saturated liquid and vapor
  outlets using the selected property backend, with lever-rule mass allocation and exact energy
  closure. See [Equilibrium flash separation](docs/equilibrium-flash-separation.md).
- A transient equilibrium drum stores total mass and internal energy, solves pressure and vapor
  quality from rigid-volume saturation closure, reports liquid level, and exchanges feed, phase
  outlets, and boundary heat through typed ports. See
  [Dynamic equilibrium drum](docs/dynamic-equilibrium-drum.md).
- A steady/transient actuated liquid valve consumes the platform's normalized control contract,
  maps command to effective area, and composes directly with controller and actuator-lag blocks.
  See [Actuated valve control](docs/actuated-valve-control.md).
- A transient bounded PI controller provides explicit setpoint/error handling, configurable command
  limits, and back-calculation anti-windup as a reusable control-graph component. See
  [Bounded PI control](docs/bounded-pi-control.md).
- A declared closed-loop drum example composes a fluid boundary, actuated feed valve, equilibrium
  two-phase inventory, level measurement, bounded PI controller, and actuator lag in one DAE. See
  [Closed-loop drum feed control](docs/closed-loop-drum-control.md).
- A steady/transient heat-exchanger cell combines two property-backed constant fluid holdups,
  wall thermal capacitance, and flow-dependent pressure loss. Cells compose into co-current or
  counterflow distributed equipment. See
  [Dynamic heat-exchanger cell](docs/dynamic-heat-exchanger-cell.md).
- A two-cell gas-to-water reference reverses the cold-side cell ordering to prove that distributed
  counterflow equipment is ordinary declared topology, with steady and transient whole-system
  energy closure. See [Distributed heat exchanger](docs/distributed-heat-exchanger.md).
- A composition-aware steady/transient cell couples a Cantera material stream to a property-backed
  fluid inventory and wall, preserving every declared exhaust species while exchanging heat with
  IF97 water. See [Dynamic material-to-fluid heat exchanger](docs/dynamic-material-fluid-heat-exchanger.md).
- A declared two-cell counterflow reference composes those mixed-domain cells into a distributed
  Cantera-exhaust/IF97-water exchanger with cumulative pressure loss and whole-system energy
  closure. See [Distributed heat exchanger](docs/distributed-heat-exchanger.md).
- A transient equilibrium two-phase cell couples composition-aware hot gas to a rigid saturated
  fluid inventory, resolving mass, energy, pressure, vapor quality, and wall storage. See
  [Dynamic two-phase heat-transfer cell](docs/dynamic-two-phase-heat-transfer-cell.md).
- A declaration-only single-pressure HRSG composes economizer, equilibrium evaporator,
  two-phase hydraulic loss, separator drum, and superheater models across counterflow Cantera
  exhaust and IF97 water paths. See
  [Dynamic single-pressure HRSG](docs/dynamic-single-pressure-hrsg.md).
- A declaration-only forced-circulation evaporator closes drum liquid through a quasi-steady pump,
  lumped hydraulic momentum storage, equilibrium boiling cell, and two-phase resistance. The
  dynamic graph conserves combined inventory mass and energy while resolving loop-flow
  acceleration. See [Dynamic forced circulation](docs/dynamic-forced-circulation.md).
- A pump-free natural-circulation reference combines signed elevation/local-loss segments with
  hydraulic inertance, resolving dense-liquid downcomer pressure recovery and low-density
  two-phase riser head from registered properties. See
  [Dynamic natural circulation](docs/dynamic-natural-circulation.md).
- A distributed two-cell boiling riser composes equilibrium inventories with independent
  constant-slip elevation/loss segments. Vapor quality, cell pressure, inter-cell flow, exhaust
  cooling, and storage closure evolve together without a boiler-specific solver. See
  [Distributed two-phase riser](docs/dynamic-distributed-two-phase-riser.md).
- Two-phase pipe instances can replace the baseline slip parameter with a versioned engineering
  correlation that consumes dimension-checked live quality, saturation densities, flow, geometry,
  and pressure. Nonphysical void-fraction outputs are rejected before they can enter the pressure
  balance. See [Engineering correlations](docs/engineering-correlations.md).
- Transient two-phase inventories can use the same artifact contract to distinguish conserved
  thermodynamic holdup quality from correlation-driven transported outlet quality. Pressure,
  mass, total energy, void fraction, and outlet enthalpy remain coupled in one conservative DAE.
- Correlation revisions can declare qualified SI ranges for individual inputs. Publication rejects
  malformed envelopes, while runtime diagnostics reject out-of-range component evaluations with
  the live value and exact qualified interval instead of silently extrapolating engineering data.
- Correlation families select among named, regime-labelled candidate laws using qualified
  envelopes and explicit priorities. Coverage gaps and equal-priority ambiguity are errors, while
  the same artifact role works unchanged in pipes, inventories, and other generic consumers.
- Recursive hierarchical assemblies expose public ports and parameters while expanding into
  ordinary registered components before compilation. The same contract supports one-machine,
  section-level, or stage-level modeling without machine-specific solver logic. See
  [Hierarchical component assemblies](docs/hierarchical-assemblies.md).
- Fixed-composition material sources use sparse species-keyed, bounds-checked mass fractions
  (omitted mechanism species are zero) while leaving total flow available for the connected
  equipment graph to solve.
- Performance-map compressor and turbine models for both ordinary fluid and composition-aware
  material gas paths, with component-owned flow-capacity, pressure-ratio, and efficiency
  corrections that can be calibrated without modifying immutable map artifacts.
- Conditioned three-coordinate map families for variable-geometry turbomachinery, with per-case
  angle selection across complete corrected-flow/speed maps.
- Typed per-case component-parameter overrides for operating controls and configuration changes
  without duplicating system topology.
- Scale-invariant built-in dense/CSR direct solvers for small problems and backend testing.
- Damped Newton with dimensionless row/column scaling, bounds, recoverable model-evaluation
  failures, backtracking line search, structural matching, and iteration diagnostics.
- Implicit index-1 DAE path for `F(t, y, y_dot) = 0` with differential/algebraic variables,
  consistent initial conditions, analytic dense/sparse Jacobians, adaptive variable-step BDF1/BDF2
  doubling, per-state physically scaled local-error control, limiting-state diagnostics, event
  detection, and trajectory diagnostics.
- First-class `thermox_platform` module with a model document, component registry, property registry
  integration, and graph compiler.
- A separate composition-aware thermochemistry contract validates ordered species bases and
  mass/mole fractions, exposes PT/PH/equilibrium-HP capabilities, and supports mechanism-specific
  backends without coupling components directly to a chemistry library.
- CoolProp-backed ambient moist-air conversion supplies humidity ratio, water mass fraction, and
  humid-air bulk properties from measured pressure, dry-bulb temperature, and relative humidity.
- First-class `thermox_service` application module with versioned validate, steady, and transient
  commands; an injectable immutable runtime; component/property/connector catalog discovery;
  layered calculation readiness with entity-scoped diagnostics and an authoritative queue gate;
  compile-aware validation; exact version-pin enforcement; and canonical
  `thermox.result/v3` JSON with complete execution provenance and graph-native steady/transient
  values. Its `thermox.job/v15` workflow adds Team-scoped idempotent execution, leased worker claims,
  optimistic job revisions, terminal states, checksummed external result artifacts, stable job
  status JSON, and service-owned result retrieval for thin RPC adapters.
- Study-owned, dimensioned engineering acceptance criteria bind canonical-SI bounds to declared
  result projections. Durable jobs snapshot and evaluate them after successful steady/transient
  projection while keeping engineering verdicts separate from numerical job status.
- Team- and Project-scoped Study comparison aligns completed projected results by stable identity,
  reports missing or incompatible outputs explicitly, and calculates candidate-minus-baseline SI
  and relative deltas through one service-owned contract used by HTTP and web clients.
- A separate framework-neutral `thermox_http_api` adapter maps health, catalog, compile-aware
  validation, steady, and transient HTTP routes onto `thermox_service`, with strict query decoding,
  JSON content checks, body limits, transport status codes, and safe response headers. A thin
  Boost.Beast API process publishes the adapter, while a separate worker process owns calculations.
- Asynchronous HTTP simulations carry a gateway-supplied identity context, namespace idempotency
  and resource access by Team, retain the submitting user for audit, and hide cross-Team job
  existence. The local host injects an explicit local identity; authentication is not yet
  implemented.
- Team-owned projects provide logical workspaces inside the tenant boundary. Users remain the
  acting principals, with a trusted per-Team membership role in the identity context. Projects own
  immutable, parent-linked, SHA-256-checksummed topology and case revisions in PostgreSQL. Every
  case revision binds to an exact topology revision; project and revision API reads always retain
  the Team predicate. Run-configuration resolution selects an exact project/topology/case tuple,
  stores the complete composed model snapshot in the immutable job request, and publishes all
  source revision IDs and checksums in job and result provenance.
- Project engineering artifacts have independent immutable revision history. PostgreSQL owns
  Team/project-scoped metadata and parent relationships; provider-neutral object storage owns
  content-addressed payloads. Revision-backed jobs resolve selected performance-map and safe
  expression-component revisions once, embed the verified payload snapshot, and expose each
  artifact revision/checksum in job and result provenance.
- Immutable run-configuration revisions bind one topology revision, one case revision, selected
  artifact revisions, and complete steady/transient solver settings. Production submission names
  only the Project and run-configuration revision; the API resolves a self-contained job snapshot
  and records the run-configuration ID and SHA-256 identity in execution provenance.
- Base C++ `ComponentModel` interface with physical compressor, turbine, pump, valve, fixed-duty
  and counterflow-UA two-stream heat exchangers, quality-target evaporator/condenser, mixer,
  splitter, lumped thermal storage, and rigid adiabatic fluid volume implementations.
- A map-driven compressor component resolves an instance-bound performance artifact, evaluates
  corrected mass flow and shaft speed, and closes pressure ratio, efficiency, and shaft power for
  off-design operating points.
- The corresponding map-driven turbine uses the same domain-neutral artifact format with
  turbine-specific expansion, efficiency, and generated shaft-power conventions.
- Typed shaft-train and generator components balance one driver against two loads, mechanical
  efficiency, fixed losses, electrical conversion efficiency, and speed/frequency conversion.
  Electrical power and frequency use their own connector contract.
- Queryable component catalog descriptors for ports, simulation modes, property capabilities,
  parameters, SI dimensions, defaults, and open/closed bounds. The compiler rejects missing,
  unknown, dimensionally incompatible, and out-of-range component parameters before equation
  assembly.
- Generic dimensioned performance-map interpolation across non-rectangular curve families, with
  analytic piecewise derivatives and explicit reject, clamp, or linear extrapolation policies.
- Versioned, checksummed performance-map artifacts bound to component instances by generic
  artifact roles and resolved from isolated per-request bundles over optional immutable deployment
  defaults during graph compilation. Bundles accept inline data or immutable references through an
  injectable resolver; equipment datasets are not embedded in component types or the numeric
  kernel, and queued runs preserve their payload identity and provenance.
- Generic model graph compiler that validates registered component port contracts, creates canonical port variables, lowers connections/fixed values/component equations into sparse equation metadata, and emits a `NonlinearProblem`.
- Topology-aware steady boundary audits aggregate mass and enthalpy flow for fluid/material streams
  and energy transfer for heat, shaft, and electrical ports, with explicit source/sink semantics
  discoverable through the component catalog. Per-component net-flow metrics attribute conversion
  losses to the equipment that owns them.
- `thermox.model/v2` component instances bind media to fluid ports while all port names, domains,
  directions, and connection cardinality come from the active runtime catalog.
- Calibration campaigns select explicit component/connection parameter targets without changing
  their physical ownership, and carry component/system sharing scope, cases, bounds, priors,
  measured graph observations, and uncertainties through canonical service serialization.
- Service validation resolves calibration observations against registered graph results, while
  the result layer derives thermodynamic temperature and composition metadata for reacting
  material ports through the selected thermochemistry backend.
- A bounded multi-case calibration service performs sequential uncertainty-weighted coordinate
  search through ordinary steady simulations and returns fitted parameters, residual attribution,
  execution provenance, and a reusable canonical fitted model.
- A leakage-guarded engineering-study service calibrates designated baseline cases, freezes the
  canonical fitted model, runs independent steady prediction cases, and reports per-observation
  and aggregate normalized validation residuals.
- Composition-aware compressor and turbine residuals cache thermochemistry flashes by their local
  pressure, enthalpy, and species-flow dependencies, preventing unrelated graph perturbations from
  repeatedly invoking expensive backend calculations.
- Deterministic closed-loop reduction omits only compiler-generated connection rows proven to be
  consistent linear combinations of retained equations; reduced row names remain available as
  graph diagnostics.
- Fluid connectors use conserved primary unknowns (`m_dot`, `p`, and `h`); temperature, entropy,
  density, phase, and quality are derived through the selected property package after solving.
- Natural temperature boundary specifications compile into PH property equations without adding
  redundant temperature unknowns.
- Compile-time degree-of-freedom validation rejects under- and over-specified models with variable
  and equation counts before Newton is invoked.
- Transient graph compiler that validates component simulation-mode support, creates internal
  component states, lowers accumulation and algebraic equations, and emits a `DaeProblem`.
- Property-aware compressor, turbine, and pump residuals declare their required flash capabilities,
  resolve their medium backend from the model,
  use PH/PS flashes for isentropic efficiency, propagate recoverable property-domain failures to
  Newton, and support ideal gas and real fluids without changing component equations.

The dependency-free direct solvers remain reference backends. Builds configured with
`-DTHERMOX_ENABLE_UMFPACK=ON` use reusable SuiteSparse symbolic analysis across fixed-pattern
Newton iterations and DAE stages, with fresh numeric factors for each Jacobian. Higher-order/stiff
transient work can be added behind the established DAE contract. Cycle examples do not define or
constrain the platform API.

## Repository layout

```text
core/
  example_models/       Isolated example calculations; not a platform dependency
  examples/             Example model/case inputs
  include/thermox/      Public cycle-agnostic C++ solver headers
  src/                  Cycle-agnostic solver implementation
  tests/                Steady and transient numeric-kernel tests
platform/
  include/              Generic model-document and component-registry API
  src/                  Modular physical component families, catalog validation, and graph
                        compilation
  tests/                System-agnostic platform and property-integration tests
service/
  include/              Transport-neutral commands, jobs, repository ports, and result contracts
  src/                  Simulation/job orchestration, serialization, and local adapters
  tests/                Service-boundary, repository, and workflow tests
physics/
  include/              Property-independent physics interfaces
  src/                  Ideal-gas and CoolProp-backed CO2/IF97 adapters
  tests/                Property, steady-solver, and transient integration tests
modules/
  properties/           Pinned CoolProp dependency
                        and optional pinned Cantera source
scripts/
  verify.sh             Configure, build, test, and run example solve
```

CoolProp 8.0.0 is pinned as the sole real-fluid implementation behind the unified Thermox property
interface. CO2 uses CoolProp HEOS/Span-Wagner, while water and steam use CoolProp IF97.
The former in-house CO2 and water/steam submodules are not built or retained.

## Prerequisites

- CMake 3.20+
- A C++20 compiler such as GCC or Clang
- POSIX shell for `scripts/verify.sh`

The CoolProp submodule is required for the default build. Its pinned build dependencies are
resolved by CoolProp through CPM during configuration and can be shared through `CPM_SOURCE_CACHE`.

## Clone

The production build does not require reference code:

```sh
git clone git@github.com:zhenrong-wang/thermox.git
cd thermox
git submodule update --init --recursive
```

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
```

## Run tests

```sh
ctest --test-dir build --output-on-failure
```

## Verify the native extension SDK

This explicit target stages an install and builds the separate SDK consumer serially:

```sh
cmake --build build --target thermox_sdk_conformance --parallel 1
```

## Run a generic model

Text output:

```sh
./build/thermox_cli solve --model core/examples/air_compressor.json
```

An optional Cantera-enabled build can run the full methane-air Brayton graph—material compressor,
equilibrium combustor, material turbine, shaft train, generator, and electrical boundary:

```sh
./build/thermox_cli solve \
  --model core/examples/brayton_cantera.json \
  --case design
```

JSON output:

```sh
./build/thermox_cli solve --model core/examples/air_compressor.json --format json
```

For a difficult initial guess, opt into adaptive residual continuation:

```sh
./build/thermox_cli solve \
  --model core/examples/air_compressor.json \
  --case design \
  --continuation \
  --format json
```

For a model whose validated fixed sparse pattern contains multiple dependency-ordered blocks, the
default `--structural-policy automatic` uses block execution when block-local callbacks are
available and the compiler certifies root equivalence (currently fully linear assembled systems).
`--structural-policy monolithic` and `--structural-policy blocks` provide explicit
comparison overrides. The same policy applies to `simulate`; results report the executed block
count and largest linear system. A single irreducible block remains monolithic. Validation also
reports deterministic structural tear-variable hints for cyclic blocks; these are complexity and
initialization diagnostics, not an instruction to run an unverified outer/inner tearing solve.

The CLI is a thin terminal adapter. It reads arguments and model text, calls `thermox_service`, and
renders the returned contract. Model parsing, registry resolution, graph compilation, solving,
derived-property evaluation, provenance, and JSON result construction belong to the service.

Run a generic transient model:

```sh
./build/thermox_cli simulate \
  --model core/examples/lumped_thermal_storage.json \
  --case charge \
  --end-time 10 \
  --format json
```

The transient example uses a registered lumped thermal-storage component with an internal
differential temperature state. The platform performs consistent DAE initialization before
adaptive integration.

The transient component path also supports a property-backed rigid fluid volume. Its mass and
total internal energy are differential states, while pressure and enthalpy are algebraic states
closed by the selected fluid package. The same component therefore works with any registered
backend that supplies a PH flash. Its closure Jacobian consumes the shared PH-derivative contract:
analytic where the backend advertises it and an explicit bounded fallback otherwise.
`volume.fluid.rigid_heat_transfer` adds a heat port, and the compact IF97 reference case verifies a
two-phase-to-vapor transition. See
[Regime-spanning rigid fluid volume](docs/regime-spanning-rigid-fluid-volume.md).

Run the property-backed closed-loop drum and feed-control model:

```sh
./build/thermox_cli simulate \
  --model core/examples/closed_loop_drum_control.json \
  --case level_control \
  --end-time 0.5 \
  --format json
```

Run the IF97 Rankine regression:

```sh
./build/thermox_cli solve \
  --model core/examples/simple_rankine.json \
  --case design \
  --format json
```

This example is compiled as a true closed component graph. The compiler detects and omits the
redundant mass-flow and pressure connection rows at the loop closure while retaining enthalpy
closure. Mass flow and base pressure are normal case specifications.

## Verify everything

```sh
./scripts/verify.sh
```

Verification uses two build jobs by default so the pinned CoolProp build does not monopolize a
development host. A machine with more available capacity can opt in explicitly, for example:

```sh
THERMOX_BUILD_JOBS=4 ./scripts/verify.sh
```

## Run the complete local platform

The root Compose stack builds and starts PostgreSQL, the checksum-aware database runner, MinIO,
the API, the calculation worker, and the web client:

```sh
docker compose up -d --build --wait
```

Open `http://127.0.0.1:5173`. See the
[local Compose guide](docs/local-compose.md) for lifecycle commands, configuration, endpoints,
and volume behavior.

## Local PostgreSQL job metadata

PostgreSQL is an optional outer adapter; the numerical, physics, graph, and service libraries do
not depend on it. Start the loopback-only development database:

```sh
docker compose -f deploy/compose.postgres.yml up -d --wait
```

Configure after installing the standard `libpq` development package, then run the gated
repository contract test:

```sh
cmake -S . -B build
cmake --build build --target thermox_postgres_job_tests --parallel 1
THERMOX_TEST_POSTGRES_URL='postgresql://thermox:thermox-local@127.0.0.1:55432/thermox' \
  ctest --test-dir build -R thermox_postgres_job_tests --output-on-failure -j1
```

The complete root stack runs the checksum-aware migration runner before the API and worker start.
For the dependency-only database stack, apply migrations with the same runner:

```sh
PGHOST=127.0.0.1 PGPORT=55432 PGDATABASE=thermox PGUSER=thermox \
PGPASSWORD=thermox-local THERMOX_MIGRATION_DIR=adapters/postgres/migrations \
  ./deploy/migrate.sh
```

## Local MinIO result and engineering-artifact storage

The object-storage abstraction is provider-neutral. Thermox result logic depends on `ObjectStore`;
the first concrete driver speaks the S3-compatible REST protocol using Signature V4. MinIO is the
local integration provider, while a future native OSS driver can implement the same byte-object
port.

Start the loopback-only MinIO API and console:

```sh
docker compose -f deploy/compose.object-storage.yml up -d --wait
```

- S3-compatible API: `http://127.0.0.1:59000`
- MinIO console: `http://127.0.0.1:59001`
- Development bucket: `thermox-results`
- Development login: `thermox-minio` / `thermox-minio-local`

Run the gated live driver test:

```sh
THERMOX_TEST_S3_ENDPOINT='http://127.0.0.1:59000' \
THERMOX_TEST_S3_BUCKET='thermox-results' \
THERMOX_TEST_S3_ACCESS_KEY='thermox-minio' \
THERMOX_TEST_S3_SECRET_KEY='thermox-minio-local' \
  ctest --test-dir build -R thermox_s3_object_store_tests \
  --output-on-failure -j1
```

The API and worker are separate long-running roles. Both require the same durable-store
configuration; process-local storage is intentionally rejected because it cannot be shared between
roles:

```sh
export THERMOX_POSTGRES_URL='postgresql://thermox:thermox-local@127.0.0.1:55432/thermox'
export THERMOX_OBJECT_STORE_DRIVER='s3-compatible'
export THERMOX_S3_ENDPOINT='http://127.0.0.1:59000'
export THERMOX_S3_REGION='us-east-1'
export THERMOX_S3_BUCKET='thermox-results'
export THERMOX_S3_ACCESS_KEY='thermox-minio'
export THERMOX_S3_SECRET_KEY='thermox-minio-local'

./build/adapters/http/thermox_api_server
# In another shell with the same environment:
THERMOX_WORKER_ID='local-worker-1' ./build/adapters/host/thermox_worker
```

`THERMOX_S3_ADDRESSING_STYLE` accepts `path` (the MinIO development default) or
`virtual-hosted`. `THERMOX_OBJECT_KEY_PREFIX` defaults to `results`, while
`THERMOX_ARTIFACT_KEY_PREFIX` defaults to `engineering-artifacts`. The credentials above are local
development credentials only.

Workers default to a 30-second lease, a 10-second heartbeat, and three total attempts. Deployments
can configure `THERMOX_WORKER_LEASE_MS`, `THERMOX_WORKER_HEARTBEAT_MS`, and
`THERMOX_WORKER_MAX_ATTEMPTS`; every concurrent worker must have a unique `THERMOX_WORKER_ID`.
`THERMOX_WORKER_POLL_MS` defaults to 250 ms. `THERMOX_LIBRARY_THREADS` defaults to one and caps
common numerical-library thread pools to prevent worker-count multiplication from oversubscribing
the host. The heartbeat must be shorter than the lease. Expired work is atomically requeued with a
new fencing revision; exhausted work becomes a structured terminal failure. `SIGINT` and `SIGTERM`
stop a worker after its current calculation.

## Local web workspace

The first React/TypeScript client lives in `web/`. It is a thin, system-agnostic interface over the
HTTP service: project and immutable revision browsing, topology rendering with typed catalog ports,
and a searchable runtime-generated component palette. Its system-driven workflow leads from draft
topology through engineering definition and authoritative service compilation to calculation and
result analysis. The browser reports a system as calculatable only when the exact selected model,
case, and artifact revisions compile successfully. It contains no physics, compiler, or solver
logic.

With the durable API running on its default loopback address:

```sh
cd web
npm install
npm run dev
```

See `web/README.md` for alternate API ports and bounded verification commands. Production hosting
is intentionally deferred until gateway identity and a public API endpoint exist.

Trusted deployments and request-scoped simulations can compose safe steady algebraic component
definitions through `thermox.expression_component/v2` for steady algebraic models and
`thermox.expression_component/v3` for safe index-1 transient residuals with declared internal
states and analytic sparse DAE derivatives. Expressions use registered connector
variables and dimensioned SI parameters, produce analytic sparse Jacobian rows, and cannot execute
arbitrary code. Durable jobs snapshot the exact definitions under `thermox.job/v15`. Project-owned
definitions are discovered from the project component catalog and appear directly in the canvas
library with automatic source-revision binding. The library also authors and revises safe
definitions as immutable project artifacts; component identity/version rules are enforced by the
service rather than the browser. See
[Safe expression components](docs/custom-expression-components.md).

Study v3 can impose dimensioned execution envelopes on the typed inputs of performance maps,
correlations, and regime maps. The shared core enforcement applies equally to local, HTTP, queued,
steady, and transient execution and reports stable policy violations without mutating source
artifacts or their native applicability declarations.

## Next steps

1. Package cited geometry-specific flow maps and matching closures using the delivered immutable
   regime-map persistence, optional component binding, surface-tension capability, and derived
   nondimensional inputs.
2. Add multi-signal transient comparison, event/window reductions, and server-side export for
   result sets too large for browser materialization.
3. Add native analytic PH derivatives for IF97; ideal gas, HEOS CO2, and single-phase HEOS water
   already provide analytic derivatives, while IF97 uses the shared provenance-marked bounded
   fallback.
4. Add an optional IDA-class DAE backend behind the transient problem contract for mature BDF
   order 1-5 and Newton/Krylov operation. The dependency-free native backend now provides
   adaptive variable-step BDF1/BDF2 with robust order restart.
5. Extend the delivered anchor-aware component homotopy hooks from fixed/fluid-map
   turbomachinery, heat exchangers, and the equilibrium combustor to
   composition-coupled material maps and future finite-rate reactor models.
