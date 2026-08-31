import { useState, type FormEvent } from 'react'
import {
  scopeRequiresComponent,
  scopeRequiresPort,
} from './runAuthoring'
import type {
  ArtifactRevision,
  Catalog,
  EngineeringAcceptanceCriterion,
  ResultAggregation,
  ResultProjection,
  ResultValueScope,
  StudyRevision,
  StudyTrajectoryValidationBinding,
  TopologyDocument,
  ValidationSeriesCatalogEntry,
} from './types'

interface StudyPublishFormProps {
  topology: TopologyDocument
  catalog: Catalog
  base?: StudyRevision
  transient: boolean
  validationSeries: ValidationSeriesCatalogEntry[]
  artifactRevisions: ArtifactRevision[]
  onCancel: () => void
  onSubmit: (
    projections: ResultProjection[],
    acceptanceCriteria: EngineeringAcceptanceCriterion[],
    trajectoryBindings: StudyTrajectoryValidationBinding[],
    balanceUncertaintyArtifactRevisionId: string,
  ) => Promise<void>
}

const scopes: ResultValueScope[] = [
  'system_balance',
  'kpi',
  'component_metric',
  'component_internal',
  'port_primary',
  'port_derived',
]

export function StudyPublishForm({
  topology,
  catalog,
  base,
  transient,
  validationSeries,
  artifactRevisions,
  onCancel,
  onSubmit,
}: StudyPublishFormProps) {
  const [projections, setProjections] = useState<ResultProjection[]>(
    base?.result_projections ?? [],
  )
  const [acceptanceCriteria, setAcceptanceCriteria] = useState<
    EngineeringAcceptanceCriterion[]
  >(base?.acceptance_criteria ?? [])
  const [trajectoryBindings, setTrajectoryBindings] = useState<
    StudyTrajectoryValidationBinding[]
  >(base?.trajectory_validation_bindings ?? [])
  const balanceUncertaintyRevisions = artifactRevisions.filter(
    (revision) => revision.artifact_type === 'thermox.balance_uncertainty',
  )
  const [balanceUncertaintyArtifactRevisionId,
    setBalanceUncertaintyArtifactRevisionId] = useState(
    base?.artifact_revision_ids.find((revisionId) =>
      balanceUncertaintyRevisions.some((revision) =>
        revision.artifact_revision_id === revisionId)) ?? '',
  )
  const [submitting, setSubmitting] = useState(false)
  const [error, setError] = useState('')

  const ports = (componentId: string) => {
    const kind = topology.model.components.find(
      (component) => component.id === componentId,
    )?.kind
    return catalog.components.find((component) => component.kind === kind)
      ?.ports ?? []
  }
  const update = (index: number, value: Partial<ResultProjection>) =>
    setProjections((current) =>
      current.map((projection, item) =>
        item === index ? { ...projection, ...value } : projection,
      ),
    )
  const add = () => {
    const component = topology.model.components[0]
    setProjections((current) => [
      ...current,
      {
        id: `result_${current.length + 1}`,
        scope: 'port_derived',
        component_id: component?.id ?? '',
        port_name: component ? ports(component.id)[0]?.name ?? '' : '',
        value_name: 'T',
        dimension: 'temperature',
        aggregation: 'final',
      },
    ])
  }
  const updateCriterion = (
    index: number,
    value: Partial<EngineeringAcceptanceCriterion>,
  ) => setAcceptanceCriteria((current) =>
    current.map((criterion, item) =>
      item === index ? { ...criterion, ...value } : criterion,
    ),
  )
  const addCriterion = () => {
    const projection = projections[0]
    setAcceptanceCriteria((current) => [
      ...current,
      {
        id: `acceptance_${current.length + 1}`,
        projection_id: projection?.id ?? '',
        dimension: projection?.dimension ?? '',
        lower_bound_si: null,
        upper_bound_si: null,
        lower_inclusive: true,
        upper_inclusive: true,
      },
    ])
  }
  const updateBinding = (
    index: number,
    value: Partial<StudyTrajectoryValidationBinding>,
  ) => setTrajectoryBindings((current) => current.map(
    (binding, item) => item === index ? { ...binding, ...value } : binding,
  ))
  const addBinding = () => {
    const evidence = validationSeries.find((item) =>
      item.definition.signals.some((candidate) => projections.some(
        (projection) => !projection.window &&
          projection.dimension === candidate.dimension,
      )),
    )
    const signal = evidence?.definition.signals.find((candidate) =>
      projections.some((projection) => !projection.window &&
        projection.dimension === candidate.dimension),
    )
    const projection = projections.find(
      (item) => !item.window && item.dimension === signal?.dimension,
    )
    setTrajectoryBindings((current) => [...current, {
      id: `trajectory_${current.length + 1}`,
      artifact_revision_id: evidence?.source.artifact_revision_id ?? '',
      signal_id: signal?.id ?? '',
      projection_id: projection?.id ?? '',
      comparison: 'absolute',
      time_offset_si: 0,
      baseline_time_si: 0,
      absolute_tolerance_si: 0,
      relative_tolerance: 0,
      uncertainty_multiplier: 0,
      maximum_interpolation_gap_si: 0,
    }])
  }
  const submit = async (event: FormEvent) => {
    event.preventDefault()
    setSubmitting(true)
    setError('')
    try {
      await onSubmit(
        projections,
        acceptanceCriteria,
        trajectoryBindings,
        balanceUncertaintyArtifactRevisionId,
      )
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : 'Study was rejected.')
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form className="component-dialog run-config-dialog" onSubmit={submit}>
        <header>
          <div>
            <span className="eyebrow">Immutable engineering intent</span>
            <h2>{base ? 'Publish Study revision' : 'Publish Study'}</h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>×</button>
        </header>
        <p className="form-note">
          The Study pins the validated physical inputs and declares the outputs
          the calculation should retain and analyze.
        </p>
        <fieldset>
          <legend>Balance metrology evidence</legend>
          <p className="form-note acceptance-note">
            Optionally pin one immutable boundary-uncertainty revision. Balance
            reports resolve it automatically; it does not alter solver equations.
          </p>
          <label>
            <span>Boundary uncertainty revision</span>
            <select value={balanceUncertaintyArtifactRevisionId}
              onChange={(event) =>
                setBalanceUncertaintyArtifactRevisionId(event.target.value)}>
              <option value="">None</option>
              {balanceUncertaintyRevisions.map((revision) => (
                <option key={revision.artifact_revision_id}
                  value={revision.artifact_revision_id}>
                  {revision.artifact_id} · r{revision.revision_number}
                </option>
              ))}
            </select>
          </label>
        </fieldset>
        <fieldset>
          <legend>Result projections</legend>
          <div className="projection-editor">
            {!projections.length && <p>No summary outputs selected.</p>}
            {projections.map((projection, index) => (
              <div className="projection-row" key={`${projection.id}-${index}`}>
                <input aria-label="Projection ID" value={projection.id}
                  onChange={(event) => update(index, { id: event.target.value })} />
                <select aria-label="Projection scope" value={projection.scope}
                  onChange={(event) => {
                    const scope = event.target.value as ResultValueScope
                    update(index, {
                      scope,
                      component_id: scopeRequiresComponent(scope)
                        ? projection.component_id || topology.model.components[0]?.id || ''
                        : '',
                      port_name: scopeRequiresPort(scope) ? projection.port_name : '',
                    })
                  }}>
                  {scopes.map((scope) => <option key={scope}>{scope}</option>)}
                </select>
                {scopeRequiresComponent(projection.scope) && (
                  <select aria-label="Projection component"
                    value={projection.component_id}
                    onChange={(event) => update(index, {
                      component_id: event.target.value,
                      port_name: scopeRequiresPort(projection.scope)
                        ? ports(event.target.value)[0]?.name ?? '' : '',
                    })}>
                    {topology.model.components.map((component) => (
                      <option key={component.id}>{component.id}</option>
                    ))}
                  </select>
                )}
                {scopeRequiresPort(projection.scope) && (
                  <select aria-label="Projection port" value={projection.port_name}
                    onChange={(event) => update(index, { port_name: event.target.value })}>
                    {ports(projection.component_id).map((port) => (
                      <option key={port.name}>{port.name}</option>
                    ))}
                  </select>
                )}
                <input aria-label="Projection value" value={projection.value_name}
                  placeholder="Value" onChange={(event) =>
                    update(index, { value_name: event.target.value })} />
                <input aria-label="Projection dimension" value={projection.dimension}
                  placeholder="Dimension" onChange={(event) =>
                    update(index, { dimension: event.target.value })} />
                <select aria-label="Projection aggregation"
                  value={projection.aggregation} disabled={!transient}
                  onChange={(event) => update(index, {
                    aggregation: event.target.value as ResultAggregation,
                  })}>
                  <option value="final">final</option>
                  <option value="minimum">minimum</option>
                  <option value="maximum">maximum</option>
                  <option value="mean">mean</option>
                  <option value="root_mean_square">root mean square</option>
                  <option value="change">change over window</option>
                </select>
                {transient && (
                  <label className="projection-window-toggle">
                    <input type="checkbox"
                      checked={projection.window !== undefined}
                      onChange={(event) => update(index, {
                        window: event.target.checked ? {
                          anchor: 'simulation',
                          start_time: 0,
                          end_time: 1,
                          event_name: '',
                          event_occurrence: 0,
                        } : undefined,
                      })} />
                    window
                  </label>
                )}
                {transient && projection.window && (
                  <>
                    <select aria-label="Projection window anchor"
                      value={projection.window.anchor}
                      onChange={(event) => update(index, {
                        window: {
                          ...projection.window!,
                          anchor: event.target.value as 'simulation' | 'event',
                          event_name: event.target.value === 'event'
                            ? projection.window!.event_name
                            : '',
                          event_occurrence: event.target.value === 'event'
                            ? projection.window!.event_occurrence
                            : 0,
                        },
                      })}>
                      <option value="simulation">simulation time</option>
                      <option value="event">event relative</option>
                    </select>
                    <input aria-label="Projection window start" type="number"
                      step="any" value={projection.window.start_time}
                      onChange={(event) => update(index, { window: {
                        ...projection.window!, start_time: Number(event.target.value),
                      } })} />
                    <input aria-label="Projection window end" type="number"
                      step="any" value={projection.window.end_time}
                      onChange={(event) => update(index, { window: {
                        ...projection.window!, end_time: Number(event.target.value),
                      } })} />
                    {projection.window.anchor === 'event' && (
                      <>
                        <input aria-label="Projection window event"
                          value={projection.window.event_name}
                          placeholder="Event name"
                          onChange={(event) => update(index, { window: {
                            ...projection.window!, event_name: event.target.value,
                          } })} />
                        <input aria-label="Projection window occurrence"
                          type="number" min="0" step="1"
                          title="Zero-based event occurrence"
                          value={projection.window.event_occurrence}
                          onChange={(event) => update(index, { window: {
                            ...projection.window!,
                            event_occurrence: Number(event.target.value),
                          } })} />
                      </>
                    )}
                  </>
                )}
                <button type="button" className="projection-remove"
                  onClick={() => setProjections((current) =>
                    current.filter((_, item) => item !== index))}>×</button>
              </div>
            ))}
            <button type="button" className="secondary-button projection-add"
              onClick={add}>+ Result projection</button>
          </div>
        </fieldset>
        <fieldset>
          <legend>Engineering acceptance</legend>
          <p className="form-note acceptance-note">
            Optional canonical-SI bounds are evaluated after a converged run.
            They do not change numerical solver status.
          </p>
          <div className="projection-editor">
            {!acceptanceCriteria.length && (
              <p>No engineering acceptance criteria configured.</p>
            )}
            {acceptanceCriteria.map((criterion, index) => (
              <div className="acceptance-row" key={`${criterion.id}-${index}`}>
                <input aria-label="Acceptance criterion ID" value={criterion.id}
                  placeholder="Criterion ID" onChange={(event) =>
                    updateCriterion(index, { id: event.target.value })} />
                <select aria-label="Acceptance projection"
                  value={criterion.projection_id}
                  onChange={(event) => {
                    const projection = projections.find(
                      (item) => item.id === event.target.value,
                    )
                    updateCriterion(index, {
                      projection_id: event.target.value,
                      dimension: projection?.dimension ?? '',
                    })
                  }}>
                  {!projections.length && <option value="">No projection</option>}
                  {projections.map((projection) => (
                    <option value={projection.id} key={projection.id}>
                      {projection.id}
                    </option>
                  ))}
                </select>
                <input aria-label="Acceptance dimension" value={criterion.dimension}
                  readOnly placeholder="Dimension" />
                <input aria-label="Acceptance lower bound SI" type="number" step="any"
                  value={criterion.lower_bound_si ?? ''} placeholder="Lower bound (SI)"
                  onChange={(event) => updateCriterion(index, {
                    lower_bound_si: event.target.value === ''
                      ? null : Number(event.target.value),
                  })} />
                <input aria-label="Acceptance upper bound SI" type="number" step="any"
                  value={criterion.upper_bound_si ?? ''} placeholder="Upper bound (SI)"
                  onChange={(event) => updateCriterion(index, {
                    upper_bound_si: event.target.value === ''
                      ? null : Number(event.target.value),
                  })} />
                <label className="acceptance-inclusive">
                  <input type="checkbox" checked={criterion.lower_inclusive}
                    onChange={(event) => updateCriterion(index, {
                      lower_inclusive: event.target.checked,
                    })} /> lower inclusive
                </label>
                <label className="acceptance-inclusive">
                  <input type="checkbox" checked={criterion.upper_inclusive}
                    onChange={(event) => updateCriterion(index, {
                      upper_inclusive: event.target.checked,
                    })} /> upper inclusive
                </label>
                <button type="button" className="projection-remove"
                  aria-label="Remove acceptance criterion"
                  onClick={() => setAcceptanceCriteria((current) =>
                    current.filter((_, item) => item !== index))}>×</button>
              </div>
            ))}
            <button type="button" className="secondary-button projection-add"
              disabled={!projections.length} onClick={addCriterion}>
              + Acceptance criterion
            </button>
          </div>
        </fieldset>
        <fieldset>
          <legend>Reference trajectory validation</legend>
          <p className="form-note acceptance-note">
            Bind immutable external signals to graph outputs. Policies affect
            validation evidence only; they never tune the physical model.
          </p>
          {!transient && <p>Trajectory validation requires a transient case.</p>}
          {transient && !validationSeries.length && (
            <p>Import validation data before adding a trajectory binding.</p>
          )}
          <div className="projection-editor">
            {trajectoryBindings.map((binding, index) => {
              const evidence = validationSeries.find(
                (item) => item.source.artifact_revision_id ===
                  binding.artifact_revision_id,
              )
              const signal = evidence?.definition.signals.find(
                (item) => item.id === binding.signal_id,
              )
              const compatibleProjections = projections.filter(
                (projection) => !projection.window &&
                  projection.dimension === signal?.dimension,
              )
              return (
                <div className="acceptance-row" key={`${binding.id}-${index}`}>
                  <input aria-label="Trajectory binding ID" value={binding.id}
                    onChange={(event) => updateBinding(index, { id: event.target.value })} />
                  <select aria-label="Validation data revision"
                    value={binding.artifact_revision_id}
                    onChange={(event) => {
                      const selected = validationSeries.find(
                        (item) => item.source.artifact_revision_id === event.target.value,
                      )
                      const selectedSignal = selected?.definition.signals[0]
                      updateBinding(index, {
                        artifact_revision_id: event.target.value,
                        signal_id: selectedSignal?.id ?? '',
                        projection_id: projections.find(
                          (projection) => !projection.window &&
                            projection.dimension === selectedSignal?.dimension,
                        )?.id ?? '',
                      })
                    }}>
                    {validationSeries.map((item) => (
                      <option value={item.source.artifact_revision_id}
                        key={item.source.artifact_revision_id}>
                        {item.source.artifact_id} r{item.source.revision_number}
                      </option>
                    ))}
                  </select>
                  <select aria-label="Validation signal" value={binding.signal_id}
                    onChange={(event) => {
                      const selectedSignal = evidence?.definition.signals.find(
                        (item) => item.id === event.target.value,
                      )
                      updateBinding(index, {
                        signal_id: event.target.value,
                        projection_id: projections.find(
                          (projection) => !projection.window &&
                            projection.dimension === selectedSignal?.dimension,
                        )?.id ?? '',
                      })
                    }}>
                    {(evidence?.definition.signals ?? []).map((signal) => (
                      <option value={signal.id} key={signal.id}>
                        {signal.id} · {signal.dimension}
                      </option>
                    ))}
                  </select>
                  <select aria-label="Validation projection" value={binding.projection_id}
                    onChange={(event) => updateBinding(index, {
                      projection_id: event.target.value,
                    })}>
                    {!compatibleProjections.length && (
                      <option value="">No dimension-compatible projection</option>
                    )}
                    {compatibleProjections.map((projection) => (
                      <option value={projection.id} key={projection.id}>
                        {projection.id} · {projection.dimension}
                      </option>
                    ))}
                  </select>
                  <select aria-label="Trajectory comparison" value={binding.comparison}
                    onChange={(event) => updateBinding(index, {
                      comparison: event.target.value as 'absolute' | 'projected_change',
                    })}>
                    <option value="absolute">absolute value</option>
                    <option value="projected_change">change from baseline</option>
                  </select>
                  <input aria-label="Time offset SI" type="number" step="any"
                    title="Reference-to-simulation time offset in seconds"
                    value={binding.time_offset_si} onChange={(event) =>
                      updateBinding(index, { time_offset_si: Number(event.target.value) })} />
                  <input aria-label="Baseline time SI" type="number" step="any"
                    title="Simulation baseline time in seconds"
                    value={binding.baseline_time_si} disabled={binding.comparison === 'absolute'}
                    onChange={(event) => updateBinding(index, {
                      baseline_time_si: Number(event.target.value),
                    })} />
                  <input aria-label="Absolute tolerance SI" type="number" min="0" step="any"
                    value={binding.absolute_tolerance_si} onChange={(event) =>
                      updateBinding(index, { absolute_tolerance_si: Number(event.target.value) })} />
                  <input aria-label="Relative tolerance" type="number" min="0" step="any"
                    value={binding.relative_tolerance} onChange={(event) =>
                      updateBinding(index, { relative_tolerance: Number(event.target.value) })} />
                  <input aria-label="Uncertainty multiplier" type="number" min="0" step="any"
                    value={binding.uncertainty_multiplier} onChange={(event) =>
                      updateBinding(index, { uncertainty_multiplier: Number(event.target.value) })} />
                  <input aria-label="Maximum interpolation gap SI" type="number" min="0" step="any"
                    value={binding.maximum_interpolation_gap_si} onChange={(event) =>
                      updateBinding(index, {
                        maximum_interpolation_gap_si: Number(event.target.value),
                      })} />
                  <button type="button" className="projection-remove"
                    aria-label="Remove trajectory binding"
                    onClick={() => setTrajectoryBindings((current) =>
                      current.filter((_, item) => item !== index))}>×</button>
                </div>
              )
            })}
            <button type="button" className="secondary-button projection-add"
              disabled={!transient || !validationSeries.some((evidence) =>
                evidence.definition.signals.some((signal) => projections.some(
                  (projection) => !projection.window &&
                    projection.dimension === signal.dimension,
                )),
              )}
              onClick={addBinding}>+ Trajectory binding</button>
          </div>
        </fieldset>
        {error && <div className="form-error">{error}</div>}
        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>Cancel</button>
          <button type="submit" className="primary-button" disabled={submitting}>
            {submitting ? 'Publishing…' : 'Publish Study'}
          </button>
        </footer>
      </form>
    </div>
  )
}
