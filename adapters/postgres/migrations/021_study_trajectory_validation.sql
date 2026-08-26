BEGIN;

ALTER TABLE thermox_study_revisions
    ADD COLUMN trajectory_validation_bindings_payload jsonb
    NOT NULL DEFAULT '[]'::jsonb;

ALTER TABLE thermox_study_revisions
    ADD CONSTRAINT thermox_study_trajectory_validation_array
    CHECK (
        jsonb_typeof(trajectory_validation_bindings_payload) = 'array'
    );

COMMIT;
