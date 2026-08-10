import { useState, type FormEvent } from 'react'
import {
  defaultSteadySolver,
  defaultTransientSolver,
} from './runAuthoring'
import type {
  CreateRunConfiguration,
  RunConfigurationRevision,
  SteadySolverSettings,
  StudyRevision,
  TransientSolverSettings,
} from './types'

interface RunConfigurationFormProps {
  study: StudyRevision
  revisions: RunConfigurationRevision[]
  base?: RunConfigurationRevision
  onCancel: () => void
  onSubmit: (request: CreateRunConfiguration) => Promise<void>
}

type SteadyNumericKey = Exclude<
  keyof SteadySolverSettings,
  'continuation_enabled' | 'structural_decomposition_enabled'
>

const steadyFields: Array<{
  key: SteadyNumericKey
  label: string
  integer?: boolean
}> = [
  { key: 'max_iterations', label: 'Max iterations', integer: true },
  { key: 'residual_tolerance', label: 'Residual tolerance' },
  { key: 'step_tolerance', label: 'Step tolerance' },
  { key: 'linear_residual_tolerance', label: 'Linear solve tolerance' },
  { key: 'finite_difference_epsilon', label: 'FD epsilon' },
  { key: 'min_damping', label: 'Minimum damping' },
  { key: 'damping_reduction', label: 'Damping reduction' },
  { key: 'sufficient_decrease', label: 'Sufficient decrease' },
  {
    key: 'max_line_search_steps',
    label: 'Line-search steps',
    integer: true,
  },
  { key: 'continuation_initial_step', label: 'Continuation initial step' },
  { key: 'continuation_minimum_step', label: 'Continuation minimum step' },
  { key: 'continuation_step_growth', label: 'Continuation growth' },
  { key: 'continuation_step_reduction', label: 'Continuation reduction' },
  {
    key: 'continuation_maximum_stages',
    label: 'Continuation stages',
    integer: true,
  },
]

const transientFields: Array<{
  key: Exclude<
    keyof TransientSolverSettings,
    'compute_consistent_initial_conditions' | 'nonlinear_solver'
  >
  label: string
  integer?: boolean
}> = [
  { key: 'start_time', label: 'Start time' },
  { key: 'end_time', label: 'End time' },
  { key: 'initial_step', label: 'Initial step' },
  { key: 'min_step', label: 'Minimum step' },
  { key: 'max_step', label: 'Maximum step' },
  { key: 'absolute_tolerance', label: 'Scaled absolute tolerance' },
  { key: 'relative_tolerance', label: 'Relative tolerance' },
  { key: 'max_steps', label: 'Max steps', integer: true },
  {
    key: 'max_consecutive_rejections',
    label: 'Max rejected steps',
    integer: true,
  },
  { key: 'maximum_order', label: 'Maximum BDF order', integer: true },
]

function suggestedId(
  study: StudyRevision,
  revisions: RunConfigurationRevision[],
) {
  const base = `${study.study_id}-run`
  const used = new Set(
    revisions.map((revision) => revision.run_configuration_id),
  )
  if (!used.has(base)) return base
  let suffix = 2
  while (used.has(`run-${suffix}`)) suffix += 1
  return `run-${suffix}`
}

