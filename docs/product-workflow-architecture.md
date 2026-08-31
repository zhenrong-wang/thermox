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

- Select an explicit engineering intent and persisted steady/transient mode.
- Complete physical definitions and publish case-specific boundary values, parameter overrides,
  initial conditions, and numeric options as immutable case revisions.
- Select exact immutable engineering-data revisions and invoke authoritative validation for that
  topology/case/artifact selection.
- Only after the exact selection is calculatable, define outputs, acceptance criteria, and evidence
  bindings and publish an immutable Study revision.

The web workspace presents these as four guided preparation stages: intent and mode, definitions
and boundaries, engineering data and compilation, then outputs and Study publication. Stage states
before compilation are navigation hints, not solver claims. Artifact selections are shared
workspace state rather than private form state: changing a selected revision immediately makes the
prior compiler result stale, hides it from the current preparation context, and blocks Study
publication until the new exact set is compiled. This prevents the browser from displaying one
artifact selection while publishing another. When locating an already published Study, the client
requires every selected calculation artifact and the exact case revision; additional Study-owned
validation-evidence artifacts remain valid and do not make that Study disappear from execution
authoring.

### 4. Calculate

- Pin solver and integration policies.
- Submit durable jobs only after authoritative readiness.
- Show truthful queue, worker, cancellation, failure, and terminal state without fabricated
  numerical progress.

### 5. Analyze

- Verify immutable provenance before graph overlays.
- Explore balances, KPIs, component/port values, trajectories, events, and solver evidence.
- Evaluate Study-owned dimensioned acceptance criteria without conflating them with solver status.
- Compare completed Study jobs through service-owned projection alignment and export data through
  service-owned large-result paths.

## Current implementation audit

| Layer | Current strength | Required refactor |
|---|---|---|
| Numerical core | Scaled steady nonlinear solve, sparse structure, continuation, implicit DAE, diagnostics | Preserve; add new optimization backends only behind study contracts |
| Physics | Registered CoolProp property packages and Cantera thermochemistry | Preserve registry boundary; expand packages independently |
| Component/compiler platform | Generic typed graph, 54 built-in runtime component models, maps, correlations, expression components, structural validation, layered entity readiness, dimensioned engineering acceptance, classified validation evidence, and durable steady/transient counterflow-feasibility evidence | Preserve; extend physical audits and criteria beyond absolute scalar intervals behind the Study contract |
| Calibration/study service | Immutable Study and calibration revisions bind exact topology, cases, artifacts, intent, dataset split, and output projections; bounded trust-region calibration executes as a durable job and distinguishes measurement identifiability from prior-regularized posterior uncertainty | Preserve the optimizer callback/evidence boundary; add sparse backends only when scale requires them |
| Persistence/jobs | Team/project scoping, immutable topology/case/study/calibration/run revisions, PostgreSQL, object storage, leased worker queue, and service-owned comparison with exact provenance | Preserve; add generated-report artifacts through existing storage ports |
| Web workflow | Build/Define/Study/Calculate/Analyze workspaces, canvas and catalog authoring, exact revision validation, durable runs, readiness, acceptance, and Study comparison | Project entity readiness onto canvas badges; add generated engineering report workflows |

## Migration rules

- No backward-compatibility adapters are required during this early-stage refactor.
- Do not duplicate compiler or physics rules in the browser.
- Do not store durable engineering definitions only in browser state or loose files.
- Do not turn calibration corrections into unowned global fudge factors.
- Do not call a system calculatable until the exact service compilation passes.
- Keep expensive verification serial or explicitly bounded on development hosts.
