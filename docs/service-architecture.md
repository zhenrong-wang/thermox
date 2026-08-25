# Service Architecture

_Decision date: 2026-07-28_

## Boundary

`thermox_service` is the application and orchestration boundary for Thermox. It owns simulation
use cases, versioned command/result/error contracts, model serialization, provenance collection,
and translation between public data-transfer objects and engine types.

```text
CLI             Web / RPC             workers
 └───────────────┴───────────────────────┘
                     │
              thermox_service
              ├── discover runtime catalog
              ├── validate model
              ├── run steady
              ├── run transient
              ├── calibrate steady cases
              ├── submit / inspect jobs
              ├── claim / execute work
              ├── publish terminal state
              └── serialize results
                     │
                  platform
                     │
             physics + numeric core
```

The dependency direction is one-way. Interfaces may depend on `thermox_service`; the service
depends on the platform; the platform depends on physics and the numerical core. Engine layers do
not depend on services, transports, databases, or user interfaces.

## Public contract

The current synchronous service exposes:

- `ValidateModelRequest` / `ValidateModelResponse`;
- `CatalogRequest` / `CatalogResponse`;
- `SteadySimulationRequest` / `SteadySimulationResponse`;
- `TransientSimulationRequest` / `TransientSimulationResponse`;
- `CalibrationRequest` / `CalibrationResponse`;
- `EngineeringStudyRequest` / `EngineeringStudyResponse`;
- `thermox.command/v1`, `thermox.catalog/v13`, `thermox.result/v5`, and `thermox.error/v1`
  contracts;
- stable operation status and error stage/code fields;
- requested/resolved component and property versions, connector contracts, platform build, model,
  case, solver contract, effective solver-setting provenance, and request engineering-artifact
  identity;
- canonical model JSON and steady/transient/calibration result JSON.
- exact transient output schedules and mixed steady/transient held-out engineering-study evidence;
- typed case-owned piecewise-linear and right-continuous sample-and-hold input schedules compiled
  into ordinary graph boundary equations, with problem-owned knot/discontinuity times enforced by
  the adaptive DAE solver;
- typed case-owned state-threshold events with dimensional validation, directional crossing,
  nonterminal evidence capture, and terminal protective-trip behavior;
- typed event-driven `set_input` transitions over declared algebraic boundaries and controls, with
  post-transition graph evidence and no service-owned physics logic;
- dimension-checked case-owned cross-component reset sources, evaluated from one pre-event graph
  snapshot and committed atomically across input, mode, and differential-state actions;
- dimensioned directed-event hysteresis and explicit simultaneous-transition priority projected
  through canonical models and durable transient event evidence;
- deterministic runtime-catalog fingerprints and native application composition.
- `thermox.job/v18` Team-owned queued/running/succeeded/failed/cancelled simulation, calibration,
  and data-reconciliation jobs with required
  immutable request-scoped component-definition snapshots,
  idempotency keys,
  optimistic revisions, worker claims, revision-source execution provenance, and result-artifact
  manifests.
- `thermox.study_revision/v4` artifact-generic, dimensioned operating envelopes bound by the
  service to performance maps, correlations, and regime maps before execution.
- `thermox.reconciliation_revision/v1` immutable constraint/held-out Study bindings, canonical
  adjustable/observation definitions, hard-versus-weighted mode,
  solver/profile/joint-region policy, and
  exact model/artifact provenance, with production HTTP publication/submission and web authoring.

The public service DTOs contain only standard C++ data types. Solver and compiler objects do not
cross this boundary. This keeps local callers simple and permits an RPC adapter to map wire
messages without importing engine internals.

`SimulationRuntime` is immutable after construction. Normal clients see only its transport-neutral
handle. Native hosts that register C++ component or property implementations use the separate
`thermox::service_native` composition target, then inject the resulting runtime into
`SimulationService`.

Engineering datasets are not service-global mutable state. Validate, steady, transient,
calibration, and job requests may carry a `SimulationArtifactBundle` containing inline payloads or
immutable references. An injected `EngineeringArtifactResolver` resolves references and the
service requires an exact type/schema/revision/checksum match before constructing an
execution-local, typed engineering-artifact registry over any immutable deployment defaults.
Performance maps and safe algebraic correlations are the first payload adapters; component compilation and native extension
boundaries are artifact-type neutral. The overlay is destroyed after the call, duplicate
identities are rejected, queued jobs preserve references, and result-v3 records resolved artifact
provenance. Project-owned expression-component artifacts use
the same revision and content-storage boundary, then resolve into an immutable
`SimulationComponentBundle` overlay. Production PostgreSQL/object-storage adapters and in-memory
test adapters implement the same application ports.

