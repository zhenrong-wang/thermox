BEGIN;

ALTER TABLE thermox_case_revisions
    ADD CONSTRAINT
        thermox_case_revisions_model_id_unique
    UNIQUE (
        team_id,
        project_id,
        model_revision_id,
        case_revision_id
    );

ALTER TABLE thermox_artifact_revisions
    ADD CONSTRAINT
        thermox_artifact_revisions_project_id_unique
    UNIQUE (
        team_id,
        project_id,
        artifact_revision_id
    );

CREATE SEQUENCE IF NOT EXISTS
    thermox_run_configuration_revision_id_seq;

CREATE TABLE IF NOT EXISTS
    thermox_run_configuration_revisions (
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
        revision_number bigint NOT NULL
            CHECK (revision_number > 0),
        parent_run_configuration_revision_id text,
        model_revision_id text NOT NULL,
        case_revision_id text NOT NULL,
        mode text NOT NULL CHECK (
            mode IN ('steady', 'transient')
        ),
        steady_solver_payload text NOT NULL CHECK (
            jsonb_typeof(steady_solver_payload::jsonb) = 'object'
        ),
        transient_solver_payload text NOT NULL CHECK (
            jsonb_typeof(transient_solver_payload::jsonb) =
                'object'
        ),
        checksum text NOT NULL CHECK (
            checksum ~ '^sha256:[0-9a-f]{64}$'
        ),
        created_by_user_id text NOT NULL
            CHECK (created_by_user_id <> ''),
        created_at timestamptz NOT NULL DEFAULT clock_timestamp(),
        CONSTRAINT thermox_run_configurations_model_fk
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
        CONSTRAINT thermox_run_configurations_case_fk
            FOREIGN KEY (
                team_id,
                project_id,
                model_revision_id,
                case_revision_id
            )
            REFERENCES thermox_case_revisions (
                team_id,
                project_id,
                model_revision_id,
                case_revision_id
            ),
        CONSTRAINT thermox_run_configurations_number_unique
            UNIQUE (
                team_id,
                project_id,
                run_configuration_id,
                revision_number
            ),
        CONSTRAINT thermox_run_configurations_scoped_id_unique
            UNIQUE (
                team_id,
                project_id,
                run_configuration_id,
                run_configuration_revision_id
            ),
        CONSTRAINT thermox_run_configurations_project_id_unique
            UNIQUE (
                team_id,
                project_id,
                run_configuration_revision_id
            ),
        CONSTRAINT thermox_run_configurations_parent_fk
            FOREIGN KEY (
                team_id,
                project_id,
                run_configuration_id,
                parent_run_configuration_revision_id
            )
            REFERENCES thermox_run_configuration_revisions (
                team_id,
                project_id,
                run_configuration_id,
                run_configuration_revision_id
            )
    );

CREATE TABLE IF NOT EXISTS
    thermox_run_configuration_artifacts (
        run_configuration_revision_id text NOT NULL,
        project_id text NOT NULL,
        team_id text NOT NULL,
        position integer NOT NULL CHECK (position >= 0),
        artifact_revision_id text NOT NULL,
        PRIMARY KEY (
            team_id,
            project_id,
            run_configuration_revision_id,
            position
        ),
        CONSTRAINT thermox_run_configuration_artifacts_run_fk
            FOREIGN KEY (
                team_id,
                project_id,
                run_configuration_revision_id
            )
            REFERENCES thermox_run_configuration_revisions (
                team_id,
                project_id,
                run_configuration_revision_id
            ),
        CONSTRAINT
            thermox_run_configuration_artifacts_artifact_fk
            FOREIGN KEY (
                team_id,
                project_id,
                artifact_revision_id
            )
            REFERENCES thermox_artifact_revisions (
                team_id,
                project_id,
                artifact_revision_id
            ),
        CONSTRAINT
            thermox_run_configuration_artifacts_revision_unique
            UNIQUE (
                team_id,
                project_id,
                run_configuration_revision_id,
                artifact_revision_id
            )
    );

CREATE INDEX IF NOT EXISTS
    thermox_run_configurations_project_created_idx
ON thermox_run_configuration_revisions (
    team_id,
    project_id,
    run_configuration_id,
    revision_number
);

COMMIT;
