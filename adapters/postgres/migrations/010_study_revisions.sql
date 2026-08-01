BEGIN;

CREATE SEQUENCE IF NOT EXISTS thermox_study_revision_id_seq;

CREATE TABLE IF NOT EXISTS thermox_study_revisions (
    study_revision_id text PRIMARY KEY DEFAULT (
        'study-revision-' || lpad(
            nextval('thermox_study_revision_id_seq')::text,
            12,
            '0'
        )
    ),
    study_id text NOT NULL CHECK (study_id <> ''),
    project_id text NOT NULL,
    team_id text NOT NULL,
    revision_number bigint NOT NULL CHECK (revision_number > 0),
    parent_study_revision_id text,
    model_revision_id text NOT NULL,
    case_revision_id text NOT NULL,
    intent text NOT NULL CHECK (
        intent IN (
            'steady_state_design',
            'steady_state_off_design',
            'dynamic_initialization',
            'dynamic_transient'
        )
    ),
    result_projections_payload jsonb NOT NULL DEFAULT '[]'::jsonb
        CHECK (jsonb_typeof(result_projections_payload) = 'array'),
    checksum text NOT NULL CHECK (
        checksum ~ '^sha256:[0-9a-f]{64}$'
    ),
    created_by_user_id text NOT NULL CHECK (created_by_user_id <> ''),
    created_at timestamptz NOT NULL DEFAULT clock_timestamp(),
    CONSTRAINT thermox_studies_model_fk
        FOREIGN KEY (team_id, project_id, model_revision_id)
        REFERENCES thermox_model_revisions (
            team_id, project_id, model_revision_id
        ),
    CONSTRAINT thermox_studies_case_fk
        FOREIGN KEY (
            team_id, project_id, model_revision_id, case_revision_id
        )
        REFERENCES thermox_case_revisions (
            team_id, project_id, model_revision_id, case_revision_id
        ),
    CONSTRAINT thermox_studies_number_unique
        UNIQUE (team_id, project_id, study_id, revision_number),
    CONSTRAINT thermox_studies_scoped_id_unique
        UNIQUE (
            team_id, project_id, study_id, study_revision_id
        ),
    CONSTRAINT thermox_studies_project_id_unique
        UNIQUE (team_id, project_id, study_revision_id),
    CONSTRAINT thermox_studies_parent_fk
        FOREIGN KEY (
            team_id, project_id, study_id, parent_study_revision_id
        )
        REFERENCES thermox_study_revisions (
            team_id, project_id, study_id, study_revision_id
        )
);

CREATE TABLE IF NOT EXISTS thermox_study_artifacts (
    study_revision_id text NOT NULL,
    project_id text NOT NULL,
    team_id text NOT NULL,
    position integer NOT NULL CHECK (position >= 0),
    artifact_revision_id text NOT NULL,
    PRIMARY KEY (team_id, project_id, study_revision_id, position),
    CONSTRAINT thermox_study_artifacts_study_fk
        FOREIGN KEY (team_id, project_id, study_revision_id)
        REFERENCES thermox_study_revisions (
            team_id, project_id, study_revision_id
        ),
    CONSTRAINT thermox_study_artifacts_artifact_fk
        FOREIGN KEY (team_id, project_id, artifact_revision_id)
        REFERENCES thermox_artifact_revisions (
            team_id, project_id, artifact_revision_id
        ),
    CONSTRAINT thermox_study_artifacts_revision_unique
        UNIQUE (
            team_id, project_id, study_revision_id,
            artifact_revision_id
        )
);

CREATE INDEX IF NOT EXISTS thermox_studies_project_revision_idx
ON thermox_study_revisions (
    team_id, project_id, study_id, revision_number
);

COMMIT;
