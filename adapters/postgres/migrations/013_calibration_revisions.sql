BEGIN;

CREATE SEQUENCE IF NOT EXISTS thermox_calibration_revision_id_seq;

CREATE TABLE IF NOT EXISTS thermox_calibration_revisions (
    calibration_revision_id text PRIMARY KEY DEFAULT (
        'calibration-revision-' || lpad(
            nextval('thermox_calibration_revision_id_seq')::text,
            12,
            '0'
        )
    ),
    calibration_id text NOT NULL CHECK (calibration_id <> ''),
    project_id text NOT NULL,
    team_id text NOT NULL,
    revision_number bigint NOT NULL CHECK (revision_number > 0),
    parent_calibration_revision_id text,
    model_revision_id text NOT NULL,
    definition_payload jsonb NOT NULL CHECK (
        definition_payload->>'schema_version' = 'thermox.calibration/v1'
        AND jsonb_typeof(definition_payload->'calibration') = 'object'
    ),
    solver_payload jsonb NOT NULL CHECK (
        jsonb_typeof(solver_payload) = 'object'
    ),
    checksum text NOT NULL CHECK (checksum ~ '^sha256:[0-9a-f]{64}$'),
    created_by_user_id text NOT NULL CHECK (created_by_user_id <> ''),
    created_at timestamptz NOT NULL DEFAULT clock_timestamp(),
    CONSTRAINT thermox_calibrations_model_fk
        FOREIGN KEY (team_id, project_id, model_revision_id)
        REFERENCES thermox_model_revisions (
            team_id, project_id, model_revision_id
        ),
    CONSTRAINT thermox_calibrations_number_unique
        UNIQUE (team_id, project_id, calibration_id, revision_number),
    CONSTRAINT thermox_calibrations_scoped_id_unique
        UNIQUE (
            team_id, project_id, calibration_id,
            calibration_revision_id
        ),
    CONSTRAINT thermox_calibrations_project_id_unique
        UNIQUE (team_id, project_id, calibration_revision_id),
    CONSTRAINT thermox_calibrations_parent_fk
        FOREIGN KEY (
            team_id, project_id, calibration_id,
            parent_calibration_revision_id
        )
        REFERENCES thermox_calibration_revisions (
            team_id, project_id, calibration_id,
            calibration_revision_id
        )
);

CREATE TABLE IF NOT EXISTS thermox_calibration_studies (
    calibration_revision_id text NOT NULL,
    project_id text NOT NULL,
    team_id text NOT NULL,
    role text NOT NULL CHECK (role IN ('training', 'validation')),
    position integer NOT NULL CHECK (position >= 0),
    study_revision_id text NOT NULL,
    PRIMARY KEY (
        team_id, project_id, calibration_revision_id, role, position
    ),
    CONSTRAINT thermox_calibration_studies_calibration_fk
        FOREIGN KEY (team_id, project_id, calibration_revision_id)
        REFERENCES thermox_calibration_revisions (
            team_id, project_id, calibration_revision_id
        ),
    CONSTRAINT thermox_calibration_studies_study_fk
        FOREIGN KEY (team_id, project_id, study_revision_id)
        REFERENCES thermox_study_revisions (
            team_id, project_id, study_revision_id
        ),
    CONSTRAINT thermox_calibration_studies_revision_unique
        UNIQUE (
            team_id, project_id, calibration_revision_id,
            study_revision_id
        )
);

CREATE INDEX IF NOT EXISTS thermox_calibrations_project_revision_idx
ON thermox_calibration_revisions (
    team_id, project_id, calibration_id, revision_number
);

COMMIT;
