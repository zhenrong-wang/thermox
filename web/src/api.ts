import type {
  ArtifactRevision,
  ArtifactRevisionList,
  ProjectComponentCatalog,
  Catalog,
  CaseDocument,
  CaseEditOperation,
  CaseRevision,
  CaseRevisionList,
  CreateRunConfiguration,
  GraphEditOperation,
  ModelRevision,
  ModelRevisionList,
  ProjectModelValidation,
  ProjectList,
  RunConfigurationRevision,
  RunConfigurationRevisionList,
  SimulationJob,
  SimulationJobPage,
  SimulationResult,
  SimulationJobState,
  ExpressionComponentDefinition,
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
  artifactRevisions: (projectId: string, signal?: AbortSignal) =>
    getJson<ArtifactRevisionList>(
      `/api/v1/projects/${encodeURIComponent(projectId)}/artifact-revisions`,
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
      artifact_schema_version: 'thermox.expression_component/v1',
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
      `/api/v1/simulations?${query.toString()}`,
      signal,
    )
  },
  simulationJob: (jobId: string, signal?: AbortSignal) =>
    getJson<SimulationJob>(
      `/api/v1/simulations/${encodeURIComponent(jobId)}`,
      signal,
    ),
  simulationResult: (jobId: string, signal?: AbortSignal) =>
    getJson<SimulationResult>(
      `/api/v1/simulations/${encodeURIComponent(jobId)}/result`,
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
      `/api/v1/simulations?${query.toString()}`,
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
      `/api/v1/simulations/${encodeURIComponent(jobId)}`,
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
