import type {
  ProjectModelValidation,
  ResultValueScope,
  SteadySolverSettings,
  TransientSolverSettings,
} from './types'

export function validationMatchesExecutionSelection(
  validation: ProjectModelValidation | undefined,
  modelRevisionId: string,
  caseRevisionId: string,
  artifactRevisionIds: string[],
) {
  if (
    !validation?.validation.readiness.calculatable ||
    validation.model_revision_id !== modelRevisionId ||
    validation.case_revision_id !== caseRevisionId
  ) {
    return false
  }
  const validated = validation.artifact_revisions
    .map((revision) => revision.artifact_revision_id)
    .sort()
  const selected = [...artifactRevisionIds].sort()
  return (
    validated.length === selected.length &&
    validated.every(
      (revisionId, index) =>
        revisionId === selected[index],
    )
  )
}

export const defaultSteadySolver: SteadySolverSettings = {
  max_iterations: 50,
  residual_tolerance: 1e-10,
  step_tolerance: 1e-10,
  linear_residual_tolerance: 1e-10,
  structural_decomposition_enabled: false,
  finite_difference_epsilon: 1e-6,
  min_damping: 1e-6,
  damping_reduction: 0.5,
  sufficient_decrease: 1e-4,
  max_line_search_steps: 50,
  continuation_enabled: false,
  continuation_initial_step: 0.25,
  continuation_minimum_step: 1 / 64,
  continuation_step_growth: 1.5,
  continuation_step_reduction: 0.5,
  continuation_maximum_stages: 100,
}

export const defaultTransientSolver: TransientSolverSettings = {
  start_time: 0,
  end_time: 1,
  initial_step: 1e-3,
  min_step: 1e-9,
  max_step: 0.1,
  absolute_tolerance: 1e-7,
  relative_tolerance: 1e-5,
  max_steps: 100000,
  max_consecutive_rejections: 20,
  maximum_order: 2,
  compute_consistent_initial_conditions: true,
  nonlinear_solver: { ...defaultSteadySolver },
}

export function scopeRequiresComponent(scope: ResultValueScope) {
  return (
    scope === 'component_metric' ||
    scope === 'component_internal' ||
    scope === 'port_primary' ||
    scope === 'port_derived'
  )
}

export function scopeRequiresPort(scope: ResultValueScope) {
  return scope === 'port_primary' || scope === 'port_derived'
}
