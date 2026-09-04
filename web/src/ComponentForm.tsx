import { useMemo, useState, type FormEvent } from 'react'
import { useDisplayUnits } from './DisplayUnitsContext'
import {
  displayUnit,
  displayValue,
  valueToSi,
  type DisplayUnitProfile,
} from './displayUnits'
import { latestArtifactRevisions } from './resourceBindings'
import type {
  ArtifactRevision,
  CatalogComponent,
  CatalogUnitDimension,
  ComponentDefinition,
  TopologyDocument,
} from './types'

interface ComponentFormProps {
  componentType: CatalogComponent
  topology: TopologyDocument
  artifactRevisions: ArtifactRevision[]
  component?: ComponentDefinition
  intent?: 'draft' | 'define'
  onCancel: () => void
  onSubmit: (component: ComponentDefinition) => Promise<void>
}

type CatalogParameter = CatalogComponent['parameters'][number]

export function instanceParameterDescriptors(
  componentType: CatalogComponent,
  topology: TopologyDocument,
  bindings: Readonly<Record<string, string>>,
): CatalogParameter[] {
  const species = new Set<string>()
  for (const port of componentType.ports) {
    if (port.domain !== 'material') continue
    const materialId = bindings[port.name]
    const material = (topology.model.materials ?? []).find(
      (candidate) => candidate.id === materialId,
    )
    for (const name of material?.species ?? []) species.add(name)
  }
  return componentType.parameters.flatMap((parameter) =>
    parameter.name.includes('{species}')
      ? [...species].map((name) => ({
          ...parameter,
          name: parameter.name.replaceAll('{species}', name),
        }))
      : [parameter],
  )
}

function parameterValue(
  value: unknown,
  fallback: number | null,
  dimension: string,
  profile: DisplayUnitProfile,
  unitDimensions: readonly CatalogUnitDimension[],
): string {
  let valueSi: number | null = typeof value === 'number' ? value : null
  if (value && typeof value === 'object') {
    const scalar = value as Record<string, unknown>
    if (typeof scalar.value_si === 'number') {
      valueSi = scalar.value_si
    }
    if (valueSi === null && typeof scalar.value === 'number') {
      valueSi = scalar.value
    }
  }
  if (valueSi === null) valueSi = fallback
  return valueSi === null
    ? ''
    : String(
        displayValue(
          valueSi,
          dimension,
          profile,
          unitDimensions,
        ).value,
      )
}

function suggestedId(
  componentType: CatalogComponent,
  topology: TopologyDocument,
): string {
  const base =
    componentType.kind
      .split('.')
      .filter(Boolean)
      .at(-1)
      ?.replace(/[^a-zA-Z0-9_-]/g, '_') || 'component'
  const used = new Set(
    topology.model.components.map((component) => component.id),
  )
  if (!used.has(base)) return base
  let suffix = 2
  while (used.has(`${base}_${suffix}`)) suffix += 1
  return `${base}_${suffix}`
}

