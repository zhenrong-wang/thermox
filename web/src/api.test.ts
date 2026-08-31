import { afterEach, describe, expect, it, vi } from 'vitest'
import { api } from './api'
import type {
  ArtifactRevision,
  CorrelationArtifactDefinition,
  ExpressionComponentDefinition,
  PerformanceMapArtifactDefinition,
  TopologyDocument,
  TopologyPresentation,
} from './types'
import type { StudyPackageDocument } from './studyPackage'
import type { TopologyDraftDefinition } from './topologyDraft'

afterEach(() => {
  vi.unstubAllGlobals()
})

describe('topology presentation API', () => {
  it('loads an optional view and persists typed layout metadata with PUT', async () => {
    const presentation: TopologyPresentation = {
      schema_version: 'thermox.topology_presentation/v1',
      nodes: [{ entity_id: 'compressor', x: 120, y: 80 }],
      viewport: { x: 10, y: 20, zoom: 1.1 },
    }
    const record = {
      schema_version: 'thermox.topology_presentation/v1',
      project_id: 'project/a',
      team_id: 'team-a',
      user_id: 'user-a',
      model_revision_id: 'model-r2',
      updated_at_epoch_ms: 123,
      presentation,
    }
    const fetchMock = vi
      .fn()
      .mockResolvedValueOnce(new Response('', { status: 404 }))
      .mockResolvedValueOnce(new Response(JSON.stringify(record), {
        status: 200,
        headers: { 'Content-Type': 'application/json' },
      }))
    vi.stubGlobal('fetch', fetchMock)

    await expect(api.topologyPresentation('project/a')).resolves.toBeUndefined()
    await expect(
      api.putTopologyPresentation('project/a', 'model-r2', presentation),
    ).resolves.toEqual(record)

    expect(fetchMock.mock.calls[1]).toEqual([
      '/api/v1/projects/project%2Fa/topology-presentation',
      expect.objectContaining({
        method: 'PUT',
        body: JSON.stringify({
          schema_version: 'thermox.topology_presentation.put/v1',
          model_revision_id: 'model-r2',
          presentation,
        }),
      }),
    ])
  })
})

describe('topology revision API', () => {
  it('publishes the public declaration as a child of the selected revision', async () => {
    const definition: TopologyDocument = {
      schema_version: 'thermox.topology/v1',
      model: {
        id: 'declared-cycle',
        name: 'Declared cycle',
        revision: '2',
        media: [],
        components: [],
        connections: [],
      },
    }
    const revision = {
      schema_version: 'thermox.model_revision/v1',
      model_revision_id: 'model-r2',
      revision_number: 2,
    }
    const fetchMock = vi.fn(async (
      _input: RequestInfo | URL,
      _init?: RequestInit,
    ) =>
      new Response(JSON.stringify(revision), {
        status: 201,
        headers: { 'Content-Type': 'application/json' },
      }))
    vi.stubGlobal('fetch', fetchMock)

    await expect(
      api.createModelRevision('project/a', definition, 'model-r1'),
    ).resolves.toEqual(revision)

    const [path, request] = fetchMock.mock.calls[0]
    const url = new URL(String(path), 'http://thermox.local')
    expect(url.pathname).toBe(
      '/api/v1/projects/project%2Fa/model-revisions',
    )
    expect(url.searchParams.get('parent_revision_id')).toBe('model-r1')
    expect(request).toMatchObject({
      method: 'POST',
      body: JSON.stringify(definition),
    })
  })
})

