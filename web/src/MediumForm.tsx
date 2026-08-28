import { useMemo, useState, type FormEvent } from 'react'
import type {
  MediumDefinition,
  PropertyBackend,
  TopologyDocument,
} from './types'

interface MediumFormProps {
  backends: PropertyBackend[]
  topology: TopologyDocument
  onCancel: () => void
  onSubmit: (medium: MediumDefinition) => Promise<void>
}

function suggestedId(substance: string, topology: TopologyDocument) {
  const base =
    substance
      .trim()
      .toLowerCase()
      .replace(/[^a-z0-9_-]+/g, '_')
      .replace(/^_+|_+$/g, '') || 'fluid'
  const used = new Set(topology.model.media.map((medium) => medium.id))
  if (!used.has(base)) return base
  let suffix = 2
  while (used.has(`${base}_${suffix}`)) suffix += 1
  return `${base}_${suffix}`
}

export function MediumForm({
  backends,
  topology,
  onCancel,
  onSubmit,
}: MediumFormProps) {
  const [backendId, setBackendId] = useState(backends[0]?.backend ?? '')
  const backend = backends.find((item) => item.backend === backendId)
  const [substance, setSubstance] = useState(
    backend?.supported_substances[0] ?? '',
  )
  const [mediumId, setMediumId] = useState(() =>
    suggestedId(substance, topology),
  )
  const [idTouched, setIdTouched] = useState(false)
  const [submitting, setSubmitting] = useState(false)
  const [formError, setFormError] = useState('')

  const capabilities = useMemo(
    () => backend?.capabilities.join(', ') ?? '',
    [backend],
  )

  function selectBackend(value: string) {
    const selected = backends.find((item) => item.backend === value)
    const nextSubstance = selected?.supported_substances[0] ?? ''
    setBackendId(value)
    setSubstance(nextSubstance)
    if (!idTouched) setMediumId(suggestedId(nextSubstance, topology))
  }

  function selectSubstance(value: string) {
    setSubstance(value)
    if (!idTouched) setMediumId(suggestedId(value, topology))
  }

  async function submit(event: FormEvent) {
    event.preventDefault()
    setFormError('')
    if (!backend || !substance || !mediumId.trim()) {
      setFormError('Select a property backend, substance, and medium ID.')
      return
    }
    setSubmitting(true)
    try {
      await onSubmit({
        id: mediumId.trim(),
        backend: backend.backend,
        substance,
        package_version: backend.implementation_version,
      })
    } catch (reason) {
      setFormError(
        reason instanceof Error ? reason.message : 'Fluid was rejected.',
      )
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form className="component-dialog medium-dialog" onSubmit={submit}>
        <header>
          <div>
            <span className="eyebrow">Property registry</span>
            <h2>Add fluid definition</h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>
            ×
          </button>
        </header>
        <div className="form-grid">
          <label>
            <span>Property backend</span>
            <select
              value={backendId}
              required
              onChange={(event) => selectBackend(event.target.value)}
            >
              {backends.map((item) => (
                <option key={item.backend} value={item.backend}>
                  {item.backend} · {item.implementation_name}
                </option>
              ))}
            </select>
          </label>
          <label>
            <span>Substance</span>
            {backend?.supported_substances.length ? (
              <select
                value={substance}
                required
                onChange={(event) => selectSubstance(event.target.value)}
              >
                {backend.supported_substances.map((item) => (
                  <option key={item} value={item}>
                    {item}
                  </option>
                ))}
              </select>
            ) : (
              <input
                value={substance}
                required
                placeholder="Provider substance identifier, e.g. R245fa"
                onChange={(event) => selectSubstance(event.target.value)}
              />
            )}
          </label>
          <label>
            <span>Medium ID</span>
            <input
              value={mediumId}
              required
              onChange={(event) => {
                setIdTouched(true)
                setMediumId(event.target.value)
              }}
            />
          </label>
          <label>
            <span>Package version</span>
            <input value={backend?.implementation_version ?? ''} disabled />
          </label>
        </div>
        <div className="registry-note">
          <strong>Capabilities</strong>
          <span>{capabilities || 'No capabilities reported.'}</span>
        </div>
        {formError && <div className="form-error">{formError}</div>}
        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>
            Cancel
          </button>
          <button type="submit" className="primary-button" disabled={submitting}>
            {submitting ? 'Publishing…' : 'Publish fluid revision'}
          </button>
        </footer>
      </form>
    </div>
  )
}
