import { useMemo, useState, type FormEvent } from 'react'
import { PerformanceMapImportPanel } from './PerformanceMapImportPanel'
import type {
  ArtifactRevision,
  CatalogUnitDimension,
  MapExtrapolationPolicy,
  PerformanceMapArtifactDefinition,
} from './types'

interface PerformanceMapArtifactFormProps {
  unitDimensions: CatalogUnitDimension[]
  artifactRevisions: ArtifactRevision[]
  base?: {
    source: ArtifactRevision
    definition: PerformanceMapArtifactDefinition
  }
  onCancel: () => void
  onSubmit: (
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: PerformanceMapArtifactDefinition,
  ) => Promise<void>
}

type VariableDraft = { name: string; dimension: string }
type CurveDraft = { familyCoordinate: string; samples: string }
type ConstraintDraft = {
  output: string
  minimum: string
  maximum: string
  minimumInclusive: boolean
  maximumInclusive: boolean
}

const policies: MapExtrapolationPolicy[] = ['reject', 'clamp', 'linear']

export function parsePerformanceMapSamples(
  text: string,
  outputCount: number,
): PerformanceMapArtifactDefinition['curves'][number]['samples'] {
  return text
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean)
    .map((line, index) => {
      const values = line.split(/[\s,]+/).map(Number)
      if (values.length !== outputCount + 1 || values.some((value) => !Number.isFinite(value))) {
        throw new Error(
          `Sample line ${index + 1} needs one finite primary coordinate and ${outputCount} finite output value${outputCount === 1 ? '' : 's'}.`,
        )
      }
      return { coordinate: values[0], outputs: values.slice(1) }
    })
}

