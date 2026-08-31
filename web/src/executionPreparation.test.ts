import { describe, expect, it } from 'vitest'
import {
  buildExecutionPreparationStages,
  executionSelectionReady,
  studyExecutionMode,
} from './executionPreparation'
import type {
  RunConfigurationRevision,
  SimulationJob,
  StudyRevision,
} from './types'

const study = {
  project_id: 'project-1',
  team_id: 'team-1',
  study_revision_id: 'study-r3',
  revision_number: 3,
  intent: 'transient_prediction',
  result_projections: [{ id: 'shaft-speed' }, { id: 'power' }],
} as StudyRevision

const revision = {
  project_id: 'project-1',
  team_id: 'team-1',
  study_revision_id: 'study-r3',
  revision_number: 2,
} as RunConfigurationRevision

describe('Execution preparation', () => {
  it('derives the calculation mode from the published Study intent', () => {
    expect(studyExecutionMode(study)).toBe('transient')
    expect(studyExecutionMode({ ...study, intent: 'steady_performance' })).toBe(
      'steady',
    )
  })

  it('requires an exact same-project and same-team Study binding', () => {
    expect(executionSelectionReady(revision, study)).toBe(true)
    expect(
      executionSelectionReady(
        revision,
        { ...study, study_revision_id: 'study-r4' },
      ),
    ).toBe(false)
    expect(
      executionSelectionReady(revision, { ...study, team_id: 'team-2' }),
    ).toBe(false)
  })

  it('unlocks review and submission for an exact immutable selection', () => {
    const stages = buildExecutionPreparationStages(revision, study, [])

    expect(stages.map((stage) => stage.state)).toEqual([
      'complete',
      'complete',
      'complete',
      'ready',
    ])
    expect(stages[2].detail).toContain('2 projected outputs')
  })

  it('marks durable execution history complete once a job exists', () => {
    const stages = buildExecutionPreparationStages(
      revision,
      study,
      [{} as SimulationJob],
    )

    expect(stages[3].state).toBe('complete')
    expect(stages[3].detail).toContain('1 durable execution recorded')
  })

  it('blocks policy, review, and submission for a stale binding', () => {
    const stages = buildExecutionPreparationStages(
      revision,
      { ...study, study_revision_id: 'study-r4' },
      [],
    )

    expect(stages.map((stage) => stage.state)).toEqual([
      'attention',
      'locked',
      'locked',
      'locked',
    ])
  })
})
