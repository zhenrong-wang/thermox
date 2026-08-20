import type { ReconciliationResult } from './types'

interface ReconciliationResultPanelProps {
  result?: ReconciliationResult
  loading: boolean
  error: string
  onClose: () => void
}

function value(number: number | null): string {
  if (number === null) return 'unavailable'
  if (!Number.isFinite(number)) return String(number)
  return number.toLocaleString('en-US', { maximumSignificantDigits: 7 })
}

export function ReconciliationResultPanel({
  result,
  loading,
  error,
  onClose,
}: ReconciliationResultPanelProps) {
  return (
    <div className="dialog-backdrop" role="presentation">
      <section className="component-dialog run-config-dialog reconciliation-result">
        <header>
          <div>
            <span className="eyebrow">Calculation evidence</span>
            <h2>Data reconciliation result</h2>
          </div>
          <button type="button" className="icon-button" onClick={onClose}>×</button>
        </header>
        {loading && <p className="form-note">Loading immutable result artifact…</p>}
        {error && <div className="form-error">{error}</div>}
        {result && (
          <>
            <section className="physical-summary-grid">
              <div><span>Converged</span><strong>{result.diagnostics.converged ? 'yes' : 'no'}</strong><small>{result.diagnostics.iterations} iterations</small></div>
              <div><span>Identifiable</span><strong>{result.diagnostics.locally_identifiable ? 'yes' : 'no'}</strong><small>local sensitivity rank</small></div>
              <div><span>Active bounds</span><strong>{result.diagnostics.active_bound_count}</strong><small>{result.diagnostics.locally_bound_limited ? 'solution is bound-limited' : 'not bound-limited'}</small></div>
              <div><span>Held-out cases</span><strong>{result.held_out_results.length}</strong><small>excluded from inference</small></div>
            </section>
            <p className="form-note">{result.diagnostics.message}</p>
            <section className="physical-component-section">
              <header><div><span className="section-kicker">Inferred</span><h2>Adjusted quantities</h2></div></header>
              <div className="engineering-artifact-list">
                {result.inferred_parameters.map((parameter) => (
                  <article key={parameter.id}>
                    <div><strong>{parameter.id}</strong><code>{parameter.dimension || 'dimensionless'}</code></div>
                    <span>{value(parameter.initial_value_si)} → {value(parameter.inferred_value_si)}</span>
                    <small>{parameter.targets.join(', ')}</small>
                  </article>
                ))}
              </div>
            </section>
            <section className="physical-component-section">
              <header><div><span className="section-kicker">Constrained</span><h2>Measurements used by the solve</h2></div></header>
              <div className="engineering-artifact-list">
                {[...result.hard_constraints, ...result.weighted_measurements].map((measurement) => (
                  <article key={`${measurement.case_id}-${measurement.id}`}>
                    <div><strong>{measurement.id}</strong><code>{measurement.case_id}</code></div>
                    <span>normalized residual {value(measurement.normalized_residual)}</span>
                    <small>{measurement.target}</small>
                  </article>
                ))}
              </div>
            </section>
            <section className="physical-component-section">
              <header><div><span className="section-kicker">Held out</span><h2>Independent checks</h2></div></header>
              <div className="engineering-artifact-list">
                {result.held_out_results.flatMap((heldOut) =>
                  heldOut.observations.map((observation) => (
                    <article key={`${heldOut.case_id}-${observation.id}`}>
                      <div><strong>{observation.id}</strong><code>{heldOut.case_id}</code></div>
                      <span>measured {value(observation.measured_si)} · predicted {value(observation.predicted_si)}</span>
                      <small>normalized residual {value(observation.normalized_residual)}</small>
                    </article>
                  )),
                )}
                {!result.held_out_results.length && <p className="form-note">No independent Study was reserved. This run can demonstrate reconciliation, but not predictive credibility.</p>}
              </div>
            </section>
            <section className="physical-component-section">
              <header><div><span className="section-kicker">Identifiability</span><h2>Uncertainty evidence</h2></div></header>
              <div className="engineering-artifact-list">
                {result.parameter_uncertainties.map((uncertainty) => (
                  <article key={uncertainty.parameter_id}>
                    <div><strong>{uncertainty.parameter_id}</strong><code>{uncertainty.dimension || 'dimensionless'}</code></div>
                    <span>standard uncertainty {value(uncertainty.standard_uncertainty_si)}</span>
                    <small>{uncertainty.interpretation}</small>
                  </article>
                ))}
                {result.profile_likelihood_intervals.map((interval) => (
                  <article key={`profile-${interval.parameter_id}`}>
                    <div><strong>{interval.parameter_id}</strong><code>profile likelihood</code></div>
                    <span>{interval.succeeded ? 'interval resolved' : 'incomplete'}</span>
                    <small>{interval.message}</small>
                  </article>
                ))}
                {result.joint_confidence_region && (
                  <article>
                    <div><strong>Joint parameter region</strong><code>local ellipsoid</code></div>
                    <span>{result.joint_confidence_region.succeeded
                      ? `${result.joint_confidence_region.parameter_ids.length} parameters · Δobjective ${value(result.joint_confidence_region.requested_objective_increase)}`
                      : 'unavailable'}</span>
                    <small>{result.joint_confidence_region.message}. {result.joint_confidence_region.interpretation}</small>
                  </article>
                )}
              </div>
            </section>
          </>
        )}
      </section>
    </div>
  )
}
