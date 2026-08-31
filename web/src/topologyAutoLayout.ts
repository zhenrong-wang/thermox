import { assemblyPorts } from './assemblyAuthoring'
import type {
  CatalogComponent,
  TopologyDocument,
  TopologyPresentation,
} from './types'

interface Endpoint {
  entityId: string
  portName: string
}

function endpoint(value: string): Endpoint {
  const separator = value.lastIndexOf('.')
  return separator === -1
    ? { entityId: value, portName: '' }
    : {
        entityId: value.slice(0, separator),
        portName: value.slice(separator + 1),
      }
}

function portDomain(
  topology: TopologyDocument,
  catalog: Map<string, CatalogComponent>,
  entityId: string,
  portName: string,
): string {
  const component = topology.model.components.find(
    (candidate) => candidate.id === entityId,
  )
  if (component) {
    return catalog
      .get(component.kind)
      ?.ports.find((port) => port.name === portName)?.domain ?? ''
  }
  const assembly = (topology.model.assemblies ?? []).find(
    (candidate) => candidate.id === entityId,
  )
  return assembly
    ? assemblyPorts(assembly, catalog).find((port) => port.name === portName)
        ?.domain ?? ''
    : ''
}

function entityIds(topology: TopologyDocument): string[] {
  return [
    ...topology.model.components.map((component) => component.id),
    ...(topology.model.assemblies ?? []).map((assembly) => assembly.id),
  ]
}

export function autoLayoutTopology(
  topology: TopologyDocument,
  catalog: Map<string, CatalogComponent>,
): TopologyPresentation['nodes'] {
  const ids = entityIds(topology)
  const idSet = new Set(ids)
  const primaryEdges = topology.model.connections.flatMap((connection) => {
    const from = endpoint(connection.from)
    const to = endpoint(connection.to)
    const domain = portDomain(
      topology,
      catalog,
      from.entityId,
      from.portName,
    )
    return idSet.has(from.entityId) &&
      idSet.has(to.entityId) &&
      (domain === 'material' || domain === 'fluid')
      ? [{ from: from.entityId, to: to.entityId }]
      : []
  })
  const layer = new Map(ids.map((id) => [id, 0]))
  const incoming = new Map(ids.map((id) => [id, 0]))
  for (const edge of primaryEdges) {
    incoming.set(edge.to, (incoming.get(edge.to) ?? 0) + 1)
  }
  const queue = ids.filter((id) => incoming.get(id) === 0)
  const visited = new Set<string>()
  while (queue.length) {
    const id = queue.shift()!
    if (visited.has(id)) continue
    visited.add(id)
    for (const edge of primaryEdges.filter((candidate) => candidate.from === id)) {
      layer.set(edge.to, Math.max(layer.get(edge.to) ?? 0, (layer.get(id) ?? 0) + 1))
      incoming.set(edge.to, (incoming.get(edge.to) ?? 1) - 1)
      if (incoming.get(edge.to) === 0) queue.push(edge.to)
    }
  }

  // Place equipment outside the main thermodynamic path next to the closest
  // already-layered physical component. This covers generators, shaft loads,
  // sensors, controls, and terminal boundaries without letting shaft loops
  // distort the material-flow direction.
  for (let pass = 0; pass < ids.length; pass += 1) {
    let changed = false
    for (const connection of topology.model.connections) {
      const from = endpoint(connection.from).entityId
      const to = endpoint(connection.to).entityId
      if (!idSet.has(from) || !idSet.has(to)) continue
      const fromOnPrimary = primaryEdges.some(
        (edge) => edge.from === from || edge.to === from,
      )
      const toOnPrimary = primaryEdges.some(
        (edge) => edge.from === to || edge.to === to,
      )
      if ((fromOnPrimary || (layer.get(from) ?? 0) > 0) && !toOnPrimary) {
        const next = (layer.get(from) ?? 0) + 1
        if (next > (layer.get(to) ?? 0)) {
          layer.set(to, next)
          changed = true
        }
      }
    }
    if (!changed) break
  }

  const byLayer = new Map<number, string[]>()
  for (const id of ids) {
    const value = layer.get(id) ?? 0
    byLayer.set(value, [...(byLayer.get(value) ?? []), id])
  }
  return [...byLayer.entries()]
    .sort(([left], [right]) => left - right)
    .flatMap(([column, columnIds]) =>
      columnIds.map((id, row) => ({
        entity_id: id,
        x: column * 340,
        y: row * 220,
      })),
    )
}
