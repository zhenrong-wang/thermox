import { useDisplayUnits } from './DisplayUnitsContext'
import { displayValue } from './displayUnits'
import { formatResultValue } from './resultPresentation'
import type { BalanceReport } from './types'

export function ThermalBalanceReport({
  report,
  exportingFormat,
  exportError,
  onExport,
}: {
  report: BalanceReport
  exportingFormat: 'markdown' | 'csv' | ''
  exportError: string
  onExport: (format: 'markdown' | 'csv') => void
}) {
  const { profile, unitDimensions } = useDisplayUnits()
  const power = (value: number) =>
    displayValue(value, 'power', profile, unitDimensions)
  const input = power(report.boundary.energy_input_si)
  const output = power(report.boundary.energy_output_si)
  const residual = power(report.boundary.net_energy_flow_si)
  const maximum = Math.max(
    Math.abs(report.boundary.energy_input_si),
    Math.abs(report.boundary.energy_output_si),
    1,
  )
  const streams = (direction: 'input' | 'output') =>
    report.boundary_streams.filter(
      (stream) => stream.boundary_direction === direction,
    )

  return (
    <section className="thermal-balance-report">
      <header>
        <div>
          <span className="section-kicker">Standards-profiled accounting</span>
          <h2>Whole-system energy balance</h2>
        </div>
        <div className="balance-report-actions">
          <strong>{report.profile.diagram}</strong>
          <span>{report.profile.conformance}</span>
          <button
            type="button"
            className="secondary-button"
            disabled={Boolean(exportingFormat)}
            onClick={() => onExport('markdown')}
          >
            {exportingFormat === 'markdown' ? 'Exporting…' : 'Markdown'}
          </button>
          <button
            type="button"
            className="secondary-button"
            disabled={Boolean(exportingFormat)}
            onClick={() => onExport('csv')}
          >
            {exportingFormat === 'csv' ? 'Exporting…' : 'CSV'}
          </button>
        </div>
      </header>
      {exportError && (
        <div className="operation-banner is-error">{exportError}</div>
      )}
      <div className="balance-flow-diagram">
        <div className="balance-streams input">
          {streams('input').map((stream) => (
            <span key={`${stream.component_id}.${stream.port_name}`}>
              <strong>{stream.component_id}</strong>
              <small>{formatResultValue(power(Math.abs(stream.energy_flow_si)).value)} {input.unit}</small>
            </span>
          ))}
        </div>
        <div className="balance-system-node">
          <strong>System boundary</strong>
          <span>{report.mode}</span>
          <small>{report.boundary.closure_interpretation}</small>
        </div>
        <div className="balance-streams output">
          {streams('output').map((stream) => (
            <span key={`${stream.component_id}.${stream.port_name}`}>
              <strong>{stream.component_id}</strong>
              <small>{formatResultValue(power(Math.abs(stream.energy_flow_si)).value)} {output.unit}</small>
            </span>
          ))}
        </div>
      </div>
      <div className="balance-accounting-strip">
        <div><span>Input</span><strong>{formatResultValue(input.value)} {input.unit}</strong><i style={{ width: `${Math.abs(report.boundary.energy_input_si) / maximum * 100}%` }} /></div>
        <div><span>Output</span><strong>{formatResultValue(output.value)} {output.unit}</strong><i style={{ width: `${Math.abs(report.boundary.energy_output_si) / maximum * 100}%` }} /></div>
        <div><span>Net / closure</span><strong>{formatResultValue(residual.value)} {residual.unit}</strong></div>
      </div>
      <footer>
        <span>Informative profile; clause-level conformity is not demonstrated. Model <code>{report.provenance.model_revision_id || 'unavailable'}</code></span>
        <code>{report.result_checksum}</code>
      </footer>
    </section>
  )
}
