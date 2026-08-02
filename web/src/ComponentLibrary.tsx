import { useMemo, useState, type DragEvent } from 'react'
import {
  COMPONENT_DRAG_TYPE,
  componentMatchesFilter,
} from './componentLibrary'
import type { CatalogComponent } from './types'

interface ComponentLibraryProps {
  components: CatalogComponent[]
  disabled: boolean
  fluidCount: number
  materialCount: number
  artifactRevisionCount: number
  catalogFingerprint: string
  onChoose: (component: CatalogComponent) => void
  onAddFluid: () => void
  onAddMaterial: () => void
  onCreateTopology?: () => void
  onDefine: () => void
  onRevise: (component: CatalogComponent) => void
}

export function ComponentLibrary({
  components,
  disabled,
  fluidCount,
  materialCount,
  artifactRevisionCount,
  catalogFingerprint,
  onChoose,
  onAddFluid,
  onAddMaterial,
  onCreateTopology,
  onDefine,
  onRevise,
}: ComponentLibraryProps) {
  const [filter, setFilter] = useState('')
  const visibleComponents = useMemo(
    () =>
      components.filter((component) =>
        componentMatchesFilter(component, filter),
      ),
    [components, filter],
  )
  const templates = useMemo(() => {
    const grouped = new Map<string, CatalogComponent[]>()
    for (const component of visibleComponents) {
      const variants = grouped.get(component.template_kind) ?? []
      variants.push(component)
      grouped.set(component.template_kind, variants)
    }
    return [...grouped.entries()]
      .map(([templateKind, variants]) => ({
        templateKind,
        displayName: variants[0].display_name,
        category: variants[0].category,
        variants: variants.sort((left, right) =>
          left.model_name.localeCompare(right.model_name),
        ),
      }))
      .sort(
        (left, right) =>
          left.category.localeCompare(right.category) ||
          left.displayName.localeCompare(right.displayName),
      )
  }, [visibleComponents])

  function beginDrag(
    event: DragEvent<HTMLButtonElement>,
    component: CatalogComponent,
  ) {
    event.dataTransfer.effectAllowed = 'copy'
    event.dataTransfer.setData(COMPONENT_DRAG_TYPE, component.kind)
    event.dataTransfer.setData('text/plain', component.kind)
  }

  return (
    <div className="component-library">
      <div className="palette-heading">
        <span className="eyebrow">Physical topology</span>
        <div className="palette-title-row">
          <div>
            <h2>Components</h2>
            <p>
              {new Set(components.map((item) => item.template_kind)).size}{' '}
              physical templates · {components.length} calculation models
            </p>
          </div>
        </div>
        <p className="library-instruction">
          {onCreateTopology
            ? 'Create the first topology revision to activate this library.'
            : 'Choose a physical template and calculation model, then drag it onto the canvas.'}
        </p>
        {onCreateTopology && (
          <button
            type="button"
            className="library-start-button"
            onClick={onCreateTopology}
          >
            Create topology
          </button>
        )}
        <div className="definition-resources">
          <span className="eyebrow">Definition resources</span>
          <div className="resource-summary">
            <span>{fluidCount} fluids</span>
            <span>{materialCount} reacting mixtures</span>
            <span>{artifactRevisionCount} models &amp; data</span>
          </div>
          <div className="resource-actions">
            <button type="button" className="resource-button" disabled={disabled} onClick={onAddFluid}>
              + Fluid
            </button>
            <button type="button" className="resource-button" disabled={disabled} onClick={onAddMaterial}>
              + Reacting mixture
            </button>
            <button type="button" className="resource-button" disabled={disabled} onClick={onDefine}>
              + Custom component
            </button>
          </div>
        </div>
      </div>
      <label className="search">
        <span>⌕</span>
        <input
          value={filter}
          onChange={(event) => setFilter(event.target.value)}
          placeholder="Filter equipment, model, port, or domain"
          aria-label="Filter component library"
        />
      </label>
      <div className="component-list">
        {templates.length === 0 ? (
          <p className="library-empty">No registered types match this filter.</p>
        ) : (
          templates.map((template) => (
            <div className="component-template" key={template.templateKind}>
              <header>
                <span>{template.category}</span>
                <strong>{template.displayName}</strong>
                <code>{template.templateKind}</code>
              </header>
              {template.variants.map((component) => (
                <div className="component-card-shell" key={component.kind}>
                  <button
                    type="button"
                    className="component-card"
                    disabled={disabled}
                    draggable={!disabled}
                    onDragStart={(event) => beginDrag(event, component)}
                    onClick={() => onChoose(component)}
                    title={`Drag ${component.display_name} using ${component.model_name}`}
                  >
                    <div>
                      <span className="kind-family">{component.model_name}</span>
                      {component.supports_transient && <span className="transient-badge">transient</span>}
                      {component.source_artifact_revision_id && <span className="project-badge">project</span>}
                    </div>
                    <div className="port-summary">
                      {component.ports.map((port) => (
                        <span key={`${port.name}-${port.domain}`}>
                          <i className={`port-${port.domain}`} />
                          {port.name}
                        </span>
                      ))}
                    </div>
                  </button>
                  {component.source_artifact_revision_id && (
                    <button type="button" className="component-revise-button" disabled={disabled} onClick={() => onRevise(component)}>
                      Revise
                    </button>
                  )}
                </div>
              ))}
            </div>
          ))
        )}
      </div>
      <footer>
        <span>Catalog</span>
        <code>{catalogFingerprint.slice(0, 18) || 'loading…'}</code>
      </footer>
    </div>
  )
}
