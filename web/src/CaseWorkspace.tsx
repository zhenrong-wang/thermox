import { useMemo, useState, type FormEvent } from 'react'
import { buildMetadataEdits, buildScalarEdit } from './caseAuthoring'
import { studyMode, studyModes } from './studyIntent'
import { useDisplayUnits } from './DisplayUnitsContext'
import {
  dimensionForUnit,
  displayValue,
  supportedCaseUnits,
  type DisplayUnitProfile,
} from './displayUnits'
import { formatResultValue } from './resultPresentation'
import { DefinitionOverview } from './DefinitionOverview'
import { definitionIssues } from './definitionReadiness'
import { StudyPreparationStrip } from './StudyPreparationStrip'
import { buildStudyPreparationStages } from './studyPreparation'
import { ValidationPanel } from './ValidationPanel'
import type {
  ArtifactRevision,
  Catalog,
  CaseDocument,
  CaseEditOperation,
  CaseRevision,
  CaseScalarField,
  ProjectModelValidation,
  ScalarValue,
  CatalogUnitDimension,
  TopologyDocument,
  ValidationDiagnostic,
} from './types'

interface CaseWorkspaceProps {
  revision?: CaseRevision
  publishing: boolean
  operationError: string
  operationStatus: string
  artifactRevisions: ArtifactRevision[]
  requiredArtifactIds: string[]
  artifactSelections: Record<string, string>
  onArtifactSelectionChange: (artifactId: string, revisionId: string) => void
  topology?: TopologyDocument
  catalog?: Catalog
  unresolvedArtifactCount: number
  exactRevisionCompiled: boolean
  hasPublishedStudy: boolean
  validationResult?: ProjectModelValidation
  validating: boolean
  onDismissOperation: () => void
  onEdit: (operations: CaseEditOperation[], message: string) => Promise<void>
  onValidate: (artifactRevisionIds: string[]) => Promise<void>
  onInspectComponent: (componentId: string) => void
  onInspectDiagnostic: (diagnostic: ValidationDiagnostic) => void
  onPublishStudy: () => void
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
  artifactSelections,
  onArtifactSelectionChange,
  topology,
  catalog,
  unresolvedArtifactCount,
  exactRevisionCompiled,
  hasPublishedStudy,
  validationResult,
  validating,
  onDismissOperation,
  onEdit,
  onValidate,
  onInspectComponent,
  onInspectDiagnostic,
  onPublishStudy,
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
  const intent = studyMode(activeCase.mode)
  const boundaryValueCount =
    Object.keys(activeCase.fixed_values ?? {}).length +
    Object.keys(activeCase.parameter_overrides ?? {}).length
  const localDefinitionIssueCount = definitionIssues(topology, catalog).length
  const preparationStages = buildStudyPreparationStages({
    recognizedIntent: Boolean(intent),
    localDefinitionIssueCount,
    boundaryValueCount,
    requiredArtifactCount: requiredArtifactIds.length,
    unresolvedArtifactCount,
    exactRevisionCompiled,
    hasPublishedStudy,
  })

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
        <StudyPreparationStrip stages={preparationStages} />

        <section id="study-intent" className="study-preparation-stage">
          <header className="study-preparation-heading">
            <span>1</span>
            <div>
              <h2>Intent and calculation mode</h2>
              <p>
                State the engineering question before entering numerical
                constraints.
              </p>
            </div>
          </header>
          <div className="study-intent-card">
            <div>
              <span className="section-kicker">Engineering question</span>
              <h2>{intent?.title ?? activeCase.mode}</h2>
              <p>
                {intent?.description ??
                  'This persisted operating mode is not recognized by the current client catalog.'}
              </p>
            </div>
            <span>{intent?.execution ?? 'unknown'} execution</span>
          </div>
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
              <select
                value={mode}
                onChange={(event) => setMode(event.target.value)}
              >
                {studyModes.map((item) => (
                  <option key={item.id} value={item.id}>
                    {item.title}
                  </option>
                ))}
              </select>
            </label>
            <button
              type="submit"
              className="secondary-button"
              disabled={publishing}
            >
              Publish metadata
            </button>
          </form>
        </section>

        <section id="study-inputs" className="study-preparation-stage">
          <header className="study-preparation-heading">
            <span>2</span>
            <div>
              <h2>Physical definitions and boundary conditions</h2>
              <p>
                Complete component inputs, then declare case-specific fixed
                values, overrides, guesses, and numeric options.
              </p>
            </div>
          </header>
          <DefinitionOverview
            topology={topology}
            catalog={catalog}
            caseRevision={revision}
            requiredArtifactCount={requiredArtifactIds.length}
            unresolvedArtifactCount={unresolvedArtifactCount}
            compiled={exactRevisionCompiled}
            onInspectComponent={onInspectComponent}
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
        </section>

        <section id="study-validation" className="study-preparation-stage">
          <header className="study-preparation-heading">
            <span>3</span>
            <div>
              <h2>Engineering data and authoritative compilation</h2>
              <p>
                Select exact immutable artifact revisions and ask the service
                to compile this exact input set.
              </p>
            </div>
          </header>
          <ValidationPanel
            artifactRevisions={artifactRevisions}
            requiredArtifactIds={requiredArtifactIds}
            artifactSelections={artifactSelections}
            onArtifactSelectionChange={onArtifactSelectionChange}
            result={validationResult}
            validating={validating}
            onValidate={onValidate}
            onInspectDiagnostic={onInspectDiagnostic}
          />
        </section>

        <section
          id="study-publication"
          className="study-preparation-stage study-publication-stage"
        >
          <header className="study-preparation-heading">
            <span>4</span>
            <div>
              <h2>Outputs and immutable Study</h2>
              <p>
                Choose retained outputs and engineering acceptance criteria,
                then pin the validated input revisions.
              </p>
            </div>
          </header>
          <div
            className={`study-publication-card${
              hasPublishedStudy ? ' complete' : ''
            }`}
          >
            <div>
              <strong>
                {hasPublishedStudy
                  ? 'Study published for this input selection'
                  : exactRevisionCompiled
                    ? 'Validated inputs are ready to publish'
                    : 'Compilation must pass before publication'}
              </strong>
              <p>
                The Study remains separate from solver settings: it declares intent,
                exact inputs, outputs, and acceptance evidence.
              </p>
            </div>
            <button
              type="button"
              className="primary-button"
              disabled={!exactRevisionCompiled || publishing}
              onClick={onPublishStudy}
            >
              {hasPublishedStudy
                ? 'Revise Study outputs'
                : 'Define outputs and publish'}
            </button>
          </div>
        </section>
      </div>
    </section>
  )
}
