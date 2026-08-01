import { describe, expect, it } from 'vitest'
import { buildWorkflowStages, type WorkflowInputs } from './workflow'

const empty: WorkflowInputs = {
  componentCount: 0,
  connectionCount: 0,
  mediumCount: 0,
  definitionIssueCount: 0,
  hasCase: false,
  unresolvedArtifactCount: 0,
  compiled: false,
  studyRevisionCount: 0,
  variableCount: 0,
  equationCount: 0,
  runConfigurationCount: 0,
  activeJobCount: 0,
  succeededJobCount: 0,
}

describe('engineering workflow', () => {
  it('keeps execution and analysis locked before a system is defined', () => {
    const stages = buildWorkflowStages(empty)

    expect(stages.map((stage) => stage.state)).toEqual([
      'attention',
      'locked',
      'locked',
      'locked',
      'locked',
    ])
    expect(stages[0].detail).toContain('component template')
  })

  it('requires compilation and a published study before calculation', () => {
    const beforeCompile = buildWorkflowStages({
      ...empty,
      componentCount: 4,
      connectionCount: 3,
      mediumCount: 1,
      hasCase: true,
    })
    expect(beforeCompile[1].state).toBe('complete')
    expect(beforeCompile[2].state).toBe('attention')
    expect(beforeCompile[3].state).toBe('locked')

    const compiled = buildWorkflowStages({
      ...empty,
      componentCount: 4,
      connectionCount: 3,
      mediumCount: 1,
      hasCase: true,
      compiled: true,
      variableCount: 18,
      equationCount: 18,
    })
    expect(compiled[1].state).toBe('complete')
    expect(compiled[2].state).toBe('attention')
    expect(compiled[2].detail).toContain('immutable study')
    expect(compiled[3].state).toBe('locked')

    const published = buildWorkflowStages({
      ...empty,
      componentCount: 4,
      connectionCount: 3,
      mediumCount: 1,
      hasCase: true,
      compiled: true,
      variableCount: 18,
      equationCount: 18,
      studyRevisionCount: 1,
    })
    expect(published[2].state).toBe('complete')
    expect(published[2].detail).toBe('18 variables · 18 equations')
    expect(published[3].state).toBe('ready')
  })

  it('promotes successful execution into analysis readiness', () => {
    const stages = buildWorkflowStages({
      ...empty,
      componentCount: 2,
      connectionCount: 1,
      hasCase: true,
      compiled: true,
      studyRevisionCount: 1,
      variableCount: 6,
      equationCount: 6,
      runConfigurationCount: 1,
      succeededJobCount: 2,
    })

    expect(stages[3].state).toBe('complete')
    expect(stages[4].state).toBe('ready')
    expect(stages[4].detail).toContain('2 successful results')
  })

  it('keeps study authoring behind incomplete physical definitions', () => {
    const stages = buildWorkflowStages({
      ...empty,
      componentCount: 2,
      definitionIssueCount: 3,
    })
    expect(stages[1].state).toBe('attention')
    expect(stages[1].detail).toContain('3 physical inputs')
    expect(stages[2].state).toBe('locked')
  })
})
