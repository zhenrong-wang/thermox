BEGIN;

CREATE SEQUENCE IF NOT EXISTS
    thermox_performance_map_quality_review_id_seq;

ALTER TABLE thermox_artifact_revisions
    ADD CONSTRAINT thermox_artifact_revisions_scoped_checksum_unique
    UNIQUE (
        team_id,
        project_id,
        artifact_revision_id,
        checksum
    );

CREATE TABLE IF NOT EXISTS thermox_performance_map_quality_reviews (
    review_id text PRIMARY KEY DEFAULT (
        'map-quality-review-' || lpad(
            nextval(
                'thermox_performance_map_quality_review_id_seq'
            )::text,
            12,
            '0'
        )
    ),
    project_id text NOT NULL,
    team_id text NOT NULL,
    artifact_revision_id text NOT NULL,
    artifact_checksum text NOT NULL CHECK (
        artifact_checksum ~ '^sha256:[0-9a-f]{64}$'
    ),
    supersedes_review_id text,
    disposition text NOT NULL CHECK (
        disposition IN (
            'approved',
            'approved_with_conditions',
            'rejected'
        )
    ),
    reviewed_scope text NOT NULL CHECK (reviewed_scope <> ''),
    rationale text NOT NULL CHECK (rationale <> ''),
    quality_schema_version text NOT NULL
        CHECK (quality_schema_version <> ''),
    quality_snapshot_json text NOT NULL
        CHECK (quality_snapshot_json <> ''),
    quality_snapshot_checksum text NOT NULL CHECK (
        quality_snapshot_checksum ~ '^sha256:[0-9a-f]{64}$'
    ),
    created_by_user_id text NOT NULL
        CHECK (created_by_user_id <> ''),
    created_at timestamptz NOT NULL DEFAULT clock_timestamp(),
    CONSTRAINT thermox_map_quality_reviews_artifact_fk
        FOREIGN KEY (
            team_id,
            project_id,
            artifact_revision_id,
            artifact_checksum
        )
        REFERENCES thermox_artifact_revisions (
            team_id,
            project_id,
            artifact_revision_id,
            checksum
        ),
    CONSTRAINT thermox_map_quality_reviews_scoped_id_unique
        UNIQUE (
            team_id,
            project_id,
            artifact_revision_id,
            review_id
        ),
    CONSTRAINT thermox_map_quality_reviews_supersedes_fk
        FOREIGN KEY (
            team_id,
            project_id,
            artifact_revision_id,
            supersedes_review_id
        )
        REFERENCES thermox_performance_map_quality_reviews (
            team_id,
            project_id,
            artifact_revision_id,
            review_id
        )
);

CREATE INDEX IF NOT EXISTS
    thermox_map_quality_reviews_artifact_created_idx
ON thermox_performance_map_quality_reviews (
    team_id,
    project_id,
    artifact_revision_id,
    created_at,
    review_id
);

COMMIT;
