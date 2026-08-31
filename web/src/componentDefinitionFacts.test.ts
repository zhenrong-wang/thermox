import { describe, expect, it } from 'vitest'
import {
  componentDefinitionCounts,
  componentParameterFacts,
} from './componentDefinitionFacts'
import type { CatalogComponent, ComponentDefinition } from './types'

const descriptor = {
  parameters: [
    { name: 'pressure_ratio', dimension: 'dimensionless' },
    { name: 'efficiency', dimension: 'dimensionless' },
    { name: 'speed', dimension: 'angular_speed' },
  ],
} as CatalogComponent

describe('component definition facts', () => {
  it('extracts finite declared parameters in registry order', () => {
    const component = {
      id: 'compressor',
      kind: 'compressor.test',
      parameters: {
        speed: { value_si: 314.159 },
        efficiency: { value: 0.88 },
        pressure_ratio: 14,
        invalid: Number.NaN,
      },
    } satisfies ComponentDefinition

    expect(componentParameterFacts(component, descriptor, 2)).toEqual([
      { name: 'pressure_ratio', dimension: 'dimensionless', valueSi: 14 },
      { name: 'efficiency', dimension: 'dimensionless', valueSi: 0.88 },
    ])
  })

  it('counts physical bindings, parameters, and engineering artifacts', () => {
    expect(componentDefinitionCounts({
      id: 'turbine',
      kind: 'turbine.test',
      media: { inlet: 'gas', outlet: 'gas' },
      materials: { cooling: 'air' },
      parameters: { efficiency: 0.9 },
      artifacts: { performance_map: 'map-1' },
    })).toEqual({ bindings: 3, parameters: 1, artifacts: 1 })
  })
})
