import type {
  AssemblyDefinition,
  Catalog,
  ComponentDefinition,
  TopologyDocument,
} from './types'

interface DeclaredComponent {
  component: ComponentDefinition
  id: string
}

function declaredComponents(
  topology: TopologyDocument,
): DeclaredComponent[] {
  const result = topology.model.components.map((component) => ({
    component,
    id: component.id,
  }))
  const visit = (assembly: AssemblyDefinition, prefix: string) => {
    const assemblyId = prefix ? `${prefix}/${assembly.id}` : assembly.id
    for (const component of assembly.components) {
      result.push({
        component,
        id: `${assemblyId}/${component.id}`,
      })
    }
    for (const nested of assembly.assemblies ?? []) visit(nested, assemblyId)
  }
  for (const assembly of topology.model.assemblies ?? []) visit(assembly, '')
  return result
}

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

export type ComponentDefinitionState = 'draft' | 'incomplete' | 'defined'

export interface ComponentDefinitionReadiness {
  state: ComponentDefinitionState
  issues: DefinitionIssue[]
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

  for (const declared of declaredComponents(topology)) {
    const { component } = declared
    const componentId = declared.id
    const descriptor = catalog.components.find(
      (candidate) => candidate.kind === component.kind,
    )
    if (!descriptor) {
      issues.push({
        id: `${componentId}:catalog`,
        kind: 'catalog',
        componentId,
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
          id: `${componentId}:binding:${port.name}`,
          kind: 'binding',
          componentId,
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
          id: `${componentId}:parameter:${parameter.name}`,
          kind: 'parameter',
          componentId,
          message: `Required parameter ${parameter.name} is missing.`,
        })
      }
    }

    for (const artifact of descriptor.artifacts) {
      if (artifact.required && !component.artifacts?.[artifact.role]?.trim()) {
        issues.push({
          id: `${componentId}:artifact:${artifact.role}`,
          kind: 'artifact',
          componentId,
          message: `Required artifact ${artifact.role} is not bound.`,
        })
      }
    }
  }

  return issues
}

export function componentDefinitionReadiness(
  topology: TopologyDocument | undefined,
  catalog: Catalog | undefined,
): Record<string, ComponentDefinitionReadiness> {
  if (!topology || !catalog) return {}
  const allIssues = definitionIssues(topology, catalog)
  const components = declaredComponents(topology)
  const readiness: Record<string, ComponentDefinitionReadiness> = Object.fromEntries(
    components.map(({ component, id }) => {
      const issues = allIssues.filter(
        (issue) => issue.componentId === id,
      )
      const hasPhysicalDefinition =
        Object.keys(component.media ?? {}).length > 0 ||
        Object.keys(component.materials ?? {}).length > 0 ||
        Object.keys(component.artifacts ?? {}).length > 0 ||
        Object.keys(component.parameters ?? {}).length > 0
      return [
        id,
        {
          state:
            issues.length === 0
              ? 'defined'
              : hasPhysicalDefinition
                ? 'incomplete'
                : 'draft',
          issues,
        },
      ]
    }),
  )
  const addAssembly = (assembly: AssemblyDefinition, prefix: string) => {
    const id = prefix ? `${prefix}/${assembly.id}` : assembly.id
    for (const nested of assembly.assemblies ?? []) addAssembly(nested, id)
    const descendants = Object.entries(readiness).filter(
      ([candidate]) => candidate.startsWith(`${id}/`),
    )
    const issues = descendants.flatMap(([, value]) => value.issues)
    readiness[id] = {
      state:
        issues.length === 0
          ? 'defined'
          : descendants.some(([, value]) => value.state !== 'draft')
            ? 'incomplete'
            : 'draft',
      issues,
    }
  }
  for (const assembly of topology.model.assemblies ?? []) addAssembly(assembly, '')
  return readiness
}
