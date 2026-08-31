# Balance Uncertainty Architecture

## Purpose and boundary

Thermox balance reports separate three different statements:

1. the numeric solver completed;
2. the calculated boundary balance satisfies declared engineering limits;
3. the calculated closure is distinguishable from zero given declared measurement uncertainty.

The third statement requires a traceable uncertainty declaration. Thermox does not infer sensor
accuracy, distribution shape, confidence level, or missing correlations from a component kind or a
measured value. Without a declaration the report returns `not_declared`, not a zero uncertainty.

The model is system-agnostic. It binds metrology data to the component/port endpoints that cross the
selected system boundary; it contains no Rankine-cycle, gas-turbine, or equipment-specific logic.

## HTTP contract

`POST /api/v1/jobs/{job_id}/balance-report` and the corresponding `balance-report-export` endpoint
accept `thermox.balance_report_request/v2`. Its optional `uncertainty_model` is either `null` or a
`thermox.balance_uncertainty/v1` object. For example:

```json
{
  "schema_version": "thermox.balance_report_request/v2",
  "profile_id": "engineering-summary",
  "uncertainty_model": {
    "schema_version": "thermox.balance_uncertainty/v1",
    "id": "site-metering-revision-4",
    "source": {
      "reference": "calibration-certificate-bundle-2026-04",
      "checksum_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      "note": "Standard uncertainties converted to canonical SI units",
      "limitations": ["Common ambient compensation is not represented"]
    },
    "streams": [
      {
        "component_id": "feed",
        "port_name": "outlet",
        "mass_flow_standard_uncertainty_si": 0.02,
        "specific_enthalpy_standard_uncertainty_si": 1200.0,
        "energy_flow_standard_uncertainty_si": null,
        "mass_enthalpy_correlation": 0.0
      },
      {
        "component_id": "product",
        "port_name": "inlet",
        "mass_flow_standard_uncertainty_si": 0.02,
        "specific_enthalpy_standard_uncertainty_si": null,
        "energy_flow_standard_uncertainty_si": 18000.0,
        "mass_enthalpy_correlation": 0.0
      }
    ],
    "correlations": [
      {
        "quantity": "mass_flow",
        "first": {"component_id": "feed", "port_name": "outlet"},
        "second": {"component_id": "product", "port_name": "inlet"},
        "coefficient": 0.5
      }
    ]
  }
}
```

Uncertainties are non-negative one-standard-deviation values in canonical SI units. An energy-flow
uncertainty can be supplied directly, or calculated from mass flow and specific enthalpy. A stream
must not declare both a direct energy-flow uncertainty and a specific-enthalpy uncertainty.
Correlation coefficients must be in `[-1, 1]`; the complete declared correlation matrix must be
positive semidefinite. The source checksum is the lowercase 64-character SHA-256 digest of the
controlled source artifact.

## Propagation

For an energy flow `E = m h`, first-order propagation is:

```text
u(E)^2 = h^2 u(m)^2 + m^2 u(h)^2 + 2 m h rho(m,h) u(m) u(h)
```

Across boundary streams, Thermox applies the report's input/output signs and the declared covariance
matrix:

```text
u(closure)^2 = s^T C s
```

The report includes the combined standard uncertainty and an expanded uncertainty using coverage
factor `k = 2`. The latter is a deterministic scaling for engineering presentation; Thermox does
not claim a coverage probability because the declaration does not encode distributions or effective
degrees of freedom.

## Completeness and transient interpretation

Coverage is `complete` only when every applicable boundary endpoint has the necessary declaration;
otherwise it is `partial` or `not_evaluated`, with limitations reported explicitly. Mass-flow
uncertainty is not applicable to boundaries without mass-domain streams.

For transient jobs, the current balance report assesses the instantaneous net boundary rate at the
reported state. It does not claim a transient conservation closure because storage accumulation and
its uncertainty are not yet integrated over a time window. That extension belongs in the generic
balance-report service, not in individual component models.

## Product integration

JSON, Markdown, and CSV reports expose the same source identity, coverage, propagated results, and
limitations. The web result view renders these values. The first contract is intentionally
declaration/API-first; persisted uncertainty artifacts, revision binding, and a browser authoring
workflow are the next product-layer extension.
