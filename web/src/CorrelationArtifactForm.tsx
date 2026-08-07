import { useMemo, useState, type FormEvent } from 'react'
import type {
  ArtifactRevision,
  CatalogUnitDimension,
  CorrelationArtifactDefinition,
} from './types'

interface CorrelationArtifactFormProps {
  unitDimensions: CatalogUnitDimension[]
  artifactRevisions: ArtifactRevision[]
  base?: {
    source: ArtifactRevision
    definition: CorrelationArtifactDefinition
  }
  onCancel: () => void
  onSubmit: (
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: CorrelationArtifactDefinition,
  ) => Promise<void>
}

type VariableDraft = CorrelationArtifactDefinition['inputs'][number]
type CoefficientDraft = { name: string; value: string }
type ApplicabilityDraft = {
  input: string
  minimum: string
  maximum: string
  minimum_inclusive: boolean
  maximum_inclusive: boolean
}
type CandidateDraft = {
  id: string
  regime: string
  priority: string
  coefficients: CoefficientDraft[]
  expression: string
  applicability: ApplicabilityDraft[]
  flow_regimes: string
  fallback_for_unmapped_flow_regime: boolean
}

const identifierPattern = /^[A-Za-z_][A-Za-z0-9_]*$/

export function validateCorrelationDefinition(
  definition: CorrelationArtifactDefinition,
): string[] {
  const issues: string[] = []
  if (!definition.inputs.length) issues.push('Define at least one input.')
  const inputNames = new Set<string>()
  for (const input of definition.inputs) {
    if (!identifierPattern.test(input.name)) {
      issues.push(`Input "${input.name}" is not a valid expression identifier.`)
    }
    if (!input.dimension) issues.push(`Input "${input.name}" needs a dimension.`)
    if (inputNames.has(input.name)) issues.push(`Symbol "${input.name}" is duplicated.`)
    inputNames.add(input.name)
  }
  if (!identifierPattern.test(definition.output.name)) {
    issues.push('Output name must be a valid expression identifier.')
  }
  if (!definition.output.dimension) issues.push('Output needs a dimension.')
  if (!definition.candidates.length) issues.push('Define at least one candidate law.')

  const candidateIds = new Set<string>()
  for (const candidate of definition.candidates) {
    const label = candidate.id || '(unnamed)'
    if (!identifierPattern.test(candidate.id)) {
      issues.push(`Candidate "${label}" does not have a valid identifier.`)
    }
    if (candidateIds.has(candidate.id)) {
      issues.push(`Candidate "${label}" is duplicated.`)
    }
    candidateIds.add(candidate.id)
    if (!candidate.regime.trim()) issues.push(`Candidate "${label}" needs a regime.`)
    if (!Number.isInteger(candidate.priority)) {
      issues.push(`Candidate "${label}" priority must be an integer.`)
    }
    if (!candidate.expression.trim()) {
      issues.push(`Candidate "${label}" expression is required.`)
    }
    if (new Set(candidate.flow_regimes).size !== candidate.flow_regimes.length ||
      candidate.flow_regimes.some((regime) => !regime.trim())) {
      issues.push(`Candidate "${label}" has an empty or duplicate flow-regime route.`)
    }

    const symbols = new Set(inputNames)
    for (const [name, value] of Object.entries(candidate.coefficients)) {
      if (!identifierPattern.test(name)) {
        issues.push(`Coefficient "${name}" is not a valid expression identifier.`)
      }
      if (!Number.isFinite(value)) issues.push(`Coefficient "${name}" must be finite.`)
      if (symbols.has(name)) issues.push(`Symbol "${name}" is duplicated.`)
      symbols.add(name)
    }

    const rangedInputs = new Set<string>()
    for (const range of candidate.applicability ?? []) {
      if (!inputNames.has(range.input)) {
        issues.push(`Applicability input "${range.input}" is not declared.`)
      }
      if (rangedInputs.has(range.input)) {
        issues.push(`Applicability input "${range.input}" is duplicated.`)
      }
      rangedInputs.add(range.input)
      if (range.minimum === undefined && range.maximum === undefined) {
        issues.push(`Applicability input "${range.input}" needs a minimum or maximum.`)
      }
      if (range.minimum !== undefined && !Number.isFinite(range.minimum)) {
        issues.push(`Applicability minimum for "${range.input}" must be finite.`)
      }
      if (range.maximum !== undefined && !Number.isFinite(range.maximum)) {
        issues.push(`Applicability maximum for "${range.input}" must be finite.`)
      }
      if (
        range.minimum !== undefined && range.maximum !== undefined &&
        (range.minimum > range.maximum ||
          (range.minimum === range.maximum &&
            (!range.minimum_inclusive || !range.maximum_inclusive)))
      ) {
        issues.push(`Applicability range for "${range.input}" is empty.`)
      }
    }
  }
  return issues
}

