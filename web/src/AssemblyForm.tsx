import { useState, type FormEvent } from 'react'
import type { TopologyDocument } from './types'

interface AssemblyFormProps {
  topology: TopologyDocument
  onCancel: () => void
  onSubmit: (
    assemblyId: string,
    label: string,
    componentIds: string[],
  ) => Promise<void>
}

function suggestedId(topology: TopologyDocument): string {
  const used = new Set([
    ...topology.model.components.map((component) => component.id),
    ...(topology.model.assemblies ?? []).map((assembly) => assembly.id),
  ])
  let suffix = 1
  while (used.has(`assembly_${suffix}`)) suffix += 1
  return `assembly_${suffix}`
}

export function AssemblyForm({
  topology,
  onCancel,
  onSubmit,
}: AssemblyFormProps) {
  const [assemblyId, setAssemblyId] = useState(() => suggestedId(topology))
  const [label, setLabel] = useState('')
  const [selected, setSelected] = useState<string[]>([])
  const [submitting, setSubmitting] = useState(false)
  const [formError, setFormError] = useState('')

  async function submit(event: FormEvent) {
    event.preventDefault()
    setFormError('')
    if (selected.length === 0) {
      setFormError('Select at least one component to group.')
      return
    }
    setSubmitting(true)
    try {
      await onSubmit(assemblyId, label, selected)
    } catch (reason) {
      setFormError(
        reason instanceof Error ? reason.message : 'Assembly was rejected.',
      )
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form className="component-dialog assembly-dialog" onSubmit={submit}>
        <header>
          <div>
            <span className="eyebrow">Hierarchical topology</span>
            <h2>Group components</h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>×</button>
        </header>
        <p className="dialog-guidance">
          Internal links move inside the assembly. Connected boundary ports are exported automatically.
        </p>
        <div className="form-grid">
          <label>
            <span>Assembly ID</span>
            <input
              value={assemblyId}
              onChange={(event) => setAssemblyId(event.target.value)}
              required
            />
          </label>
          <label>
            <span>Label</span>
            <input
              value={label}
              onChange={(event) => setLabel(event.target.value)}
              placeholder="Optional engineering label"
            />
          </label>
        </div>
        <fieldset>
          <legend>Top-level components</legend>
          <div className="assembly-choice-list">
            {topology.model.components.map((component) => (
              <label key={component.id}>
                <input
                  type="checkbox"
                  checked={selected.includes(component.id)}
                  onChange={(event) =>
                    setSelected((current) =>
                      event.target.checked
                        ? [...current, component.id]
                        : current.filter((id) => id !== component.id),
                    )
                  }
                />
                <span>
                  <strong>{component.label || component.id}</strong>
                  <code>{component.kind}</code>
                </span>
              </label>
            ))}
          </div>
        </fieldset>
        {formError && <p className="form-error">{formError}</p>}
        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>
            Cancel
          </button>
          <button type="submit" className="primary-button" disabled={submitting}>
            {submitting ? 'Publishing…' : 'Group into assembly'}
          </button>
        </footer>
      </form>
    </div>
  )
}
