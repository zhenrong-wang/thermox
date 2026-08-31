# Study package architecture

`thermox.study_package/v1` is the declarative boundary for reconstructing one calculatable Study
without using the visual authoring workflow. It contains:

- one `thermox.topology/v1` physical topology;
- one `thermox.case/v1` operating case;
- exact engineering-artifact revision identities and content checksums;
- the Study intent, result projections, acceptance criteria, and validation bindings;
- an optional steady/transient run configuration.

The package deliberately does not embed engineering-artifact bodies. Performance maps,
correlations, expression components, evidence series, and metrology definitions remain governed,
independently reviewable project resources. A package is importable only when every dependency is
present in the target Team/project with the declared revision ID, artifact identity, type, schema,
and checksum. This makes the first contract a reproducible declaration package rather than an
unreviewed artifact-cloning format.

## Application-service operation

Clients submit the complete document to:

```text
POST /api/v1/projects/{project_id}/study-packages
```

An optional `parent_model_revision_id` query value attaches the imported topology to an existing
model lineage. The service operation performs the following sequence:

1. parse the versioned package and reject unknown fields;
2. verify all artifact dependency pins before creating any resource;
3. publish the canonical immutable topology and case revisions;
4. compile and validate that exact topology/case/artifact selection;
5. publish the immutable Study revision only when readiness is calculatable;
6. publish the optional run-configuration revision;
7. return all created revision records plus the authoritative validation result.

The web workbench is therefore a thin editor/caller. CLI, local GUI, and RPC adapters can invoke the
same service operation. Client-side review is advisory; the service parser, Team isolation,
dependency verification, compiler, canonicalization, checksums, and lineage rules are authoritative.

## Failure and immutability

Dependency failures occur before publication. Compilation failure can occur only after the topology
and case have been canonicalized and stored; those immutable draft revisions remain available for
diagnosis and correction, while no Study or run configuration is published. The error explicitly
reports `study_package_validation_blocked`. A later Study- or solver-contract rejection likewise
retains any successfully published predecessor revisions. This is intentional audit behavior, not a
mutable transaction rollback that would erase the failed engineering declaration.

## Scope

The v1 package reconstructs a Study within a project that already governs its artifact revisions.
Cross-project transfer with embedded artifact content is not part of v1. A future portable archive
must preserve artifact provenance and review records explicitly rather than weakening checksum pins
or silently minting unrelated artifacts.
