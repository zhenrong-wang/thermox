# Topology draft architecture

Thermal systems are often authored incrementally. Requiring a complete `thermox.topology/v1`
document before any server-side persistence would make the declaration interface less useful than
the canvas and encourage browser-local or plain-file state. Thermox therefore separates a saved
draft from a published physical model.

## Immutable draft contract

A draft is an ordinary project engineering-artifact revision:

```json
{
  "schema_version": "thermox.topology_draft/v1",
  "id": "draft-combined-cycle",
  "label": "Initial equipment list",
  "document": {
    "model": {
      "id": "combined-cycle",
      "components": []
    }
  }
}
```

Its artifact type is `thermox.topology_draft`. The `document` may be any valid JSON object and is
not interpreted as a physical model while it remains a draft. The service canonicalizes the wrapper
and persists it through the existing Team-scoped artifact repository and object store, producing an
immutable checksum and parent/child revision lineage. The payload is limited to 2 MiB.

This boundary intentionally rejects malformed JSON, non-object documents, unknown wrapper fields,
ID mismatches, and unsupported draft schemas. It does not require topology fields, component
registry membership, fluids, connections, cases, artifacts, or calculation intent.

## Promotion boundary

Draft promotion uses the normal model-revision API; there is no relaxed model type and no solver
path for drafts. Before promotion, the workbench reports missing topology-contract fields. The
service then remains authoritative for strict topology parsing, unit normalization,
canonicalization, immutable model checksums, and parent lineage.

After promotion, the ordinary workflow still requires physical definitions, an operating case,
artifact pins, Study intent, authoritative compilation/readiness, and a run configuration before
execution. Saving a draft therefore never implies that a system is physically valid or
calculatable.

## Client behavior

The Topology JSON workbench supports three distinct states:

1. malformed JSON — neither draft persistence nor promotion is allowed;
2. valid JSON object with topology blockers — immutable draft persistence is allowed;
3. valid `thermox.topology/v1` — draft persistence and topology publication are both allowed.

Saved draft revisions can be reopened and published as child drafts. Canvas layout remains outside
both the draft declaration and physical topology, using the existing presentation document.
