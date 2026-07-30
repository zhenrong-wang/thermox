import type { CaseRevision } from './types'

interface CaseRevisionPanelProps {
  revisions: CaseRevision[]
  selectedId: string
  publishing: boolean
  onSelect: (revisionId: string) => void
  onCreate: () => void
}

export function CaseRevisionPanel({
  revisions,
  selectedId,
  publishing,
  onSelect,
  onCreate,
}: CaseRevisionPanelProps) {
  return (
    <div className="case-revision-panel">
      <header>
        <div>
          <span className="eyebrow">Exact topology binding</span>
          <h2>Case revisions</h2>
          <p>{revisions.length} immutable revisions</p>
        </div>
        <button
          type="button"
          className="resource-button"
          disabled={publishing}
          onClick={onCreate}
        >
          + Case
        </button>
      </header>
      <div className="case-revision-list">
        {!revisions.length && (
          <div className="case-list-empty">
            <strong>No operating cases</strong>
            <span>Create one for this exact topology revision.</span>
          </div>
        )}
        {revisions.map((revision) => (
          <button
            type="button"
            key={revision.case_revision_id}
            className={
              revision.case_revision_id === selectedId
                ? 'case-revision-card selected'
                : 'case-revision-card'
            }
            onClick={() => onSelect(revision.case_revision_id)}
          >
            <div>
              <strong>{revision.case_id}</strong>
              <span>r{revision.revision_number}</span>
            </div>
            <small>{revision.mode}</small>
            <code>{revision.checksum.slice(7, 19)}</code>
          </button>
        ))}
      </div>
      <footer>
        <span>Topology-scoped</span>
        <code>thermox.case/v1</code>
      </footer>
    </div>
  )
}
