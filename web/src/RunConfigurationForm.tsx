import { useState, type FormEvent } from 'react'
import {
  defaultSteadySolver,
  defaultTransientSolver,
  scopeRequiresComponent,
  scopeRequiresPort,
} from './runAuthoring'
import type {
  Catalog,
  CaseRevision,
  CreateRunConfiguration,
  ResultAggregation,
  ResultProjection,
  ResultValueScope,
  RunConfigurationRevision,
  SteadySolverSettings,
  TopologyDocument,
  TransientSolverSettings,
} from './types'

interface RunConfigurationFormProps {
  topology: TopologyDocument
  catalog: Catalog
  modelRevisionId: string
  caseRevision: CaseRevision
  artifactRevisionIds: string[]
  revisions: RunConfigurationRevision[]
  base?: RunConfigurationRevision
  onCancel: () => void
  onSubmit: (request: CreateRunConfiguration) => Promise<void>
}

const steadyFields: Array<{
  key: keyof SteadySolverSettings
  label: string
  integer?: boolean
}> = [
  { key: 'max_iterations', label: 'Max iterations', integer: true },
  { key: 'residual_tolerance', label: 'Residual tolerance' },
  { key: 'step_tolerance', label: 'Step tolerance' },
  { key: 'finite_difference_epsilon', label: 'FD epsilon' },
  { key: 'min_damping', label: 'Minimum damping' },
  { key: 'damping_reduction', label: 'Damping reduction' },
  { key: 'sufficient_decrease', label: 'Sufficient decrease' },
  {
    key: 'max_line_search_steps',
    label: 'Line-search steps',
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
  { key: 'absolute_tolerance', label: 'Absolute tolerance' },
  { key: 'relative_tolerance', label: 'Relative tolerance' },
  { key: 'max_steps', label: 'Max steps', integer: true },
  {
    key: 'max_consecutive_rejections',
    label: 'Max rejected steps',
    integer: true,
  },
]

const scopes: ResultValueScope[] = [
  'system_balance',
  'kpi',
  'component_metric',
  'component_internal',
  'port_primary',
  'port_derived',
]

function suggestedId(
  caseRevision: CaseRevision,
  revisions: RunConfigurationRevision[],
) {
  const base = `${caseRevision.case_id}-run`
  const used = new Set(
    revisions.map((revision) => revision.run_configuration_id),
  )
  if (!used.has(base)) return base
  let suffix = 2
  while (used.has(`run-${suffix}`)) suffix += 1
  return `run-${suffix}`
}

function projectionId(index: number) {
  return `result_${index + 1}`
}

export function RunConfigurationForm({
  topology,
  catalog,
  modelRevisionId,
  caseRevision,
  artifactRevisionIds,
  revisions,
  base,
  onCancel,
  onSubmit,
}: RunConfigurationFormProps) {
  const mode =
    base?.mode ??
    (caseRevision.mode.includes('dynamic') ||
    caseRevision.mode.includes('transient')
      ? 'transient'
      : 'steady')
  const [configurationId, setConfigurationId] = useState(
    base?.run_configuration_id ?? suggestedId(caseRevision, revisions),
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
  const [projections, setProjections] = useState<ResultProjection[]>(
    base?.result_projections ?? [],
  )
  const [submitting, setSubmitting] = useState(false)
  const [formError, setFormError] = useState('')

  const componentPorts = (componentId: string) => {
    const component = topology.model.components.find(
      (item) => item.id === componentId,
    )
    return (
      catalog.components.find((item) => item.kind === component?.kind)?.ports ??
      []
    )
  }

  function updateProjection(
    index: number,
    update: Partial<ResultProjection>,
  ) {
    setProjections((current) =>
      current.map((projection, itemIndex) =>
        itemIndex === index ? { ...projection, ...update } : projection,
      ),
    )
  }

  function addProjection() {
    const component = topology.model.components[0]
    const port = component ? componentPorts(component.id)[0] : undefined
    setProjections((current) => [
      ...current,
      {
        id: projectionId(current.length),
        scope: 'port_derived',
        component_id: component?.id ?? '',
        port_name: port?.name ?? '',
        value_name: 'T',
        dimension: 'temperature',
        aggregation: 'final',
      },
    ])
  }

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
        schema_version: 'thermox.run_configuration.create/v2',
        run_configuration_id: configurationId.trim(),
        parent_run_configuration_revision_id:
          base?.run_configuration_revision_id ?? '',
        model_revision_id: base?.model_revision_id ?? modelRevisionId,
        case_revision_id: base?.case_revision_id ?? caseRevision.case_revision_id,
        artifact_revision_ids:
          base?.artifact_revision_ids ?? artifactRevisionIds,
        steady_solver: steadySolver,
        transient_solver: transientSolver,
        result_projections: projections,
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
            <span>Topology</span>
            <code>{base?.model_revision_id ?? modelRevisionId}</code>
          </div>
          <div>
            <span>Case</span>
            <code>{base?.case_revision_id ?? caseRevision.case_revision_id}</code>
          </div>
          <div>
            <span>Artifacts</span>
            <strong>
              {(base?.artifact_revision_ids ?? artifactRevisionIds).length}
            </strong>
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

        <fieldset>
          <legend>Result projections</legend>
          <div className="projection-editor">
            {!projections.length && (
              <p>No result summary projections. Full graph results remain available.</p>
            )}
            {projections.map((projection, index) => (
              <div className="projection-row" key={`${projection.id}-${index}`}>
                <input
                  aria-label="Projection ID"
                  value={projection.id}
                  placeholder="Projection ID"
                  onChange={(event) =>
                    updateProjection(index, { id: event.target.value })
                  }
                />
                <select
                  aria-label="Projection scope"
                  value={projection.scope}
                  onChange={(event) => {
                    const scope = event.target.value as ResultValueScope
                    updateProjection(index, {
                      scope,
                      component_id: scopeRequiresComponent(scope)
                        ? projection.component_id ||
                          topology.model.components[0]?.id ||
                          ''
                        : '',
                      port_name: scopeRequiresPort(scope)
                        ? projection.port_name
                        : '',
                    })
                  }}
                >
                  {scopes.map((scope) => (
                    <option key={scope} value={scope}>
                      {scope}
                    </option>
                  ))}
                </select>
                {scopeRequiresComponent(projection.scope) && (
                  <select
                    aria-label="Projection component"
                    value={projection.component_id}
                    onChange={(event) =>
                      updateProjection(index, {
                        component_id: event.target.value,
                        port_name: scopeRequiresPort(projection.scope)
                          ? componentPorts(event.target.value)[0]?.name ?? ''
                          : '',
                      })
                    }
                  >
                    {topology.model.components.map((component) => (
                      <option key={component.id} value={component.id}>
                        {component.id}
                      </option>
                    ))}
                  </select>
                )}
                {scopeRequiresPort(projection.scope) && (
                  <select
                    aria-label="Projection port"
                    value={projection.port_name}
                    onChange={(event) =>
                      updateProjection(index, {
                        port_name: event.target.value,
                      })
                    }
                  >
                    {componentPorts(projection.component_id).map((port) => (
                      <option key={port.name} value={port.name}>
                        {port.name}
                      </option>
                    ))}
                  </select>
                )}
                <input
                  aria-label="Projection value"
                  value={projection.value_name}
                  placeholder="Value name"
                  onChange={(event) =>
                    updateProjection(index, {
                      value_name: event.target.value,
                    })
                  }
                />
                <input
                  aria-label="Projection dimension"
                  value={projection.dimension}
                  placeholder="Dimension"
                  onChange={(event) =>
                    updateProjection(index, {
                      dimension: event.target.value,
                    })
                  }
                />
                <select
                  aria-label="Projection aggregation"
                  value={projection.aggregation}
                  disabled={mode === 'steady'}
                  onChange={(event) =>
                    updateProjection(index, {
                      aggregation: event.target.value as ResultAggregation,
                    })
                  }
                >
                  <option value="final">final</option>
                  <option value="minimum">minimum</option>
                  <option value="maximum">maximum</option>
                </select>
                <button
                  type="button"
                  className="projection-remove"
                  onClick={() =>
                    setProjections((current) =>
                      current.filter((_, itemIndex) => itemIndex !== index),
                    )
                  }
                >
                  ×
                </button>
              </div>
            ))}
            <button
              type="button"
              className="secondary-button projection-add"
              onClick={addProjection}
            >
              + Result projection
            </button>
          </div>
        </fieldset>

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
