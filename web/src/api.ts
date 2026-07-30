import type {
  Catalog,
  ModelRevision,
  ModelRevisionList,
  ProjectList,
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
