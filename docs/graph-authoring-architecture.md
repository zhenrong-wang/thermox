# Graph Authoring Architecture

Thermox graph authoring is an application-service capability, not frontend business logic. A web
canvas, local GUI, CLI, or RPC client may all issue the same typed edit commands. The service owns
entity parsing, unit normalization, reference rules, Team isolation, and immutable revision
publication.

## Revision model

An edit batch names one exact base model revision. The service loads that immutable topology,
applies every operation in memory, canonicalizes and reparses the complete result, and publishes
one child revision. If any operation or final topology is invalid, no revision is written.

This gives clients an explicit optimistic-concurrency boundary:

- the base revision never changes;
- a successful batch returns a new revision whose parent is the base;
- concurrent editors can detect that they started from different revision IDs;
- undo, branching, comparison, and audit history do not require mutable topology rows.

Canvas layout and other presentation metadata do not belong in the physical topology and will use
a separate document boundary.

## Typed edit contract

The first HTTP mapping is:

```text
POST /api/v1/projects/{project_id}/model-revisions/{base_revision_id}/edits
```

with `thermox.graph_edit_batch/v1`:

```json
{
  "schema_version": "thermox.graph_edit_batch/v1",
  "operations": [
    {
      "action": "upsert",
      "entity_type": "component",
      "entity_id": "compressor",
      "entity": {
        "id": "compressor",
        "kind": "compressor.fluid.isentropic_efficiency",
        "version": "1.0.0",
        "media": {"inlet": "air", "outlet": "air"},
        "parameters": {"pressure_ratio": 14.0, "eta_is": 0.87}
      }
    }
  ]
}
```

Supported entity types are `medium`, `material`, `component`, and `connection`. `upsert` requires
an entity object whose ID matches `entity_id`. `remove` does not accept an entity object.
Component removal rejects attached connections unless the caller explicitly sets `cascade: true`;
that cascade removes only those connections. Referenced media and materials cannot be removed.

The HTTP adapter only validates and maps the wire document. It wraps each entity in its versioned
platform fragment schema and invokes the transport-neutral `ProjectService` command. Consequently,
future RPC and local bindings do not need to reproduce graph rules.

The React workspace consumes this contract directly. Its component-creation form is generated from
the runtime catalog: registered ports select existing medium/material IDs, parameters are entered
as explicit SI values against catalog bounds and defaults, and artifact roles remain instance
bindings. Canvas connections use the two selected catalog ports to choose a registered connector
domain, connection kind, and exact contract version. A successful command selects the returned
child revision; the browser never mutates the loaded parent document in place. Client checks are
interaction guidance only—the service remains authoritative and may reject the whole batch.

Selecting a canvas node or edge opens an instance inspector backed entirely by the selected
revision. Editing sends an `upsert` with the existing entity ID, so labels, bindings, parameters,
and connection endpoints can change without changing instance identity. Removal is a separate,
guarded action. Component removal explicitly requests `cascade: true` only after confirmation, so
attached connection removal is visible user intent rather than an implicit canvas mutation.

## Case authoring

Operating cases use the same immutable-parent pattern:

```text
POST /api/v1/projects/{project_id}/model-revisions/{model_revision_id}/case-revisions/{base_case_revision_id}/edits
```

The `thermox.case_edit_batch/v1` request applies one atomic batch. Case ID and model-revision
binding are immutable. Operations may update or remove the optional label, update the required
mode, or upsert/remove entries in `parameter_override`, `fixed_value`, `initial_guess`, and
`solver_option` maps:

```json
{
  "schema_version": "thermox.case_edit_batch/v1",
  "operations": [
    {
      "action": "upsert",
      "field": "fixed_value",
      "key": "compressor.inlet.p",
      "value": {"value": 101.325, "unit": "kPa"}
    }
  ]
}
```

Scalar values pass through the platform unit authority and are stored canonically in SI. Removing
a missing scalar, removing the required mode, supplying an invalid unit, or any other invalid
operation rejects the entire batch without publishing a partial revision. A successful request
publishes one child case revision linked to the exact base.

## Validation boundary

Fragment parsing uses the same strict platform parser as complete topology documents. It preserves
JSON scalar types, rejects unknown fields, normalizes quantities to SI, and validates component
medium/material bindings. After all operations, the full topology is serialized canonically and
parsed again to enforce cross-entity structural integrity.

Compile-time checks that require a simulation case—port compatibility, equation/unknown balance,
registry resolution, and component-specific case data—remain in model validation and run
configuration execution. The revision-backed validation command exposes those checks at:

```text
POST /api/v1/projects/{project_id}/model-revisions/{model_revision_id}/case-revisions/{case_revision_id}/validate
```

Its `thermox.project_model_validation_request/v1` body optionally selects immutable engineering
artifact revision IDs. The application service resolves and verifies the topology, case, and
artifact content, composes the executable model, and invokes the production compiler. Its response
includes compilation diagnostics plus every selected revision ID and checksum. The frontend does
not assemble model documents, fetch object content, or reproduce compiler rules.

## Security and persistence

The identity context supplies the acting user and Team. The base revision must belong to the
requested Project and Team; cross-Team access is reported as not found. The new child revision uses
the existing repository port, so in-memory development and PostgreSQL production persistence share
the same authoring behavior and audit metadata.