export function validatePerformanceMapDefinition(
  definition: PerformanceMapArtifactDefinition,
): string[] {
  const issues: string[] = []
  const variables = [
    definition.primary_variable,
    definition.family_variable,
    ...definition.output_variables,
  ]
  if (!definition.primary_variable.name || !definition.primary_variable.dimension) {
    issues.push('The primary axis needs a name and dimension.')
  }
  if (!definition.family_variable.name || !definition.family_variable.dimension) {
    issues.push('The family axis needs a name and dimension.')
  }
  if (!definition.output_variables.length) issues.push('Define at least one output.')
  definition.output_variables.forEach((output, index) => {
    if (!output.name || !output.dimension) {
      issues.push(`Output ${index + 1} needs a name and dimension.`)
    }
  })
  const outputNames = new Set(
    definition.output_variables.map((output) => output.name),
  )
  const constrainedOutputs = new Set<string>()
  definition.output_constraints?.forEach((constraint, index) => {
    if (!outputNames.has(constraint.output)) {
      issues.push(`Constraint ${index + 1} references unknown output "${constraint.output}".`)
    }
    if (constrainedOutputs.has(constraint.output)) {
      issues.push(`Output constraint for "${constraint.output}" is duplicated.`)
    }
    constrainedOutputs.add(constraint.output)
    if (constraint.minimum === undefined && constraint.maximum === undefined) {
      issues.push(`Constraint ${index + 1} needs at least one bound.`)
    }
    if (
      (constraint.minimum !== undefined && !Number.isFinite(constraint.minimum)) ||
      (constraint.maximum !== undefined && !Number.isFinite(constraint.maximum))
    ) {
      issues.push(`Constraint ${index + 1} bounds must be finite.`)
    }
    if (
      constraint.minimum !== undefined &&
      constraint.maximum !== undefined &&
      (constraint.minimum > constraint.maximum ||
        (constraint.minimum === constraint.maximum &&
          (!constraint.minimum_inclusive || !constraint.maximum_inclusive)))
    ) {
      issues.push(`Constraint ${index + 1} has an empty interval.`)
    }
  })
  const names = new Set<string>()
  for (const variable of variables) {
    if (variable.name && names.has(variable.name)) {
      issues.push(`Map variable "${variable.name}" is duplicated.`)
    }
    names.add(variable.name)
    if (variable.name && !variable.dimension) {
      issues.push(`Map variable "${variable.name}" needs a dimension.`)
    }
  }
  if (definition.curves.length < 2) issues.push('Define at least two family curves.')
  let previousFamily = Number.NEGATIVE_INFINITY
  definition.curves.forEach((curve, curveIndex) => {
    if (!Number.isFinite(curve.family_coordinate) || curve.family_coordinate <= previousFamily) {
      issues.push(`Curve ${curveIndex + 1} family coordinate must be finite and strictly increasing.`)
    }
    previousFamily = curve.family_coordinate
    if (curve.samples.length < 2) {
      issues.push(`Curve ${curveIndex + 1} needs at least two samples.`)
    }
    let previousPrimary = Number.NEGATIVE_INFINITY
    curve.samples.forEach((sample, sampleIndex) => {
      if (!Number.isFinite(sample.coordinate) || sample.coordinate <= previousPrimary) {
        issues.push(`Curve ${curveIndex + 1} sample ${sampleIndex + 1} coordinate must be finite and strictly increasing.`)
      }
      previousPrimary = sample.coordinate
      if (
        sample.outputs.length !== definition.output_variables.length ||
        sample.outputs.some((value) => !Number.isFinite(value))
      ) {
        issues.push(`Curve ${curveIndex + 1} sample ${sampleIndex + 1} does not match the declared finite outputs.`)
      }
      definition.output_constraints?.forEach((constraint) => {
        const outputIndex = definition.output_variables.findIndex(
          (output) => output.name === constraint.output,
        )
        if (outputIndex < 0 || !Number.isFinite(sample.outputs[outputIndex])) return
        const value = sample.outputs[outputIndex]
        const below = constraint.minimum !== undefined &&
          (value < constraint.minimum ||
            (value === constraint.minimum && !constraint.minimum_inclusive))
        const above = constraint.maximum !== undefined &&
          (value > constraint.maximum ||
            (value === constraint.maximum && !constraint.maximum_inclusive))
        if (below || above) {
          issues.push(
            `Curve ${curveIndex + 1} sample ${sampleIndex + 1} violates the declared constraint for "${constraint.output}".`,
          )
        }
      })
    })
  })
  return issues
}

function samplesText(
  samples: PerformanceMapArtifactDefinition['curves'][number]['samples'],
) {
  return samples.map((sample) => [sample.coordinate, ...sample.outputs].join(', ')).join('\n')
}

function MapPreview({ definition }: { definition?: PerformanceMapArtifactDefinition }) {
  if (!definition?.curves.length || !definition.output_variables.length) return null
  const points = definition.curves.flatMap((curve) =>
    curve.samples.map((sample) => ({ x: sample.coordinate, y: sample.outputs[0] })),
  ).filter((point) => Number.isFinite(point.x) && Number.isFinite(point.y))
  if (points.length < 2) return null
  const minX = Math.min(...points.map((point) => point.x))
  const maxX = Math.max(...points.map((point) => point.x))
  const minY = Math.min(...points.map((point) => point.y))
  const maxY = Math.max(...points.map((point) => point.y))
  const xSpan = maxX - minX || 1
  const ySpan = maxY - minY || 1
  const colors = ['#27798b', '#e48b43', '#6f8c46', '#845c9e', '#bc5965']
  const path = (curve: PerformanceMapArtifactDefinition['curves'][number]) =>
    curve.samples.map((sample, index) => {
      const x = 22 + ((sample.coordinate - minX) / xSpan) * 316
      const y = 138 - ((sample.outputs[0] - minY) / ySpan) * 116
      return `${index ? 'L' : 'M'} ${x.toFixed(2)} ${y.toFixed(2)}`
    }).join(' ')
  return (
    <div className="performance-map-preview">
      <div>
        <strong>Map preview</strong>
        <span>{definition.output_variables[0].name} by {definition.primary_variable.name}</span>
      </div>
      <svg viewBox="0 0 360 160" role="img" aria-label="First performance map output across family curves">
        <path className="map-axis" d="M 22 16 V 138 H 344" />
        {definition.curves.map((curve, index) => (
          <path key={index} className="map-curve" stroke={colors[index % colors.length]} d={path(curve)} />
        ))}
      </svg>
    </div>
  )
}

