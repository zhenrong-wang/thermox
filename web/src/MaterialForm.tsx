import { useMemo, useState, type FormEvent } from 'react'
import type {
  MaterialDefinition,
  ThermochemistryBackend,
  TopologyDocument,
} from './types'

interface MaterialFormProps {
  backends: ThermochemistryBackend[]
  topology: TopologyDocument
  onCancel: () => void
  onSubmit: (material: MaterialDefinition) => Promise<void>
}

export function parseSpecies(value: string) {
  return [...new Set(value.split(/[\s,;]+/).map((item) => item.trim()).filter(Boolean))]
}

function suggestedId(topology: TopologyDocument) {
  const used = new Set(
    (topology.model.materials ?? []).map((material) => material.id),
  )
  if (!used.has('reacting_gas')) return 'reacting_gas'
  let suffix = 2
  while (used.has(`reacting_gas_${suffix}`)) suffix += 1
  return `reacting_gas_${suffix}`
}

export function MaterialForm({
  backends,
  topology,
  onCancel,
  onSubmit,
}: MaterialFormProps) {
  const [backendId, setBackendId] = useState(backends[0]?.backend ?? '')
  const backend = backends.find((item) => item.backend === backendId)
  const [materialId, setMaterialId] = useState(() => suggestedId(topology))
  const [mechanism, setMechanism] = useState('')
  const [phase, setPhase] = useState('')
  const [speciesText, setSpeciesText] = useState('')
  const [submitting, setSubmitting] = useState(false)
  const [formError, setFormError] = useState('')
  const species = useMemo(() => parseSpecies(speciesText), [speciesText])

  async function submit(event: FormEvent) {
    event.preventDefault()
    setFormError('')
    if (!backend || !materialId.trim() || !mechanism.trim() || !phase.trim()) {
      setFormError('Select a backend and provide an ID, mechanism, and phase.')
      return
    }
    if (species.length === 0) {
      setFormError('Provide at least one species exposed to the model.')
      return
    }
    setSubmitting(true)
    try {
      await onSubmit({
        id: materialId.trim(),
        backend: backend.backend,
        mechanism: mechanism.trim(),
        phase: phase.trim(),
        species,
        package_version: backend.implementation_version,
      })
    } catch (reason) {
      setFormError(
        reason instanceof Error ? reason.message : 'Reacting mixture was rejected.',
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
            <span className="eyebrow">Thermochemistry registry</span>
            <h2>Add reacting mixture</h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>×</button>
        </header>
        <div className="form-grid">
          <label>
            <span>Thermochemistry backend</span>
            <select value={backendId} required onChange={(event) => setBackendId(event.target.value)}>
              {backends.map((item) => (
                <option key={item.backend} value={item.backend}>
                  {item.backend} · {item.implementation_name}
                </option>
              ))}
            </select>
          </label>
          <label>
            <span>Mixture ID</span>
            <input value={materialId} required onChange={(event) => setMaterialId(event.target.value)} />
          </label>
          <label>
            <span>Mechanism</span>
            <input value={mechanism} required placeholder="gri30.yaml" onChange={(event) => setMechanism(event.target.value)} />
          </label>
          <label>
            <span>Phase</span>
            <input value={phase} required placeholder="gri30" onChange={(event) => setPhase(event.target.value)} />
          </label>
          <label className="form-grid-wide">
            <span>Model species</span>
            <textarea
              value={speciesText}
              required
              rows={4}
              placeholder="N2, O2, CH4, CO2, H2O"
              onChange={(event) => setSpeciesText(event.target.value)}
            />
            <small>Comma, space, or line separated. {species.length} unique species.</small>
          </label>
        </div>
        <div className="registry-note">
          <strong>Capabilities</strong>
          <span>{backend?.capabilities.join(', ') || 'No capabilities reported.'}</span>
        </div>
        {formError && <div className="form-error">{formError}</div>}
        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>Cancel</button>
          <button type="submit" className="primary-button" disabled={submitting}>
            {submitting ? 'Publishing…' : 'Publish mixture revision'}
          </button>
        </footer>
      </form>
    </div>
  )
}
