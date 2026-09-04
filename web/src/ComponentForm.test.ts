import { describe, expect, it } from 'vitest'
import { instanceParameterDescriptors } from './ComponentForm'
import type { CatalogComponent, TopologyDocument } from './types'

describe('component instance parameter descriptors', () => {
  it('expands species-keyed catalog parameters from bound materials', () => {
    const component = {
      ports: [{ name: 'outlet', domain: 'material', direction: 'out' }],
      parameters: [
        {
          name: 'reference_mass_flow',
          dimension: 'mass_flow',
          required: true,
          default_value_si: null,
        },
        {
          name: 'mass_fraction[{species}]',
          dimension: 'dimensionless',
          required: false,
          default_value_si: 0,
        },
      ],
    } as CatalogComponent
    const topology = {
      model: {
        materials: [{
          id: 'gas',
          backend: 'cantera',
          mechanism: 'gri30.yaml',
          phase: 'gri30',
          species: ['N2', 'O2', 'CH4'],
        }],
      },
    } as TopologyDocument

    expect(instanceParameterDescriptors(component, topology, { outlet: 'gas' })
      .map((parameter) => parameter.name))
      .toEqual([
        'reference_mass_flow',
        'mass_fraction[N2]',
        'mass_fraction[O2]',
        'mass_fraction[CH4]',
      ])
  })
})
