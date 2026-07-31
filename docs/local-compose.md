# Local Compose stack

The root `compose.yml` is the canonical local Thermox stack. It starts:

- PostgreSQL for durable platform state;
- a one-shot, checksum-aware database migration runner;
- MinIO and a one-shot private-bucket initializer;
- the Thermox API and calculation worker as separate native processes; and
- the web client behind an Nginx reverse proxy.

The dependency order is enforced by health and completion conditions:

```text
PostgreSQL healthy ──> migrations complete ──┐
                                             ├──> API healthy ──> web
MinIO healthy ───────> bucket initialized ───┤
                                             └──> worker
```

## Start and stop

Build and start the complete stack from the repository root:

```sh
docker compose up -d --build --wait
```

The native build compiles Cantera and Thermox serially to avoid monopolizing a development host.
The first build is therefore intentionally slower; Docker caches later builds.

Local endpoints:

- web workspace: `http://127.0.0.1:5173`
- API: `http://127.0.0.1:8080`
- MinIO API: `http://127.0.0.1:59000`
- MinIO console: `http://127.0.0.1:59001`
- PostgreSQL: `127.0.0.1:55432`

Useful lifecycle commands:

```sh
docker compose ps -a
docker compose logs -f api worker
docker compose restart api worker web
docker compose down
```

`docker compose down` retains the named PostgreSQL and MinIO volumes. Removing them with
`docker compose down --volumes` permanently deletes local platform state and objects.

## Database runner

`db-migrate` runs after PostgreSQL becomes healthy and must finish successfully before the API or
worker starts. It discovers the ordered SQL files in `adapters/postgres/migrations/`, records each
filename and SHA-256 checksum in `thermox_schema_migrations`, and fails if an already-recorded file
was modified.

Run or inspect it independently:

```sh
docker compose run --rm db-migrate
docker compose logs db-migrate
```

Migrations must remain safely rerunnable so an existing database can be adopted into the ledger.
Published migration files are immutable; schema changes require a new numbered file.

## Configuration

Compose provides safe loopback-only development defaults. Copy `.env.example` to `.env` to
override ports, local credentials, identity IDs, worker settings, the image tag, or the optional
Cantera build:

```sh
cp .env.example .env
```

The object-store binding remains provider-agnostic at the application boundary. This stack selects
the `s3-compatible` driver and configures MinIO as its endpoint; another S3-compatible provider can
be supplied through the same runtime configuration.

The API's local static identity and development credentials are not production authentication or
secrets management. A deployed environment must inject trusted identity context and managed
credentials at its gateway/runtime boundary.
