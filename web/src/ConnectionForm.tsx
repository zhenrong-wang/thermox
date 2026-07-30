import { useMemo, useState, type FormEvent } from 'react'
import type { ConnectionIntent } from './graphAuthoring'
import type {
  Catalog,
  ConnectionDefinition,
  TopologyDocument,
} from './types'

interface ConnectionFormProps {
  connection: ConnectionDefinition
  topology: TopologyDocument
  catalog: Catalog
  onCancel: () => void
  onSubmit: (intent: ConnectionIntent) => Promise<void>
}

function splitEndpoint(value: string): [string, string] {
  const separator = value.lastIndexOf('.')
  return [value.slice(0, separator), value.slice(separator + 1)]
}

export function ConnectionForm({
  connection,
  topology,
  catalog,
  onCancel,
  onSubmit,
}: ConnectionFormProps) {
  const [source, setSource] = useState(connection.from)
  const [target, setTarget] = useState(connection.to)
  const [submitting, setSubmitting] = useState(false)
  const [formError, setFormError] = useState('')

  const endpoints = useMemo(() => {
    const componentTypes = new Map(
      catalog.components.map((item) => [item.kind, item]),
    )
    return topology.model.components.flatMap((component) => {
      const descriptor = componentTypes.get(component.kind)
      return (descriptor?.ports ?? []).map((port) => ({
        value: `${component.id}.${port.name}`,
        componentId: component.id,
        portName: port.name,
        domain: port.domain,
        direction: port.direction,
      }))
    })
  }, [catalog, topology])
  const sources = endpoints.filter(
    (item) =>
      item.direction === 'out' || item.direction === 'bidirectional',
  )
  const sourceDomain = endpoints.find(
    (item) => item.value === source,
  )?.domain
  const targets = endpoints.filter(
    (item) =>
      (item.direction === 'in' ||
        item.direction === 'bidirectional') &&
      item.domain === sourceDomain &&
      item.componentId !== splitEndpoint(source)[0],
  )

  async function submit(event: FormEvent) {
    event.preventDefault()
    setFormError('')
    const [sourceComponent, sourcePort] = splitEndpoint(source)
    const [targetComponent, targetPort] = splitEndpoint(target)
    if (
      !sourceComponent ||
      !sourcePort ||
      !targetComponent ||
      !targetPort
    ) {
      setFormError('Select two concrete compatible ports.')
      return
    }
    setSubmitting(true)
    try {
      await onSubmit({
        source: sourceComponent,
        sourceHandle: sourcePort,
        target: targetComponent,
        targetHandle: targetPort,
      })
    } catch (reason) {
      setFormError(
        reason instanceof Error ? reason.message : 'Connection was rejected.',
      )
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form className="component-dialog connection-dialog" onSubmit={submit}>
        <header>
          <div>
            <span className="eyebrow">Registered connector contract</span>
            <h2>Edit connection</h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>
            ×
          </button>
        </header>
        <div className="dialog-kind">
          <code>{connection.id}</code>
          <span>{connection.kind}</span>
        </div>
        <div className="form-grid">
          <label>
            <span>Source port</span>
            <select
              value={source}
              onChange={(event) => {
                setSource(event.target.value)
                setTarget('')
              }}
            >
              {sources.map((item) => (
                <option key={item.value} value={item.value}>
                  {item.value} · {item.domain}
                </option>
              ))}
            </select>
          </label>
          <label>
            <span>Target port</span>
            <select
              value={target}
              onChange={(event) => setTarget(event.target.value)}
              required
            >
              <option value="">Select compatible port</option>
              {targets.map((item) => (
                <option key={item.value} value={item.value}>
                  {item.value} · {item.domain}
                </option>
              ))}
            </select>
          </label>
        </div>
        {formError && <div className="form-error">{formError}</div>}
        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>
            Cancel
          </button>
          <button type="submit" className="primary-button" disabled={submitting}>
            {submitting ? 'Publishing…' : 'Publish updated revision'}
          </button>
        </footer>
      </form>
    </div>
  )
}
