import type {
  AssemblyDefinition,
  CatalogComponent,
  CatalogPort,
  ConnectionDefinition,
  GraphEditOperation,
  TopologyDocument,
} from './types'

function endpoint(value: string): [string, string] {
  const separator = value.lastIndexOf('.')
  return separator === -1
    ? [value, '']
    : [value.slice(0, separator), value.slice(separator + 1)]
}

function resolvePort(
  assembly: AssemblyDefinition,
  endpointValue: string,
  catalog: Map<string, CatalogComponent>,
): CatalogPort | undefined {
  const [childId, portName] = endpoint(endpointValue)
  const component = assembly.components.find((item) => item.id === childId)
  if (component) {
    return catalog
      .get(component.kind)
      ?.ports.find((port) => port.name === portName)
  }
  const nested = (assembly.assemblies ?? []).find(
    (item) => item.id === childId,
  )
  const exported = nested?.ports.find((port) => port.name === portName)
  return nested && exported
    ? resolvePort(nested, exported.endpoint, catalog)
    : undefined
}

export function assemblyPorts(
  assembly: AssemblyDefinition,
  catalog: Map<string, CatalogComponent>,
): CatalogPort[] {
  return assembly.ports.flatMap((exported) => {
    const resolved = resolvePort(assembly, exported.endpoint, catalog)
    return resolved ? [{ ...resolved, name: exported.name }] : []
  })
}

function publicPortName(
  componentId: string,
  portName: string,
  used: Set<string>,
): string {
  const base = `${componentId}_${portName}`.replace(
    /[^a-zA-Z0-9_-]/g,
    '_',
  )
  let candidate = base
  let suffix = 2
  while (used.has(candidate)) {
    candidate = `${base}_${suffix}`
    suffix += 1
  }
  used.add(candidate)
  return candidate
}

export function buildAssemblyGroupingOperations(
  topology: TopologyDocument,
  assemblyId: string,
  label: string,
  componentIds: string[],
): GraphEditOperation[] {
  const id = assemblyId.trim()
  if (!id || id.includes('.') || id.includes('/')) {
    throw new Error("Assembly ID is required and cannot contain '.' or '/'.")
  }
  const selected = new Set(componentIds)
  if (selected.size === 0) {
    throw new Error('Select at least one top-level component.')
  }
  const allEntityIds = new Set([
    ...topology.model.components.map((component) => component.id),
    ...(topology.model.assemblies ?? []).map((assembly) => assembly.id),
  ])
  if (allEntityIds.has(id)) {
    throw new Error(`Topology entity ${id} already exists.`)
  }
  const components = topology.model.components.filter((component) =>
    selected.has(component.id),
  )
  if (components.length !== selected.size) {
    throw new Error('Every selected component must be a top-level component.')
  }

  const internalConnections: ConnectionDefinition[] = []
  const boundaryConnections: ConnectionDefinition[] = []
  const ports: AssemblyDefinition['ports'] = []
  const exports = new Map<string, string>()
  const usedPortNames = new Set<string>()
  const exportEndpoint = (value: string) => {
    const [componentId, portName] = endpoint(value)
    const existing = exports.get(value)
    if (existing) return existing
    const name = publicPortName(componentId, portName, usedPortNames)
    exports.set(value, name)
    ports.push({ name, endpoint: value })
    return name
  }

  for (const connection of topology.model.connections) {
    const [fromComponent] = endpoint(connection.from)
    const [toComponent] = endpoint(connection.to)
    const fromSelected = selected.has(fromComponent)
    const toSelected = selected.has(toComponent)
    if (fromSelected && toSelected) {
      internalConnections.push({ ...connection })
      continue
    }
    if (fromSelected || toSelected) {
      boundaryConnections.push({
        ...connection,
        from: fromSelected
          ? `${id}.${exportEndpoint(connection.from)}`
          : connection.from,
        to: toSelected
          ? `${id}.${exportEndpoint(connection.to)}`
          : connection.to,
      })
    }
  }

  const assembly: AssemblyDefinition = {
    id,
    components,
    connections: internalConnections,
    ports,
    parameters: [],
  }
  if (label.trim()) assembly.label = label.trim()

  return [
    {
      action: 'upsert',
      entity_type: 'assembly',
      entity_id: id,
      entity: { ...assembly },
    },
    ...components.map(
      (component): GraphEditOperation => ({
        action: 'remove',
        entity_type: 'component',
        entity_id: component.id,
        cascade: true,
      }),
    ),
    ...boundaryConnections.map(
      (connection): GraphEditOperation => ({
        action: 'upsert',
        entity_type: 'connection',
        entity_id: connection.id,
        entity: { ...connection },
      }),
    ),
  ]
}