describe('topology draft API', () => {
  it('publishes an immutable draft artifact without requiring a complete topology', async () => {
    const definition: TopologyDraftDefinition = {
      schema_version: 'thermox.topology_draft/v1',
      id: 'draft-cycle',
      document: { model: { id: 'cycle', components: [] } },
    }
    const revision = {
      schema_version: 'thermox.artifact_revision/v1',
      artifact_revision_id: 'draft-r2',
      artifact_id: 'draft-cycle',
      revision_number: 2,
    }
    const fetchMock = vi.fn(async (
      _input: RequestInfo | URL,
      _init?: RequestInit,
    ) => new Response(JSON.stringify(revision), {
      status: 201,
      headers: { 'Content-Type': 'application/json' },
    }))
    vi.stubGlobal('fetch', fetchMock)

    await api.createTopologyDraftRevision(
      'project/a', 'draft-cycle', 'draft-r1', definition,
    )

    const [path, request] = fetchMock.mock.calls[0]
    const url = new URL(String(path), 'http://thermox.local')
    expect(url.searchParams.get('artifact_type')).toBe('thermox.topology_draft')
    expect(url.searchParams.get('artifact_schema_version')).toBe(
      'thermox.topology_draft/v1',
    )
    expect(url.searchParams.get('parent_revision_id')).toBe('draft-r1')
    expect(request).toMatchObject({ method: 'POST', body: JSON.stringify(definition) })
  })
})

