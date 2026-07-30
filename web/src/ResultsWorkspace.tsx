import { useEffect, useMemo, useState } from 'react'
import { useDisplayUnits } from './DisplayUnitsContext'
import { displayDeltaValue, displayValue } from './displayUnits'
import {
  filterResultRows,
  flattenGraphResult,
  resultRowsCsv,
  simulationResultCsv,
  transientSeries,
  transientSeriesOptions,
  type ResultScopeFilter,
  type TransientSeriesPoint,
} from './resultExploration'
import { TopologyCanvas } from './TopologyCanvas'
import {
  formatResultValue,
  isTransientResult,
  projectedGraphNodeValues,
  resultGraph,
  resultSampleCount,
  resultSampleTime,
} from './resultPresentation'
import type {
  CatalogComponent,
  SimulationJob,
  SimulationResult,
  TopologyDocument,
} from './types'

interface ResultsWorkspaceProps {
  topology?: TopologyDocument
  catalog: CatalogComponent[]
  job?: SimulationJob
  result?: SimulationResult
  loading: boolean
  error: string
  onRetry: () => void
}

const scopeLabels: Record<ResultScopeFilter, string> = {
  all: 'All scopes',
  system_balance: 'System balances',
  kpi: 'KPIs',
  component_metric: 'Component metrics',
  component_internal: 'Internal values',
  port_primary: 'Port primary values',
  port_derived: 'Port derived values',
}

function safeFilePart(value: string): string {
  return value.replaceAll(/[^a-zA-Z0-9._-]/g, '_')
}

function downloadCsv(content: string, filename: string) {
  const url = URL.createObjectURL(
    new Blob([content], { type: 'text/csv;charset=utf-8' }),
  )
  const link = document.createElement('a')
  link.href = url
  link.download = filename
  link.click()
  URL.revokeObjectURL(url)
}

function TransientPlot({
  points,
  dimension,
  selectedTime,
}: {
  points: TransientSeriesPoint[]
  dimension: string
  selectedTime: number | null
}) {
  const { profile, unitDimensions } = useDisplayUnits()
  if (!points.length) {
    return <p className="result-plot-empty">This signal has no finite samples.</p>
  }

  const displayed = points.map((point) => ({
    time: point.time,
    value: displayValue(
      point.valueSi,
      dimension,
      profile,
      unitDimensions,
    ).value,
  }))
  const unit = displayValue(
    points[0].valueSi,
    dimension,
    profile,
    unitDimensions,
  ).unit
  const width = 900
  const height = 250
  const left = 72
  const right = 20
  const top = 18
  const bottom = 38
  const rawTimeMin = Math.min(...displayed.map((point) => point.time))
  const rawTimeMax = Math.max(...displayed.map((point) => point.time))
  const rawValueMin = Math.min(...displayed.map((point) => point.value))
  const rawValueMax = Math.max(...displayed.map((point) => point.value))
  const timePadding = rawTimeMin === rawTimeMax ? 0.5 : 0
  const valuePadding =
    rawValueMin === rawValueMax
      ? Math.max(Math.abs(rawValueMin) * 0.05, 0.5)
      : 0
  const timeMin = rawTimeMin - timePadding
  const timeMax = rawTimeMax + timePadding
  const valueMin = rawValueMin - valuePadding
  const valueMax = rawValueMax + valuePadding
  const x = (time: number) =>
    left +
    ((time - timeMin) / (timeMax - timeMin)) *
      (width - left - right)
  const y = (value: number) =>
    top +
    ((valueMax - value) / (valueMax - valueMin)) *
      (height - top - bottom)
  const polyline = displayed
    .map((point) => `${x(point.time)},${y(point.value)}`)
    .join(' ')
  const selected =
    selectedTime === null
      ? undefined
      : displayed.reduce((closest, point) =>
          Math.abs(point.time - selectedTime) <
          Math.abs(closest.time - selectedTime)
            ? point
            : closest,
        )

  return (
    <div className="result-plot">
      <svg
        role="img"
        aria-label={`Transient ${dimension} signal over time`}
        viewBox={`0 0 ${width} ${height}`}
      >
        <line
          className="result-plot-axis"
          x1={left}
          y1={top}
          x2={left}
          y2={height - bottom}
        />
        <line
          className="result-plot-axis"
          x1={left}
          y1={height - bottom}
          x2={width - right}
          y2={height - bottom}
        />
        <line
          className="result-plot-grid"
          x1={left}
          y1={top}
          x2={width - right}
          y2={top}
        />
        <polyline className="result-plot-line" points={polyline} />
        {selected && (
          <circle
            className="result-plot-marker"
            cx={x(selected.time)}
            cy={y(selected.value)}
            r="5"
          />
        )}
        <text className="result-plot-label" x={left - 9} y={top + 4}>
          {formatResultValue(valueMax)}
        </text>
        <text
          className="result-plot-label"
          x={left - 9}
          y={height - bottom + 4}
        >
          {formatResultValue(valueMin)}
        </text>
        <text
          className="result-plot-label result-plot-time-label"
          x={left}
          y={height - 13}
        >
          {formatResultValue(timeMin)} s
        </text>
        <text
          className="result-plot-label result-plot-time-label"
          x={width - right}
          y={height - 13}
        >
          {formatResultValue(timeMax)} s
        </text>
      </svg>
      <div className="result-plot-range">
        <span>
          Range {formatResultValue(valueMin)}–{formatResultValue(valueMax)}{' '}
          {unit}
        </span>
        <span>{points.length} finite samples</span>
      </div>
    </div>
  )
}

