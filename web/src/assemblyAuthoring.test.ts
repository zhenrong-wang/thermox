import { describe, expect, it } from 'vitest'
import {
  assemblyPorts,
  buildAssemblyGroupingOperations,
  buildAssemblyTemplateDocument,
  buildAssemblyTemplateInstantiationOperations,
  buildAssemblyUngroupingOperations,
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
  supported_modes: [],
  events: [],
  default_mode: '',
  ports: [
    { name: 'inlet', domain: 'fluid', direction: 'in', maximum_connections: 1, medium_source_port: '' },
    { name: 'outlet', domain: 'fluid', direction: 'out', maximum_connections: 1, medium_source_port: '' },
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
        medium_source_port: '',
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

  it('exports only explicitly defined child parameters under public names', () => {
    const topology: TopologyDocument = {
      schema_version: 'thermox.topology/v1',
      model: {
        id: 'cycle',
        name: 'Cycle',
        revision: '1',
        media: [],
        components: [{
          id: 'stage',
          kind: compressor.kind,
          parameters: { pressure_ratio: 3, efficiency: 0.88 },
        }],
        connections: [],
      },
    }
    const operations = buildAssemblyGroupingOperations(
      topology,
      'train',
      '',
      ['stage'],
      [{ name: 'design_pressure_ratio', target: 'stage.pressure_ratio' }],
    )
    const assembly = operations[0].action === 'upsert'
      ? operations[0].entity as unknown as AssemblyDefinition
      : undefined
    expect(assembly?.parameters).toEqual([
      { name: 'design_pressure_ratio', target: 'stage.pressure_ratio' },
    ])
    expect(() =>
      buildAssemblyGroupingOperations(
        topology,
        'train',
        '',
        ['stage'],
        [{ name: 'map_scale', target: 'stage.map_scale' }],
      ),
    ).toThrow('must target a defined parameter')
  })

  it('rejects duplicate public parameter names and targets', () => {
    const topology: TopologyDocument = {
      schema_version: 'thermox.topology/v1',
      model: {
        id: 'cycle',
        name: 'Cycle',
        revision: '1',
        media: [],
        components: [{
          id: 'stage',
          kind: compressor.kind,
          parameters: { pressure_ratio: 3, efficiency: 0.88 },
        }],
        connections: [],
      },
    }
    expect(() =>
      buildAssemblyGroupingOperations(
        topology,
        'train',
        '',
        ['stage'],
        [
          { name: 'setting', target: 'stage.pressure_ratio' },
          { name: 'setting', target: 'stage.efficiency' },
        ],
      ),
    ).toThrow('name setting is duplicated')
    expect(() =>
      buildAssemblyGroupingOperations(
        topology,
        'train',
        '',
        ['stage'],
        [
          { name: 'ratio_a', target: 'stage.pressure_ratio' },
          { name: 'ratio_b', target: 'stage.pressure_ratio' },
        ],
      ),
    ).toThrow('exported more than once')
  })

  it('ungroups children and restores internal and boundary connections', () => {
    const topology: TopologyDocument = {
      schema_version: 'thermox.topology/v1',
      model: {
        id: 'cycle',
        name: 'Cycle',
        revision: '2',
        media: [],
        components: [
          { id: 'source', kind: 'boundary' },
          { id: 'sink', kind: 'boundary' },
        ],
        assemblies: [{
          id: 'compressor_train',
          components: [
            { id: 'stage_1', kind: compressor.kind },
            { id: 'stage_2', kind: compressor.kind },
          ],
          connections: [{
            id: 'interstage',
            from: 'stage_1.outlet',
            to: 'stage_2.inlet',
            kind: 'fluid_link',
          }],
          ports: [
            { name: 'stage_1_inlet', endpoint: 'stage_1.inlet' },
            { name: 'stage_2_outlet', endpoint: 'stage_2.outlet' },
          ],
        }],
        connections: [
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
        ],
      },
    }

    const operations = buildAssemblyUngroupingOperations(
      topology,
      'compressor_train',
    )
    expect(
      operations.map((operation) => [operation.action, operation.entity_type]),
    ).toEqual([
      ['upsert', 'component'],
      ['upsert', 'component'],
      ['upsert', 'connection'],
      ['upsert', 'connection'],
      ['upsert', 'connection'],
      ['remove', 'assembly'],
    ])
    expect(
      operations.flatMap((operation) =>
        operation.action === 'upsert' &&
        operation.entity_type === 'connection' &&
        operation.entity_id !== 'interstage'
          ? [operation.entity]
          : [],
      ),
    ).toEqual([
      {
        id: 'feed',
        from: 'source.outlet',
        to: 'stage_1.inlet',
        kind: 'fluid_link',
      },
      {
        id: 'delivery',
        from: 'stage_2.outlet',
        to: 'sink.inlet',
        kind: 'fluid_link',
      },
    ])
  })

  it('refuses to overwrite a top-level entity while ungrouping', () => {
    const topology: TopologyDocument = {
      schema_version: 'thermox.topology/v1',
      model: {
        id: 'cycle',
        name: 'Cycle',
        revision: '2',
        media: [],
        components: [{ id: 'stage', kind: 'outside.kind' }],
        assemblies: [{
          id: 'train',
          components: [{ id: 'stage', kind: compressor.kind }],
          connections: [],
          ports: [],
        }],
        connections: [],
      },
    }
    expect(() =>
      buildAssemblyUngroupingOperations(topology, 'train'),
    ).toThrow('topology entity stage already exists')
  })

  it('packages one assembly with only its registry dependencies', () => {
    const assembly: AssemblyDefinition = {
      id: 'train',
      components: [{
        id: 'stage',
        kind: compressor.kind,
        media: { inlet: 'air', outlet: 'air' },
      }],
      connections: [],
      ports: [],
    }
    const topology: TopologyDocument = {
      schema_version: 'thermox.topology/v1',
      model: {
        id: 'cycle',
        name: 'Cycle',
        revision: '1',
        media: [
          { id: 'air', backend: 'ideal', substance: 'Air' },
          { id: 'water', backend: 'coolprop', substance: 'Water' },
        ],
        components: [],
        assemblies: [assembly],
        connections: [],
      },
    }
    const template = buildAssemblyTemplateDocument(topology, assembly)
    expect(template.model.media.map((medium) => medium.id)).toEqual(['air'])
    expect(template.model.assemblies).toEqual([assembly])
    expect(template.model.components).toEqual([])
  })

  it('instantiates a template and atomically adds missing dependencies', () => {
    const target: TopologyDocument = {
      schema_version: 'thermox.topology/v1',
      model: {
        id: 'cycle',
        name: 'Cycle',
        revision: '1',
        media: [],
        components: [],
        connections: [],
      },
    }
    const template: TopologyDocument = {
      schema_version: 'thermox.topology/v1',
      model: {
        id: 'train_template',
        name: 'Train',
        revision: '1',
        media: [{ id: 'air', backend: 'ideal', substance: 'Air' }],
        components: [],
        assemblies: [{
          id: 'train',
          label: 'Compressor train',
          components: [{
            id: 'stage',
            kind: compressor.kind,
            media: { inlet: 'air', outlet: 'air' },
          }],
          connections: [],
          ports: [],
        }],
        connections: [],
      },
    }
    const operations = buildAssemblyTemplateInstantiationOperations(
      target,
      template,
      'main_compressor',
      'Main compressor',
    )
    expect(operations.map((operation) => operation.entity_type)).toEqual([
      'medium',
      'assembly',
    ])
    expect(
      operations[1].action === 'upsert' ? operations[1].entity : undefined,
    ).toMatchObject({ id: 'main_compressor', label: 'Main compressor' })
  })

  it('rejects incompatible template dependencies', () => {
    const target: TopologyDocument = {
      schema_version: 'thermox.topology/v1',
      model: {
        id: 'cycle',
        name: 'Cycle',
        revision: '1',
        media: [{ id: 'air', backend: 'coolprop', substance: 'Air' }],
        components: [],
        connections: [],
      },
    }
    const template: TopologyDocument = {
      schema_version: 'thermox.topology/v1',
      model: {
        id: 'template',
        name: 'Template',
        revision: '1',
        media: [{ id: 'air', backend: 'ideal', substance: 'Air' }],
        components: [],
        assemblies: [{
          id: 'train',
          components: [{ id: 'stage', kind: compressor.kind }],
          connections: [],
          ports: [],
        }],
        connections: [],
      },
    }
    expect(() =>
      buildAssemblyTemplateInstantiationOperations(
        target,
        template,
        'train',
        '',
      ),
    ).toThrow('conflicts with the template dependency')
  })
})
