# Study Comparison Architecture

## Purpose

Study comparison is an application-service operation over completed, immutable job evidence. It
does not rerun a solver, reinterpret component physics, or derive differences independently in a
client. The same versioned comparison contract is available to the web application and to direct
API callers.

## Eligibility and scope

A pairwise comparison requires:

- two different, successful jobs with projected result summaries;
- the same Team ownership and Project provenance;
- immutable Study revision provenance on both jobs;
- the same steady or transient summary mode.

Team-scoped repository reads preserve non-disclosure: if either job is absent or belongs to
another Team, the comparison is reported as not found. Jobs from different run configurations,
cases, model revisions, or Study revisions may be compared when they share a Project and mode.
Comparing repeat executions of the same Study is also valid for reproducibility analysis.

## Projection alignment

Values align by stable result-projection ID. Every ID in the union of both summaries appears in
the response with one status:

- `matched`: dimension, aggregation, and resolved time window agree;
- `baseline_only` or `candidate_only`: the other Study did not declare that output;
- `dimension_mismatch`: equal ID but different physical dimensions;
- `aggregation_mismatch`: equal ID but different transient aggregation semantics.
- `window_mismatch`: equal ID and reduction, but different resolved absolute/event-relative
  windows.

Only matched values receive numeric deltas. `absolute_delta_si` is candidate minus baseline in
canonical SI. `relative_delta` is that delta divided by the absolute baseline value; it is null
when the baseline is zero. Missing or incompatible values remain visible rather than being
silently discarded.

## Engineering acceptance

The comparison carries each job's overall engineering acceptance verdict and its transition, for
example `accepted_to_not_accepted`. If either Study had no acceptance evaluation, the transition
is `not_evaluated`. This evidence remains distinct from both the successful job states and the
per-projection numerical deltas.

## Reference-validation regression

Job Comparison v3 also reports the baseline and candidate trajectory-validation verdicts and
aligned-sample counts. A transition such as `matched_to_not_matched` is emitted only when both jobs
use the same immutable evidence revision and the same signal/projection, comparison mode, clock
alignment, uncertainty multiplier, interpolation limit, and absolute/relative tolerance policy.
Otherwise the response reports `evidence_policy_mismatch` and suppresses a potentially misleading
transition. A missing evaluation on either side is explicit. Reference agreement remains separate
from numerical success and Study engineering acceptance.

## Interface and persistence boundary

`POST /api/v1/job-comparisons` accepts baseline and candidate job IDs and returns
`thermox.job_comparison/v3`. The Analyze workspace presents the same service response with display
unit conversion. The comparison is currently a deterministic read model and is not persisted.

Generated reports, approvals, comments, or signed snapshots are durable business artifacts and
should later use the existing object-storage and PostgreSQL metadata ports. They should embed or
reference this service-owned comparison contract rather than introduce a second calculation path.
