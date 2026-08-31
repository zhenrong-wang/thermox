import type {
  SystemReadinessIssue,
  SystemReadinessViewModel,
} from './systemReadiness'

interface SystemReadinessPanelProps {
  readiness: SystemReadinessViewModel
  onInspect: (issue: SystemReadinessIssue) => void
  onValidate: () => void
  onClose: () => void
}

const statusCopy: Record<SystemReadinessViewModel['status'], string> = {
  calculatable: 'Calculatable',
  blocked: 'Compiler blocked',
  not_validated: 'Validation required',
}

export function SystemReadinessPanel({
  readiness,
  onInspect,
  onValidate,
  onClose,
}: SystemReadinessPanelProps) {
  return (
    <div className="system-readiness-panel">
      <header>
        <div>
          <span className="eyebrow">Exact revision readiness</span>
          <h2>{statusCopy[readiness.status]}</h2>
        </div>
        <button type="button" className="icon-button" onClick={onClose}>
          ×
        </button>
      </header>
      <p className="readiness-authority-note">
        Local checks guide authoring. Only service compilation of the exact
        topology, case, and artifact revisions establishes calculatability.
      </p>
      <div className="system-readiness-layers">
        {readiness.layers.map((layer) => (
          <article key={layer.id} className={layer.state}>
            <div>
              <strong>{layer.label}</strong>
              <span>{layer.authority === 'service' ? 'Service' : 'Local hint'}</span>
            </div>
            <p>{layer.detail}</p>
          </article>
        ))}
      </div>
      <section className="system-readiness-issues">
        <header>
          <h3>Actionable issues</h3>
          <span>{readiness.issues.length}</span>
        </header>
        {readiness.issues.map((issue) => (
          <button
            type="button"
            key={issue.id}
            className={`system-readiness-issue ${issue.severity}`}
            onClick={() => onInspect(issue)}
          >
            <span>{issue.layer}</span>
            <strong>{issue.message}</strong>
            {issue.suggestions[0] && <small>{issue.suggestions[0]}</small>}
            <em>{issue.authority === 'service' ? 'Inspect diagnostic' : 'Resolve input'} →</em>
          </button>
        ))}
        {!readiness.issues.length && (
          <div className="system-readiness-empty">
            <strong>No unresolved diagnostics</strong>
            <p>The exact revision set is ready for calculation.</p>
          </div>
        )}
      </section>
      {readiness.status !== 'calculatable' && (
        <footer>
          <button type="button" className="primary-button" onClick={onValidate}>
            Review study and compile
          </button>
        </footer>
      )}
    </div>
  )
}