Validation parses, canonicalizes, resolves the active runtime catalog, and compiles the selected
steady or transient case without invoking a solver. It returns variable/equation counts, reduced
closed-loop equations, the catalog fingerprint, and structured diagnostics. Diagnostic entity
paths and compiler-specific codes will become progressively more precise as compiler internals
move from exceptions to diagnostic records. Degree-of-freedom failures include bounded unmatched
equation/variable candidates, and equal-count incidence failures use the stable
`structurally_singular_model` diagnostic code.
Compiler degree-of-freedom diagnostics also include connected underdetermined and overdetermined
structural regions from the numerical core's shared incidence analyzer. These regions localize
affected equation and variable neighborhoods without claiming that any one candidate is uniquely
at fault.

Solver settings on the service command are the sole execution authority. Case-level
`solver_options` remain model metadata and are never merged implicitly into a run. Result-v3
provenance records every effective command setting so a stored run does not depend on defaults
from a later build.

Steady commands may opt into `thermox.newton-continuation/v12`, which wraps the same compiled
system and Newton solver in an adaptive scaled-residual homotopy. Continuation settings, accepted
and rejected stages, stage-level solver diagnostics, and the reached continuation parameter are
part of the service result and provenance. This is a numerical globalization mechanism, not a
gas-turbine- or cycle-specific workflow. Compiled components may supply anchor-aware physical
paths behind the same contract, and the result reports whether one was used; solver policy does
not move into a transport or UI.

Calibration is also service orchestration: the service exposes uncertainty-weighted residuals to
the numerical core's bounded trust-region least-squares callback, applies candidate parameters to
model copies, and invokes ordinary steady simulations sequentially across observation cases. It
returns the fitted canonical model and residual
attribution, while post-fit sensitivity diagnostics distinguish what measurements identify from
what declared priors regularize and provide bound-aware local covariance when justified. Neither
the platform compiler nor the nonlinear solver owns calibration behavior.
Engineering studies compose calibration with independent steady prediction cases: calibration
cases cannot be reused as predictions, the fitted canonical model is frozen before prediction,
and measurements are evaluated only against completed graph results.

Result-v3 represents steady solutions and every transient sample through the same graph structure:
components contain stable port identities for fluid, heat, shaft, signal, and control domains,
primary SI values, fluid-derived properties, internal states, and derivatives. Typed component
metrics, system balances, and KPI collections are part of the contract. Steady evaluation now
populates topology-aware net boundary mass and energy flows using registered source/sink roles and
unconnected equipment ports. Positive flow enters the modeled system; the audit does not disguise
unmodeled losses as numerical closure. Generic per-component net mass and energy metrics use the
same port-direction convention, allowing conversion losses to be attributed to their physical
equipment owner.

## Interface responsibilities

The CLI owns argument parsing, file/stdin access, exit codes, and terminal rendering. It does not
parse models, resolve registries, compile graphs, invoke numerical kernels, derive properties, or
construct result contracts.

A future HTTP or RPC adapter follows the same rule: authenticate, decode, invoke one service use
case, encode the response. `SimulationJobService` is the transport-neutral long-running workflow:
it submits idempotent jobs, coordinates leased worker claims, reuses `SimulationService` for
execution, writes the result artifact, and only then publishes success.

Stateful job operations require a trusted `IdentityContext` containing an opaque user ID, Team ID,
optional request ID, and the user's role in that Team. Job ownership and idempotency are Team-scoped, the submitting user is
recorded for audit, and lookup/result/cancellation operations require the same Team scope.
Cross-Team lookup returns not found rather than revealing resource existence. The context is
supplied after authentication by an outer gateway or trusted local host; no service code accepts
unverified identity headers, and platform/physics/numeric layers remain identity-agnostic.

## Persistence

Repository interfaces and job transactions belong beside the service layer. Database and object
storage adapters depend inward on those ports. They must never be introduced into the platform,
physics, or numerical libraries.

`SimulationJobRepository` defines atomic create-or-get, claim, terminal publication, and queued-job
cancellation operations. Every update carries an optimistic revision. `ResultArtifactStore`
defines the large-result boundary; a successful job contains a versioned, checksummed manifest
instead of embedding trajectory data in the job record. In-memory implementations are provided
for local execution and repository contract tests. The PostgreSQL job-metadata adapter now
implements the same port without changing a solver or component. The provider-neutral object
result adapter and its first S3-compatible driver implement the content boundary independently.

## API mapping

The application boundary needed by a thin network adapter is now complete:

