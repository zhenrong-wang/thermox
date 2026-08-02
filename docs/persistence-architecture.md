# Persistence Architecture

_Decision date: 2026-07-27_

## Decision

Thermox does not add a database dependency to the numerical, physics, component, or graph-compiler
libraries. Those layers remain deterministic, embeddable, and usable from files, tests, desktop
applications, batch tools, and future services.

Database implementation begins after the simulation service's model/result serialization
contracts and job lifecycle are stable. Persistence is accessed through application-layer
repository interfaces; database adapters depend on those interfaces, while the simulation engine
does not know that a database exists.

```text
API / job service
  ├── simulation application workflow
  │     └── thermox platform → physics → numeric core
  └── repository interfaces
        ├── PostgreSQL metadata adapter
        └── object-storage result adapter
```

## Intended storage split

PostgreSQL is the preferred production metadata store:

- Teams, projects, and ownership;
- immutable model revisions;
- immutable case revisions;
- simulation run state, options, timestamps, and diagnostics;
- component, property-backend, schema, and solver versions;
- result manifests and searchable summary metrics.

Large trajectories, field data, and report artifacts should live in object storage with
content-addressed checksums. They should not be stored as large database blobs. SQLite may support
single-user/local development through the same repository interfaces, but it is not the production
architecture.

## Initial records

- `Project`
- `ModelRevision`
- `CaseRevision`
- `SimulationRun`
- `ResultManifest`
- `ResultArtifact`

Model and case revisions are immutable. A run references exact revisions and records the component
catalog snapshot, property backend versions, solver version/options, source commit when available,
and artifact checksums. This makes every result reproducible and auditable.

## Service transaction boundary

Submitting a simulation transaction creates a queued run with an idempotency key. Workers claim
runs, execute outside a long-running database transaction, then atomically publish terminal status,
diagnostics, summary values, and an artifact manifest. Failed uploads never expose a successful run
without its referenced artifacts.

Every stateful record is owned by a Team. Job idempotency uniqueness is `(team_id,
idempotency_key)`, and repository reads/cancellation must include `team_id` in their predicate.
The actor user ID is audit metadata rather than the tenant boundary. A missing or mismatched Team
scope returns not found. This contract is already enforced by the in-memory adapter and must be
preserved by PostgreSQL row constraints and repository queries.

The Team is the tenant and hard isolation boundary. A Project is only a Team-owned logical
workspace for related engineering models, cases, runs, and artifacts; it is not a nested tenant
and does not own users. A User is the acting principal. The trusted identity context carries the
selected Team membership role, allowing the same user to be an admin in one Team and a regular
member in another. Authentication, membership resolution, and role policy belong to the identity
gateway/application boundary. Repository predicates still enforce Team isolation independently of
that policy.

## Implementation gate

Add database code when all of the following are ready:

1. versioned model/case/result serialization;
2. an application-level simulation command independent of the CLI;
3. explicit queued/running/succeeded/failed/cancelled job states;
4. stable provenance fields and result artifact boundaries;
5. repository contract tests that can run without PostgreSQL.

All gates are now in place. `thermox.job/v8` defines the Team-owned job lifecycle and idempotent
submission, leased worker claim, optimistic terminal publication, and queued cancellation. The application
service writes a checksummed `thermox.result/v3` JSON artifact before publishing a succeeded job.
In-memory adapters exercise the repository contract without a database.

The first PostgreSQL metadata adapter is now implemented under `adapters/postgres`. It preserves
the atomic and compare-and-swap semantics of `SimulationJobRepository` without changing the
service, platform, physics, or numerical contracts:

- `(team_id, idempotency_key)` is a database uniqueness constraint;
- complete immutable job requests are stored as internal versioned JSON payloads so any worker can
  reconstruct the calculation;
- revision-backed submissions retain the exact project, topology revision, and case revision IDs
  and both source SHA-256 checksums alongside the composed executable model snapshot;
- workers claim ordered queued jobs using `FOR UPDATE SKIP LOCKED`;
- success, failure, and cancellation are revision-checked state transitions;
- all user-facing reads and cancellation predicates include `team_id`;
- result content remains behind `ResultArtifactStore`; PostgreSQL stores only its manifest.

The adapter is optional at build time and uses the standard PostgreSQL `libpq` client. The local
HTTP host selects it when `THERMOX_POSTGRES_URL` is set.

