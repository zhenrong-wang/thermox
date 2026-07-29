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

## Implementation gate

Add database code when all of the following are ready:

1. versioned model/case/result serialization;
2. an application-level simulation command independent of the CLI;
3. explicit queued/running/succeeded/failed/cancelled job states;
4. stable provenance fields and result artifact boundaries;
5. repository contract tests that can run without PostgreSQL.

All gates are now in place. `thermox.job/v2` defines the Team-owned job lifecycle and idempotent
submission, atomic worker claim, optimistic terminal publication, and queued cancellation. The application
service writes a checksummed `thermox.result/v3` JSON artifact before publishing a succeeded job.
In-memory adapters exercise the repository contract without a database.

The first PostgreSQL metadata adapter is now implemented under `adapters/postgres`. It preserves
the atomic and compare-and-swap semantics of `SimulationJobRepository` without changing the
service, platform, physics, or numerical contracts:

- `(team_id, idempotency_key)` is a database uniqueness constraint;
- complete immutable job requests are stored as internal versioned JSON payloads so any worker can
  reconstruct the calculation;
- workers claim ordered queued jobs using `FOR UPDATE SKIP LOCKED`;
- success, failure, and cancellation are revision-checked state transitions;
- all user-facing reads and cancellation predicates include `team_id`;
- result content remains behind `ResultArtifactStore`; PostgreSQL stores only its manifest.

The adapter is optional at build time and uses the standard PostgreSQL `libpq` client. The local
HTTP host selects it when `THERMOX_POSTGRES_URL` is set.

The schema migration is
`adapters/postgres/migrations/001_simulation_jobs.sql`. The local-only Compose service mounts the
migration into PostgreSQL's initialization directory and binds PostgreSQL to loopback.

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

`ObjectResultArtifactStore` owns Thermox semantics. It derives immutable content-addressed keys,
publishes SHA-256 manifests, and verifies downloaded bytes against the PostgreSQL-published
checksum, size, media type, schema version, and provider metadata before returning content.

The first driver uses libcurl's AWS Signature V4 support and accepts endpoint, region, bucket,
credentials, and path or virtual-hosted addressing. MinIO is the first integration target, not a
platform dependency. The same driver can address conforming S3-compatible services through
configuration. A future native Alibaba OSS driver implements `ObjectStore` rather than changing
the result, job, HTTP, platform, physics, or numerical layers.

The local MinIO stack is `deploy/compose.object-storage.yml`. It is loopback-only, creates a
private `thermox-results` bucket, and uses pinned container versions. Integration tests are gated
by `THERMOX_TEST_S3_*` variables.

Engineering input data has a separate read boundary from result storage.
`EngineeringArtifactResolver` resolves immutable type/schema/revision/checksum-pinned references
into validated service DTOs. Its in-memory adapter is complete; a production object-store adapter
must verify stored bytes against the referenced checksum before decoding and returning a payload.
