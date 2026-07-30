import type { ArtifactRevision } from './types'

export function latestArtifactRevisions(
  revisions: ArtifactRevision[],
): ArtifactRevision[] {
  const latest = new Map<string, ArtifactRevision>()
  for (const revision of revisions) {
    const current = latest.get(revision.artifact_id)
    if (!current || revision.revision_number > current.revision_number) {
      latest.set(revision.artifact_id, revision)
    }
  }
  return [...latest.values()]
}
