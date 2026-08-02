import { afterEach, describe, expect, it, vi } from 'vitest'
import { api } from './api'
import type { ArtifactRevision, ExpressionComponentDefinition } from './types'

afterEach(() => {
  vi.unstubAllGlobals()
})

describe('expression component authoring API', () => {
  it('publishes a typed immutable child revision', async () => {
    const definition: ExpressionComponentDefinition = {
      schema_version: 'thermox.expression_component/v2',
      kind: 'custom.signal.gain',
      version: '1.0.1',
      template_kind: 'custom.signal.gain',
      display_name: 'Signal gain',
      category: 'Project components',
      model_name: 'Custom expression',
      system_boundary_role: '',
      ports: [
        {
          name: 'input',
          domain: 'signal',
          direction: 'in',
          maximum_connections: 1,
        },
      ],
      parameters: [],
      equations: [
        {
          name: 'gain',
          expression: 'input.value',
          residual_scale: 1,
        },
      ],
    }
    const revision = {
      schema_version: 'thermox.artifact_revision/v1',
      artifact_revision_id: 'revision-2',
      revision_number: 2,
    } as ArtifactRevision
    const fetchMock = vi.fn(
      async (_input: RequestInfo | URL, _init?: RequestInit) =>
        new Response(JSON.stringify(revision), {
          status: 201,
          headers: { 'Content-Type': 'application/json' },
        }),
    )
    vi.stubGlobal('fetch', fetchMock)

    await expect(
      api.createExpressionComponentRevision(
        'project with space',
        'gain-definition',
        'revision-1',
        definition,
      ),
    ).resolves.toEqual(revision)

    expect(fetchMock).toHaveBeenCalledOnce()
    const [path, request] = fetchMock.mock.calls[0]
    const url = new URL(
      path instanceof Request ? path.url : path,
      'http://thermox.local',
    )
    expect(url.pathname).toBe(
      '/api/v1/projects/project%20with%20space/artifact-revisions',
    )
    expect(url.searchParams.get('artifact_id')).toBe('gain-definition')
    expect(url.searchParams.get('artifact_type')).toBe(
      'thermox.expression_component',
    )
    expect(url.searchParams.get('artifact_schema_version')).toBe(
      'thermox.expression_component/v2',
    )
    expect(url.searchParams.get('parent_revision_id')).toBe('revision-1')
    expect(request).toMatchObject({
      method: 'POST',
      body: JSON.stringify(definition),
    })
  })
})
