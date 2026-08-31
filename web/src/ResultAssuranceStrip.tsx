import type { ResultAssuranceLayer } from './resultAssurance'

interface ResultAssuranceStripProps {
  layers: ResultAssuranceLayer[]
}

const statusLabel: Record<ResultAssuranceLayer['status'], string> = {
  supported: 'Supported',
  failed: 'Failed',
  review: 'Review',
  not_evaluated: 'Not evaluated',
}

export function ResultAssuranceStrip({ layers }: ResultAssuranceStripProps) {
  return (
    <section className="result-assurance-card" aria-label="Result assurance ladder">
      <header>
        <div>
          <span className="section-kicker">Evidence, not a global verdict</span>
          <h2>Result assurance ladder</h2>
        </div>
        <p>
          Numerical success, physical checks, Study acceptance, and independent
          validation are distinct claims.
        </p>
      </header>
      <div className="result-assurance-layers">
        {layers.map((layer, index) => (
          <div className={layer.status} key={layer.id}>
            <span>{index + 1}</span>
            <strong>{layer.title}</strong>
            <em>{statusLabel[layer.status]}</em>
            <small>{layer.detail}</small>
          </div>
        ))}
      </div>
    </section>
  )
}
