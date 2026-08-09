BEGIN;

ALTER TABLE thermox_study_revisions
    ADD COLUMN artifact_operating_envelopes_payload jsonb
    NOT NULL DEFAULT '[]'::jsonb;

ALTER TABLE thermox_study_revisions
    ADD CONSTRAINT thermox_study_operating_envelopes_array
    CHECK (
        jsonb_typeof(artifact_operating_envelopes_payload) = 'array'
    );

COMMIT;
