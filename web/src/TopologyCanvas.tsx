import {
  Background,
  BackgroundVariant,
  Controls,
  MiniMap,
  Panel,
  ReactFlow,
  type Connection,
  type Edge,
  type Node,
} from '@xyflow/react'
import '@xyflow/react/dist/style.css'
import { COMPONENT_DRAG_TYPE } from './componentLibrary'
import { TopologyNode, type TopologyNodeData } from './TopologyNode'
import type { GraphSelection } from './InspectorPanel'
import type { ResultNodeValue } from './resultPresentation'
import type { CatalogComponent, TopologyDocument } from './types'

interface TopologyCanvasProps {
  topology?: TopologyDocument
  catalog: CatalogComponent[]
  revisionId: string
  publishing: boolean
  onConnect: (connection: Connection) => Promise<void>
  onSelect: (selection?: GraphSelection) => void
  onAddComponent?: (component: CatalogComponent) => void
  readOnly?: boolean
  resultValues?: Record<string, ResultNodeValue[]>
  selection?: GraphSelection
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
  resultValues: Record<string, ResultNodeValue[]>,
  selection?: GraphSelection,
): Node<TopologyNodeData>[] {
  const columns = Math.max(1, Math.ceil(Math.sqrt(topology.model.components.length)))
  const rowHeight = Object.keys(resultValues).length > 0 ? 310 : 230
  return topology.model.components.map((component, index) => ({
    id: component.id,
    selected:
      selection?.type === 'component' && selection.id === component.id,
    type: 'topology',
    position: {
      x: (index % columns) * 330,
      y: Math.floor(index / columns) * rowHeight,
    },
    data: {
      component,
      ports: catalog.get(component.kind)?.ports ?? [],
      resultValues: resultValues[component.id] ?? [],
    },
  }))
}

function layoutEdges(
  topology: TopologyDocument,
  selection?: GraphSelection,
): Edge[] {
  return topology.model.connections.map((connection) => {
    const [source, sourceHandle] = endpoint(connection.from)
    const [target, targetHandle] = endpoint(connection.to)
    return {
      id: connection.id,
      selected:
        selection?.type === 'connection' && selection.id === connection.id,
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
  revisionId,
  publishing,
  onConnect,
  onSelect,
  onAddComponent,
  readOnly = false,
  resultValues = {},
  selection,
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
  const nodes = layoutNodes(topology, catalogByKind, resultValues, selection)
  const edges = layoutEdges(topology, selection)
  const elementProps = readOnly
    ? { nodes, edges }
    : { defaultNodes: nodes, defaultEdges: edges }
  const componentsById = new Map(
    topology.model.components.map((component) => [component.id, component]),
  )
  const connectionDomain = (
    componentId: string | null,
    handleId: string | null,
  ) => {
    if (!componentId || !handleId) return undefined
    const component = componentsById.get(componentId)
    if (!component) return undefined
    return catalogByKind
      .get(component.kind)
      ?.ports.find((port) => port.name === handleId)?.domain
  }
  const validConnection = (connection: Connection | Edge) => {
    if (
      connection.source === connection.target ||
      !connection.sourceHandle ||
      !connection.targetHandle
    ) {
      return false
    }
    const sourceDomain = connectionDomain(
      connection.source,
      connection.sourceHandle,
    )
    return (
      sourceDomain !== undefined &&
      sourceDomain ===
        connectionDomain(connection.target, connection.targetHandle)
    )
  }

  return (
    <ReactFlow
      key={revisionId}
      {...elementProps}
      nodeTypes={nodeTypes}
      fitView
      fitViewOptions={{ padding: 0.22 }}
      nodesDraggable={!readOnly}
      nodesConnectable={!readOnly && !publishing}
      isValidConnection={validConnection}
      onConnect={(connection) => {
        if (!readOnly) void onConnect(connection)
      }}
      onNodeClick={(_, node) =>
        onSelect({ type: 'component', id: node.id })
      }
      onEdgeClick={(_, edge) =>
        onSelect({ type: 'connection', id: edge.id })
      }
      onPaneClick={() => onSelect(undefined)}
      onDragOver={(event) => {
        if (
          onAddComponent &&
          !readOnly &&
          !publishing &&
          event.dataTransfer.types.includes(COMPONENT_DRAG_TYPE)
        ) {
          event.preventDefault()
          event.dataTransfer.dropEffect = 'copy'
        }
      }}
      onDrop={(event) => {
        if (!onAddComponent || readOnly || publishing) return
        const kind = event.dataTransfer.getData(COMPONENT_DRAG_TYPE)
        const component = catalogByKind.get(kind)
        if (!component) return
        event.preventDefault()
        onAddComponent(component)
      }}
      elementsSelectable
      minZoom={0.2}
      maxZoom={2}
      proOptions={{ hideAttribution: true }}
    >
      {onAddComponent && !readOnly && (
        <Panel position="top-left" className="canvas-authoring-hint">
          Drag a registered component here to add it
        </Panel>
      )}
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
