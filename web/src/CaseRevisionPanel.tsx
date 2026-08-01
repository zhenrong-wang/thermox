import type { CaseRevision, StudyRevision } from './types'

interface CaseRevisionPanelProps {
  revisions: CaseRevision[]
  selectedId: string
  publishing: boolean
  studies: StudyRevision[]
  canPublishStudy: boolean
  onSelect: (revisionId: string) => void
  onCreate: () => void
  onPublishStudy: () => void
}

export function CaseRevisionPanel({
  revisions,
  selectedId,
  publishing,
  studies,
  canPublishStudy,
  onSelect,
  onCreate,
  onPublishStudy,
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
      <header className="study-revision-heading">
        <div>
          <span className="eyebrow">Executable intent</span>
          <h2>Studies</h2>
          <p>{studies.length} immutable revisions</p>
        </div>
        <button
          type="button"
          className="resource-button"
          disabled={publishing || !canPublishStudy}
          onClick={onPublishStudy}
          title={
            canPublishStudy
              ? 'Publish the validated revision set as a study'
              : 'Validate the exact topology, case, and artifacts first'
          }
        >
          Publish
        </button>
      </header>
      <div className="case-revision-list">
        {!studies.length && (
          <div className="case-list-empty">
            <strong>No published studies</strong>
            <span>Validate an operating case, then publish its intent.</span>
          </div>
        )}
        {studies.map((revision) => (
          <div className="case-revision-card" key={revision.study_revision_id}>
            <div>
              <strong>{revision.study_id}</strong>
              <span>r{revision.revision_number}</span>
            </div>
            <small>
              {revision.intent} · {revision.result_projections.length} outputs
            </small>
            <code>{revision.checksum.slice(7, 19)}</code>
          </div>
        ))}
      </div>
      <footer>
        <span>Case → durable study</span>
        <code>thermox.study_revision/v1</code>
      </footer>
    </div>
  )
}
