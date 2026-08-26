import type {
  CalibrationRevision,
  CaseRevision,
  StudyRevision,
  SimulationJob,
  ReconciliationRevision,
  ValidationCampaignCatalogEntry,
} from './types'

interface CaseRevisionPanelProps {
  revisions: CaseRevision[]
  selectedId: string
  publishing: boolean
  studies: StudyRevision[]
  validationCampaigns: ValidationCampaignCatalogEntry[]
  campaignStudyCount: number
  calibrations: CalibrationRevision[]
  calibrationJobs: SimulationJob[]
  reconciliations: ReconciliationRevision[]
  reconciliationJobs: SimulationJob[]
  canPublishStudy: boolean
  onSelect: (revisionId: string) => void
  onCreate: () => void
  onImportEvidence: () => void
  onPublishStudy: () => void
  onPublishValidationCampaign: () => void
  onReviseValidationCampaign: (
    campaign: ValidationCampaignCatalogEntry,
  ) => void
  onPublishCalibration: () => void
  onRunCalibration: (revision: CalibrationRevision) => void
  onPublishReconciliation: () => void
  onRunReconciliation: (revision: ReconciliationRevision) => void
  onInspectReconciliationResult: (job: SimulationJob) => void
}

export function CaseRevisionPanel({
  revisions,
  selectedId,
  publishing,
  studies,
  validationCampaigns,
  campaignStudyCount,
  calibrations,
  calibrationJobs,
  reconciliations,
  reconciliationJobs,
  canPublishStudy,
  onSelect,
  onCreate,
  onImportEvidence,
  onPublishStudy,
  onPublishValidationCampaign,
  onReviseValidationCampaign,
  onPublishCalibration,
  onRunCalibration,
  onPublishReconciliation,
  onRunReconciliation,
  onInspectReconciliationResult,
}: CaseRevisionPanelProps) {
  return (
    <div className="case-revision-panel">
      <header>
        <div>
          <span className="eyebrow">Exact topology binding</span>
          <h2>Case revisions</h2>
          <p>{revisions.length} immutable revisions</p>
        </div>
        <button
          type="button"
          className="resource-button"
          disabled={publishing}
          onClick={onCreate}
        >
          + Case
        </button>
      </header>
      <div className="case-revision-list">
        {!revisions.length && (
          <div className="case-list-empty">
            <strong>No operating cases</strong>
            <span>Create one for this exact topology revision.</span>
          </div>
        )}
        {revisions.map((revision) => (
          <button
            type="button"
            key={revision.case_revision_id}
            className={
              revision.case_revision_id === selectedId
                ? 'case-revision-card selected'
                : 'case-revision-card'
            }
            onClick={() => onSelect(revision.case_revision_id)}
          >
            <div>
              <strong>{revision.case_id}</strong>
              <span>r{revision.revision_number}</span>
            </div>
            <small>{revision.mode}</small>
            <code>{revision.checksum.slice(7, 19)}</code>
          </button>
        ))}
      </div>
      <header className="study-revision-heading">
        <div>
          <span className="eyebrow">Executable intent</span>
          <h2>Studies</h2>
          <p>{studies.length} immutable revisions</p>
        </div>
        <div>
          <button type="button" className="resource-button"
            disabled={publishing} onClick={onImportEvidence}>
            + Evidence
          </button>
          <button
            type="button"
            className="resource-button"
            disabled={publishing || !canPublishStudy}
            onClick={onPublishStudy}
            title={
              canPublishStudy
                ? 'Publish the validated revision set as a study'
                : 'Validate the exact topology, case, and artifacts first'
            }
          >
            Publish
          </button>
        </div>
      </header>
      <div className="case-revision-list">
        {!studies.length && (
          <div className="case-list-empty">
            <strong>No published studies</strong>
            <span>Validate an operating case, then publish its intent.</span>
          </div>
        )}
        {studies.map((revision) => (
          <div className="case-revision-card" key={revision.study_revision_id}>
            <div>
              <strong>{revision.study_id}</strong>
              <span>r{revision.revision_number}</span>
            </div>
            <small>
              {revision.intent} · {revision.result_projections.length} outputs ·{' '}
              {revision.trajectory_validation_bindings.length} evidence bindings
            </small>
            <code>{revision.checksum.slice(7, 19)}</code>
          </div>
        ))}
      </div>
      <header className="study-revision-heading">
        <div>
          <span className="eyebrow">Cross-Study evidence scope</span>
          <h2>Validation campaigns</h2>
          <p>{validationCampaigns.length} immutable revisions</p>
        </div>
        <button type="button" className="resource-button"
          disabled={publishing || campaignStudyCount === 0}
          onClick={onPublishValidationCampaign}>Publish</button>
      </header>
      <div className="case-revision-list">
        {!validationCampaigns.length && (
          <div className="case-list-empty">
            <strong>No validation campaigns</strong>
            <span>Pin exact Studies, an objective, and known limitations.</span>
          </div>
        )}
        {validationCampaigns.map((campaign) => {
          const latest = !validationCampaigns.some((candidate) =>
            candidate.source.artifact_id === campaign.source.artifact_id &&
            candidate.source.revision_number > campaign.source.revision_number)
          return (
            <div className="case-revision-card"
              key={campaign.source.artifact_revision_id}>
              <div>
                <strong>{campaign.definition.name}</strong>
                <span>r{campaign.source.revision_number}</span>
              </div>
              <small>
                {campaign.definition.study_revision_ids.length} Studies ·{' '}
                {campaign.definition.objective}
              </small>
              <code>{campaign.source.content.checksum.slice(7, 19)}</code>
              {latest && (
                <button type="button" className="resource-button"
                  onClick={() => onReviseValidationCampaign(campaign)}>
                  Revise
                </button>
              )}
            </div>
          )
        })}
      </div>
      <header className="study-revision-heading">
        <div>
          <span className="eyebrow">Parameter estimation</span>
          <h2>Calibrations</h2>
          <p>{calibrations.length} immutable revisions</p>
        </div>
        <button type="button" className="resource-button"
          disabled={publishing || studies.length === 0}
          onClick={onPublishCalibration}>Publish</button>
      </header>
      <div className="case-revision-list">
        {!calibrations.length && (
          <div className="case-list-empty">
            <strong>No calibration campaigns</strong>
            <span>Publish Studies, then define parameters and observations.</span>
          </div>
        )}
        {calibrations.map((revision) => (
          <div className="case-revision-card" key={revision.calibration_revision_id}>
            <div>
              <strong>{revision.calibration_id}</strong>
              <span>r{revision.revision_number}</span>
            </div>
            <small>{revision.training_study_revision_ids.length} training · {revision.validation_study_revision_ids.length} validation · {calibrationJobs.find((job) => job.request.source_revisions?.calibration_revision_id === revision.calibration_revision_id)?.state ?? 'not run'}</small>
            <button type="button" className="resource-button"
              onClick={() => onRunCalibration(revision)}>Run</button>
            {calibrationJobs.find((job) =>
              job.request.source_revisions?.calibration_revision_id ===
                revision.calibration_revision_id &&
              job.state === 'succeeded') && (
              <a className="resource-button" target="_blank" rel="noreferrer"
                href={`/api/v1/jobs/${calibrationJobs.find((job) =>
                  job.request.source_revisions?.calibration_revision_id ===
                    revision.calibration_revision_id &&
                  job.state === 'succeeded')!.job_id}/result`}>
                Result
              </a>
            )}
          </div>
        ))}
      </div>
      <header className="study-revision-heading">
        <div>
          <span className="eyebrow">Measured-system inference</span>
          <h2>Data reconciliations</h2>
          <p>{reconciliations.length} immutable revisions</p>
        </div>
        <button type="button" className="resource-button"
          disabled={publishing || studies.length === 0}
          onClick={onPublishReconciliation}>Publish</button>
      </header>
      <div className="case-revision-list">
        {!reconciliations.length && (
          <div className="case-list-empty">
            <strong>No reconciliation definitions</strong>
            <span>Partition Studies into constraints and independent held-out evidence.</span>
          </div>
        )}
        {reconciliations.map((revision) => {
          const job = reconciliationJobs.find((candidate) =>
            candidate.request.source_revisions?.reconciliation_revision_id ===
              revision.reconciliation_revision_id)
          return (
            <div className="case-revision-card"
              key={revision.reconciliation_revision_id}>
              <div>
                <strong>{revision.reconciliation_id}</strong>
                <span>r{revision.revision_number}</span>
              </div>
              <small>{revision.mode.replaceAll('_', ' ')} · {revision.constraint_study_revision_ids.length} constrained · {revision.held_out_study_revision_ids.length} held out · {job?.state ?? 'not run'}</small>
              <button type="button" className="resource-button"
                onClick={() => onRunReconciliation(revision)}>Run</button>
              {job?.state === 'succeeded' && (
                <button type="button" className="resource-button"
                  onClick={() => onInspectReconciliationResult(job)}>
                  Evidence
                </button>
              )}
            </div>
          )
        })}
      </div>
      <footer>
        <span>Case → Study → declared calculation intent</span>
        <code>immutable provenance</code>
      </footer>
    </div>
  )
}
