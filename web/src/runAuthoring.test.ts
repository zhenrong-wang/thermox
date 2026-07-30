import { describe, expect, it } from 'vitest'
import {
  defaultSteadySolver,
  defaultTransientSolver,
  scopeRequiresComponent,
  scopeRequiresPort,
} from './runAuthoring'

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
})
