import { useEffect, useMemo, useState } from 'react'
import { api, errorMessage, isAbortError } from './api'
import { TopologyCanvas } from './TopologyCanvas'
import type {
  Catalog,
  ModelRevision,
  Project,
  TopologyDocument,
} from './types'

function shortKind(kind: string): string {
  const parts = kind.split('.')
  return parts.length > 2 ? `${parts[0]}.${parts[1]}` : kind
}

function App() {
  const [catalog, setCatalog] = useState<Catalog>()
  const [projects, setProjects] = useState<Project[]>([])
  const [selectedProjectId, setSelectedProjectId] = useState('')
  const [revisions, setRevisions] = useState<ModelRevision[]>([])
  const [selectedRevisionId, setSelectedRevisionId] = useState('')
  const [topology, setTopology] = useState<TopologyDocument>()
  const [filter, setFilter] = useState('')
  const [error, setError] = useState('')
  const [loading, setLoading] = useState(true)

  useEffect(() => {
    const controller = new AbortController()
    Promise.all([
      api.catalog(controller.signal),
      api.projects(controller.signal),
    ])
      .then(([catalogResponse, projectResponse]) => {
        setError('')
        setCatalog(catalogResponse)
        setProjects(projectResponse.projects)
        setSelectedProjectId(projectResponse.projects[0]?.project_id ?? '')
      })
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setError(errorMessage(reason))
      })
      .finally(() => setLoading(false))
    return () => controller.abort()
  }, [])

  useEffect(() => {
    setRevisions([])
    setSelectedRevisionId('')
    setTopology(undefined)
    if (!selectedProjectId) return
    const controller = new AbortController()
    api
      .modelRevisions(selectedProjectId, controller.signal)
      .then((response) => {
        setError('')
        const ordered = [...response.model_revisions].sort(
          (left, right) => right.revision_number - left.revision_number,
        )
        setRevisions(ordered)
        setSelectedRevisionId(ordered[0]?.model_revision_id ?? '')
      })
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setError(errorMessage(reason))
      })
    return () => controller.abort()
  }, [selectedProjectId])

  useEffect(() => {
    setTopology(undefined)
    if (!selectedProjectId || !selectedRevisionId) return
    const controller = new AbortController()
    api
      .modelRevision(
        selectedProjectId,
        selectedRevisionId,
        controller.signal,
      )
      .then((response) => {
        setError('')
        setTopology(response.model)
      })
      .catch((reason: unknown) => {
        if (!isAbortError(reason)) setError(errorMessage(reason))
      })
    return () => controller.abort()
  }, [selectedProjectId, selectedRevisionId])

  const selectedProject = projects.find(
    (project) => project.project_id === selectedProjectId,
  )
  const selectedRevision = revisions.find(
    (revision) => revision.model_revision_id === selectedRevisionId,
  )
  const palette = useMemo(() => {
    const query = filter.trim().toLowerCase()
    return (catalog?.components ?? []).filter(
      (component) =>
        !query ||
        component.kind.toLowerCase().includes(query) ||
        component.ports.some((port) =>
          port.domain.toLowerCase().includes(query),
        ),
    )
  }, [catalog, filter])

  return (
    <main className="app-shell">
      <header className="topbar">
        <div className="brand">
          <div className="brand-mark" aria-hidden="true">
            <span />
            <span />
            <span />
          </div>
          <div>
            <strong>thermox</strong>
            <small>system workspace</small>
          </div>
        </div>
        <div className="context">
          <span className="context-label">Project</span>
          <select
            aria-label="Project"
            value={selectedProjectId}
            onChange={(event) => setSelectedProjectId(event.target.value)}
          >
            {projects.map((project) => (
              <option key={project.project_id} value={project.project_id}>
                {project.name}
              </option>
            ))}
          </select>
          <span className="context-divider">/</span>
          <span className="context-label">Revision</span>
          <select
            aria-label="Topology revision"
            value={selectedRevisionId}
            onChange={(event) => setSelectedRevisionId(event.target.value)}
            disabled={!revisions.length}
          >
            {revisions.map((revision) => (
              <option
                key={revision.model_revision_id}
                value={revision.model_revision_id}
              >
                r{revision.revision_number} · {revision.model_revision_label}
              </option>
            ))}
          </select>
        </div>
        <div className="runtime-status">
          <span className={error ? 'status-dot error' : 'status-dot'} />
          {error ? 'API unavailable' : loading ? 'Connecting' : 'Local runtime'}
        </div>
      </header>

      <section className="workspace">
        <aside className="project-rail">
          <div className="rail-heading">
            <span>Workspace</span>
            <button title="Refresh browser data" onClick={() => location.reload()}>
              ↻
            </button>
          </div>
          <nav>
            <button className="nav-item active">
              <span className="nav-icon">⌘</span>
              Topology
            </button>
            <button className="nav-item" disabled>
              <span className="nav-icon">◇</span>
              Cases
            </button>
            <button className="nav-item" disabled>
              <span className="nav-icon">▶</span>
              Runs
            </button>
            <button className="nav-item" disabled>
              <span className="nav-icon">▥</span>
              Results
            </button>
          </nav>
          <div className="project-summary">
            <span>Current project</span>
            <strong>{selectedProject?.name ?? 'No project'}</strong>
            <p>{selectedProject?.description || 'No description provided.'}</p>
          </div>
          <div className="model-stats">
            <div>
              <strong>{topology?.model.components.length ?? 0}</strong>
              <span>components</span>
            </div>
            <div>
              <strong>{topology?.model.connections.length ?? 0}</strong>
              <span>connections</span>
            </div>
          </div>
        </aside>

        <section className="canvas-panel">
          <div className="canvas-toolbar">
            <div>
              <span className="eyebrow">Immutable topology</span>
              <h1>{topology?.model.name ?? 'Thermal system graph'}</h1>
            </div>
            <div className="revision-chip">
              <span>SHA-256</span>
              <code>{selectedRevision?.checksum.slice(7, 19) ?? '—'}</code>
            </div>
          </div>
          {error ? (
            <div className="error-state">
              <strong>Could not load the Thermox API</strong>
              <p>{error}</p>
              <code>npm run dev</code>
              <span>expects the API at http://127.0.0.1:8080</span>
            </div>
          ) : (
            <TopologyCanvas
              topology={topology}
              catalog={catalog?.components ?? []}
            />
          )}
        </section>

        <aside className="palette">
          <div className="palette-heading">
            <span className="eyebrow">Runtime catalog</span>
            <h2>Components</h2>
            <p>{catalog?.components.length ?? 0} registered types</p>
          </div>
          <label className="search">
            <span>⌕</span>
            <input
              value={filter}
              onChange={(event) => setFilter(event.target.value)}
              placeholder="Filter type or domain"
            />
          </label>
          <div className="component-list">
            {palette.map((component) => (
              <article className="component-card" key={component.kind}>
                <div>
                  <span className="kind-family">{shortKind(component.kind)}</span>
                  {component.supports_transient && (
                    <span className="transient-badge">transient</span>
                  )}
                </div>
                <strong>{component.kind.split('.').at(-1)}</strong>
                <div className="port-summary">
                  {component.ports.map((port) => (
                    <span key={`${port.name}-${port.domain}`}>
                      <i className={`port-${port.domain}`} />
                      {port.name}
                    </span>
                  ))}
                </div>
              </article>
            ))}
          </div>
          <footer>
            <span>Catalog</span>
            <code>{catalog?.fingerprint.slice(0, 18) ?? 'loading…'}</code>
          </footer>
        </aside>
      </section>
    </main>
  )
}

export default App
