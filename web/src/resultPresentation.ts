import type {
  GraphResult,
  SimulationJob,
  SimulationResult,
  TransientSimulationResult,
} from './types'

export interface ResultNodeValue {
  label: string
  dimension: string
  valueSi: number
}

export function isTransientResult(
  result: SimulationResult,
): result is TransientSimulationResult {
  return 'trajectory' in result
}

export function resultSampleCount(result: SimulationResult): number {
  return isTransientResult(result) ? result.trajectory.length : 1
}

export function resultGraph(
  result: SimulationResult,
  sampleIndex: number,
): GraphResult | undefined {
  if (!isTransientResult(result)) return result.graph
  if (!result.trajectory.length) return undefined
  const bounded = Math.min(
    Math.max(0, sampleIndex),
    result.trajectory.length - 1,
  )
  return result.trajectory[bounded]?.graph
}

export function resultSampleTime(
  result: SimulationResult,
  sampleIndex: number,
): number | null {
  if (!isTransientResult(result) || !result.trajectory.length) return null
  const bounded = Math.min(
    Math.max(0, sampleIndex),
    result.trajectory.length - 1,
  )
  return result.trajectory[bounded]?.time ?? null
}

export function projectedGraphNodeValues(
  job: SimulationJob,
  graph: GraphResult,
): Record<string, ResultNodeValue[]> {
  const values: Record<string, ResultNodeValue[]> = {}
  for (const projection of job.request.result_projections) {
    if (!projection.component_id) continue
    const component = graph.components.find(
      (item) => item.component_id === projection.component_id,
    )
    if (!component) continue
    const port = component.ports.find(
      (item) => item.port_name === projection.port_name,
    )
    const candidates =
      projection.scope === 'component_metric'
        ? component.metrics
        : projection.scope === 'component_internal'
          ? component.internal_values
          : projection.scope === 'port_primary'
            ? port?.primary_values
            : projection.scope === 'port_derived'
              ? port?.derived_values
              : undefined
    const resultValue = candidates?.find(
      (value) => value.name === projection.value_name,
    )
    if (!resultValue) continue
    const label =
      projection.scope === 'port_primary' ||
      projection.scope === 'port_derived'
        ? `${projection.port_name}.${projection.value_name}`
        : projection.value_name
    values[projection.component_id] ??= []
    values[projection.component_id].push({
      label,
      dimension: resultValue.dimension,
      valueSi: resultValue.value_si,
    })
  }
  return values
}

export function formatResultValue(value: number): string {
  if (!Number.isFinite(value)) return String(value)
  const magnitude = Math.abs(value)
  if (magnitude !== 0 && (magnitude >= 1.0e6 || magnitude < 1.0e-3)) {
    return value.toExponential(5)
  }
  return value.toLocaleString('en-US', {
    maximumFractionDigits: 6,
    useGrouping: false,
  })
}
