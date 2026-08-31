import type { TopologyDocument } from './types'

export interface TopologyJsonSummary {
  modelId: string
  modelName: string
  mediumCount: number
  materialCount: number
  componentCount: number
  assemblyCount: number
  connectionCount: number
}

export interface TopologyJsonReview {
  document?: TopologyDocument
  issues: string[]
  summary?: TopologyJsonSummary
}

function record(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value)
}

function nonEmptyString(value: unknown): value is string {
  return typeof value === 'string' && value.trim().length > 0
}

function requiredArray(
  value: Record<string, unknown>,
  field: string,
  issues: string[],
): unknown[] {
  if (!Array.isArray(value[field])) {
    issues.push(`model.${field} must be an array.`)
    return []
  }
  return value[field]
}

function optionalArray(
  value: Record<string, unknown>,
  field: string,
  issues: string[],
): unknown[] {
  if (value[field] === undefined) return []
  if (!Array.isArray(value[field])) {
    issues.push(`model.${field} must be an array when declared.`)
    return []
  }
  return value[field]
}

function validateEntityIds(
  entities: unknown[],
  field: string,
  issues: string[],
): Set<string> {
  const ids = new Set<string>()
  entities.forEach((entity, index) => {
    if (!record(entity) || !nonEmptyString(entity.id)) {
      issues.push(`model.${field}[${index}].id must be a non-empty string.`)
      return
    }
    if (ids.has(entity.id)) {
      issues.push(`model.${field} contains duplicate id "${entity.id}".`)
    }
    ids.add(entity.id)
  })
  return ids
}

function endpointEntity(value: string): string {
  const separator = value.indexOf('.')
  return separator === -1 ? '' : value.slice(0, separator)
}

export function reviewTopologyJson(source: string): TopologyJsonReview {
  let parsed: unknown
  try {
    parsed = JSON.parse(source)
  } catch (reason) {
    return {
      issues: [reason instanceof Error ? reason.message : 'Invalid JSON.'],
    }
  }
  const issues: string[] = []
  if (!record(parsed)) return { issues: ['Topology document must be an object.'] }
  if (parsed.schema_version !== 'thermox.topology/v1') {
    issues.push('schema_version must be thermox.topology/v1.')
  }
  if (!record(parsed.model)) {
    issues.push('model must be an object.')
    return { issues }
  }
  const model = parsed.model
  for (const field of ['id', 'name', 'revision']) {
    if (!nonEmptyString(model[field])) {
      issues.push(`model.${field} must be a non-empty string.`)
    }
  }
  const media = requiredArray(model, 'media', issues)
  const materials = optionalArray(model, 'materials', issues)
  const components = requiredArray(model, 'components', issues)
  const assemblies = optionalArray(model, 'assemblies', issues)
  const connections = requiredArray(model, 'connections', issues)

  validateEntityIds(media, 'media', issues)
  validateEntityIds(materials, 'materials', issues)
  const componentIds = validateEntityIds(components, 'components', issues)
  const assemblyIds = validateEntityIds(assemblies, 'assemblies', issues)
  validateEntityIds(connections, 'connections', issues)
  const topologyEntityIds = new Set([...componentIds, ...assemblyIds])
  for (const id of componentIds) {
    if (assemblyIds.has(id)) {
      issues.push(`Top-level component/assembly id "${id}" is ambiguous.`)
    }
  }
  components.forEach((component, index) => {
    if (record(component) && !nonEmptyString(component.kind)) {
      issues.push(`model.components[${index}].kind must be a non-empty string.`)
    }
  })
  connections.forEach((connection, index) => {
    if (!record(connection)) return
    for (const field of ['from', 'to', 'kind']) {
      if (!nonEmptyString(connection[field])) {
        issues.push(`model.connections[${index}].${field} must be a non-empty string.`)
      }
    }
    for (const field of ['from', 'to'] as const) {
      const value = connection[field]
      if (nonEmptyString(value)) {
        const entity = endpointEntity(value)
        if (!entity || !topologyEntityIds.has(entity)) {
          issues.push(`model.connections[${index}].${field} references unknown top-level entity "${entity || value}".`)
        }
      }
    }
  })
  const summary: TopologyJsonSummary = {
    modelId: nonEmptyString(model.id) ? model.id : '',
    modelName: nonEmptyString(model.name) ? model.name : '',
    mediumCount: media.length,
    materialCount: materials.length,
    componentCount: components.length,
    assemblyCount: assemblies.length,
    connectionCount: connections.length,
  }
  return {
    ...(issues.length ? {} : { document: parsed as unknown as TopologyDocument }),
    issues,
    summary,
  }
}

export function topologyJsonText(document: TopologyDocument): string {
  return `${JSON.stringify(document, null, 2)}\n`
}
