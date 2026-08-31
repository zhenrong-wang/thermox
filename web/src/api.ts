import type {
  ArtifactRevision,
  BalanceReport,
  BalanceUncertaintyModel,
  ArtifactRevisionContent,
  PerformanceMapQualityReview,
  PerformanceMapQualityReviewList,
  EngineeringReviewDisposition,
  ArtifactRevisionList,
  AssemblyTemplateCatalogEntry,
  ProjectComponentCatalog,
  Catalog,
  CaseDocument,
  CaseEditOperation,
  CaseRevision,
  CaseRevisionList,
  CalibrationRevision,
  CalibrationRevisionList,
  CorrelationArtifactDefinition,
  CreateCalibrationRevision,
  CreateReconciliationRevision,
  CreateRunConfiguration,
  CreateStudyRevision,
  GraphEditOperation,
  ModelRevision,
  ModelRevisionList,
  PerformanceMapArtifactDefinition,
  ProjectModelValidation,
  ProjectList,
  ReconciliationResult,
  ReconciliationRevision,
  ReconciliationRevisionList,
  RunConfigurationRevision,
  RunConfigurationRevisionList,
  SimulationJob,
  SimulationJobComparison,
  JobValidationReport,
  SimulationJobPage,
  SimulationResult,
  SimulationJobState,
  StudyRevision,
  StudyRevisionList,
  ExpressionComponentDefinition,
  TopologyDocument,
  TopologyPresentation,
  TopologyPresentationRecord,
  ValidationCampaignArtifact,
  ValidationSeriesArtifact,
} from './types'
import type {
  StudyPackageDocument,
  StudyPackageImportResult,
} from './studyPackage'
import type { TopologyDraftDefinition } from './topologyDraft'

class ApiError extends Error {
  readonly status: number

  constructor(status: number, message: string) {
    super(message)
    this.name = 'ApiError'
    this.status = status
  }
}

export interface ReportDownload {
  content: string
  filename: string
  mediaType: string
}

async function getJson<T>(path: string, signal?: AbortSignal): Promise<T> {
  const response = await fetch(path, {
    headers: { Accept: 'application/json' },
    signal,
  })
  if (!response.ok) {
    const body = await response.text()
    throw new ApiError(
      response.status,
      body || `${response.status} ${response.statusText}`,
    )
  }
  return (await response.json()) as T
}

async function getOptionalJson<T>(
  path: string,
  signal?: AbortSignal,
): Promise<T | undefined> {
  const response = await fetch(path, {
    headers: { Accept: 'application/json' },
    signal,
  })
  if (response.status === 404) return undefined
  if (!response.ok) {
    const body = await response.text()
    throw new ApiError(
      response.status,
      body || `${response.status} ${response.statusText}`,
    )
  }
  return (await response.json()) as T
}

async function postJson<T>(
  path: string,
  body: unknown,
  signal?: AbortSignal,
): Promise<T> {
  const response = await fetch(path, {
    method: 'POST',
    headers: {
      Accept: 'application/json',
      'Content-Type': 'application/json',
    },
    body: JSON.stringify(body),
    signal,
  })
  if (!response.ok) {
    const responseBody = await response.text()
    throw new ApiError(
      response.status,
      responseBody || `${response.status} ${response.statusText}`,
    )
  }
  return (await response.json()) as T
}

async function putJson<T>(
  path: string,
  body: unknown,
  signal?: AbortSignal,
): Promise<T> {
  const response = await fetch(path, {
    method: 'PUT',
    headers: {
      Accept: 'application/json',
      'Content-Type': 'application/json',
    },
    body: JSON.stringify(body),
    signal,
  })
  if (!response.ok) {
    const responseBody = await response.text()
    throw new ApiError(
      response.status,
      responseBody || `${response.status} ${response.statusText}`,
    )
  }
  return (await response.json()) as T
}

async function postDownload(
  path: string,
  body: unknown,
  fallbackFilename: string,
  signal?: AbortSignal,
): Promise<ReportDownload> {
  const response = await fetch(path, {
    method: 'POST',
    headers: {
      Accept: 'text/markdown, text/csv',
      'Content-Type': 'application/json',
    },
    body: JSON.stringify(body),
    signal,
  })
  if (!response.ok) {
    const responseBody = await response.text()
    throw new ApiError(
      response.status,
      responseBody || `${response.status} ${response.statusText}`,
    )
  }
  const disposition = response.headers.get('Content-Disposition') ?? ''
  const filenameMatch = disposition.match(/filename="([^"]+)"/i)
  return {
    content: await response.text(),
    filename: filenameMatch?.[1] ?? fallbackFilename,
    mediaType: response.headers.get('Content-Type') ?? 'text/plain',
  }
}

