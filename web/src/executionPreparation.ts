import type {
  RunConfigurationRevision,
  SimulationJob,
  StudyRevision,
} from './types'

export type ExecutionPreparationStageState =
  | 'complete'
  | 'ready'
  | 'attention'
  | 'locked'

export interface ExecutionPreparationStage {
  id: 'study' | 'policy' | 'review' | 'execution'
  number: number
  title: string
  detail: string
  state: ExecutionPreparationStageState
}

export function studyExecutionMode(
  study: StudyRevision | undefined,
): 'steady' | 'transient' {
  return study?.intent.includes('dynamic') ||
    study?.intent.includes('transient')
    ? 'transient'
    : 'steady'
}

export function executionSelectionReady(
  revision: RunConfigurationRevision | undefined,
  study: StudyRevision | undefined,
): boolean {
  return Boolean(
    revision &&
      study &&
      revision.project_id === study.project_id &&
      revision.team_id === study.team_id &&
      revision.study_revision_id === study.study_revision_id,
  )
}

export function buildExecutionPreparationStages(
  revision: RunConfigurationRevision | undefined,
  study: StudyRevision | undefined,
  jobs: SimulationJob[],
): ExecutionPreparationStage[] {
  const exactSelection = executionSelectionReady(revision, study)
  const mode = studyExecutionMode(study)
  const outputCount = study?.result_projections.length ?? 0
  const jobCount = jobs.length

  return [
    {
      id: 'study',
      number: 1,
      title: 'Published Study',
      detail: exactSelection
        ? `Exact Study r${study?.revision_number} binds model, case, and engineering data.`
        : 'Select a run configuration with an accessible, matching Study revision.',
      state: exactSelection ? 'complete' : 'attention',
    },
    {
      id: 'policy',
      number: 2,
      title: 'Solver policy',
      detail: exactSelection
        ? `Immutable ${mode} solver settings are published as configuration r${revision?.revision_number}.`
        : 'Solver policy is locked until the Study binding is resolved.',
      state: exactSelection ? 'complete' : 'locked',
    },
    {
      id: 'review',
      number: 3,
      title: 'Execution review',
      detail: exactSelection
        ? `${outputCount} projected output${outputCount === 1 ? '' : 's'} will be evaluated with exact revision provenance.`
        : 'Resolve immutable revision bindings before execution.',
      state: exactSelection ? 'complete' : 'locked',
    },
    {
      id: 'execution',
      number: 4,
      title: 'Submit and diagnose',
      detail: jobCount
        ? `${jobCount} durable execution${jobCount === 1 ? '' : 's'} recorded for this configuration revision.`
        : exactSelection
          ? 'Ready to queue an idempotent job on the worker service.'
          : 'Submission is blocked until execution review is ready.',
      state:
        exactSelection && jobCount
          ? 'complete'
          : exactSelection
            ? 'ready'
            : 'locked',
    },
  ]
}
