import { describe, expect, it } from 'vitest'
import type { FinalConnectionState } from '@xyflow/react'
import {
  centeredComponentPosition,
  finalConnectionIntent,
  layoutNodes,
} from './TopologyCanvas'
import { presentationFromNodes } from './topologyPresentation'
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

  it('centers a dragged component on the pointer and snapshots its viewport', () => {
    expect(centeredComponentPosition({ x: 500, y: 300 })).toEqual({
      x: 375,
      y: 241,
    })
    expect(
      presentationFromNodes(
        [{ id: 'pump', position: { x: 375, y: 241 } }],
        { x: 12, y: 18, zoom: 0.8 },
      ),
    ).toEqual({
      schema_version: 'thermox.topology_presentation/v1',
      nodes: [{ entity_id: 'pump', x: 375, y: 241 }],
      viewport: { x: 12, y: 18, zoom: 0.8 },
    })
  })

  it('normalizes a connection drawn from either handle direction', () => {
    const state = {
      fromHandle: { type: 'target', id: 'inlet' },
      fromNode: { id: 'sink' },
      toHandle: { type: 'source', id: 'outlet' },
      toNode: { id: 'source' },
    } as unknown as FinalConnectionState

    expect(finalConnectionIntent(state)).toEqual({
      source: 'source',
      sourceHandle: 'outlet',
      target: 'sink',
      targetHandle: 'inlet',
    })
  })
})
