# Validation-series artifacts

## Purpose

`thermox.validation_series/v1` is the transport-neutral contract for external transient reference
data. It belongs to the service and evidence layer, not the numerical or component core. A caller
may obtain the declaration from a database, object store, S3-compatible provider, uploaded file,
or direct API body; every provider reaches the same parser and canonical SI representation.

The artifact records an immutable source reference and lowercase SHA-256 checksum, evidence basis,
acquisition class (`measured`, `computational`, `derived`, or `digitized`), notes, and explicit
limitations. Signals declare a physical dimension and input unit. Sample time, value, and optional
standard uncertainty are normalized through the platform unit registry. Offset units are handled
correctly: an absolute Celsius value receives the offset, while its uncertainty is converted as a
temperature difference.

Signals require finite values and strictly increasing non-negative timestamps. Identities are
unique, limitations cannot be duplicated, and the contract bounds both signal and sample counts.
Milliseconds are accepted as ordinary input time units; persisted canonical declarations use SI
seconds and each dimension's canonical SI unit.

## Binding and comparison

Reference data stays independent of a particular topology. A
`TrajectoryValidationBinding` maps one artifact signal to a normal graph result selector only when
a Study or validation run is evaluated. The binding declares:

- absolute-value or projected-change comparison;
- a time offset between the reference clock and simulation clock;
- the simulation baseline for change comparisons;
- absolute and relative model-agreement tolerances;
- a multiplier on each sample's standard uncertainty;
- the largest permitted simulation interpolation gap.

The allowed absolute error for one sample is

```text
absolute_tolerance
  + uncertainty_multiplier * standard_uncertainty
  + relative_tolerance * abs(reference)
```

Uncertainty is therefore preserved as measurement evidence, not mistaken for a fitted correction.
An absent uncertainty contributes zero. The evaluator rejects missing or duplicate bindings,
dimension drift, out-of-range timestamps, changes that precede their baseline, and interpolation
spans wider than policy.

`thermox.trajectory_validation/v1` reports exact and interpolated alignment counts, the maximum
alignment span, and the complete `thermox.validation_evidence/v1` result. Every sample retains its
source, evidence classification, actual/reference values, tolerance, signed error, and verdict.
It does not issue a global engineering-ready badge.

## First engineering use

The NASA AGTF30 fuel-step reference is now a validation-series artifact with two angular-speed
signals expressed in rpm and five timestamps. A 0.001 s binding offset aligns source elapsed time
with the scheduled Thermox fuel step. Both signals use projected-change comparison from that step,
and all ten samples are exact trajectory alignments. The retained 25% cross-code band passes while
the previously reported 18--22% dynamic difference remains visible.

That artifact is computational and derived from NASA's local A/B model. Replacing it with measured
data requires no numerical-core change: publish measured signals with their checksum, units,
uncertainties, and `independent_reference` basis, then bind them to the same graph outputs.

The canonical parser, serializer, and evaluator are exposed through the service library. Validation
series can also be stored as immutable, content-addressed Project artifact revisions and selected by
a Study. A Study v5 binding names the exact artifact revision, signal, result projection, comparison
mode, clock alignment, tolerance, uncertainty multiplier, and interpolation policy. Publication
checks artifact selection, signal existence, projection dimension, uniqueness, finite policy, and
transient intent. Resolution keeps evidence beside the executable physics artifact bundle, so it
cannot accidentally enter model compilation. Job v19 snapshots the resolved evidence and bindings
into its fingerprinted request, schedules every reference and change-baseline timestamp as a
required transient output, evaluates the completed graph trajectory in the worker, and embeds the
full alignment and classified pass/fail evidence in the immutable result artifact. Workers never
need to query mutable Project state.
