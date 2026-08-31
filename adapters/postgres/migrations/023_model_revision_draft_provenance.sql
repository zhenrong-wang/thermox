BEGIN;

CREATE UNIQUE INDEX IF NOT EXISTS
    thermox_artifact_revisions_id_checksum_unique
ON thermox_artifact_revisions (artifact_revision_id, checksum);

ALTER TABLE thermox_model_revisions
    ADD COLUMN IF NOT EXISTS source_draft_artifact_revision_id text,
    ADD COLUMN IF NOT EXISTS source_draft_checksum text;

ALTER TABLE thermox_model_revisions
    DROP CONSTRAINT IF EXISTS
        thermox_model_revisions_source_draft_checksum_valid;

ALTER TABLE thermox_model_revisions
    ADD CONSTRAINT thermox_model_revisions_source_draft_checksum_valid
    CHECK (
        (source_draft_artifact_revision_id IS NULL AND
         source_draft_checksum IS NULL) OR
        (source_draft_artifact_revision_id IS NOT NULL AND
         source_draft_checksum ~ '^sha256:[0-9a-f]{64}$')
    );

ALTER TABLE thermox_model_revisions
    DROP CONSTRAINT IF EXISTS
        thermox_model_revisions_source_draft_fk;

ALTER TABLE thermox_model_revisions
    ADD CONSTRAINT thermox_model_revisions_source_draft_fk
    FOREIGN KEY (
        source_draft_artifact_revision_id,
        source_draft_checksum
    )
    REFERENCES thermox_artifact_revisions (
        artifact_revision_id,
        checksum
    );

COMMIT;