| Intended operation | Application call | Wire representation |
| --- | --- | --- |
| Discover component, correlation-family, correlation, and regime-map templates | `SimulationService::get_catalog` | `thermox.catalog/v13` JSON |
| Instantiate a correlation artifact from a registered family ID or compatible explicit bindings | `SimulationService::instantiate_correlation` | `thermox.correlation_instantiation/v1` JSON with canonical payload and SHA-256 |
| Instantiate a regime-map artifact from a template | `SimulationService::instantiate_regime_map` | `thermox.regime_map_instantiation/v1` JSON with canonical payload and SHA-256 |
| Validate readiness and compile a model | `SimulationService::validate_model` | result-v3 validation JSON with layered readiness and an authoritative calculation gate |
| Create/list Team projects | `ProjectService` | `thermox.project/v1` JSON |
| Publish/read topology revisions | `ProjectService` | `thermox.model_revision/v1` JSON |
| Publish/read operating-case revisions | `ProjectService` | `thermox.case_revision/v1` JSON |
| Publish/read run-configuration revisions | `ProjectService` | `thermox.run_configuration_revision/v3` JSON |
| Resolve an executable model/case pair | `ProjectService::resolve_model_case` | internal `thermox.model/v2` composition |
| Resolve a complete execution intent | `ProjectService::resolve_run_configuration` | immutable model/artifact/solver snapshot |
| Submit a calculation | `SimulationJobService::submit` | `thermox.job/v18` JSON |
| Inspect a calculation | `SimulationJobService::get` | `thermox.job/v18` JSON |
| Retrieve results | `SimulationJobService::get_result` | stored `thermox.result/v5` JSON |

Job-status JSON intentionally omits the submitted model body and idempotency key. It exposes the
request mode, case, exact source revision IDs and checksums, stable request fingerprint, state,
optimistic revision, structured error, execution provenance, and result manifest. The immutable
repository payload retains the complete composed model snapshot so a worker never depends on a
later project read. Result retrieval is owned by the job application service, so an HTTP or RPC
adapter never reaches directly into object storage.

## HTTP application adapter

`thermox_http_api` is the first concrete transport adapter. It remains outside
`thermox_service` and accepts framework-neutral HTTP request/response values so a socket server,
embedded host, reverse proxy integration, or test harness can provide the network I/O. The adapter
owns route matching, query decoding, content negotiation, request-size limits, transport status
codes, and safe response headers. It delegates model parsing, validation, compilation, simulation,
and result serialization to `SimulationService`.

`thermox_api_server` is a dependency-light Boost.Beast host for local and integration deployment.
It defaults to `127.0.0.1:8080`, is intentionally single-threaded, accepts explicit listen address,
port, body limit, local user, and local Team, and never executes a simulation in the request
process. It injects the configured local identity directly and does not interpret request headers
as authentication. Non-loopback binding is rejected unless the operator supplies an explicit
insecure-development override.

`thermox_worker` is an independent, long-running calculation role. Each worker claims and executes
one job at a time, renews its lease during a solve, finishes its active calculation during graceful
shutdown, and relies on lease recovery after a hard stop. Deployments scale calculations by adding
workers with unique IDs. The default numerical-library thread cap is one, avoiding nested
oversubscription when several worker processes are deployed. Component-level parallel evaluation
can be added later behind the same process and service boundaries.

Both process roles compose the same `SimulationJobService` through `thermox_host_runtime` and
require PostgreSQL plus provider-neutral object-backed result storage. They deliberately reject
in-memory stores: separate processes cannot share process-local job or result state. The reusable
application and HTTP adapter still provide in-memory implementations for tests and embedded local
callers.

Internet-facing production deployment still requires supervised processes, an authentication
gateway, TLS termination, concurrency limits, and request timeouts; none of those concerns are
pushed into the simulation service.

The initial routes are:

