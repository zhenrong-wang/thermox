import { useDisplayUnits } from './DisplayUnitsContext'
import { displayValue } from './displayUnits'
import { formatResultValue } from './resultPresentation'
import { jobLifecycle } from './jobLifecycle'
import type {
  SimulationJob,
  SimulationJobState,
} from './types'

interface ExecutionHistoryProps {
  jobs: SimulationJob[]
  selectedJobId: string
  loading: boolean
  submitting: boolean
  stateFilter: '' | SimulationJobState
  nextCursor: string | null
  onSelect: (jobId: string) => void
  onStateFilter: (state: '' | SimulationJobState) => void
  onSubmit: () => void
  onRefresh: () => void
  onLoadMore: () => void
  onCancel: (job: SimulationJob) => void
}

const states: SimulationJobState[] = [
  'queued',
  'running',
  'succeeded',
  'failed',
  'cancelled',
]

function createdAt(epochMs: number) {
  return new Date(epochMs).toISOString().replace('T', ' ').replace('.000Z', 'Z')
}

export function ExecutionHistory({
  jobs,
  selectedJobId,
  loading,
  submitting,
  stateFilter,
  nextCursor,
  onSelect,
  onStateFilter,
  onSubmit,
  onRefresh,
  onLoadMore,
  onCancel,
}: ExecutionHistoryProps) {
  const { profile, unitDimensions } = useDisplayUnits()
  const selected = jobs.find((job) => job.job_id === selectedJobId) ?? jobs[0]
  const active = selected?.state === 'queued' || selected?.state === 'running'
  const progressNote =
    selected?.state === 'queued'
      ? 'Waiting for a worker claim. This view refreshes automatically.'
      : selected?.state === 'running'
        ? 'The worker is solving this job. Numerical progress is not estimated by the queue.'
        : ''

  return (
    <section className="execution-history">
      <header>
        <div>
          <span className="section-kicker">Durable worker queue</span>
          <h2>Execution history</h2>
          <p>Team-scoped jobs for this exact run-configuration revision.</p>
        </div>
        <div className="execution-actions">
          <select
            aria-label="Execution state filter"
            value={stateFilter}
            onChange={(event) =>
              onStateFilter(event.target.value as '' | SimulationJobState)
            }
          >
            <option value="">All states</option>
            {states.map((state) => (
              <option key={state} value={state}>
                {state}
              </option>
            ))}
          </select>
          <button
            type="button"
            className="secondary-button"
            disabled={loading}
            onClick={onRefresh}
          >
            {loading ? 'Refreshing…' : 'Refresh'}
          </button>
          <button
            type="button"
            className="primary-button"
            disabled={submitting}
            onClick={onSubmit}
          >
            {submitting ? 'Submitting…' : 'Queue execution'}
          </button>
        </div>
      </header>
      <div className="execution-body">
        <div className="job-list">
          {!jobs.length && (
            <div className="job-empty">
              No jobs match this configuration and state filter.
            </div>
          )}
          {jobs.map((job) => (
            <button
              type="button"
              key={job.job_id}
              className={
                job.job_id === selected?.job_id
                  ? 'job-card selected'
                  : 'job-card'
              }
              onClick={() => onSelect(job.job_id)}
            >
              <div>
                <span className={`job-state ${job.state}`}>{job.state}</span>
                <small>revision {job.revision}</small>
              </div>
              <code>{job.job_id}</code>
              <span>{createdAt(job.created_at_unix_ms)}</span>
              <small>
                attempt {job.attempt} · {job.request.mode}
              </small>
            </button>
          ))}
          {nextCursor && (
            <button
              type="button"
              className="secondary-button job-load-more"
              disabled={loading}
              onClick={onLoadMore}
            >
              Load older jobs
            </button>
          )}
        </div>

        <div className="job-detail">
          {!selected ? (
            <div className="job-detail-empty">Select or submit a job.</div>
          ) : (
            <>
              <header>
                <div>
                  <span className={`job-state ${selected.state}`}>
                    {selected.state}
                  </span>
                  <code>{selected.job_id}</code>
                </div>
                {active && (
                  <button
                    type="button"
                    className="danger-button"
                    onClick={() => onCancel(selected)}
                  >
                    Cancel
                  </button>
                )}
              </header>
              <div className="job-facts">
                <label>
                  <span>Job revision</span>
                  <strong>{selected.revision}</strong>
                </label>
                <label>
                  <span>Attempt</span>
                  <strong>{selected.attempt}</strong>
                </label>
                <label>
                  <span>Worker</span>
                  <code>{selected.worker_id || 'unclaimed'}</code>
                </label>
                <label>
                  <span>Submitted by</span>
                  <code>{selected.owner.submitted_by_user_id}</code>
                </label>
              </div>
              <div className="job-lifecycle" aria-label="Execution lifecycle">
                {jobLifecycle(
                  selected.state,
                  Boolean(selected.worker_id || selected.attempt > 0),
                ).map((stage) => (
                  <div className={stage.status} key={stage.id}>
                    <i />
                    <span>{stage.label}</span>
                  </div>
                ))}
              </div>
              {progressNote &&
                <p className="job-progress-note">
                  {progressNote}
                </p>}
              {selected.request.source_revisions && (
                <div className="job-provenance">
                  <span>
                    Model{' '}
                    <code>
                      {selected.request.source_revisions.model_revision_id}
                    </code>
                  </span>
                  <span>
                    Case{' '}
                    <code>
                      {selected.request.source_revisions.case_revision_id}
                    </code>
                  </span>
                  <span>
                    Config{' '}
                    <code>
                      {
                        selected.request.source_revisions
                          .run_configuration_revision_id
                      }
                    </code>
                  </span>
                </div>
              )}
              {selected.error && (
                <div className="job-error">
                  <strong>{selected.error.code}</strong>
                  <span>{selected.error.stage}</span>
                  <p>{selected.error.message}</p>
                </div>
              )}
              {selected.result_summary && (
                <div className="job-summary">
                  <header>
                    <h3>Projected results</h3>
                    <span>{selected.result_summary.mode}</span>
                  </header>
                  {selected.result_summary.values.map((value) => (
                    (() => {
                      const displayed = displayValue(
                        value.value_si,
                        value.dimension,
                        profile,
                        unitDimensions,
                      )
                      return (
                        <div key={value.id}>
                          <strong>{value.id}</strong>
                          <code>{formatResultValue(displayed.value)}</code>
                          <span>{displayed.unit}</span>
                          <small>{value.aggregation}</small>
                        </div>
                      )
                    })()
                  ))}
                </div>
              )}
              {selected.result_artifact && (
                <div className="result-manifest">
                  <span>Full result artifact</span>
                  <code>{selected.result_artifact.checksum}</code>
                  <small>{selected.result_artifact.byte_size} bytes</small>
                </div>
              )}
            </>
          )}
        </div>
      </div>
    </section>
  )
}