function draftCandidate(
  candidate: CorrelationArtifactDefinition['candidates'][number],
): CandidateDraft {
  return {
    id: candidate.id,
    regime: candidate.regime,
    priority: String(candidate.priority),
    coefficients: Object.entries(candidate.coefficients).map(([name, value]) => ({
      name,
      value: String(value),
    })),
    expression: candidate.expression,
    applicability: (candidate.applicability ?? []).map((range) => ({
      input: range.input,
      minimum: range.minimum === undefined ? '' : String(range.minimum),
      maximum: range.maximum === undefined ? '' : String(range.maximum),
      minimum_inclusive: range.minimum_inclusive,
      maximum_inclusive: range.maximum_inclusive,
    })),
    flow_regimes: candidate.flow_regimes.join(', '),
    fallback_for_unmapped_flow_regime:
      candidate.fallback_for_unmapped_flow_regime,
  }
}

function defaultCandidate(): CandidateDraft {
  return {
    id: 'default',
    regime: 'general',
    priority: '0',
    coefficients: [{ name: 'loss_coefficient', value: '1.5' }],
    expression:
      'loss_coefficient * mass_flow * abs(mass_flow) / (2 * density * area * area)',
    applicability: [],
    flow_regimes: '',
    fallback_for_unmapped_flow_regime: false,
  }
}

