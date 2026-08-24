import { useMemo, useState, type FormEvent } from 'react'
import type {
  CatalogUnitDimension,
  ConnectorDomain,
  ExpressionComponentDefinition,
  ProjectComponentCatalogEntry,
} from './types'

interface ExpressionComponentFormProps {
  connectorDomains: ConnectorDomain[]
  unitDimensions: CatalogUnitDimension[]
  base?: ProjectComponentCatalogEntry
  onCancel: () => void
  onSubmit: (
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: ExpressionComponentDefinition,
  ) => Promise<void>
}

type PortDraft = ExpressionComponentDefinition['ports'][number]
type ParameterDraft = ExpressionComponentDefinition['parameters'][number]
type EquationDraft = ExpressionComponentDefinition['equations'][number]

const defaultPorts: PortDraft[] = [
  {
    name: 'input',
    domain: 'signal',
    direction: 'in',
    maximum_connections: 1,
  },
  {
    name: 'output',
    domain: 'signal',
    direction: 'out',
    maximum_connections: 1,
  },
]

function optionalNumber(value: string): number | null {
  if (!value.trim()) return null
  const parsed = Number(value)
  if (!Number.isFinite(parsed)) throw new Error(`"${value}" is not a finite number.`)
  return parsed
}

function numberText(value: number | null) {
  return value === null ? '' : String(value)
}

function nextPatchVersion(version: string) {
  const match = /^(\d+)\.(\d+)\.(\d+)$/.exec(version.trim())
  return match
    ? `${match[1]}.${match[2]}.${Number(match[3]) + 1}`
    : version
}

