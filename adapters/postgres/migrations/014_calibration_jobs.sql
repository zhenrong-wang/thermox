BEGIN;

-- Early-development contract reset: job v8 adds calibration workloads and
-- exact calibration provenance. Queued or historical v7 payloads cannot be
-- decoded by the v8 worker contract.
DELETE FROM thermox_simulation_jobs;

ALTER TABLE thermox_simulation_jobs
    ADD COLUMN IF NOT EXISTS calibration_revision_id text;

ALTER TABLE thermox_simulation_jobs
    DROP CONSTRAINT IF EXISTS
        thermox_simulation_jobs_calibration_revision_nonempty,
    ADD CONSTRAINT
        thermox_simulation_jobs_calibration_revision_nonempty
        CHECK (
            calibration_revision_id IS NULL
            OR calibration_revision_id <> ''
        );

CREATE INDEX IF NOT EXISTS
    thermox_simulation_jobs_team_calibration_history_idx
ON thermox_simulation_jobs (
    team_id,
    calibration_revision_id,
    created_at DESC,
    job_id DESC
);

COMMIT;
