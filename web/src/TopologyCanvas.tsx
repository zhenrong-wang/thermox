import {
  Background,
  BackgroundVariant,
  Controls,
  MiniMap,
  Panel,
  ReactFlow,
  type Connection,
  type Edge,
  type FinalConnectionState,
  type Node,
  type ReactFlowInstance,
  type Viewport,
  type XYPosition,
} from '@xyflow/react'
import { useRef, useState, type KeyboardEvent } from 'react'
import '@xyflow/react/dist/style.css'
import { COMPONENT_DRAG_TYPE } from './componentLibrary'
import { assemblyPorts } from './assemblyAuthoring'
import { TopologyNode, type TopologyNodeData } from './TopologyNode'
import type { GraphSelection } from './InspectorPanel'
import type { ResultNodeValue } from './resultPresentation'
import type {
  CatalogComponent,
  TopologyDocument,
  TopologyPresentation,
} from './types'
import type { ComponentDefinitionReadiness } from './definitionReadiness'
import { connectionIntentIssue } from './graphAuthoring'
import { connectionLineStyle, topologyDomainColor } from './topologyVisuals'
import { autoLayoutTopology } from './topologyAutoLayout'
import {
  presentationFromNodes,
  type CanvasComponentPlacement,
} from './topologyPresentation'

interface TopologyCanvasProps {
  topology?: TopologyDocument
  catalog: CatalogComponent[]
  revisionId: string
  publishing: boolean
  onConnect: (connection: Connection) => Promise<void>
  onSelect: (selection?: GraphSelection) => void
  onAddComponent?: (
    component: CatalogComponent,
    placement?: CanvasComponentPlacement,
  ) => void
  onDeleteSelection?: (selection: GraphSelection) => void
  readOnly?: boolean
  resultValues?: Record<string, ResultNodeValue[]>
  selection?: GraphSelection
  onCreateTopology?: () => void
  componentReadiness?: Record<string, ComponentDefinitionReadiness>
  presentation?: TopologyPresentation
  onPresentationChange?: (presentation: TopologyPresentation) => void
}

const defaultViewport: Viewport = { x: 0, y: 0, zoom: 1 }

export function centeredComponentPosition(position: XYPosition): XYPosition {
  return { x: position.x - 125, y: position.y - 59 }
}

export function finalConnectionIntent(
  state: FinalConnectionState,
): Connection | undefined {
  if (!state.fromHandle || !state.fromNode || !state.toHandle || !state.toNode) {
    return undefined
  }
  if (state.fromHandle.type === 'source') {
    return {
      source: state.fromNode.id,
      sourceHandle: state.fromHandle.id ?? null,
      target: state.toNode.id,
      targetHandle: state.toHandle.id ?? null,
    }
  }
  return {
    source: state.toNode.id,
    sourceHandle: state.toHandle.id ?? null,
    target: state.fromNode.id,
    targetHandle: state.fromHandle.id ?? null,
  }
}

function isTextEditingTarget(target: EventTarget | null): boolean {
  return (
    target instanceof HTMLInputElement ||
    target instanceof HTMLTextAreaElement ||
    target instanceof HTMLSelectElement ||
    (target instanceof HTMLElement && target.isContentEditable)
  )
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
      catalogComponent: catalog.get(component.kind),
      resultValues: resultValues[component.id] ?? [],
      definition: componentReadiness[component.id],
    },
  }))
}

