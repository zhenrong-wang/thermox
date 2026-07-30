import { useMemo, useState, type FormEvent } from 'react'
import { latestArtifactRevisions } from './resourceBindings'
import type {
  ArtifactRevision,
  CatalogComponent,
  ComponentDefinition,
  TopologyDocument,
} from './types'

interface ComponentFormProps {
  componentType: CatalogComponent
  topology: TopologyDocument
  artifactRevisions: ArtifactRevision[]
  component?: ComponentDefinition
  onCancel: () => void
  onSubmit: (component: ComponentDefinition) => Promise<void>
}

function parameterValue(
  value: unknown,
  fallback: number | null,
): string {
  if (typeof value === 'number') return String(value)
  if (value && typeof value === 'object') {
    const scalar = value as Record<string, unknown>
    if (typeof scalar.value_si === 'number') {
      return String(scalar.value_si)
    }
    if (typeof scalar.value === 'number') {
      return String(scalar.value)
    }
  }
  return fallback === null ? '' : String(fallback)
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
  onCancel,
  onSubmit,
}: ComponentFormProps) {
  const [componentId, setComponentId] = useState(() =>
    component?.id ?? suggestedId(componentType, topology),
  )
  const [label, setLabel] = useState(component?.label ?? '')
  const [bindings, setBindings] = useState<Record<string, string>>({
    ...component?.media,
    ...component?.materials,
  })
  const [artifacts, setArtifacts] = useState<Record<string, string>>({
    ...component?.artifacts,
  })
  const [parameters, setParameters] = useState<Record<string, string>>(() =>
    Object.fromEntries(
      componentType.parameters.map((parameter) => [
        parameter.name,
        parameterValue(
          component?.parameters?.[parameter.name],
          parameter.default_value_si,
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
    const media: Record<string, string> = {}
    const materials: Record<string, string> = {}
    for (const port of bindingPorts) {
      const value = bindings[port.name]
      if (!value) {
        setFormError(`Select a ${port.domain} binding for ${port.name}.`)
        return
      }
      if (port.domain === 'fluid') media[port.name] = value
      else materials[port.name] = value
    }
    const numericParameters: Record<string, number> = {}
    for (const descriptor of componentType.parameters) {
      const raw = parameters[descriptor.name]?.trim() ?? ''
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
      numericParameters[descriptor.name] = value
    }
    for (const descriptor of componentType.artifacts) {
      if (descriptor.required && !artifacts[descriptor.role]?.trim()) {
        setFormError(`Artifact binding ${descriptor.role} is required.`)
        return
      }
    }

    const component: ComponentDefinition = {
      id: trimmedId,
      kind: componentType.kind,
      version: componentType.version,
    }
    if (label.trim()) component.label = label.trim()
    if (Object.keys(media).length) component.media = media
    if (Object.keys(materials).length) component.materials = materials
    const artifactBindings = Object.fromEntries(
      Object.entries(artifacts)
        .map(([key, value]) => [key, value.trim()])
        .filter(([, value]) => Boolean(value)),
    )
    if (Object.keys(artifactBindings).length) {
      component.artifacts = artifactBindings
    }
    if (Object.keys(numericParameters).length) {
      component.parameters = numericParameters
    }

    setSubmitting(true)
    try {
      await onSubmit(component)
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
            <span className="eyebrow">Catalog-driven instance</span>
            <h2>{component ? 'Edit component' : 'Add component'}</h2>
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

        {bindingPorts.length > 0 && (
          <fieldset>
            <legend>Medium and material bindings</legend>
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

        {componentType.parameters.length > 0 && (
          <fieldset>
            <legend>Parameters · SI values</legend>
            <div className="form-grid">
              {componentType.parameters.map((parameter) => (
                <label key={parameter.name}>
                  <span>
                    {parameter.name}
                    <small>{parameter.dimension}</small>
                  </span>
                  <input
                    type="number"
                    step="any"
                    value={parameters[parameter.name] ?? ''}
                    min={parameter.lower_bound ?? undefined}
                    max={parameter.upper_bound ?? undefined}
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

        {componentType.artifacts.length > 0 && (
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
