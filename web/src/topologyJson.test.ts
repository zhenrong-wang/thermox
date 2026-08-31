import { describe, expect, it } from 'vitest'
import { reviewTopologyJson, topologyJsonText } from './topologyJson'
import type { TopologyDocument } from './types'

const topology: TopologyDocument = {
  schema_version: 'thermox.topology/v1',
  model: {
    id: 'json-cycle',
    name: 'JSON cycle',
    revision: '1',
    media: [{ id: 'water', backend: 'coolprop', substance: 'Water' }],
    materials: [],
    components: [
      { id: 'source', kind: 'source.fluid.boundary' },
      { id: 'sink', kind: 'sink.fluid.boundary' },
    ],
    assemblies: [],
    connections: [{
      id: 'flow',
      from: 'source.outlet',
      to: 'sink.inlet',
      kind: 'fluid_link',
    }],
  },
}

describe('topology JSON workbench contract', () => {
  it('round-trips the public topology declaration without a UI-only shape', () => {
    const text = topologyJsonText(topology)
    const review = reviewTopologyJson(text)

    expect(review.issues).toEqual([])
    expect(review.document).toEqual(topology)
    expect(review.summary).toMatchObject({
      modelId: 'json-cycle',
      componentCount: 2,
      connectionCount: 1,
      mediumCount: 1,
    })
  })

  it('blocks malformed declarations before authoritative publication', () => {
    const malformed = structuredClone(topology) as unknown as {
      schema_version: string
      model: TopologyDocument['model']
    }
    malformed.schema_version = 'unknown'
    malformed.model.components.push({ id: 'source', kind: 'duplicate' })
    malformed.model.connections[0].to = 'missing.inlet'

    const review = reviewTopologyJson(JSON.stringify(malformed))
    expect(review.document).toBeUndefined()
    expect(review.issues).toEqual(expect.arrayContaining([
      'schema_version must be thermox.topology/v1.',
      'model.components contains duplicate id "source".',
      'model.connections[0].to references unknown top-level entity "missing".',
    ]))
  })

  it('reports syntax errors without throwing from the editor render path', () => {
    expect(reviewTopologyJson('{').issues[0]).toContain('JSON')
  })
})
