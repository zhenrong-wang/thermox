import { describe, expect, it } from 'vitest'
import { buildWorkflowStages, type WorkflowInputs } from './workflow'

const empty: WorkflowInputs = {
  componentCount: 0,
  connectionCount: 0,
  mediumCount: 0,
  hasCase: false,
  unresolvedArtifactCount: 0,
  compiled: false,
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
    ])
    expect(stages[0].detail).toContain('component template')
  })

  it('only marks a system calculatable after authoritative compilation', () => {
    const beforeCompile = buildWorkflowStages({
      ...empty,
      componentCount: 4,
      connectionCount: 3,
      mediumCount: 1,
      hasCase: true,
    })
    expect(beforeCompile[1].state).toBe('attention')
    expect(beforeCompile[2].state).toBe('locked')

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
    expect(compiled[1].detail).toBe('18 variables · 18 equations')
    expect(compiled[2].state).toBe('ready')
  })

  it('promotes successful execution into analysis readiness', () => {
    const stages = buildWorkflowStages({
      ...empty,
      componentCount: 2,
      connectionCount: 1,
      hasCase: true,
      compiled: true,
      variableCount: 6,
      equationCount: 6,
      runConfigurationCount: 1,
      succeededJobCount: 2,
    })

    expect(stages[2].state).toBe('complete')
    expect(stages[3].state).toBe('ready')
    expect(stages[3].detail).toContain('2 successful results')
  })
})
