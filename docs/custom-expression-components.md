# Safe expression components

Thermox supports deployment-composed steady algebraic and index-1 transient components through
the unified `thermox.expression_component/v5` contract.
The component descriptor remains authoritative for
physical-template identity, executable kind/version, typed ports, parameter dimensions, defaults,
and bounds. Equations add residual
behavior without loading executable user code.

An expression can reference:

- a canonical connector variable as `<port>.<variable>`, for example `inlet.p` or
  `shaft.W_dot`;
- an SI-normalized component parameter as `parameter.<name>`;
- finite decimal numbers and the constants `pi` and `e`;
- `+`, `-`, `*`, `/`, parentheses, and unary signs;
- `abs(x)`, `sqrt(x)`, `exp(x)`, `log(x)`, and `pow(x, y)`;
- the constrained p-h calls `property.temperature_ph(<port>.p, <port>.h)`,
  `property.density_ph(<port>.p, <port>.h)`, and
  `property.internal_energy_ph(<port>.p, <port>.h)`.

Each expression is a residual whose target value is zero. For example, a steady fluid
pressure-loss component can declare:

```text
outlet.m_dot - inlet.m_dot
outlet.p - inlet.p * parameter.pressure_ratio
outlet.h - inlet.h
```

Registration also infers a physical-dimension signature for the complete expression tree.
Addition and subtraction require compatible dimensions; multiplication, division, square root,
and constant powers compose SI base dimensions; `exp` and `log` require dimensionless arguments;
and a dimensioned `pow` base requires a compile-time constant, dimensionless exponent. State-rate
symbols carry their state dimension divided by time. Exact numeric zero is accepted as the
dimension-polymorphic additive identity, while other numeric literals are dimensionless; physical
constants should therefore be declared as dimensioned component parameters. Thermox's standard
quantity names map to SI base dimensions, so derived identities such as mass flow times specific
enthalpy equaling power are recognized. Extension-defined dimension names remain distinct opaque
dimensions and still receive equality and cancellation checks.

Property calls must use the direct pressure and enthalpy symbols of the same declared fluid port.
Registration derives the component's `state_ph` capability requirement; graph compilation binds
that port to its selected registered property package and rejects unsupported media before solving.
Evaluation uses the backend-neutral p-h derivative contract, including its bounded documented
fallback where an analytic provider derivative is unavailable, and chains those derivatives into
the expression's fixed sparse Jacobian row. Invalid or out-of-range thermodynamic states are
recoverable physical evaluations; unsupported providers and backend failures are fatal. Arbitrary
property names, computed state arguments, cross-port states, transport calls without derivative
contracts, and user callbacks are not accepted.

The declaration distinguishes the physical template from one executable model, so nonvisual
clients can publish the same catalog structure used by the canvas:

```json
{
  "kind": "project.fitting.return_bend.empirical",
  "version": "1.0.0",
  "template_kind": "project.fitting.return_bend",
  "display_name": "Project return bend",
  "category": "Project fittings",
  "model_name": "Plant empirical equation",
  "ports": [],
  "parameters": [],
  "equations": []
}
```

The abbreviated empty arrays above illustrate identity only; publication requires valid ports and
at least one residual equation.

Registration parses and validates every expression before its runtime becomes immutable. A
deployment may register trusted definitions in the base runtime. Validation, steady simulation,
calibration, engineering-study, transient, and job requests may instead carry a
`SimulationComponentBundle`; the service composes a temporary immutable overlay without changing
the shared process runtime. Unknown symbols and functions are rejected before model compilation.
Expressions are limited to 4,096 characters, 512 syntax nodes, and 64 nesting levels. There is no
file, process, network, allocation, reflection, loop, or arbitrary callback syntax. Evaluation derives exact
sparse dependencies from referenced port variables and uses forward analytic differentiation for
the solver-owned Jacobian pattern. Domain failures such as division by zero or an invalid
logarithm are reported as recoverable physical evaluations.

Equation content has a deterministic implementation fingerprint. The service includes it in the
runtime catalog fingerprint, so changing a custom equation changes validation and execution
provenance even if its public descriptor is unchanged. `thermox.job/v16` also snapshots the complete
request-scoped bundle and includes it in the idempotency fingerprint. A worker therefore
reconstructs exactly the submitted component implementation after a process restart.

## Transient equations

A definition can
declare transient connector variables, bounded internal algebraic or differential variables, and
transient residual equations. Transient residuals can reference:

- ordinary connector states, such as `input.value`;
- internal states, such as `internal.filtered_temperature`;
- the rate of a declared differential state, such as
  `derivative.internal.filtered_temperature`;
- the rate of a connector variable explicitly declared differential, such as
  `derivative.inventory.mass`;
- SI parameters and the independent variable `time`.

