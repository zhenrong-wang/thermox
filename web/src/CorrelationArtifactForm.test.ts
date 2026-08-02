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
    coefficients: { coefficient: 1.5 },
    expression: 'coefficient * mass_flow * abs(mass_flow) / density',
  }
}

describe('correlation authoring validation', () => {
  it('accepts distinct typed symbols and a finite coefficient', () => {
    expect(validateCorrelationDefinition(definition())).toEqual([])
  })

  it('rejects duplicate or invalid symbols before publication', () => {
    const invalid = definition()
    invalid.inputs[1].name = 'mass_flow'
    invalid.coefficients = { 'unsafe-name': Number.NaN }
    expect(validateCorrelationDefinition(invalid)).toEqual(
      expect.arrayContaining([
        'Symbol "mass_flow" is duplicated.',
        'Coefficient "unsafe-name" is not a valid expression identifier.',
        'Coefficient "unsafe-name" must be finite.',
      ]),
    )
  })
})
