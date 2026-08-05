import { useState, type FormEvent } from 'react'
import type {
  AssemblyTemplateCatalogEntry,
  TopologyDocument,
} from './types'

interface AssemblyTemplateInstanceFormProps {
  template: AssemblyTemplateCatalogEntry
  topology: TopologyDocument
  onCancel: () => void
  onSubmit: (instanceId: string, label: string) => Promise<void>
}

function suggestedId(
  template: AssemblyTemplateCatalogEntry,
  topology: TopologyDocument,
) {
  const source = template.definition.model.assemblies?.[0]
  const base = source?.id || template.source.artifact_id
  const used = new Set([
    ...topology.model.components.map((component) => component.id),
    ...(topology.model.assemblies ?? []).map((assembly) => assembly.id),
  ])
  if (!used.has(base)) return base
  let suffix = 2
  while (used.has(`${base}_${suffix}`)) suffix += 1
  return `${base}_${suffix}`
}

export function AssemblyTemplateInstanceForm({
  template,
  topology,
  onCancel,
  onSubmit,
}: AssemblyTemplateInstanceFormProps) {
  const source = template.definition.model.assemblies?.[0]
  const [instanceId, setInstanceId] = useState(() =>
    suggestedId(template, topology),
  )
  const [label, setLabel] = useState(source?.label ?? '')
  const [submitting, setSubmitting] = useState(false)
  const [formError, setFormError] = useState('')

  async function submit(event: FormEvent) {
    event.preventDefault()
    setFormError('')
    setSubmitting(true)
    try {
      await onSubmit(instanceId, label)
    } catch (reason) {
      setFormError(
        reason instanceof Error ? reason.message : 'Instance was rejected.',
      )
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form className="component-dialog" onSubmit={submit}>
        <header>
          <div>
            <span className="eyebrow">Assembly template</span>
            <h2>Instantiate {source?.label || template.source.artifact_id}</h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>×</button>
        </header>
        <div className="dialog-kind">
          <code>{template.source.artifact_id}</code>
          <span>revision {template.source.revision_number}</span>
        </div>
        <div className="form-grid">
          <label>
            <span>Instance ID</span>
            <input
              value={instanceId}
              onChange={(event) => setInstanceId(event.target.value)}
              required
            />
          </label>
          <label>
            <span>Label</span>
            <input value={label} onChange={(event) => setLabel(event.target.value)} />
          </label>
        </div>
        {formError && <p className="form-error">{formError}</p>}
        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>
            Cancel
          </button>
          <button type="submit" className="primary-button" disabled={submitting}>
            {submitting ? 'Publishing…' : 'Add to topology'}
          </button>
        </footer>
      </form>
    </div>
  )
}
