import { describe, expect, it } from 'vitest'
import { validateCorrelationDefinition } from './CorrelationArtifactForm'
import type { CorrelationArtifactDefinition } from './types'

function definition(): CorrelationArtifactDefinition {
  return {
    schema_version: 'thermox.correlation/v1',
    inputs: [
      { name: 'mass_flow', dimension: 'mass_flow' },
      { name: 'density', dimension: 'density' },
    ],
    output: { name: 'pressure_loss', dimension: 'pressure' },
    candidates: [{
      id: 'default',
      regime: 'general',
      priority: 0,
      coefficients: { coefficient: 1.5 },
      expression: 'coefficient * mass_flow * abs(mass_flow) / density',
      applicability: [{
        input: 'mass_flow', minimum: 0, maximum: 20,
        minimum_inclusive: true, maximum_inclusive: false,
      }],
    }],
  }
}

describe('correlation authoring validation', () => {
  it('accepts distinct typed symbols and a finite coefficient', () => {
    expect(validateCorrelationDefinition(definition())).toEqual([])
  })

  it('rejects duplicate or invalid symbols before publication', () => {
    const invalid = definition()
    invalid.inputs[1].name = 'mass_flow'
    invalid.candidates[0].coefficients = { 'unsafe-name': Number.NaN }
    expect(validateCorrelationDefinition(invalid)).toEqual(
      expect.arrayContaining([
        'Symbol "mass_flow" is duplicated.',
        'Coefficient "unsafe-name" is not a valid expression identifier.',
        'Coefficient "unsafe-name" must be finite.',
      ]),
    )
  })

  it('rejects unknown, duplicate, or empty applicability ranges', () => {
    const invalid = definition()
    invalid.candidates[0].applicability = [
      {
        input: 'unknown',
        minimum_inclusive: true,
        maximum_inclusive: true,
      },
      {
        input: 'unknown',
        minimum: 2,
        maximum: 1,
        minimum_inclusive: true,
        maximum_inclusive: true,
      },
    ]
    expect(validateCorrelationDefinition(invalid)).toEqual(
      expect.arrayContaining([
        'Applicability input "unknown" is not declared.',
        'Applicability input "unknown" is duplicated.',
        'Applicability input "unknown" needs a minimum or maximum.',
        'Applicability range for "unknown" is empty.',
      ]),
    )
  })
})
