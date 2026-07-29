BEGIN;

CREATE SEQUENCE IF NOT EXISTS thermox_simulation_job_id_seq;
CREATE SEQUENCE IF NOT EXISTS thermox_simulation_job_queue_seq;

CREATE TABLE IF NOT EXISTS thermox_simulation_jobs (
    job_id text PRIMARY KEY DEFAULT (
        'job-' || lpad(
            nextval('thermox_simulation_job_id_seq')::text,
            12,
            '0'
        )
    ),
    team_id text NOT NULL CHECK (team_id <> ''),
    submitted_by_user_id text NOT NULL
        CHECK (submitted_by_user_id <> ''),
    idempotency_key text NOT NULL CHECK (idempotency_key <> ''),
    request_fingerprint text NOT NULL
        CHECK (request_fingerprint <> ''),
    request_payload jsonb NOT NULL,
    state text NOT NULL DEFAULT 'queued'
        CHECK (
            state IN (
                'queued',
                'running',
                'succeeded',
                'failed',
                'cancelled'
            )
        ),
    revision bigint NOT NULL DEFAULT 1 CHECK (revision > 0),
    worker_id text,
    execution_payload jsonb,
    error_payload jsonb,
    result_artifact_payload jsonb,
    queue_sequence bigint NOT NULL DEFAULT
        nextval('thermox_simulation_job_queue_seq'),
    created_at timestamptz NOT NULL DEFAULT clock_timestamp(),
    updated_at timestamptz NOT NULL DEFAULT clock_timestamp(),
    CONSTRAINT thermox_simulation_jobs_team_idempotency_key
        UNIQUE (team_id, idempotency_key),
    CONSTRAINT thermox_simulation_jobs_json_payloads CHECK (
        jsonb_typeof(request_payload) = 'object'
        AND (
            execution_payload IS NULL
            OR jsonb_typeof(execution_payload) = 'object'
        )
        AND (
            error_payload IS NULL
            OR jsonb_typeof(error_payload) = 'object'
        )
        AND (
            result_artifact_payload IS NULL
            OR jsonb_typeof(result_artifact_payload) = 'object'
        )
    ),
    CONSTRAINT thermox_simulation_jobs_worker_state CHECK (
        (
            state IN ('queued', 'cancelled')
            AND worker_id IS NULL
        )
        OR
        (
            state IN ('running', 'succeeded', 'failed')
            AND worker_id IS NOT NULL
            AND worker_id <> ''
        )
    ),
    CONSTRAINT thermox_simulation_jobs_terminal_payloads CHECK (
        (state = 'succeeded'
            AND execution_payload IS NOT NULL
            AND result_artifact_payload IS NOT NULL
            AND error_payload IS NULL)
        OR
        (state = 'failed'
            AND error_payload IS NOT NULL
            AND result_artifact_payload IS NULL)
        OR
        (state IN ('queued', 'running', 'cancelled')
            AND result_artifact_payload IS NULL
            AND error_payload IS NULL)
    )
);

CREATE INDEX IF NOT EXISTS
    thermox_simulation_jobs_claim_queue_idx
ON thermox_simulation_jobs (queue_sequence)
WHERE state = 'queued';

CREATE INDEX IF NOT EXISTS
    thermox_simulation_jobs_team_created_idx
ON thermox_simulation_jobs (team_id, created_at DESC);

COMMIT;
