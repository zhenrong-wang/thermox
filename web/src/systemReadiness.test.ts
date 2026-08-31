import { describe, expect, it } from 'vitest'
import { buildSystemReadiness } from './systemReadiness'
import type {
  Catalog,
  ProjectModelValidation,
  TopologyDocument,
} from './types'

const catalog = {
  schema_version: 'thermox.catalog/v14',
  status: 'succeeded',
  fingerprint: 'readiness-test',
  unit_dimensions: [],
  property_backends: [],
  thermochemistry_backends: [],
  connector_domains: [],
  correlation_templates: [],
  correlation_family_templates: [],
  regime_map_templates: [],
  components: [{
    kind: 'source.test',
    version: '1.0.0',
    template_kind: 'source',
    display_name: 'Source',
    category: 'Boundary',
    model_name: 'Test source',
    system_boundary_role: 'source',
    supports_steady: true,
    supports_transient: true,
    supported_modes: [],
    events: [],
    default_mode: '',
    ports: [{
      name: 'outlet',
      domain: 'fluid',
      direction: 'out',
      maximum_connections: 1,
      medium_source_port: '',
    }],
    parameters: [{
      name: 'pressure',
      dimension: 'pressure',
      required: true,
      default_value_si: null,
      lower_bound: null,
      upper_bound: null,
      lower_inclusive: false,
      upper_inclusive: false,
    }],
    artifacts: [],
  }],
} satisfies Catalog

function topology(): TopologyDocument {
  return {
    schema_version: 'thermox.topology/v1',
    model: {
      id: 'system',
      name: 'System',
      revision: '1',
      media: [{ id: 'air', backend: 'ideal', substance: 'air' }],
      materials: [],
      components: [{ id: 'source', kind: 'source.test' }],
      connections: [],
    },
  }
}

function validation(calculatable: boolean): ProjectModelValidation {
  return {
    validation: {
      readiness: { calculatable },
      diagnostics: calculatable
        ? []
        : [{
            code: 'under_specified',
            severity: 'error',
            stage: 'compile',
            json_path: '$.model',
            component_id: 'source',
            port_name: '',
            connection_id: '',
            message: 'The equation system is under-specified.',
            suggestions: ['Fix one additional independent boundary value.'],
          }],
    },
  } as ProjectModelValidation
}

describe('system readiness view model', () => {
  it('separates local definition and study hints from service authority', () => {
    const readiness = buildSystemReadiness(
      topology(),
      catalog,
      false,
      1,
    )

    expect(readiness.status).toBe('not_validated')
    expect(readiness.localIssueCount).toBe(4)
    expect(readiness.serviceIssueCount).toBe(0)
    expect(readiness.layers.map((layer) => [layer.id, layer.state])).toEqual([
      ['definition', 'blocked'],
      ['topology', 'ready'],
      ['study', 'blocked'],
      ['compilation', 'not_evaluated'],
    ])
    expect(readiness.issues[0].target).toEqual({
      type: 'component',
      id: 'source',
    })
  })

  it('surfaces blocked exact compiler diagnostics as service evidence', () => {
    const document = topology()
    document.model.components[0].media = { outlet: 'air' }
    document.model.components[0].parameters = { pressure: 101325 }

    const readiness = buildSystemReadiness(
      document,
      catalog,
      true,
      0,
      validation(false),
    )

    expect(readiness.status).toBe('blocked')
    expect(readiness.localIssueCount).toBe(0)
    expect(readiness.serviceIssueCount).toBe(1)
    expect(readiness.issues[0]).toMatchObject({
      authority: 'service',
      layer: 'compilation',
      message: 'The equation system is under-specified.',
    })
  })

  it('reports calculatable only from a successful exact service result', () => {
    const document = topology()
    document.model.components[0].media = { outlet: 'air' }
    document.model.components[0].parameters = { pressure: 101325 }

    const readiness = buildSystemReadiness(
      document,
      catalog,
      true,
      0,
      validation(true),
    )

    expect(readiness.status).toBe('calculatable')
    expect(readiness.issues).toEqual([])
    expect(readiness.layers.every((layer) => layer.state === 'ready')).toBe(true)
  })
})
