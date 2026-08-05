import { describe, expect, it } from 'vitest'
import {
  assemblyPorts,
  buildAssemblyGroupingOperations,
} from './assemblyAuthoring'
import type {
  AssemblyDefinition,
  CatalogComponent,
  TopologyDocument,
} from './types'

const compressor: CatalogComponent = {
  kind: 'compressor.test',
  version: '1.0.0',
  template_kind: 'compressor',
  display_name: 'Test compressor',
  category: 'rotating equipment',
  model_name: 'test',
  system_boundary_role: 'internal',
  supports_steady: true,
  supports_transient: true,
  ports: [
    { name: 'inlet', domain: 'fluid', direction: 'in', maximum_connections: 1 },
    { name: 'outlet', domain: 'fluid', direction: 'out', maximum_connections: 1 },
  ],
  parameters: [],
  artifacts: [],
}

describe('assembly authoring projection', () => {
  it('resolves renamed public ports through nested assemblies', () => {
    const nested: AssemblyDefinition = {
      id: 'section',
      components: [{ id: 'stage', kind: compressor.kind }],
      connections: [],
      ports: [{ name: 'gas_in', endpoint: 'stage.inlet' }],
    }
    const machine: AssemblyDefinition = {
      id: 'machine',
      components: [],
      assemblies: [nested],
      connections: [],
      ports: [{ name: 'inlet', endpoint: 'section.gas_in' }],
    }

    expect(
      assemblyPorts(machine, new Map([[compressor.kind, compressor]])),
    ).toEqual([
      {
        name: 'inlet',
        domain: 'fluid',
        direction: 'in',
        maximum_connections: 1,
      },
    ])
  })

  it('does not invent connector metadata for an unresolved export', () => {
    const assembly: AssemblyDefinition = {
      id: 'invalid',
      components: [],
      connections: [],
      ports: [{ name: 'inlet', endpoint: 'missing.inlet' }],
    }
    expect(assemblyPorts(assembly, new Map())).toEqual([])
  })

  it('groups leaves, moves internal links, and rewrites only boundary links', () => {
    const topology: TopologyDocument = {
      schema_version: 'thermox.topology/v1',
      model: {
        id: 'cycle',
        name: 'Cycle',
        revision: '1',
        media: [],
        components: [
          { id: 'source', kind: 'boundary' },
          { id: 'stage_1', kind: compressor.kind },
          { id: 'stage_2', kind: compressor.kind },
          { id: 'sink', kind: 'boundary' },
        ],
        connections: [
          { id: 'feed', from: 'source.outlet', to: 'stage_1.inlet', kind: 'fluid_link' },
          { id: 'interstage', from: 'stage_1.outlet', to: 'stage_2.inlet', kind: 'fluid_link' },
          { id: 'delivery', from: 'stage_2.outlet', to: 'sink.inlet', kind: 'fluid_link' },
        ],
      },
    }

    const operations = buildAssemblyGroupingOperations(
      topology,
      'compressor_train',
      'Two-stage compressor',
      ['stage_1', 'stage_2'],
    )
    expect(operations.map((operation) => [operation.action, operation.entity_type])).toEqual([
      ['upsert', 'assembly'],
      ['remove', 'component'],
      ['remove', 'component'],
      ['upsert', 'connection'],
      ['upsert', 'connection'],
    ])
    const entity = operations[0].action === 'upsert'
      ? operations[0].entity as unknown as AssemblyDefinition
      : undefined
    expect(entity?.connections.map((connection) => connection.id)).toEqual([
      'interstage',
    ])
    expect(entity?.ports).toEqual([
      { name: 'stage_1_inlet', endpoint: 'stage_1.inlet' },
      { name: 'stage_2_outlet', endpoint: 'stage_2.outlet' },
    ])
    expect(
      operations
        .flatMap((operation) =>
          operation.action === 'upsert' &&
          operation.entity_type === 'connection'
            ? [operation.entity]
            : [],
        ),
    ).toEqual([
      {
        id: 'feed',
        from: 'source.outlet',
        to: 'compressor_train.stage_1_inlet',
        kind: 'fluid_link',
      },
      {
        id: 'delivery',
        from: 'compressor_train.stage_2_outlet',
        to: 'sink.inlet',
        kind: 'fluid_link',
      },
    ])
  })

  it('rejects IDs that collide with any top-level topology entity', () => {
    const topology: TopologyDocument = {
      schema_version: 'thermox.topology/v1',
      model: {
        id: 'cycle',
        name: 'Cycle',
        revision: '1',
        media: [],
        components: [{ id: 'stage', kind: compressor.kind }],
        assemblies: [],
        connections: [],
      },
    }
    expect(() =>
      buildAssemblyGroupingOperations(topology, 'stage', '', ['stage']),
    ).toThrow('already exists')
  })
})
