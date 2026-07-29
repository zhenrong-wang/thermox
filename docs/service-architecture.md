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
- `thermox.command/v1`, `thermox.catalog/v2`, `thermox.result/v3`, and `thermox.error/v1`
  contracts;
- stable operation status and error stage/code fields;
- requested/resolved component and property versions, connector contracts, platform build, model,
  case, solver contract, effective solver-setting provenance, and request engineering-artifact
  identity;
- canonical model JSON and steady/transient/calibration result JSON.
- deterministic runtime-catalog fingerprints and native application composition.
- `thermox.job/v1` queued/running/succeeded/failed/cancelled jobs with required idempotency keys,
  optimistic revisions, worker claims, execution provenance, and result-artifact manifests.

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
execution-local performance-map registry over any immutable deployment defaults. The overlay is
destroyed after the call, duplicate identities are rejected, queued jobs preserve references, and
result-v3 records resolved artifact provenance. The provided in-memory resolver is a contract
adapter; a later database/object-store implementation can replace it without changing platform
compilation or component equations.

Validation parses, canonicalizes, resolves the active runtime catalog, and compiles the selected
steady or transient case without invoking a solver. It returns variable/equation counts, reduced
closed-loop equations, the catalog fingerprint, and structured diagnostics. Diagnostic entity
paths and compiler-specific codes will become progressively more precise as compiler internals
move from exceptions to diagnostic records.

Solver settings on the service command are the sole execution authority. Case-level
`solver_options` remain model metadata and are never merged implicitly into a run. Result-v3
provenance records every effective command setting so a stored run does not depend on defaults
from a later build.

Calibration is also service orchestration: the service applies bounded candidate parameters to
model copies, invokes ordinary steady simulations sequentially across the observation cases, and
forms an uncertainty-weighted objective. It returns the fitted canonical model and residual
attribution. Neither the platform compiler nor the nonlinear solver owns calibration behavior.
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
it submits idempotent jobs, coordinates atomic worker claims, reuses `SimulationService` for
execution, writes the result artifact, and only then publishes success.

## Persistence

Repository interfaces and job transactions belong beside the service layer. Database and object
storage adapters depend inward on those ports. They must never be introduced into the platform,
physics, or numerical libraries.

`SimulationJobRepository` defines atomic create-or-get, claim, terminal publication, and queued-job
cancellation operations. Every update carries an optimistic revision. `ResultArtifactStore`
defines the large-result boundary; a successful job contains a versioned, checksummed manifest
instead of embedding trajectory data in the job record. In-memory implementations are provided
for local execution and repository contract tests. Production PostgreSQL and object-storage
adapters can implement the same ports without changing a solver or component.

## API mapping

The application boundary needed by a thin network adapter is now complete:

| Intended operation | Application call | Wire representation |
| --- | --- | --- |
| Discover component types | `SimulationService::get_catalog` | `thermox.catalog/v2` JSON |
| Validate and compile a model | `SimulationService::validate_model` | result-v3 validation JSON |
| Submit a simulation | `SimulationJobService::submit` | `thermox.job/v1` JSON |
| Inspect a simulation | `SimulationJobService::get` | `thermox.job/v1` JSON |
| Retrieve results | `SimulationJobService::get_result` | stored `thermox.result/v3` JSON |

Job-status JSON intentionally omits the submitted model body and idempotency key. It exposes the
request mode, case, stable request fingerprint, state, optimistic revision, structured error,
execution provenance, and result manifest. Result retrieval is owned by the job application
service, so an HTTP or RPC adapter never reaches directly into object storage.

The next adapter maps authentication, transport status codes, headers, and request decoding onto
these calls. Those concerns remain outside `thermox_service`.
