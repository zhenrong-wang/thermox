import { describe, expect, it } from 'vitest'
import { buildResultAssuranceLayers } from './resultAssurance'
import type {
  SimulationJob,
  SteadySimulationResult,
  TransientSimulationResult,
} from './types'

function steadyResult(): SteadySimulationResult {
  return {
    diagnostics: { converged: true, message: 'converged' },
    thermal_feasibility: {
      checked_count: 0,
      passed: true,
      passed_count: 0,
      failed_count: 0,
    },
  } as SteadySimulationResult
}

describe('Result assurance ladder', () => {
  it('does not confuse a converged solve with physical or reference evidence', () => {
    const layers = buildResultAssuranceLayers(
      { result_summary: null } as unknown as SimulationJob,
      steadyResult(),
      true,
    )

    expect(layers.map((layer) => layer.status)).toEqual([
      'supported',
      'supported',
      'not_evaluated',
      'not_evaluated',
      'not_evaluated',
    ])
  })

  it('surfaces failed engineering acceptance independently', () => {
    const layers = buildResultAssuranceLayers(
      {
        result_summary: {
          engineering_acceptance: {
            passed: false,
            passed_count: 2,
            failed_count: 1,
          },
        },
      } as SimulationJob,
      {
        ...steadyResult(),
        thermal_feasibility: {
          checked_count: 2,
          passed: true,
          passed_count: 2,
          failed_count: 0,
        },
      } as SteadySimulationResult,
      true,
    )

    expect(layers[2].status).toBe('supported')
    expect(layers[3].status).toBe('failed')
    expect(layers[3].detail).toContain('1 failed')
  })

  it('distinguishes exact provenance review from solver failure', () => {
    const result = steadyResult()
    result.diagnostics.converged = false
    const layers = buildResultAssuranceLayers(
      { result_summary: null } as unknown as SimulationJob,
      result,
      false,
    )

    expect(layers[0].status).toBe('review')
    expect(layers[1].status).toBe('failed')
  })

  it('reports independent transient trajectory evidence separately', () => {
    const result = {
      diagnostics: { success: true, message: 'complete' },
      thermal_feasibility: {
        checked_count: 0,
        passed: true,
        passed_count: 0,
        failed_count: 0,
      },
      trajectory: [],
      trajectory_validations: [
        { evidence: { passed: true } },
        { evidence: { passed: false } },
      ],
    } as unknown as TransientSimulationResult
    const layers = buildResultAssuranceLayers(
      { result_summary: null } as unknown as SimulationJob,
      result,
      true,
    )

    expect(layers[4].status).toBe('failed')
    expect(layers[4].detail).toContain('1 of 2')
  })
})