async function postValidation(
  path: string,
  body: unknown,
  signal?: AbortSignal,
): Promise<ProjectModelValidation> {
  const response = await fetch(path, {
    method: 'POST',
    headers: {
      Accept: 'application/json',
      'Content-Type': 'application/json',
    },
    body: JSON.stringify(body),
    signal,
  })
  const document = (await response.json()) as
    | ProjectModelValidation
    | { schema_version?: string; message?: string }
  if (document.schema_version === 'thermox.project_model_validation/v1') {
    return document as ProjectModelValidation
  }
  throw new ApiError(
    response.status,
    ('message' in document && document.message) ||
      `${response.status} ${response.statusText}`,
  )
}

async function postEmptyJson<T>(
  path: string,
  headers: Record<string, string>,
  signal?: AbortSignal,
): Promise<T> {
  const response = await fetch(path, {
    method: 'POST',
    headers: { Accept: 'application/json', ...headers },
    signal,
  })
  if (!response.ok) {
    const body = await response.text()
    throw new ApiError(
      response.status,
      body || `${response.status} ${response.statusText}`,
    )
  }
  return (await response.json()) as T
}

async function deleteJson<T>(
  path: string,
  headers: Record<string, string>,
  signal?: AbortSignal,
): Promise<T> {
  const response = await fetch(path, {
    method: 'DELETE',
    headers: { Accept: 'application/json', ...headers },
    signal,
  })
  if (!response.ok) {
    const body = await response.text()
    throw new ApiError(
      response.status,
      body || `${response.status} ${response.statusText}`,
    )
  }
  return (await response.json()) as T
}

