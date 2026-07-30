import type {
  RunConfigurationRevision,
  SteadySolverSettings,
} from './types'

interface RunConfigurationWorkspaceProps {
  revision?: RunConfigurationRevision
  publishing: boolean
  operationError: string
  operationStatus: string
  onDismissOperation: () => void
  onCreate: () => void
  onRevise: () => void
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
  publishing,
  operationError,
  operationStatus,
  onDismissOperation,
  onCreate,
  onRevise,
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
        <section className="run-provenance-card">
          <header>
            <h2>Revision bindings</h2>
            <span>{revision.mode}</span>
          </header>
          <div>
            <label>
              <span>Topology revision</span>
              <code>{revision.model_revision_id}</code>
            </label>
            <label>
              <span>Case revision</span>
              <code>{revision.case_revision_id}</code>
            </label>
            <label>
              <span>Parent configuration</span>
              <code>{revision.parent_run_configuration_revision_id || '—'}</code>
            </label>
            <label>
              <span>Artifact revisions</span>
              {revision.artifact_revision_ids.length ? (
                revision.artifact_revision_ids.map((id) => (
                  <code key={id}>{id}</code>
                ))
              ) : (
                <code>none</code>
              )}
            </label>
          </div>
        </section>

        <section className="run-detail-card">
          <header>
            <h2>
              {revision.mode === 'steady'
                ? 'Steady nonlinear solver'
                : 'Transient integration'}
            </h2>
          </header>
          {revision.mode === 'steady' ? (
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

        <section className="run-detail-card">
          <header>
            <h2>Result projections</h2>
            <span>{revision.result_projections.length}</span>
          </header>
          <div className="run-projection-list">
            {!revision.result_projections.length && (
              <p>No summary projections configured.</p>
            )}
            {revision.result_projections.map((projection) => (
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
      </div>
    </section>
  )
}