export function layoutEdges(
  topology: TopologyDocument,
  catalog: Map<string, CatalogComponent>,
  selection?: GraphSelection,
): Edge[] {
  return topology.model.connections.map((connection) => {
    const [source, sourceHandle] = endpoint(connection.from)
    const [target, targetHandle] = endpoint(connection.to)
    const sourceComponent = topology.model.components.find(
      (component) => component.id === source,
    )
    const sourceAssembly = (topology.model.assemblies ?? []).find(
      (assembly) => assembly.id === source,
    )
    const sourcePort = sourceComponent
      ? catalog
          .get(sourceComponent.kind)
          ?.ports.find((port) => port.name === sourceHandle)
      : sourceAssembly
        ? assemblyPorts(sourceAssembly, catalog).find(
            (port) => port.name === sourceHandle,
          )
        : undefined
    const domain = sourcePort?.domain ?? connection.kind
    const selectedEdge =
      selection?.type === 'connection' && selection.id === connection.id
    return {
      id: connection.id,
      selected: selectedEdge,
      source,
      sourceHandle,
      target,
      targetHandle,
      type: 'smoothstep',
      label: domain,
      labelStyle: {
        fill: topologyDomainColor(domain),
        fontSize: 9,
        fontWeight: 700,
      },
      style: {
        ...connectionLineStyle(domain),
        strokeWidth: selectedEdge ? 3 : 2,
      },
    }
  })
}

const nodeTypes = { topology: TopologyNode }

const domainOrder = [
  'material',
  'fluid',
  'heat',
  'shaft',
  'electrical',
  'force',
  'signal',
  'control',
]

export function activeTopologyDomains(
  topology: TopologyDocument,
  catalog: Map<string, CatalogComponent>,
): string[] {
  const domains = new Set<string>()
  for (const component of topology.model.components) {
    for (const port of catalog.get(component.kind)?.ports ?? []) {
      domains.add(port.domain)
    }
  }
  for (const assembly of topology.model.assemblies ?? []) {
    for (const port of assemblyPorts(assembly, catalog)) {
      domains.add(port.domain)
    }
  }
  return [...domains].sort((left, right) => {
    const leftOrder = domainOrder.indexOf(left)
    const rightOrder = domainOrder.indexOf(right)
    return (
      (leftOrder === -1 ? domainOrder.length : leftOrder) -
        (rightOrder === -1 ? domainOrder.length : rightOrder) ||
      left.localeCompare(right)
    )
  })
}

