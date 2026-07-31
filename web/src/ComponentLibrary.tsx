import { useMemo, useState, type DragEvent } from 'react'
import {
  COMPONENT_DRAG_TYPE,
  componentDisplayName,
  componentFamily,
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
        <span className="eyebrow">Runtime catalog</span>
        <div className="palette-title-row">
          <div>
            <h2>Component library</h2>
            <p>{components.length} registered types</p>
          </div>
          <div className="resource-actions">
            <button
              type="button"
              className="resource-button"
              disabled={disabled}
              onClick={onDefine}
            >
              + Component
            </button>
            <button
              type="button"
              className="resource-button"
              disabled={disabled}
              onClick={onAddFluid}
            >
              + Fluid
            </button>
            <button
              type="button"
              className="resource-button"
              disabled={disabled}
              onClick={onAddMaterial}
            >
              + Material
            </button>
          </div>
        </div>
        <p className="library-instruction">
          Drag a type onto the canvas, or click it, to configure an instance.
        </p>
        <div className="resource-summary">
          <span>{fluidCount} fluids</span>
          <span>{materialCount} materials</span>
          <span>{artifactRevisionCount} artifact revisions</span>
        </div>
      </div>
      <label className="search">
        <span>⌕</span>
        <input
          value={filter}
          onChange={(event) => setFilter(event.target.value)}
          placeholder="Filter type, role, port, or domain"
          aria-label="Filter component library"
        />
      </label>
      <div className="component-list">
        {visibleComponents.length === 0 ? (
          <p className="library-empty">No registered types match this filter.</p>
        ) : (
          visibleComponents.map((component) => (
            <div className="component-card-shell" key={component.kind}>
              <button
                type="button"
                className="component-card"
                disabled={disabled}
                draggable={!disabled}
                onDragStart={(event) => beginDrag(event, component)}
                onClick={() => onChoose(component)}
                title={`Drag ${component.kind} onto the canvas`}
              >
                <div>
                  <span className="kind-family">
                    {componentFamily(component.kind)}
                  </span>
                  {component.supports_transient && (
                    <span className="transient-badge">transient</span>
                  )}
                  {component.source_artifact_revision_id && (
                    <span className="project-badge">project</span>
                  )}
                </div>
                <strong>{componentDisplayName(component.kind)}</strong>
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
                <button
                  type="button"
                  className="component-revise-button"
                  disabled={disabled}
                  onClick={() => onRevise(component)}
                >
                  Revise
                </button>
              )}
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