export function ResultsWorkspace({
  topology,
  catalog,
  job,
  result,
  loading,
  error,
  onRetry,
}: ResultsWorkspaceProps) {
  const { profile, unitDimensions } = useDisplayUnits()
  const [sampleIndex, setSampleIndex] = useState(0)
  const [query, setQuery] = useState('')
  const [scope, setScope] = useState<ResultScopeFilter>('all')
  const [seriesKey, setSeriesKey] = useState('')
  const sampleCount = result ? resultSampleCount(result) : 0

  useEffect(() => {
    setSampleIndex(Math.max(0, sampleCount - 1))
    setQuery('')
    setScope('all')
  }, [job?.job_id, sampleCount])

  const graph = result ? resultGraph(result, sampleIndex) : undefined
  const overlays = useMemo(
    () => (job && graph ? projectedGraphNodeValues(job, graph) : {}),
    [graph, job],
  )
  const sampleTime = result ? resultSampleTime(result, sampleIndex) : null
  const rows = useMemo(
    () => (graph ? flattenGraphResult(graph) : []),
    [graph],
  )
  const filteredRows = useMemo(
    () => filterResultRows(rows, query, scope),
    [query, rows, scope],
  )
  const seriesOptions = useMemo(
    () =>
      result && isTransientResult(result)
        ? transientSeriesOptions(result)
        : [],
    [result],
  )

  useEffect(() => {
    setSeriesKey((current) =>
      seriesOptions.some((option) => option.key === current)
        ? current
        : (seriesOptions[0]?.key ?? ''),
    )
  }, [seriesOptions])

  const selectedSeries = seriesOptions.find(
    (option) => option.key === seriesKey,
  )
  const seriesPoints = useMemo(
    () =>
      result && isTransientResult(result) && seriesKey
        ? transientSeries(result, seriesKey)
        : [],
    [result, seriesKey],
  )

  if (!job) {
    return (
      <section className="results-workspace empty-results">
        <span className="section-kicker">Graph-native result contract</span>
        <h1>No successful execution selected</h1>
        <p>
          Choose a run configuration with a succeeded job. Results remain
          bound to its exact topology, case, artifacts, and solver policy.
        </p>
      </section>
    )
  }

  const resultFilename = safeFilePart(job.job_id)

  return (
    <section className="results-workspace">
      <header className="results-header">
        <div>
          <span className="section-kicker">Immutable calculation output</span>
          <h1>Result inspection</h1>
          <p>
            <code>{job.job_id}</code> · {job.request.mode} · attempt{' '}
            {job.attempt}
          </p>
        </div>
        <div className="result-provenance-chip">
          <span>Topology</span>
          <code>
            {job.request.source_revisions?.model_revision_id ?? 'snapshot'}
          </code>
        </div>
      </header>

      {error && (
        <div className="operation-banner is-error">
          {error}
          <button type="button" onClick={onRetry}>
            Retry
          </button>
        </div>
      )}
      {loading && <div className="result-loading">Loading full result…</div>}
      {!loading && result && !graph && (
        <div className="result-loading">
          This result does not contain a graph sample to inspect.
        </div>
      )}

      {!loading && result && graph && (
        <div className="results-content">
          <div className="result-summary-strip">
            {(job.result_summary?.values ?? []).map((value) => {
              const displayed = displayValue(
                value.value_si,
                value.dimension,
                profile,
                unitDimensions,
              )
              return (
                <div key={value.id}>
                  <span>{value.id}</span>
                  <strong>{formatResultValue(displayed.value)}</strong>
                  <small>{displayed.unit}</small>
                </div>
              )
            })}
            {!job.result_summary?.values.length && (
              <p>No projected summary values were configured for this run.</p>
            )}
          </div>

          {isTransientResult(result) && sampleCount > 0 && (
            <>
              <section className="result-timeline">
                <div>
                  <strong>Transient sample</strong>
                  <span>
                    {sampleIndex + 1} / {sampleCount} · t ={' '}
                    {sampleTime === null
                      ? '—'
                      : formatResultValue(sampleTime)}{' '}
                    s
                  </span>
                </div>
                <input
                  aria-label="Transient result sample"
                  type="range"
                  min="0"
                  max={Math.max(0, sampleCount - 1)}
                  value={sampleIndex}
                  onChange={(event) =>
                    setSampleIndex(Number(event.target.value))
                  }
                />
              </section>
              <section className="result-plot-section">
                <header>
                  <div>
                    <span className="section-kicker">Trajectory explorer</span>
                    <h2>Transient signal</h2>
                  </div>
                  <label>
                    <span>Signal</span>
                    <select
                      aria-label="Transient signal"
                      value={seriesKey}
                      onChange={(event) => setSeriesKey(event.target.value)}
                    >
                      {seriesOptions.map((option) => (
                        <option key={option.key} value={option.key}>
                          {option.label}
                        </option>
                      ))}
                    </select>
                  </label>
                </header>
                {selectedSeries && (
                  <TransientPlot
                    points={seriesPoints}
                    dimension={selectedSeries.dimension}
                    selectedTime={sampleTime}
                  />
                )}
              </section>
            </>
          )}

          <section className="result-graph-section">
            <header>
              <div>
                <span className="section-kicker">Projected graph overlay</span>
                <h2>System topology</h2>
              </div>
              <small>Values shown in {profile} display units</small>
            </header>
            <div className="result-canvas">
              <TopologyCanvas
                topology={topology}
                catalog={catalog}
                revisionId={`${job.job_id}-${sampleIndex}`}
                publishing={false}
                readOnly
                resultValues={overlays}
                onConnect={async () => undefined}
                onSelect={() => undefined}
              />
            </div>
          </section>

          <div className="result-diagnostics">
            <span>Solver contract</span>
            <code>{result.metadata.solver.contract_version || '—'}</code>
            <span>Catalog</span>
            <code>{result.metadata.catalog_fingerprint || '—'}</code>
          </div>

          <section className="result-value-section">
            <header className="result-explorer-header">
              <div>
                <h3>Graph values</h3>
                <span>
                  {filteredRows.length} of {rows.length} values in sample
                </span>
              </div>
              <div className="result-explorer-controls">
                <input
                  aria-label="Filter result values"
                  type="search"
                  placeholder="Filter identity, medium, value, or dimension"
                  value={query}
                  onChange={(event) => setQuery(event.target.value)}
                />
                <select
                  aria-label="Result value scope"
                  value={scope}
                  onChange={(event) =>
                    setScope(event.target.value as ResultScopeFilter)
                  }
                >
                  {Object.entries(scopeLabels).map(([value, label]) => (
                    <option key={value} value={value}>
                      {label}
                    </option>
                  ))}
                </select>
                <button
                  type="button"
                  className="secondary-button"
                  disabled={!filteredRows.length}
                  onClick={() =>
                    downloadCsv(
                      resultRowsCsv(
                        filteredRows,
                        isTransientResult(result) ? sampleIndex : 0,
                        sampleTime ?? undefined,
                        unitDimensions,
                      ),
                      `${resultFilename}-sample-${sampleIndex}.csv`,
                    )
                  }
                >
                  Export visible
                </button>
                <button
                  type="button"
                  className="secondary-button"
                  onClick={() =>
                    downloadCsv(
                      simulationResultCsv(result, unitDimensions),
                      `${resultFilename}-complete.csv`,
                    )
                  }
                >
                  Export complete
                </button>
              </div>
            </header>
            <div className="result-table-scroll">
              <table className="result-table stream-result-table">
                <thead>
                  <tr>
                    <th>Scope</th>
                    <th>Component</th>
                    <th>Port</th>
                    <th>Domain / context</th>
                    <th>Value</th>
                    <th>Displayed value</th>
                    <th>Unit</th>
                    <th>Displayed derivative</th>
                  </tr>
                </thead>
                <tbody>
                  {filteredRows.map((value) => {
                    const displayed = displayValue(
                      value.valueSi,
                      value.dimension,
                      profile,
                      unitDimensions,
                    )
                    const derivative =
                      value.derivativeSiS === undefined
                        ? undefined
                        : displayDeltaValue(
                            value.derivativeSiS,
                            value.dimension,
                            profile,
                            unitDimensions,
                          )
                    const context =
                      [value.mediumId, value.phase]
                        .filter(Boolean)
                        .join(' · ') || value.componentKind
                    return (
                      <tr key={value.key}>
                        <td>{scopeLabels[value.scope]}</td>
                        <th>{value.componentId || 'system'}</th>
                        <td>{value.portName || '—'}</td>
                        <td>
                          {[value.domain, context]
                            .filter(Boolean)
                            .join(' · ') || '—'}
                        </td>
                        <td>{value.name}</td>
                        <td>{formatResultValue(displayed.value)}</td>
                        <td>{displayed.unit}</td>
                        <td>
                          {derivative
                            ? `${formatResultValue(derivative.value)} ${
                                derivative.unit
                              }`
                            : '—'}
                        </td>
                      </tr>
                    )
                  })}
                  {!filteredRows.length && (
                    <tr>
                      <td colSpan={8} className="result-table-empty">
                        No graph values match the current filter.
                      </td>
                    </tr>
                  )}
                </tbody>
              </table>
            </div>
          </section>

          {isTransientResult(result) && result.events.length > 0 && (
            <section className="result-events">
              <h3>Events</h3>
              {result.events.map((event, index) => (
                <div key={`${event.name}-${event.time}-${index}`}>
                  <strong>{event.name}</strong>
                  <span>t = {formatResultValue(event.time)} s</span>
                  <small>{event.terminal ? 'terminal' : 'non-terminal'}</small>
                </div>
              ))}
            </section>
          )}
        </div>
      )}
    </section>
  )
}
