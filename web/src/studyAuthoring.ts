import type {
  StudyTrajectoryValidationBinding,
  ValidationSeriesArtifact,
} from './types'

export function parseValidationSeriesArtifact(
  payload: string,
  artifactId: string,
): ValidationSeriesArtifact {
  const definition = JSON.parse(payload) as ValidationSeriesArtifact
  if (definition.schema_version !== 'thermox.validation_series/v1') {
    throw new Error('Expected thermox.validation_series/v1.')
  }
  if (definition.id !== artifactId) {
    throw new Error('Artifact ID must match the declaration ID.')
  }
  if (!definition.signals?.length) {
    throw new Error('At least one reference signal is required.')
  }
  return definition
}

export function studyArtifactRevisionIds(
  physicalArtifactRevisionIds: string[],
  trajectoryBindings: StudyTrajectoryValidationBinding[],
): string[] {
  return [...new Set([
    ...physicalArtifactRevisionIds,
    ...trajectoryBindings.map((binding) => binding.artifact_revision_id),
  ])].sort()
}
