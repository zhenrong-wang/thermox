import { useEffect, useMemo, useState } from 'react'
import { useDisplayUnits } from './DisplayUnitsContext'
import { displayDeltaValue, displayValue } from './displayUnits'
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
  GraphResultValue,
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

function ResultValueTable({
  title,
  values,
}: {
  title: string
  values: GraphResultValue[]
}) {
  const { profile, unitDimensions } = useDisplayUnits()
  if (!values.length) return null
  return (
    <section className="result-value-section">
      <h3>{title}</h3>
      <div className="result-table-scroll">
        <table className="result-table">
          <thead>
            <tr>
              <th>Value</th>
              <th>Displayed value</th>
              <th>Unit</th>
              <th>Displayed derivative</th>
            </tr>
          </thead>
          <tbody>
            {values.map((value) => (
              (() => {
                const displayed = displayValue(
                  value.value_si,
                  value.dimension,
                  profile,
                  unitDimensions,
                )
                const derivative =
                  value.derivative_si_s === undefined
                    ? undefined
                    : displayDeltaValue(
                        value.derivative_si_s,
                        value.dimension,
                        profile,
                        unitDimensions,
                      )
                return (
                  <tr key={`${title}-${value.name}`}>
                    <th>{value.name}</th>
                    <td>{formatResultValue(displayed.value)}</td>
                    <td>{displayed.unit}</td>
                    <td>
                      {derivative
                        ? `${formatResultValue(derivative.value)} ${derivative.unit}`
                        : '—'}
                    </td>
                  </tr>
                )
              })()
            ))}
          </tbody>
        </table>
      </div>
    </section>
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
  const sampleCount = result ? resultSampleCount(result) : 0

  useEffect(() => {
    setSampleIndex(Math.max(0, sampleCount - 1))
  }, [job?.job_id, sampleCount])

  const graph = result ? resultGraph(result, sampleIndex) : undefined
  const overlays = useMemo(
    () => (job && graph ? projectedGraphNodeValues(job, graph) : {}),
    [graph, job],
  )
  const sampleTime = result ? resultSampleTime(result, sampleIndex) : null

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
            {(job.result_summary?.values ?? []).map((value) => (
              (() => {
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
              })()
            ))}
            {!job.result_summary?.values.length && (
              <p>No projected summary values were configured for this run.</p>
            )}
          </div>

          {isTransientResult(result) && sampleCount > 0 && (
            <section className="result-timeline">
              <div>
                <strong>Transient sample</strong>
                <span>
                  {sampleIndex + 1} / {sampleCount} · t ={' '}
                  {sampleTime === null ? '—' : formatResultValue(sampleTime)} s
                </span>
              </div>
              <input
                aria-label="Transient result sample"
                type="range"
                min="0"
                max={Math.max(0, sampleCount - 1)}
                value={sampleIndex}
                onChange={(event) => setSampleIndex(Number(event.target.value))}
              />
            </section>
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

          <ResultValueTable
            title="System balances"
            values={graph.system_balances}
          />
          <ResultValueTable title="KPIs" values={graph.kpis} />

          <section className="result-value-section">
            <h3>Component and stream results</h3>
            <div className="result-table-scroll">
              <table className="result-table stream-result-table">
                <thead>
                  <tr>
                    <th>Component</th>
                    <th>Port / scope</th>
                    <th>Domain</th>
                    <th>Medium / phase</th>
                    <th>Value</th>
                    <th>Displayed value</th>
                    <th>Unit</th>
                    <th>Displayed derivative</th>
                  </tr>
                </thead>
                <tbody>
                  {graph.components.flatMap((component) => [
                    ...component.metrics.map((value) => (
                      <tr key={`${component.component_id}-metric-${value.name}`}>
                        <th>{component.component_id}</th>
                        <td>metric</td>
                        <td>{component.kind}</td>
                        <td>—</td>
                        <td>{value.name}</td>
                        <td>
                          {formatResultValue(
                            displayValue(
                              value.value_si,
                              value.dimension,
                              profile,
                              unitDimensions,
                            ).value,
                          )}
                        </td>
                        <td>
                          {displayValue(
                            value.value_si,
                            value.dimension,
                            profile,
                            unitDimensions,
                          ).unit}
                        </td>
                        <td>
                          {value.derivative_si_s === undefined
                            ? '—'
                            : `${formatResultValue(
                                displayDeltaValue(
                                  value.derivative_si_s,
                                  value.dimension,
                                  profile,
                                  unitDimensions,
                                ).value,
                              )} ${
                                displayDeltaValue(
                                  value.derivative_si_s,
                                  value.dimension,
                                  profile,
                                  unitDimensions,
                                ).unit
                              }`}
                        </td>
                      </tr>
                    )),
                    ...component.internal_values.map((value) => (
                      <tr
                        key={`${component.component_id}-internal-${value.name}`}
                      >
                        <th>{component.component_id}</th>
                        <td>internal</td>
                        <td>{component.kind}</td>
                        <td>—</td>
                        <td>{value.name}</td>
                        <td>
                          {formatResultValue(
                            displayValue(
                              value.value_si,
                              value.dimension,
                              profile,
                              unitDimensions,
                            ).value,
                          )}
                        </td>
                        <td>
                          {displayValue(
                            value.value_si,
                            value.dimension,
                            profile,
                            unitDimensions,
                          ).unit}
                        </td>
                        <td>
                          {value.derivative_si_s === undefined
                            ? '—'
                            : `${formatResultValue(
                                displayDeltaValue(
                                  value.derivative_si_s,
                                  value.dimension,
                                  profile,
                                  unitDimensions,
                                ).value,
                              )} ${
                                displayDeltaValue(
                                  value.derivative_si_s,
                                  value.dimension,
                                  profile,
                                  unitDimensions,
                                ).unit
                              }`}
                        </td>
                      </tr>
                    )),
                    ...component.ports.flatMap((port) =>
                      [...port.primary_values, ...port.derived_values].map(
                        (value) => (
                          <tr
                            key={`${component.component_id}-${port.port_name}-${value.name}`}
                          >
                            <th>{component.component_id}</th>
                            <td>{port.port_name}</td>
                            <td>{port.domain}</td>
                            <td>
                              {[port.medium_id, port.phase]
                                .filter(Boolean)
                                .join(' · ') || '—'}
                            </td>
                            <td>{value.name}</td>
                            <td>
                              {formatResultValue(
                                displayValue(
                                  value.value_si,
                                  value.dimension,
                                  profile,
                                  unitDimensions,
                                ).value,
                              )}
                            </td>
                            <td>
                              {displayValue(
                                value.value_si,
                                value.dimension,
                                profile,
                                unitDimensions,
                              ).unit}
                            </td>
                            <td>
                              {value.derivative_si_s === undefined
                                ? '—'
                                : `${formatResultValue(
                                    displayDeltaValue(
                                      value.derivative_si_s,
                                      value.dimension,
                                      profile,
                                      unitDimensions,
                                    ).value,
                                  )} ${
                                    displayDeltaValue(
                                      value.derivative_si_s,
                                      value.dimension,
                                      profile,
                                      unitDimensions,
                                    ).unit
                                  }`}
                            </td>
                          </tr>
                        ),
                      ),
                    ),
                  ])}
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
