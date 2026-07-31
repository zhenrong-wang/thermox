BEGIN;

UPDATE thermox_simulation_jobs
SET request_payload = jsonb_set(
    jsonb_set(
        request_payload,
        '{components}',
        coalesce(
            request_payload->'components',
            '{"expression_components":[]}'::jsonb
        ),
        true
    ),
    '{schema_version}',
    '"thermox.job/v6"'::jsonb,
    true
);

COMMIT;
