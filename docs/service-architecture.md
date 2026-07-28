# Service Architecture

_Decision date: 2026-07-28_

## Boundary

`thermox_service` is the application and orchestration boundary for Thermox. It owns simulation
use cases, versioned command/result/error contracts, model serialization, provenance collection,
and translation between public data-transfer objects and engine types.

```text
CLI             Web / RPC             workers
 └───────────────┴───────────────────────┘
                     │
              thermox_service
              ├── validate model
              ├── run steady
              ├── run transient
              └── serialize results
                     │
                  platform
                     │
             physics + numeric core
```

The dependency direction is one-way. Interfaces may depend on `thermox_service`; the service
depends on the platform; the platform depends on physics and the numerical core. Engine layers do
not depend on services, transports, databases, or user interfaces.

## Public contract

The current synchronous service exposes:

- `ValidateModelRequest` / `ValidateModelResponse`;
- `SteadySimulationRequest` / `SteadySimulationResponse`;
- `TransientSimulationRequest` / `TransientSimulationResponse`;
- `thermox.command/v1`, `thermox.result/v1`, and `thermox.error/v1` contracts;
- stable operation status and error stage/code fields;
- component implementation, property package, model, case, and solver provenance;
- canonical model JSON and steady/transient result JSON.

The public service DTOs contain only standard C++ data types. Solver and compiler objects do not
cross this boundary. This keeps local callers simple and permits an RPC adapter to map wire
messages without importing engine internals.

## Interface responsibilities

The CLI owns argument parsing, file/stdin access, exit codes, and terminal rendering. It does not
parse models, resolve registries, compile graphs, invoke numerical kernels, derive properties, or
construct result contracts.

A future HTTP or RPC adapter follows the same rule: authenticate, decode, invoke one service use
case, encode the response. Long-running execution will use a separate job application service
which coordinates repositories and workers while reusing the synchronous simulation service.

## Persistence

Repository interfaces and job transactions belong beside the service layer. Database and object
storage adapters depend inward on those ports. They must never be introduced into the platform,
physics, or numerical libraries.
