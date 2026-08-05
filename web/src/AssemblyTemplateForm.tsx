import { useMemo, useState, type FormEvent } from 'react'
import { buildAssemblyTemplateDocument } from './assemblyAuthoring'
import type {
  ArtifactRevision,
  AssemblyDefinition,
  TopologyDocument,
} from './types'

interface AssemblyTemplateFormProps {
  assembly: AssemblyDefinition
  topology: TopologyDocument
  artifactRevisions: ArtifactRevision[]
  onCancel: () => void
  onSubmit: (
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: TopologyDocument,
  ) => Promise<void>
}

export function AssemblyTemplateForm({
  assembly,
  topology,
  artifactRevisions,
  onCancel,
  onSubmit,
}: AssemblyTemplateFormProps) {
  const [artifactId, setArtifactId] = useState(`${assembly.id}_template`)
  const [submitting, setSubmitting] = useState(false)
  const [formError, setFormError] = useState('')
  const definition = useMemo(
    () => buildAssemblyTemplateDocument(topology, assembly),
    [assembly, topology],
  )
  const latest = artifactRevisions
    .filter(
      (revision) =>
        revision.artifact_type === 'thermox.assembly_template' &&
        revision.artifact_id === artifactId.trim(),
    )
    .sort((left, right) => right.revision_number - left.revision_number)[0]

  async function submit(event: FormEvent) {
    event.preventDefault()
    setFormError('')
    if (!artifactId.trim()) {
      setFormError('Template artifact ID is required.')
      return
    }
    setSubmitting(true)
    try {
      await onSubmit(
        artifactId.trim(),
        latest?.artifact_revision_id ?? '',
        definition,
      )
    } catch (reason) {
      setFormError(
        reason instanceof Error ? reason.message : 'Template was rejected.',
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
            <span className="eyebrow">Project assembly registry</span>
            <h2>Publish reusable template</h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>×</button>
        </header>
        <p className="dialog-guidance">
          The immutable template captures this hierarchy, its public contract, and required fluid/material definitions.
        </p>
        <div className="form-grid">
          <label>
            <span>Template artifact ID</span>
            <input
              value={artifactId}
              onChange={(event) => setArtifactId(event.target.value)}
              required
            />
            <small>
              {latest
                ? `Publishes revision ${latest.revision_number + 1}`
                : 'Publishes the first revision'}
            </small>
          </label>
          <label>
            <span>Template assembly</span>
            <input value={assembly.label || assembly.id} disabled />
          </label>
        </div>
        <div className="registry-note">
          <strong>Dependencies</strong>
          <span>
            {definition.model.media.length} fluids ·{' '}
            {definition.model.materials?.length ?? 0} materials ·{' '}
            {assembly.components.length} direct components
          </span>
        </div>
        {formError && <p className="form-error">{formError}</p>}
        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>
            Cancel
          </button>
          <button type="submit" className="primary-button" disabled={submitting}>
            {submitting ? 'Publishing…' : latest ? 'Publish revision' : 'Publish template'}
          </button>
        </footer>
      </form>
    </div>
  )
}
