import { resultDiagnosticSummary } from './resultDiagnostics'
import { isTransientResult } from './resultPresentation'
import type { SimulationJob, SimulationResult } from './types'

export type ResultAssuranceStatus =
  | 'supported'
  | 'failed'
  | 'review'
  | 'not_evaluated'

export interface ResultAssuranceLayer {
  id: 'provenance' | 'numerical' | 'physical' | 'criteria' | 'reference'
  title: string
  status: ResultAssuranceStatus
  detail: string
}

export function buildResultAssuranceLayers(
  job: SimulationJob,
  result: SimulationResult,
  exactProvenance: boolean,
): ResultAssuranceLayer[] {
  const diagnostic = resultDiagnosticSummary(result)
  const feasibility = result.thermal_feasibility
  const acceptance = job.result_summary?.engineering_acceptance
  const trajectoryValidations = isTransientResult(result)
    ? result.trajectory_validations
    : []
  const failedTrajectoryCount = trajectoryValidations.filter(
    (validation) => !validation.evidence.passed,
  ).length

  return [
    {
      id: 'provenance',
      title: 'Exact provenance',
      status: exactProvenance ? 'supported' : 'review',
      detail: exactProvenance
        ? 'Requested, executed, and loaded model revisions agree.'
        : 'The viewer cannot establish an exact requested/executed/topology match.',
    },
    {
      id: 'numerical',
      title: 'Numerical solution',
      status: diagnostic.successful ? 'supported' : 'failed',
      detail: diagnostic.successful
        ? `${diagnostic.mode === 'steady' ? 'Nonlinear solve converged' : 'Time integration completed'} with recorded diagnostics.`
        : diagnostic.message || 'The solver did not report a successful outcome.',
    },
    {
      id: 'physical',
      title: 'Physical checks',
      status:
        feasibility.checked_count === 0
          ? 'not_evaluated'
          : feasibility.passed
            ? 'supported'
            : 'failed',
      detail:
        feasibility.checked_count === 0
          ? 'No applicable counterflow exchanger checks were present.'
          : `${feasibility.passed_count} of ${feasibility.checked_count} counterflow checks passed.`,
    },
    {
      id: 'criteria',
      title: 'Study acceptance',
      status: !acceptance
        ? 'not_evaluated'
        : acceptance.passed
          ? 'supported'
          : 'failed',
      detail: !acceptance
        ? 'This Study declared no engineering acceptance criteria.'
        : `${acceptance.passed_count} passed and ${acceptance.failed_count} failed.`,
    },
    {
      id: 'reference',
      title: 'Reference evidence',
      status: !trajectoryValidations.length
        ? 'not_evaluated'
        : failedTrajectoryCount === 0
          ? 'supported'
          : 'failed',
      detail: !trajectoryValidations.length
        ? 'No independent reference trajectory was evaluated by this job.'
        : `${trajectoryValidations.length - failedTrajectoryCount} of ${trajectoryValidations.length} trajectory datasets matched.`,
    },
  ]
}
