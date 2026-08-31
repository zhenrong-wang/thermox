import { definitionIssues } from './definitionReadiness'
import { connectionIntentIssue } from './graphAuthoring'
import type {
  Catalog,
  ProjectModelValidation,
  TopologyDocument,
  ValidationDiagnostic,
} from './types'

export type SystemReadinessStatus =
  | 'calculatable'
  | 'blocked'
  | 'not_validated'

export type SystemReadinessLayerId =
  | 'definition'
  | 'topology'
  | 'study'
  | 'compilation'

export interface SystemReadinessLayer {
  id: SystemReadinessLayerId
  label: string
  state: 'ready' | 'blocked' | 'not_evaluated'
  detail: string
  authority: 'authoring_hint' | 'service'
}

export type SystemReadinessTarget =
  | { type: 'component'; id: string }
  | { type: 'assembly'; id: string }
  | { type: 'connection'; id: string }
  | { type: 'workspace'; view: 'topology' | 'definition' | 'studies' }
  | { type: 'compiler'; diagnostic: ValidationDiagnostic }

export interface SystemReadinessIssue {
  id: string
  layer: SystemReadinessLayerId
  severity: 'information' | 'warning' | 'error'
  authority: 'authoring_hint' | 'service'
  message: string
  target: SystemReadinessTarget
  suggestions: string[]
}

export interface SystemReadinessViewModel {
  status: SystemReadinessStatus
  layers: SystemReadinessLayer[]
  issues: SystemReadinessIssue[]
  localIssueCount: number
  serviceIssueCount: number
}

function definitionTarget(componentId: string): SystemReadinessTarget {
  const separator = componentId.indexOf('/')
  return separator === -1
    ? { type: 'component', id: componentId }
    : { type: 'assembly', id: componentId.slice(0, separator) }
}

function connectionEndpoints(value: string): [string, string] {
  const separator = value.lastIndexOf('.')
  return separator === -1
    ? [value, '']
    : [value.slice(0, separator), value.slice(separator + 1)]
}

function countLabel(count: number, singular: string): string {
  return `${count} ${singular}${count === 1 ? '' : 's'}`
}

export function buildSystemReadiness(
  topology: TopologyDocument | undefined,
  catalog: Catalog | undefined,
  hasCase: boolean,
  unresolvedArtifactCount: number,
  exactValidation?: ProjectModelValidation,
): SystemReadinessViewModel {
  const issues: SystemReadinessIssue[] = []
  const entityCount = topology
    ? topology.model.components.length + (topology.model.assemblies?.length ?? 0)
    : 0
  const localDefinitionIssues = definitionIssues(topology, catalog)

  for (const issue of localDefinitionIssues) {
    issues.push({
      id: `definition:${issue.id}`,
      layer: 'definition',
      severity: 'error',
      authority: 'authoring_hint',
      message: issue.message,
      target: definitionTarget(issue.componentId),
      suggestions: [],
    })
  }

  const topologyIssues: SystemReadinessIssue[] = []
  if (topology && catalog) {
    for (const connection of topology.model.connections) {
      const [source, sourceHandle] = connectionEndpoints(connection.from)
      const [target, targetHandle] = connectionEndpoints(connection.to)
      const message = connectionIntentIssue(
        { source, sourceHandle, target, targetHandle },
        topology,
        catalog.components,
        connection.id,
      )
      if (message) {
        topologyIssues.push({
          id: `topology:${connection.id}`,
          layer: 'topology',
          severity: 'error',
          authority: 'authoring_hint',
          message,
          target: { type: 'connection', id: connection.id },
          suggestions: [],
        })
      }
    }
  }
  if (topology && entityCount === 0) {
    topologyIssues.push({
      id: 'topology:no-components',
      layer: 'topology',
      severity: 'error',
      authority: 'authoring_hint',
      message: 'Add at least one registered physical component or assembly.',
      target: { type: 'workspace', view: 'topology' },
      suggestions: [],
    })
  }
  issues.push(...topologyIssues)

  if (!hasCase) {
    issues.push({
      id: 'study:no-case',
      layer: 'study',
      severity: 'error',
      authority: 'authoring_hint',
      message: 'Create an operating case with boundaries and calculation mode.',
      target: { type: 'workspace', view: 'studies' },
      suggestions: [],
    })
  }
  if (unresolvedArtifactCount > 0) {
    issues.push({
      id: 'study:artifact-revisions',
      layer: 'study',
      severity: 'error',
      authority: 'authoring_hint',
      message: `${countLabel(unresolvedArtifactCount, 'required artifact revision')} must be selected.`,
      target: { type: 'workspace', view: 'studies' },
      suggestions: [],
    })
  }

  if (exactValidation) {
    for (const diagnostic of exactValidation.validation.diagnostics) {
      issues.push({
        id: `compiler:${diagnostic.code}:${diagnostic.json_path}:${diagnostic.component_id}:${diagnostic.connection_id}`,
        layer: 'compilation',
        severity: diagnostic.severity,
        authority: 'service',
        message: diagnostic.message,
        target: { type: 'compiler', diagnostic },
        suggestions: diagnostic.suggestions,
      })
    }
  }

  const definitionReady = entityCount > 0 && localDefinitionIssues.length === 0
  const topologyReady = entityCount > 0 && topologyIssues.length === 0
  const studyReady = hasCase && unresolvedArtifactCount === 0
  const serviceCalculatable = Boolean(
    exactValidation?.validation.readiness.calculatable,
  )
  const localIssueCount = issues.filter(
    (issue) => issue.authority === 'authoring_hint',
  ).length
  const serviceIssueCount = issues.filter(
    (issue) => issue.authority === 'service',
  ).length

  return {
    status: serviceCalculatable
      ? 'calculatable'
      : exactValidation
        ? 'blocked'
        : 'not_validated',
    layers: [
      {
        id: 'definition',
        label: 'Physical definition',
        state: definitionReady ? 'ready' : 'blocked',
        detail: definitionReady
          ? 'Local catalog requirements complete'
          : entityCount === 0
            ? 'No equipment instances'
            : countLabel(localDefinitionIssues.length, 'local issue'),
        authority: 'authoring_hint',
      },
      {
        id: 'topology',
        label: 'Graph structure',
        state: topologyReady ? 'ready' : 'blocked',
        detail: topologyReady
          ? 'Known connector contracts are consistent'
          : countLabel(topologyIssues.length, 'structural issue'),
        authority: 'authoring_hint',
      },
      {
        id: 'study',
        label: 'Study inputs',
        state: studyReady ? 'ready' : 'blocked',
        detail: studyReady
          ? 'Case and artifact revisions selected'
          : 'Case or artifact selection incomplete',
        authority: 'authoring_hint',
      },
      {
        id: 'compilation',
        label: 'Service compilation',
        state: exactValidation
          ? serviceCalculatable
            ? 'ready'
            : 'blocked'
          : 'not_evaluated',
        detail: exactValidation
          ? serviceCalculatable
            ? 'Exact revision set compiled'
            : serviceIssueCount > 0
              ? countLabel(serviceIssueCount, 'service diagnostic')
              : 'Service reported blocked readiness'
          : 'Not evaluated for this exact revision set',
        authority: 'service',
      },
    ],
    issues,
    localIssueCount,
    serviceIssueCount,
  }
}
