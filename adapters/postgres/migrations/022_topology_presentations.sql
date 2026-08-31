BEGIN;

CREATE TABLE IF NOT EXISTS thermox_topology_presentations (
    team_id text NOT NULL,
    project_id text NOT NULL,
    user_id text NOT NULL CHECK (user_id <> ''),
    model_revision_id text NOT NULL,
    presentation_payload jsonb NOT NULL CHECK (
        jsonb_typeof(presentation_payload) = 'object'
    ),
    updated_at timestamptz NOT NULL DEFAULT clock_timestamp(),
    PRIMARY KEY (team_id, project_id, user_id),
    CONSTRAINT thermox_topology_presentations_project_fk
        FOREIGN KEY (team_id, project_id)
        REFERENCES thermox_projects (team_id, project_id)
        ON DELETE CASCADE,
    CONSTRAINT thermox_topology_presentations_revision_fk
        FOREIGN KEY (team_id, project_id, model_revision_id)
        REFERENCES thermox_model_revisions (
            team_id,
            project_id,
            model_revision_id
        )
);

CREATE INDEX IF NOT EXISTS
    thermox_topology_presentations_revision_idx
ON thermox_topology_presentations (
    team_id,
    project_id,
    model_revision_id
);

COMMIT;
