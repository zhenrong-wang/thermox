import type {
  Catalog,
  ComponentDefinition,
  ConnectionDefinition,
  TopologyDocument,
} from './types'

export type GraphSelection =
  | { type: 'component'; id: string }
  | { type: 'connection'; id: string }

interface InspectorPanelProps {
  selection: GraphSelection
  topology: TopologyDocument
  catalog: Catalog
  publishing: boolean
  onEditComponent: (component: ComponentDefinition) => void
  onEditConnection: (connection: ConnectionDefinition) => void
  onRemoveComponent: (component: ComponentDefinition) => void
  onRemoveConnection: (connection: ConnectionDefinition) => void
  onClose: () => void
}

function DetailRow({
  label,
  value,
}: {
  label: string
  value: string
}) {
  return (
    <div className="detail-row">
      <span>{label}</span>
      <code>{value || '—'}</code>
    </div>
  )
}

export function InspectorPanel({
  selection,
  topology,
  catalog,
  publishing,
  onEditComponent,
  onEditConnection,
  onRemoveComponent,
  onRemoveConnection,
  onClose,
}: InspectorPanelProps) {
  const component =
    selection.type === 'component'
      ? topology.model.components.find((item) => item.id === selection.id)
      : undefined
  const connection =
    selection.type === 'connection'
      ? topology.model.connections.find((item) => item.id === selection.id)
      : undefined

  if (component) {
    const descriptor = catalog.components.find(
      (item) => item.kind === component.kind,
    )
    return (
      <div className="inspector-panel">
        <header>
          <div>
            <span className="eyebrow">Component instance</span>
            <h2>{component.label || component.id}</h2>
          </div>
          <button type="button" className="icon-button" onClick={onClose}>
            ×
          </button>
        </header>
        <section>
          <DetailRow label="ID" value={component.id} />
          <DetailRow label="Kind" value={component.kind} />
          <DetailRow
            label="Version"
            value={component.version || descriptor?.version || ''}
          />
          <DetailRow
            label="Modes"
            value={[
              descriptor?.supports_steady ? 'steady' : '',
              descriptor?.supports_transient ? 'transient' : '',
            ]
              .filter(Boolean)
              .join(', ')}
          />
        </section>
        <section>
          <h3>Bindings</h3>
          {Object.entries({
            ...component.media,
            ...component.materials,
            ...component.artifacts,
          }).map(([name, value]) => (
            <DetailRow key={name} label={name} value={value} />
          ))}
        </section>
        <section>
          <h3>Parameters</h3>
          {Object.entries(component.parameters ?? {}).map(([name, value]) => (
            <DetailRow
              key={name}
              label={name}
              value={
                typeof value === 'number'
                  ? String(value)
                  : JSON.stringify(value)
              }
            />
          ))}
        </section>
        <footer>
          <button
            type="button"
            className="danger-button"
            disabled={publishing}
            onClick={() => onRemoveComponent(component)}
          >
            Remove
          </button>
          <button
            type="button"
            className="primary-button"
            disabled={publishing || !descriptor}
            onClick={() => onEditComponent(component)}
          >
            Edit instance
          </button>
        </footer>
      </div>
    )
  }

  if (connection) {
    return (
      <div className="inspector-panel">
        <header>
          <div>
            <span className="eyebrow">Connection instance</span>
            <h2>{connection.id}</h2>
          </div>
          <button type="button" className="icon-button" onClick={onClose}>
            ×
          </button>
        </header>
        <section>
          <DetailRow label="Source" value={connection.from} />
          <DetailRow label="Target" value={connection.to} />
          <DetailRow label="Kind" value={connection.kind} />
          <DetailRow
            label="Contract"
            value={connection.contract_version || ''}
          />
        </section>
        <footer>
          <button
            type="button"
            className="danger-button"
            disabled={publishing}
            onClick={() => onRemoveConnection(connection)}
          >
            Remove
          </button>
          <button
            type="button"
            className="primary-button"
            disabled={publishing}
            onClick={() => onEditConnection(connection)}
          >
            Edit endpoints
          </button>
        </footer>
      </div>
    )
  }

  return null
}
