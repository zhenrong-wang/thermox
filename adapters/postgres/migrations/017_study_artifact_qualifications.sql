BEGIN;

CREATE TABLE thermox_study_artifact_qualifications (
    study_revision_id text NOT NULL,
    project_id text NOT NULL,
    team_id text NOT NULL,
    position integer NOT NULL CHECK (position >= 0),
    artifact_revision_id text NOT NULL,
    review_id text NOT NULL,
    acceptable_dispositions text[] NOT NULL CHECK (
        cardinality(acceptable_dispositions) > 0
        AND acceptable_dispositions <@ ARRAY[
            'approved',
            'approved_with_conditions'
        ]::text[]
    ),
    PRIMARY KEY (
        team_id,
        project_id,
        study_revision_id,
        position
    ),
    CONSTRAINT thermox_study_qualifications_artifact_unique
        UNIQUE (
            team_id,
            project_id,
            study_revision_id,
            artifact_revision_id
        ),
    CONSTRAINT thermox_study_qualifications_bound_artifact_fk
        FOREIGN KEY (
            team_id,
            project_id,
            study_revision_id,
            artifact_revision_id
        )
        REFERENCES thermox_study_artifacts (
            team_id,
            project_id,
            study_revision_id,
            artifact_revision_id
        ),
    CONSTRAINT thermox_study_qualifications_review_fk
        FOREIGN KEY (
            team_id,
            project_id,
            artifact_revision_id,
            review_id
        )
        REFERENCES thermox_performance_map_quality_reviews (
            team_id,
            project_id,
            artifact_revision_id,
            review_id
        )
);

CREATE INDEX thermox_study_qualifications_review_idx
ON thermox_study_artifact_qualifications (
    team_id,
    project_id,
    artifact_revision_id,
    review_id
);

COMMIT;
