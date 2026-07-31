import { describe, expect, it } from 'vitest'
import {
  defaultSteadySolver,
  defaultTransientSolver,
  scopeRequiresComponent,
  scopeRequiresPort,
  validationMatchesExecutionSelection,
} from './runAuthoring'
import type { ProjectModelValidation } from './types'

describe('run authoring defaults', () => {
  it('matches the service solver defaults', () => {
    expect(defaultSteadySolver.max_iterations).toBe(50)
    expect(defaultSteadySolver.residual_tolerance).toBe(1e-10)
    expect(defaultTransientSolver.end_time).toBe(1)
    expect(defaultTransientSolver.max_steps).toBe(100000)
    expect(defaultTransientSolver.nonlinear_solver).toEqual(
      defaultSteadySolver,
    )
  })

  it('applies generic projection selector requirements', () => {
    expect(scopeRequiresComponent('system_balance')).toBe(false)
    expect(scopeRequiresComponent('component_metric')).toBe(true)
    expect(scopeRequiresPort('component_metric')).toBe(false)
    expect(scopeRequiresPort('port_derived')).toBe(true)
  })

  it('requires validation of the exact execution revision set', () => {
    const validation = {
      model_revision_id: 'model-1',
      case_revision_id: 'case-1',
      artifact_revisions: [
        { artifact_revision_id: 'artifact-2' },
        { artifact_revision_id: 'artifact-1' },
      ],
      validation: { compilation: { compiled: true } },
    } as ProjectModelValidation
    expect(
      validationMatchesExecutionSelection(
        validation,
        'model-1',
        'case-1',
        ['artifact-1', 'artifact-2'],
      ),
    ).toBe(true)
    expect(
      validationMatchesExecutionSelection(
        validation,
        'model-1',
        'case-1',
        ['artifact-1'],
      ),
    ).toBe(false)
    expect(
      validationMatchesExecutionSelection(
        validation,
        'model-2',
        'case-1',
        ['artifact-1', 'artifact-2'],
      ),
    ).toBe(false)
  })
})
