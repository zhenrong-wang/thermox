import { describe, expect, it } from 'vitest'
import {
  exactRevisionProvenance,
  resultDiagnosticSummary,
} from './resultDiagnostics'
import type { RevisionProvenance, SimulationResult } from './types'

function steadyResult(): SimulationResult {
  return {
    schema_version: 'thermox.result/v3',
    status: 'succeeded',
    error: { schema_version: '', code: '', stage: '', message: '' },
    metadata: {} as SimulationResult['metadata'],
    diagnostics: {
      converged: true,
      iterations: 7,
      final_residual_norm: 2.5e-8,
      final_maximum_absolute_normalized_residual: 2e-8,
      limiting_residual: 'component.compressor.energy',
      final_step_norm: 1e-9,
      last_linear_backward_error: 3e-15,
      maximum_linear_backward_error: 8e-15,
      structural_block_solves: 4,
      largest_linear_system_size: 12,
      failed_structural_block: '',
      function_evaluations: 11,
      jacobian_evaluations: 4,
      linear_solver_evaluations: 4,
      symbolic_factorizations: 1,
      numeric_factorizations: 4,
      linear_solver_backend: 'klu',
      message: 'converged',
    },
    continuation: {
      enabled: false,
      converged: true,
      used_informed_path: false,
      reached_parameter: 1,
      accepted_stages: 0,
      rejected_stages: 0,
      message: '',
      stages: [],
    },
    graph: { components: [], system_balances: [], kpis: [] },
    reduced_connection_equations: [],
  }
}

describe('resultDiagnosticSummary', () => {
  it('surfaces steady convergence evidence', () => {
    const summary = resultDiagnosticSummary(steadyResult())
    expect(summary).toMatchObject({ successful: true, mode: 'steady' })
    expect(summary.facts).toContainEqual({
      label: 'Residual norm',
      value: '2.500e-8',
    })
    expect(summary.facts).toContainEqual({
      label: 'Limiting equation',
      value: 'component.compressor.energy',
    })
    expect(summary.facts).toContainEqual({
      label: 'Worst linear error',
      value: '8.000e-15',
    })
    expect(summary.facts).toContainEqual({
      label: 'Largest linear system',
      value: '12',
    })
  })

  it('surfaces transient integration evidence', () => {
    const result = {
      schema_version: 'thermox.result/v3',
      status: 'succeeded',
      error: { schema_version: '', code: '', stage: '', message: '' },
      metadata: {} as SimulationResult['metadata'],
      trajectory: [],
      events: [],
      diagnostics: {
        success: true,
        accepted_steps: 12,
        rejected_steps: 1,
        maximum_order_used: 2,
        nonlinear_solves: 12,
        nonlinear_iterations: 20,
        symbolic_factorizations: 1,
        numeric_factorizations: 12,
        linear_solver_backend: 'klu',
        final_time: 5,
        last_step: 0.25,
        last_error_norm: 0.42,
        maximum_accepted_error_norm: 0.91,
        maximum_error_ratio: 1.4,
        limiting_error_variable: 'drum.total_energy',
        maximum_absolute_normalized_residual: 8e-10,
        limiting_nonlinear_residual: 'component.drum.energy',
        maximum_linear_backward_error: 7e-14,
        structural_block_solves: 30,
        largest_linear_system_size: 8,
        message: 'integration complete',
      },
    } satisfies SimulationResult
    const summary = resultDiagnosticSummary(result)
    expect(summary).toMatchObject({ successful: true, mode: 'transient' })
    expect(summary.facts).toContainEqual({
      label: 'Accepted steps',
      value: '12',
    })
    expect(summary.facts).toContainEqual({
      label: 'Limiting state',
      value: 'drum.total_energy',
    })
    expect(summary.facts).toContainEqual({
      label: 'Limiting constraint',
      value: 'component.drum.energy',
    })
    expect(summary.facts).toContainEqual({
      label: 'Worst linear error',
      value: '7.000e-14',
    })
  })

  it('requires request, execution, and loaded model revisions to agree', () => {
    const provenance: RevisionProvenance = {
      project_id: 'project-1',
      run_configuration_revision_id: 'run-r1',
      run_configuration_checksum: 'sha256:run',
      study_revision_id: 'study-r1',
      study_checksum: 'sha256:study',
      model_revision_id: 'model-r1',
      model_checksum: 'sha256:model',
      case_revision_id: 'case-r1',
      case_checksum: 'sha256:case',
      calibration_revision_id: '',
      calibration_checksum: '',
    }
    expect(
      exactRevisionProvenance(provenance, provenance, 'model-r1', true),
    ).toBe(true)
    expect(
      exactRevisionProvenance(provenance, provenance, 'model-r2', true),
    ).toBe(false)
    expect(
      exactRevisionProvenance(
        provenance,
        { ...provenance, case_checksum: 'sha256:different' },
        'model-r1',
        true,
      ),
    ).toBe(false)
  })
})
