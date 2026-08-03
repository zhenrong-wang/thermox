BEGIN;

ALTER TABLE thermox_study_revisions
    ADD COLUMN IF NOT EXISTS acceptance_criteria_payload jsonb
    NOT NULL DEFAULT '[]'::jsonb;

ALTER TABLE thermox_study_revisions
    DROP CONSTRAINT IF EXISTS
        thermox_study_acceptance_criteria_array;

ALTER TABLE thermox_study_revisions
    ADD CONSTRAINT thermox_study_acceptance_criteria_array
    CHECK (jsonb_typeof(acceptance_criteria_payload) = 'array');

COMMIT;
