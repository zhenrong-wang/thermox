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

const identifierPattern = /^[A-Za-z_][A-Za-z0-9_]*$/

export function validateCorrelationDefinition(
  definition: CorrelationArtifactDefinition,
): string[] {
  const issues: string[] = []
  if (!definition.inputs.length) issues.push('Define at least one input.')
  const names = new Set<string>()
  for (const input of definition.inputs) {
    if (!identifierPattern.test(input.name)) {
      issues.push(`Input "${input.name}" is not a valid expression identifier.`)
    }
    if (!input.dimension) issues.push(`Input "${input.name}" needs a dimension.`)
    if (names.has(input.name)) issues.push(`Symbol "${input.name}" is duplicated.`)
    names.add(input.name)
  }
  for (const [name, value] of Object.entries(definition.coefficients)) {
    if (!identifierPattern.test(name)) {
      issues.push(`Coefficient "${name}" is not a valid expression identifier.`)
    }
    if (!Number.isFinite(value)) issues.push(`Coefficient "${name}" must be finite.`)
    if (names.has(name)) issues.push(`Symbol "${name}" is duplicated.`)
    names.add(name)
  }
  if (!identifierPattern.test(definition.output.name)) {
    issues.push('Output name must be a valid expression identifier.')
  }
  if (!definition.output.dimension) issues.push('Output needs a dimension.')
  if (!definition.expression.trim()) issues.push('Expression is required.')
  const rangedInputs = new Set<string>()
  for (const range of definition.applicability ?? []) {
    if (!definition.inputs.some((input) => input.name === range.input)) {
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
      range.minimum !== undefined &&
      range.maximum !== undefined &&
      (range.minimum > range.maximum ||
        (range.minimum === range.maximum &&
          (!range.minimum_inclusive || !range.maximum_inclusive)))
    ) {
      issues.push(`Applicability range for "${range.input}" is empty.`)
    }
  }
  return issues
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
    () =>
      new Set(artifactRevisions
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
  const [coefficients, setCoefficients] = useState<CoefficientDraft[]>(
    base
      ? Object.entries(base.definition.coefficients).map(([name, value]) => ({
          name,
          value: String(value),
        }))
      : [{ name: 'loss_coefficient', value: '1.5' }],
  )
  const [expression, setExpression] = useState(
    base?.definition.expression ??
      'loss_coefficient * mass_flow * abs(mass_flow) / (2 * density * area * area)',
  )
  const [applicability, setApplicability] = useState<ApplicabilityDraft[]>(
    (base?.definition.applicability ?? []).map((range) => ({
      input: range.input,
      minimum: range.minimum === undefined ? '' : String(range.minimum),
      maximum: range.maximum === undefined ? '' : String(range.maximum),
      minimum_inclusive: range.minimum_inclusive,
      maximum_inclusive: range.maximum_inclusive,
    })),
  )
  const [submitting, setSubmitting] = useState(false)
  const [formError, setFormError] = useState('')

  async function submit(event: FormEvent) {
    event.preventDefault()
    setFormError('')
    try {
      if (!artifactId.trim()) throw new Error('Artifact ID is required.')
      if (!base && correlationArtifactIds.has(artifactId.trim())) {
        throw new Error(
          'That artifact ID already exists. Use its Revise action to create a child revision.',
        )
      }
      const coefficientValues: Record<string, number> = {}
      const coefficientNames = new Set<string>()
      for (const coefficient of coefficients) {
        const value = Number(coefficient.value)
        const name = coefficient.name.trim()
        if (!name || !Number.isFinite(value)) {
          throw new Error('Every coefficient needs a name and finite SI value.')
        }
        if (coefficientNames.has(name)) {
          throw new Error(`Coefficient "${name}" is duplicated.`)
        }
        coefficientNames.add(name)
        coefficientValues[name] = value
      }
      const definition: CorrelationArtifactDefinition = {
        schema_version: 'thermox.correlation/v1',
        inputs: inputs.map((input) => ({
          name: input.name.trim(),
          dimension: input.dimension,
        })),
        output: {
          name: outputName.trim(),
          dimension: outputDimension,
        },
        coefficients: coefficientValues,
        expression: expression.trim(),
        applicability: applicability.map((range) => ({
          input: range.input,
          ...(range.minimum.trim() === '' ? {} : { minimum: Number(range.minimum) }),
          ...(range.maximum.trim() === '' ? {} : { maximum: Number(range.maximum) }),
          minimum_inclusive: range.minimum_inclusive,
          maximum_inclusive: range.maximum_inclusive,
        })),
      }
      const issues = validateCorrelationDefinition(definition)
      if (issues.length) throw new Error(issues.join('\n'))
      setSubmitting(true)
      await onSubmit(
        artifactId.trim(),
        base?.source.artifact_revision_id ?? '',
        definition,
      )
    } catch (reason) {
      setFormError(
        reason instanceof Error ? reason.message : 'The correlation was rejected.',
      )
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
          <strong>Typed immutable correlation</strong>
          Declare the physical inputs and output separately from the component that consumes them.
          Values and coefficients are SI. The service validates the bounded expression and derives
          its analytic first derivatives before persistence.
        </p>
        <div className="form-grid">
          <label>
            <span>Artifact ID</span>
            <input
              value={artifactId}
              required
              disabled={Boolean(base)}
              onChange={(event) => setArtifactId(event.target.value)}
            />
            <small>
              {base
                ? `Publishes an immutable child of r${base.source.revision_number}.`
                : 'Creates a new logical engineering artifact. Use Revise for an existing ID.'}
            </small>
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
              <label>
                <span>Name</span>
                <input value={input.name} required onChange={(event) => setInputs((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, name: event.target.value } : item))} />
              </label>
              <label>
                <span>Dimension</span>
                <select value={input.dimension} required onChange={(event) => setInputs((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, dimension: event.target.value } : item))}>
                  {dimensions.map((dimension) => <option key={dimension} value={dimension}>{dimension}</option>)}
                </select>
              </label>
              <button type="button" className="row-remove-button" aria-label={`Remove input ${input.name || index + 1}`} onClick={() => setInputs((current) => current.filter((_, itemIndex) => itemIndex !== index))}>×</button>
            </div>
          ))}
          <button type="button" className="add-row-button" onClick={() => setInputs((current) => [...current, { name: `input_${current.length + 1}`, dimension: defaultDimension }])}>+ Add input</button>
        </fieldset>

        <fieldset>
          <legend>Qualified operating envelope</legend>
          <p className="registry-note">
            Optional SI limits reject evaluation outside the range qualified by the correlation owner.
          </p>
          {applicability.map((range, index) => (
            <div className="repeatable-row correlation-variable-row" key={`${range.input}-${index}`}>
              <label>
                <span>Input</span>
                <select value={range.input} required onChange={(event) => setApplicability((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, input: event.target.value } : item))}>
                  <option value="">Select input</option>
                  {inputs.map((input) => <option key={input.name} value={input.name}>{input.name}</option>)}
                </select>
              </label>
              <label>
                <span>Minimum (SI)</span>
                <input type="number" step="any" value={range.minimum} onChange={(event) => setApplicability((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, minimum: event.target.value } : item))} />
                <small><input type="checkbox" checked={range.minimum_inclusive} onChange={(event) => setApplicability((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, minimum_inclusive: event.target.checked } : item))} /> Inclusive</small>
              </label>
              <label>
                <span>Maximum (SI)</span>
                <input type="number" step="any" value={range.maximum} onChange={(event) => setApplicability((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, maximum: event.target.value } : item))} />
                <small><input type="checkbox" checked={range.maximum_inclusive} onChange={(event) => setApplicability((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, maximum_inclusive: event.target.checked } : item))} /> Inclusive</small>
              </label>
              <button type="button" className="row-remove-button" aria-label={`Remove applicability range ${range.input || index + 1}`} onClick={() => setApplicability((current) => current.filter((_, itemIndex) => itemIndex !== index))}>×</button>
            </div>
          ))}
          <button type="button" className="add-row-button" disabled={!inputs.length} onClick={() => setApplicability((current) => [...current, { input: inputs[0]?.name ?? '', minimum: '', maximum: '', minimum_inclusive: true, maximum_inclusive: true }])}>+ Add qualified range</button>
        </fieldset>

        <fieldset>
          <legend>Immutable coefficients</legend>
          {coefficients.map((coefficient, index) => (
            <div className="repeatable-row correlation-variable-row" key={index}>
              <label>
                <span>Name</span>
                <input value={coefficient.name} required onChange={(event) => setCoefficients((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, name: event.target.value } : item))} />
              </label>
              <label>
                <span>SI value</span>
                <input type="number" step="any" value={coefficient.value} required onChange={(event) => setCoefficients((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, value: event.target.value } : item))} />
              </label>
              <button type="button" className="row-remove-button" aria-label={`Remove coefficient ${coefficient.name || index + 1}`} onClick={() => setCoefficients((current) => current.filter((_, itemIndex) => itemIndex !== index))}>×</button>
            </div>
          ))}
          <button type="button" className="add-row-button" onClick={() => setCoefficients((current) => [...current, { name: `coefficient_${current.length + 1}`, value: '1' }])}>+ Add coefficient</button>
        </fieldset>

        <fieldset>
          <legend>Safe expression</legend>
          <div className="repeatable-row correlation-expression-row">
            <label>
              <span>Output equation</span>
              <textarea rows={4} value={expression} required onChange={(event) => setExpression(event.target.value)} />
              <small>Available functions: abs, sqrt, exp, log, and pow. The expression returns the declared output.</small>
            </label>
          </div>
        </fieldset>

        {formError && <p className="form-error">{formError}</p>}
        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>Cancel</button>
          <button type="submit" className="primary-button" disabled={submitting}>
            {submitting ? 'Publishing…' : base ? 'Publish revision' : 'Publish correlation'}
          </button>
        </footer>
      </form>
    </div>
  )
}
