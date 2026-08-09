import type {
  ArtifactRevision,
  ArtifactRevisionContent,
  PerformanceMapQualityReview,
  PerformanceMapQualityReviewList,
  PerformanceMapReviewDisposition,
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
  CreateRunConfiguration,
  CreateStudyRevision,
  GraphEditOperation,
  ModelRevision,
  ModelRevisionList,
  PerformanceMapArtifactDefinition,
  ProjectModelValidation,
  ProjectList,
  RunConfigurationRevision,
  RunConfigurationRevisionList,
  SimulationJob,
  SimulationJobComparison,
  SimulationJobPage,
  SimulationResult,
  SimulationJobState,
  StudyRevision,
  StudyRevisionList,
  ExpressionComponentDefinition,
  TopologyDocument,
} from './types'

class ApiError extends Error {
  readonly status: number

  constructor(status: number, message: string) {
    super(message)
    this.name = 'ApiError'
    this.status = status
  }
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
    disposition: PerformanceMapReviewDisposition,
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
      artifact_schema_version: 'thermox.expression_component/v2',
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
