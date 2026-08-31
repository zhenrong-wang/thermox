import { describe, expect, it } from 'vitest'
import { boundaryEndpoints } from './BalanceUncertaintyArtifactForm'
import type { Catalog, TopologyDocument } from './types'

describe('balance uncertainty authoring', () => {
  it('offers only connected one-direction boundary component ports', () => {
    const topology = {
      model: {
        components: [
          { id: 'feed', kind: 'source' },
          { id: 'machine', kind: 'machine' },
          { id: 'product', kind: 'sink' },
        ],
        connections: [
          { from: 'feed.outlet', to: 'machine.inlet' },
          { from: 'machine.outlet', to: 'product.inlet' },
        ],
      },
    } as TopologyDocument
    const catalog = {
      components: [
        { kind: 'source', ports: [
          { name: 'outlet', domain: 'fluid', direction: 'out' },
        ] },
        { kind: 'machine', ports: [
          { name: 'inlet', domain: 'fluid', direction: 'in' },
          { name: 'outlet', domain: 'fluid', direction: 'out' },
        ] },
        { kind: 'sink', ports: [
          { name: 'inlet', domain: 'fluid', direction: 'in' },
        ] },
      ],
    } as Catalog

    expect(boundaryEndpoints(topology, catalog).map((item) => item.key))
      .toEqual(['feed.outlet', 'product.inlet'])
  })
})
