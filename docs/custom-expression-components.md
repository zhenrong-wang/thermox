# Safe expression components

Thermox supports deployment-composed steady algebraic components through the versioned
`thermox.expression_component/v1` contract. The component descriptor remains authoritative for
kind, version, typed ports, parameter dimensions, defaults, and bounds. Equations add residual
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
provenance even if its public descriptor is unchanged. `thermox.job/v6` also snapshots the complete
request-scoped bundle and includes it in the idempotency fingerprint. A worker therefore
reconstructs exactly the submitted component implementation after a process restart.

## Version 1 boundary

Version 1 is deliberately limited to steady algebraic equations over fixed-shape connector
domains. It does not expose property-package calls, thermochemistry calls, artifact access,
internal/transient states, parameter templates, or species-expanded material variables.
Deployment code can register definitions at the trusted composition root through
`register_expression_component`; application code can supply the same safe definition through a
request bundle. Persisted Team-owned definition revisions and their HTTP authoring/approval
workflow, dimension algebra across compound expressions, transient equations, and constrained
property functions require later versioned contracts. Arbitrary Python is not part of this path.