export const api = {
  catalog: (signal?: AbortSignal) =>
    getJson<Catalog>('/api/v1/catalog', signal),
  projects: (signal?: AbortSignal) =>
    getJson<ProjectList>('/api/v1/projects', signal),
  modelRevisions: (projectId: string, signal?: AbortSignal) =>
    getJson<ModelRevisionList>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/model-revisions`,
      signal,
    ),
  modelRevision: (
    projectId: string,
    revisionId: string,
    signal?: AbortSignal,
  ) =>
    getJson<ModelRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/model-revisions/${encodeURIComponent(revisionId)}`,
      signal,
    ),
  topologyPresentation: (
    projectId: string,
    signal?: AbortSignal,
  ) =>
    getOptionalJson<TopologyPresentationRecord>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/topology-presentation`,
      signal,
    ),
  putTopologyPresentation: (
    projectId: string,
    modelRevisionId: string,
    presentation: TopologyPresentation,
    signal?: AbortSignal,
  ) =>
    putJson<TopologyPresentationRecord>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/topology-presentation`,
      {
        schema_version: 'thermox.topology_presentation.put/v1',
        model_revision_id: modelRevisionId,
        presentation,
      },
      signal,
    ),
  createModelRevision: (
    projectId: string,
    document: TopologyDocument,
    parentRevisionId = '',
    signal?: AbortSignal,
  ) => {
    const query = new URLSearchParams()
    if (parentRevisionId) query.set('parent_revision_id', parentRevisionId)
    const suffix = query.size ? `?${query.toString()}` : ''
    return postJson<ModelRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/model-revisions${suffix}`,
      document,
      signal,
    )
  },
  artifactRevisions: (projectId: string, signal?: AbortSignal) =>
    getJson<ArtifactRevisionList>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/artifact-revisions`,
      signal,
    ),
  artifactRevision: <T = unknown>(
    projectId: string,
    artifactRevisionId: string,
    signal?: AbortSignal,
  ) =>
    getJson<ArtifactRevisionContent<T>>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/artifact-revisions/${encodeURIComponent(artifactRevisionId)}`,
      signal,
    ),
  performanceMapQualityReviews: (
    projectId: string,
    artifactRevisionId: string,
    signal?: AbortSignal,
  ) =>
    getJson<PerformanceMapQualityReviewList>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/artifact-revisions/${encodeURIComponent(artifactRevisionId)}/quality-reviews`,
      signal,
    ),
  createPerformanceMapQualityReview: (
    projectId: string,
    artifactRevisionId: string,
    disposition: EngineeringReviewDisposition,
    reviewedScope: string,
    rationale: string,
    supersedesReviewId = '',
    signal?: AbortSignal,
  ) => postJson<PerformanceMapQualityReview>(
    `/api/v1/projects/${encodeURIComponent(projectId)}/artifact-revisions/${encodeURIComponent(artifactRevisionId)}/quality-reviews`,
    {
      schema_version: 'thermox.performance_map_quality_review.create/v1',
      supersedes_review_id: supersedesReviewId,
      disposition,
      reviewed_scope: reviewedScope,
      rationale,
    },
    signal,
  ),
  projectComponentCatalog: (
    projectId: string,
    signal?: AbortSignal,
  ) =>
    getJson<ProjectComponentCatalog>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/component-catalog`,
      signal,
    ),
  createExpressionComponentRevision: (
    projectId: string,
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: ExpressionComponentDefinition,
    signal?: AbortSignal,
  ) => {
    const query = new URLSearchParams({
      artifact_id: artifactId,
      artifact_type: 'thermox.expression_component',
      artifact_schema_version: 'thermox.expression_component/v5',
    })
    if (parentArtifactRevisionId) {
      query.set('parent_revision_id', parentArtifactRevisionId)
    }
    return postJson<ArtifactRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/artifact-revisions?${query.toString()}`,
      definition,
      signal,
    )
  },
  createAssemblyTemplateRevision: (
    projectId: string,
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: AssemblyTemplateCatalogEntry['definition'],
    signal?: AbortSignal,
  ) => {
    const query = new URLSearchParams({
      artifact_id: artifactId,
      artifact_type: 'thermox.assembly_template',
      artifact_schema_version: 'thermox.topology/v1',
    })
    if (parentArtifactRevisionId) {
      query.set('parent_revision_id', parentArtifactRevisionId)
    }
    return postJson<ArtifactRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/artifact-revisions?${query.toString()}`,
      definition,
      signal,
    )
  },
  createTopologyDraftRevision: (
    projectId: string,
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: TopologyDraftDefinition,
    signal?: AbortSignal,
  ) => {
    const query = new URLSearchParams({
      artifact_id: artifactId,
      artifact_type: 'thermox.topology_draft',
      artifact_schema_version: 'thermox.topology_draft/v1',
    })
    if (parentArtifactRevisionId) {
      query.set('parent_revision_id', parentArtifactRevisionId)
    }
    return postJson<ArtifactRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/artifact-revisions?${query.toString()}`,
      definition,
      signal,
    )
  },
  createCorrelationRevision: (
    projectId: string,
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: CorrelationArtifactDefinition,
    signal?: AbortSignal,
  ) => {
    const query = new URLSearchParams({
      artifact_id: artifactId,
      artifact_type: 'thermox.correlation',
      artifact_schema_version: 'thermox.correlation/v2',
    })
    if (parentArtifactRevisionId) {
      query.set('parent_revision_id', parentArtifactRevisionId)
    }
    return postJson<ArtifactRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/artifact-revisions?${query.toString()}`,
      definition,
      signal,
    )
  },
  createBalanceUncertaintyRevision: (
    projectId: string,
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: BalanceUncertaintyModel,
    signal?: AbortSignal,
  ) => {
    const query = new URLSearchParams({
      artifact_id: artifactId,
      artifact_type: 'thermox.balance_uncertainty',
      artifact_schema_version: 'thermox.balance_uncertainty/v1',
    })
    if (parentArtifactRevisionId) {
      query.set('parent_revision_id', parentArtifactRevisionId)
    }
    return postJson<ArtifactRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/artifact-revisions?${query.toString()}`,
      definition,
      signal,
    )
  },
  createPerformanceMapRevision: (
    projectId: string,
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: PerformanceMapArtifactDefinition,
    signal?: AbortSignal,
  ) => {
    const query = new URLSearchParams({
      artifact_id: artifactId,
      artifact_type: 'thermox.performance_map',
      artifact_schema_version: 'thermox.performance_map/v1',
    })
    if (parentArtifactRevisionId) {
      query.set('parent_revision_id', parentArtifactRevisionId)
    }
    return postJson<ArtifactRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/artifact-revisions?${query.toString()}`,
      definition,
      signal,
    )
  },
  createValidationSeriesRevision: (
    projectId: string,
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: ValidationSeriesArtifact,
    signal?: AbortSignal,
  ) => {
    const query = new URLSearchParams({
      artifact_id: artifactId,
      artifact_type: 'thermox.validation_series',
      artifact_schema_version: 'thermox.validation_series/v1',
    })
    if (parentArtifactRevisionId) {
      query.set('parent_revision_id', parentArtifactRevisionId)
    }
    return postJson<ArtifactRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/artifact-revisions?${query.toString()}`,
      definition,
      signal,
    )
  },
  createValidationCampaignRevision: (
    projectId: string,
    artifactId: string,
    parentArtifactRevisionId: string,
    definition: ValidationCampaignArtifact,
    signal?: AbortSignal,
  ) => {
    const query = new URLSearchParams({
      artifact_id: artifactId,
      artifact_type: 'thermox.validation_campaign',
      artifact_schema_version: 'thermox.validation_campaign/v1',
    })
    if (parentArtifactRevisionId) {
      query.set('parent_revision_id', parentArtifactRevisionId)
    }
    return postJson<ArtifactRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/artifact-revisions?${query.toString()}`,
      definition,
      signal,
    )
  },
  caseRevisions: (
    projectId: string,
    modelRevisionId: string,
    signal?: AbortSignal,
  ) =>
    getJson<CaseRevisionList>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/model-revisions/${encodeURIComponent(modelRevisionId)}/case-revisions`,
      signal,
    ),
  caseRevision: (
    projectId: string,
    modelRevisionId: string,
    caseRevisionId: string,
    signal?: AbortSignal,
  ) =>
    getJson<CaseRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/model-revisions/${encodeURIComponent(modelRevisionId)}/case-revisions/${encodeURIComponent(caseRevisionId)}`,
      signal,
    ),
  createCaseRevision: (
    projectId: string,
    modelRevisionId: string,
    document: CaseDocument,
    signal?: AbortSignal,
  ) =>
    postJson<CaseRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/model-revisions/${encodeURIComponent(modelRevisionId)}/case-revisions`,
      document,
      signal,
    ),
  applyCaseEdits: (
    projectId: string,
    modelRevisionId: string,
    caseRevisionId: string,
    operations: CaseEditOperation[],
    signal?: AbortSignal,
  ) =>
    postJson<CaseRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/model-revisions/${encodeURIComponent(modelRevisionId)}/case-revisions/${encodeURIComponent(caseRevisionId)}/edits`,
      {
        schema_version: 'thermox.case_edit_batch/v1',
        operations,
      },
      signal,
    ),
  validateCaseRevision: (
    projectId: string,
    modelRevisionId: string,
    caseRevisionId: string,
    artifactRevisionIds: string[],
    signal?: AbortSignal,
  ) =>
    postValidation(
      `/api/v1/projects/${encodeURIComponent(projectId)}/model-revisions/${encodeURIComponent(modelRevisionId)}/case-revisions/${encodeURIComponent(caseRevisionId)}/validate`,
      {
        schema_version: 'thermox.project_model_validation_request/v1',
        artifact_revision_ids: artifactRevisionIds,
      },
      signal,
    ),
  studyRevisions: (projectId: string, signal?: AbortSignal) =>
    getJson<StudyRevisionList>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/study-revisions`,
      signal,
    ),
  studyRevision: (
    projectId: string,
    revisionId: string,
    signal?: AbortSignal,
  ) =>
    getJson<StudyRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/study-revisions/${encodeURIComponent(revisionId)}`,
      signal,
    ),
  createStudyRevision: (
    projectId: string,
    request: CreateStudyRevision,
    signal?: AbortSignal,
  ) =>
    postJson<StudyRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/study-revisions`,
      request,
      signal,
    ),
  importStudyPackage: (
    projectId: string,
    document: StudyPackageDocument,
    parentModelRevisionId = '',
    signal?: AbortSignal,
  ) => {
    const query = new URLSearchParams()
    if (parentModelRevisionId) {
      query.set('parent_model_revision_id', parentModelRevisionId)
    }
    const suffix = query.size ? `?${query.toString()}` : ''
    return postJson<StudyPackageImportResult>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/study-packages${suffix}`,
      document,
      signal,
    )
  },
  calibrationRevisions: (projectId: string, signal?: AbortSignal) =>
    getJson<CalibrationRevisionList>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/calibration-revisions`,
      signal,
    ),
  calibrationRevision: (
    projectId: string,
    revisionId: string,
    signal?: AbortSignal,
  ) =>
    getJson<CalibrationRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/calibration-revisions/${encodeURIComponent(revisionId)}`,
      signal,
    ),
  createCalibrationRevision: (
    projectId: string,
    request: CreateCalibrationRevision,
    signal?: AbortSignal,
  ) =>
    postJson<CalibrationRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/calibration-revisions`,
      request,
      signal,
    ),
  reconciliationRevisions: (projectId: string, signal?: AbortSignal) =>
    getJson<ReconciliationRevisionList>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/reconciliation-revisions`,
      signal,
    ),
  reconciliationRevision: (
    projectId: string,
    revisionId: string,
    signal?: AbortSignal,
  ) =>
    getJson<ReconciliationRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/reconciliation-revisions/${encodeURIComponent(revisionId)}`,
      signal,
    ),
  createReconciliationRevision: (
    projectId: string,
    request: CreateReconciliationRevision,
    signal?: AbortSignal,
  ) =>
    postJson<ReconciliationRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/reconciliation-revisions`,
      request,
      signal,
    ),
  runConfigurationRevisions: (
    projectId: string,
    signal?: AbortSignal,
  ) =>
    getJson<RunConfigurationRevisionList>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/run-configuration-revisions`,
      signal,
    ),
  runConfigurationRevision: (
    projectId: string,
    revisionId: string,
    signal?: AbortSignal,
  ) =>
    getJson<RunConfigurationRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/run-configuration-revisions/${encodeURIComponent(revisionId)}`,
      signal,
    ),
  createRunConfigurationRevision: (
    projectId: string,
    request: CreateRunConfiguration,
    signal?: AbortSignal,
  ) =>
    postJson<RunConfigurationRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/run-configuration-revisions`,
      request,
      signal,
    ),
  simulationJobs: (
    projectId: string,
    runConfigurationRevisionId: string,
    state?: SimulationJobState,
    cursor?: string,
    signal?: AbortSignal,
  ) => {
    const query = new URLSearchParams({
      project_id: projectId,
      run_configuration_revision_id: runConfigurationRevisionId,
      limit: '50',
    })
    if (state) query.set('state', state)
    if (cursor) query.set('cursor', cursor)
    return getJson<SimulationJobPage>(
      `/api/v1/jobs?${query.toString()}`,
      signal,
    )
  },
  projectJobs: (projectId: string, signal?: AbortSignal) => {
    const query = new URLSearchParams({ project_id: projectId, limit: '100' })
    return getJson<SimulationJobPage>(
      `/api/v1/jobs?${query.toString()}`,
      signal,
    )
  },
  calibrationJobs: (
    projectId: string,
    calibrationRevisionId: string,
    state?: SimulationJobState,
    signal?: AbortSignal,
  ) => {
    const query = new URLSearchParams({
      project_id: projectId,
      calibration_revision_id: calibrationRevisionId,
      limit: '50',
    })
    if (state) query.set('state', state)
    return getJson<SimulationJobPage>(
      `/api/v1/jobs?${query.toString()}`,
      signal,
    )
  },
  reconciliationJobs: (
    projectId: string,
    reconciliationRevisionId: string,
    state?: SimulationJobState,
    signal?: AbortSignal,
  ) => {
    const query = new URLSearchParams({
      project_id: projectId,
      reconciliation_revision_id: reconciliationRevisionId,
      limit: '50',
    })
    if (state) query.set('state', state)
    return getJson<SimulationJobPage>(
      `/api/v1/jobs?${query.toString()}`,
      signal,
    )
  },
  simulationJob: (jobId: string, signal?: AbortSignal) =>
    getJson<SimulationJob>(
      `/api/v1/jobs/${encodeURIComponent(jobId)}`,
      signal,
    ),
  simulationResult: (jobId: string, signal?: AbortSignal) =>
    getJson<SimulationResult>(
      `/api/v1/jobs/${encodeURIComponent(jobId)}/result`,
      signal,
    ),
  balanceReport: (
    jobId: string,
    signal?: AbortSignal,
    uncertaintyModel?: BalanceUncertaintyModel,
  ) =>
    postJson<BalanceReport>(
      `/api/v1/jobs/${encodeURIComponent(jobId)}/balance-report`,
      {
        schema_version: 'thermox.balance_report_request/v2',
        accounting_basis: 'energy',
        system_boundary: 'whole_system',
        diagram_profile: 'iso-14084-1:2015',
        calculation_profile: 'none',
        uncertainty_model: uncertaintyModel ?? null,
      },
      signal,
    ),
  balanceReportExport: (
    jobId: string,
    format: 'markdown' | 'csv',
    signal?: AbortSignal,
    uncertaintyModel?: BalanceUncertaintyModel,
  ) =>
    postDownload(
      `/api/v1/jobs/${encodeURIComponent(jobId)}/balance-report-export?format=${format}`,
      {
        schema_version: 'thermox.balance_report_request/v2',
        accounting_basis: 'energy',
        system_boundary: 'whole_system',
        diagram_profile: 'iso-14084-1:2015',
        calculation_profile: 'none',
        uncertainty_model: uncertaintyModel ?? null,
      },
      `thermox-balance-report.${format === 'markdown' ? 'md' : 'csv'}`,
      signal,
    ),
  reconciliationResult: (jobId: string, signal?: AbortSignal) =>
    getJson<ReconciliationResult>(
      `/api/v1/jobs/${encodeURIComponent(jobId)}/result`,
      signal,
    ),
  compareSimulationJobs: (
    baselineJobId: string,
    candidateJobId: string,
    signal?: AbortSignal,
  ) =>
    postJson<SimulationJobComparison>(
      '/api/v1/job-comparisons',
      {
        schema_version: 'thermox.job_comparison.create/v1',
        baseline_job_id: baselineJobId,
        candidate_job_id: candidateJobId,
      },
      signal,
    ),
  validationReport: (
    projectId: string,
    campaignArtifactRevisionId: string,
    jobIds: string[],
    signal?: AbortSignal,
  ) =>
    postJson<JobValidationReport>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/validation-reports`,
      {
        schema_version: 'thermox.job_validation_report.create/v2',
        campaign_artifact_revision_id: campaignArtifactRevisionId,
        job_ids: jobIds,
      },
      signal,
    ),
  validationReportExport: (
    projectId: string,
    campaignArtifactRevisionId: string,
    jobIds: string[],
    format: 'markdown' | 'csv',
    signal?: AbortSignal,
  ) =>
    postDownload(
      `/api/v1/projects/${encodeURIComponent(projectId)}/validation-report-exports?format=${format}`,
      {
        schema_version: 'thermox.job_validation_report.create/v2',
        campaign_artifact_revision_id: campaignArtifactRevisionId,
        job_ids: jobIds,
      },
      `thermox-validation-report.${format === 'markdown' ? 'md' : 'csv'}`,
      signal,
    ),
  submitSimulation: (
    projectId: string,
    runConfigurationRevisionId: string,
    idempotencyKey: string,
    signal?: AbortSignal,
  ) => {
    const query = new URLSearchParams({
      project_id: projectId,
      run_configuration_revision_id: runConfigurationRevisionId,
    })
    return postEmptyJson<SimulationJob>(
      `/api/v1/jobs?${query.toString()}`,
      { 'Idempotency-Key': idempotencyKey },
      signal,
    )
  },
  submitCalibration: (
    projectId: string,
    calibrationRevisionId: string,
    idempotencyKey: string,
    signal?: AbortSignal,
  ) => {
    const query = new URLSearchParams({
      project_id: projectId,
      calibration_revision_id: calibrationRevisionId,
    })
    return postEmptyJson<SimulationJob>(
      `/api/v1/jobs?${query.toString()}`,
      { 'Idempotency-Key': idempotencyKey },
      signal,
    )
  },
  submitReconciliation: (
    projectId: string,
    reconciliationRevisionId: string,
    idempotencyKey: string,
    signal?: AbortSignal,
  ) => {
    const query = new URLSearchParams({
      project_id: projectId,
      reconciliation_revision_id: reconciliationRevisionId,
    })
    return postEmptyJson<SimulationJob>(
      `/api/v1/jobs?${query.toString()}`,
      { 'Idempotency-Key': idempotencyKey },
      signal,
    )
  },
  cancelSimulation: (
    jobId: string,
    revision: number,
    signal?: AbortSignal,
  ) =>
    deleteJson<SimulationJob>(
      `/api/v1/jobs/${encodeURIComponent(jobId)}`,
      { 'If-Match': `"revision-${revision}"` },
      signal,
    ),
  applyGraphEdits: (
    projectId: string,
    revisionId: string,
    operations: GraphEditOperation[],
    signal?: AbortSignal,
  ) =>
    postJson<ModelRevision>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/model-revisions/${encodeURIComponent(revisionId)}/edits`,
      {
        schema_version: 'thermox.graph_edit_batch/v1',
        operations,
      },
      signal,
    ),
}

export function errorMessage(error: unknown): string {
  if (error instanceof Error) {
    return error.message
  }
  return 'An unexpected error occurred'
}

export function isAbortError(error: unknown): boolean {
  return (
    error instanceof DOMException &&
    error.name === 'AbortError'
  )
}
