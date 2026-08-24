import { afterEach, describe, expect, it, vi } from 'vitest'
import { api } from './api'
import type {
  ArtifactRevision,
  CorrelationArtifactDefinition,
  ExpressionComponentDefinition,
  PerformanceMapArtifactDefinition,
  TopologyDocument,
} from './types'

afterEach(() => {
  vi.unstubAllGlobals()
})

describe('assembly template authoring API', () => {
  it('publishes a versioned topology artifact', async () => {
    const definition: TopologyDocument = {
      schema_version: 'thermox.topology/v1',
      model: {
        id: 'train_template',
        name: 'Train template',
        revision: '1',
        media: [],
        components: [],
        assemblies: [{
          id: 'train',
          components: [{ id: 'stage', kind: 'compressor.test' }],
          connections: [],
          ports: [],
        }],
        connections: [],
      },
    }
    const revision = {
      artifact_revision_id: 'template-r2',
      revision_number: 2,
    } as ArtifactRevision
    const fetchMock = vi.fn(
      async (_input: RequestInfo | URL, _init?: RequestInit) =>
        new Response(JSON.stringify(revision), { status: 201 }),
    )
    vi.stubGlobal('fetch', fetchMock)

    await api.createAssemblyTemplateRevision(
      'project-a',
      'compressor-train',
      'template-r1',
      definition,
    )

    const [path, request] = fetchMock.mock.calls[0]
    const url = new URL(String(path), 'http://thermox.local')
    expect(url.searchParams.get('artifact_type')).toBe(
      'thermox.assembly_template',
    )
    expect(url.searchParams.get('artifact_schema_version')).toBe(
      'thermox.topology/v1',
    )
    expect(url.searchParams.get('parent_revision_id')).toBe('template-r1')
    expect(request).toMatchObject({
      method: 'POST',
      body: JSON.stringify(definition),
    })
  })
})

describe('reconciliation revision API', () => {
  it('publishes intent and submits only its immutable revision identity', async () => {
    const fetchMock = vi.fn(async (
      _input: RequestInfo | URL,
      _init?: RequestInit,
    ) =>
      new Response(JSON.stringify({
        schema_version: 'thermox.reconciliation_revision/v1',
        reconciliation_revision_id: 'reconciliation-r1',
      }), { status: 201, headers: { 'Content-Type': 'application/json' } }),
    )
    vi.stubGlobal('fetch', fetchMock)
    const definition = {
      schema_version: 'thermox.calibration/v1' as const,
      calibration: { id: 'balance', parameters: [], observations: [] },
    }

    await api.createReconciliationRevision('project/a', {
      schema_version: 'thermox.reconciliation_revision.create/v1',
      reconciliation_id: 'balance',
      parent_reconciliation_revision_id: '',
      model_revision_id: 'model-r1',
      constraint_study_revision_ids: ['study-r1'],
      held_out_study_revision_ids: ['study-r2'],
      definition,
      mode: 'weighted_measurements',
    })
    await api.submitReconciliation(
      'project/a', 'reconciliation-r1', 'idempotency-1',
    )

    const [publishPath, publishRequest] = fetchMock.mock.calls[0]
    expect(publishPath).toBe(
      '/api/v1/projects/project%2Fa/reconciliation-revisions',
    )
    expect(publishRequest).toMatchObject({
      method: 'POST',
      body: expect.stringContaining('held_out_study_revision_ids'),
    })
    const [submitPath, submitRequest] = fetchMock.mock.calls[1]
    const submitUrl = new URL(String(submitPath), 'http://thermox.local')
    expect(submitUrl.searchParams.get('reconciliation_revision_id')).toBe(
      'reconciliation-r1',
    )
    expect(submitRequest).toMatchObject({ method: 'POST' })
    expect(submitRequest).not.toHaveProperty('body')
    expect(submitRequest?.headers).toMatchObject({
      'Idempotency-Key': 'idempotency-1',
    })
  })
})

describe('correlation artifact authoring API', () => {
  it('retrieves an exact immutable artifact payload for revision editing', async () => {
    const content = {
      schema_version: 'thermox.artifact_revision_content/v1',
      revision: {
        schema_version: 'thermox.artifact_revision/v1',
        artifact_revision_id: 'correlation-revision-1',
        artifact_type: 'thermox.correlation',
      },
      artifact: {
        schema_version: 'thermox.correlation/v2',
        inputs: [],
        output: { name: 'loss', dimension: 'pressure' },
        coefficients: {},
        expression: '0',
      },
    }
    const fetchMock = vi.fn(async () =>
      new Response(JSON.stringify(content), {
        status: 200,
        headers: { 'Content-Type': 'application/json' },
      }),
    )
    vi.stubGlobal('fetch', fetchMock)

    await expect(
      api.artifactRevision<CorrelationArtifactDefinition>(
        'project/a',
        'revision 1',
      ),
    ).resolves.toEqual(content)
    expect(fetchMock).toHaveBeenCalledWith(
      '/api/v1/projects/project%2Fa/artifact-revisions/revision%201',
      expect.objectContaining({
        headers: { Accept: 'application/json' },
      }),
    )
  })

  it('publishes a typed immutable correlation revision', async () => {
    const definition: CorrelationArtifactDefinition = {
      schema_version: 'thermox.correlation/v2',
      inputs: [{ name: 'mass_flow', dimension: 'mass_flow' }],
      output: { name: 'pressure_loss', dimension: 'pressure' },
      candidates: [{
        id: 'default', regime: 'general', priority: 0,
        coefficients: { coefficient: 1.5 },
        expression: 'coefficient * mass_flow * abs(mass_flow)',
        flow_regimes: [],
        fallback_for_unmapped_flow_regime: true,
      }],
    }
    const revision = {
      schema_version: 'thermox.artifact_revision/v1',
      artifact_revision_id: 'correlation-revision-2',
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
      api.createCorrelationRevision(
        'project-a',
        'bend-loss',
        'correlation-revision-1',
        definition,
      ),
    ).resolves.toEqual(revision)

    const [path, request] = fetchMock.mock.calls[0]
    const url = new URL(String(path), 'http://thermox.local')
    expect(url.searchParams.get('artifact_type')).toBe('thermox.correlation')
    expect(url.searchParams.get('artifact_schema_version')).toBe(
      'thermox.correlation/v2',
    )
    expect(url.searchParams.get('parent_revision_id')).toBe(
      'correlation-revision-1',
    )
    expect(request).toMatchObject({
      method: 'POST',
      body: JSON.stringify(definition),
    })
  })
})

