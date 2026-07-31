#!/bin/sh
set -eu

: "${PGHOST:?PGHOST is required}"
: "${PGDATABASE:?PGDATABASE is required}"
: "${PGUSER:?PGUSER is required}"
: "${PGPASSWORD:?PGPASSWORD is required}"

MIGRATION_DIR=${THERMOX_MIGRATION_DIR:-/migrations}

psql -v ON_ERROR_STOP=1 <<'SQL'
CREATE TABLE IF NOT EXISTS thermox_schema_migrations (
    version text PRIMARY KEY,
    checksum_sha256 text NOT NULL CHECK (
        checksum_sha256 ~ '^[0-9a-f]{64}$'
    ),
    applied_at timestamptz NOT NULL DEFAULT clock_timestamp()
);
SQL

for migration in "${MIGRATION_DIR}"/*.sql; do
    version=$(basename "${migration}")
    checksum=$(sha256sum "${migration}" | awk '{print $1}')
    applied_checksum=$(
        printf '%s\n' \
            "SELECT checksum_sha256 FROM thermox_schema_migrations WHERE version = :'version';" \
        | psql -v ON_ERROR_STOP=1 -At \
            -v version="${version}"
    )

    if [ -n "${applied_checksum}" ]; then
        if [ "${applied_checksum}" != "${checksum}" ]; then
            echo "migration checksum mismatch: ${version}" >&2
            exit 1
        fi
        echo "migration already applied: ${version}"
        continue
    fi

    echo "applying migration: ${version}"
    psql -v ON_ERROR_STOP=1 -f "${migration}"
    printf '%s\n' \
        "INSERT INTO thermox_schema_migrations(version, checksum_sha256) VALUES (:'version', :'checksum');" \
    | psql -v ON_ERROR_STOP=1 \
        -v version="${version}" \
        -v checksum="${checksum}"
done

echo "database migrations are current"
