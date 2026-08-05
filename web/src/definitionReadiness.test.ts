import { describe, expect, it } from 'vitest'
import {
  componentDefinitionReadiness,
  definitionIssues,
} from './definitionReadiness'
import type { Catalog, TopologyDocument } from './types'

const catalog = {
  components: [
    {
      kind: 'test.compressor',
      version: '1.0.0',
      system_boundary_role: 'none',
      supports_steady: true,
      supports_transient: false,
      ports: [
        {
          name: 'inlet',
          domain: 'fluid',
          direction: 'in',
          maximum_connections: 1,
        },
      ],
      parameters: [
        {
          name: 'efficiency',
          dimension: 'dimensionless',
          required: true,
          default_value_si: null,
          lower_bound: 0,
          upper_bound: 1,
          lower_inclusive: false,
          upper_inclusive: true,
        },
      ],
      artifacts: [{ role: 'map', required: true, artifact_type: 'perfmap' }],
    },
  ],
} as Catalog

function topology(component: TopologyDocument['model']['components'][number]) {
  return {
    schema_version: 'thermox.topology/v1',
    model: {
      id: 'model',
      name: 'Model',
      revision: '1',
      media: [{ id: 'air', backend: 'ideal', substance: 'air' }],
      materials: [],
      components: [component],
      connections: [],
    },
  } as TopologyDocument
}

describe('definition readiness hints', () => {
  it('identifies missing catalog-driven component inputs', () => {
    const issues = definitionIssues(
      topology({ id: 'c1', kind: 'test.compressor' }),
      catalog,
    )

    expect(issues.map((issue) => issue.kind)).toEqual([
      'binding',
      'parameter',
      'artifact',
    ])
  })

  it('accepts complete bindings, parameters, and artifacts', () => {
    const issues = definitionIssues(
      topology({
        id: 'c1',
        kind: 'test.compressor',
        media: { inlet: 'air' },
        parameters: { efficiency: 0.88 },
        artifacts: { map: 'compressor-map' },
      }),
      catalog,
    )

    expect(issues).toEqual([])
  })

  it('flags model types that cannot be resolved from the catalog', () => {
    const issues = definitionIssues(
      topology({ id: 'legacy', kind: 'missing.kind' }),
      catalog,
    )

    expect(issues).toHaveLength(1)
    expect(issues[0].kind).toBe('catalog')
  })

  it('distinguishes untouched drafts from partial physical definitions', () => {
    expect(
      componentDefinitionReadiness(
        topology({ id: 'c1', kind: 'test.compressor' }),
        catalog,
      ).c1.state,
    ).toBe('draft')

    expect(
      componentDefinitionReadiness(
        topology({
          id: 'c1',
          kind: 'test.compressor',
          parameters: { efficiency: 0.88 },
        }),
        catalog,
    ).c1.state,
    ).toBe('incomplete')
  })

  it('projects descendant readiness onto a collapsed assembly', () => {
    const document = topology({ id: 'outside', kind: 'missing.kind' })
    document.model.components = []
    document.model.assemblies = [{
      id: 'compressor_train',
      components: [{
        id: 'stage_1',
        kind: 'test.compressor',
        media: { inlet: 'air' },
        parameters: { efficiency: 0.88 },
        artifacts: { map: 'stage-map' },
      }],
      connections: [],
      ports: [{ name: 'inlet', endpoint: 'stage_1.inlet' }],
    }]

    const readiness = componentDefinitionReadiness(document, catalog)
    expect(readiness['compressor_train/stage_1'].state).toBe('defined')
    expect(readiness.compressor_train.state).toBe('defined')
  })
})
