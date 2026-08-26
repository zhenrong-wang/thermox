import { useEffect, useMemo, useState } from 'react'
import { useDisplayUnits } from './DisplayUnitsContext'
import {
  displayDeltaValue,
  displayMarginValue,
  displayValue,
} from './displayUnits'
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
import type { GraphSelection } from './InspectorPanel'
import {
  exactRevisionProvenance,
  resultDiagnosticSummary,
} from './resultDiagnostics'
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
  JobValidationReport,
  SimulationJob,
  SimulationJobComparison,
  SimulationResult,
  TopologyDocument,
} from './types'

interface ResultsWorkspaceProps {
  topology?: TopologyDocument
  topologyRevisionId: string
  catalog: CatalogComponent[]
  job?: SimulationJob
  result?: SimulationResult
  loading: boolean
  error: string
  comparisonJobs: SimulationJob[]
  comparison?: SimulationJobComparison
  comparisonLoading: boolean
  comparisonError: string
  validationReport?: JobValidationReport
  validationReportLoading: boolean
  validationReportError: string
  onRetry: () => void
  onCompare: (candidateJobId: string) => void
  onClearComparison: () => void
  onGenerateValidationReport: (jobIds: string[]) => void
  onClearValidationReport: () => void
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
  topologyRevisionId,
  catalog,
  job,
  result,
  loading,
  error,
  comparisonJobs,
  comparison,
  comparisonLoading,
  comparisonError,
  validationReport,
  validationReportLoading,
  validationReportError,
  onRetry,
  onCompare,
  onClearComparison,
  onGenerateValidationReport,
  onClearValidationReport,
}: ResultsWorkspaceProps) {
  const { profile, unitDimensions } = useDisplayUnits()
  const [sampleIndex, setSampleIndex] = useState(0)
  const [query, setQuery] = useState('')
  const [scope, setScope] = useState<ResultScopeFilter>('all')
  const [seriesKey, setSeriesKey] = useState('')
  const [selection, setSelection] = useState<GraphSelection>()
  const [candidateJobId, setCandidateJobId] = useState('')
  const [validationJobIds, setValidationJobIds] = useState<string[]>([])
  const sampleCount = result ? resultSampleCount(result) : 0

  useEffect(() => {
    setSampleIndex(Math.max(0, sampleCount - 1))
    setQuery('')
    setScope('all')
    setSelection(undefined)
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

  const selectedComponentId =
    selection?.type === 'component' ? selection.id : ''
  const candidates = comparisonJobs.filter(
    (candidate) =>
      candidate.job_id !== job?.job_id &&
      candidate.request.mode === job?.request.mode &&
      candidate.state === 'succeeded' &&
      Boolean(candidate.result_summary),
  )
  const validationCandidates = comparisonJobs.filter((candidate) =>
    ['succeeded', 'failed', 'cancelled'].includes(candidate.state),
  )

  useEffect(() => {
    setCandidateJobId((current) =>
      candidates.some((candidate) => candidate.job_id === current)
        ? current
        : candidates[0]?.job_id ?? '',
    )
  }, [job?.job_id, comparisonJobs])

  useEffect(() => {
    const available = new Set(
      validationCandidates.map((candidate) => candidate.job_id),
    )
    setValidationJobIds((current) => {
      const retained = current.filter((jobId) => available.has(jobId))
      if (retained.length) return retained
      return job && available.has(job.job_id) ? [job.job_id] : []
    })
    onClearValidationReport()
  }, [job?.job_id, comparisonJobs])

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
  const diagnosticSummary = result
    ? resultDiagnosticSummary(result)
    : undefined
  const requestedSource = job.request.source_revisions
  const executedSource = result?.metadata.source_revisions
  const exactProvenance = exactRevisionProvenance(
    requestedSource,
    executedSource,
    topologyRevisionId,
    Boolean(topology),
  )
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
        <div
          className={`result-provenance-chip${exactProvenance ? ' exact' : ''}`}
        >
          <span>{exactProvenance ? 'Exact Study' : 'Topology source'}</span>
          <code>
            {job.request.source_revisions?.study_revision_id ?? 'snapshot'}
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
      <section className="comparison-control">
        <div>
          <span className="section-kicker">Study evidence</span>
          <strong>Compare this result</strong>
        </div>
        <select
          aria-label="Comparison candidate"
          value={candidateJobId}
          disabled={!candidates.length || comparisonLoading}
          onChange={(event) => {
            setCandidateJobId(event.target.value)
            onClearComparison()
          }}
        >
          {!candidates.length && (
            <option value="">No compatible result</option>
          )}
          {candidates.map((candidate) => (
            <option value={candidate.job_id} key={candidate.job_id}>
              {candidate.request.source_revisions?.study_revision_id || 'Study'} ·{' '}
              {candidate.job_id}
            </option>
          ))}
        </select>
        <button
          type="button"
          className="secondary-button"
          disabled={!candidateJobId || comparisonLoading}
          onClick={() => onCompare(candidateJobId)}
        >
          {comparisonLoading ? 'Comparing…' : 'Compare'}
        </button>
      </section>
      {comparisonError && (
        <div className="operation-banner is-error">{comparisonError}</div>
      )}
      <details className="validation-report-control">
        <summary>
          <span>
            <span className="section-kicker">Validation campaign</span>
            <strong>Build evidence coverage report</strong>
          </span>
          <small>{validationJobIds.length} of {validationCandidates.length} selected</small>
        </summary>
        <p>
          Select terminal Study jobs from this Project. Agreement is reported
          only against each job&apos;s immutable evidence and is not a global
          engineering-readiness verdict.
        </p>
        <div className="validation-job-picker">
          {validationCandidates.map((candidate) => (
            <label key={candidate.job_id}>
              <input
                type="checkbox"
                checked={validationJobIds.includes(candidate.job_id)}
                onChange={(event) => {
                  setValidationJobIds((current) => event.target.checked
                    ? [...current, candidate.job_id]
                    : current.filter((jobId) => jobId !== candidate.job_id))
                  onClearValidationReport()
                }}
              />
              <span>
                <strong>
                  {candidate.request.source_revisions?.study_revision_id ?? 'Study'}
                </strong>
                <small>{candidate.state} · {candidate.request.mode}</small>
                <code>{candidate.job_id}</code>
              </span>
            </label>
          ))}
          {!validationCandidates.length && (
            <p>No terminal revision-backed Study jobs are available.</p>
          )}
        </div>
        <button
          type="button"
          className="secondary-button"
          disabled={!validationJobIds.length || validationReportLoading}
          onClick={() => onGenerateValidationReport(validationJobIds)}
        >
          {validationReportLoading ? 'Building report…' : 'Build report'}
        </button>
      </details>
      {validationReportError && (
        <div className="operation-banner is-error">{validationReportError}</div>
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
                  <small>{value.aggregation}</small>
                  {value.window && (
                    <small>
                      t={formatResultValue(value.window.start_time)}…
                      {formatResultValue(value.window.end_time)} s
                    </small>
                  )}
                </div>
              )
            })}
            {!job.result_summary?.values.length && (
              <p>No projected summary values were configured for this run.</p>
            )}
          </div>

          {job.result_summary?.engineering_acceptance && (
            <section className={`engineering-acceptance-card ${
              job.result_summary.engineering_acceptance.passed
                ? 'accepted' : 'not-accepted'
            }`}>
              <header>
                <div>
                  <span className="section-kicker">Engineering criteria</span>
                  <h2>Acceptance evaluation</h2>
                </div>
                <strong>
                  {job.result_summary.engineering_acceptance.passed
                    ? 'Accepted' : 'Not accepted'}
                </strong>
              </header>
              {job.result_summary.engineering_acceptance.criteria.map(
                (criterion) => {
                  const actual = displayValue(
                    criterion.actual_value_si,
                    criterion.dimension,
                    profile,
                    unitDimensions,
                  )
                  const lower = criterion.lower_bound_si === null
                    ? '−∞'
                    : formatResultValue(displayValue(
                      criterion.lower_bound_si,
                      criterion.dimension,
                      profile,
                      unitDimensions,
                    ).value)
                  const upper = criterion.upper_bound_si === null
                    ? '+∞'
                    : formatResultValue(displayValue(
                      criterion.upper_bound_si,
                      criterion.dimension,
                      profile,
                      unitDimensions,
                    ).value)
                  const margin = displayMarginValue(
                    criterion.limiting_margin_si,
                    criterion.dimension,
                    profile,
                    unitDimensions,
                  )
                  return (
                    <div className="acceptance-result-row" key={criterion.criterion_id}>
                      <span className={criterion.passed ? 'passed' : 'failed'}>
                        {criterion.passed ? 'Pass' : 'Fail'}
                      </span>
                      <strong>{criterion.criterion_id}</strong>
                      <code>
                        {formatResultValue(actual.value)} {actual.unit}
                      </code>
                      <small>
                        {criterion.lower_inclusive ? '[' : '('}{lower}, {upper}
                        {criterion.upper_inclusive ? ']' : ')'} {actual.unit}
                        {' · '}{criterion.limiting_bound} margin{' '}
                        {formatResultValue(margin.value)} {margin.unit}
                      </small>
                    </div>
                  )
                },
              )}
            </section>
          )}

          {isTransientResult(result) && result.trajectory_validations.map(
            (validation, validationIndex) => (
              <section className={`engineering-acceptance-card ${
                validation.evidence.passed ? 'accepted' : 'not-accepted'
              }`} key={`${validation.artifact_id}-${validationIndex}`}>
                <header>
                  <div>
                    <span className="section-kicker">Reference evidence</span>
                    <h2>Trajectory validation</h2>
                  </div>
                  <strong>
                    {validation.evidence.passed ? 'Validated' : 'Not validated'}
                  </strong>
                </header>
                <div className="solver-diagnostic-facts">
                  <div>
                    <span>Artifact</span>
                    <code>{validation.artifact_id}</code>
                  </div>
                  <div>
                    <span>Exact alignments</span>
                    <code>{validation.exact_alignment_count}</code>
                  </div>
                  <div>
                    <span>Interpolated alignments</span>
                    <code>{validation.interpolated_alignment_count}</code>
                  </div>
                  <div>
                    <span>Maximum alignment gap</span>
                    <code>{formatResultValue(
                      validation.maximum_alignment_gap_si,
                    )} s</code>
                  </div>
                </div>
                {validation.evidence.criteria.map((criterion) => {
                  const actual = displayValue(
                    criterion.actual_value_si,
                    criterion.dimension,
                    profile,
                    unitDimensions,
                  )
                  const reference = displayValue(
                    criterion.reference_value_si,
                    criterion.dimension,
                    profile,
                    unitDimensions,
                  )
                  const error = displayMarginValue(
                    criterion.absolute_error_si,
                    criterion.dimension,
                    profile,
                    unitDimensions,
                  )
                  const allowed = displayMarginValue(
                    criterion.allowed_absolute_error_si,
                    criterion.dimension,
                    profile,
                    unitDimensions,
                  )
                  return (
                    <div className="acceptance-result-row"
                      key={criterion.criterion_id}>
                      <span className={criterion.passed ? 'passed' : 'failed'}>
                        {criterion.passed ? 'Pass' : 'Fail'}
                      </span>
                      <strong>{criterion.criterion_id}</strong>
                      <code>
                        {formatResultValue(actual.value)} {actual.unit}
                      </code>
                      <small>
                        reference {formatResultValue(reference.value)}{' '}
                        {reference.unit} · |error| {formatResultValue(error.value)}{' '}
                        {error.unit} ≤ {formatResultValue(allowed.value)} {allowed.unit}
                        {' · '}{criterion.basis}
                      </small>
                      <small>{criterion.source_reference}</small>
                      {criterion.note && <small>{criterion.note}</small>}
                    </div>
                  )
                })}
                {validation.evidence.limitations.map((limitation) => (
                  <p key={limitation}><strong>Limitation:</strong> {limitation}</p>
                ))}
              </section>
            ),
          )}

          {comparison && (
            <section className="study-comparison-card">
              <header>
                <div>
                  <span className="section-kicker">Service-owned comparison</span>
                  <h2>Study result delta</h2>
                </div>
                <strong>
                  {comparison.engineering_acceptance.transition.replaceAll('_', ' ')}
                </strong>
              </header>
              <div className="comparison-provenance">
                <span>Baseline <code>{comparison.baseline_study_revision_id}</code></span>
                <span>Candidate <code>{comparison.candidate_study_revision_id}</code></span>
                <span>
                  {comparison.coverage.matched_count} matched ·{' '}
                  {comparison.coverage.incompatible_count} incompatible
                </span>
              </div>
              {comparison.trajectory_validation.compatibility !==
                'not_evaluated' && (
                <div className={`job-acceptance ${
                  comparison.trajectory_validation.compatibility === 'comparable' &&
                  comparison.trajectory_validation.candidate_passed
                    ? 'accepted' : 'not-accepted'
                }`}>
                  <strong>Reference validation regression</strong>
                  <span>
                    {comparison.trajectory_validation.compatibility === 'comparable'
                      ? comparison.trajectory_validation.transition.replaceAll('_', ' ')
                      : comparison.trajectory_validation.compatibility.replaceAll('_', ' ')}
                    {' · '}baseline {comparison.trajectory_validation.baseline_sample_count}
                    {' samples · '}candidate{' '}
                    {comparison.trajectory_validation.candidate_sample_count} samples
                  </span>
                </div>
              )}
              <div className="comparison-table-wrap">
                <table className="comparison-table">
                  <thead>
                    <tr>
                      <th>Projection</th>
                      <th>Status</th>
                      <th>Baseline</th>
                      <th>Candidate</th>
                      <th>Δ candidate − baseline</th>
                      <th>Relative</th>
                    </tr>
                  </thead>
                  <tbody>
                    {comparison.values.map((value) => {
                      const baseline = value.baseline_value_si === null
                        ? undefined
                        : displayValue(
                            value.baseline_value_si,
                            value.baseline_dimension,
                            profile,
                            unitDimensions,
                          )
                      const candidate = value.candidate_value_si === null
                        ? undefined
                        : displayValue(
                            value.candidate_value_si,
                            value.candidate_dimension,
                            profile,
                            unitDimensions,
                          )
                      const delta = value.absolute_delta_si === null
                        ? undefined
                        : (() => {
                            const displayed = displayValue(
                              value.absolute_delta_si,
                              value.baseline_dimension,
                              profile,
                              unitDimensions,
                            )
                            const zero = displayValue(
                              0,
                              value.baseline_dimension,
                              profile,
                              unitDimensions,
                            )
                            return {
                              value: displayed.value - zero.value,
                              unit: displayed.unit,
                            }
                          })()
                      return (
                        <tr key={value.id}>
                          <td><strong>{value.id}</strong></td>
                          <td>
                            <span className={`comparison-status ${value.status}`}>
                              {value.status.replaceAll('_', ' ')}
                            </span>
                          </td>
                          <td>{baseline
                            ? `${formatResultValue(baseline.value)} ${baseline.unit}`
                            : '—'}</td>
                          <td>{candidate
                            ? `${formatResultValue(candidate.value)} ${candidate.unit}`
                            : '—'}</td>
                          <td>{delta
                            ? `${formatResultValue(delta.value)} ${delta.unit}`
                            : '—'}</td>
                          <td>{value.relative_delta === null
                            ? '—'
                            : `${formatResultValue(value.relative_delta * 100)}%`}</td>
                        </tr>
                      )
                    })}
                  </tbody>
                </table>
              </div>
            </section>
          )}

          {validationReport && (
            <section className="validation-report-card">
              <header>
                <div>
                  <span className="section-kicker">Service-owned evidence matrix</span>
                  <h2>Validation coverage</h2>
                </div>
                <strong>{validationReport.coverage.job_count} jobs</strong>
              </header>
              <div className="validation-coverage-strip">
                <span>Numerically succeeded <strong>{validationReport.coverage.succeeded_count}</strong></span>
                <span>Evidence declared <strong>{validationReport.coverage.evidence_declared_count}</strong></span>
                <span>Evaluated <strong>{validationReport.coverage.evaluated_count}</strong></span>
                <span>Matched <strong>{validationReport.coverage.matched_count}</strong></span>
                <span>Not matched <strong>{validationReport.coverage.not_matched_count}</strong></span>
                <span>Unevaluated <strong>{validationReport.coverage.unevaluated_count}</strong></span>
              </div>
              <div className="comparison-table-wrap">
                <table className="comparison-table">
                  <thead>
                    <tr>
                      <th>Study / job</th>
                      <th>Numerical state</th>
                      <th>Reference status</th>
                      <th>Samples pass / fail</th>
                      <th>Alignment exact / interpolated</th>
                      <th>Evidence revisions</th>
                    </tr>
                  </thead>
                  <tbody>
                    {validationReport.jobs.map((entry) => (
                      <tr key={entry.job_id}>
                        <td>
                          <strong>{entry.study_revision_id}</strong><br />
                          <code>{entry.job_id}</code>
                        </td>
                        <td>{entry.state}</td>
                        <td>
                          <span className={`validation-report-status ${entry.validation_status}`}>
                            {entry.validation_status.replaceAll('_', ' ')}
                          </span>
                        </td>
                        <td>{entry.passed_count} / {entry.failed_count}</td>
                        <td>{entry.exact_alignment_count} / {entry.interpolated_alignment_count}</td>
                        <td>{entry.evidence_artifact_revision_ids.length
                          ? entry.evidence_artifact_revision_ids.map((revisionId) => (
                              <code className="validation-evidence-id" key={revisionId}>
                                {revisionId}
                              </code>
                            ))
                          : '—'}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
              <footer>
                {validationReport.samples.passed_count} passed and{' '}
                {validationReport.samples.failed_count} failed reference samples ·{' '}
                {validationReport.samples.exact_alignment_count} exact and{' '}
                {validationReport.samples.interpolated_alignment_count} interpolated alignments
              </footer>
            </section>
          )}

          {diagnosticSummary && (
            <section className="solver-diagnostic-card">
              <header>
                <div>
                  <span className="section-kicker">Numerical evidence</span>
                  <h2>
                    {diagnosticSummary.mode === 'steady'
                      ? 'Steady convergence'
                      : 'Transient integration'}
                  </h2>
                </div>
                <span
                  className={
                    diagnosticSummary.successful
                      ? 'solver-outcome successful'
                      : 'solver-outcome unsuccessful'
                  }
                >
                  {diagnosticSummary.successful ? 'Successful' : 'Review'}
                </span>
              </header>
              <div className="solver-diagnostic-facts">
                {diagnosticSummary.facts.map((fact) => (
                  <div key={fact.label}>
                    <span>{fact.label}</span>
                    <code>{fact.value}</code>
                  </div>
                ))}
              </div>
              <p>{diagnosticSummary.message || 'No solver message reported.'}</p>
            </section>
          )}

          <section className={`solver-diagnostic-card ${
            result.thermal_feasibility.passed
              ? 'physical-feasibility-passed'
              : 'physical-feasibility-failed'
          }`}>
            <header>
              <div>
                <span className="section-kicker">Physical evidence</span>
                <h2>Counterflow thermal feasibility</h2>
              </div>
              <span className={
                result.thermal_feasibility.passed
                  ? 'solver-outcome successful'
                  : 'solver-outcome unsuccessful'
              }>
                {result.thermal_feasibility.passed
                  ? 'Admissible' : 'Not admissible'}
              </span>
            </header>
            <div className="solver-diagnostic-facts">
              <div>
                <span>Checked exchangers</span>
                <code>{result.thermal_feasibility.checked_count}</code>
              </div>
              <div>
                <span>Passed</span>
                <code>{result.thermal_feasibility.passed_count}</code>
              </div>
              <div>
                <span>Failed</span>
                <code>{result.thermal_feasibility.failed_count}</code>
              </div>
              <div>
                <span>Evaluation scope</span>
                <code>{result.thermal_feasibility.scope}</code>
              </div>
            </div>
            {result.thermal_feasibility.counterflow_approaches
              .filter((approach) => !approach.passed)
              .slice(0, 5)
              .map((approach) => (
                <p key={approach.component_id}>
                  <strong>{approach.component_id}</strong>{' '}
                  crosses by {formatResultValue(
                    Math.abs(approach.minimum_approach_k),
                  )} K
                  {approach.sample_time === null
                    ? ''
                    : ` at t=${formatResultValue(approach.sample_time)} s`}
                </p>
              ))}
            {!result.thermal_feasibility.checked_count && (
              <p>No counterflow four-port components were present.</p>
            )}
          </section>

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
              <div className="result-graph-context">
                {selectedComponentId && (
                  <button
                    type="button"
                    onClick={() => setSelection(undefined)}
                  >
                    {selectedComponentId} ×
                  </button>
                )}
                <small>Values shown in {profile} display units</small>
              </div>
            </header>
            <div className="result-canvas">
              {exactProvenance ? (
                <TopologyCanvas
                  topology={topology}
                  catalog={catalog}
                  revisionId={`${job.job_id}-${sampleIndex}`}
                  publishing={false}
                  readOnly
                  resultValues={overlays}
                  selection={selection}
                  onConnect={async () => undefined}
                  onSelect={setSelection}
                />
              ) : (
                <div className="result-provenance-block">
                  <strong>Canvas overlay withheld</strong>
                  <p>
                    Load the exact topology revision recorded by both the job
                    request and worker execution before projecting values onto
                    equipment.
                  </p>
                  <code>
                    expected {executedSource?.model_revision_id ?? 'unknown'} ·
                    loaded {topologyRevisionId || 'none'}
                  </code>
                </div>
              )}
            </div>
          </section>

          <div className="result-diagnostics">
            <span>Solver contract</span>
            <code>{result.metadata.solver.contract_version || '—'}</code>
            <span>Catalog</span>
            <code>{result.metadata.catalog_fingerprint || '—'}</code>
            <span>Provenance</span>
            <code>
              {exactProvenance
                ? 'revision provenance exact'
                : 'review revision bindings'}
            </code>
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
                    const selected =
                      Boolean(value.componentId) &&
                      value.componentId === selectedComponentId
                    return (
                      <tr
                        key={value.key}
                        className={selected ? 'selected-entity-row' : ''}
                      >
                        <td>{scopeLabels[value.scope]}</td>
                        <th>
                          {value.componentId ? (
                            <button
                              type="button"
                              className="result-entity-link"
                              onClick={() =>
                                setSelection({
                                  type: 'component',
                                  id: value.componentId,
                                })
                              }
                            >
                              {value.componentId}
                            </button>
                          ) : (
                            'system'
                          )}
                        </th>
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