export function PerformanceMapArtifactForm({
  unitDimensions,
  artifactRevisions,
  base,
  onCancel,
  onSubmit,
}: PerformanceMapArtifactFormProps) {
  const dimensions = unitDimensions.map((item) => item.dimension)
  const dimensionless = dimensions.includes('dimensionless') ? 'dimensionless' : dimensions[0] ?? ''
  const [artifactId, setArtifactId] = useState(base?.source.artifact_id ?? 'compressor-performance-map')
  const [primary, setPrimary] = useState<VariableDraft>(base?.definition.primary_variable ?? {
    name: 'corrected_mass_flow', dimension: dimensions.includes('mass_flow') ? 'mass_flow' : dimensionless,
  })
  const [family, setFamily] = useState<VariableDraft>(base?.definition.family_variable ?? {
    name: 'corrected_speed', dimension: dimensions.includes('angular_speed') ? 'angular_speed' : dimensionless,
  })
  const [outputs, setOutputs] = useState<VariableDraft[]>(base?.definition.output_variables ?? [
    { name: 'pressure_ratio', dimension: dimensionless },
    { name: 'isentropic_efficiency', dimension: dimensionless },
  ])
  const [constraints, setConstraints] = useState<ConstraintDraft[]>(
    (base?.definition.output_constraints ?? []).map((constraint) => ({
      output: constraint.output,
      minimum: constraint.minimum === undefined ? '' : String(constraint.minimum),
      maximum: constraint.maximum === undefined ? '' : String(constraint.maximum),
      minimumInclusive: constraint.minimum_inclusive,
      maximumInclusive: constraint.maximum_inclusive,
    })),
  )
  const [curves, setCurves] = useState<CurveDraft[]>(base
    ? base.definition.curves.map((curve) => ({ familyCoordinate: String(curve.family_coordinate), samples: samplesText(curve.samples) }))
    : [
        { familyCoordinate: '250', samples: '70, 10, 0.82\n120, 8, 0.84' },
        { familyCoordinate: '400', samples: '70, 12, 0.84\n120, 10, 0.86' },
      ])
  const [primaryExtrapolation, setPrimaryExtrapolation] = useState<MapExtrapolationPolicy>(base?.definition.primary_extrapolation ?? 'reject')
  const [familyExtrapolation, setFamilyExtrapolation] = useState<MapExtrapolationPolicy>(base?.definition.family_extrapolation ?? 'reject')
  const [submitting, setSubmitting] = useState(false)
  const [formError, setFormError] = useState('')
  const existingIds = useMemo(() => new Set(
    artifactRevisions
      .filter((revision) => revision.artifact_type === 'thermox.performance_map')
      .map((revision) => revision.artifact_id),
  ), [artifactRevisions])

  function definition(): PerformanceMapArtifactDefinition {
    return {
      primary_variable: { name: primary.name.trim(), dimension: primary.dimension },
      family_variable: { name: family.name.trim(), dimension: family.dimension },
      output_variables: outputs.map((output) => ({ name: output.name.trim(), dimension: output.dimension })),
      output_constraints: constraints.map((constraint) => ({
        output: constraint.output,
        ...(constraint.minimum.trim() === '' ? {} : { minimum: Number(constraint.minimum) }),
        ...(constraint.maximum.trim() === '' ? {} : { maximum: Number(constraint.maximum) }),
        minimum_inclusive: constraint.minimumInclusive,
        maximum_inclusive: constraint.maximumInclusive,
      })),
      curves: curves.map((curve) => ({
        family_coordinate: Number(curve.familyCoordinate),
        samples: parsePerformanceMapSamples(curve.samples, outputs.length),
      })),
      primary_extrapolation: primaryExtrapolation,
      family_extrapolation: familyExtrapolation,
    }
  }

  let preview: PerformanceMapArtifactDefinition | undefined
  try { preview = definition() } catch { preview = undefined }

  async function submit(event: FormEvent) {
    event.preventDefault()
    setFormError('')
    try {
      const id = artifactId.trim()
      if (!id) throw new Error('Artifact ID is required.')
      if (!base && existingIds.has(id)) {
        throw new Error('That artifact ID already exists. Use its Revise action to create a child revision.')
      }
      const next = definition()
      const issues = validatePerformanceMapDefinition(next)
      if (issues.length) throw new Error(issues.join('\n'))
      setSubmitting(true)
      await onSubmit(id, base?.source.artifact_revision_id ?? '', next)
    } catch (reason) {
      setFormError(reason instanceof Error ? reason.message : 'The performance map was rejected.')
    } finally {
      setSubmitting(false)
    }
  }

  const variableEditor = (
    label: string,
    value: VariableDraft,
    change: (value: VariableDraft) => void,
  ) => (
    <div className="repeatable-row correlation-variable-row">
      <label><span>{label} name</span><input required value={value.name} onChange={(event) => change({ ...value, name: event.target.value })} /></label>
      <label><span>Dimension</span><select required value={value.dimension} onChange={(event) => change({ ...value, dimension: event.target.value })}>{dimensions.map((dimension) => <option key={dimension}>{dimension}</option>)}</select></label>
    </div>
  )

  return (
    <div className="dialog-backdrop" role="presentation">
      <form className="component-dialog expression-component-dialog performance-map-dialog" onSubmit={submit}>
        <header><div><span className="eyebrow">Engineering data registry</span><h2>{base ? 'Revise performance map' : 'Publish performance map'}</h2></div><button type="button" className="icon-button" onClick={onCancel}>×</button></header>
        <p className="registry-note"><strong>Typed immutable map</strong>Declare generic axes, outputs, non-rectangular family curves, and explicit extrapolation behavior. Values are stored in SI and validated by the platform map kernel.</p>
        <div className="form-grid">
          <label><span>Artifact ID</span><input required disabled={Boolean(base)} value={artifactId} onChange={(event) => setArtifactId(event.target.value)} /><small>{base ? `Publishes an immutable child of r${base.source.revision_number}.` : 'Creates a new logical map artifact.'}</small></label>
          <label><span>Primary extrapolation</span><select value={primaryExtrapolation} onChange={(event) => setPrimaryExtrapolation(event.target.value as MapExtrapolationPolicy)}>{policies.map((policy) => <option key={policy}>{policy}</option>)}</select></label>
          <label><span>Family extrapolation</span><select value={familyExtrapolation} onChange={(event) => setFamilyExtrapolation(event.target.value as MapExtrapolationPolicy)}>{policies.map((policy) => <option key={policy}>{policy}</option>)}</select></label>
        </div>
        <fieldset><legend>Coordinates</legend>{variableEditor('Primary axis', primary, setPrimary)}{variableEditor('Family axis', family, setFamily)}</fieldset>
        <fieldset><legend>Outputs</legend>{outputs.map((output, index) => <div className="repeatable-row correlation-variable-row" key={index}><label><span>Output name</span><input required value={output.name} onChange={(event) => setOutputs((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, name: event.target.value } : item))} /></label><label><span>Dimension</span><select required value={output.dimension} onChange={(event) => setOutputs((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, dimension: event.target.value } : item))}>{dimensions.map((dimension) => <option key={dimension}>{dimension}</option>)}</select></label><button type="button" className="row-remove-button" aria-label={`Remove output ${output.name}`} onClick={() => setOutputs((current) => current.filter((_, itemIndex) => itemIndex !== index))}>×</button></div>)}<button type="button" className="add-row-button" onClick={() => setOutputs((current) => [...current, { name: `output_${current.length + 1}`, dimension: dimensionless }])}>+ Add output</button></fieldset>
        <fieldset>
          <legend>Physical output constraints</legend>
          <p className="field-help">Optional admissible bounds in each output's declared SI dimension. Bounds are enforced when publishing and whenever the solver evaluates the map.</p>
          {constraints.map((constraint, index) => (
            <div className="repeatable-row performance-map-constraint-row" key={index}>
              <label><span>Output</span><select required value={constraint.output} onChange={(event) => setConstraints((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, output: event.target.value } : item))}><option value="">Select output</option>{outputs.map((output) => <option key={output.name} value={output.name}>{output.name || 'Unnamed output'}</option>)}</select></label>
              <label><span>Minimum</span><input type="number" step="any" value={constraint.minimum} onChange={(event) => setConstraints((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, minimum: event.target.value } : item))} /></label>
              <label><span>Maximum</span><input type="number" step="any" value={constraint.maximum} onChange={(event) => setConstraints((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, maximum: event.target.value } : item))} /></label>
              <label className="checkbox-label"><input type="checkbox" checked={constraint.minimumInclusive} onChange={(event) => setConstraints((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, minimumInclusive: event.target.checked } : item))} /><span>Include minimum</span></label>
              <label className="checkbox-label"><input type="checkbox" checked={constraint.maximumInclusive} onChange={(event) => setConstraints((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, maximumInclusive: event.target.checked } : item))} /><span>Include maximum</span></label>
              <button type="button" className="row-remove-button" aria-label={`Remove constraint ${index + 1}`} onClick={() => setConstraints((current) => current.filter((_, itemIndex) => itemIndex !== index))}>×</button>
            </div>
          ))}
          <button type="button" className="add-row-button" disabled={!outputs.length} onClick={() => setConstraints((current) => [...current, { output: outputs[0]?.name ?? '', minimum: '', maximum: '', minimumInclusive: true, maximumInclusive: true }])}>+ Add output constraint</button>
        </fieldset>
        <PerformanceMapImportPanel
          primaryName={primary.name}
          familyName={family.name}
          outputNames={outputs.map((output) => output.name)}
          onApply={(imported) => setCurves(imported.map((curve) => ({
            familyCoordinate: String(curve.family_coordinate),
            samples: samplesText(curve.samples),
          })))}
        />
        <fieldset><legend>Family curves</legend><p className="field-help">Enter one sample per line: primary coordinate, then outputs in the declared order ({outputs.map((output) => output.name || '?').join(', ')}).</p>{curves.map((curve, index) => <div className="performance-map-curve" key={index}><label><span>{family.name || 'Family'} coordinate</span><input type="number" step="any" required value={curve.familyCoordinate} onChange={(event) => setCurves((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, familyCoordinate: event.target.value } : item))} /></label><label><span>Samples · {primary.name || 'primary'}, outputs…</span><textarea rows={4} required value={curve.samples} onChange={(event) => setCurves((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, samples: event.target.value } : item))} /></label><button type="button" className="row-remove-button" aria-label={`Remove curve ${index + 1}`} onClick={() => setCurves((current) => current.filter((_, itemIndex) => itemIndex !== index))}>×</button></div>)}<button type="button" className="add-row-button" onClick={() => setCurves((current) => [...current, { familyCoordinate: '', samples: '' }])}>+ Add curve</button></fieldset>
        <MapPreview definition={preview} />
        {formError && <p className="form-error">{formError}</p>}
        <footer><button type="button" className="secondary-button" onClick={onCancel}>Cancel</button><button type="submit" className="primary-button" disabled={submitting}>{submitting ? 'Publishing…' : base ? 'Publish revision' : 'Publish map'}</button></footer>
      </form>
    </div>
  )
}
