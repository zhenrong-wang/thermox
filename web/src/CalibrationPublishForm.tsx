import { useMemo, useState, type FormEvent } from 'react'
import type {
  CalibrationDocument,
  CaseRevision,
  CreateCalibrationRevision,
  StudyRevision,
} from './types'

interface CalibrationPublishFormProps {
  modelRevisionId: string
  studies: StudyRevision[]
  cases: CaseRevision[]
  onCancel: () => void
  onSubmit: (request: CreateCalibrationRevision) => Promise<void>
}

export function CalibrationPublishForm({
  modelRevisionId,
  studies,
  cases,
  onCancel,
  onSubmit,
}: CalibrationPublishFormProps) {
  const firstCaseId = cases.find(
    (item) => item.case_revision_id === studies[0]?.case_revision_id,
  )?.case_id ?? ''
  const [calibrationId, setCalibrationId] = useState('calibration_1')
  const [trainingIds, setTrainingIds] = useState<string[]>(
    studies[0] ? [studies[0].study_revision_id] : [],
  )
  const [validationIds, setValidationIds] = useState<string[]>([])
  const [maxIterations, setMaxIterations] = useState(20)
  const [definitionText, setDefinitionText] = useState(() =>
    JSON.stringify(
      {
        schema_version: 'thermox.calibration/v1',
        calibration: {
          id: 'calibration_1',
          label: 'Engineering calibration',
          parameters: [
            {
              id: 'parameter_1',
              scope: 'component',
              targets: ['components.component_id.parameters.parameter_name'],
              cases: firstCaseId ? [firstCaseId] : [],
              bounds: { lower: 0, upper: 1 },
            },
          ],
          observations: [
            {
              id: 'observation_1',
              case: firstCaseId,
              target: 'component_id.port_name.value_name',
              measured: 0,
              sigma: 1,
            },
          ],
        },
      } satisfies CalibrationDocument,
      null,
      2,
    ),
  )
  const [submitting, setSubmitting] = useState(false)
  const [error, setError] = useState('')

  const selected = useMemo(
    () => new Set([...trainingIds, ...validationIds]),
    [trainingIds, validationIds],
  )
  const toggle = (
    id: string,
    role: 'training' | 'validation',
    checked: boolean,
  ) => {
    const update = role === 'training' ? setTrainingIds : setValidationIds
    const clear = role === 'training' ? setValidationIds : setTrainingIds
    update((current) =>
      checked ? [...current, id] : current.filter((item) => item !== id),
    )
    if (checked) clear((current) => current.filter((item) => item !== id))
  }
  const submit = async (event: FormEvent) => {
    event.preventDefault()
    setError('')
    setSubmitting(true)
    try {
      const definition = JSON.parse(definitionText) as CalibrationDocument
      definition.calibration.id = calibrationId
      await onSubmit({
        schema_version: 'thermox.calibration_revision.create/v1',
        calibration_id: calibrationId,
        parent_calibration_revision_id: '',
        model_revision_id: modelRevisionId,
        training_study_revision_ids: trainingIds,
        validation_study_revision_ids: validationIds,
        definition,
        solver: { max_iterations: maxIterations },
      })
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : 'Calibration was rejected.')
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form className="component-dialog run-config-dialog" onSubmit={submit}>
        <header>
          <div>
            <span className="eyebrow">Immutable parameter estimation</span>
            <h2>Publish calibration</h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>×</button>
        </header>
        <p className="form-note">
          Select exact Study revisions for fitting and independent validation.
          Every selected Study must use the same model and engineering data.
        </p>
        <div className="form-grid form-grid-wide">
          <label>
            Calibration ID
            <input value={calibrationId} required
              onChange={(event) => setCalibrationId(event.target.value)} />
          </label>
          <label>
            Maximum iterations
            <input type="number" min={1} value={maxIterations}
              onChange={(event) => setMaxIterations(Number(event.target.value))} />
          </label>
        </div>
        <fieldset>
          <legend>Dataset split</legend>
          <div className="case-revision-list">
            {studies.map((study) => (
              <div className="case-revision-card" key={study.study_revision_id}>
                <div><strong>{study.study_id}</strong><span>r{study.revision_number}</span></div>
                <label><input type="checkbox"
                  checked={trainingIds.includes(study.study_revision_id)}
                  onChange={(event) => toggle(study.study_revision_id, 'training', event.target.checked)} /> training</label>
                <label><input type="checkbox"
                  checked={validationIds.includes(study.study_revision_id)}
                  onChange={(event) => toggle(study.study_revision_id, 'validation', event.target.checked)} /> validation</label>
              </div>
            ))}
          </div>
          <small>{selected.size} exact Study revision{selected.size === 1 ? '' : 's'} selected</small>
        </fieldset>
        <label>
          Calibration definition
          <textarea rows={18} value={definitionText}
            onChange={(event) => setDefinitionText(event.target.value)} />
        </label>
        {error && <div className="form-error">{error}</div>}
        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>Cancel</button>
          <button type="submit" className="primary-button"
            disabled={submitting || trainingIds.length === 0}>
            {submitting ? 'Publishing…' : 'Publish calibration'}
          </button>
        </footer>
      </form>
    </div>
  )
}