export function RunConfigurationForm({
  study,
  revisions,
  base,
  onCancel,
  onSubmit,
}: RunConfigurationFormProps) {
  const mode =
    study.intent.includes('dynamic') || study.intent.includes('transient')
      ? 'transient'
      : 'steady'
  const [configurationId, setConfigurationId] = useState(
    base?.run_configuration_id ?? suggestedId(study, revisions),
  )
  const [steadySolver, setSteadySolver] = useState<SteadySolverSettings>(
    base?.steady_solver ?? { ...defaultSteadySolver },
  )
  const [transientSolver, setTransientSolver] =
    useState<TransientSolverSettings>(
      base?.transient_solver ?? {
        ...defaultTransientSolver,
        nonlinear_solver: { ...defaultSteadySolver },
      },
    )
  const [submitting, setSubmitting] = useState(false)
  const [formError, setFormError] = useState('')

  async function submit(event: FormEvent) {
    event.preventDefault()
    setFormError('')
    if (!configurationId.trim()) {
      setFormError('Run configuration ID is required.')
      return
    }
    setSubmitting(true)
    try {
      await onSubmit({
        schema_version: 'thermox.run_configuration.create/v3',
        run_configuration_id: configurationId.trim(),
        parent_run_configuration_revision_id:
          base?.run_configuration_revision_id ?? '',
        study_revision_id: base?.study_revision_id ?? study.study_revision_id,
        steady_solver: steadySolver,
        transient_solver: transientSolver,
      })
    } catch (reason) {
      setFormError(
        reason instanceof Error
          ? reason.message
          : 'Run configuration was rejected.',
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
            <span className="eyebrow">Immutable execution intent</span>
            <h2>{base ? 'Revise run configuration' : 'Create run configuration'}</h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>
            ×
          </button>
        </header>
        <div className="run-binding-summary">
          <div>
            <span>Study revision</span>
            <code>{base?.study_revision_id ?? study.study_revision_id}</code>
          </div>
          <div>
            <span>Intent</span>
            <code>{study.intent}</code>
          </div>
          <div>
            <span>Outputs</span>
            <strong>{study.result_projections.length}</strong>
          </div>
          <div>
            <span>Mode</span>
            <strong>{mode}</strong>
          </div>
        </div>
        <div className="form-grid">
          <label>
            <span>Configuration ID</span>
            <input
              value={configurationId}
              disabled={Boolean(base)}
              required
              onChange={(event) => setConfigurationId(event.target.value)}
            />
          </label>
          {base && (
            <label>
              <span>Parent revision</span>
              <input
                value={base.run_configuration_revision_id}
                disabled
              />
            </label>
          )}
        </div>

        {mode === 'steady' ? (
          <SolverFields
            title="Steady nonlinear solver"
            value={steadySolver}
            onChange={setSteadySolver}
          />
        ) : (
          <>
            <fieldset>
              <legend>Transient integration</legend>
              <div className="form-grid">
                {transientFields.map((field) => (
                  <label key={field.key}>
                    <span>{field.label}</span>
                    <input
                      type="number"
                      step={field.integer ? 1 : 'any'}
                      value={transientSolver[field.key]}
                      onChange={(event) =>
                        setTransientSolver((current) => ({
                          ...current,
                          [field.key]: Number(event.target.value),
                        }))
                      }
                    />
                  </label>
                ))}
                <label className="checkbox-field">
                  <input
                    type="checkbox"
                    checked={
                      transientSolver.compute_consistent_initial_conditions
                    }
                    onChange={(event) =>
                      setTransientSolver((current) => ({
                        ...current,
                        compute_consistent_initial_conditions:
                          event.target.checked,
                      }))
                    }
                  />
                  <span>Compute consistent initial conditions</span>
                </label>
              </div>
            </fieldset>
            <SolverFields
              title="Transient nonlinear solver"
              value={transientSolver.nonlinear_solver}
              onChange={(value) =>
                setTransientSolver((current) => ({
                  ...current,
                  nonlinear_solver: value,
                }))
              }
            />
          </>
        )}

        <p className="form-note">
          Physical inputs and result projections are owned by the bound Study.
          This configuration controls execution policy only.
        </p>

        {formError && <div className="form-error">{formError}</div>}
        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>
            Cancel
          </button>
          <button type="submit" className="primary-button" disabled={submitting}>
            {submitting
              ? 'Publishing…'
              : base
                ? 'Publish child configuration'
                : 'Publish run configuration'}
          </button>
        </footer>
      </form>
    </div>
  )
}

function SolverFields({
  title,
  value,
  onChange,
}: {
  title: string
  value: SteadySolverSettings
  onChange: (value: SteadySolverSettings) => void
}) {
  return (
    <fieldset>
      <legend>{title}</legend>
      <label className="checkbox-row">
        <input
          type="checkbox"
          checked={value.continuation_enabled}
          onChange={(event) =>
            onChange({
              ...value,
              continuation_enabled: event.target.checked,
            })
          }
        />
        <span>Use adaptive continuation</span>
      </label>
      <label className="checkbox-row">
        <input
          type="checkbox"
          checked={value.structural_decomposition_enabled}
          onChange={(event) =>
            onChange({
              ...value,
              structural_decomposition_enabled: event.target.checked,
            })
          }
        />
        <span>Solve dependency-ordered structural blocks</span>
      </label>
      <div className="form-grid">
        {steadyFields.map((field) => (
          <label key={field.key}>
            <span>{field.label}</span>
            <input
              type="number"
              step={field.integer ? 1 : 'any'}
              value={value[field.key]}
              onChange={(event) =>
                onChange({
                  ...value,
                  [field.key]: Number(event.target.value),
                })
              }
            />
          </label>
        ))}
      </div>
    </fieldset>
  )
}
