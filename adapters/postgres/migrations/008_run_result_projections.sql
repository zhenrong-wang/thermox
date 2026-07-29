BEGIN;

ALTER TABLE thermox_run_configuration_revisions
    ADD COLUMN IF NOT EXISTS result_projections_payload jsonb
        NOT NULL DEFAULT '[]'::jsonb;

ALTER TABLE thermox_run_configuration_revisions
    DROP CONSTRAINT IF EXISTS
        thermox_run_configurations_result_projections_array,
    ADD CONSTRAINT
        thermox_run_configurations_result_projections_array
        CHECK (
            jsonb_typeof(result_projections_payload) = 'array'
        );

UPDATE thermox_simulation_jobs
SET request_payload = jsonb_set(
    jsonb_set(
        request_payload,
        '{result_projections}',
        coalesce(
            request_payload->'result_projections',
            '[]'::jsonb
        ),
        true
    ),
    '{schema_version}',
    '"thermox.job/v5"'::jsonb,
    true
);

ALTER TABLE thermox_simulation_jobs
    ADD COLUMN IF NOT EXISTS result_summary_payload jsonb;

ALTER TABLE thermox_simulation_jobs
    DROP CONSTRAINT IF EXISTS
        thermox_simulation_jobs_result_summary_state,
    ADD CONSTRAINT thermox_simulation_jobs_result_summary_state
        CHECK (
            result_summary_payload IS NULL
            OR (
                state = 'succeeded'
                AND jsonb_typeof(result_summary_payload) =
                    'object'
            )
        );

COMMIT;
