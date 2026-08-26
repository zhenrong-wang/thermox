import type { ValidationCampaignArtifact } from './types'

interface ValidationCampaignDraft {
  artifactId: string
  name: string
  objective: string
  studyRevisionIds: string[]
  limitationsText: string
}

export function buildValidationCampaign(
  draft: ValidationCampaignDraft,
): ValidationCampaignArtifact {
  const id = draft.artifactId.trim()
  const name = draft.name.trim()
  const objective = draft.objective.trim()
  const studyRevisionIds = draft.studyRevisionIds.map((value) => value.trim())
  const limitations = draft.limitationsText
    .split('\n')
    .map((value) => value.trim())
    .filter(Boolean)
  if (!id || !name || !objective) {
    throw new Error('Campaign ID, name, and objective are required.')
  }
  if (!studyRevisionIds.length || studyRevisionIds.length > 100) {
    throw new Error('Select between 1 and 100 exact Study revisions.')
  }
  if (
    studyRevisionIds.some((value) => !value) ||
    new Set(studyRevisionIds).size !== studyRevisionIds.length
  ) {
    throw new Error('Study revision selections must be non-empty and unique.')
  }
  if (limitations.length > 64 || new Set(limitations).size !== limitations.length) {
    throw new Error('Limitations must be unique and contain at most 64 entries.')
  }
  return {
    schema_version: 'thermox.validation_campaign/v1',
    id,
    name,
    objective,
    study_revision_ids: studyRevisionIds,
    limitations,
  }
}