export function ExpressionComponentForm({
  connectorDomains,
  unitDimensions,
  base,
  onCancel,
  onSubmit,
}: ExpressionComponentFormProps) {
  const safeDomains = useMemo(
    () =>
      connectorDomains.filter(
        (domain) =>
          !domain.variables.some((variable) => variable.expand_species),
      ),
    [connectorDomains],
  )
  const defaultDomain =
    safeDomains.find((domain) => domain.domain === 'signal')?.domain ??
    safeDomains[0]?.domain ??
    ''
  const [artifactId, setArtifactId] = useState(
    base?.source.artifact_id ?? 'custom-component',
  )
  const [kind, setKind] = useState(
    base?.definition.kind ?? 'custom.signal.component',
  )
  const [version, setVersion] = useState(
    base ? nextPatchVersion(base.definition.version) : '1.0.0',
  )
  const [templateKind, setTemplateKind] = useState(
    base?.definition.template_kind ?? 'custom.signal.component',
  )
  const [displayName, setDisplayName] = useState(
    base?.definition.display_name ?? 'Custom component',
  )
  const [category, setCategory] = useState(
    base?.definition.category ?? 'Project components',
  )
  const [modelName, setModelName] = useState(
    base?.definition.model_name ?? 'Custom expression',
  )
  const [boundaryRole, setBoundaryRole] = useState(
    base?.definition.system_boundary_role ?? '',
  )
  const [ports, setPorts] = useState<PortDraft[]>(
    base?.definition.ports.map((port) => ({ ...port })) ??
      defaultPorts.map((port) => ({
        ...port,
        domain: defaultDomain || port.domain,
      })),
  )
  const [parameters, setParameters] = useState<ParameterDraft[]>(
    base?.definition.parameters.map((parameter) => ({ ...parameter })) ?? [],
  )
  const [equations, setEquations] = useState<EquationDraft[]>(
    base?.definition.equations.map((equation) => ({ ...equation })) ?? [
      {
        name: 'identity',
        expression: 'output.value - input.value',
        residual_scale: 1,
      },
    ],
  )
  const [submitting, setSubmitting] = useState(false)
  const [formError, setFormError] = useState('')

  function updatePort(index: number, patch: Partial<PortDraft>) {
    setPorts((current) =>
      current.map((port, itemIndex) =>
        itemIndex === index ? { ...port, ...patch } : port,
      ),
    )
  }

  function updateParameter(index: number, patch: Partial<ParameterDraft>) {
    setParameters((current) =>
      current.map((parameter, itemIndex) =>
        itemIndex === index ? { ...parameter, ...patch } : parameter,
      ),
    )
  }

  function updateEquation(index: number, patch: Partial<EquationDraft>) {
    setEquations((current) =>
      current.map((equation, itemIndex) =>
        itemIndex === index ? { ...equation, ...patch } : equation,
      ),
    )
  }

  async function submit(event: FormEvent) {
    event.preventDefault()
    setFormError('')
    try {
      if (
        !artifactId.trim() ||
        !kind.trim() ||
        !version.trim() ||
        !templateKind.trim() ||
        !displayName.trim() ||
        !category.trim() ||
        !modelName.trim()
      ) {
        throw new Error(
          'Artifact ID, physical template metadata, component kind, and version are required.',
        )
      }
      if (base && version.trim() === base.definition.version) {
        throw new Error(
          'A changed implementation needs a new component version so existing topologies remain reproducible.',
        )
      }
      if (ports.length === 0 || equations.length === 0) {
        throw new Error('Define at least one port and one residual equation.')
      }
      if (
        ports.some(
          (port) =>
            !port.name.trim() ||
            !port.domain ||
            !Number.isInteger(port.maximum_connections) ||
            port.maximum_connections < 1,
        )
      ) {
        throw new Error('Every port needs a name, domain, and positive connection limit.')
      }
      if (
        parameters.some(
          (parameter) => !parameter.name.trim() || !parameter.dimension,
        )
      ) {
        throw new Error('Every parameter needs a name and registered dimension.')
      }
      if (
        equations.some(
          (equation) =>
            !equation.name.trim() ||
            !equation.expression.trim() ||
            !Number.isFinite(equation.residual_scale) ||
            equation.residual_scale <= 0,
        )
      ) {
        throw new Error('Every equation needs a name, expression, and positive residual scale.')
      }
      setSubmitting(true)
      await onSubmit(
        artifactId.trim(),
        base?.source.artifact_revision_id ?? '',
        {
          schema_version: 'thermox.expression_component/v4',
          kind: kind.trim(),
          version: version.trim(),
          template_kind: templateKind.trim(),
          display_name: displayName.trim(),
          category: category.trim(),
          model_name: modelName.trim(),
          system_boundary_role: boundaryRole.trim(),
          supports_steady: true,
          supports_transient: false,
          default_mode: '',
          ports: ports.map((port) => ({
            ...port,
            name: port.name.trim(),
          })),
          parameters: parameters.map((parameter) => ({
            ...parameter,
            name: parameter.name.trim(),
          })),
          equations: equations.map((equation) => ({
            ...equation,
            name: equation.name.trim(),
            expression: equation.expression.trim(),
          })),
          transient_variables: [],
          internal_variables: [],
          transient_equations: [],
          modes: [],
        },
      )
    } catch (reason) {
      setFormError(
        reason instanceof Error
          ? reason.message
          : 'The component definition was rejected.',
      )
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div className="dialog-backdrop" role="presentation">
      <form
        className="component-dialog expression-component-dialog"
        onSubmit={submit}
      >
        <header>
          <div>
            <span className="eyebrow">Project component registry</span>
            <h2>{base ? 'Publish component revision' : 'Define component'}</h2>
          </div>
          <button type="button" className="icon-button" onClick={onCancel}>
            ×
          </button>
        </header>
        <p className="registry-note">
          <strong>Safe algebraic contract</strong>
          Equations are residuals equal to zero. Use port variables such as
          input.value and SI parameters such as parameter.gain. The service
          validates symbols, dimensions, bounds, and safe expression syntax.
        </p>
        <div className="form-grid">
          <label>
            <span>Artifact ID</span>
            <input
              value={artifactId}
              disabled={Boolean(base)}
              required
              onChange={(event) => setArtifactId(event.target.value)}
            />
          </label>
          <label>
            <span>Component kind</span>
            <input
              value={kind}
              disabled={Boolean(base)}
              required
              onChange={(event) => setKind(event.target.value)}
            />
          </label>
          <label>
            <span>Version</span>
            <input
              value={version}
              required
              onChange={(event) => setVersion(event.target.value)}
            />
          </label>
          <label>
            <span>Physical template kind</span>
            <input
              value={templateKind}
              required
              onChange={(event) => setTemplateKind(event.target.value)}
            />
          </label>
          <label>
            <span>Equipment display name</span>
            <input
              value={displayName}
              required
              onChange={(event) => setDisplayName(event.target.value)}
            />
          </label>
          <label>
            <span>Library category</span>
            <input
              value={category}
              required
              onChange={(event) => setCategory(event.target.value)}
            />
          </label>
          <label>
            <span>Calculation model name</span>
            <input
              value={modelName}
              required
              onChange={(event) => setModelName(event.target.value)}
            />
          </label>
          <label>
            <span>System boundary role <small>optional</small></span>
            <input
              value={boundaryRole}
              onChange={(event) => setBoundaryRole(event.target.value)}
            />
          </label>
        </div>

        <fieldset>
          <legend>Typed ports</legend>
          {ports.map((port, index) => (
            <div className="repeatable-row port-definition-row" key={index}>
              <label>
                <span>Name</span>
                <input
                  value={port.name}
                  required
                  onChange={(event) =>
                    updatePort(index, { name: event.target.value })
                  }
                />
              </label>
              <label>
                <span>Domain</span>
                <select
                  value={port.domain}
                  required
                  onChange={(event) =>
                    updatePort(index, { domain: event.target.value })
                  }
                >
                  {safeDomains.map((domain) => (
                    <option key={domain.domain} value={domain.domain}>
                      {domain.domain}
                    </option>
                  ))}
                </select>
              </label>
              <label>
                <span>Direction</span>
                <select
                  value={port.direction}
                  onChange={(event) =>
                    updatePort(index, {
                      direction: event.target.value as PortDraft['direction'],
                    })
                  }
                >
                  <option value="in">in</option>
                  <option value="out">out</option>
                  <option value="bidirectional">bidirectional</option>
                </select>
              </label>
              <label>
                <span>Max links</span>
                <input
                  type="number"
                  min="1"
                  step="1"
                  value={port.maximum_connections}
                  onChange={(event) =>
                    updatePort(index, {
                      maximum_connections: Number(event.target.value),
                    })
                  }
                />
              </label>
              <button
                type="button"
                className="row-remove-button"
                aria-label={`Remove port ${port.name || index + 1}`}
                onClick={() =>
                  setPorts((current) =>
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
            className="add-row-button"
            onClick={() =>
              setPorts((current) => [
                ...current,
                {
                  name: `port_${current.length + 1}`,
                  domain: defaultDomain,
                  direction: 'in',
                  maximum_connections: 1,
                },
              ])
            }
          >
            + Add port
          </button>
        </fieldset>

        <fieldset>
          <legend>SI parameters</legend>
          {parameters.map((parameter, index) => (
            <div className="repeatable-row parameter-definition-row" key={index}>
              <label>
                <span>Name</span>
                <input
                  value={parameter.name}
                  required
                  onChange={(event) =>
                    updateParameter(index, { name: event.target.value })
                  }
                />
              </label>
              <label>
                <span>Dimension</span>
                <select
                  value={parameter.dimension}
                  required
                  onChange={(event) =>
                    updateParameter(index, { dimension: event.target.value })
                  }
                >
                  {unitDimensions.map((dimension) => (
                    <option
                      key={dimension.dimension}
                      value={dimension.dimension}
                    >
                      {dimension.dimension}
                    </option>
                  ))}
                </select>
              </label>
              <label>
                <span>Default</span>
                <input
                  type="number"
                  inputMode="decimal"
                  step="any"
                  defaultValue={numberText(parameter.default_value_si)}
                  onBlur={(event) => {
                    try {
                      updateParameter(index, {
                        default_value_si: optionalNumber(event.target.value),
                      })
                    } catch {
                      // Keep the last valid numeric value; server validation remains authoritative.
                    }
                  }}
                />
              </label>
              <label>
                <span>Lower</span>
                <input
                  type="number"
                  inputMode="decimal"
                  step="any"
                  defaultValue={numberText(parameter.lower_bound)}
                  onBlur={(event) => {
                    try {
                      updateParameter(index, {
                        lower_bound: optionalNumber(event.target.value),
                      })
                    } catch {
                      // Keep the last valid numeric value.
                    }
                  }}
                />
              </label>
              <label>
                <span>Upper</span>
                <input
                  type="number"
                  inputMode="decimal"
                  step="any"
                  defaultValue={numberText(parameter.upper_bound)}
                  onBlur={(event) => {
                    try {
                      updateParameter(index, {
                        upper_bound: optionalNumber(event.target.value),
                      })
                    } catch {
                      // Keep the last valid numeric value.
                    }
                  }}
                />
              </label>
              <label className="checkbox-label">
                <input
                  type="checkbox"
                  checked={parameter.required}
                  onChange={(event) =>
                    updateParameter(index, { required: event.target.checked })
                  }
                />
                Required
              </label>
              <label className="checkbox-label">
                <input
                  type="checkbox"
                  checked={parameter.lower_inclusive}
                  onChange={(event) =>
                    updateParameter(index, {
                      lower_inclusive: event.target.checked,
                    })
                  }
                />
                Include lower
              </label>
              <label className="checkbox-label">
                <input
                  type="checkbox"
                  checked={parameter.upper_inclusive}
                  onChange={(event) =>
                    updateParameter(index, {
                      upper_inclusive: event.target.checked,
                    })
                  }
                />
                Include upper
              </label>
              <button
                type="button"
                className="row-remove-button"
                aria-label={`Remove parameter ${parameter.name || index + 1}`}
                onClick={() =>
                  setParameters((current) =>
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
            className="add-row-button"
            onClick={() =>
              setParameters((current) => [
                ...current,
                {
                  name: `parameter_${current.length + 1}`,
                  dimension:
                    unitDimensions.find(
                      (dimension) => dimension.dimension === 'dimensionless',
                    )?.dimension ??
                    unitDimensions[0]?.dimension ??
                    '',
                  required: true,
                  default_value_si: null,
                  lower_bound: null,
                  upper_bound: null,
                  lower_inclusive: true,
                  upper_inclusive: true,
                },
              ])
            }
          >
            + Add parameter
          </button>
        </fieldset>

        <fieldset>
          <legend>Residual equations</legend>
          {equations.map((equation, index) => (
            <div className="repeatable-row equation-definition-row" key={index}>
              <label>
                <span>Name</span>
                <input
                  value={equation.name}
                  required
                  onChange={(event) =>
                    updateEquation(index, { name: event.target.value })
                  }
                />
              </label>
              <label className="equation-expression">
                <span>Expression</span>
                <textarea
                  rows={3}
                  value={equation.expression}
                  required
                  onChange={(event) =>
                    updateEquation(index, { expression: event.target.value })
                  }
                />
              </label>
              <label>
                <span>Residual scale</span>
                <input
                  type="number"
                  min="0"
                  step="any"
                  defaultValue={equation.residual_scale}
                  onBlur={(event) =>
                    updateEquation(index, {
                      residual_scale: Number(event.target.value),
                    })
                  }
                />
              </label>
              <button
                type="button"
                className="row-remove-button"
                aria-label={`Remove equation ${equation.name || index + 1}`}
                onClick={() =>
                  setEquations((current) =>
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
            className="add-row-button"
            onClick={() =>
              setEquations((current) => [
                ...current,
                {
                  name: `equation_${current.length + 1}`,
                  expression: '',
                  residual_scale: 1,
                },
              ])
            }
          >
            + Add equation
          </button>
        </fieldset>

        {formError && <p className="form-error">{formError}</p>}
        <footer>
          <button
            type="button"
            className="secondary-button"
            onClick={onCancel}
          >
            Cancel
          </button>
          <button
            type="submit"
            className="primary-button"
            disabled={submitting}
          >
            {submitting
              ? 'Publishing…'
              : base
                ? 'Publish revision'
                : 'Publish component'}
          </button>
        </footer>
      </form>
    </div>
  )
}
