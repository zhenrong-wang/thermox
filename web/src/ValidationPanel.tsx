import { useEffect, useMemo, useState } from 'react'
import type {
  ArtifactRevision,
  ProjectModelValidation,
  ValidationDiagnostic,
} from './types'

interface ValidationPanelProps {
  artifactRevisions: ArtifactRevision[]
  requiredArtifactIds: string[]
  preferredArtifactRevisionIds: Record<string, string>
  result?: ProjectModelValidation
  validating: boolean
  onValidate: (artifactRevisionIds: string[]) => Promise<void>
  onInspectDiagnostic: (diagnostic: ValidationDiagnostic) => void
}

function diagnosticLocation(diagnostic: ValidationDiagnostic) {
  return [
    diagnostic.component_id,
    diagnostic.port_name,
    diagnostic.connection_id,
    diagnostic.json_path,
  ]
    .filter(Boolean)
    .join(' · ')
}

export function ValidationPanel({
  artifactRevisions,
  requiredArtifactIds,
  preferredArtifactRevisionIds,
  result,
  validating,
  onValidate,
  onInspectDiagnostic,
}: ValidationPanelProps) {
  const revisionsByArtifact = useMemo(() => {
    const grouped = new Map<string, ArtifactRevision[]>()
    for (const revision of artifactRevisions) {
      const entries = grouped.get(revision.artifact_id) ?? []
      entries.push(revision)
      grouped.set(revision.artifact_id, entries)
    }
    for (const entries of grouped.values()) {
      entries.sort(
        (left, right) => right.revision_number - left.revision_number,
      )
    }
    return grouped
  }, [artifactRevisions])
  const [selections, setSelections] = useState<Record<string, string>>(() =>
    Object.fromEntries(
      requiredArtifactIds.map((artifactId) => [
        artifactId,
        preferredArtifactRevisionIds[artifactId] ??
          revisionsByArtifact.get(artifactId)?.[0]?.artifact_revision_id ??
          '',
      ]),
    ),
  )
  useEffect(() => {
    setSelections((current) =>
      Object.fromEntries(
        requiredArtifactIds.map((artifactId) => [
          artifactId,
          preferredArtifactRevisionIds[artifactId] ??
            current[artifactId] ??
            revisionsByArtifact.get(artifactId)?.[0]
              ?.artifact_revision_id ??
            '',
        ]),
      ),
    )
  }, [
    preferredArtifactRevisionIds,
    requiredArtifactIds,
    revisionsByArtifact,
  ])
  const missing = requiredArtifactIds.filter(
    (artifactId) => !selections[artifactId],
  )
  const compilation = result?.validation.compilation
  const diagnostics = result?.validation.diagnostics ?? []

  return (
    <section className="validation-panel">
      <header>
        <div>
          <span className="section-kicker">Compiler authority</span>
          <h2>Validate exact revision set</h2>
          <p>
            Resolve artifacts and compile this topology/case pair before
            creating a run configuration.
          </p>
        </div>
        <button
          type="button"
          className="primary-button"
          disabled={validating || missing.length > 0}
          onClick={() =>
            void onValidate(
              requiredArtifactIds.map((artifactId) => selections[artifactId]),
            ).catch(() => {
              // The workspace operation banner exposes transport diagnostics.
            })
          }
        >
          {validating ? 'Validating…' : 'Compile and validate'}
        </button>
      </header>

      <div className="artifact-resolution">
        {!requiredArtifactIds.length && (
          <span className="artifact-free">No engineering artifacts required.</span>
        )}
        {requiredArtifactIds.map((artifactId) => {
          const revisions = revisionsByArtifact.get(artifactId) ?? []
          return (
            <label key={artifactId}>
              <span>{artifactId}</span>
              <select
                value={selections[artifactId] ?? ''}
                onChange={(event) =>
                  setSelections((current) => ({
                    ...current,
                    [artifactId]: event.target.value,
                  }))
                }
              >
                <option value="">
                  {revisions.length
                    ? 'Select exact revision'
                    : 'Missing project artifact'}
                </option>
                {revisions.map((revision) => (
                  <option
                    key={revision.artifact_revision_id}
                    value={revision.artifact_revision_id}
                  >
                    r{revision.revision_number} ·{' '}
                    {revision.content.checksum.slice(7, 19)}
                  </option>
                ))}
              </select>
            </label>
          )
        })}
      </div>

      {missing.length > 0 && (
        <div className="validation-warning">
          Missing immutable project revisions for: {missing.join(', ')}.
        </div>
      )}

      {result && (
        <div className="validation-result">
          <div className="validation-summary">
            <span
              className={
                compilation?.compiled
                  ? 'validation-state valid'
                  : 'validation-state invalid'
              }
            >
              {compilation?.compiled ? 'Compiled' : 'Not compiled'}
            </span>
            <div>
              <strong>{compilation?.variable_count ?? 0}</strong>
              <span>variables</span>
            </div>
            <div>
              <strong>{compilation?.equation_count ?? 0}</strong>
              <span>equations</span>
            </div>
            <code>
              {compilation?.catalog_fingerprint.slice(0, 18) || 'no catalog'}
            </code>
          </div>
          <div className="validation-provenance">
            <span>
              Model <code>{result.model_checksum.slice(7, 19)}</code>
            </span>
            <span>
              Case <code>{result.case_checksum.slice(7, 19)}</code>
            </span>
            <span>
              Artifacts <strong>{result.artifact_revisions.length}</strong>
            </span>
          </div>
          <div className="diagnostic-list">
            {!diagnostics.length && (
              <div className="diagnostic-empty">
                No compiler diagnostics were emitted.
              </div>
            )}
            {diagnostics.map((diagnostic, index) => (
              <article
                key={`${diagnostic.code}-${index}`}
                className={`diagnostic-card ${diagnostic.severity}`}
              >
                <header>
                  <span>{diagnostic.severity}</span>
                  <code>{diagnostic.code}</code>
                  <small>{diagnostic.stage}</small>
                </header>
                <p>{diagnostic.message}</p>
                {diagnosticLocation(diagnostic) && (
                  <code className="diagnostic-location">
                    {diagnosticLocation(diagnostic)}
                  </code>
                )}
                {diagnostic.suggestions.length > 0 && (
                  <ul>
                    {diagnostic.suggestions.map((suggestion) => (
                      <li key={suggestion}>{suggestion}</li>
                    ))}
                  </ul>
                )}
                {(diagnostic.component_id || diagnostic.connection_id) && (
                  <button
                    type="button"
                    className="diagnostic-inspect"
                    onClick={() => onInspectDiagnostic(diagnostic)}
                  >
                    Inspect on canvas →
                  </button>
                )}
              </article>
            ))}
          </div>
        </div>
      )}
    </section>
  )
}
