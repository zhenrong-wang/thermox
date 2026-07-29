BEGIN;

ALTER TABLE thermox_simulation_jobs
    ADD COLUMN IF NOT EXISTS project_id text,
    ADD COLUMN IF NOT EXISTS run_configuration_revision_id text;

UPDATE thermox_simulation_jobs
SET
    project_id = NULLIF(
        request_payload #>>
            '{source_revisions,project_id}',
        ''),
    run_configuration_revision_id = NULLIF(
        request_payload #>>
            '{source_revisions,run_configuration_revision_id}',
        '')
WHERE
    project_id IS NULL
    OR run_configuration_revision_id IS NULL;

ALTER TABLE thermox_simulation_jobs
    DROP CONSTRAINT IF EXISTS
        thermox_simulation_jobs_project_id_nonempty,
    ADD CONSTRAINT thermox_simulation_jobs_project_id_nonempty
        CHECK (project_id IS NULL OR project_id <> ''),
    DROP CONSTRAINT IF EXISTS
        thermox_simulation_jobs_run_configuration_nonempty,
    ADD CONSTRAINT
        thermox_simulation_jobs_run_configuration_nonempty
        CHECK (
            run_configuration_revision_id IS NULL
            OR run_configuration_revision_id <> ''
        );

CREATE INDEX IF NOT EXISTS
    thermox_simulation_jobs_team_created_job_idx
ON thermox_simulation_jobs (
    team_id,
    created_at DESC,
    job_id DESC
);

CREATE INDEX IF NOT EXISTS
    thermox_simulation_jobs_team_project_history_idx
ON thermox_simulation_jobs (
    team_id,
    project_id,
    created_at DESC,
    job_id DESC
);

CREATE INDEX IF NOT EXISTS
    thermox_simulation_jobs_team_configuration_history_idx
ON thermox_simulation_jobs (
    team_id,
    run_configuration_revision_id,
    created_at DESC,
    job_id DESC
);

CREATE INDEX IF NOT EXISTS
    thermox_simulation_jobs_team_state_history_idx
ON thermox_simulation_jobs (
    team_id,
    state,
    created_at DESC,
    job_id DESC
);

COMMIT;