The schema migration is
`adapters/postgres/migrations/001_simulation_jobs.sql`. The local-only Compose service mounts the
migration into PostgreSQL's initialization directory and binds PostgreSQL to loopback.

Migration `003_projects_and_model_revisions.sql` adds Team-owned projects and immutable topology
revision history. Topology revisions use `thermox.topology/v1`, are assigned an atomic per-project
sequence, may reference a parent only inside the same `(team_id, project_id)` scope, preserve exact
canonical JSON bytes, and publish a SHA-256 checksum. PostgreSQL composite foreign keys make a
cross-Team or cross-Project parent relationship impossible.

Migration `004_case_revisions.sql` adds independent `thermox.case/v1` operating-case history. Every
case revision binds to one exact topology revision. Its parent must have the same Team, Project,
topology revision, and logical case ID; revision numbering is atomic within that scope. The service
can compose an exact topology/case pair into the existing internal `thermox.model/v2` compiler
input. This preserves solver, physics, component, calibration, and direct embedded-caller behavior
while removing embedded cases from the persisted product model.

`ProjectService` can resolve an exact Team-scoped project/topology/case tuple into a complete
composed `thermox.model/v2` snapshot. Run configurations use this internal operation during job
submission. `thermox.job/v8` captures the immutable source provenance and composed snapshot, so
workers never reread mutable project state and can execute even if newer revisions are published
later.

Migration `012_job_study_provenance.sql` resets early-development job history for the v7 contract.
Every run-backed request and execution record carries the exact Study revision and checksum in
addition to run configuration, topology, and case provenance. Engineering intent is therefore
directly queryable and independently verifiable in job and result records.

Migration `005_artifact_revisions.sql` adds independent project engineering-artifact history.
Each logical artifact has an ordered, parent-linked revision chain inside one Team and Project.
PostgreSQL stores type/schema identity plus the object manifest; canonical payload bytes live in
provider-neutral object storage. The service verifies checksum and byte size before decoding a
supported payload. Supported types currently include performance maps and safe steady algebraic
component definitions. Both use the same ownership, parent-history, object-storage, and
run-configuration binding rules.

Simulation submission may select exact artifact revision IDs from the same Team and Project. The
API resolves and verifies those revisions once and stores complete typed map and component
payloads in the immutable job snapshot. Consequently workers do not need project or object-store
metadata reads during calculation, and job/result provenance records the logical artifact ID,
persisted revision ID, schema, and SHA-256 identity.

Migrations `006_run_configuration_revisions.sql` and
`011_run_configurations_bind_studies.sql` establish reusable execution-policy history. The v3
contract binds one exact Study revision and complete steady/transient solver settings. The Study
owns topology, case, engineering artifacts, intent, and output projections. Composite foreign keys
prevent cross-Team and cross-Project bindings. Parent revisions remain in the same logical chain,
and a deterministic SHA-256 checksum identifies the Study binding and solver policy.

Production simulation submission now names only a Team-scoped Project and run-configuration
revision. The application resolves its Study dependencies, verifies artifact content, and snapshots the
composed model, typed artifacts, and solver settings into the durable job. Job and result
provenance retain the run-configuration revision/checksum alongside topology and case identities.

The React workspace exposes these records as reusable execution policies rather than immediately
starting calculations. Creation selects a published Study and submits complete steady and
transient solver policies. Revising a configuration preserves its logical ID, records the selected
immutable parent, and republishes the policy; the browser never patches solver settings or bindings
in place. Project-wide records are presented in the context of their Study topology.

Migration `007_simulation_job_history.sql` promotes jobs into the queryable execution history used
by product interfaces. It backfills and persists indexed Project and run-configuration identities,
while the application exposes bounded, newest-first cursor pagination with optional state,
Project, and run-configuration filters. The repository always applies Team ownership before any
filter or cursor, and the HTTP cursor encoding remains outside the transport-neutral service
contract.

Queued jobs may be cancelled through `DELETE /api/v1/jobs/{job_id}`. The thin HTTP adapter
requires the exact ETag returned by submission or status lookup in `If-Match`; missing, stale, and
malformed preconditions remain distinct from job-state conflicts. Cancellation publishes a new
terminal revision rather than deleting history, and cross-Team targets are reported as missing.

