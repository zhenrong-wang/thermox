import { useMemo, useState, type FormEvent } from 'react'
import type {
  CalibrationDocument,
  CaseRevision,
  CreateReconciliationRevision,
  ReconciliationMode,
  StudyRevision,
} from './types'
import {
  jointConfidenceRegionIssues,
  reconciliationReadiness,
} from './reconciliationAuthoring'

interface ReconciliationPublishFormProps {
  modelRevisionId: string
  studies: StudyRevision[]
  cases: CaseRevision[]
  onCancel: () => void
  onSubmit: (request: CreateReconciliationRevision) => Promise<void>
}

export function ReconciliationPublishForm({
  modelRevisionId,
  studies,
  cases,
  onCancel,
  onSubmit,
}: ReconciliationPublishFormProps) {
  const firstCaseId = cases.find(
    (item) => item.case_revision_id === studies[0]?.case_revision_id,
  )?.case_id ?? ''
  const [reconciliationId, setReconciliationId] =
    useState('reconciliation_1')
  const [mode, setMode] =
    useState<ReconciliationMode>('hard_equalities')
  const [constraintIds, setConstraintIds] = useState<string[]>(
    studies[0] ? [studies[0].study_revision_id] : [],
  )
  const [heldOutIds, setHeldOutIds] = useState<string[]>([])
  const [maxIterations, setMaxIterations] = useState(12)
  const [profileEnabled, setProfileEnabled] = useState(false)
  const [profileParameterIds, setProfileParameterIds] = useState('')
  const [jointRegionEnabled, setJointRegionEnabled] = useState(false)
  const [jointRegionObjectiveIncrease, setJointRegionObjectiveIncrease] =
    useState('1')
  const [jointRegionParameterIds, setJointRegionParameterIds] = useState('')
  const [definitionText, setDefinitionText] = useState(() =>
    JSON.stringify(
      {
        schema_version: 'thermox.calibration/v1',
        calibration: {
          id: 'reconciliation_1',
          label: 'Engineering data reconciliation',
          parameters: [
            {
              id: 'adjustable_1',
              scope: 'component',
              targets: ['components.component_id.parameters.parameter_name'],
              cases: firstCaseId ? [firstCaseId] : [],
              bounds: { lower: 0, upper: 1 },
            },
          ],
          observations: [
            {
              id: 'measurement_1',
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
    () => new Set([...constraintIds, ...heldOutIds]),
    [constraintIds, heldOutIds],
  )
  const readiness = useMemo(() => {
    try {
      return reconciliationReadiness(
        JSON.parse(definitionText) as CalibrationDocument,
        mode,
        constraintIds,
        heldOutIds,
        studies,
        cases,
      )
    } catch {
      return {
        ready: false,
        issues: ['Definition must be valid JSON.'],
        constraintObservationCount: 0,
        adjustableQuantityCount: 0,
      }
    }
  }, [definitionText, mode, constraintIds, heldOutIds, studies, cases])

  const toggle = (
    id: string,
    role: 'constraint' | 'held_out',
    checked: boolean,
  ) => {
    const update = role === 'constraint' ? setConstraintIds : setHeldOutIds
    const clear = role === 'constraint' ? setHeldOutIds : setConstraintIds
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
      definition.calibration.id = reconciliationId
      const jointParameterIds = jointRegionParameterIds
        .split(',')
        .map((item) => item.trim())
        .filter(Boolean)
      const jointIssues = jointConfidenceRegionIssues(
        jointRegionEnabled,
        mode,
        Number(jointRegionObjectiveIncrease),
        jointParameterIds,
      )
      if (jointIssues.length) throw new Error(jointIssues.join(' '))
      await onSubmit({
        schema_version: 'thermox.reconciliation_revision.create/v1',
        reconciliation_id: reconciliationId,
        parent_reconciliation_revision_id: '',
        model_revision_id: modelRevisionId,
        constraint_study_revision_ids: constraintIds,
        held_out_study_revision_ids: heldOutIds,
        definition,
        mode,
        solver: { max_iterations: maxIterations },
        profile_likelihood: {
          enabled: profileEnabled,
          parameter_ids: profileParameterIds
            .split(',')
            .map((item) => item.trim())
            .filter(Boolean),
        },
        joint_confidence_region: {
          enabled: jointRegionEnabled,
          objective_increase: jointRegionEnabled
            ? Number(jointRegionObjectiveIncrease)
            : 0,
          parameter_ids: jointParameterIds,
        },
      })
    } catch (reason) {
      setError(
        reason instanceof Error
          ? reason.message
          : 'Reconciliation was rejected.',
      )
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form className="component-dialog run-config-dialog" onSubmit={submit}>
        <header>
          <div>
            <span className="eyebrow">Calculation intent · measured system</span>
            <h2>Publish data reconciliation</h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>×</button>
        </header>
        <p className="form-note">
          Constraint Studies influence inferred quantities. Held-out Studies are
          evaluated afterward and never influence the solution.
        </p>
        <div className="form-grid form-grid-wide">
          <label>
            Reconciliation ID
            <input value={reconciliationId} required
              onChange={(event) => setReconciliationId(event.target.value)} />
          </label>
          <label>
            Measurement treatment
            <select value={mode}
              onChange={(event) => {
                const next = event.target.value as ReconciliationMode
                setMode(next)
                if (next !== 'weighted_measurements') setJointRegionEnabled(false)
              }}>
              <option value="hard_equalities">Hard equalities</option>
              <option value="weighted_measurements">Weighted measurements</option>
            </select>
          </label>
          <label>
            Maximum iterations
            <input type="number" min={1} value={maxIterations}
              onChange={(event) => setMaxIterations(Number(event.target.value))} />
          </label>
          <label className="checkbox-label">
            <input type="checkbox" checked={profileEnabled}
              onChange={(event) => setProfileEnabled(event.target.checked)} />
            Profile-likelihood evidence
          </label>
          <label className="checkbox-label">
            <input type="checkbox" checked={jointRegionEnabled}
              disabled={mode !== 'weighted_measurements'}
              onChange={(event) => setJointRegionEnabled(event.target.checked)} />
            Local joint confidence region
          </label>
        </div>
        {profileEnabled && (
          <label>
            Profile parameter IDs (comma separated; blank means all)
            <input value={profileParameterIds}
              onChange={(event) => setProfileParameterIds(event.target.value)} />
          </label>
        )}
        {jointRegionEnabled && (
          <fieldset>
            <legend>Local joint covariance ellipsoid</legend>
            <p className="form-note">
              Supply an objective increase explicitly. Thermox does not infer a
              coverage percentage; this is local sensitivity evidence, not an
              independent validation result.
            </p>
            <div className="form-grid">
              <label>
                Objective increase
                <input type="number" min="0" step="any" required
                  value={jointRegionObjectiveIncrease}
                  onChange={(event) => setJointRegionObjectiveIncrease(event.target.value)} />
              </label>
              <label>
                Parameter IDs (comma separated; blank means all)
                <input value={jointRegionParameterIds}
                  onChange={(event) => setJointRegionParameterIds(event.target.value)} />
              </label>
            </div>
          </fieldset>
        )}
        <fieldset>
          <legend>Evidence partition</legend>
          <div className="case-revision-list">
            {studies.map((study) => (
              <div className="case-revision-card" key={study.study_revision_id}>
                <div><strong>{study.study_id}</strong><span>r{study.revision_number}</span></div>
                <label><input type="checkbox"
                  checked={constraintIds.includes(study.study_revision_id)}
                  onChange={(event) => toggle(study.study_revision_id, 'constraint', event.target.checked)} /> constraint</label>
                <label><input type="checkbox"
                  checked={heldOutIds.includes(study.study_revision_id)}
                  onChange={(event) => toggle(study.study_revision_id, 'held_out', event.target.checked)} /> held out</label>
              </div>
            ))}
          </div>
          <small>{selected.size} exact Study revision{selected.size === 1 ? '' : 's'} selected</small>
        </fieldset>
        <label>
          Adjustable quantities and measured observations
          <textarea rows={18} value={definitionText}
            onChange={(event) => setDefinitionText(event.target.value)} />
        </label>
        {!readiness.ready && (
          <div className="form-error">{readiness.issues.join(' ')}</div>
        )}
        {error && <div className="form-error">{error}</div>}
        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>Cancel</button>
          <button type="submit" className="primary-button"
            disabled={submitting || !readiness.ready}>
            {submitting ? 'Publishing…' : 'Publish reconciliation'}
          </button>
        </footer>
      </form>
    </div>
  )
}