describe('performance map artifact authoring API', () => {
  it('publishes an immutable ordinary map revision', async () => {
    const definition: PerformanceMapArtifactDefinition = {
      primary_variable: { name: 'flow', dimension: 'mass_flow' },
      family_variable: { name: 'speed', dimension: 'angular_speed' },
      output_variables: [{ name: 'efficiency', dimension: 'dimensionless' }],
      curves: [
        { family_coordinate: 1, samples: [{ coordinate: 1, outputs: [0.8] }, { coordinate: 2, outputs: [0.81] }] },
        { family_coordinate: 2, samples: [{ coordinate: 1, outputs: [0.82] }, { coordinate: 2, outputs: [0.83] }] },
      ],
      primary_extrapolation: 'reject',
      family_extrapolation: 'clamp',
    }
    const revision = { artifact_revision_id: 'map-r2', revision_number: 2 } as ArtifactRevision
    const fetchMock = vi.fn(
      async (_input: RequestInfo | URL, _init?: RequestInit) =>
        new Response(JSON.stringify(revision), { status: 201 }),
    )
    vi.stubGlobal('fetch', fetchMock)

    await api.createPerformanceMapRevision('project-a', 'map-a', 'map-r1', definition)

    const [path, request] = fetchMock.mock.calls[0]
    const url = new URL(String(path), 'http://thermox.local')
    expect(url.searchParams.get('artifact_type')).toBe('thermox.performance_map')
    expect(url.searchParams.get('artifact_schema_version')).toBe('thermox.performance_map/v1')
    expect(url.searchParams.get('parent_revision_id')).toBe('map-r1')
    expect(request).toMatchObject({ method: 'POST', body: JSON.stringify(definition) })
  })
})

describe('expression component authoring API', () => {
  it('publishes a typed immutable child revision', async () => {
    const definition: ExpressionComponentDefinition = {
      schema_version: 'thermox.expression_component/v5',
      kind: 'custom.signal.gain',
      version: '1.0.1',
      template_kind: 'custom.signal.gain',
      display_name: 'Signal gain',
      category: 'Project components',
      model_name: 'Custom expression',
      system_boundary_role: '',
      supports_steady: true,
      supports_transient: false,
      default_mode: '',
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
      transient_variables: [],
      internal_variables: [],
      transient_equations: [],
      modes: [],
      events: [],
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
      'thermox.expression_component/v5',
    )
    expect(url.searchParams.get('parent_revision_id')).toBe('revision-1')
    expect(request).toMatchObject({
      method: 'POST',
      body: JSON.stringify(definition),
    })
  })
})

describe('performance-map quality review API', () => {
  it('creates a review against an exact artifact revision', async () => {
    const review = {
      schema_version: 'thermox.performance_map_quality_review/v1',
      review_id: 'map-quality-review-00000001',
      project_id: 'project-a',
      artifact_revision_id: 'map revision/1',
      disposition: 'approved_with_conditions',
    }
    const fetchMock = vi.fn(async (
      _input: RequestInfo | URL,
      _init?: RequestInit,
    ) => new Response(JSON.stringify(review), {
        status: 201,
        headers: { 'Content-Type': 'application/json' },
    }))
    vi.stubGlobal('fetch', fetchMock)

    await expect(api.createPerformanceMapQualityReview(
      'project-a',
      'map revision/1',
      'approved_with_conditions',
      'Corrected speed 250-400 rad/s',
      'Qualified within the measured envelope.',
      'map-quality-review-00000000',
    )).resolves.toEqual(review)

    const [path, request] = fetchMock.mock.calls[0]
    expect(path).toBe(
      '/api/v1/projects/project-a/artifact-revisions/map%20revision%2F1/quality-reviews',
    )
    expect(request).toMatchObject({
      method: 'POST',
      body: JSON.stringify({
        schema_version: 'thermox.performance_map_quality_review.create/v1',
        supersedes_review_id: 'map-quality-review-00000000',
        disposition: 'approved_with_conditions',
        reviewed_scope: 'Corrected speed 250-400 rad/s',
        rationale: 'Qualified within the measured envelope.',
      }),
    })
  })

  it('lists the immutable review history', async () => {
    const history = {
      schema_version: 'thermox.performance_map_quality_review_list/v1',
      reviews: [],
    }
    const fetchMock = vi.fn(async () =>
      new Response(JSON.stringify(history), {
        status: 200,
        headers: { 'Content-Type': 'application/json' },
      }),
    )
    vi.stubGlobal('fetch', fetchMock)

    await expect(api.performanceMapQualityReviews(
      'project/a',
      'map-r1',
    )).resolves.toEqual(history)
    expect(fetchMock).toHaveBeenCalledWith(
      '/api/v1/projects/project%2Fa/artifact-revisions/map-r1/quality-reviews',
      expect.objectContaining({
        headers: { Accept: 'application/json' },
      }),
    )
  })
})
