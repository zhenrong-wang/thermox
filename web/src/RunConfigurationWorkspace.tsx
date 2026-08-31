import { ExecutionHistory } from './ExecutionHistory'
import { ExecutionPreparationStrip } from './ExecutionPreparationStrip'
import {
  buildExecutionPreparationStages,
  executionSelectionReady,
  studyExecutionMode,
} from './executionPreparation'
import type {
  RunConfigurationRevision,
  SimulationJob,
  SimulationJobState,
  SteadySolverSettings,
  StudyRevision,
} from './types'

interface RunConfigurationWorkspaceProps {
  revision?: RunConfigurationRevision
  study?: StudyRevision
  publishing: boolean
  operationError: string
  operationStatus: string
  jobs: SimulationJob[]
  selectedJobId: string
  jobsLoading: boolean
  jobSubmitting: boolean
  jobStateFilter: '' | SimulationJobState
  jobsNextCursor: string | null
  onDismissOperation: () => void
  onCreate: () => void
  onRevise: () => void
  onSelectJob: (jobId: string) => void
  onJobStateFilter: (state: '' | SimulationJobState) => void
  onSubmitJob: () => void
  onRefreshJobs: () => void
  onLoadMoreJobs: () => void
  onCancelJob: (job: SimulationJob) => void
}

function SteadySolverSummary({ value }: { value: SteadySolverSettings }) {
  return (
    <div className="solver-summary-grid">
      {Object.entries(value).map(([name, setting]) => (
        <div key={name}>
          <span>{name.replaceAll('_', ' ')}</span>
          <code>{setting}</code>
        </div>
      ))}
    </div>
  )
}

export function RunConfigurationWorkspace({
  revision,
  study,
  publishing,
  operationError,
  operationStatus,
  jobs,
  selectedJobId,
  jobsLoading,
  jobSubmitting,
  jobStateFilter,
  jobsNextCursor,
  onDismissOperation,
  onCreate,
  onRevise,
  onSelectJob,
  onJobStateFilter,
  onSubmitJob,
  onRefreshJobs,
  onLoadMoreJobs,
  onCancelJob,
}: RunConfigurationWorkspaceProps) {
  if (!revision) {
    return (
      <section className="run-workspace">
        {operationError && (
          <div className="operation-banner is-error">
            {operationError}
            <button type="button" onClick={onDismissOperation}>
              ×
            </button>
          </div>
        )}
        <div className="case-empty">
          <div className="empty-orbit" />
          <h2>No run configuration selected</h2>
          <p>Pin validated engineering inputs into a reusable execution intent.</p>
          <button type="button" className="primary-button" onClick={onCreate}>
            Create run configuration
          </button>
        </div>
      </section>
    )
  }

  const mode = studyExecutionMode(study)
  const submissionReady = executionSelectionReady(revision, study)
  const preparationStages = buildExecutionPreparationStages(
    revision,
    study,
    jobs,
  )

  return (
    <section className="run-workspace">
      <div className="case-toolbar">
        <div>
          <span className="eyebrow">Immutable execution intent</span>
          <h1>{revision.run_configuration_id}</h1>
        </div>
        <div className="run-toolbar-actions">
          <div className="revision-chip">
            <span>CONFIG r{revision.revision_number}</span>
            <code>{revision.checksum.slice(7, 19)}</code>
          </div>
          <button
            type="button"
            className="secondary-button"
            disabled={publishing}
            onClick={onRevise}
          >
            Revise
          </button>
        </div>
      </div>
      {(operationError || operationStatus || publishing) && (
        <div
          className={`operation-banner${operationError ? ' is-error' : ''}`}
        >
          {publishing
            ? 'Publishing immutable run configuration…'
            : operationError || operationStatus}
          <button type="button" onClick={onDismissOperation}>
            ×
          </button>
        </div>
      )}
      <div className="run-editor-scroll">
        <ExecutionPreparationStrip stages={preparationStages} />
        <section className="run-provenance-card" id="run-study">
          <header>
            <h2>Revision bindings</h2>
            <span>{mode}</span>
          </header>
          <div>
            <label>
              <span>Study revision</span>
              <code>{revision.study_revision_id}</code>
            </label>
            <label>
              <span>Parent configuration</span>
              <code>{revision.parent_run_configuration_revision_id || '—'}</code>
            </label>
            <label>
              <span>Study intent</span>
              <code>{study?.intent ?? 'unavailable'}</code>
            </label>
          </div>
        </section>

        <section className="run-detail-card" id="run-policy">
          <header>
            <h2>
              {mode === 'steady'
                ? 'Steady nonlinear solver'
                : 'Transient integration'}
            </h2>
          </header>
          {mode === 'steady' ? (
            <SteadySolverSummary value={revision.steady_solver} />
          ) : (
            <>
              <div className="solver-summary-grid">
                {Object.entries(revision.transient_solver)
                  .filter(([name]) => name !== 'nonlinear_solver')
                  .map(([name, setting]) => (
                    <div key={name}>
                      <span>{name.replaceAll('_', ' ')}</span>
                      <code>{String(setting)}</code>
                    </div>
                  ))}
              </div>
              <h3>Nonlinear solver</h3>
              <SteadySolverSummary
                value={revision.transient_solver.nonlinear_solver}
              />
            </>
          )}
        </section>

        <section className="run-detail-card" id="run-review">
          <header>
            <h2>Exact execution review</h2>
            <span>{submissionReady ? 'ready' : 'blocked'}</span>
          </header>
          <div className="execution-review-grid">
            <div>
              <span>Model revision</span>
              <code>{study?.model_revision_id ?? 'unavailable'}</code>
            </div>
            <div>
              <span>Case revision</span>
              <code>{study?.case_revision_id ?? 'unavailable'}</code>
            </div>
            <div>
              <span>Study fingerprint</span>
              <code>{study?.checksum ?? 'unavailable'}</code>
            </div>
            <div>
              <span>Configuration fingerprint</span>
              <code>{revision.checksum}</code>
            </div>
            <div>
              <span>Engineering artifacts</span>
              <strong>{study?.artifact_revision_ids.length ?? 0}</strong>
            </div>
            <div>
              <span>Acceptance criteria</span>
              <strong>{study?.acceptance_criteria.length ?? 0}</strong>
            </div>
          </div>
          <header className="run-subheading">
            <h2>Result projections</h2>
            <span>{study?.result_projections.length ?? 0}</span>
          </header>
          <div className="run-projection-list">
            {!study?.result_projections.length && (
              <p>No summary projections configured.</p>
            )}
            {study?.result_projections.map((projection) => (
              <div key={projection.id}>
                <strong>{projection.id}</strong>
                <code>{projection.scope}</code>
                <span>
                  {[projection.component_id, projection.port_name]
                    .filter(Boolean)
                    .join('.')}
                </span>
                <span>
                  {projection.value_name} · {projection.dimension}
                </span>
                <small>{projection.aggregation}</small>
              </div>
            ))}
          </div>
        </section>

        <ExecutionHistory
          jobs={jobs}
          selectedJobId={selectedJobId}
          loading={jobsLoading}
          submitting={jobSubmitting}
          stateFilter={jobStateFilter}
          nextCursor={jobsNextCursor}
          submissionReady={submissionReady}
          onSelect={onSelectJob}
          onStateFilter={onJobStateFilter}
          onSubmit={onSubmitJob}
          onRefresh={onRefreshJobs}
          onLoadMore={onLoadMoreJobs}
          onCancel={onCancelJob}
        />
      </div>
    </section>
  )
}
