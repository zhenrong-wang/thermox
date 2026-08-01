import { useState, type FormEvent } from 'react'
import {
  scopeRequiresComponent,
  scopeRequiresPort,
} from './runAuthoring'
import type {
  Catalog,
  ResultAggregation,
  ResultProjection,
  ResultValueScope,
  StudyRevision,
  TopologyDocument,
} from './types'

interface StudyPublishFormProps {
  topology: TopologyDocument
  catalog: Catalog
  base?: StudyRevision
  transient: boolean
  onCancel: () => void
  onSubmit: (projections: ResultProjection[]) => Promise<void>
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
  onCancel,
  onSubmit,
}: StudyPublishFormProps) {
  const [projections, setProjections] = useState<ResultProjection[]>(
    base?.result_projections ?? [],
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
  const submit = async (event: FormEvent) => {
    event.preventDefault()
    setSubmitting(true)
    setError('')
    try {
      await onSubmit(projections)
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
                </select>
                <button type="button" className="projection-remove"
                  onClick={() => setProjections((current) =>
                    current.filter((_, item) => item !== index))}>×</button>
              </div>
            ))}
            <button type="button" className="secondary-button projection-add"
              onClick={add}>+ Result projection</button>
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
