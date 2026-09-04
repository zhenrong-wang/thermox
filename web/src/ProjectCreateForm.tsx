import { useState, type FormEvent } from 'react'
import type { Project } from './types'

interface ProjectCreateFormProps {
  onCancel: () => void
  onSubmit: (name: string, description: string) => Promise<Project>
}

export function ProjectCreateForm({ onCancel, onSubmit }: ProjectCreateFormProps) {
  const [name, setName] = useState('')
  const [description, setDescription] = useState('')
  const [submitting, setSubmitting] = useState(false)
  const [error, setError] = useState('')

  async function submit(event: FormEvent) {
    event.preventDefault()
    setError('')
    if (!name.trim()) {
      setError('Project name is required.')
      return
    }
    setSubmitting(true)
    try {
      await onSubmit(name.trim(), description.trim())
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : 'Project creation failed.')
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form className="component-dialog project-create-dialog" onSubmit={submit}>
        <header>
          <div>
            <span className="eyebrow">Team workspace</span>
            <h2>Create project</h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>×</button>
        </header>
        <div className="form-grid">
          <label>
            <span>Project name</span>
            <input autoFocus required value={name}
              onChange={(event) => setName(event.target.value)} />
          </label>
          <label className="form-grid-wide">
            <span>Description</span>
            <textarea rows={3} value={description}
              onChange={(event) => setDescription(event.target.value)} />
          </label>
        </div>
        <div className="registry-note">
          <strong>Start with the system</strong>
          <span>Create a Team-owned workspace, publish its first topology, then draft with registered components.</span>
        </div>
        {error && <div className="form-error">{error}</div>}
        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>Cancel</button>
          <button type="submit" className="primary-button" disabled={submitting}>
            {submitting ? 'Creating…' : 'Create project'}
          </button>
        </footer>
      </form>
    </div>
  )
}
