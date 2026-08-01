BEGIN;

-- Early-development contract reset: v3 run configurations own execution
-- policy only. Study revisions own physical inputs and output selection.
DROP TABLE thermox_run_configuration_artifacts;
DROP TABLE thermox_run_configuration_revisions;

CREATE TABLE thermox_run_configuration_revisions (
    run_configuration_revision_id text PRIMARY KEY DEFAULT (
        'run-configuration-revision-' || lpad(
            nextval(
                'thermox_run_configuration_revision_id_seq'
            )::text,
            12,
            '0'
        )
    ),
    run_configuration_id text NOT NULL
        CHECK (run_configuration_id <> ''),
    project_id text NOT NULL,
    team_id text NOT NULL,
    revision_number bigint NOT NULL CHECK (revision_number > 0),
    parent_run_configuration_revision_id text,
    study_revision_id text NOT NULL,
    steady_solver_payload text NOT NULL CHECK (
        jsonb_typeof(steady_solver_payload::jsonb) = 'object'
    ),
    transient_solver_payload text NOT NULL CHECK (
        jsonb_typeof(transient_solver_payload::jsonb) = 'object'
    ),
    checksum text NOT NULL CHECK (
        checksum ~ '^sha256:[0-9a-f]{64}$'
    ),
    created_by_user_id text NOT NULL
        CHECK (created_by_user_id <> ''),
    created_at timestamptz NOT NULL DEFAULT clock_timestamp(),
    CONSTRAINT thermox_run_configurations_study_fk
        FOREIGN KEY (team_id, project_id, study_revision_id)
        REFERENCES thermox_study_revisions (
            team_id, project_id, study_revision_id
        ),
    CONSTRAINT thermox_run_configurations_number_unique
        UNIQUE (
            team_id, project_id, run_configuration_id,
            revision_number
        ),
    CONSTRAINT thermox_run_configurations_scoped_id_unique
        UNIQUE (
            team_id, project_id, run_configuration_id,
            run_configuration_revision_id
        ),
    CONSTRAINT thermox_run_configurations_project_id_unique
        UNIQUE (
            team_id, project_id, run_configuration_revision_id
        ),
    CONSTRAINT thermox_run_configurations_parent_fk
        FOREIGN KEY (
            team_id, project_id, run_configuration_id,
            parent_run_configuration_revision_id
        )
        REFERENCES thermox_run_configuration_revisions (
            team_id, project_id, run_configuration_id,
            run_configuration_revision_id
        )
);

CREATE INDEX thermox_run_configurations_project_created_idx
ON thermox_run_configuration_revisions (
    team_id, project_id, run_configuration_id, revision_number
);

COMMIT;