export function ComponentForm({
  componentType,
  topology,
  artifactRevisions,
  component,
  intent = 'define',
  onCancel,
  onSubmit,
}: ComponentFormProps) {
  const { profile, unitDimensions } = useDisplayUnits()
  const [componentId, setComponentId] = useState(() =>
    component?.id ?? suggestedId(componentType, topology),
  )
  const [label, setLabel] = useState(component?.label ?? '')
  const [bindings, setBindings] = useState<Record<string, string>>({
    ...component?.media,
    ...component?.materials,
  })
  const instanceParameters = useMemo(
    () => instanceParameterDescriptors(componentType, topology, bindings),
    [bindings, componentType, topology],
  )
  const [artifacts, setArtifacts] = useState<Record<string, string>>({
    ...component?.artifacts,
  })
  const [parameters, setParameters] = useState<Record<string, string>>(() =>
    Object.fromEntries(
      instanceParameters.map((parameter) => [
        parameter.name,
        parameterValue(
          component?.parameters?.[parameter.name],
          parameter.default_value_si,
          parameter.dimension,
          profile,
          unitDimensions,
        ),
      ]),
    ),
  )
  const [submitting, setSubmitting] = useState(false)
  const [formError, setFormError] = useState('')

  const bindingPorts = useMemo(
    () =>
      componentType.ports.filter(
        (port) => port.domain === 'fluid' || port.domain === 'material',
      ),
    [componentType],
  )
  const latestArtifacts = useMemo(() => {
    return latestArtifactRevisions(artifactRevisions)
  }, [artifactRevisions])

  async function submit(event: FormEvent) {
    event.preventDefault()
    setFormError('')
    const trimmedId = componentId.trim()
    if (!trimmedId) {
      setFormError('Component ID is required.')
      return
    }
    const draftOnly = intent === 'draft' && !component
    const media: Record<string, string> = {}
    const materials: Record<string, string> = {}
    for (const port of draftOnly ? [] : bindingPorts) {
      const value = bindings[port.name]
      if (!value) {
        setFormError(`Select a ${port.domain} binding for ${port.name}.`)
        return
      }
      if (port.domain === 'fluid') media[port.name] = value
      else materials[port.name] = value
    }
    const numericParameters: Record<string, number> = {}
    for (const descriptor of draftOnly ? [] : instanceParameters) {
      const raw = (
        parameters[descriptor.name] ?? parameterValue(
          component?.parameters?.[descriptor.name],
          descriptor.default_value_si,
          descriptor.dimension,
          profile,
          unitDimensions,
        )
      ).trim()
      if (!raw) {
        if (descriptor.required && descriptor.default_value_si === null) {
          setFormError(`${descriptor.name} is required.`)
          return
        }
        continue
      }
      const value = Number(raw)
      if (!Number.isFinite(value)) {
        setFormError(`${descriptor.name} must be a finite number.`)
        return
      }
      numericParameters[descriptor.name] = valueToSi(
        value,
        descriptor.dimension,
        profile,
        unitDimensions,
      )
    }
    for (const descriptor of draftOnly ? [] : componentType.artifacts) {
      if (descriptor.required && !artifacts[descriptor.role]?.trim()) {
        setFormError(`Artifact binding ${descriptor.role} is required.`)
        return
      }
    }

    const nextComponent: ComponentDefinition = {
      id: trimmedId,
      kind: componentType.kind,
      version: componentType.version,
    }
    if (label.trim()) nextComponent.label = label.trim()
    if (Object.keys(media).length) nextComponent.media = media
    if (Object.keys(materials).length) nextComponent.materials = materials
    const artifactBindings = Object.fromEntries(
      Object.entries(artifacts)
        .map(([key, value]) => [key, value.trim()])
        .filter(([, value]) => Boolean(value)),
    )
    if (Object.keys(artifactBindings).length) {
      nextComponent.artifacts = artifactBindings
    }
    if (Object.keys(numericParameters).length) {
      nextComponent.parameters = numericParameters
    }

    setSubmitting(true)
    try {
      await onSubmit(nextComponent)
    } catch (reason) {
      setFormError(
        reason instanceof Error ? reason.message : 'Component was rejected.',
      )
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form className="component-dialog" onSubmit={submit}>
        <header>
          <div>
            <span className="eyebrow">
              {intent === 'draft' ? 'Draft topology instance' : 'Physical definition'}
            </span>
            <h2>
              {component
                ? 'Define component'
                : intent === 'draft'
                  ? 'Place component template'
                  : 'Add component'}
            </h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>
            ×
          </button>
        </header>

        <div className="dialog-kind">
          <code>{componentType.kind}</code>
          <span>v{componentType.version}</span>
        </div>

        <div className="form-grid">
          <label>
            <span>Component ID</span>
            <input
              value={componentId}
              onChange={(event) => setComponentId(event.target.value)}
              disabled={Boolean(component)}
              required
            />
          </label>
          <label>
            <span>Display label</span>
            <input
              value={label}
              onChange={(event) => setLabel(event.target.value)}
              placeholder="Optional"
            />
          </label>
        </div>

        {intent === 'draft' && !component && (
          <div className="registry-note">
            <strong>Definition happens next</strong>
            <span>
              This creates a draft graph instance. Media, parameters, and
              engineering data can be supplied in the Define workspace.
            </span>
          </div>
        )}

        {intent === 'define' && bindingPorts.length > 0 && (
          <fieldset>
            <legend>Fluid and reacting-mixture bindings</legend>
            <div className="form-grid">
              {bindingPorts.map((port) => {
                const options =
                  port.domain === 'fluid'
                    ? topology.model.media
                    : topology.model.materials ?? []
                return (
                  <label key={port.name}>
                    <span>
                      {port.name} · {port.domain}
                    </span>
                    <select
                      value={bindings[port.name] ?? ''}
                      onChange={(event) =>
                        setBindings((current) => ({
                          ...current,
                          [port.name]: event.target.value,
                        }))
                      }
                      required
                    >
                      <option value="">Select binding</option>
                      {options.map((item) => (
                        <option key={item.id} value={item.id}>
                          {item.id}
                        </option>
                      ))}
                    </select>
                  </label>
                )
              })}
            </div>
          </fieldset>
        )}

        {intent === 'define' && instanceParameters.length > 0 && (
          <fieldset>
            <legend>Parameters · displayed in {profile} units</legend>
            <div className="form-grid">
              {instanceParameters.map((parameter) => (
                <label key={parameter.name}>
                  <span>
                    {parameter.name}
                    <small>
                      {displayUnit(
                        parameter.dimension,
                        profile,
                        unitDimensions,
                      )}{' '}
                      ·{' '}
                      {parameter.dimension}
                    </small>
                  </span>
                  <input
                    type="number"
                    step="any"
                    value={
                      parameters[parameter.name] ?? parameterValue(
                        component?.parameters?.[parameter.name],
                        parameter.default_value_si,
                        parameter.dimension,
                        profile,
                        unitDimensions,
                      )
                    }
                    min={
                      parameter.lower_bound === null
                        ? undefined
                        : displayValue(
                            parameter.lower_bound,
                            parameter.dimension,
                            profile,
                            unitDimensions,
                          ).value
                    }
                    max={
                      parameter.upper_bound === null
                        ? undefined
                        : displayValue(
                            parameter.upper_bound,
                            parameter.dimension,
                            profile,
                            unitDimensions,
                          ).value
                    }
                    required={
                      parameter.required &&
                      parameter.default_value_si === null
                    }
                    onChange={(event) =>
                      setParameters((current) => ({
                        ...current,
                        [parameter.name]: event.target.value,
                      }))
                    }
                  />
                </label>
              ))}
            </div>
          </fieldset>
        )}

        {intent === 'define' && componentType.artifacts.length > 0 && (
          <fieldset>
            <legend>Engineering artifact bindings</legend>
            <div className="form-grid">
              {componentType.artifacts.map((artifact) => (
                <ArtifactBindingField
                  key={artifact.role}
                  role={artifact.role}
                  artifactType={artifact.artifact_type}
                  required={artifact.required}
                  value={artifacts[artifact.role] ?? ''}
                  revisions={latestArtifacts}
                  onChange={(value) =>
                    setArtifacts((current) => ({
                      ...current,
                      [artifact.role]: value,
                    }))
                  }
                />
              ))}
            </div>
          </fieldset>
        )}

        {formError && <div className="form-error">{formError}</div>}

        <footer>
          <button type="button" className="secondary-button" onClick={onCancel}>
            Cancel
          </button>
          <button type="submit" className="primary-button" disabled={submitting}>
            {submitting
              ? 'Publishing…'
              : component
                ? 'Publish updated revision'
                : intent === 'draft'
                  ? 'Place draft component'
                  : 'Publish child revision'}
          </button>
        </footer>
      </form>
    </div>
  )
}

function ArtifactBindingField({
  role,
  artifactType,
  required,
  value,
  revisions,
  onChange,
}: {
  role: string
  artifactType: string
  required: boolean
  value: string
  revisions: ArtifactRevision[]
  onChange: (value: string) => void
}) {
  const compatible = revisions.filter(
    (revision) => revision.artifact_type === artifactType,
  )
  const preservesExternalBinding =
    value && !compatible.some((revision) => revision.artifact_id === value)
  return (
    <label>
      <span>
        {role}
        <small>{artifactType}</small>
      </span>
      <select
        value={value}
        required={required}
        onChange={(event) => onChange(event.target.value)}
      >
        <option value="">
          {compatible.length
            ? 'Select project artifact'
            : 'No compatible project artifacts'}
        </option>
        {preservesExternalBinding && (
          <option value={value}>{value} · external binding</option>
        )}
        {compatible.map((revision) => (
          <option
            key={revision.artifact_revision_id}
            value={revision.artifact_id}
          >
            {revision.artifact_id} · r{revision.revision_number}
          </option>
        ))}
      </select>
    </label>
  )
}
