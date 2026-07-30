import { useState, type FormEvent } from 'react'
import type { CaseDocument, CaseRevision } from './types'

export const caseModes = [
  'steady_state_design',
  'steady_state_off_design',
  'dynamic_initialization',
  'dynamic_transient',
] as const

interface CaseCreateFormProps {
  revisions: CaseRevision[]
  onCancel: () => void
  onSubmit: (document: CaseDocument) => Promise<void>
}

function suggestedCaseId(revisions: CaseRevision[]) {
  const used = new Set(revisions.map((revision) => revision.case_id))
  if (!used.has('design')) return 'design'
  let suffix = 2
  while (used.has(`case_${suffix}`)) suffix += 1
  return `case_${suffix}`
}

export function CaseCreateForm({
  revisions,
  onCancel,
  onSubmit,
}: CaseCreateFormProps) {
  const [caseId, setCaseId] = useState(() => suggestedCaseId(revisions))
  const [label, setLabel] = useState('')
  const [mode, setMode] = useState<string>('steady_state_design')
  const [submitting, setSubmitting] = useState(false)
  const [formError, setFormError] = useState('')

  async function submit(event: FormEvent) {
    event.preventDefault()
    setFormError('')
    const id = caseId.trim()
    if (!id) {
      setFormError('Case ID is required.')
      return
    }
    const caseDefinition: CaseDocument['case'] = { id, mode }
    if (label.trim()) caseDefinition.label = label.trim()
    setSubmitting(true)
    try {
      await onSubmit({
        schema_version: 'thermox.case/v1',
        case: caseDefinition,
      })
    } catch (reason) {
      setFormError(
        reason instanceof Error ? reason.message : 'Case was rejected.',
      )
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form className="component-dialog case-create-dialog" onSubmit={submit}>
        <header>
          <div>
            <span className="eyebrow">Immutable operating case</span>
            <h2>Create case</h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>
            ×
          </button>
        </header>
        <div className="form-grid">
          <label>
            <span>Case ID</span>
            <input
              value={caseId}
              required
              onChange={(event) => setCaseId(event.target.value)}
            />
          </label>
          <label>
            <span>Label</span>
            <input
              value={label}
              placeholder="Optional"
              onChange={(event) => setLabel(event.target.value)}
            />
          </label>
          <label>
            <span>Mode</span>
            <select
              value={mode}
              onChange={(event) => setMode(event.target.value)}
            >
              {caseModes.map((item) => (
                <option key={item} value={item}>
                  {item}
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
            {submitting ? 'Publishing…' : 'Publish base case'}
          </button>
        </footer>
      </form>
    </div>
  )
}
