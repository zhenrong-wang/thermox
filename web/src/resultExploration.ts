import type {
  GraphResult,
  GraphResultValue,
  CatalogUnitDimension,
  SimulationResult,
  TransientSimulationResult,
} from './types'
import { isTransientResult } from './resultPresentation'

export type ResultValueScope =
  | 'system_balance'
  | 'kpi'
  | 'component_metric'
  | 'component_internal'
  | 'port_primary'
  | 'port_derived'

export type ResultScopeFilter = 'all' | ResultValueScope

export interface ResultRow {
  key: string
  scope: ResultValueScope
  componentId: string
  componentKind: string
  portName: string
  domain: string
  mediumId: string
  phase: string
  name: string
  dimension: string
  valueSi: number
  derivativeSiS?: number
}

export interface TransientSeriesOption {
  key: string
  label: string
  dimension: string
}

export interface TransientSeriesPoint {
  time: number
  valueSi: number
}

function row(
  scope: ResultValueScope,
  value: GraphResultValue,
  identity: Omit<
    ResultRow,
    'key' | 'scope' | 'name' | 'dimension' | 'valueSi' | 'derivativeSiS'
  >,
): ResultRow {
  const key = [
    scope,
    identity.componentId,
    identity.portName,
    value.name,
    value.dimension,
  ].join('\u001f')
  return {
    key,
    scope,
    ...identity,
    name: value.name,
    dimension: value.dimension,
    valueSi: value.value_si,
    derivativeSiS: value.derivative_si_s,
  }
}

const systemIdentity = {
  componentId: '',
  componentKind: '',
  portName: '',
  domain: '',
  mediumId: '',
  phase: '',
}

export function flattenGraphResult(graph: GraphResult): ResultRow[] {
  const rows: ResultRow[] = [
    ...graph.system_balances.map((value) =>
      row('system_balance', value, systemIdentity),
    ),
    ...graph.kpis.map((value) => row('kpi', value, systemIdentity)),
  ]
  for (const component of graph.components) {
    const componentIdentity = {
      componentId: component.component_id,
      componentKind: component.kind,
      portName: '',
      domain: '',
      mediumId: '',
      phase: '',
    }
    rows.push(
      ...component.metrics.map((value) =>
        row('component_metric', value, componentIdentity),
      ),
      ...component.internal_values.map((value) =>
        row('component_internal', value, componentIdentity),
      ),
    )
    for (const port of component.ports) {
      const portIdentity = {
        componentId: component.component_id,
        componentKind: component.kind,
        portName: port.port_name,
        domain: port.domain,
        mediumId: port.medium_id,
        phase: port.phase,
      }
      rows.push(
        ...port.primary_values.map((value) =>
          row('port_primary', value, portIdentity),
        ),
        ...port.derived_values.map((value) =>
          row('port_derived', value, portIdentity),
        ),
      )
    }
  }
  return rows
}

export function filterResultRows(
  rows: readonly ResultRow[],
  query: string,
  scope: ResultScopeFilter,
): ResultRow[] {
  const normalized = query.trim().toLowerCase()
  return rows.filter((value) => {
    if (scope !== 'all' && value.scope !== scope) return false
    if (!normalized) return true
    return [
      value.scope,
      value.componentId,
      value.componentKind,
      value.portName,
      value.domain,
      value.mediumId,
      value.phase,
      value.name,
      value.dimension,
    ].some((field) => field.toLowerCase().includes(normalized))
  })
}

function csvCell(value: string | number | undefined): string {
  if (value === undefined) return ''
  const text = String(value)
  return /[",\r\n]/.test(text) ? `"${text.replaceAll('"', '""')}"` : text
}

const csvColumns = [
  'sample_index',
  'time_s',
  'scope',
  'component_id',
  'component_kind',
  'port_name',
  'domain',
  'medium_id',
  'phase',
  'name',
  'dimension',
  'canonical_unit',
  'value_si',
  'derivative_si_s',
]

function csvRow(
  value: ResultRow,
  sampleIndex: number,
  time: number | undefined,
  unitDimensions: readonly CatalogUnitDimension[],
): string {
  const canonicalUnit =
    unitDimensions.find(
      (dimension) => dimension.dimension === value.dimension,
    )?.canonical_unit ?? ''
  return [
    sampleIndex,
    time,
    value.scope,
    value.componentId,
    value.componentKind,
    value.portName,
    value.domain,
    value.mediumId,
    value.phase,
    value.name,
    value.dimension,
    canonicalUnit,
    value.valueSi,
    value.derivativeSiS,
  ]
    .map(csvCell)
    .join(',')
}

export function resultRowsCsv(
  rows: readonly ResultRow[],
  sampleIndex = 0,
  time?: number,
  unitDimensions: readonly CatalogUnitDimension[] = [],
): string {
  return [
    csvColumns.join(','),
    ...rows.map((value) =>
      csvRow(value, sampleIndex, time, unitDimensions),
    ),
  ].join('\n')
}

export function simulationResultCsv(
  result: SimulationResult,
  unitDimensions: readonly CatalogUnitDimension[] = [],
): string {
  if (!isTransientResult(result)) {
    return resultRowsCsv(
      flattenGraphResult(result.graph),
      0,
      undefined,
      unitDimensions,
    )
  }
  return [
    csvColumns.join(','),
    ...result.trajectory.flatMap((sample, sampleIndex) =>
      flattenGraphResult(sample.graph).map((value) =>
        csvRow(value, sampleIndex, sample.time, unitDimensions),
      ),
    ),
  ].join('\n')
}

function resultRowLabel(value: ResultRow): string {
  const path = [
    value.componentId,
    value.portName,
    value.name,
  ].filter(Boolean)
  if (!path.length) path.push(value.scope, value.name)
  return `${path.join('.')} [${value.dimension}]`
}

export function transientSeriesOptions(
  result: TransientSimulationResult,
): TransientSeriesOption[] {
  const options = new Map<string, TransientSeriesOption>()
  for (const sample of result.trajectory) {
    for (const value of flattenGraphResult(sample.graph)) {
      if (!options.has(value.key)) {
        options.set(value.key, {
          key: value.key,
          label: resultRowLabel(value),
          dimension: value.dimension,
        })
      }
    }
  }
  return [...options.values()].sort((left, right) =>
    left.label.localeCompare(right.label),
  )
}

export function transientSeries(
  result: TransientSimulationResult,
  key: string,
): TransientSeriesPoint[] {
  const points: TransientSeriesPoint[] = []
  for (const sample of result.trajectory) {
    const value = flattenGraphResult(sample.graph).find(
      (candidate) => candidate.key === key,
    )
    if (value && Number.isFinite(sample.time) && Number.isFinite(value.valueSi)) {
      points.push({ time: sample.time, valueSi: value.valueSi })
    }
  }
  return points
}
