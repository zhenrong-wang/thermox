BEGIN;

CREATE SEQUENCE IF NOT EXISTS thermox_artifact_revision_id_seq;

CREATE TABLE IF NOT EXISTS thermox_artifact_revisions (
    artifact_revision_id text PRIMARY KEY DEFAULT (
        'artifact-revision-' || lpad(
            nextval('thermox_artifact_revision_id_seq')::text,
            12,
            '0'
        )
    ),
    project_id text NOT NULL,
    team_id text NOT NULL,
    artifact_id text NOT NULL CHECK (artifact_id <> ''),
    revision_number bigint NOT NULL CHECK (revision_number > 0),
    parent_artifact_revision_id text,
    artifact_type text NOT NULL CHECK (artifact_type <> ''),
    artifact_schema_version text NOT NULL
        CHECK (artifact_schema_version <> ''),
    object_key text NOT NULL CHECK (object_key <> ''),
    media_type text NOT NULL CHECK (media_type <> ''),
    byte_size bigint NOT NULL CHECK (byte_size > 0),
    checksum text NOT NULL CHECK (
        checksum ~ '^sha256:[0-9a-f]{64}$'
    ),
    created_by_user_id text NOT NULL
        CHECK (created_by_user_id <> ''),
    created_at timestamptz NOT NULL DEFAULT clock_timestamp(),
    CONSTRAINT thermox_artifact_revisions_project_fk
        FOREIGN KEY (team_id, project_id)
        REFERENCES thermox_projects (team_id, project_id),
    CONSTRAINT thermox_artifact_revisions_number_unique
        UNIQUE (
            team_id,
            project_id,
            artifact_id,
            revision_number
        ),
    CONSTRAINT thermox_artifact_revisions_scoped_id_unique
        UNIQUE (
            team_id,
            project_id,
            artifact_id,
            artifact_revision_id
        ),
    CONSTRAINT thermox_artifact_revisions_parent_fk
        FOREIGN KEY (
            team_id,
            project_id,
            artifact_id,
            parent_artifact_revision_id
        )
        REFERENCES thermox_artifact_revisions (
            team_id,
            project_id,
            artifact_id,
            artifact_revision_id
        )
);

CREATE INDEX IF NOT EXISTS
    thermox_artifact_revisions_project_created_idx
ON thermox_artifact_revisions (
    team_id,
    project_id,
    artifact_id,
    revision_number
);

COMMIT;
