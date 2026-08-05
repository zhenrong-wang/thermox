import { describe, expect, it } from 'vitest'
import { assemblyPorts } from './assemblyAuthoring'
import type { AssemblyDefinition, CatalogComponent } from './types'

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
})