| Method | Route | Purpose |
| --- | --- | --- |
| `GET` | `/healthz` | Process-level liveness |
| `GET` | `/api/v1/catalog` | Runtime catalog discovery |
| `POST` | `/api/v1/correlation-artifacts/instantiate` | Materialize a registered family ID or compatible explicit bindings as a canonical, content-addressed correlation payload |
| `POST` | `/api/v1/regime-map-artifacts/instantiate` | Materialize one physical regime-map template as a canonical, content-addressed payload |
| `GET`, `POST` | `/api/v1/projects` | List/create Team-owned logical workspaces |
| `GET` | `/api/v1/projects/{project_id}` | Read Team-scoped project metadata |
| `GET`, `POST` | `/api/v1/projects/{project_id}/model-revisions` | List/publish immutable model revisions |
| `GET` | `/api/v1/projects/{project_id}/model-revisions/{revision_id}` | Read canonical revision content |
| `POST` | `/api/v1/projects/{project_id}/model-revisions/{revision_id}/edits` | Apply a typed atomic edit batch and publish a child revision |
| `GET`, `POST` | `/api/v1/projects/{project_id}/model-revisions/{revision_id}/case-revisions` | List/publish immutable cases |
| `GET` | `/api/v1/projects/{project_id}/model-revisions/{revision_id}/case-revisions/{case_revision_id}` | Read canonical case content |
| `POST` | `/api/v1/projects/{project_id}/model-revisions/{revision_id}/case-revisions/{case_revision_id}/edits` | Apply typed operating-case edits and publish an immutable child revision |
| `POST` | `/api/v1/projects/{project_id}/model-revisions/{revision_id}/case-revisions/{case_revision_id}/validate` | Compile exact model/case/artifact revisions and return diagnostics with provenance |
| `GET`, `POST` | `/api/v1/projects/{project_id}/artifact-revisions` | List/publish immutable engineering artifacts |
| `GET` | `/api/v1/projects/{project_id}/artifact-revisions/{artifact_revision_id}` | Read tenant-scoped revision metadata and its integrity-checked canonical artifact payload |
| `GET` | `/api/v1/projects/{project_id}/component-catalog` | Discover project-owned component descriptors with exact source revisions |
| `GET`, `POST` | `/api/v1/projects/{project_id}/run-configuration-revisions` | List/publish reusable execution intents |
| `GET` | `/api/v1/projects/{project_id}/run-configuration-revisions/{revision_id}` | Read an exact execution intent |
| `GET`, `POST` | `/api/v1/projects/{project_id}/study-revisions` | List/publish immutable engineering study intent |
| `GET` | `/api/v1/projects/{project_id}/study-revisions/{revision_id}` | Read an exact study revision |
| `GET`, `POST` | `/api/v1/projects/{project_id}/calibration-revisions` | List/publish immutable calibration campaigns bound to exact model and Study revisions |
| `GET` | `/api/v1/projects/{project_id}/calibration-revisions/{revision_id}` | Read an exact calibration definition, dataset split, and solver policy |
| `GET`, `POST` | `/api/v1/projects/{project_id}/reconciliation-revisions` | List/publish immutable reconciliation intent with disjoint constraint and held-out Studies |
| `GET` | `/api/v1/projects/{project_id}/reconciliation-revisions/{revision_id}` | Read exact measurement treatment, adjustable quantities, evidence partition, and solver/profile policy |
| `POST` | `/api/v1/models/validate?case_id=...` | Compile-aware model validation |
| `POST` | `/api/v1/simulations/structural-policy-audit?case_id=...&policies=monolithic,tearing&normalized_solution_tolerance=...` | Development-only synchronous, read-only comparison against a monolithic baseline; disabled by production API defaults |
| `GET`, `POST` | `/api/v1/jobs?project_id=...&run_configuration_revision_id=...` | List or submit run-configuration-backed asynchronous jobs |
| `POST` | `/api/v1/jobs?project_id=...&calibration_revision_id=...` | Submit a calibration-revision-backed asynchronous job |
| `GET`, `POST` | `/api/v1/jobs?project_id=...&reconciliation_revision_id=...` | List or submit reconciliation-revision-backed asynchronous jobs |
| `GET` | `/api/v1/jobs/{job_id}` | Read Team-scoped job status |
| `GET` | `/api/v1/jobs/{job_id}/result` | Retrieve a succeeded simulation, calibration, or reconciliation result |

The structural-policy audit compiles the submitted model/case/artifact set once, then executes the
factory-neutral core audit against that compiled problem. Its versioned response retains compilation
structure, exact model/runtime provenance, per-policy nonlinear diagnostics, normalized root deltas,
and aggregate equivalence flags. A candidate failure is audit evidence, so a successfully completed
audit returns success even when a candidate does not converge. It never mutates a run configuration,
persists a job, or selects a production solver policy. Like the synchronous simulation routes, the
HTTP route exists for controlled local development and is absent under production API defaults.

Calibration execution composes the exact topology, cases, component artifacts, and calibration
definition named by the immutable revision. Observations belonging to training Study cases drive
the parameter fit. Observations belonging to validation Study cases are excluded from that
objective and are evaluated afterward against the fitted model, preventing validation data from
leaking into estimation.

The deployed API disables synchronous steady/transient routes so expensive work cannot enter the
request process. An explicitly configured embedded adapter may enable those routes for tests or
trusted local use. Production asynchronous submission has an empty body and names an exact
Team-scoped Project and run-configuration revision. That immutable revision selects topology,
case, performance-map/component-definition artifact revisions, steady/transient mode, and complete
solver policy. The API verifies and snapshots the executable model and typed artifacts before
enqueueing. Authentication and authorization policy follow without changing the simulation
application boundary.

The runtime catalog also publishes every native component's `supported_modes` and `default_mode`.
Canonical case documents retain initial `component_modes`; canonical state-event actions retain
typed `set_mode` transitions and dimensioned constant or graph-sourced `set_input`/`set_state`
resets. These declarations flow through the ordinary
parse/validate/snapshot/fingerprint/result path, so CLI, HTTP, worker, and future RPC clients invoke
one application contract rather than owning hybrid-simulation logic themselves.
