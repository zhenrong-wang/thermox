import { isTransientResult } from './resultPresentation'
import type { RevisionProvenance, SimulationResult } from './types'

export interface DiagnosticFact {
  label: string
  value: string
}

export interface ResultDiagnosticSummary {
  successful: boolean
  mode: 'steady' | 'transient'
  message: string
  facts: DiagnosticFact[]
}

export function exactRevisionProvenance(
  requested: RevisionProvenance | null | undefined,
  executed: RevisionProvenance | null | undefined,
  loadedModelRevisionId: string,
  topologyLoaded: boolean,
): boolean {
  if (!requested || !executed || !topologyLoaded) return false
  return (
    requested.project_id === executed.project_id &&
    requested.run_configuration_revision_id ===
      executed.run_configuration_revision_id &&
    requested.run_configuration_checksum ===
      executed.run_configuration_checksum &&
    requested.study_revision_id === executed.study_revision_id &&
    requested.study_checksum === executed.study_checksum &&
    requested.model_revision_id === executed.model_revision_id &&
    requested.model_checksum === executed.model_checksum &&
    requested.case_revision_id === executed.case_revision_id &&
    requested.case_checksum === executed.case_checksum &&
    loadedModelRevisionId === executed.model_revision_id
  )
}

function number(value: number): string {
  if (!Number.isFinite(value)) return String(value)
  if (value !== 0 && Math.abs(value) < 1e-3) return value.toExponential(3)
  return value.toLocaleString('en-US', { maximumFractionDigits: 6 })
}

export function resultDiagnosticSummary(
  result: SimulationResult,
): ResultDiagnosticSummary {
  if (isTransientResult(result)) {
    return {
      successful: result.diagnostics.success,
      mode: 'transient',
      message: result.diagnostics.message,
      facts: [
        {
          label: 'Accepted steps',
          value: number(result.diagnostics.accepted_steps),
        },
        {
          label: 'Rejected steps',
          value: number(result.diagnostics.rejected_steps),
        },
        {
          label: 'Nonlinear iterations',
          value: number(result.diagnostics.nonlinear_iterations),
        },
        {
          label: 'Final time',
          value: `${number(result.diagnostics.final_time)} s`,
        },
        {
          label: 'Last step',
          value: `${number(result.diagnostics.last_step)} s`,
        },
        {
          label: 'Last local error',
          value: number(result.diagnostics.last_error_norm),
        },
        {
          label: 'Limiting state',
          value: result.diagnostics.limiting_error_variable || '—',
        },
        {
          label: 'Worst constraint residual',
          value: number(
            result.diagnostics.maximum_absolute_normalized_residual,
          ),
        },
        {
          label: 'Limiting constraint',
          value: result.diagnostics.limiting_nonlinear_residual || '—',
        },
        {
          label: 'Worst linear error',
          value: number(result.diagnostics.maximum_linear_backward_error),
        },
        {
          label: 'Minimum reciprocal pivot ratio',
          value: result.diagnostics.factorization_quality_observations
            ? number(result.diagnostics.minimum_reciprocal_pivot_ratio)
            : 'unavailable',
        },
        {
          label: 'Pivot evidence',
          value: result.diagnostics.factorization_quality_observations
            ? `${result.diagnostics.accepted_pivot_count_at_minimum_ratio}/${result.diagnostics.factorization_size_at_minimum_ratio} · ${result.diagnostics.factorization_quality_method}`
            : 'unavailable',
        },
        {
          label: 'Structural block solves',
          value: number(result.diagnostics.structural_block_solves),
        },
        {
          label: 'Tearing success / attempts',
          value: `${number(result.diagnostics.structural_tearing_successes)} / ${number(result.diagnostics.structural_tearing_attempts)}`,
        },
        {
          label: 'Tearing fallbacks',
          value: number(result.diagnostics.structural_tearing_fallbacks),
        },
        {
          label: 'Largest linear system',
          value: number(result.diagnostics.largest_linear_system_size),
        },
        {
          label: 'Linear solver',
          value: result.diagnostics.linear_solver_backend || '—',
        },
      ],
    }
  }
  return {
    successful: result.diagnostics.converged,
    mode: 'steady',
    message: result.diagnostics.message,
    facts: [
      { label: 'Iterations', value: number(result.diagnostics.iterations) },
      {
        label: 'Residual norm',
        value: number(result.diagnostics.final_residual_norm),
      },
      {
        label: 'Worst equation residual',
        value: number(
          result.diagnostics.final_maximum_absolute_normalized_residual,
        ),
      },
      {
        label: 'Limiting equation',
        value: result.diagnostics.limiting_residual || '—',
      },
      {
        label: 'Step norm',
        value: number(result.diagnostics.final_step_norm),
      },
      {
        label: 'Worst linear error',
        value: number(result.diagnostics.maximum_linear_backward_error),
      },
      {
        label: 'Minimum reciprocal pivot ratio',
        value: result.diagnostics.factorization_quality_observations
          ? number(result.diagnostics.minimum_reciprocal_pivot_ratio)
          : 'unavailable',
      },
      {
        label: 'Pivot evidence',
        value: result.diagnostics.factorization_quality_observations
          ? `${result.diagnostics.accepted_pivot_count_at_minimum_ratio}/${result.diagnostics.factorization_size_at_minimum_ratio} · ${result.diagnostics.factorization_quality_method}`
          : 'unavailable',
      },
      {
        label: 'Structural block solves',
        value: number(result.diagnostics.structural_block_solves),
      },
      {
        label: 'Tearing success / attempts',
        value: `${number(result.diagnostics.structural_tearing_successes)} / ${number(result.diagnostics.structural_tearing_attempts)}`,
      },
      {
        label: 'Tearing fallbacks',
        value: number(result.diagnostics.structural_tearing_fallbacks),
      },
      {
        label: 'Largest linear system',
        value: number(result.diagnostics.largest_linear_system_size),
      },
      {
        label: 'Function evaluations',
        value: number(result.diagnostics.function_evaluations),
      },
      {
        label: 'Jacobian evaluations',
        value: number(result.diagnostics.jacobian_evaluations),
      },
      {
        label: 'Linear solver',
        value: result.diagnostics.linear_solver_backend || '—',
      },
    ],
  }
}