export function CorrelationArtifactForm({
  unitDimensions,
  artifactRevisions,
  base,
  onCancel,
  onSubmit,
}: CorrelationArtifactFormProps) {
  const dimensions = unitDimensions.map((item) => item.dimension)
  const defaultDimension = dimensions.includes('dimensionless')
    ? 'dimensionless'
    : dimensions[0] ?? ''
  const correlationArtifactIds = useMemo(
    () => new Set(artifactRevisions
      .filter((revision) => revision.artifact_type === 'thermox.correlation')
      .map((revision) => revision.artifact_id)),
    [artifactRevisions],
  )
  const [artifactId, setArtifactId] = useState(
    base?.source.artifact_id ?? 'pressure-loss-correlation',
  )
  const [inputs, setInputs] = useState<VariableDraft[]>(base?.definition.inputs ?? [
    { name: 'mass_flow', dimension: dimensions.includes('mass_flow') ? 'mass_flow' : defaultDimension },
    { name: 'density', dimension: dimensions.includes('density') ? 'density' : defaultDimension },
    { name: 'area', dimension: dimensions.includes('area') ? 'area' : defaultDimension },
  ])
  const [outputName, setOutputName] = useState(base?.definition.output.name ?? 'pressure_loss')
  const [outputDimension, setOutputDimension] = useState(
    base?.definition.output.dimension ??
      (dimensions.includes('pressure') ? 'pressure' : defaultDimension),
  )
  const [candidates, setCandidates] = useState<CandidateDraft[]>(
    base ? base.definition.candidates.map(draftCandidate) : [defaultCandidate()],
  )
  const [submitting, setSubmitting] = useState(false)
  const [formError, setFormError] = useState('')

  function updateCandidate(index: number, update: (candidate: CandidateDraft) => CandidateDraft) {
    setCandidates((current) => current.map((candidate, itemIndex) =>
      itemIndex === index ? update(candidate) : candidate))
  }

  async function submit(event: FormEvent) {
    event.preventDefault()
    setFormError('')
    try {
      if (!artifactId.trim()) throw new Error('Artifact ID is required.')
      if (!base && correlationArtifactIds.has(artifactId.trim())) {
        throw new Error('That artifact ID already exists. Use Revise to create a child revision.')
      }
      const definition: CorrelationArtifactDefinition = {
        schema_version: 'thermox.correlation/v2',
        inputs: inputs.map((input) => ({
          name: input.name.trim(), dimension: input.dimension,
        })),
        output: { name: outputName.trim(), dimension: outputDimension },
        candidates: candidates.map((candidate) => {
          const coefficients: Record<string, number> = {}
          for (const coefficient of candidate.coefficients) {
            coefficients[coefficient.name.trim()] = Number(coefficient.value)
          }
          return {
            id: candidate.id.trim(),
            regime: candidate.regime.trim(),
            priority: Number(candidate.priority),
            coefficients,
            expression: candidate.expression.trim(),
            flow_regimes: candidate.flow_regimes
              .split(',')
              .map((regime) => regime.trim())
              .filter(Boolean),
            fallback_for_unmapped_flow_regime:
              candidate.fallback_for_unmapped_flow_regime,
            applicability: candidate.applicability.map((range) => ({
              input: range.input,
              ...(range.minimum.trim() === '' ? {} : { minimum: Number(range.minimum) }),
              ...(range.maximum.trim() === '' ? {} : { maximum: Number(range.maximum) }),
              minimum_inclusive: range.minimum_inclusive,
              maximum_inclusive: range.maximum_inclusive,
            })),
          }
        }),
      }
      const issues = validateCorrelationDefinition(definition)
      if (issues.length) throw new Error(issues.join('\n'))
      setSubmitting(true)
      await onSubmit(
        artifactId.trim(), base?.source.artifact_revision_id ?? '', definition,
      )
    } catch (reason) {
      setFormError(reason instanceof Error ? reason.message : 'The correlation was rejected.')
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form className="component-dialog expression-component-dialog" onSubmit={submit}>
        <header>
          <div>
            <span className="eyebrow">Engineering data registry</span>
            <h2>{base ? 'Revise correlation' : 'Publish correlation'}</h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>×</button>
        </header>
        <p className="registry-note">
          <strong>Typed immutable correlation family</strong>
          Declare shared physical inputs and output, then one or more qualified candidate laws.
          At runtime the applicable candidate with the highest priority is selected deterministically.
        </p>
        <div className="form-grid">
          <label>
            <span>Artifact ID</span>
            <input value={artifactId} required disabled={Boolean(base)} onChange={(event) => setArtifactId(event.target.value)} />
          </label>
          <label>
            <span>Output name</span>
            <input value={outputName} required onChange={(event) => setOutputName(event.target.value)} />
          </label>
          <label>
            <span>Output dimension</span>
            <select value={outputDimension} required onChange={(event) => setOutputDimension(event.target.value)}>
              {dimensions.map((dimension) => <option key={dimension} value={dimension}>{dimension}</option>)}
            </select>
          </label>
        </div>

        <fieldset>
          <legend>Typed inputs</legend>
          {inputs.map((input, index) => (
            <div className="repeatable-row correlation-variable-row" key={index}>
              <label><span>Name</span><input value={input.name} required onChange={(event) => setInputs((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, name: event.target.value } : item))} /></label>
              <label><span>Dimension</span><select value={input.dimension} required onChange={(event) => setInputs((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, dimension: event.target.value } : item))}>{dimensions.map((dimension) => <option key={dimension} value={dimension}>{dimension}</option>)}</select></label>
              <button type="button" className="row-remove-button" aria-label={`Remove input ${input.name || index + 1}`} onClick={() => setInputs((current) => current.filter((_, itemIndex) => itemIndex !== index))}>×</button>
            </div>
          ))}
          <button type="button" className="add-row-button" onClick={() => setInputs((current) => [...current, { name: `input_${current.length + 1}`, dimension: defaultDimension }])}>+ Add input</button>
        </fieldset>

        {candidates.map((candidate, candidateIndex) => (
          <fieldset key={`${candidate.id}-${candidateIndex}`}>
            <legend>Candidate law {candidateIndex + 1}</legend>
            <div className="form-grid">
              <label><span>Candidate ID</span><input value={candidate.id} required onChange={(event) => updateCandidate(candidateIndex, (item) => ({ ...item, id: event.target.value }))} /></label>
              <label><span>Regime</span><input value={candidate.regime} required onChange={(event) => updateCandidate(candidateIndex, (item) => ({ ...item, regime: event.target.value }))} /></label>
              <label><span>Priority</span><input type="number" step="1" value={candidate.priority} required onChange={(event) => updateCandidate(candidateIndex, (item) => ({ ...item, priority: event.target.value }))} /></label>
              <label><span>Flow-pattern routes</span><input value={candidate.flow_regimes} placeholder="bubbly, slug" onChange={(event) => updateCandidate(candidateIndex, (item) => ({ ...item, flow_regimes: event.target.value }))} /><small>Comma-separated regimes emitted by a bound regime map.</small></label>
              <label><span>General fallback</span><small><input type="checkbox" checked={candidate.fallback_for_unmapped_flow_regime} onChange={(event) => updateCandidate(candidateIndex, (item) => ({ ...item, fallback_for_unmapped_flow_regime: event.target.checked }))} /> Use only when no exact flow-pattern route exists</small></label>
            </div>

            <p className="registry-note">Coefficients are immutable SI values local to this candidate.</p>
            {candidate.coefficients.map((coefficient, index) => (
              <div className="repeatable-row correlation-variable-row" key={index}>
                <label><span>Coefficient</span><input value={coefficient.name} required onChange={(event) => updateCandidate(candidateIndex, (item) => ({ ...item, coefficients: item.coefficients.map((entry, itemIndex) => itemIndex === index ? { ...entry, name: event.target.value } : entry) }))} /></label>
                <label><span>SI value</span><input type="number" step="any" value={coefficient.value} required onChange={(event) => updateCandidate(candidateIndex, (item) => ({ ...item, coefficients: item.coefficients.map((entry, itemIndex) => itemIndex === index ? { ...entry, value: event.target.value } : entry) }))} /></label>
                <button type="button" className="row-remove-button" aria-label={`Remove coefficient ${coefficient.name || index + 1}`} onClick={() => updateCandidate(candidateIndex, (item) => ({ ...item, coefficients: item.coefficients.filter((_, itemIndex) => itemIndex !== index) }))}>×</button>
              </div>
            ))}
            <button type="button" className="add-row-button" onClick={() => updateCandidate(candidateIndex, (item) => ({ ...item, coefficients: [...item.coefficients, { name: `coefficient_${item.coefficients.length + 1}`, value: '1' }] }))}>+ Add coefficient</button>

            <div className="repeatable-row correlation-expression-row">
              <label><span>Output equation</span><textarea rows={4} value={candidate.expression} required onChange={(event) => updateCandidate(candidateIndex, (item) => ({ ...item, expression: event.target.value }))} /><small>Available functions: abs, sqrt, exp, log, and pow.</small></label>
            </div>

            <p className="registry-note">Optional SI limits define where this candidate is qualified.</p>
            {candidate.applicability.map((range, index) => (
              <div className="repeatable-row correlation-variable-row" key={`${range.input}-${index}`}>
                <label><span>Input</span><select value={range.input} required onChange={(event) => updateCandidate(candidateIndex, (item) => ({ ...item, applicability: item.applicability.map((entry, itemIndex) => itemIndex === index ? { ...entry, input: event.target.value } : entry) }))}><option value="">Select input</option>{inputs.map((input) => <option key={input.name} value={input.name}>{input.name}</option>)}</select></label>
                <label><span>Minimum (SI)</span><input type="number" step="any" value={range.minimum} onChange={(event) => updateCandidate(candidateIndex, (item) => ({ ...item, applicability: item.applicability.map((entry, itemIndex) => itemIndex === index ? { ...entry, minimum: event.target.value } : entry) }))} /><small><input type="checkbox" checked={range.minimum_inclusive} onChange={(event) => updateCandidate(candidateIndex, (item) => ({ ...item, applicability: item.applicability.map((entry, itemIndex) => itemIndex === index ? { ...entry, minimum_inclusive: event.target.checked } : entry) }))} /> Inclusive</small></label>
                <label><span>Maximum (SI)</span><input type="number" step="any" value={range.maximum} onChange={(event) => updateCandidate(candidateIndex, (item) => ({ ...item, applicability: item.applicability.map((entry, itemIndex) => itemIndex === index ? { ...entry, maximum: event.target.value } : entry) }))} /><small><input type="checkbox" checked={range.maximum_inclusive} onChange={(event) => updateCandidate(candidateIndex, (item) => ({ ...item, applicability: item.applicability.map((entry, itemIndex) => itemIndex === index ? { ...entry, maximum_inclusive: event.target.checked } : entry) }))} /> Inclusive</small></label>
                <button type="button" className="row-remove-button" aria-label={`Remove applicability range ${range.input || index + 1}`} onClick={() => updateCandidate(candidateIndex, (item) => ({ ...item, applicability: item.applicability.filter((_, itemIndex) => itemIndex !== index) }))}>×</button>
              </div>
            ))}
            <button type="button" className="add-row-button" disabled={!inputs.length} onClick={() => updateCandidate(candidateIndex, (item) => ({ ...item, applicability: [...item.applicability, { input: inputs[0]?.name ?? '', minimum: '', maximum: '', minimum_inclusive: true, maximum_inclusive: true }] }))}>+ Add qualified range</button>
            {candidates.length > 1 && <button type="button" className="secondary-button" onClick={() => setCandidates((current) => current.filter((_, index) => index !== candidateIndex))}>Remove candidate</button>}
          </fieldset>
        ))}
        <button type="button" className="add-row-button" onClick={() => setCandidates((current) => [...current, { ...defaultCandidate(), id: `candidate_${current.length + 1}`, coefficients: [], expression: '' }])}>+ Add candidate law</button>

        {formError && <p className="form-error">{formError}</p>}
        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>Cancel</button>
          <button type="submit" className="primary-button" disabled={submitting}>{submitting ? 'Publishing…' : base ? 'Publish revision' : 'Publish correlation'}</button>
        </footer>
      </form>
    </div>
  )
}
