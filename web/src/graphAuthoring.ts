import type {
  Catalog,
  CatalogComponent,
  CatalogPort,
  GraphUpsertOperation,
  TopologyDocument,
} from './types'
import { assemblyPorts } from './assemblyAuthoring'

export interface ConnectionIntent {
  source: string | null
  sourceHandle: string | null
  target: string | null
  targetHandle: string | null
}

function resolveTopologyPortFromCatalog(
  topology: TopologyDocument,
  catalog: Map<string, CatalogComponent>,
  entityId: string,
  portName: string,
): CatalogPort | undefined {
  const component = topology.model.components.find(
    (item) => item.id === entityId,
  )
  if (component) {
    return catalog
      .get(component.kind)
      ?.ports.find((port) => port.name === portName)
  }
  const assembly = (topology.model.assemblies ?? []).find(
    (item) => item.id === entityId,
  )
  return assembly
    ? assemblyPorts(assembly, catalog).find((port) => port.name === portName)
    : undefined
}

export function resolveTopologyPort(
  topology: TopologyDocument,
  components: CatalogComponent[],
  entityId: string,
  portName: string,
): CatalogPort | undefined {
  return resolveTopologyPortFromCatalog(
    topology,
    new Map(components.map((item) => [item.kind, item])),
    entityId,
    portName,
  )
}

function endpointConnectionCount(
  topology: TopologyDocument,
  endpoint: string,
  existingConnectionId?: string,
): number {
  return topology.model.connections.filter(
    (connection) =>
      connection.id !== existingConnectionId &&
      (connection.from === endpoint || connection.to === endpoint),
  ).length
}

export function connectionIntentIssue(
  connection: ConnectionIntent,
  topology: TopologyDocument,
  components: CatalogComponent[],
  existingConnectionId?: string,
): string | undefined {
  if (
    !connection.source ||
    !connection.sourceHandle ||
    !connection.target ||
    !connection.targetHandle
  ) {
    return 'A connection requires two concrete component ports.'
  }
  if (connection.source === connection.target) {
    return 'A topology entity cannot connect to itself.'
  }

  const catalog = new Map(components.map((item) => [item.kind, item]))
  const source = resolveTopologyPortFromCatalog(
    topology,
    catalog,
    connection.source,
    connection.sourceHandle,
  )
  const target = resolveTopologyPortFromCatalog(
    topology,
    catalog,
    connection.target,
    connection.targetHandle,
  )
  if (!source || !target) {
    return 'Both endpoints must be registered public ports.'
  }
  if (source.direction !== 'out' && source.direction !== 'bidirectional') {
    return `Port ${connection.source}.${connection.sourceHandle} cannot be used as a source.`
  }
  if (target.direction !== 'in' && target.direction !== 'bidirectional') {
    return `Port ${connection.target}.${connection.targetHandle} cannot be used as a target.`
  }
  if (source.domain !== target.domain) {
    return 'Connection ports must use the same registered domain.'
  }

  const sourceEndpoint = `${connection.source}.${connection.sourceHandle}`
  const targetEndpoint = `${connection.target}.${connection.targetHandle}`
  if (
    topology.model.connections.some(
      (item) =>
        item.id !== existingConnectionId &&
        item.from === sourceEndpoint &&
        item.to === targetEndpoint,
    )
  ) {
    return 'This exact connection already exists.'
  }
  if (
    endpointConnectionCount(topology, sourceEndpoint, existingConnectionId) >=
    source.maximum_connections
  ) {
    return `Port ${sourceEndpoint} has reached its connection limit.`
  }
  if (
    endpointConnectionCount(topology, targetEndpoint, existingConnectionId) >=
    target.maximum_connections
  ) {
    return `Port ${targetEndpoint} has reached its connection limit.`
  }
  return undefined
}

function nextConnectionId(
  connection: Required<ConnectionIntent>,
  topology: TopologyDocument,
): string {
  const base = [
    'link',
    connection.source,
    connection.sourceHandle,
    connection.target,
    connection.targetHandle,
  ]
    .join('_')
    .replace(/[^a-zA-Z0-9_-]/g, '_')
  const used = new Set(
    topology.model.connections.map((item) => item.id),
  )
  if (!used.has(base)) return base
  let suffix = 2
  while (used.has(`${base}_${suffix}`)) suffix += 1
  return `${base}_${suffix}`
}

export function buildConnectionOperation(
  connection: ConnectionIntent,
  topology: TopologyDocument,
  catalog: Catalog,
  existingConnectionId?: string,
): GraphUpsertOperation {
  const issue = connectionIntentIssue(
    connection,
    topology,
    catalog.components,
    existingConnectionId,
  )
  if (issue) throw new Error(issue)

  const sourcePort = resolveTopologyPort(
    topology,
    catalog.components,
    connection.source!,
    connection.sourceHandle!,
  )!
  const connector = catalog.connector_domains.find(
    (item) => item.domain === sourcePort.domain,
  )
  if (!connector) {
    throw new Error(
      'The runtime catalog does not define this connection domain.',
    )
  }

  const complete = connection as Required<ConnectionIntent>
  const id =
    existingConnectionId || nextConnectionId(complete, topology)
  return {
    action: 'upsert',
    entity_type: 'connection',
    entity_id: id,
    entity: {
      id,
      from: `${complete.source}.${complete.sourceHandle}`,
      to: `${complete.target}.${complete.targetHandle}`,
      kind: connector.connection_kind,
      contract_version: connector.contract_version,
    },
  }
}