Migration `008_run_result_projections.sql` originally added output selections to run configurations;
the v3 boundary moves those unit-checked, system-agnostic selections to immutable Study revisions.
The selections are checksummed and snapshotted into jobs. Successful
workers atomically publish the projected `thermox.result_summary/v1` in PostgreSQL alongside the
full result-artifact manifest, allowing history and status views to render selected engineering
outputs without reading large object-store results.

The thin React Runs workspace consumes this contract directly. Submission uses a caller-stable
idempotency key so an uncertain browser retry cannot create a duplicate calculation. History is
limited to the selected immutable run-configuration revision, supports the service's state filter
and cursor, and retains the selected job while refreshing. It polls at a bounded interval only
while the Runs workspace is visible and at least one loaded job is queued or running. Cancellation
uses the selected job revision as its `If-Match` precondition. Terminal views expose revision
provenance, structured worker failures, projected summaries, and the full-result manifest without
moving orchestration or physics into the browser.

## Object storage

Durable result content now follows a two-level adapter design:

```text
ResultArtifactStore (service contract)
  └── ObjectResultArtifactStore
        └── ObjectStore (provider-neutral byte-object port)
              ├── S3CompatibleObjectStore → MinIO / S3-compatible services
              ├── future AWS-specific composition if required
              └── future Alibaba OSS or other native provider driver
```

`ObjectStore` knows only relative keys, bytes, media type, and string metadata. It does not expose
S3 buckets, ETags, MinIO concepts, presigned URLs, or provider exceptions. Provider drivers own
authentication, endpoint/bucket or container addressing, transport, retries, and error mapping.
This keeps the abstraction usable by both S3-compatible APIs and native object-storage APIs that
are not S3-compatible.

`ObjectResultArtifactStore` owns Thermox result semantics. It derives immutable content-addressed keys,
publishes SHA-256 manifests, and verifies downloaded bytes against the PostgreSQL-published
checksum, size, media type, schema version, and provider metadata before returning content.
`ObjectEngineeringArtifactContentStore` uses the same provider-neutral byte boundary for project
inputs, with Team/Project namespacing and content-addressed keys. Neither adapter exposes provider
keys through the public service API.

The first driver uses libcurl's AWS Signature V4 support and accepts endpoint, region, bucket,
credentials, and path or virtual-hosted addressing. MinIO is the first integration target, not a
platform dependency. The same driver can address conforming S3-compatible services through
configuration. A future native Alibaba OSS driver implements `ObjectStore` rather than changing
the result, job, HTTP, platform, physics, or numerical layers.

The local MinIO stack is `deploy/compose.object-storage.yml`. It is loopback-only, creates a
private `thermox-results` bucket, and uses pinned container versions. Integration tests are gated
by `THERMOX_TEST_S3_*` variables.

## Worker leases and recovery

Every running job has an attempt number, fencing revision, worker ID, and lease expiry. Heartbeats
extend only the expiry; they never change the fencing revision. Terminal publication requires the
same revision and a live lease.

Workers recover expired rows before claiming new work. An eligible expired attempt is requeued
with its revision incremented and worker cleared, so the previous worker can no longer publish.
When the configured attempt limit is reached, recovery publishes a structured
`worker_attempts_exhausted` failure instead. PostgreSQL performs recovery in one atomic update;
concurrent workers cannot recover the same row twice.

Migration `002_worker_leases.sql` adds the attempt and lease columns, backfills historical terminal
jobs, and makes any historical running row immediately recoverable. Compose initialization applies
it automatically to new volumes. Existing development or deployed databases must run the migration
explicitly.

The deployed API and worker are independent processes composed through the same durable adapter
configuration. PostgreSQL is the coordination authority and object storage is the result-content
authority; neither role permits an in-memory fallback. Each worker executes one calculation at a
time, and deployments add worker processes for bounded job-level parallelism.

Engineering input data has a separate read boundary from result storage. Project artifact
revisions are now resolved through `EngineeringArtifactContentStore`; its object adapter verifies
stored bytes against the PostgreSQL manifest before decoding them into service DTOs. The older
`EngineeringArtifactResolver` remains useful for deployment-installed references and embedded
callers, while production revision-backed HTTP jobs carry verified inline snapshots.