describe('Study package API', () => {
  it('submits one package operation with the selected topology parent', async () => {
    const document = {
      schema_version: 'thermox.study_package/v1',
      package_id: 'package-a',
      topology: {
        schema_version: 'thermox.topology/v1',
        model: {
          id: 'model-a', name: 'Model A', revision: '1',
          media: [], components: [], connections: [],
        },
      },
      case: {
        schema_version: 'thermox.case/v1',
        case: { id: 'case-a', mode: 'steady_state_design' },
      },
      artifact_dependencies: [],
      study: {
        study_id: 'study-a', intent: 'steady_state_design',
        artifact_revision_ids: [], artifact_qualification_requirements: [],
        artifact_operating_envelopes: [], result_projections: [],
        acceptance_criteria: [], trajectory_validation_bindings: [],
      },
    } satisfies StudyPackageDocument
    const fetchMock = vi.fn(async (
      _input: RequestInfo | URL,
      _init?: RequestInit,
    ) => new Response(JSON.stringify({
      schema_version: 'thermox.study_package_import/v1',
      package_id: 'package-a',
    }), { status: 201, headers: { 'Content-Type': 'application/json' } }))
    vi.stubGlobal('fetch', fetchMock)

    await api.importStudyPackage('project/a', document, 'model-r1')

    const [path, request] = fetchMock.mock.calls[0]
    const url = new URL(String(path), 'http://thermox.local')
    expect(url.pathname).toBe('/api/v1/projects/project%2Fa/study-packages')
    expect(url.searchParams.get('parent_model_revision_id')).toBe('model-r1')
    expect(request).toMatchObject({ method: 'POST', body: JSON.stringify(document) })
  })
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

describe('validation report API', () => {
  it('publishes a typed immutable validation campaign revision', async () => {
    const revision = {
      schema_version: 'thermox.artifact_revision/v1',
      artifact_revision_id: 'campaign-r2',
      revision_number: 2,
    } as ArtifactRevision
    const fetchMock = vi.fn(async (
      _input: RequestInfo | URL,
      _init?: RequestInit,
    ) =>
      new Response(JSON.stringify(revision), {
        status: 201,
        headers: { 'Content-Type': 'application/json' },
      }),
    )
    vi.stubGlobal('fetch', fetchMock)
    const definition = {
      schema_version: 'thermox.validation_campaign/v1' as const,
      id: 'campaign-a',
      name: 'Campaign A',
      objective: 'Check reference agreement',
      study_revision_ids: ['study-r1'],
      limitations: [],
    }

    await api.createValidationCampaignRevision(
      'project/a',
      'campaign-a',
      'campaign-r1',
      definition,
    )

    const [path, request] = fetchMock.mock.calls[0]
    const url = new URL(String(path), 'http://thermox.local')
    expect(url.pathname).toBe('/api/v1/projects/project%2Fa/artifact-revisions')
    expect(url.searchParams.get('artifact_type')).toBe(
      'thermox.validation_campaign',
    )
    expect(url.searchParams.get('artifact_schema_version')).toBe(
      'thermox.validation_campaign/v1',
    )
    expect(url.searchParams.get('parent_revision_id')).toBe('campaign-r1')
    expect(request).toMatchObject({
      method: 'POST',
      body: JSON.stringify(definition),
    })
  })

  it('submits only the explicitly selected immutable job identities', async () => {
    const response = {
      schema_version: 'thermox.job_validation_report/v2',
      team_id: 'team-a',
      project_id: 'project-a',
      campaign: {
        artifact_revision_id: 'campaign-r2',
        artifact_checksum: 'sha256:campaign',
        id: 'campaign-a',
        name: 'Campaign A',
        objective: 'Check reference agreement',
        limitations: [],
      },
      coverage: {
        job_count: 2,
        succeeded_count: 1,
        unsuccessful_count: 1,
        evidence_declared_count: 2,
        evaluated_count: 1,
        matched_count: 1,
        not_matched_count: 0,
        unevaluated_count: 1,
      },
      samples: {
        passed_count: 2,
        failed_count: 0,
        exact_alignment_count: 2,
        interpolated_alignment_count: 0,
      },
      jobs: [],
    }
    const fetchMock = vi.fn(async () =>
      new Response(JSON.stringify(response), {
        status: 200,
        headers: { 'Content-Type': 'application/json' },
      }),
    )
    vi.stubGlobal('fetch', fetchMock)

    await expect(api.validationReport(
      'project/a',
      'campaign-r2',
      ['job-2', 'job-1'],
    )).resolves.toEqual(response)
    expect(fetchMock).toHaveBeenCalledWith(
      '/api/v1/projects/project%2Fa/validation-reports',
      expect.objectContaining({
        method: 'POST',
        body: JSON.stringify({
          schema_version: 'thermox.job_validation_report.create/v2',
          campaign_artifact_revision_id: 'campaign-r2',
          job_ids: ['job-2', 'job-1'],
        }),
      }),
    )
  })

  it('downloads the server-rendered validation report representation', async () => {
    const fetchMock = vi.fn(async () =>
      new Response('# Thermox validation campaign report', {
        status: 200,
        headers: {
          'Content-Type': 'text/markdown; charset=utf-8',
          'Content-Disposition':
            'attachment; filename="thermox-validation-campaign-a.md"',
        },
      }),
    )
    vi.stubGlobal('fetch', fetchMock)

    await expect(api.validationReportExport(
      'project/a',
      'campaign-r2',
      ['job-2', 'job-1'],
      'markdown',
    )).resolves.toEqual({
      content: '# Thermox validation campaign report',
      filename: 'thermox-validation-campaign-a.md',
      mediaType: 'text/markdown; charset=utf-8',
    })
    expect(fetchMock).toHaveBeenCalledWith(
      '/api/v1/projects/project%2Fa/validation-report-exports?format=markdown',
      expect.objectContaining({
        method: 'POST',
        body: JSON.stringify({
          schema_version: 'thermox.job_validation_report.create/v2',
          campaign_artifact_revision_id: 'campaign-r2',
          job_ids: ['job-2', 'job-1'],
        }),
      }),
    )
  })
})

describe('thermal balance report API', () => {
  it('downloads the server-rendered balance report representation', async () => {
    const fetchMock = vi.fn(async () =>
      new Response('# Thermox thermal balance report', {
        status: 200,
        headers: {
          'Content-Type': 'text/markdown; charset=utf-8',
          'Content-Disposition':
            'attachment; filename="thermox-balance-job-a.md"',
        },
      }),
    )
    vi.stubGlobal('fetch', fetchMock)

    await expect(api.balanceReportExport(
      'job/a',
      'markdown',
    )).resolves.toEqual({
      content: '# Thermox thermal balance report',
      filename: 'thermox-balance-job-a.md',
      mediaType: 'text/markdown; charset=utf-8',
    })
    expect(fetchMock).toHaveBeenCalledWith(
      '/api/v1/jobs/job%2Fa/balance-report-export?format=markdown',
      expect.objectContaining({
        method: 'POST',
        body: JSON.stringify({
          schema_version: 'thermox.balance_report_request/v2',
          accounting_basis: 'energy',
          system_boundary: 'whole_system',
          diagram_profile: 'iso-14084-1:2015',
          calculation_profile: 'none',
          uncertainty_model: null,
        }),
      }),
    )
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
