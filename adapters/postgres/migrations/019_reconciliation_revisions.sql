BEGIN;

CREATE SEQUENCE IF NOT EXISTS thermox_reconciliation_revision_id_seq;

CREATE TABLE IF NOT EXISTS thermox_reconciliation_revisions (
    reconciliation_revision_id text PRIMARY KEY DEFAULT (
        'reconciliation-revision-' || lpad(
            nextval('thermox_reconciliation_revision_id_seq')::text,
            12, '0')),
    reconciliation_id text NOT NULL CHECK (reconciliation_id <> ''),
    project_id text NOT NULL,
    team_id text NOT NULL,
    revision_number bigint NOT NULL CHECK (revision_number > 0),
    parent_reconciliation_revision_id text,
    model_revision_id text NOT NULL,
    definition_payload jsonb NOT NULL CHECK (
        definition_payload->>'schema_version' = 'thermox.calibration/v1'
        AND jsonb_typeof(definition_payload->'calibration') = 'object'),
    mode text NOT NULL CHECK (
        mode IN ('hard_equalities', 'weighted_measurements')),
    policy_payload jsonb NOT NULL CHECK (
        jsonb_typeof(policy_payload) = 'object'),
    checksum text NOT NULL CHECK (checksum ~ '^sha256:[0-9a-f]{64}$'),
    created_by_user_id text NOT NULL CHECK (created_by_user_id <> ''),
    created_at timestamptz NOT NULL DEFAULT clock_timestamp(),
    FOREIGN KEY (team_id, project_id, model_revision_id)
        REFERENCES thermox_model_revisions (
            team_id, project_id, model_revision_id),
    UNIQUE (team_id, project_id, reconciliation_id, revision_number),
    UNIQUE (team_id, project_id, reconciliation_revision_id),
    FOREIGN KEY (
        team_id, project_id, reconciliation_id,
        parent_reconciliation_revision_id)
        REFERENCES thermox_reconciliation_revisions (
            team_id, project_id, reconciliation_id,
            reconciliation_revision_id),
    UNIQUE (
        team_id, project_id, reconciliation_id,
        reconciliation_revision_id)
);

CREATE TABLE IF NOT EXISTS thermox_reconciliation_studies (
    reconciliation_revision_id text NOT NULL,
    project_id text NOT NULL,
    team_id text NOT NULL,
    role text NOT NULL CHECK (role IN ('constraint', 'held_out')),
    position integer NOT NULL CHECK (position >= 0),
    study_revision_id text NOT NULL,
    PRIMARY KEY (
        team_id, project_id, reconciliation_revision_id,
        role, position),
    FOREIGN KEY (team_id, project_id, reconciliation_revision_id)
        REFERENCES thermox_reconciliation_revisions (
            team_id, project_id, reconciliation_revision_id),
    FOREIGN KEY (team_id, project_id, study_revision_id)
        REFERENCES thermox_study_revisions (
            team_id, project_id, study_revision_id),
    UNIQUE (
        team_id, project_id, reconciliation_revision_id,
        study_revision_id)
);

CREATE INDEX IF NOT EXISTS thermox_reconciliations_project_revision_idx
ON thermox_reconciliation_revisions (
    team_id, project_id, reconciliation_id, revision_number);

COMMIT;
