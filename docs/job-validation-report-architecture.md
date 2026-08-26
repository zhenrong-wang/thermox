# Job Validation Report Architecture

`thermox.job_validation_report/v1` is a service-owned read model over persisted, revision-backed
Study jobs. It answers a deliberately narrow question: across a caller-selected set of calculations,
how much reference evidence was declared, evaluated, and matched?

The report does not execute a model, reinterpret a result artifact, or persist a new engineering
claim. It summarizes the immutable job request and compact terminal result summary already retained
by the platform. Full per-sample errors, tolerances, provenance, uncertainty, and source limitations
remain in `thermox.result/v6`.

## Selection contract

`POST /api/v1/job-validation-reports` accepts
`thermox.job_validation_report.create/v1` with one to 100 unique job IDs. Every selected job must:

- be visible to the caller's Team;
- be terminal;
- originate from a revision-backed Study; and
- belong to the same Project.

Requested order is retained. If any ID is outside the Team boundary, the whole request returns the
same not-found response as an unknown job, preserving tenant non-disclosure.

## Independent report dimensions

The response keeps four concerns separate:

- numerical execution: succeeded versus unsuccessful terminal jobs;
- evidence coverage: jobs that declared at least one exact validation artifact revision;
- evaluation outcome: matched, not matched, or unevaluated;
- alignment volume: passed/failed samples and exact/interpolated alignments.

Each row carries its Study revision, mode, terminal state, exact evidence artifact revision IDs,
sample/alignment counts, and one status:

- `not_declared`: the Study did not bind reference evidence;
- `matched`: execution succeeded, evidence was evaluated, and every sample passed;
- `not_matched`: execution succeeded and at least one evaluated sample failed;
- `not_evaluated_execution_unsuccessful`: evidence was declared, but numerical execution did not
  produce an evaluable result.

The report intentionally has no global `passed`, `credible`, or `engineering_ready` field. A matched
set only establishes agreement with the selected evidence under its exact declared policies. It
does not establish model validity outside those cases, evidence independence, uncertainty quality,
or fitness for a different engineering decision.

## Adapter boundary

`SimulationJobService::validation_report` owns authorization, selection invariants, consistency
checks, and aggregation. HTTP, future RPC adapters, the web client, and direct C++ callers consume
the same application contract. This keeps validation semantics out of transport and UI code while
the Results workspace renders an explicit caller-selected matrix. Future RPC and export layers can
render the same report without changing the solver or physics core.
