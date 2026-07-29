BEGIN;

CREATE SEQUENCE IF NOT EXISTS thermox_case_revision_id_seq;

CREATE TABLE IF NOT EXISTS thermox_case_revisions (
    case_revision_id text PRIMARY KEY DEFAULT (
        'case-revision-' || lpad(
            nextval('thermox_case_revision_id_seq')::text,
            12,
            '0'
        )
    ),
    model_revision_id text NOT NULL,
    project_id text NOT NULL,
    team_id text NOT NULL,
    case_id text NOT NULL CHECK (case_id <> ''),
    revision_number bigint NOT NULL CHECK (revision_number > 0),
    parent_case_revision_id text,
    mode text NOT NULL CHECK (mode <> ''),
    canonical_case_payload text NOT NULL CHECK (
        jsonb_typeof(canonical_case_payload::jsonb) = 'object'
    ),
    checksum text NOT NULL CHECK (
        checksum ~ '^sha256:[0-9a-f]{64}$'
    ),
    created_by_user_id text NOT NULL
        CHECK (created_by_user_id <> ''),
    created_at timestamptz NOT NULL DEFAULT clock_timestamp(),
    CONSTRAINT thermox_case_revisions_model_fk
        FOREIGN KEY (
            team_id,
            project_id,
            model_revision_id
        )
        REFERENCES thermox_model_revisions (
            team_id,
            project_id,
            model_revision_id
        ),
    CONSTRAINT thermox_case_revisions_number_unique
        UNIQUE (
            team_id,
            project_id,
            model_revision_id,
            case_id,
            revision_number
        ),
    CONSTRAINT thermox_case_revisions_scoped_id_unique
        UNIQUE (
            team_id,
            project_id,
            model_revision_id,
            case_id,
            case_revision_id
        ),
    CONSTRAINT thermox_case_revisions_parent_fk
        FOREIGN KEY (
            team_id,
            project_id,
            model_revision_id,
            case_id,
            parent_case_revision_id
        )
        REFERENCES thermox_case_revisions (
            team_id,
            project_id,
            model_revision_id,
            case_id,
            case_revision_id
        )
);

CREATE INDEX IF NOT EXISTS
    thermox_case_revisions_model_created_idx
ON thermox_case_revisions (
    team_id,
    project_id,
    model_revision_id,
    case_id,
    revision_number
);

COMMIT;
