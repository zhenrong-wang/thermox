BEGIN;

CREATE SEQUENCE IF NOT EXISTS thermox_project_id_seq;
CREATE SEQUENCE IF NOT EXISTS thermox_model_revision_id_seq;

CREATE TABLE IF NOT EXISTS thermox_projects (
    project_id text PRIMARY KEY DEFAULT (
        'project-' || lpad(
            nextval('thermox_project_id_seq')::text,
            12,
            '0'
        )
    ),
    team_id text NOT NULL CHECK (team_id <> ''),
    name text NOT NULL CHECK (
        name <> '' AND length(name) <= 200
    ),
    description text NOT NULL DEFAULT '' CHECK (
        length(description) <= 4000
    ),
    created_by_user_id text NOT NULL
        CHECK (created_by_user_id <> ''),
    next_model_revision_number bigint NOT NULL DEFAULT 1
        CHECK (next_model_revision_number > 0),
    created_at timestamptz NOT NULL DEFAULT clock_timestamp(),
    CONSTRAINT thermox_projects_team_project_unique
        UNIQUE (team_id, project_id)
);

CREATE INDEX IF NOT EXISTS thermox_projects_team_created_idx
ON thermox_projects (team_id, created_at, project_id);

CREATE TABLE IF NOT EXISTS thermox_model_revisions (
    model_revision_id text PRIMARY KEY DEFAULT (
        'model-revision-' || lpad(
            nextval('thermox_model_revision_id_seq')::text,
            12,
            '0'
        )
    ),
    project_id text NOT NULL,
    team_id text NOT NULL,
    revision_number bigint NOT NULL CHECK (revision_number > 0),
    parent_model_revision_id text,
    model_schema_version text NOT NULL
        CHECK (model_schema_version <> ''),
    model_id text NOT NULL CHECK (model_id <> ''),
    model_revision_label text NOT NULL DEFAULT '',
    canonical_model_payload text NOT NULL CHECK (
        jsonb_typeof(canonical_model_payload::jsonb) = 'object'
    ),
    checksum text NOT NULL CHECK (
        checksum ~ '^sha256:[0-9a-f]{64}$'
    ),
    created_by_user_id text NOT NULL
        CHECK (created_by_user_id <> ''),
    created_at timestamptz NOT NULL DEFAULT clock_timestamp(),
    CONSTRAINT thermox_model_revisions_project_fk
        FOREIGN KEY (team_id, project_id)
        REFERENCES thermox_projects (team_id, project_id),
    CONSTRAINT thermox_model_revisions_project_number_unique
        UNIQUE (team_id, project_id, revision_number),
    CONSTRAINT thermox_model_revisions_scoped_id_unique
        UNIQUE (team_id, project_id, model_revision_id),
    CONSTRAINT thermox_model_revisions_parent_fk
        FOREIGN KEY (
            team_id,
            project_id,
            parent_model_revision_id
        )
        REFERENCES thermox_model_revisions (
            team_id,
            project_id,
            model_revision_id
        )
);

CREATE INDEX IF NOT EXISTS
    thermox_model_revisions_project_created_idx
ON thermox_model_revisions (
    team_id,
    project_id,
    revision_number
);

COMMIT;
