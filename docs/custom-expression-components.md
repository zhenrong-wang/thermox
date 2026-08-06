# Safe expression components

Thermox supports deployment-composed steady algebraic and index-1 transient components through
the versioned `thermox.expression_component/v2` and `thermox.expression_component/v3` contracts.
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
- `abs(x)`, `sqrt(x)`, `exp(x)`, `log(x)`, and `pow(x, y)`.

Each expression is a residual whose target value is zero. For example, a steady fluid
pressure-loss component can declare:

```text
outlet.m_dot - inlet.m_dot
outlet.p - inlet.p * parameter.pressure_ratio
outlet.h - inlet.h
```

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
file, process, network, allocation, reflection, loop, or callback syntax. Evaluation derives exact
sparse dependencies from referenced port variables and uses forward analytic differentiation for
the solver-owned Jacobian pattern. Domain failures such as division by zero or an invalid
logarithm are reported as recoverable physical evaluations.

Equation content has a deterministic implementation fingerprint. The service includes it in the
runtime catalog fingerprint, so changing a custom equation changes validation and execution
provenance even if its public descriptor is unchanged. `thermox.job/v10` also snapshots the complete
request-scoped bundle and includes it in the idempotency fingerprint. A worker therefore
reconstructs exactly the submitted component implementation after a process restart.

## Version 3 transient equations

Version 3 preserves the safe grammar and adds solver-owned DAE declarations. A definition can
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

## Version 2 boundary

Version 2 remains deliberately limited to steady algebraic equations over fixed-shape connector
domains. Version 3 removes the internal/transient-state restriction, but neither safe contract
currently exposes property-package calls, thermochemistry calls, artifact access, parameter
templates, or species-expanded material variables.
Deployment code can register definitions at the trusted composition root through
`register_expression_component`; application code can supply the same safe definition through a
request bundle.

## Team-owned revisions

The generic engineering-artifact revision API accepts
`artifact_type=thermox.expression_component` with an expression-component v2 or v3 schema. The
JSON payload contains physical
template metadata (`template_kind`, `display_name`, and `category`), executable `kind`, `version`,
`model_name`, `ports`, `parameters`, and `equations`; schema identity remains revision metadata.
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

Approval policy, dimension algebra across compound expressions, constrained property functions,
events/discrete modes, and richer equation syntax assistance require later versioned contracts.
Arbitrary Python is not part of this path.
