import { useState, type ChangeEvent, type FormEvent } from 'react'
import { parseValidationSeriesArtifact } from './studyAuthoring'
import type { ValidationSeriesArtifact } from './types'

interface Props {
  onCancel: () => void
  onSubmit: (
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: ValidationSeriesArtifact,
  ) => Promise<void>
}

const example = `{
  "schema_version": "thermox.validation_series/v1",
  "id": "measured-response",
  "source": {
    "reference": "Test report or historian export",
    "checksum_sha256": "replace-with-lowercase-64-character-sha256",
    "evidence_basis": "independent_reference",
    "acquisition": "measured",
    "note": "",
    "limitations": []
  },
  "time_unit": "s",
  "signals": [{
    "id": "shaft_speed",
    "dimension": "angular_speed",
    "unit": "rpm",
    "samples": [{"time": 0, "value": 3000}]
  }]
}`

export function ValidationSeriesArtifactForm({ onCancel, onSubmit }: Props) {
  const [artifactId, setArtifactId] = useState('measured-response')
  const [payload, setPayload] = useState(example)
  const [error, setError] = useState('')
  const [submitting, setSubmitting] = useState(false)

  const loadFile = async (event: ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0]
    if (!file) return
    setPayload(await file.text())
    setError('')
  }

  const submit = async (event: FormEvent) => {
    event.preventDefault()
    setError('')
    let definition: ValidationSeriesArtifact
    try {
      definition = parseValidationSeriesArtifact(payload, artifactId)
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : 'Invalid JSON.')
      return
    }
    setSubmitting(true)
    try {
      await onSubmit(artifactId, '', definition)
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : 'Import failed.')
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form className="component-dialog run-config-dialog" onSubmit={submit}>
        <header>
          <div>
            <span className="eyebrow">Immutable reference evidence</span>
            <h2>Import validation series</h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>×</button>
        </header>
        <p className="form-note">
          Import measured, computational, derived, or digitized time-series data.
          The service verifies provenance, units, uncertainty, and timestamps.
        </p>
        <label>
          Artifact ID
          <input value={artifactId} onChange={(event) => setArtifactId(event.target.value)} />
        </label>
        <label>
          JSON file
          <input type="file" accept="application/json,.json" onChange={loadFile} />
        </label>
        <label>
          Validation-series declaration
          <textarea rows={20} spellCheck={false} value={payload}
            onChange={(event) => setPayload(event.target.value)} />
        </label>
        {error && <div className="form-error">{error}</div>}
        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>Cancel</button>
          <button type="submit" className="primary-button" disabled={submitting}>
            {submitting ? 'Publishing…' : 'Publish immutable evidence'}
          </button>
        </footer>
      </form>
    </div>
  )
}
