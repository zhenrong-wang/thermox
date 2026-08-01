import type { RunConfigurationRevision } from './types'

interface RunConfigurationPanelProps {
  revisions: RunConfigurationRevision[]
  selectedId: string
  publishing: boolean
  onSelect: (revisionId: string) => void
  onCreate: () => void
  onRevise: () => void
}

export function RunConfigurationPanel({
  revisions,
  selectedId,
  publishing,
  onSelect,
  onCreate,
  onRevise,
}: RunConfigurationPanelProps) {
  return (
    <div className="case-revision-panel run-revision-panel">
      <header>
        <div>
          <span className="eyebrow">Reusable execution intent</span>
          <h2>Run configurations</h2>
          <p>{revisions.length} immutable revisions</p>
        </div>
        <button
          type="button"
          className="resource-button"
          disabled={publishing}
          onClick={onCreate}
        >
          + Config
        </button>
      </header>
      <div className="case-revision-list">
        {!revisions.length && (
          <div className="case-list-empty">
            <strong>No run configurations</strong>
            <span>Pin a case, artifacts, solver policy, and outputs.</span>
          </div>
        )}
        {revisions.map((revision) => (
          <button
            type="button"
            key={revision.run_configuration_revision_id}
            className={
              revision.run_configuration_revision_id === selectedId
                ? 'case-revision-card selected'
                : 'case-revision-card'
            }
            onClick={() => onSelect(revision.run_configuration_revision_id)}
          >
            <div>
              <strong>{revision.run_configuration_id}</strong>
              <span>r{revision.revision_number}</span>
            </div>
            <small>
              Study {revision.study_revision_id}
            </small>
            <code>{revision.checksum.slice(7, 19)}</code>
          </button>
        ))}
      </div>
      <footer>
        <span>Execution-scoped</span>
        {selectedId ? (
          <button type="button" onClick={onRevise} disabled={publishing}>
            Revise selected
          </button>
        ) : (
          <code>thermox.run/v3</code>
        )}
      </footer>
    </div>
  )
}
