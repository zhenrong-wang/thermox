# Validation Campaign and Report Architecture

Thermox separates immutable validation intent from a report generated over concrete executions.

- `thermox.validation_campaign/v1` is a typed, content-addressed Project artifact. It declares an
  identity, name, engineering objective, one to 100 exact Study revision IDs, and known campaign
  limitations.
- `thermox.job_validation_report/v2` is a service-owned read model over one selected terminal job
  for every Study in that exact campaign revision.

Neither object executes a model or persists a new physics result. Full per-sample errors,
tolerances, provenance, uncertainty, and source limitations remain in `thermox.result/v6`.

## Publication contract

Campaigns use the ordinary Project artifact-revision API with artifact type
`thermox.validation_campaign` and schema `thermox.validation_campaign/v1`. Publication rejects
empty or duplicate Study IDs and any Study revision outside the Team-scoped Project. Canonical
payload bytes are stored through the existing object-store abstraction; revision metadata and
checksums remain in the durable Project repository.

This makes campaign definitions usable by HTTP, future RPC adapters, CLI clients, and the web UI
without adding transport-specific or filesystem persistence.

## Report selection contract

`POST /api/v1/projects/{project_id}/validation-reports` accepts
`thermox.job_validation_report.create/v2` with an exact campaign artifact revision ID and job IDs.
Every selected job must:

- be visible to the caller's Team;
- be terminal;
- be a steady or transient revision-backed Study job;
- belong to the campaign's exact Project; and
- provide exactly one execution for each Study pinned by the campaign, with no extras.

If the campaign or any job is outside the Team boundary, the request returns the same not-found
response as an unknown resource, preserving tenant non-disclosure.

## Independent report dimensions

The response retains the campaign revision ID and checksum, objective, limitations, and four
separate concerns:

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
campaign only establishes agreement with its selected evidence under its exact declared policies.
It does not establish model validity outside those cases, evidence independence, uncertainty
quality, or fitness for a different engineering decision.

## Application boundary

`ValidationCampaignReportService` resolves and integrity-checks the exact campaign artifact through
`ProjectService`, then delegates job authorization and aggregation to `SimulationJobService`. The
Results workspace renders that application response and never derives campaign verdicts itself.
Future RPC and export layers can consume the same contract without changing the solver, physics
core, or persistence model.
