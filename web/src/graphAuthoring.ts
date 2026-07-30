import type {
  Catalog,
  GraphUpsertOperation,
  TopologyDocument,
} from './types'

export interface ConnectionIntent {
  source: string | null
  sourceHandle: string | null
  target: string | null
  targetHandle: string | null
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
): GraphUpsertOperation {
  if (
    !connection.source ||
    !connection.sourceHandle ||
    !connection.target ||
    !connection.targetHandle
  ) {
    throw new Error(
      'A connection requires two concrete component ports.',
    )
  }
  if (connection.source === connection.target) {
    throw new Error('A component cannot connect to itself.')
  }

  const components = new Map(
    topology.model.components.map((item) => [item.id, item]),
  )
  const portDomain = (componentId: string, portName: string) => {
    const component = components.get(componentId)
    return catalog.components
      .find((item) => item.kind === component?.kind)
      ?.ports.find((port) => port.name === portName)?.domain
  }
  const sourceDomain = portDomain(
    connection.source,
    connection.sourceHandle,
  )
  const targetDomain = portDomain(
    connection.target,
    connection.targetHandle,
  )
  if (!sourceDomain || sourceDomain !== targetDomain) {
    throw new Error(
      'Connection ports must use the same registered domain.',
    )
  }
  const connector = catalog.connector_domains.find(
    (item) => item.domain === sourceDomain,
  )
  if (!connector) {
    throw new Error(
      'The runtime catalog does not define this connection domain.',
    )
  }

  const complete = connection as Required<ConnectionIntent>
  const id = nextConnectionId(complete, topology)
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
