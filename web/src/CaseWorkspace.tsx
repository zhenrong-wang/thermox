import { useMemo, useState, type FormEvent } from 'react'
import { buildMetadataEdits, buildScalarEdit } from './caseAuthoring'
import { caseModes } from './CaseCreateForm'
import { useDisplayUnits } from './DisplayUnitsContext'
import {
  dimensionForUnit,
  displayValue,
  supportedCaseUnits,
  type DisplayUnitProfile,
} from './displayUnits'
import { formatResultValue } from './resultPresentation'
import { ValidationPanel } from './ValidationPanel'
import type {
  ArtifactRevision,
  CaseDocument,
  CaseEditOperation,
  CaseRevision,
  CaseScalarField,
  ProjectModelValidation,
  ScalarValue,
  CatalogUnitDimension,
} from './types'

interface CaseWorkspaceProps {
  revision?: CaseRevision
  publishing: boolean
  operationError: string
  operationStatus: string
  artifactRevisions: ArtifactRevision[]
  requiredArtifactIds: string[]
  validationResult?: ProjectModelValidation
  validating: boolean
  onDismissOperation: () => void
  onEdit: (operations: CaseEditOperation[], message: string) => Promise<void>
  onValidate: (artifactRevisionIds: string[]) => Promise<void>
  onCreate: () => void
}

const scalarSections: Array<{
  field: CaseScalarField
  documentKey: keyof CaseDocument['case']
  title: string
  help: string
}> = [
  {
    field: 'parameter_override',
    documentKey: 'parameter_overrides',
    title: 'Parameter overrides',
    help: 'Case-specific values for registered component parameters.',
  },
  {
    field: 'fixed_value',
    documentKey: 'fixed_values',
    title: 'Fixed values',
    help: 'Boundary conditions and other constrained model variables.',
  },
  {
    field: 'initial_guess',
    documentKey: 'initial_guesses',
    title: 'Initial guesses',
    help: 'Starting values for nonlinear or transient initialization.',
  },
  {
    field: 'solver_option',
    documentKey: 'solver_options',
    title: 'Solver options',
    help: 'Case-level numeric options represented as scalar values.',
  },
]

function scalarParts(
  value: ScalarValue,
  profile: DisplayUnitProfile,
  unitDimensions: readonly CatalogUnitDimension[],
): { value: string; unit: string } {
  if (typeof value === 'number') return { value: String(value), unit: '' }
  const dimension = dimensionForUnit(value.unit, unitDimensions)
  if (!dimension) return { value: String(value.value), unit: value.unit }
  const displayed = displayValue(
    value.value,
    dimension,
    profile,
    unitDimensions,
  )
  return {
    value: formatResultValue(displayed.value),
    unit: displayed.unit,
  }
}

