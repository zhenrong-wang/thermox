import type { ArtifactRevision, StudyRevision } from './types'

export type StudyPreparationStageState =
  | 'complete'
  | 'ready'
  | 'attention'
  | 'locked'

export interface StudyPreparationStage {
  id: 'intent' | 'inputs' | 'validation' | 'publication'
  number: number
  title: string
  detail: string
  state: StudyPreparationStageState
}

export interface StudyPreparationInput {
  recognizedIntent: boolean
  localDefinitionIssueCount: number
  boundaryValueCount: number
  requiredArtifactCount: number
  unresolvedArtifactCount: number
  exactRevisionCompiled: boolean
  hasPublishedStudy: boolean
}

export function resolveStudyArtifactSelections(
  requiredArtifactIds: string[],
  artifactRevisions: ArtifactRevision[],
  preferredRevisionIds: Record<string, string>,
  current: Record<string, string>,
): Record<string, string> {
  return Object.fromEntries(
    requiredArtifactIds.map((artifactId) => {
      const revisions = artifactRevisions
        .filter((revision) => revision.artifact_id === artifactId)
        .sort(
          (left, right) => right.revision_number - left.revision_number,
        )
      const available = new Set(
        revisions.map((revision) => revision.artifact_revision_id),
      )
      const currentSelection = current[artifactId]
      const preferred = preferredRevisionIds[artifactId]
      return [
        artifactId,
        currentSelection && available.has(currentSelection)
          ? currentSelection
          : preferred && available.has(preferred)
            ? preferred
            : revisions[0]?.artifact_revision_id ?? '',
      ]
    }),
  )
}

export function selectedStudyArtifactRevisionIds(
  requiredArtifactIds: string[],
  selections: Record<string, string>,
): string[] {
  return requiredArtifactIds
    .map((artifactId) => selections[artifactId])
    .filter((id): id is string => Boolean(id))
}

export function studyMatchesPreparationSelection(
  study: StudyRevision,
  caseRevisionId: string,
  selectedCalculationArtifactRevisionIds: string[],
): boolean {
  return (
    study.case_revision_id === caseRevisionId &&
    selectedCalculationArtifactRevisionIds.every((revisionId) =>
      study.artifact_revision_ids.includes(revisionId),
    )
  )
}

function countLabel(count: number, singular: string): string {
  return `${count} ${singular}${count === 1 ? '' : 's'}`
}

export function buildStudyPreparationStages(
  input: StudyPreparationInput,
): StudyPreparationStage[] {
  const inputsLocallyComplete = input.localDefinitionIssueCount === 0
  const artifactsResolved = input.unresolvedArtifactCount === 0
  const inputDetail = !inputsLocallyComplete
    ? `${countLabel(input.localDefinitionIssueCount, 'physical definition issue')} unresolved.`
    : input.boundaryValueCount === 0
      ? 'No explicit fixed values or parameter overrides; compiler decides sufficiency.'
      : `${countLabel(input.boundaryValueCount, 'fixed value or override')} declared.`
  const validationDetail = input.exactRevisionCompiled
    ? 'The exact topology, case, and artifact revisions compiled.'
    : artifactsResolved
      ? input.requiredArtifactCount === 0
        ? 'No artifacts required; compile the exact topology and case.'
        : 'All required artifact revisions selected; compile next.'
      : `${countLabel(input.unresolvedArtifactCount, 'artifact revision')} unresolved.`

  return [
    {
      id: 'intent',
      number: 1,
      title: 'Intent and mode',
      detail: input.recognizedIntent
        ? 'The persisted case mode maps to a registered engineering intent.'
        : 'Choose a recognized steady or transient engineering intent.',
      state: input.recognizedIntent ? 'complete' : 'attention',
    },
    {
      id: 'inputs',
      number: 2,
      title: 'Definitions and boundaries',
      detail: inputDetail,
      state: !input.recognizedIntent
        ? 'locked'
        : inputsLocallyComplete && input.boundaryValueCount > 0
          ? 'complete'
          : 'attention',
    },
    {
      id: 'validation',
      number: 3,
      title: 'Engineering data and compile',
      detail: validationDetail,
      state: input.exactRevisionCompiled
        ? 'complete'
        : artifactsResolved && input.recognizedIntent
          ? 'ready'
          : 'attention',
    },
    {
      id: 'publication',
      number: 4,
      title: 'Outputs and Study',
      detail: input.hasPublishedStudy
        ? 'An immutable Study exists for this exact input selection.'
        : input.exactRevisionCompiled
          ? 'Declare outputs and publish the validated Study.'
          : 'Compile the exact input revisions before publishing.',
      state: input.hasPublishedStudy
        ? 'complete'
        : input.exactRevisionCompiled
          ? 'ready'
          : 'locked',
    },
  ]
}
