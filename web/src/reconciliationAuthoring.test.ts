import { describe, expect, it } from 'vitest'
import {
  jointConfidenceRegionIssues,
  reconciliationReadiness,
} from './reconciliationAuthoring'
import type { CalibrationDocument, CaseRevision, StudyRevision } from './types'

const cases = [
  { case_revision_id: 'case-r1', case_id: 'measured' },
  { case_revision_id: 'case-r2', case_id: 'check' },
] as CaseRevision[]
const studies = [
  { study_revision_id: 'study-r1', case_revision_id: 'case-r1' },
  { study_revision_id: 'study-r2', case_revision_id: 'case-r2' },
] as StudyRevision[]
const definition: CalibrationDocument = {
  schema_version: 'thermox.calibration/v1',
  calibration: {
    id: 'balance',
    parameters: [{
      id: 'fuel_flow', scope: 'system', targets: ['parameters.fuel_flow'],
    }],
    observations: [
      { id: 'power', case: 'measured', target: 'generator.power', measured: 1, sigma: 0.01 },
      { id: 'heat_rate', case: 'check', target: 'system.heat_rate', measured: 2, sigma: 0.02 },
    ],
  },
}

describe('reconciliation calculation readiness', () => {
  it('keeps held-out evidence outside the constrained solve', () => {
    expect(reconciliationReadiness(
      definition, 'hard_equalities', ['study-r1'], ['study-r2'], studies, cases,
    )).toMatchObject({
      ready: true,
      constraintObservationCount: 1,
      adjustableQuantityCount: 1,
    })
  })

  it('blocks an underdetermined or leaking evidence partition', () => {
    const result = reconciliationReadiness(
      definition, 'hard_equalities', ['study-r1', 'study-r2'], ['study-r2'], studies, cases,
    )
    expect(result.ready).toBe(false)
    expect(result.issues.join(' ')).toContain('both constrained and held out')
    expect(result.issues.join(' ')).toContain('one constrained observation')
  })

  it('requires explicit coherent joint-region policy', () => {
    expect(jointConfidenceRegionIssues(
      true, 'weighted_measurements', 2, ['fuel_flow', 'efficiency'],
    )).toEqual([])
    expect(jointConfidenceRegionIssues(
      true, 'hard_equalities', 0, ['fuel_flow', 'fuel_flow'],
    ).join(' ')).toContain('requires weighted measurements')
    expect(jointConfidenceRegionIssues(
      true, 'hard_equalities', 0, ['fuel_flow', 'fuel_flow'],
    ).join(' ')).toContain('finite and positive')
    expect(jointConfidenceRegionIssues(
      true, 'hard_equalities', 0, ['fuel_flow', 'fuel_flow'],
    ).join(' ')).toContain('must be unique')
  })
})
