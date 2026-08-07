import { describe, expect, it } from 'vitest'
import { buildConnectionOperation } from './graphAuthoring'
import type { Catalog, TopologyDocument } from './types'

const topology: TopologyDocument = {
  schema_version: 'thermox.topology/v1',
  model: {
    id: 'test',
    name: 'Test',
    revision: '1',
    media: [],
    components: [
      { id: 'source', kind: 'source.test' },
      { id: 'sink', kind: 'sink.test' },
    ],
    connections: [
      {
        id: 'link_source_outlet_sink_inlet',
        from: 'source.outlet',
        to: 'sink.inlet',
        kind: 'fluid_link',
      },
    ],
  },
}

const catalog: Catalog = {
  schema_version: 'thermox.catalog/v8',
  status: 'succeeded',
  fingerprint: 'test',
  unit_dimensions: [],
  property_backends: [],
  thermochemistry_backends: [],
  correlation_templates: [],
  regime_map_templates: [],
  components: [
    {
      kind: 'source.test',
      version: '1.0.0',
      template_kind: 'source.test',
      display_name: 'Test source',
      category: 'Boundaries',
      model_name: 'Fixed source',
      system_boundary_role: 'source',
      supports_steady: true,
      supports_transient: false,
      ports: [
        {
          name: 'outlet',
          domain: 'fluid',
          direction: 'out',
          maximum_connections: 1,
        },
      ],
      parameters: [],
      artifacts: [],
    },
    {
      kind: 'sink.test',
      version: '1.0.0',
      template_kind: 'sink.test',
      display_name: 'Test sink',
      category: 'Boundaries',
      model_name: 'Fixed sink',
      system_boundary_role: 'sink',
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
      parameters: [],
      artifacts: [],
    },
  ],
  connector_domains: [
    {
      domain: 'fluid',
      connection_kind: 'fluid_link',
      contract_version: 'thermox.connector.fluid/v1',
      variables: [],
    },
  ],
}

describe('buildConnectionOperation', () => {
  it('uses the runtime connector contract and a collision-safe ID', () => {
    expect(
      buildConnectionOperation(
        {
          source: 'source',
          sourceHandle: 'outlet',
          target: 'sink',
          targetHandle: 'inlet',
        },
        topology,
        catalog,
      ),
    ).toEqual({
      action: 'upsert',
      entity_type: 'connection',
      entity_id: 'link_source_outlet_sink_inlet_2',
      entity: {
        id: 'link_source_outlet_sink_inlet_2',
        from: 'source.outlet',
        to: 'sink.inlet',
        kind: 'fluid_link',
        contract_version: 'thermox.connector.fluid/v1',
      },
    })
  })

  it('rejects incomplete and incompatible connections', () => {
    expect(() =>
      buildConnectionOperation(
        {
          source: 'source',
          sourceHandle: null,
          target: 'sink',
          targetHandle: 'inlet',
        },
        topology,
        catalog,
      ),
    ).toThrow('two concrete component ports')

    const incompatible: Catalog = {
      ...catalog,
      components: catalog.components.map((component) =>
        component.kind === 'sink.test'
          ? {
              ...component,
              ports: [
                {
                  ...component.ports[0],
                  domain: 'heat',
                },
              ],
            }
          : component,
      ),
    }
    expect(() =>
      buildConnectionOperation(
        {
          source: 'source',
          sourceHandle: 'outlet',
          target: 'sink',
          targetHandle: 'inlet',
        },
        topology,
        incompatible,
      ),
    ).toThrow('same registered domain')
  })

  it('preserves an existing connection identity during updates', () => {
    const operation = buildConnectionOperation(
      {
        source: 'source',
        sourceHandle: 'outlet',
        target: 'sink',
        targetHandle: 'inlet',
      },
      topology,
      catalog,
      'existing-link',
    )
    expect(operation.entity_id).toBe('existing-link')
    expect(operation.entity.id).toBe('existing-link')
  })
})