export function CaseWorkspace({
  revision,
  publishing,
  operationError,
  operationStatus,
  artifactRevisions,
  requiredArtifactIds,
  validationResult,
  validating,
  onDismissOperation,
  onEdit,
  onValidate,
  onCreate,
}: CaseWorkspaceProps) {
  const { profile, unitDimensions } = useDisplayUnits()
  const caseUnits = useMemo(
    () =>
      [
        ...new Set([
          ...supportedCaseUnits,
          ...unitDimensions.flatMap((dimension) =>
            dimension.accepted_units.flatMap((accepted) => [
              accepted.symbol,
              ...accepted.aliases,
            ]),
          ),
        ]),
      ].sort(),
    [unitDimensions],
  )
  const document = revision?.case_document
  const simulationCase = document?.case
  const [label, setLabel] = useState(simulationCase?.label ?? '')
  const [mode, setMode] = useState(
    simulationCase?.mode ?? 'steady_state_design',
  )
  const [scalarField, setScalarField] =
    useState<CaseScalarField>('fixed_value')
  const [key, setKey] = useState('')
  const [value, setValue] = useState('')
  const [unit, setUnit] = useState('')
  const [formError, setFormError] = useState('')

  if (!revision || !simulationCase) {
    return (
      <section className="case-workspace">
        {operationError && (
          <div className="operation-banner is-error">
            {operationError}
            <button type="button" onClick={onDismissOperation}>
              ×
            </button>
          </div>
        )}
        <div className="case-empty">
          <div className="empty-orbit" />
          <h2>No operating case selected</h2>
          <p>Cases hold boundary conditions independently of topology.</p>
          <button type="button" className="primary-button" onClick={onCreate}>
            Create operating case
          </button>
        </div>
      </section>
    )
  }
  const activeCase = simulationCase

  async function updateMetadata(event: FormEvent) {
    event.preventDefault()
    setFormError('')
    const operations = buildMetadataEdits(activeCase, label, mode)
    if (!operations.length) {
      setFormError('No metadata changes to publish.')
      return
    }
    try {
      await onEdit(operations, `Updated case ${activeCase.id}.`)
    } catch (reason) {
      setFormError(reason instanceof Error ? reason.message : 'Edit rejected.')
    }
  }

  async function addScalar(event: FormEvent) {
    event.preventDefault()
    setFormError('')
    try {
      const operation = buildScalarEdit(scalarField, key, value, unit)
      await onEdit(
        [operation],
        `Updated ${key.trim()}.`,
      )
      setKey('')
      setValue('')
      setUnit('')
    } catch (reason) {
      setFormError(reason instanceof Error ? reason.message : 'Edit rejected.')
    }
  }

  async function removeScalar(field: CaseScalarField, scalarKey: string) {
    setFormError('')
    try {
      await onEdit(
        [{ action: 'remove', field, key: scalarKey }],
        `Removed ${scalarKey}.`,
      )
    } catch (reason) {
      setFormError(reason instanceof Error ? reason.message : 'Edit rejected.')
    }
  }

  return (
    <section className="case-workspace">
      <div className="case-toolbar">
        <div>
          <span className="eyebrow">Immutable operating case</span>
          <h1>{activeCase.label || activeCase.id}</h1>
        </div>
        <div className="revision-chip">
          <span>CASE r{revision.revision_number}</span>
          <code>{revision.checksum.slice(7, 19)}</code>
        </div>
      </div>
      {(operationError || operationStatus || publishing) && (
        <div
          className={`operation-banner${operationError ? ' is-error' : ''}`}
        >
          {publishing
            ? 'Publishing immutable case revision…'
            : operationError || operationStatus}
          <button type="button" onClick={onDismissOperation}>
            ×
          </button>
        </div>
      )}
      <div className="case-editor-scroll">
        <form className="case-metadata" onSubmit={updateMetadata}>
          <div>
            <span className="section-kicker">Case identity</span>
            <strong>{activeCase.id}</strong>
          </div>
          <label>
            <span>Label</span>
            <input
              value={label}
              placeholder="Optional"
              onChange={(event) => setLabel(event.target.value)}
            />
          </label>
          <label>
            <span>Mode</span>
            <select value={mode} onChange={(event) => setMode(event.target.value)}>
              {caseModes.map((item) => (
                <option key={item} value={item}>
                  {item}
                </option>
              ))}
            </select>
          </label>
          <button type="submit" className="secondary-button" disabled={publishing}>
            Publish metadata
          </button>
        </form>

        <ValidationPanel
          artifactRevisions={artifactRevisions}
          requiredArtifactIds={requiredArtifactIds}
          result={validationResult}
          validating={validating}
          onValidate={onValidate}
        />

        {scalarSections.map((section) => {
          const entries = Object.entries(
            (activeCase[section.documentKey] ?? {}) as Record<
              string,
              ScalarValue
            >,
          )
          return (
            <section className="case-scalar-section" key={section.field}>
              <header>
                <div>
                  <h2>{section.title}</h2>
                  <p>{section.help}</p>
                </div>
                <span>{entries.length}</span>
              </header>
              <div className="scalar-table">
                {!entries.length && <p className="scalar-empty">No values.</p>}
                {entries.map(([scalarKey, scalar]) => {
                  const parts = scalarParts(
                    scalar,
                    profile,
                    unitDimensions,
                  )
                  return (
                    <div className="scalar-row" key={scalarKey}>
                      <code>{scalarKey}</code>
                      <strong>{parts.value}</strong>
                      <span>{parts.unit || 'dimensionless'}</span>
                      <button
                        type="button"
                        disabled={publishing}
                        onClick={() => void removeScalar(section.field, scalarKey)}
                      >
                        ×
                      </button>
                    </div>
                  )
                })}
              </div>
            </section>
          )
        })}

        <form className="scalar-add-form" onSubmit={addScalar}>
          <header>
            <div>
              <span className="section-kicker">Atomic case edit</span>
              <h2>Add or replace scalar</h2>
            </div>
          </header>
          <label>
            <span>Field</span>
            <select
              value={scalarField}
              onChange={(event) =>
                setScalarField(event.target.value as CaseScalarField)
              }
            >
              {scalarSections.map((section) => (
                <option key={section.field} value={section.field}>
                  {section.title}
                </option>
              ))}
            </select>
          </label>
          <label>
            <span>Variable or option key</span>
            <input
              value={key}
              required
              placeholder="compressor.inlet.p"
              onChange={(event) => setKey(event.target.value)}
            />
          </label>
          <label>
            <span>Value</span>
            <input
              type="number"
              step="any"
              value={value}
              required
              onChange={(event) => setValue(event.target.value)}
            />
          </label>
          <label>
            <span>Unit</span>
            <input
              value={unit}
              list="thermox-supported-case-units"
              placeholder="Pa, kPa, K, kg/s…"
              onChange={(event) => setUnit(event.target.value)}
            />
            <datalist id="thermox-supported-case-units">
              {caseUnits.map((item) => (
                <option key={item} value={item} />
              ))}
            </datalist>
          </label>
          <button type="submit" className="primary-button" disabled={publishing}>
            Publish scalar revision
          </button>
        </form>
        {formError && <div className="form-error case-form-error">{formError}</div>}
      </div>
    </section>
  )
}
