import { describe, expect, it } from 'vitest'
import { autoLayoutTopology } from './topologyAutoLayout'
import type { CatalogComponent, TopologyDocument } from './types'

const materialMachine = {
  kind: 'machine',
  ports: [
    { name: 'inlet', domain: 'material', direction: 'in' },
    { name: 'outlet', domain: 'material', direction: 'out' },
    { name: 'shaft', domain: 'shaft', direction: 'out' },
  ],
} as CatalogComponent
const generator = {
  kind: 'generator',
  ports: [{ name: 'shaft', domain: 'shaft', direction: 'in' }],
} as CatalogComponent

describe('topology auto layout', () => {
  it('lays out the conserved flow from boundaries through equipment', () => {
    const topology = {
      model: {
        components: [
          { id: 'air', kind: 'machine' },
          { id: 'compressor', kind: 'machine' },
          { id: 'combustor', kind: 'machine' },
          { id: 'turbine', kind: 'machine' },
        ],
        assemblies: [],
        connections: [
          { id: 'a', from: 'air.outlet', to: 'compressor.inlet' },
          { id: 'b', from: 'compressor.outlet', to: 'combustor.inlet' },
          { id: 'c', from: 'combustor.outlet', to: 'turbine.inlet' },
        ],
      },
    } as unknown as TopologyDocument
    const positions = autoLayoutTopology(
      topology,
      new Map([['machine', materialMachine]]),
    )
    const x = Object.fromEntries(positions.map((item) => [item.entity_id, item.x]))

    expect(x.air).toBeLessThan(x.compressor)
    expect(x.compressor).toBeLessThan(x.combustor)
    expect(x.combustor).toBeLessThan(x.turbine)
  })

  it('does not let a shaft loop reverse the material-flow layout', () => {
    const topology = {
      model: {
        components: [
          { id: 'compressor', kind: 'machine' },
          { id: 'turbine', kind: 'machine' },
          { id: 'generator', kind: 'generator' },
        ],
        assemblies: [],
        connections: [
          { id: 'gas', from: 'compressor.outlet', to: 'turbine.inlet' },
          { id: 'drive', from: 'turbine.shaft', to: 'compressor.shaft' },
          { id: 'power', from: 'turbine.shaft', to: 'generator.shaft' },
        ],
      },
    } as unknown as TopologyDocument
    const positions = autoLayoutTopology(
      topology,
      new Map([
        ['machine', materialMachine],
        ['generator', generator],
      ]),
    )
    const x = Object.fromEntries(positions.map((item) => [item.entity_id, item.x]))

    expect(x.compressor).toBeLessThan(x.turbine)
    expect(x.generator).toBeGreaterThan(x.turbine)
  })
})
