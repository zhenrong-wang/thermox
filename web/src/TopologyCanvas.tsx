import {
  Background,
  BackgroundVariant,
  Controls,
  MiniMap,
  ReactFlow,
  type Edge,
  type Node,
} from '@xyflow/react'
import '@xyflow/react/dist/style.css'
import { TopologyNode, type TopologyNodeData } from './TopologyNode'
import type { CatalogComponent, TopologyDocument } from './types'

interface TopologyCanvasProps {
  topology?: TopologyDocument
  catalog: CatalogComponent[]
}

function endpoint(value: string): [string, string] {
  const separator = value.lastIndexOf('.')
  return separator === -1
    ? [value, '']
    : [value.slice(0, separator), value.slice(separator + 1)]
}

function layoutNodes(
  topology: TopologyDocument,
  catalog: Map<string, CatalogComponent>,
): Node<TopologyNodeData>[] {
  const columns = Math.max(1, Math.ceil(Math.sqrt(topology.model.components.length)))
  return topology.model.components.map((component, index) => ({
    id: component.id,
    type: 'topology',
    position: {
      x: (index % columns) * 330,
      y: Math.floor(index / columns) * 230,
    },
    data: {
      component,
      ports: catalog.get(component.kind)?.ports ?? [],
    },
  }))
}

function layoutEdges(topology: TopologyDocument): Edge[] {
  return topology.model.connections.map((connection) => {
    const [source, sourceHandle] = endpoint(connection.from)
    const [target, targetHandle] = endpoint(connection.to)
    return {
      id: connection.id,
      source,
      sourceHandle,
      target,
      targetHandle,
      type: 'smoothstep',
      label: connection.kind,
      labelStyle: { fill: '#617083', fontSize: 10 },
      style: { stroke: '#8795a6', strokeWidth: 1.5 },
    }
  })
}

const nodeTypes = { topology: TopologyNode }

export function TopologyCanvas({
  topology,
  catalog,
}: TopologyCanvasProps) {
  if (!topology) {
    return (
      <div className="empty-canvas">
        <div className="empty-orbit" />
        <h2>Select a topology revision</h2>
        <p>The immutable system graph will appear here.</p>
      </div>
    )
  }

  const catalogByKind = new Map(catalog.map((item) => [item.kind, item]))
  const nodes = layoutNodes(topology, catalogByKind)
  const edges = layoutEdges(topology)

  return (
    <ReactFlow
      nodes={nodes}
      edges={edges}
      nodeTypes={nodeTypes}
      fitView
      fitViewOptions={{ padding: 0.22 }}
      nodesDraggable
      nodesConnectable={false}
      elementsSelectable
      minZoom={0.2}
      maxZoom={2}
      proOptions={{ hideAttribution: true }}
    >
      <Background
        color="#cbd4dd"
        gap={22}
        size={1}
        variant={BackgroundVariant.Dots}
      />
      <MiniMap
        pannable
        zoomable
        nodeColor="#183d5d"
        maskColor="rgba(239, 244, 247, 0.72)"
      />
      <Controls showInteractive={false} />
    </ReactFlow>
  )
}
