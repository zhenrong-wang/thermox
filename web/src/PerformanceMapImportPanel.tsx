import { useEffect, useState, type ChangeEvent } from 'react'
import {
  mapCurvesFromTable,
  parseDelimitedTable,
  suggestedColumn,
  type DelimitedTable,
} from './performanceMapImport'
import type { PerformanceMapArtifactDefinition } from './types'

interface PerformanceMapImportPanelProps {
  primaryName: string
  familyName: string
  outputNames: string[]
  onApply: (curves: PerformanceMapArtifactDefinition['curves']) => void
}

function ColumnSelect({
  label,
  headers,
  value,
  onChange,
}: {
  label: string
  headers: string[]
  value: string
  onChange: (value: string) => void
}) {
  return (
    <label>
      <span>{label}</span>
      <select value={value} onChange={(event) => onChange(event.target.value)}>
        <option value="">Select column</option>
        {headers.map((header) => <option key={header}>{header}</option>)}
      </select>
    </label>
  )
}

export function PerformanceMapImportPanel({
  primaryName,
  familyName,
  outputNames,
  onApply,
}: PerformanceMapImportPanelProps) {
  const [table, setTable] = useState<DelimitedTable>()
  const [fileName, setFileName] = useState('')
  const [primaryColumn, setPrimaryColumn] = useState('')
  const [familyColumn, setFamilyColumn] = useState('')
  const [outputColumns, setOutputColumns] = useState<string[]>([])
  const [error, setError] = useState('')
  const [status, setStatus] = useState('')

  useEffect(() => {
    if (!table) return
    setPrimaryColumn((current) => suggestedColumn(table.headers, primaryName, current))
    setFamilyColumn((current) => suggestedColumn(table.headers, familyName, current))
    setOutputColumns((current) => outputNames.map(
      (name, index) => suggestedColumn(table.headers, name, current[index] ?? ''),
    ))
  }, [familyName, outputNames, primaryName, table])

  async function loadFile(event: ChangeEvent<HTMLInputElement>) {
    const file = event.target.files?.[0]
    if (!file) return
    setError('')
    setStatus('')
    try {
      const parsed = parseDelimitedTable(await file.text())
      setFileName(file.name)
      setTable(parsed)
      setPrimaryColumn(suggestedColumn(parsed.headers, primaryName, parsed.headers[1] ?? ''))
      setFamilyColumn(suggestedColumn(parsed.headers, familyName, parsed.headers[0] ?? ''))
      setOutputColumns(outputNames.map((name, index) =>
        suggestedColumn(parsed.headers, name, parsed.headers[index + 2] ?? ''),
      ))
    } catch (reason) {
      setTable(undefined)
      setFileName('')
      setError(reason instanceof Error ? reason.message : 'The table could not be read.')
    }
  }

  function apply() {
    if (!table) return
    setError('')
    setStatus('')
    try {
      const curves = mapCurvesFromTable(table, {
        primary: primaryColumn,
        family: familyColumn,
        outputs: outputColumns,
      })
      onApply(curves)
      setStatus(
        `Applied ${curves.reduce((sum, curve) => sum + curve.samples.length, 0)} operating points across ${curves.length} family curves.`,
      )
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : 'The mapped table was rejected.')
    }
  }

  return (
    <fieldset className="performance-map-import">
      <legend>Tabular import</legend>
      <div className="performance-map-import-source">
        <label>
          <span>CSV, TSV, or semicolon-delimited file</span>
          <input
            type="file"
            accept=".csv,.tsv,.txt,text/csv,text/tab-separated-values"
            onChange={(event) => { void loadFile(event) }}
          />
        </label>
        <p>
          Import happens locally. Use long-form data with one row per operating point;
          columns are mapped explicitly before replacing the curve editor.
        </p>
      </div>
      {table && (
        <div className="performance-map-column-mapping">
          <div className="import-table-summary">
            <strong>{fileName}</strong>
            <span>{table.rows.length} rows · {table.headers.length} columns</span>
          </div>
          <div className="form-grid">
            <ColumnSelect label={`Family · ${familyName || 'unnamed'}`} headers={table.headers} value={familyColumn} onChange={setFamilyColumn} />
            <ColumnSelect label={`Primary · ${primaryName || 'unnamed'}`} headers={table.headers} value={primaryColumn} onChange={setPrimaryColumn} />
            {outputNames.map((name, index) => (
              <ColumnSelect
                key={index}
                label={`Output · ${name || index + 1}`}
                headers={table.headers}
                value={outputColumns[index] ?? ''}
                onChange={(value) => setOutputColumns((current) =>
                  outputNames.map((_, itemIndex) => itemIndex === index ? value : current[itemIndex] ?? ''),
                )}
              />
            ))}
          </div>
          <button type="button" className="secondary-button import-apply-button" onClick={apply}>
            Apply mapped rows
          </button>
        </div>
      )}
      {error && <p className="form-error">{error}</p>}
      {status && <p className="import-success">{status}</p>}
    </fieldset>
  )
}
