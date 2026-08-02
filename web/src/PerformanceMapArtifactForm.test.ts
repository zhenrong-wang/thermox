import { describe, expect, it } from 'vitest'
import {
  parsePerformanceMapSamples,
  validatePerformanceMapDefinition,
} from './PerformanceMapArtifactForm'
import type { PerformanceMapArtifactDefinition } from './types'

function mapDefinition(): PerformanceMapArtifactDefinition {
  return {
    primary_variable: { name: 'corrected_mass_flow', dimension: 'mass_flow' },
    family_variable: { name: 'corrected_speed', dimension: 'angular_speed' },
    output_variables: [
      { name: 'pressure_ratio', dimension: 'dimensionless' },
      { name: 'isentropic_efficiency', dimension: 'dimensionless' },
    ],
    curves: [
      {
        family_coordinate: 250,
        samples: [
          { coordinate: 70, outputs: [10, 0.82] },
          { coordinate: 120, outputs: [8, 0.84] },
        ],
      },
      {
        family_coordinate: 400,
        samples: [
          { coordinate: 70, outputs: [12, 0.84] },
          { coordinate: 120, outputs: [10, 0.86] },
        ],
      },
    ],
    primary_extrapolation: 'reject',
    family_extrapolation: 'reject',
  }
}

describe('performance map authoring', () => {
  it('parses comma or whitespace separated non-rectangular sample rows', () => {
    expect(parsePerformanceMapSamples('70, 10, 0.82\n120 8 0.84', 2)).toEqual([
      { coordinate: 70, outputs: [10, 0.82] },
      { coordinate: 120, outputs: [8, 0.84] },
    ])
  })

  it('accepts a typed ordered map', () => {
    expect(validatePerformanceMapDefinition(mapDefinition())).toEqual([])
  })

  it('rejects duplicate variables and unordered coordinates', () => {
    const definition = mapDefinition()
    definition.output_variables[0].name = 'corrected_speed'
    definition.curves[1].family_coordinate = 200
    definition.curves[0].samples[1].coordinate = 60
    expect(validatePerformanceMapDefinition(definition)).toEqual(
      expect.arrayContaining([
        'Map variable "corrected_speed" is duplicated.',
        'Curve 1 sample 2 coordinate must be finite and strictly increasing.',
        'Curve 2 family coordinate must be finite and strictly increasing.',
      ]),
    )
  })

  it('rejects sample rows that do not match the output contract', () => {
    expect(() => parsePerformanceMapSamples('70, 10', 2)).toThrow(
      'Sample line 1 needs one finite primary coordinate and 2 finite output values.',
    )
  })
})
