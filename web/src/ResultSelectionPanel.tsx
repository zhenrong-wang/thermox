import type { RunConfigurationRevision, SimulationJob } from './types'

interface ResultSelectionPanelProps {
  revisions: RunConfigurationRevision[]
  selectedRevisionId: string
  jobs: SimulationJob[]
  selectedJobId: string
  loading: boolean
  onSelectRevision: (revisionId: string) => void
  onSelectJob: (jobId: string) => void
  onRefresh: () => void
}

export function ResultSelectionPanel({
  revisions,
  selectedRevisionId,
  jobs,
  selectedJobId,
  loading,
  onSelectRevision,
  onSelectJob,
  onRefresh,
}: ResultSelectionPanelProps) {
  return (
    <div className="result-selection-panel">
      <header>
        <span className="eyebrow">Reproducible output</span>
        <h2>Result source</h2>
        <p>Select an immutable configuration and succeeded execution.</p>
      </header>
      <label>
        <span>Run configuration</span>
        <select
          value={selectedRevisionId}
          onChange={(event) => onSelectRevision(event.target.value)}
        >
          {revisions.map((revision) => (
            <option
              key={revision.run_configuration_revision_id}
              value={revision.run_configuration_revision_id}
            >
              {revision.run_configuration_id} r{revision.revision_number}
            </option>
          ))}
        </select>
      </label>
      <div className="result-job-heading">
        <span>Succeeded jobs</span>
        <button type="button" disabled={loading} onClick={onRefresh}>
          {loading ? 'Loading…' : 'Refresh'}
        </button>
      </div>
      <div className="result-job-list">
        {!jobs.length && (
          <div className="case-list-empty">
            <strong>No successful results</strong>
            <span>Run this configuration successfully to inspect output.</span>
          </div>
        )}
        {jobs.map((job) => (
          <button
            type="button"
            key={job.job_id}
            className={
              job.job_id === selectedJobId
                ? 'result-job-card selected'
                : 'result-job-card'
            }
            onClick={() => onSelectJob(job.job_id)}
          >
            <div>
              <strong>{job.job_id}</strong>
              <span>attempt {job.attempt}</span>
            </div>
            <small>{job.request.mode}</small>
            <code>revision {job.revision}</code>
          </button>
        ))}
      </div>
      <footer>
        <span>Object-backed</span>
        <code>thermox.result/v3</code>
      </footer>
    </div>
  )
}
