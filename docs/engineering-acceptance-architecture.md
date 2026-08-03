# Engineering Acceptance Architecture

## Purpose

Thermox distinguishes three questions that have different owners:

1. **Readiness:** is the immutable system definition complete enough to calculate?
2. **Numerical outcome:** did the selected solver converge or integrate successfully?
3. **Engineering acceptance:** do the completed physical results satisfy the Study's declared
   requirements?

A successful solver run is evidence that the equations were solved. It is not, by itself, an
engineering approval. Conversely, a result outside an engineering limit remains a successfully
calculated job whose acceptance verdict is `false`.

## Contract

Acceptance criteria belong to an immutable Study revision. Each criterion contains:

- a stable criterion ID;
- the ID of one result projection declared by the same Study;
- the projection's physical dimension;
- a finite lower bound, upper bound, or both in canonical SI units;
- explicit inclusive/exclusive semantics for each bound.

Study publication rejects duplicate criterion IDs, unknown projections, dimension mismatches,
missing bounds, non-finite values, and empty intervals. Criteria are generic: they can target any
projected system balance, KPI, component metric/internal value, or primary/derived port value. No
cycle type or equipment kind is embedded in this contract.

Canonical SI storage is deliberate. Unit selection and conversion are presentation concerns;
the persisted checksum and worker evaluation do not depend on a user's display-unit profile.

## Execution and provenance

When a revision-backed job is submitted, the service resolves the Study and copies its criteria
into the immutable job request together with its result projections. Both participate in the job
fingerprint. A worker evaluates criteria only after a successful steady projection or transient
aggregation.

The result summary records:

- the overall accepted/not-accepted verdict;
- passed and failed counts;
- each criterion's actual SI value, bounds, inclusivity, dimension, and verdict.

The durable job state remains `succeeded` when evaluation completes, even if one or more criteria
fail. Solver errors still produce a failed job and no acceptance verdict. This preserves a clean
boundary between numerical evidence and engineering judgment.

PostgreSQL stores the Study criteria and the snapshotted job request/result summary. The full
numerical result remains a checksummed object-storage artifact; acceptance is a searchable job
summary derived from that immutable result and Study intent.

## Interfaces

The HTTP API accepts and returns `acceptance_criteria` on Study revisions. The web Study editor
binds optional SI limits to declared projections, and result/history views present engineering
acceptance separately from convergence diagnostics. Declaration-based clients use the same
service contract and do not depend on the browser workflow.

## Current boundary and next extensions

This first contract supports absolute scalar intervals. It intentionally does not yet define:

- tolerance relative to a reference or measured value;
- equations involving multiple projected values;
- severity levels, waivers, or approval signatures;
- comparison across Study revisions;
- generated engineering reports.

Those features should extend the Study-owned criterion/evaluation model rather than enter the
numeric kernel or individual component implementations.
