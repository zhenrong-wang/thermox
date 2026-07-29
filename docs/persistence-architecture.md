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

The next persistence slice may add PostgreSQL metadata and object-storage adapters. Those adapters
must preserve the atomic and compare-and-swap semantics of `SimulationJobRepository`; they do not
change the service, platform, physics, or numerical contracts.

Engineering input data has a separate read boundary from result storage.
`EngineeringArtifactResolver` resolves immutable type/schema/revision/checksum-pinned references
into validated service DTOs. Its in-memory adapter is complete; a production object-store adapter
must verify stored bytes against the referenced checksum before decoding and returning a payload.
