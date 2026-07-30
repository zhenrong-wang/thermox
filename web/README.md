# Thermox Web Workspace

This directory contains the thin React/TypeScript client for the Thermox service platform. The
client does not parse or compile physical models, infer ports, validate connections, apply graph
edits, or run simulations. Those responsibilities remain behind the versioned HTTP application
contract.

The workspace currently provides:

- Team-scoped project and immutable topology-revision browsing;
- a graph canvas whose nodes and typed ports come from persisted topology plus the runtime catalog;
- a searchable component palette generated from `thermox.catalog/v4`;
- catalog-generated component forms for SI parameters, medium/material ports, and artifact bindings;
- property-registry-driven fluid creation with backend, substance, version, and capability metadata;
- typed project-artifact selectors for performance maps and other declared component resources;
- typed port-to-port connection authoring using registered connector contracts;
- selection-driven component and connection inspection;
- component and connection updates that preserve instance identity;
- guarded component and connection removal, including explicit component cascade confirmation;
- atomic publication of every accepted edit as a new immutable child revision;
- topology-scoped operating-case creation and exact case-revision browsing;
- immutable case metadata and scalar-map edits with service-normalized engineering units;
- exact artifact-revision selection and compile-aware validation with structured diagnostics;
- immutable run-configuration creation and revision history;
- complete steady/transient solver policy and generic result-projection authoring;
- idempotent durable job submission for an exact run-configuration revision;
- Team-scoped, cursor-paginated execution history with state filtering;
- optimistic queued/running-job cancellation, structured worker errors, result summaries, and
  result-artifact manifests;
- bounded four-second refresh only while the Runs workspace contains an active job;
- on-demand retrieval of successful `thermox.result/v3` artifacts through the service;
- read-only projected-result overlays on the bound system topology;
- system balance, KPI, component, internal-state, and port/stream result tables;
- transient trajectory sample selection and event inspection using the same graph-native contract;
- persistent SI/engineering display profiles shared across authoring, inspection, run summaries,
  graph overlays, and result tables;
- reversible catalog-driven dimension conversion with SI publication, extension-defined unit
  support, and a conservative unavailable-catalog fallback;
- API, loading, empty, and revision-integrity states.

Start the durable API on `127.0.0.1:8080`, then:

```sh
cd web
npm install
npm run dev
```

The Vite development server publishes `http://127.0.0.1:5173` and proxies `/api` and `/healthz` to
the API. To use another local port:

```sh
THERMOX_API_URL=http://127.0.0.1:58080 npm run dev
```

Verification is intentionally bounded:

```sh
npm run typecheck
npm test
npm run build
```

Production deployment, authentication, and gateway routing are not configured yet. The client
therefore remains a local development surface.