export function TopologyCanvas({
  topology,
  catalog,
  revisionId,
  publishing,
  onConnect,
  onSelect,
  onAddComponent,
  onDeleteSelection,
  readOnly = false,
  resultValues = {},
  selection,
  onCreateTopology,
  componentReadiness = {},
  presentation,
  onPresentationChange,
}: TopologyCanvasProps) {
  const flow = useRef<ReactFlowInstance<Node<TopologyNodeData>, Edge> | null>(
    null,
  )
  const [connectionFeedback, setConnectionFeedback] = useState('')

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
  const presentedPositions = new Map<string, XYPosition>(
    (presentation?.nodes ?? []).map((node) => [
      node.entity_id,
      { x: node.x, y: node.y },
    ]),
  )
  const nodes = layoutNodes(
    topology,
    catalogByKind,
    resultValues,
    selection,
    componentReadiness,
    presentedPositions,
  )
  const edges = layoutEdges(topology, catalogByKind, selection)
  const activeDomains = activeTopologyDomains(topology, catalogByKind)
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
  const publishPresentation = (
    positions: ReadonlyMap<string, XYPosition>,
    viewport: Viewport,
  ) => {
    onPresentationChange?.(
      presentationFromNodes(
        nodes.map((node) => {
          const position = positions.get(node.id) ?? node.position
          return { ...node, position }
        }),
        viewport,
      ),
    )
  }

  const handleKeyboard = (event: KeyboardEvent<HTMLDivElement>) => {
    if (isTextEditingTarget(event.target)) return
    if (event.key === 'Escape') {
      onSelect(undefined)
      setConnectionFeedback('')
      return
    }
    if (
      (event.key === 'Delete' || event.key === 'Backspace') &&
      selection &&
      onDeleteSelection &&
      !readOnly &&
      !publishing &&
      !event.repeat
    ) {
      event.preventDefault()
      onDeleteSelection(selection)
    }
  }

  const arrangeFlow = () => {
    const instance = flow.current
    if (!instance) return
    const arranged = autoLayoutTopology(topology, catalogByKind)
    const positions = new Map(
      arranged.map((node) => [node.entity_id, { x: node.x, y: node.y }]),
    )
    instance.setNodes((current) =>
      current.map((node) => ({
        ...node,
        position: positions.get(node.id) ?? node.position,
      })),
    )
    publishPresentation(positions, instance.getViewport())
    window.requestAnimationFrame(() => void instance.fitView({ padding: 0.22 }))
  }

  return (
    <div
      className="topology-flow-shell"
      tabIndex={0}
      aria-label="Thermal system topology canvas"
      onKeyDown={handleKeyboard}
    >
      <ReactFlow
        key={`${revisionId}:${presentation ? 'persisted' : 'automatic'}`}
        {...elementProps}
        nodeTypes={nodeTypes}
        fitView={!presentation}
        fitViewOptions={{ padding: 0.22 }}
        defaultViewport={presentation?.viewport}
        nodesDraggable={!readOnly}
        nodesConnectable={!readOnly && !publishing}
        deleteKeyCode={null}
        isValidConnection={validConnection}
        onInit={(instance) => {
          flow.current = instance
        }}
        onConnectStart={() => setConnectionFeedback('')}
        onConnect={(connection) => {
          setConnectionFeedback('')
          if (!readOnly) void onConnect(connection)
        }}
        onConnectEnd={(_, state) => {
          if (readOnly || state.isValid) return
          const intent = finalConnectionIntent(state)
          setConnectionFeedback(
            intent
              ? connectionIntentIssue(intent, topology, catalog) ??
                  'This connection is not permitted.'
              : 'Drop the connection on a compatible registered port.',
          )
        }}
        onNodeDragStop={(_, node) => {
          if (!readOnly) {
            const positions = new Map(presentedPositions)
            positions.set(node.id, node.position)
            publishPresentation(
              positions,
              flow.current?.getViewport() ??
                presentation?.viewport ??
                defaultViewport,
            )
          }
        }}
        onMoveEnd={(_, viewport) => {
          if (!readOnly) publishPresentation(presentedPositions, viewport)
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
        onPaneClick={() => {
          onSelect(undefined)
          setConnectionFeedback('')
        }}
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
          const instance = flow.current
          const position = centeredComponentPosition(
            instance?.screenToFlowPosition({
              x: event.clientX,
              y: event.clientY,
            }) ?? { x: event.clientX, y: event.clientY },
          )
          onAddComponent(component, {
            position,
            presentation: presentationFromNodes(
              instance?.getNodes() ?? nodes,
              instance?.getViewport() ??
                presentation?.viewport ??
                defaultViewport,
            ),
          })
        }}
        elementsSelectable
        minZoom={0.2}
        maxZoom={2}
        proOptions={{ hideAttribution: true }}
      >
        {onAddComponent && !readOnly && (
          <Panel position="top-left" className="canvas-authoring-hint">
            <span>Drag a component here · Delete removes the selection</span>
            <button type="button" disabled={publishing} onClick={arrangeFlow}>
              Arrange flow
            </button>
          </Panel>
        )}
        {connectionFeedback && (
          <Panel position="top-center" className="canvas-connection-feedback">
            <strong>Connection not created</strong>
            <span>{connectionFeedback}</span>
          </Panel>
        )}
        {activeDomains.length > 0 && (
          <Panel position="top-right" className="canvas-domain-legend">
            {activeDomains.map((domain) => (
              <span key={domain}>
                <i
                  className={
                    domain === 'signal' || domain === 'control'
                      ? 'is-information'
                      : ''
                  }
                  style={{ borderColor: topologyDomainColor(domain) }}
                />
                {domain}
              </span>
            ))}
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
    </div>
  )
}
