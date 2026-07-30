import { describe, expect, it } from 'vitest'
import { latestArtifactRevisions } from './resourceBindings'
import type { ArtifactRevision } from './types'

function revision(
  artifactId: string,
  revisionNumber: number,
): ArtifactRevision {
  return {
    schema_version: 'thermox.artifact_revision/v1',
    artifact_revision_id: `${artifactId}-r${revisionNumber}`,
    project_id: 'project',
    team_id: 'team',
    artifact_id: artifactId,
    revision_number: revisionNumber,
    parent_artifact_revision_id: '',
    artifact_type: 'thermox.performance_map',
    artifact_schema_version: 'thermox.performance_map/v1',
    content: {
      media_type: 'application/json',
      byte_size: 1,
      checksum: `sha256:${revisionNumber}`,
    },
    created_by_user_id: 'user',
    created_at_epoch_ms: revisionNumber,
  }
}

describe('latestArtifactRevisions', () => {
  it('exposes only the newest immutable revision for each logical artifact', () => {
    const result = latestArtifactRevisions([
      revision('compressor-map', 1),
      revision('turbine-map', 1),
      revision('compressor-map', 3),
      revision('compressor-map', 2),
    ])

    expect(
      result.map((item) => [item.artifact_id, item.revision_number]),
    ).toEqual([
      ['compressor-map', 3],
      ['turbine-map', 1],
    ])
  })
})
