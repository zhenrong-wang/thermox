import { useMemo, useState, type DragEvent } from 'react'
import {
  COMPONENT_DRAG_TYPE,
  componentCategories,
  componentMatchesLibraryFilters,
} from './componentLibrary'
import { ComponentSymbol } from './ComponentSymbol'
import type {
  AssemblyTemplateCatalogEntry,
  CatalogComponent,
} from './types'

interface ComponentLibraryProps {
  components: CatalogComponent[]
  disabled: boolean
  catalogFingerprint: string
  onChoose: (component: CatalogComponent) => void
  onGroupComponents: () => void
  assemblyTemplates: AssemblyTemplateCatalogEntry[]
  onInstantiateAssembly: (template: AssemblyTemplateCatalogEntry) => void
  onCreateTopology?: () => void
  onRevise: (component: CatalogComponent) => void
}

export function ComponentLibrary({
  components,
  disabled,
  catalogFingerprint,
  onChoose,
  onGroupComponents,
  assemblyTemplates,
  onInstantiateAssembly,
  onCreateTopology,
  onRevise,
}: ComponentLibraryProps) {
  const [filter, setFilter] = useState('')
  const [category, setCategory] = useState('all')
  const categories = useMemo(
    () => componentCategories(components),
    [components],
  )
  const visibleComponents = useMemo(
    () =>
      components.filter((component) =>
        componentMatchesLibraryFilters(component, filter, category),
      ),
    [category, components, filter],
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
        <button
          type="button"
          className="library-start-button"
          disabled={disabled}
          onClick={onGroupComponents}
        >
          Group components into assembly
        </button>
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
      <label className="library-category-filter">
        <span>Equipment category</span>
        <select
          value={category}
          onChange={(event) => setCategory(event.target.value)}
          aria-label="Filter component category"
        >
          <option value="all">All categories</option>
          {categories.map((item) => (
            <option key={item} value={item}>{item}</option>
          ))}
        </select>
      </label>
      {assemblyTemplates.length > 0 && (
        <div className="assembly-template-list">
          <span className="eyebrow">Project assemblies</span>
          {assemblyTemplates.map((template) => {
            const assembly = template.definition.model.assemblies?.[0]
            return (
              <button
                type="button"
                className="assembly-template-card"
                disabled={disabled}
                key={template.source.artifact_revision_id}
                onClick={() => onInstantiateAssembly(template)}
              >
                <span>
                  <strong>{assembly?.label || template.source.artifact_id}</strong>
                  <small>revision {template.source.revision_number}</small>
                </span>
                <code>
                  {assembly?.components.length ?? 0} components ·{' '}
                  {assembly?.ports.length ?? 0} ports
                </code>
              </button>
            )
          })}
        </div>
      )}
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
                      <ComponentSymbol
                        component={component}
                      />
                      <span className="kind-family">{component.model_name}</span>
                      {component.supports_transient && <span className="transient-badge">transient</span>}
                      {component.supported_modes.length > 0 && <span className="transient-badge">modes: {component.supported_modes.join(' / ')}</span>}
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
