import { useMemo, useState, type FormEvent } from 'react'
import type {
  ArtifactRevision,
  BalanceUncertaintyModel,
  Catalog,
  TopologyDocument,
} from './types'

interface BalanceUncertaintyArtifactFormProps {
  topology: TopologyDocument
  catalog: Catalog
  artifactRevisions: ArtifactRevision[]
  base?: { source: ArtifactRevision; definition: BalanceUncertaintyModel }
  onCancel: () => void
  onSubmit: (
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: BalanceUncertaintyModel,
  ) => Promise<void>
}

type StreamDraft = {
  component_id: string
  port_name: string
  mass: string
  enthalpy: string
  energy: string
  correlation: string
}

type CorrelationDraft = {
  quantity: 'mass_flow' | 'energy_flow'
  first: string
  second: string
  coefficient: string
}

type Endpoint = {
  key: string
  component_id: string
  port_name: string
  domain: string
}

function endpointKey(componentId: string, portName: string) {
  return `${componentId}.${portName}`
}

export function boundaryEndpoints(
  topology: TopologyDocument,
  catalog: Catalog,
): Endpoint[] {
  const connectedDirections = new Map<string, 'in' | 'out'>()
  for (const connection of topology.model.connections) {
    connectedDirections.set(connection.from, 'out')
    connectedDirections.set(connection.to, 'in')
  }
  return topology.model.components.flatMap((component) => {
    const descriptor = catalog.components.find(
      (candidate) => candidate.kind === component.kind,
    )
    const connected = (descriptor?.ports ?? [])
      .map((port) => ({
        key: endpointKey(component.id, port.name),
        component_id: component.id,
        port_name: port.name,
        domain: port.domain,
        direction: connectedDirections.get(endpointKey(component.id, port.name)),
      }))
      .filter((port) => port.direction !== undefined)
    const directions = new Set(connected.map((port) => port.direction))
    return directions.size === 1
      ? connected.map(({ direction: _direction, ...port }) => port)
      : []
  })
}

function draftStream(
  stream: BalanceUncertaintyModel['streams'][number],
): StreamDraft {
  return {
    component_id: stream.component_id,
    port_name: stream.port_name,
    mass: stream.mass_flow_standard_uncertainty_si === null
      ? '' : String(stream.mass_flow_standard_uncertainty_si),
    enthalpy: stream.specific_enthalpy_standard_uncertainty_si === null
      ? '' : String(stream.specific_enthalpy_standard_uncertainty_si),
    energy: stream.energy_flow_standard_uncertainty_si === null
      ? '' : String(stream.energy_flow_standard_uncertainty_si),
    correlation: String(stream.mass_enthalpy_correlation),
  }
}

function optionalNumber(value: string): number | null {
  return value.trim() === '' ? null : Number(value)
}

