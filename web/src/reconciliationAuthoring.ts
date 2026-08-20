import type {
  CalibrationDocument,
  CaseRevision,
  ReconciliationMode,
  StudyRevision,
} from './types'

export interface ReconciliationReadiness {
  ready: boolean
  issues: string[]
  constraintObservationCount: number
  adjustableQuantityCount: number
}

export function jointConfidenceRegionIssues(
  enabled: boolean,
  mode: ReconciliationMode,
  objectiveIncrease: number,
  parameterIds: string[],
): string[] {
  if (!enabled) return []
  const issues: string[] = []
  if (mode !== 'weighted_measurements') {
    issues.push('A joint confidence region requires weighted measurements.')
  }
  if (!Number.isFinite(objectiveIncrease) || objectiveIncrease <= 0) {
    issues.push('Joint-region objective increase must be finite and positive.')
  }
  if (new Set(parameterIds).size !== parameterIds.length) {
    issues.push('Joint-region parameter IDs must be unique.')
  }
  return issues
}

export function reconciliationReadiness(
  definition: CalibrationDocument,
  mode: ReconciliationMode,
  constraintStudyRevisionIds: string[],
  heldOutStudyRevisionIds: string[],
  studies: StudyRevision[],
  cases: CaseRevision[],
): ReconciliationReadiness {
  const issues: string[] = []
  const constraints = new Set(constraintStudyRevisionIds)
  const heldOut = new Set(heldOutStudyRevisionIds)
  if (constraints.size === 0) issues.push('Select at least one constraint Study.')
  if (constraintStudyRevisionIds.some((id) => heldOut.has(id))) {
    issues.push('A Study cannot be both constrained and held out.')
  }
  const caseByRevision = new Map(
    cases.map((item) => [item.case_revision_id, item.case_id]),
  )
  const constraintCases = new Set(
    studies
      .filter((study) => constraints.has(study.study_revision_id))
      .map((study) => caseByRevision.get(study.case_revision_id))
      .filter((id): id is string => Boolean(id)),
  )
  const heldOutCases = new Set(
    studies
      .filter((study) => heldOut.has(study.study_revision_id))
      .map((study) => caseByRevision.get(study.case_revision_id))
      .filter((id): id is string => Boolean(id)),
  )
  const observations = definition.calibration.observations
  const constraintObservationCount = observations.filter((observation) =>
    constraintCases.has(observation.case),
  ).length
  const adjustableQuantityCount = definition.calibration.parameters.length
  if (
    mode === 'hard_equalities' &&
    constraintObservationCount !== adjustableQuantityCount
  ) {
    issues.push('Hard equality reconciliation requires one constrained observation per adjustable quantity.')
  }
  if (
    mode === 'weighted_measurements' &&
    constraintObservationCount < adjustableQuantityCount
  ) {
    issues.push('Weighted reconciliation requires at least as many constrained observations as adjustable quantities.')
  }
  for (const caseId of constraintCases) {
    if (!observations.some((observation) => observation.case === caseId)) {
      issues.push(`Constraint case ${caseId} has no observation.`)
    }
  }
  for (const caseId of heldOutCases) {
    if (!observations.some((observation) => observation.case === caseId)) {
      issues.push(`Held-out case ${caseId} has no observation.`)
    }
  }
  return {
    ready: issues.length === 0,
    issues,
    constraintObservationCount,
    adjustableQuantityCount,
  }
}
