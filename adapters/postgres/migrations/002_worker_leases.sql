BEGIN;

ALTER TABLE thermox_simulation_jobs
    ADD COLUMN IF NOT EXISTS attempt integer NOT NULL DEFAULT 0,
    ADD COLUMN IF NOT EXISTS lease_expires_at timestamptz;

UPDATE thermox_simulation_jobs
SET
    attempt = CASE
        WHEN state IN ('running', 'succeeded', 'failed')
            THEN GREATEST(attempt, 1)
        ELSE attempt
    END,
    lease_expires_at = CASE
        WHEN state = 'running' THEN clock_timestamp()
        ELSE NULL
    END;

ALTER TABLE thermox_simulation_jobs
    DROP CONSTRAINT IF EXISTS
        thermox_simulation_jobs_attempt_nonnegative,
    ADD CONSTRAINT thermox_simulation_jobs_attempt_nonnegative
        CHECK (attempt >= 0);

ALTER TABLE thermox_simulation_jobs
    DROP CONSTRAINT IF EXISTS
        thermox_simulation_jobs_worker_state,
    ADD CONSTRAINT thermox_simulation_jobs_worker_state CHECK (
        (
            state IN ('queued', 'cancelled')
            AND worker_id IS NULL
            AND lease_expires_at IS NULL
        )
        OR
        (
            state = 'running'
            AND worker_id IS NOT NULL
            AND worker_id <> ''
            AND lease_expires_at IS NOT NULL
            AND attempt > 0
        )
        OR
        (
            state IN ('succeeded', 'failed')
            AND worker_id IS NOT NULL
            AND worker_id <> ''
            AND lease_expires_at IS NULL
            AND attempt > 0
        )
    );

CREATE INDEX IF NOT EXISTS
    thermox_simulation_jobs_expired_lease_idx
ON thermox_simulation_jobs (lease_expires_at)
WHERE state = 'running';

COMMIT;
