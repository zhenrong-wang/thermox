import { definitionIssues } from './definitionReadiness'
import type {
  Catalog,
  CaseRevision,
  TopologyDocument,
} from './types'

interface DefinitionOverviewProps {
  topology?: TopologyDocument
  catalog?: Catalog
  caseRevision?: CaseRevision
  requiredArtifactCount: number
  unresolvedArtifactCount: number
  compiled: boolean
  onInspectComponent: (componentId: string) => void
}

export function DefinitionOverview({
  topology,
  catalog,
  caseRevision,
  requiredArtifactCount,
  unresolvedArtifactCount,
  compiled,
  onInspectComponent,
}: DefinitionOverviewProps) {
  const issues = definitionIssues(topology, catalog)
  const inputsLoaded = Boolean(topology && catalog)
  const inputsComplete = inputsLoaded && issues.length === 0
  const simulationCase = caseRevision?.case_document?.case
  const fixedValueCount = Object.keys(simulationCase?.fixed_values ?? {}).length
  const overrideCount = Object.keys(
    simulationCase?.parameter_overrides ?? {},
  ).length

  return (
    <section className="definition-overview">
      <header>
        <div>
          <span className="section-kicker">Study compiler</span>
          <h2>Study readiness</h2>
          <p>
            This operating scenario depends on the selected physical-system
            and artifact revisions. The compiler below remains authoritative.
          </p>
        </div>
        <span
          className={
            inputsLoaded && issues.length
              ? 'definition-count warning'
              : 'definition-count'
          }
        >
          {!inputsLoaded
            ? 'Checking inputs'
            : issues.length
              ? `${issues.length} to resolve`
              : 'Inputs complete'}
        </span>
      </header>
      <div className="definition-summary-grid">
        <div>
          <span>Equipment</span>
          <strong>{topology?.model.components.length ?? 0}</strong>
          <small>
            {!inputsLoaded
              ? 'loading physical inputs'
              : inputsComplete
                ? 'physical inputs present'
                : 'physical dependencies blocked'}
          </small>
        </div>
        <div>
          <span>Media</span>
          <strong>
            {(topology?.model.media.length ?? 0) +
              (topology?.model.materials?.length ?? 0)}
          </strong>
          <small>fluids and reacting mixtures registered</small>
        </div>
        <div>
          <span>Case inputs</span>
          <strong>{fixedValueCount + overrideCount}</strong>
          <small>{fixedValueCount} fixed · {overrideCount} overrides</small>
        </div>
        <div>
          <span>Artifacts</span>
          <strong>{requiredArtifactCount - unresolvedArtifactCount}/{requiredArtifactCount}</strong>
          <small>exact revisions resolved</small>
        </div>
        <div>
          <span>Compiler</span>
          <strong className={compiled ? 'definition-compiled' : ''}>
            {compiled ? 'Passed' : 'Pending'}
          </strong>
          <small>service authority</small>
        </div>
      </div>
      {issues.length > 0 && (
        <div className="definition-issue-list">
          {issues.map((issue) => (
            <button
              type="button"
              key={issue.id}
              onClick={() => onInspectComponent(issue.componentId)}
            >
              <span>{issue.kind}</span>
              <strong>{issue.componentId}</strong>
              <p>{issue.message}</p>
              <em>Open on canvas →</em>
            </button>
          ))}
        </div>
      )}
    </section>
  )
}
