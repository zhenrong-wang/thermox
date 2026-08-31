import type { CatalogComponent, ComponentDefinition } from './types'

export interface ComponentParameterFact {
  name: string
  dimension: string
  valueSi: number
}

function numericValue(value: unknown): number | undefined {
  if (typeof value === 'number' && Number.isFinite(value)) return value
  if (!value || typeof value !== 'object') return undefined
  const record = value as Record<string, unknown>
  const candidate =
    typeof record.value_si === 'number'
      ? record.value_si
      : typeof record.value === 'number'
        ? record.value
        : undefined
  return candidate !== undefined && Number.isFinite(candidate)
    ? candidate
    : undefined
}

export function componentParameterFacts(
  component: ComponentDefinition,
  descriptor: CatalogComponent | undefined,
  limit = 2,
): ComponentParameterFact[] {
  if (!descriptor || limit <= 0) return []
  return descriptor.parameters
    .flatMap((parameter) => {
      const valueSi = numericValue(component.parameters?.[parameter.name])
      return valueSi === undefined
        ? []
        : [{
            name: parameter.name,
            dimension: parameter.dimension,
            valueSi,
          }]
    })
    .slice(0, limit)
}

export function componentDefinitionCounts(component: ComponentDefinition) {
  return {
    bindings:
      Object.keys(component.media ?? {}).length +
      Object.keys(component.materials ?? {}).length,
    parameters: Object.keys(component.parameters ?? {}).length,
    artifacts: Object.keys(component.artifacts ?? {}).length,
  }
}
