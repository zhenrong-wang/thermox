import { describe, expect, it } from 'vitest'
import { layoutNodes } from './TopologyCanvas'
import type { CatalogComponent, TopologyDocument } from './types'

const componentType: CatalogComponent = {
  kind: 'pump.test',
  version: '1.0.0',
  template_kind: 'pump',
  display_name: 'Pump',
  category: 'Fluid machinery',
  model_name: 'Test pump',
  system_boundary_role: '',
  supports_steady: true,
  supports_transient: true,
  supported_modes: [],
  events: [],
  default_mode: '',
  ports: [],
  parameters: [],
  artifacts: [],
}

const topology: TopologyDocument = {
  schema_version: 'thermox.topology/v1',
  model: {
    id: 'layout-test',
    name: 'Layout test',
    revision: '1',
    media: [],
    components: [{ id: 'pump', kind: componentType.kind }],
    connections: [],
  },
}

describe('topology canvas layout', () => {
  it('preserves an authored position across immutable revision refreshes', () => {
    const position = { x: 420, y: 175 }
    const nodes = layoutNodes(
      topology,
      new Map([[componentType.kind, componentType]]),
      {},
      undefined,
      {},
      new Map([['pump', position]]),
    )

    expect(nodes).toHaveLength(1)
    expect(nodes[0].position).toEqual(position)
  })
})
