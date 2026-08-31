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
  type XYPosition,
} from '@xyflow/react'
import { useRef } from 'react'
import '@xyflow/react/dist/style.css'
import { COMPONENT_DRAG_TYPE } from './componentLibrary'
import { assemblyPorts } from './assemblyAuthoring'
import { TopologyNode, type TopologyNodeData } from './TopologyNode'
import type { GraphSelection } from './InspectorPanel'
import type { ResultNodeValue } from './resultPresentation'
import type { CatalogComponent, TopologyDocument } from './types'
import type { ComponentDefinitionReadiness } from './definitionReadiness'
import { connectionIntentIssue } from './graphAuthoring'

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
  onCreateTopology?: () => void
  componentReadiness?: Record<string, ComponentDefinitionReadiness>
}

function endpoint(value: string): [string, string] {
  const separator = value.lastIndexOf('.')
  return separator === -1
    ? [value, '']
    : [value.slice(0, separator), value.slice(separator + 1)]
}

export function layoutNodes(
  topology: TopologyDocument,
  catalog: Map<string, CatalogComponent>,
  resultValues: Record<string, ResultNodeValue[]>,
  selection?: GraphSelection,
  componentReadiness: Record<string, ComponentDefinitionReadiness> = {},
  savedPositions: ReadonlyMap<string, XYPosition> = new Map(),
): Node<TopologyNodeData>[] {
  const entities = [
    ...topology.model.components.map((component) => ({
      component,
      assembly: undefined,
    })),
    ...(topology.model.assemblies ?? []).map((assembly) => ({
      assembly,
      component: {
        id: assembly.id,
        label: assembly.label,
        kind: 'assembly.meta',
      },
    })),
  ]
  const columns = Math.max(1, Math.ceil(Math.sqrt(entities.length)))
  const rowHeight = Object.keys(resultValues).length > 0 ? 310 : 230
  return entities.map(({ component, assembly }, index) => ({
    id: component.id,
    selected:
      (selection?.type === 'component' || selection?.type === 'assembly') &&
      selection.id === component.id,
    type: 'topology',
    position:
      savedPositions.get(component.id) ?? {
        x: (index % columns) * 330,
        y: Math.floor(index / columns) * rowHeight,
      },
    data: {
      component,
      assembly,
      ports: assembly
        ? assemblyPorts(assembly, catalog)
        : catalog.get(component.kind)?.ports ?? [],
      resultValues: resultValues[component.id] ?? [],
      definition: componentReadiness[component.id],
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
  onCreateTopology,
  componentReadiness = {},
}: TopologyCanvasProps) {
  const savedPositions = useRef(new Map<string, XYPosition>())
  if (!topology) {
    return (
      <div className="empty-canvas">
        <div className="empty-orbit" />
        <h2>Select a topology revision</h2>
        <p>
          {onCreateTopology
            ? 'Create the first immutable revision, then build with registered components.'
            : 'The immutable system graph will appear here.'}
        </p>
        {onCreateTopology && (
          <button
            type="button"
            className="primary-button"
            disabled={publishing}
            onClick={onCreateTopology}
          >
            {publishing ? 'Creating…' : 'Create topology'}
          </button>
        )}
      </div>
    )
  }

  const catalogByKind = new Map(catalog.map((item) => [item.kind, item]))
  const nodes = layoutNodes(
    topology,
    catalogByKind,
    resultValues,
    selection,
    componentReadiness,
    savedPositions.current,
  )
  const edges = layoutEdges(topology, selection)
  const elementProps = readOnly
    ? { nodes, edges }
    : { defaultNodes: nodes, defaultEdges: edges }
  const validConnection = (connection: Connection | Edge) => {
    return !connectionIntentIssue(
      {
        source: connection.source,
        sourceHandle: connection.sourceHandle ?? null,
        target: connection.target,
        targetHandle: connection.targetHandle ?? null,
      },
      topology,
      catalog,
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
      onNodeDragStop={(_, node) => {
        if (!readOnly) savedPositions.current.set(node.id, node.position)
      }}
      onNodeClick={(_, node) =>
        onSelect({
          type: (node.data as TopologyNodeData).assembly
            ? 'assembly'
            : 'component',
          id: node.id,
        })
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
