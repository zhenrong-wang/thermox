import { describe, expect, it } from 'vitest'
import type { FinalConnectionState } from '@xyflow/react'
import {
  centeredComponentPosition,
  finalConnectionIntent,
  layoutEdges,
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
  ports: [
    {
      name: 'inlet',
      domain: 'fluid',
      direction: 'in',
      maximum_connections: 1,
      medium_source_port: '',
    },
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

  it('derives edge presentation from the registered connector domain', () => {
    const graph = {
      ...topology,
      model: {
        ...topology.model,
        components: [
          { id: 'pump', kind: componentType.kind },
          { id: 'pump-2', kind: componentType.kind },
        ],
        connections: [
          {
            id: 'fluid-link',
            kind: 'thermox.fluid_connection/v1',
            from: 'pump.outlet',
            to: 'pump-2.inlet',
          },
        ],
      },
    } as TopologyDocument
    const edges = layoutEdges(
      graph,
      new Map([[componentType.kind, componentType]]),
    )

    expect(edges[0].label).toBe('fluid')
    expect(edges[0].style).toMatchObject({
      stroke: '#2f8bd8',
      strokeWidth: 2,
    })
  })
})
