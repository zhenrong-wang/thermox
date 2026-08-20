BEGIN;

ALTER TABLE thermox_simulation_jobs
    ADD COLUMN IF NOT EXISTS reconciliation_revision_id text;

ALTER TABLE thermox_simulation_jobs
    DROP CONSTRAINT IF EXISTS thermox_jobs_reconciliation_revision_id_check;

ALTER TABLE thermox_simulation_jobs
    ADD CONSTRAINT thermox_jobs_reconciliation_revision_id_check CHECK (
        reconciliation_revision_id IS NULL
        OR reconciliation_revision_id <> '');

CREATE INDEX IF NOT EXISTS thermox_jobs_reconciliation_history_idx
ON thermox_simulation_jobs (
    team_id,
    reconciliation_revision_id,
    created_at DESC,
    job_id DESC);

COMMIT;
