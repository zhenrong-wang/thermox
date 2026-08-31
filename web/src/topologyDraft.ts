import { reviewTopologyJson } from './topologyJson'

export interface TopologyDraftDefinition {
  schema_version: 'thermox.topology_draft/v1'
  id: string
  label?: string
  document: Record<string, unknown>
}

export interface TopologyDraftSourceReview {
  document?: Record<string, unknown>
  syntaxIssue?: string
  promotionIssues: string[]
  promotable: boolean
}

export function reviewTopologyDraftSource(
  source: string,
): TopologyDraftSourceReview {
  let parsed: unknown
  try {
    parsed = JSON.parse(source)
  } catch (reason) {
    return {
      syntaxIssue: reason instanceof Error ? reason.message : 'Invalid JSON.',
      promotionIssues: [],
      promotable: false,
    }
  }
  if (typeof parsed !== 'object' || parsed === null || Array.isArray(parsed)) {
    return {
      syntaxIssue: 'A topology draft must be a JSON object.',
      promotionIssues: [],
      promotable: false,
    }
  }
  const promotion = reviewTopologyJson(source)
  return {
    document: parsed as Record<string, unknown>,
    promotionIssues: promotion.issues,
    promotable: Boolean(promotion.document),
  }
}

export function topologyDraftSourceText(
  definition: TopologyDraftDefinition,
): string {
  return `${JSON.stringify(definition.document, null, 2)}\n`
}

export function topologyDraftDefinition(
  id: string,
  label: string,
  document: Record<string, unknown>,
): TopologyDraftDefinition {
  return {
    schema_version: 'thermox.topology_draft/v1',
    id,
    ...(label.trim() ? { label: label.trim() } : {}),
    document,
  }
}
