# Thermox Web Workspace

This directory contains the thin React/TypeScript client for the Thermox service platform. The
client does not parse or compile physical models, infer ports, validate connections, apply graph
edits, or run simulations. Those responsibilities remain behind the versioned HTTP application
contract.

The first slice provides:

- Team-scoped project and immutable topology-revision browsing;
- a graph canvas whose nodes and typed ports come from persisted topology plus the runtime catalog;
- a searchable component palette generated from `thermox.catalog/v3`;
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
npm run build
```

Production deployment, authentication, and gateway routing are not configured yet. The client
therefore remains a local development surface.
