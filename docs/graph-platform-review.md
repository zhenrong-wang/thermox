# Graph Platform Architecture Review

_Decision date: 2026-07-28_

## Conclusion

Thermox is on the correct architectural path:

```text
CLI / Web / RPC
        ↓
thermox_service
        ↓
platform graph compiler
        ↓
component registry + property registry
        ↓
physics + numerical core
```

The numerical, physics, component, compiler, and synchronous service foundations are appropriate.
The present system is system-agnostic within its built-in registries and connector domains. It is
not yet a user-extensible graph service because registry discovery, runtime composition, semantic
validation, connector extensibility, structured diagnostics, and graph-native results are not
complete at the service boundary.

## Existing strengths

- Generic steady nonlinear and transient DAE numerical contracts.
- Registered component models own their descriptors and equations.
- Multiple fluid backends implement a common property-package interface.
- Typed ports, media compatibility, parameter bounds, property capabilities, cases, and
  degree-of-freedom checks are enforced during graph compilation.
- Deterministic exact reduction supports closed thermal loops.
- CLI, future RPC, and future UI adapters depend on the application service rather than owning
  simulation logic.
- Closed IF97 Rankine and transient-storage models prove the end-to-end path.

## Critical gaps

### Registry discovery and composition

The engine has queryable component descriptors, but the service does not expose component,
property, or connector-domain catalogs. The synchronous service also constructs only the default
registries, so deployment-specific or third-party native registrations cannot be injected.

Decision: introduce an immutable service runtime assembled at the application composition root.
The service exposes a versioned catalog snapshot and fingerprint. Native registration remains an
explicit integration concern rather than leaking engine types into RPC DTOs.

### Validation and compilation analysis

The current validation command parses and canonicalizes a document but does not resolve component
kinds, parameters, properties, capabilities, cases, simulation modes, or degrees of freedom.

Decision: validation becomes staged and compile-aware. It returns structured diagnostics and a
compilation summary without invoking the numerical solver. Parsing, catalog validation, topology,
parameters, properties, cases, and structural analysis remain distinguishable stages.

### Graph schema authority — completed

Component instances currently repeat port names, domains, and directions already owned by the
component descriptor. This permits drift between a model document and the registry.

`thermox.model/v2` derives port contracts from the registered component type. Instances retain only
identity, kind/version requirement, per-fluid-port medium bindings, parameters, and annotations.
No backward-compatibility adapter was retained.

### Connector semantics — first contract completed

Canonical connector variables are hard-coded for fluid, heat, shaft, signal, and control domains.
Connection kinds are stored but compilation currently applies the same equality semantics for a
domain. Port cardinality is not represented.

Versioned connector-domain descriptors now expose canonical variables. Connection kinds must match
their endpoint domain, port directions are compiler-validated, and registry descriptors declare
maximum connection counts. Direct fan-out is rejected; junctions, pressure loss, heat transfer,
and other physical behavior remain explicit components. Moving canonical domain definitions out of
the compiler into an injectable domain registry remains a later extensibility step.

### Diagnostics

Compiler exceptions currently collapse into one service error string.

Decision: service diagnostics have stable codes, severity, stage, JSON path, entity identifiers,
message, and suggestions. The first slice supplies the envelope and stage boundaries; compiler
internals will progressively emit precise diagnostic records.

### Reproducibility

Model component versions are parsed but not enforced. Property packages expose names but not
implementation versions. Case solver options are parsed but are not the service execution
authority.

Decision: compilation resolves and records component, property, connector-domain, schema, solver,
runtime-catalog, and build versions. Requested versions must be checked. Solver-option ownership
must be unambiguous.

### Results — graph contract completed

`thermox.result/v3` makes steady solutions, transient samples, and events graph-addressable by
stable component and port identity for every domain. It carries primary variables, fluid-derived
properties, internal states, and transient derivatives. Typed component-metric, system-balance,
and KPI collections are reserved in the same contract for registered evaluators.

## Ordered implementation

1. Immutable injectable runtime and catalog fingerprint.
2. Component, property, and connector-domain catalog service contracts.
3. Compile-aware validation and compilation preview with structured diagnostics.
4. Registry-derived model schema and formal connection/cardinality semantics. ✅
5. Complete version enforcement and provenance. ✅
6. Graph-native steady and transient result contracts. ✅
7. Asynchronous jobs, repository ports, RPC mapping, then visual graph UI.

Database and frontend implementation should not precede the catalog, validation, and graph
contracts because doing so would force business rules into adapters.
