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
  schema_version: 'thermox.catalog/v14',
  status: 'succeeded',
  fingerprint: 'test',
  unit_dimensions: [],
  property_backends: [],
  thermochemistry_backends: [],
  correlation_templates: [],
  correlation_family_templates: [],
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
      supported_modes: [],
      events: [],
      default_mode: '',
      ports: [
        {
          name: 'outlet',
          domain: 'fluid',
          direction: 'out',
          maximum_connections: 1,
          medium_source_port: '',
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
      supported_modes: [],
      events: [],
      default_mode: '',
      ports: [
        {
          name: 'inlet',
          domain: 'fluid',
          direction: 'in',
          maximum_connections: 1,
          medium_source_port: '',
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
  it('uses the runtime connector contract and a deterministic ID', () => {
    const disconnected = {
      ...topology,
      model: { ...topology.model, connections: [] },
    }
    expect(
      buildConnectionOperation(
        {
          source: 'source',
          sourceHandle: 'outlet',
          target: 'sink',
          targetHandle: 'inlet',
        },
        disconnected,
        catalog,
      ),
    ).toEqual({
      action: 'upsert',
      entity_type: 'connection',
      entity_id: 'link_source_outlet_sink_inlet',
      entity: {
        id: 'link_source_outlet_sink_inlet',
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
      'link_source_outlet_sink_inlet',
    )
    expect(operation.entity_id).toBe('link_source_outlet_sink_inlet')
    expect(operation.entity.id).toBe('link_source_outlet_sink_inlet')
  })

  it('rejects reversed, duplicate, and capacity-exhausted endpoints', () => {
    expect(() =>
      buildConnectionOperation(
        {
          source: 'sink',
          sourceHandle: 'inlet',
          target: 'source',
          targetHandle: 'outlet',
        },
        topology,
        catalog,
      ),
    ).toThrow('cannot be used as a source')

    expect(() =>
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
    ).toThrow('already exists')

    const secondSink: TopologyDocument = {
      ...topology,
      model: {
        ...topology.model,
        components: [
          ...topology.model.components,
          { id: 'sink_2', kind: 'sink.test' },
        ],
      },
    }
    expect(() =>
      buildConnectionOperation(
        {
          source: 'source',
          sourceHandle: 'outlet',
          target: 'sink_2',
          targetHandle: 'inlet',
        },
        secondSink,
        catalog,
      ),
    ).toThrow('source.outlet has reached its connection limit')
  })

  it('connects through a collapsed assembly public port', () => {
    const withAssembly: TopologyDocument = {
      ...topology,
      model: {
        ...topology.model,
        components: [{ id: 'sink', kind: 'sink.test' }],
        connections: [],
        assemblies: [
          {
            id: 'source_train',
            components: [{ id: 'source', kind: 'source.test' }],
            connections: [],
            ports: [{ name: 'outlet', endpoint: 'source.outlet' }],
          },
        ],
      },
    }
    expect(
      buildConnectionOperation(
        {
          source: 'source_train',
          sourceHandle: 'outlet',
          target: 'sink',
          targetHandle: 'inlet',
        },
        withAssembly,
        catalog,
      ).entity,
    ).toMatchObject({
      from: 'source_train.outlet',
      to: 'sink.inlet',
      kind: 'fluid_link',
    })
  })
})
