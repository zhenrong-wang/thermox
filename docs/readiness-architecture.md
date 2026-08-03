# Calculation Readiness Architecture

## Purpose

Calculation readiness is an application-service decision over an exact model, case, runtime
catalog, and engineering-artifact set. It is not a browser boolean and it is not equivalent to
nonlinear convergence. Web, CLI, RPC, and embedded clients consume the same service-owned result.

`SimulationService::validate_model` returns a `ReadinessSummary` alongside its canonical model,
compilation preview, and diagnostics. The summary contains one authoritative `calculatable` gate,
layer states, and entity states. Project validation preserves the exact topology, case, and
artifact revision identities around that response.

## Layer contract

Readiness is reported in six stable layers:

1. `draft`: the document is parseable and has valid authoring identities;
2. `physical`: component types, physical parameters, media, materials, properties, and artifacts
   resolve against the active runtime;
3. `topology`: ports, connector contracts, cardinality, and connection compatibility are valid;
4. `study`: the selected case, mode, and calibration observation contracts are coherent;
5. `compilation`: the graph compiles and its equation structure is acceptable;
6. `execution`: the exact runtime and immutable resources needed to calculate are available.

Each layer is `ready`, `blocked`, or `not_evaluated`. `not_evaluated` is intentional: the service
does not claim that a downstream or independent layer passed merely because an earlier failure
stopped compilation. `calculatable` is true only when every layer has been proven ready for the
submitted revision set.

## Entity attribution

The response carries readiness for the system and every parsed component and connection.
Diagnostics preserve stable codes and add component, port, connection, and JSON-path attribution
where the compiler evidence identifies them. An affected entity is `blocked`; unaffected entities
remain `not_evaluated` after an interrupted validation instead of being presented as ready.

Successful compilation marks all entities ready. This makes component badges and canvas
navigation projections of service evidence, while allowing clients to calculate lightweight local
authoring hints before authoritative validation.

## Calculation gate

Thin clients must gate run authoring on all of the following:

- `readiness.calculatable` is true;
- the validated model and case revision IDs match the execution selection;
- the validated immutable artifact revision set matches the execution selection.

Checking `compilation.compiled` alone is insufficient. The web client therefore uses the readiness
gate and presents all six layer states. Durable job submission still resolves and snapshots the
exact Study and run configuration, providing a second server-side consistency boundary.

## Boundary with engineering acceptance

Readiness answers whether a calculation may run. Numerical diagnostics answer whether equations
converged. Engineering acceptance will answer whether completed physical results satisfy declared,
dimensioned criteria. These remain separate contracts so a converged calculation cannot be
misrepresented as an accepted engineering result.
