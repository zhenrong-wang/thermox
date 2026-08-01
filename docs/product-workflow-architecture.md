# Product Workflow Architecture

## Product contract

Thermox is a system-driven thermal-engineering platform. It is not a cycle-specific calculator
and the canvas is not the owner of physics or solving. Every interface—web, RPC, CLI, or future
desktop GUI—drives the same service-owned workflow:

```text
Build topology -> Define physical system -> Define study -> Calculate -> Analyze
```

Each transition creates or selects immutable domain revisions. A client may author the same
contracts without a canvas, but it must not bypass their meaning.

## Domain ownership

### Registry catalog

The deployment catalog owns reusable type contracts:

- component models, including ports, parameters, required artifacts, and supported modes;
- fluid-property backends and their capabilities;
- thermochemistry backends and their capabilities;
- connector domains and canonical transported variables;
- dimensions and unit conversions;
- deployment extension identities.

Project-owned declarative component artifacts extend the component catalog without introducing a
second execution path. Catalog entries are templates, never physical equipment instances.

### Physical-system revision

A topology revision owns the durable physical asset definition:

- component instances and typed connections;
- fluid and material definitions;
- component-owned physical parameters;
- bindings from component roles to immutable engineering artifact identities;
- stable system identity and topology metadata.

Draft revisions may be incomplete. Incompleteness is a normal authoring state, not a malformed
request. Schema and graph-edit validation still reject invalid identities, malformed values, and
impossible references, but required physics is assessed as readiness rather than required to place
a template.

### Operating-case revision

A case revision owns scenario-specific conditions independently of the physical asset:

- boundary and control specifications;
- case-specific component parameter overrides;
- initial guesses or transient initial conditions;
- the requested physical operating mode.

Cases do not own reusable equipment maps, component geometry, or global solver policy.

### Calibration revision

A calibration definition is a study input tied to an exact physical-system revision. It owns:

- component- and system-scoped adjustable parameter declarations;
- physical targets, bounds, priors, and sharing scope;
- measured observations, uncertainty, and baseline case references;
- estimation policy.

The fitted result is immutable provenance, not a mutation of an OEM artifact. Effects with a clear
physical owner remain component parameters; system scope controls parameter sharing and does not
hide missing physics.

### Study revision

A study defines what engineering question is being asked. Its intent is separate from output
selection:

- forward steady performance or thermal balance;
- off-design prediction;
- design/sizing or inverse estimation;
- calibration and independent validation;
- transient simulation;
- later optimization.

The study binds exact topology, case, calibration, and engineering-artifact revisions. It declares
fixed quantities, solved/adjustable quantities where the intent requires them, objectives or
observations where applicable, and result projections. A projection only selects output; it is not
a boundary condition or optimization objective.

### Run configuration, job, and result

A run configuration owns numerical execution policy for an already defined study. A job snapshots
all exact inputs plus the runtime catalog identity. Results retain request and execution provenance,
solver diagnostics, graph-native values, events, and external artifact checksums.

## Readiness contract

Readiness is layered and must never be reduced to a single browser boolean:

1. **Draft readiness**: component identities and connection endpoints form an authorable graph.
2. **Local physical readiness**: each component resolves its type, required parameters, media or
   material bindings, and required artifact roles.
3. **Topology readiness**: connector compatibility, resource compatibility, and graph-wide
   physical rules pass.
4. **Study readiness**: case references, boundaries, targets, observations, and mode are coherent.
5. **Compilation readiness**: the service compiler resolves exact revisions and proves structural
   equation/unknown consistency for the requested mode.
6. **Execution readiness**: numerical policy is valid and every immutable input is available.

The UI may calculate local authoring hints for responsiveness. Only service validation and
compilation can authorize calculation. Component badges must distinguish `draft`, `incomplete`,
`defined`, `blocked`, and `ready`; a locally ready component does not imply a globally calculatable
system.

## UI workspace ownership

### 1. Build

- Search and browse registered component templates.
- Drag or click to create minimally identified draft instances.
- Connect compatible ports and arrange the topology.
- Show local status badges without forcing physics input during placement.

### 2. Define

- Configure component physical models and parameters.
- Create and bind fluids and materials from registered backends.
- Browse, upload, revise, and bind engineering artifacts.
- Configure system- and component-scoped physical/calibration surfaces.
- Present actionable component and connection readiness.

### 3. Study

- Create operating cases and boundary conditions.
- Select an explicit engineering intent.
- Define observations, adjustable quantities, objectives, initial conditions, and outputs as
  required by that intent.
- Invoke authoritative validation for the exact revision set.

### 4. Calculate

- Pin solver and integration policies.
- Submit durable jobs only after authoritative readiness.
- Show truthful queue, worker, cancellation, failure, and terminal state without fabricated
  numerical progress.

### 5. Analyze

- Verify immutable provenance before graph overlays.
- Explore balances, KPIs, component/port values, trajectories, events, and solver evidence.
- Compare studies and export data through service-owned large-result paths as those are added.

## Current implementation audit

| Layer | Current strength | Required refactor |
|---|---|---|
| Numerical core | Scaled steady nonlinear solve, sparse structure, continuation, implicit DAE, diagnostics | Preserve; add new optimization backends only behind study contracts |
| Physics | Registered CoolProp property packages and Cantera thermochemistry | Preserve registry boundary; expand packages independently |
| Component/compiler platform | Generic typed graph, 49 runtime component types, maps, expression components, structural validation | Preserve; report readiness by entity and study intent |
| Calibration/study service | Immutable Study revisions now bind an exact topology, case, artifacts, intent, and output projections; multi-case calibration and leakage-guarded prediction also exist synchronously | Persist calibration revisions and connect multi-case calibration to durable Study resources |
| Persistence/jobs | Team/project scoping, immutable topology/case/study/run revisions, PostgreSQL, object storage, worker queue; run configuration v3 binds an exact Study and owns solver policy only | Add first-class Study identity to job/result provenance and durable calibration resources |
| Web workflow | Canvas, component palette, physical forms, cases, compile validation, runs, results | Split Build/Define/Study, permit incomplete drafts, add entity badges, expose study semantics |

## Migration rules

- No backward-compatibility adapters are required during this early-stage refactor.
- Do not duplicate compiler or physics rules in the browser.
- Do not store durable engineering definitions only in browser state or loose files.
- Do not turn calibration corrections into unowned global fudge factors.
- Do not call a system calculatable until the exact service compilation passes.
- Keep expensive verification serial or explicitly bounded on development hosts.
