import { useState, type FormEvent } from 'react'
import { buildValidationCampaign } from './validationCampaignAuthoring'
import type {
  StudyRevision,
  ValidationCampaignArtifact,
  ValidationCampaignCatalogEntry,
} from './types'

interface ValidationCampaignFormProps {
  studies: StudyRevision[]
  campaigns: ValidationCampaignCatalogEntry[]
  base?: ValidationCampaignCatalogEntry
  onCancel: () => void
  onSubmit: (
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: ValidationCampaignArtifact,
  ) => Promise<void>
}

export function ValidationCampaignForm({
  studies,
  campaigns,
  base,
  onCancel,
  onSubmit,
}: ValidationCampaignFormProps) {
  const [artifactId, setArtifactId] = useState(
    base?.source.artifact_id ?? 'validation-campaign',
  )
  const [name, setName] = useState(base?.definition.name ?? '')
  const [objective, setObjective] = useState(base?.definition.objective ?? '')
  const [studyRevisionIds, setStudyRevisionIds] = useState<string[]>(
    base?.definition.study_revision_ids ?? [],
  )
  const [limitationsText, setLimitationsText] = useState(
    base?.definition.limitations.join('\n') ?? '',
  )
  const [error, setError] = useState('')
  const [submitting, setSubmitting] = useState(false)

  const submit = async (event: FormEvent) => {
    event.preventDefault()
    setError('')
    try {
      const definition = buildValidationCampaign({
        artifactId,
        name,
        objective,
        studyRevisionIds,
        limitationsText,
      })
      if (
        !base && campaigns.some(
          (campaign) => campaign.source.artifact_id === definition.id,
        )
      ) {
        throw new Error(
          'That campaign ID already exists. Revise its latest immutable revision.',
        )
      }
      setSubmitting(true)
      await onSubmit(
        definition.id,
        base?.source.artifact_revision_id ?? '',
        definition,
      )
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : 'Publication failed.')
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form className="component-dialog run-config-dialog validation-campaign-dialog"
        onSubmit={submit}>
        <header>
          <div>
            <span className="eyebrow">Immutable validation intent</span>
            <h2>{base ? 'Revise validation campaign' : 'Publish validation campaign'}</h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>×</button>
        </header>
        <p className="form-note">
          Pin the exact Studies that define this campaign. A report later
          selects one terminal execution per Study; this declaration does not
          execute calculations or assert engineering credibility.
        </p>
        <label>
          Campaign artifact ID
          <input required disabled={Boolean(base)} value={artifactId}
            onChange={(event) => setArtifactId(event.target.value)} />
          {base && <small>Publishes a child of revision {base.source.revision_number}.</small>}
        </label>
        <label>
          Campaign name
          <input required value={name}
            onChange={(event) => setName(event.target.value)} />
        </label>
        <label>
          Engineering objective
          <textarea required rows={3} value={objective}
            onChange={(event) => setObjective(event.target.value)} />
        </label>
        <fieldset>
          <legend>Exact Study revisions</legend>
          <div className="campaign-study-picker">
            {studies.map((study) => (
              <label key={study.study_revision_id}>
                <input type="checkbox"
                  checked={studyRevisionIds.includes(study.study_revision_id)}
                  onChange={(event) => setStudyRevisionIds((current) =>
                    event.target.checked
                      ? [...current, study.study_revision_id]
                      : current.filter((id) => id !== study.study_revision_id),
                  )} />
                <span>
                  <strong>{study.study_id} · r{study.revision_number}</strong>
                  <small>{study.intent}</small>
                  <code>{study.study_revision_id}</code>
                </span>
              </label>
            ))}
            {!studies.length && <p>No Project Studies are available.</p>}
          </div>
        </fieldset>
        <label>
          Known campaign limitations
          <textarea rows={4} value={limitationsText}
            placeholder="One limitation per line"
            onChange={(event) => setLimitationsText(event.target.value)} />
        </label>
        {error && <div className="form-error">{error}</div>}
        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>
            Cancel
          </button>
          <button type="submit" className="primary-button"
            disabled={submitting || !studies.length}>
            {submitting ? 'Publishing…' : 'Publish immutable campaign'}
          </button>
        </footer>
      </form>
    </div>
  )
}
