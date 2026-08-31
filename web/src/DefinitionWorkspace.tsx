import type { ComponentDefinitionReadiness } from './definitionReadiness'
import type {
  ArtifactRevision,
  Catalog,
  ComponentDefinition,
  TopologyDocument,
} from './types'

interface DefinitionWorkspaceProps {
  topology?: TopologyDocument
  catalog?: Catalog
  readiness: Record<string, ComponentDefinitionReadiness>
  artifactRevisions: ArtifactRevision[]
  publishing: boolean
  loadingArtifactRevision: boolean
  operationError: string
  operationStatus: string
  onDismissOperation: () => void
  onEditComponent: (component: ComponentDefinition) => void
  onAddFluid: () => void
  onAddMaterial: () => void
  onDefineComponent: () => void
  onAddCorrelation: () => void
  onReviseCorrelation: (revision: ArtifactRevision) => void
  onAddPerformanceMap: () => void
  onRevisePerformanceMap: (revision: ArtifactRevision) => void
  onBuild: () => void
}

export function DefinitionWorkspace({
  topology,
  catalog,
  readiness,
  artifactRevisions,
  publishing,
  loadingArtifactRevision,
  operationError,
  operationStatus,
  onDismissOperation,
  onEditComponent,
  onAddFluid,
  onAddMaterial,
  onDefineComponent,
  onAddCorrelation,
  onReviseCorrelation,
  onAddPerformanceMap,
  onRevisePerformanceMap,
  onBuild,
}: DefinitionWorkspaceProps) {
  if (!topology || !catalog) {
    return (
      <section className="definition-workspace">
        <div className="case-empty">
          <div className="empty-orbit" />
          <h2>No physical system selected</h2>
          <p>Build a topology before defining its engineering inputs.</p>
          <button type="button" className="primary-button" onClick={onBuild}>
            Go to Build
          </button>
        </div>
      </section>
    )
  }

  const states = Object.values(readiness)
  const definedCount = states.filter((item) => item.state === 'defined').length
  const issueCount = states.reduce((sum, item) => sum + item.issues.length, 0)
  const busy = publishing || loadingArtifactRevision
  const latestCorrelations = [...artifactRevisions]
    .filter((revision) => revision.artifact_type === 'thermox.correlation')
    .sort((left, right) => right.revision_number - left.revision_number)
    .filter(
      (revision, index, revisions) =>
        revisions.findIndex(
          (candidate) => candidate.artifact_id === revision.artifact_id,
        ) === index,
    )
  const latestPerformanceMaps = [...artifactRevisions]
    .filter((revision) => revision.artifact_type === 'thermox.performance_map')
    .sort((left, right) => right.revision_number - left.revision_number)
    .filter(
      (revision, index, revisions) =>
        revisions.findIndex(
          (candidate) => candidate.artifact_id === revision.artifact_id,
        ) === index,
    )

  return (
    <section className="definition-workspace">
      <div className="case-toolbar">
        <div>
          <span className="eyebrow">Physical asset definition</span>
          <h1>{topology.model.name}</h1>
        </div>
        <div className="definition-toolbar-actions">
          <button
            type="button"
            className="secondary-button"
            disabled={busy}
            onClick={onAddFluid}
          >
            + Fluid
          </button>
          <button
            type="button"
            className="secondary-button"
            disabled={busy}
            onClick={onAddMaterial}
          >
            + Reacting mixture
          </button>
          <button
            type="button"
            className="secondary-button"
            disabled={busy}
            onClick={onDefineComponent}
          >
            + Component model
          </button>
          <button
            type="button"
            className="secondary-button"
            disabled={busy}
            onClick={onAddCorrelation}
          >
            + Correlation
          </button>
          <button
            type="button"
            className="secondary-button"
            disabled={busy}
            onClick={onAddPerformanceMap}
          >
            + Performance map
          </button>
        </div>
      </div>

      {(operationError || operationStatus || busy) && (
        <div
          className={`operation-banner${operationError ? ' is-error' : ''}`}
        >
          {loadingArtifactRevision
            ? 'Loading and verifying immutable artifact payload…'
            : publishing
              ? 'Publishing immutable physical-system revision…'
              : operationError || operationStatus}
          <button type="button" onClick={onDismissOperation}>×</button>
        </div>
      )}

      <div className="definition-workspace-scroll">
        <section className="physical-summary-grid">
          <div>
            <span>Components</span>
            <strong>{definedCount}/{topology.model.components.length}</strong>
            <small>locally defined</small>
          </div>
          <div>
            <span>Fluids</span>
            <strong>{topology.model.media.length}</strong>
            <small>property-backed definitions</small>
          </div>
          <div>
            <span>Reacting mixtures</span>
            <strong>{topology.model.materials?.length ?? 0}</strong>
            <small>thermochemistry definitions</small>
          </div>
          <div>
            <span>Engineering data</span>
            <strong>{artifactRevisions.length}</strong>
            <small>immutable artifact revisions</small>
          </div>
          <div>
            <span>Local issues</span>
            <strong>{issueCount}</strong>
            <small>authoring hints before compilation</small>
          </div>
        </section>

        <section className="physical-component-section">
          <header>
            <div>
              <span className="section-kicker">Equipment instances</span>
              <h2>Component definitions</h2>
            </div>
            <p>
              Complete the physical inputs required by each selected component
              model. System calculatability is established later by the study
              compiler.
            </p>
          </header>
          <div className="physical-component-list">
            {topology.model.components.map((component) => {
              const state = readiness[component.id] ?? {
                state: 'draft' as const,
                issues: [],
              }
              const descriptor = catalog.components.find(
                (item) => item.kind === component.kind,
              )
              return (
                <article key={component.id}>
                  <div>
                    <span className={`physical-state ${state.state}`}>
                      {state.state}
                    </span>
                    <strong>{component.label || component.id}</strong>
                    <code>{component.kind}</code>
                  </div>
                  <div className="physical-requirements">
                    <span>{descriptor?.parameters.length ?? 0} parameters</span>
                    <span>{descriptor?.artifacts.length ?? 0} artifact roles</span>
                    <span>
                      {descriptor?.ports.filter(
                        (port) =>
                          port.domain === 'fluid' || port.domain === 'material',
                      ).length ?? 0}{' '}
                      medium bindings
                    </span>
                  </div>
                  <div className="physical-issue-copy">
                    {state.issues.length ? (
                      <>
                        <strong>{state.issues[0].message}</strong>
                        {state.issues.length > 1 && (
                          <span>+{state.issues.length - 1} more requirements</span>
                        )}
                      </>
                    ) : (
                      <span>Local catalog requirements are complete.</span>
                    )}
                  </div>
                  <button
                    type="button"
                    className="primary-button"
                    disabled={busy || !descriptor}
                    onClick={() => onEditComponent(component)}
                  >
                    {state.state === 'defined' ? 'Review definition' : 'Define'}
                  </button>
                </article>
              )
            })}
            {!topology.model.components.length && (
              <div className="definition-list-empty">
                <strong>No component instances</strong>
                <p>Build the equipment topology before defining physics.</p>
                <button type="button" onClick={onBuild}>Go to Build</button>
              </div>
            )}
          </div>
        </section>

        <section className="physical-component-section engineering-data-section">
          <header>
            <div>
              <span className="section-kicker">Engineering data registry</span>
              <h2>Correlation artifacts</h2>
            </div>
            <p>
              Correlations are immutable typed datasets. Component instances bind
              them through declared roles; they are not components, fluids, or materials.
            </p>
          </header>
          <div className="engineering-artifact-list">
            {latestCorrelations.map((revision) => (
              <article key={revision.artifact_revision_id}>
                <div>
                  <strong>{revision.artifact_id}</strong>
                  <code>{revision.artifact_schema_version}</code>
                </div>
                <span>r{revision.revision_number}</span>
                <small>{revision.content.checksum}</small>
                <button
                  type="button"
                  className="secondary-button"
                  disabled={busy}
                  onClick={() => onReviseCorrelation(revision)}
                >
                  Revise
                </button>
              </article>
            ))}
            {!latestCorrelations.length && (
              <div className="definition-list-empty">
                <strong>No project correlations</strong>
                <p>Publish a typed equation, then bind it from a compatible component model.</p>
                <button type="button" onClick={onAddCorrelation}>Publish correlation</button>
              </div>
            )}
          </div>
        </section>

        <section className="physical-component-section engineering-data-section">
          <header>
            <div>
              <span className="section-kicker">Engineering data registry</span>
              <h2>Performance maps</h2>
            </div>
            <p>
              Generic typed characteristic surfaces consumed through component artifact roles.
              Compressor and turbine semantics remain in their calculation models.
            </p>
          </header>
          <div className="engineering-artifact-list">
            {latestPerformanceMaps.map((revision) => (
              <article key={revision.artifact_revision_id}>
                <div>
                  <strong>{revision.artifact_id}</strong>
                  <code>{revision.artifact_schema_version}</code>
                </div>
                <span>r{revision.revision_number}</span>
                <small>{revision.content.checksum}</small>
                {revision.artifact_schema_version === 'thermox.performance_map/v1' ? (
                  <button
                    type="button"
                    className="secondary-button"
                    disabled={busy}
                    onClick={() => onRevisePerformanceMap(revision)}
                  >
                    Revise
                  </button>
                ) : (
                  <span className="artifact-readonly-label">conditioned map</span>
                )}
              </article>
            ))}
            {!latestPerformanceMaps.length && (
              <div className="definition-list-empty">
                <strong>No project performance maps</strong>
                <p>Publish typed family curves, then bind the map from a compatible component.</p>
                <button type="button" onClick={onAddPerformanceMap}>Publish map</button>
              </div>
            )}
          </div>
        </section>
      </div>
    </section>
  )
}

interface DefinitionSidebarProps {
  topology?: TopologyDocument
  readiness: Record<string, ComponentDefinitionReadiness>
  onSelectComponent: (component: ComponentDefinition) => void
}

export function DefinitionSidebar({
  topology,
  readiness,
  onSelectComponent,
}: DefinitionSidebarProps) {
  return (
    <div className="definition-sidebar">
      <header>
        <span className="eyebrow">Definition scope</span>
        <h2>Physical system</h2>
        <p>Reusable asset data, not operating-case boundaries.</p>
      </header>
      <div className="definition-sidebar-list">
        {(topology?.model.components ?? []).map((component) => {
          const state = readiness[component.id]?.state ?? 'draft'
          return (
            <button
              type="button"
              key={component.id}
              onClick={() => onSelectComponent(component)}
            >
              <span className={`physical-state ${state}`}>{state}</span>
              <strong>{component.label || component.id}</strong>
              <code>{component.kind}</code>
            </button>
          )
        })}
      </div>
      <footer>
        <span>Component readiness</span>
        <code>authoring hints</code>
      </footer>
    </div>
  )
}
