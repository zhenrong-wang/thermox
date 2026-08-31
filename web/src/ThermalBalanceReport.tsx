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
  const massResidual = displayValue(
    report.boundary.net_mass_flow_si,
    'mass_flow',
    profile,
    unitDimensions,
  )
  const relativePercent = (value: number | null) =>
    value === null ? 'Not defined' : `${formatResultValue(value * 100)}%`
  const acceptanceLabel = report.closure_acceptance.status
    .replaceAll('_', ' ')
  const uncertaintyLabel = report.uncertainty.status.replaceAll('_', ' ')
  const energyUncertainty = report.uncertainty.energy
    ? power(report.uncertainty.energy.standard_uncertainty_si)
    : null
  const expandedEnergyUncertainty = report.uncertainty.energy
    ? power(report.uncertainty.energy.expanded_uncertainty_si_k2)
    : null
  const massUncertainty = report.uncertainty.mass
    ? displayValue(
        report.uncertainty.mass.standard_uncertainty_si,
        'mass_flow',
        profile,
        unitDimensions,
      )
    : null
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
      <div className={`balance-closure-evaluation is-${report.closure_acceptance.status}`}>
        <div>
          <span>Declared acceptance</span>
          <strong>{acceptanceLabel}</strong>
          <small>{report.closure_acceptance.interpretation}</small>
        </div>
        <div>
          <span>Energy closure</span>
          <strong>{relativePercent(report.boundary.relative_energy_closure)}</strong>
          <small>{formatResultValue(residual.value)} {residual.unit}</small>
        </div>
        <div>
          <span>Mass closure</span>
          <strong>{relativePercent(report.boundary.relative_mass_closure)}</strong>
          <small>{formatResultValue(massResidual.value)} {massResidual.unit}</small>
        </div>
        <div>
          <span>Criteria coverage</span>
          <strong>{report.closure_acceptance.complete ? 'Mass + energy' : 'Incomplete'}</strong>
          <small>{report.closure_acceptance.criteria.length} matched criteria</small>
        </div>
      </div>
      <div className={`balance-uncertainty-summary is-${report.uncertainty.status}`}>
        <div>
          <span>Measurement uncertainty</span>
          <strong>{uncertaintyLabel}</strong>
          <small>{report.uncertainty.interpretation}</small>
        </div>
        <div>
          <span>Energy standard uncertainty</span>
          <strong>{energyUncertainty
            ? `${formatResultValue(energyUncertainty.value)} ${energyUncertainty.unit}`
            : 'Not evaluated'}</strong>
          <small>{expandedEnergyUncertainty
            ? `Expanded k=2: ${formatResultValue(expandedEnergyUncertainty.value)} ${expandedEnergyUncertainty.unit}`
            : 'Complete boundary coverage required'}</small>
        </div>
        <div>
          <span>Mass standard uncertainty</span>
          <strong>{massUncertainty
            ? `${formatResultValue(massUncertainty.value)} ${massUncertainty.unit}`
            : report.uncertainty.mass_applicable
              ? 'Not evaluated'
              : 'Not applicable'}</strong>
          <small>{report.uncertainty.correlation_count} declared correlations</small>
        </div>
        <div>
          <span>Metrology source</span>
          <strong>{report.uncertainty.source?.model_id ?? 'Not attached'}</strong>
          <small>{report.uncertainty.source
            ? `${report.uncertainty.source.reference}${
              report.uncertainty.source.artifact_revision_id
                ? ` · ${report.uncertainty.source.artifact_revision_id}` : ''}`
            : report.uncertainty.limitations[0]}</small>
        </div>
      </div>
      <footer>
        <span>Informative profile; clause-level conformity is not demonstrated. Model <code>{report.provenance.model_revision_id || 'unavailable'}</code></span>
        <code>{report.result_checksum}</code>
      </footer>
    </section>
  )
}