export function BalanceUncertaintyArtifactForm({
  topology,
  catalog,
  artifactRevisions,
  base,
  onCancel,
  onSubmit,
}: BalanceUncertaintyArtifactFormProps) {
  const endpoints = useMemo(
    () => boundaryEndpoints(topology, catalog),
    [catalog, topology],
  )
  const ids = useMemo(
    () => new Set(artifactRevisions
      .filter((revision) =>
        revision.artifact_type === 'thermox.balance_uncertainty')
      .map((revision) => revision.artifact_id)),
    [artifactRevisions],
  )
  const [artifactId, setArtifactId] = useState(
    base?.source.artifact_id ?? 'boundary-metering',
  )
  const [sourceReference, setSourceReference] = useState(
    base?.definition.source.reference ?? '',
  )
  const [sourceChecksum, setSourceChecksum] = useState(
    base?.definition.source.checksum_sha256 ?? '',
  )
  const [note, setNote] = useState(base?.definition.source.note ?? '')
  const [limitations, setLimitations] = useState(
    base?.definition.source.limitations.join('\n') ?? '',
  )
  const [streams, setStreams] = useState<StreamDraft[]>(
    base
      ? base.definition.streams.map(draftStream)
      : endpoints.map((endpoint) => ({
          component_id: endpoint.component_id,
          port_name: endpoint.port_name,
          mass: '',
          enthalpy: '',
          energy: '',
          correlation: '0',
        })),
  )
  const [correlations, setCorrelations] = useState<CorrelationDraft[]>(
    base?.definition.correlations.map((correlation) => ({
      quantity: correlation.quantity,
      first: endpointKey(
        correlation.first.component_id, correlation.first.port_name,
      ),
      second: endpointKey(
        correlation.second.component_id, correlation.second.port_name,
      ),
      coefficient: String(correlation.coefficient),
    })) ?? [],
  )
  const [submitting, setSubmitting] = useState(false)
  const [error, setError] = useState('')

  const declaredEndpointKeys = streams.map((stream) =>
    endpointKey(stream.component_id, stream.port_name))

  const addStream = () => {
    const unused = endpoints.find((endpoint) =>
      !declaredEndpointKeys.includes(endpoint.key))
    if (!unused) return
    setStreams((current) => [...current, {
      component_id: unused.component_id,
      port_name: unused.port_name,
      mass: '', enthalpy: '', energy: '', correlation: '0',
    }])
  }

  async function submit(event: FormEvent) {
    event.preventDefault()
    setError('')
    try {
      const id = artifactId.trim()
      if (!id) throw new Error('Artifact ID is required.')
      if (!base && ids.has(id)) {
        throw new Error('That artifact ID exists. Revise its latest revision instead.')
      }
      if (!sourceReference.trim()) throw new Error('Source reference is required.')
      if (!/^[0-9a-f]{64}$/.test(sourceChecksum.trim())) {
        throw new Error('Source checksum must be a lowercase 64-character SHA-256 digest.')
      }
      if (!streams.length) throw new Error('Declare at least one boundary stream.')
      const definitionStreams = streams.map((stream) => {
        const mass = optionalNumber(stream.mass)
        const enthalpy = optionalNumber(stream.enthalpy)
        const energy = optionalNumber(stream.energy)
        const rho = Number(stream.correlation)
        if ([mass, enthalpy, energy].every((value) => value === null)) {
          throw new Error(`${endpointKey(stream.component_id, stream.port_name)} needs an uncertainty.`)
        }
        for (const value of [mass, enthalpy, energy]) {
          if (value !== null && (!Number.isFinite(value) || value < 0)) {
            throw new Error('Standard uncertainties must be finite and non-negative.')
          }
        }
        if (enthalpy !== null && mass === null) {
          throw new Error('Specific-enthalpy propagation also requires mass-flow uncertainty.')
        }
        if (enthalpy !== null && energy !== null) {
          throw new Error('Use direct energy uncertainty or mass/enthalpy propagation, not both.')
        }
        if (!Number.isFinite(rho) || Math.abs(rho) > 1) {
          throw new Error('Within-stream correlation must be in [-1, 1].')
        }
        return {
          component_id: stream.component_id,
          port_name: stream.port_name,
          mass_flow_standard_uncertainty_si: mass,
          specific_enthalpy_standard_uncertainty_si: enthalpy,
          energy_flow_standard_uncertainty_si: energy,
          mass_enthalpy_correlation: rho,
        }
      })
      const unique = new Set(declaredEndpointKeys)
      if (unique.size !== declaredEndpointKeys.length) {
        throw new Error('Each boundary endpoint may be declared only once.')
      }
      const definitionCorrelations = correlations.map((correlation) => {
        const coefficient = Number(correlation.coefficient)
        if (!unique.has(correlation.first) || !unique.has(correlation.second) ||
          correlation.first === correlation.second) {
          throw new Error('Correlation endpoints must be distinct declared streams.')
        }
        if (!Number.isFinite(coefficient) || Math.abs(coefficient) > 1) {
          throw new Error('Cross-stream correlation must be in [-1, 1].')
        }
        const split = (key: string) => {
          const separator = key.lastIndexOf('.')
          return { component_id: key.slice(0, separator), port_name: key.slice(separator + 1) }
        }
        return {
          quantity: correlation.quantity,
          first: split(correlation.first),
          second: split(correlation.second),
          coefficient,
        }
      })
      const definition: BalanceUncertaintyModel = {
        schema_version: 'thermox.balance_uncertainty/v1',
        id,
        source: {
          reference: sourceReference.trim(),
          checksum_sha256: sourceChecksum.trim(),
          note: note.trim(),
          limitations: limitations.split('\n').map((item) => item.trim()).filter(Boolean),
        },
        streams: definitionStreams,
        correlations: definitionCorrelations,
      }
      setSubmitting(true)
      await onSubmit(
        id, base?.source.artifact_revision_id ?? '', definition,
      )
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : 'Metrology artifact was rejected.')
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form className="component-dialog uncertainty-dialog" onSubmit={submit}>
        <header>
          <div>
            <span className="eyebrow">Metrology registry</span>
            <h2>{base ? 'Revise boundary uncertainty' : 'Publish boundary uncertainty'}</h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>×</button>
        </header>
        <p className="form-note">
          Declare one-standard-deviation values in canonical SI units. The server
          verifies source identity, boundary coverage, and covariance consistency.
        </p>
        <div className="form-grid">
          <label><span>Artifact ID</span><input required disabled={Boolean(base)}
            value={artifactId} onChange={(event) => setArtifactId(event.target.value)} /></label>
          <label><span>Controlled source reference</span><input required
            value={sourceReference} onChange={(event) => setSourceReference(event.target.value)} /></label>
          <label className="form-span"><span>Source SHA-256 (without sha256: prefix)</span><input
            required pattern="[0-9a-f]{64}" value={sourceChecksum}
            onChange={(event) => setSourceChecksum(event.target.value)} /></label>
          <label className="form-span"><span>Source note</span><input value={note}
            onChange={(event) => setNote(event.target.value)} /></label>
          <label className="form-span"><span>Known limitations (one per line)</span><textarea
            value={limitations} onChange={(event) => setLimitations(event.target.value)} /></label>
        </div>
        <fieldset>
          <legend>Boundary stream standard uncertainties</legend>
          {!endpoints.length && (
            <p className="form-note acceptance-note">
              No connected one-direction system boundary was found. Add source
              and sink boundary components before publishing metrology.
            </p>
          )}
          <div className="uncertainty-grid uncertainty-grid-header">
            <span>Endpoint</span><span>u(ṁ)</span><span>u(h)</span><span>u(Ė)</span><span>ρ(ṁ,h)</span><span />
          </div>
          {streams.map((stream, index) => (
            <div className="uncertainty-grid" key={`${endpointKey(stream.component_id, stream.port_name)}-${index}`}>
              <select aria-label="Boundary endpoint"
                value={endpointKey(stream.component_id, stream.port_name)}
                onChange={(event) => {
                  const selected = endpoints.find((item) => item.key === event.target.value)
                  if (selected) setStreams((current) => current.map((item, itemIndex) =>
                    itemIndex === index ? { ...item, component_id: selected.component_id, port_name: selected.port_name } : item))
                }}>
                {endpoints.map((endpoint) => <option key={endpoint.key} value={endpoint.key}>
                  {endpoint.key} · {endpoint.domain}
                </option>)}
              </select>
              {(['mass', 'enthalpy', 'energy', 'correlation'] as const).map((field) => (
                <input key={field} aria-label={`${field} uncertainty`} type="number" step="any"
                  min={field === 'correlation' ? -1 : 0} max={field === 'correlation' ? 1 : undefined}
                  placeholder={field === 'correlation' ? '0' : 'optional'} value={stream[field]}
                  onChange={(event) => setStreams((current) => current.map((item, itemIndex) =>
                    itemIndex === index ? { ...item, [field]: event.target.value } : item))} />
              ))}
              <button type="button" className="projection-remove"
                onClick={() => setStreams((current) => current.filter((_, item) => item !== index))}>×</button>
            </div>
          ))}
          <button type="button" className="secondary-button" disabled={streams.length >= endpoints.length}
            onClick={addStream}>+ Boundary stream</button>
        </fieldset>
        <fieldset>
          <legend>Cross-stream correlations</legend>
          {correlations.map((correlation, index) => (
            <div className="uncertainty-correlation-row" key={index}>
              <select value={correlation.quantity} onChange={(event) => setCorrelations((current) =>
                current.map((item, itemIndex) => itemIndex === index
                  ? { ...item, quantity: event.target.value as CorrelationDraft['quantity'] } : item))}>
                <option value="mass_flow">mass flow</option><option value="energy_flow">energy flow</option>
              </select>
              <select value={correlation.first} onChange={(event) => setCorrelations((current) =>
                current.map((item, itemIndex) => itemIndex === index ? { ...item, first: event.target.value } : item))}>
                {declaredEndpointKeys.map((key) => <option key={key}>{key}</option>)}
              </select>
              <select value={correlation.second} onChange={(event) => setCorrelations((current) =>
                current.map((item, itemIndex) => itemIndex === index ? { ...item, second: event.target.value } : item))}>
                {declaredEndpointKeys.map((key) => <option key={key}>{key}</option>)}
              </select>
              <input aria-label="Correlation coefficient" type="number" min="-1" max="1" step="any"
                value={correlation.coefficient} onChange={(event) => setCorrelations((current) =>
                  current.map((item, itemIndex) => itemIndex === index ? { ...item, coefficient: event.target.value } : item))} />
              <button type="button" className="projection-remove"
                onClick={() => setCorrelations((current) => current.filter((_, item) => item !== index))}>×</button>
            </div>
          ))}
          <button type="button" className="secondary-button" disabled={streams.length < 2}
            onClick={() => setCorrelations((current) => [...current, {
              quantity: 'mass_flow', first: declaredEndpointKeys[0] ?? '',
              second: declaredEndpointKeys[1] ?? '', coefficient: '0',
            }])}>+ Correlation</button>
        </fieldset>
        {error && <div className="form-error">{error}</div>}
        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>Cancel</button>
          <button type="submit" className="primary-button" disabled={submitting || !endpoints.length}>
            {submitting ? 'Publishing…' : 'Publish immutable revision'}
          </button>
        </footer>
      </form>
    </div>
  )
}
