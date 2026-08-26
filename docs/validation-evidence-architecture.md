# Validation Evidence Architecture

## Purpose

Thermox separates benchmark agreement from the strength of the evidence behind that agreement.
A small numerical residual proves that the declared equations were solved; it does not prove that
the physical model predicts an independent operating point. Likewise, reproducing a value used to
calibrate a component is useful verification but is not independent validation.

`thermox.validation_evidence/v1` is the transport-neutral service contract for recording both the
numeric verdict and that evidence context. It operates on named, dimensioned SI observations and
does not know about a particular cycle, component kind, or benchmark source. Graph-native steady
or transient result projections can be converted directly into observations. Solver diagnostics
such as a normalized residual can be added as separate observations rather than being conflated
with physical outputs.

Transient `change` projections make finite-window response validation generic: the same contract
can compare pressure, temperature, speed, power, or another registered quantity between a declared
perturbation time and any later time. The resulting dimensioned changes flow through this evidence
contract without benchmark-specific post-processing.

## Evidence layers

Every criterion identifies the layer being checked:

- `numerical`: equation closure or integration accuracy;
- `property`: property-package agreement;
- `component`: component behavior, balance, or characteristic agreement;
- `system`: cycle or plant behavior and boundary balances.

The layer locates the claim. It does not change the comparison tolerance.

## Evidence basis

Every criterion also carries exactly one evidence basis:

- `independent_reference`: the compared output was not used directly or indirectly to determine
  the model inputs or fitted parameters for that run;
- `boundary_constrained`: agreement is evaluated at a point substantially fixed by published or
  measured boundaries, so it does not establish off-design prediction;
- `calibrated_reproduction`: the target or the same dataset determined one or more fitted model
  parameters;
- `derived_reference`: the reference is independently calculated from the same published or input
  data and verifies arithmetic, composition, or integration consistency;
- `internal_consistency`: the check has no external reference, such as numerical closure or a
  conservation identity;
- `assumption_dependent`: the comparison materially depends on an interpretation required because
  source information is missing.

Classification never changes pass/fail. A result passes when

```text
abs(actual - reference)
  <= absolute_tolerance + relative_tolerance * abs(reference)
```

The result retains actual and reference values, signed and absolute errors, optional relative
error, allowed absolute error, source citation, explanatory note, and verdict. Zero-reference
criteria intentionally omit relative error.

## Summary and limits

The summary reports overall counts and separate pass/fail counts for every evidence basis. It does
not calculate a global “predictive” badge: independence belongs to a particular quantity,
operating range, model version, and source. Consumers must present the basis with each claim and
must not promote boundary-constrained or calibrated agreement into predictive validation.

Explicit limitations are first-class summary data. Missing OEM maps, unpublished geometry,
rounded source tables, calibrated efficiencies, or assumed leakage states therefore remain visible
even when every numeric comparison passes. Summary validation rejects inconsistent errors,
verdicts, classifications, counts, duplicate identities, missing observations, dimension drift,
and non-finite values before serialization or durable use.

## Relationship to Study acceptance

Validation evidence qualifies Thermox models and benchmark claims. Study-owned engineering
acceptance answers whether one user's completed calculation meets that Study's requirements. They
share graph-native projections and canonical SI conventions but remain separate contracts:

- engineering acceptance may fail while a job remains numerically successful;
- a benchmark criterion may pass while remaining calibrated or boundary-constrained;
- neither outcome grants engineering approval or predictive scope beyond its declared evidence.

The contract is suitable for future immutable generated reports and benchmark-suite manifests,
without moving benchmark source interpretation into the numerical, physics, or component layers.
