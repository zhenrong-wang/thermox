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

Canvas layout and viewport metadata do not belong in the physical topology. They use the separate
`thermox.topology_presentation/v1` document boundary and never participate in model checksums,
compiler input, or numerical provenance. The service stores one mutable presentation per
`(team_id, project_id, user_id)`, referencing the model revision against which its entity IDs were
validated. A topology revision remains immutable while each engineer can arrange the same system
without overwriting another engineer's workspace.

## Declaration workbench

The Build workspace also accepts the public `thermox.topology/v1` JSON document directly. Loading
or pasting a declaration, reviewing it, and publishing it creates the same immutable model revision
as every other client; it does not create a browser-only graph format. The browser performs only an
early structural review. The service remains authoritative for parsing, registry validation,
canonicalization, checksum generation, Team isolation, and parent-revision linkage.

The selected model revision can be copied or downloaded from the same workbench and loaded again to
reconstruct its physical graph. Presentation coordinates remain a separate user-scoped document,
so exporting a topology does not accidentally turn viewport state into physical or numerical input.
Cases, engineering artifacts, Studies, and run configurations retain their own revision contracts;
they are not embedded implicitly in a topology declaration.

## Canvas responsibility

The canvas is a typed topology editor, not a general-purpose drawing document. React Flow supplies
selection, viewport, node, edge, and typed-handle interaction. Thermox does not embed draw.io or
adopt its diagram schema because the authoritative entities are registered components, connector
contracts, media/material definitions, engineering artifacts, and immutable revisions—not
arbitrary shapes and lines. The current presentation document contains node coordinates and a
viewport; later versions may add groups, labels, and annotations without changing the physical
model.

The intended instance workflow is:

1. select a component type from the runtime component registry;
2. bind its fluid/material ports to topology resources created from the property and
   thermochemistry registries;
3. enter catalog-declared scalar parameters;
4. bind typed, immutable project engineering artifacts such as performance maps or correlations;
5. connect compatible registered ports and ask the service to publish and validate the child
   revision.

Dropping an equipment model records the pointer position in flow coordinates before its instance
form opens. After the service publishes the new physical-model revision, the client stores that
position against the returned revision through the presentation endpoint. Failed component
publication therefore cannot leave a phantom canvas node, while a presentation-storage failure
cannot roll back or disguise a successfully published physical revision.

Connection gestures are checked against the catalog direction, domain, duplicate, and capacity
rules while they are drawn. Rejected gestures show the concrete preflight reason on the canvas;
accepted gestures still require authoritative service validation. Delete and Backspace are mapped
to the same confirmed component, assembly, or connection removal commands as the inspector. React
Flow's local element deletion is disabled, so keyboard interaction can never mutate only the
browser copy or bypass immutable revision publication. Escape clears canvas selection.

## Physical templates and calculation models

The catalog keeps the engineer-facing physical identity separate from the executable model kind.
`template_kind`, `display_name`, and `category` describe equipment presented by authoring clients;
`kind`, `version`, and `model_name` select one registered equation implementation. Several models
may therefore implement one physical template without making maps or correlations appear as
topology nodes. For example, the `compressor` template currently has isentropic-efficiency and
performance-map implementations.

The first fitting template follows the same rule:

```text
template_kind: fitting.fluid.return_bend
display_name: Return bend (180 deg)
kind: fitting.fluid.return_bend.fixed_loss_coefficient
model_name: Fixed loss coefficient
```

Its instance declares an inner diameter and loss coefficient. The component evaluates inlet
density through the bound property package and contributes the directed pressure-loss residual
`p_in - p_out - K m_dot |m_dot| / (2 rho A^2)`, plus mass and adiabatic enthalpy continuity.
Future registered correlation implementations can share the same physical template while adding
geometry parameters or typed correlation-artifact roles.

Fluids, reacting mixtures, construction materials, and engineering artifacts are definition
resources rather than physical component templates. Canvas clients may provide creation shortcuts,
but must present them separately and submit the same service-owned graph commands as nonvisual
clients.

An empirical formula is treated as a versioned correlation artifact only when a registered
component declares that artifact role and implements its semantics. The browser does not evaluate
arbitrary engineering expressions or infer equations from canvas annotations.

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

The workspace also consumes project artifact-revision metadata and property-backend metadata from
the service. Artifact selectors filter immutable logical artifacts by the type declared for each
component role and show the latest project revision; the topology stores the logical artifact ID,
while run configuration later pins the exact artifact revision used for execution. Fluid creation
selects a registered backend and one of its advertised substances, then publishes the resulting
medium definition in the same immutable graph-edit path. Backend and artifact compatibility remain
service-authoritative.

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

The React workspace exposes cases as a separate view while retaining the selected topology
revision in the global context. It creates a minimal base case, browses every immutable revision,
and edits label, mode, parameter overrides, fixed values, initial guesses, and solver options
through the typed batch contract. Values may be entered with engineering units; the selected child
revision is reloaded from the service as canonical SI. The browser may convert that canonical value
through its selected display profile, but it does not replace service-authoritative input
normalization or mutate the topology document.

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

The case workspace derives logical artifact requirements from component-instance bindings, asks the
user to select one exact immutable project revision for each, and submits that revision set to the
validation command. It presents equation/variable counts, catalog fingerprint, revision checksums,
and structured diagnostics with component, port, connection, JSON-path, and suggestion fields.
Compilation failures intentionally use HTTP 422 while retaining the complete
`thermox.project_model_validation/v1` response; clients treat that document as an engineering
result rather than a transport exception.

### Layered readiness presentation

The graph workspace presents four explicit readiness layers: physical definition, known connector
structure, study inputs, and service compilation. The first three are clearly labelled local
authoring hints. They aggregate catalog-declared missing bindings, parameters, artifacts, malformed
persisted connector intents, missing cases, and unresolved artifact-revision selections so an
engineer can navigate directly to the responsible component, assembly, connection, or workspace.
They do not assert that an equation system is closed or solvable.

The compilation layer is populated only by a `thermox.project_model_validation/v1` result whose
model, case, and artifact revision IDs exactly match the current selection. Both successful and
blocked exact results remain visible. Diagnostics retain their service provenance and navigate to
their graph entity when one is supplied; system-level diagnostics return to the study validation
view. The UI uses the word `Calculatable` only when that exact service result reports calculatable
readiness. A stale validation result is displayed as not evaluated rather than being inferred or
silently reused.

## Security and persistence

The identity context supplies the acting user and Team. The base revision must belong to the
requested Project and Team; cross-Team access is reported as not found. The new child revision uses
the existing repository port, so in-memory development and PostgreSQL production persistence share
the same authoring behavior and audit metadata.