Registration rejects rate references to algebraic variables. Evaluation supplies an exact sparse
DAE row with separate derivatives with respect to state and state rate, so the native integrator
and future IDA-class backends use the same immutable component contract. Internal initial values,
initial rates, scales, bounds, and dimensions are part of the implementation fingerprint and are
carried through request bundles, artifact revisions, and durable-job serialization.

## Fixed-topology operating modes

Version 5 optionally replaces the top-level equation arrays with named `modes`, each containing
steady and/or transient equation arrays, plus one `default_mode`. Registration derives catalog
mode metadata and requires every mode to preserve equation count, names, order, residual scales,
and referenced-symbol incidence. Ports, parameters, internal variables, connector states, and the
sparse DAE structure therefore remain immutable while safe residual behavior changes.

Cases select the initial mode through `component_modes`; state events use the ordinary validated
`set_mode` action. The same declaration works through native embedding, request-scoped execution,
immutable Team/project artifacts, queued workers, and catalog discovery. A component without
`modes` uses its top-level `equations` and `transient_equations`.

## Component-owned hybrid events

Version 5 lets a transient definition own zero-crossing surfaces and accepted-event actions. An
event expression can reference ordinary port/internal states, SI parameters, `time`, and the same
constrained p-h property functions as residual equations. State-rate symbols are forbidden. The
declaration supplies the expression's physical dimension, crossing direction, terminal flag,
priority, and an SI hysteresis distance in that dimension. Registration verifies the inferred
expression dimension and requires at least one component state on every surface.

```json
{
  "events": [{
    "name": "overspeed_trip",
    "expression": "internal.speed - parameter.trip_speed",
    "dimension": "angular_speed",
    "direction": "rising",
    "priority": 10,
    "hysteresis_si": 0.5,
    "actions": [
      {
        "type": "set_state",
        "target": "internal.valve_position",
        "expression": "internal.valve_position * parameter.trip_fraction"
      },
      {"type": "set_mode", "mode": "tripped"}
    ]
  }]
}
```

`set_state` targets must be declared local differential variables. Reset expressions use the
pre-event state, must match the target dimension, and are checked against declared bounds at the
accepted crossing. All reset expressions in one event are evaluated first and committed
atomically; a mode action is applied after the state commit. Thermox then performs its ordinary
consistent algebraic/derivative reinitialization and restarts BDF history. Simultaneous events
retain the solver's ascending-priority execution contract. Component event names are namespaced as
`component.<instance-id>.event.<name>` in result evidence, and discrete modes reset to their case
initial values before a compiled problem is executed again.

Event surfaces use the core checked-evaluation contract. Expression-domain or property-package
failures therefore produce an explicit integration diagnostic instead of being treated as a
non-crossing value.

The safe contract does not currently expose thermochemistry calls, artifact access, parameter
templates, species-expanded material variables, or derivative-free transport-property calls.
Deployment code can register definitions at the trusted composition root through
`register_expression_component`; application code can supply the same safe definition through a
request bundle.

## Team-owned revisions

The generic engineering-artifact revision API accepts
`artifact_type=thermox.expression_component` with the expression-component v5 schema. The
JSON payload contains physical
template metadata (`template_kind`, `display_name`, and `category`), executable `kind`, `version`,
`model_name`, `ports`, `parameters`, and `equations`; schema identity remains revision metadata.
Mode-aware payloads use `default_mode` and `modes` in place of the top-level equation arrays.
Creation canonicalizes the payload, validates the descriptor and safe grammar, stores it through
the provider-neutral artifact-content store, and publishes an immutable Team/project-owned
revision with a SHA-256 checksum.

Study `artifact_revision_ids` can pin component definitions and performance maps
together. Resolution verifies content integrity, reconstructs the component bundle, and records
the component artifact revision in job and execution provenance. The worker receives both the
exact definition and its immutable identity.

`GET /api/v1/projects/{project_id}/component-catalog` resolves the revision history of every
logical expression-component artifact, composes each definition against the deployed runtime,
and returns its ordinary component descriptor together with the source artifact revision.
The browser presents the latest revision of each kind in the runtime component library and labels
it as project-owned, while retaining historical descriptors so older topology revisions remain
resolvable. Adding an instance stores the usual `kind` and `version` in topology; validation and
Study authoring adds the matching source revision to the exact artifact set.
The canvas library can publish a safe definition or revise a project-owned definition through
this same service contract. Revisions preserve the logical artifact and component kind, and must
publish a new component version. The service rejects duplicate project kinds and reused
kind/version pairs so an older topology cannot silently resolve to changed equations.
Canvas rendering, instance editing, validation, and run authoring resolve the descriptor matching
the topology instance's exact kind/version rather than substituting the newest project revision.
The browser only enables new run authoring after the service compiler has validated that exact
topology, case, and artifact-revision set.

Approval policy, additional property functions backed by derivative contracts, cross-component
reset maps, variable-structure transitions, and richer equation syntax assistance require later
contracts. Cases continue to own system-level threshold events and transitions; component-owned
events remain deliberately local to the declaring instance. Arbitrary Python is not part of this
path.
