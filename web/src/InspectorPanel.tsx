import { useDisplayUnits } from './DisplayUnitsContext'
import { displayValue } from './displayUnits'
import { formatResultValue } from './resultPresentation'
import type {
  Catalog,
  AssemblyDefinition,
  ComponentDefinition,
  ConnectionDefinition,
  TopologyDocument,
} from './types'

export type GraphSelection =
  | { type: 'component'; id: string }
  | { type: 'assembly'; id: string }
  | { type: 'connection'; id: string }

interface InspectorPanelProps {
  selection: GraphSelection
  topology: TopologyDocument
  catalog: Catalog
  publishing: boolean
  onEditComponent: (component: ComponentDefinition) => void
  onEditConnection: (connection: ConnectionDefinition) => void
  onRemoveComponent: (component: ComponentDefinition) => void
  onRemoveAssembly: (assembly: AssemblyDefinition) => void
  onUngroupAssembly: (assembly: AssemblyDefinition) => void
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
  onRemoveAssembly,
  onUngroupAssembly,
  onRemoveConnection,
  onClose,
}: InspectorPanelProps) {
  const { profile, unitDimensions } = useDisplayUnits()
  const component =
    selection.type === 'component'
      ? topology.model.components.find((item) => item.id === selection.id)
      : undefined
  const connection =
    selection.type === 'connection'
      ? topology.model.connections.find((item) => item.id === selection.id)
      : undefined
  const assembly =
    selection.type === 'assembly'
      ? (topology.model.assemblies ?? []).find(
          (item) => item.id === selection.id,
        )
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
            (() => {
              const parameter = descriptor?.parameters.find(
                (item) => item.name === name,
              )
              const record =
                value && typeof value === 'object'
                  ? (value as Record<string, unknown>)
                  : undefined
              const valueSi =
                typeof value === 'number'
                  ? value
                  : typeof record?.value_si === 'number'
                    ? record.value_si
                    : typeof record?.value === 'number'
                      ? record.value
                      : undefined
              if (!parameter || valueSi === undefined) {
                return (
                  <DetailRow
                    key={name}
                    label={name}
                    value={JSON.stringify(value)}
                  />
                )
              }
              const displayed = displayValue(
                valueSi,
                parameter.dimension,
                profile,
                unitDimensions,
              )
              return (
                <DetailRow
                  key={name}
                  label={name}
                  value={`${formatResultValue(displayed.value)} ${displayed.unit}`}
                />
              )
            })()
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

  if (assembly) {
    return (
      <div className="inspector-panel">
        <header>
          <div>
            <span className="eyebrow">Assembly instance</span>
            <h2>{assembly.label || assembly.id}</h2>
          </div>
          <button type="button" className="icon-button" onClick={onClose}>×</button>
        </header>
        <section>
          <DetailRow label="ID" value={assembly.id} />
          <DetailRow
            label="Contents"
            value={`${assembly.components.length} components, ${(assembly.assemblies ?? []).length} assemblies`}
          />
          <DetailRow
            label="Public ports"
            value={assembly.ports.map((port) => port.name).join(', ')}
          />
          <DetailRow
            label="Public parameters"
            value={(assembly.parameters ?? []).map((parameter) => parameter.name).join(', ')}
          />
        </section>
        <footer>
          <button
            type="button"
            className="danger-button"
            disabled={publishing}
            onClick={() => onRemoveAssembly(assembly)}
          >
            Remove
          </button>
          <button
            type="button"
            className="primary-button"
            disabled={publishing}
            onClick={() => onUngroupAssembly(assembly)}
          >
            Ungroup for editing
          </button>
        </footer>
      </div>
    )
  }

  return null
}
