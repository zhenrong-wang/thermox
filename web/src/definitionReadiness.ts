import type {
  Catalog,
  ComponentDefinition,
  TopologyDocument,
} from './types'

export type DefinitionIssueKind =
  | 'catalog'
  | 'binding'
  | 'parameter'
  | 'artifact'

export interface DefinitionIssue {
  id: string
  kind: DefinitionIssueKind
  componentId: string
  message: string
}

function hasParameter(
  component: ComponentDefinition,
  name: string,
): boolean {
  const value = component.parameters?.[name]
  if (typeof value === 'number') return Number.isFinite(value)
  if (!value || typeof value !== 'object') return false
  const scalar = value as Record<string, unknown>
  return (
    (typeof scalar.value_si === 'number' &&
      Number.isFinite(scalar.value_si)) ||
    (typeof scalar.value === 'number' && Number.isFinite(scalar.value))
  )
}

export function definitionIssues(
  topology: TopologyDocument | undefined,
  catalog: Catalog | undefined,
): DefinitionIssue[] {
  if (!topology || !catalog) return []
  const mediumIds = new Set(topology.model.media.map((medium) => medium.id))
  const materialIds = new Set(
    (topology.model.materials ?? []).map((material) => material.id),
  )
  const issues: DefinitionIssue[] = []

  for (const component of topology.model.components) {
    const descriptor = catalog.components.find(
      (candidate) => candidate.kind === component.kind,
    )
    if (!descriptor) {
      issues.push({
        id: `${component.id}:catalog`,
        kind: 'catalog',
        componentId: component.id,
        message: `Component type ${component.kind} is unavailable in the selected catalog.`,
      })
      continue
    }

    for (const port of descriptor.ports) {
      if (port.domain !== 'fluid' && port.domain !== 'material') continue
      const binding =
        port.domain === 'fluid'
          ? component.media?.[port.name]
          : component.materials?.[port.name]
      const known =
        port.domain === 'fluid'
          ? Boolean(binding && mediumIds.has(binding))
          : Boolean(binding && materialIds.has(binding))
      if (!known) {
        issues.push({
          id: `${component.id}:binding:${port.name}`,
          kind: 'binding',
          componentId: component.id,
          message: `${port.name} needs a registered ${port.domain} binding.`,
        })
      }
    }

    for (const parameter of descriptor.parameters) {
      if (
        parameter.required &&
        parameter.default_value_si === null &&
        !hasParameter(component, parameter.name)
      ) {
        issues.push({
          id: `${component.id}:parameter:${parameter.name}`,
          kind: 'parameter',
          componentId: component.id,
          message: `Required parameter ${parameter.name} is missing.`,
        })
      }
    }

    for (const artifact of descriptor.artifacts) {
      if (artifact.required && !component.artifacts?.[artifact.role]?.trim()) {
        issues.push({
          id: `${component.id}:artifact:${artifact.role}`,
          kind: 'artifact',
          componentId: component.id,
          message: `Required artifact ${artifact.role} is not bound.`,
        })
      }
    }
  }

  return issues
}
