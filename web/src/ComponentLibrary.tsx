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
  artifactRevisionCount: number
  catalogFingerprint: string
  onChoose: (component: CatalogComponent) => void
  onAddFluid: () => void
}

export function ComponentLibrary({
  components,
  disabled,
  fluidCount,
  artifactRevisionCount,
  catalogFingerprint,
  onChoose,
  onAddFluid,
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
          <button
            type="button"
            className="resource-button"
            disabled={disabled}
            onClick={onAddFluid}
          >
            + Fluid
          </button>
        </div>
        <p className="library-instruction">
          Drag a type onto the canvas, or click it, to configure an instance.
        </p>
        <div className="resource-summary">
          <span>{fluidCount} fluids</span>
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
            <button
              type="button"
              className="component-card"
              key={component.kind}
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
