import { describe, expect, it } from 'vitest'
import {
  mergeProjectComponentCatalog,
  requiredProjectComponentSources,
} from './projectComponentCatalog'
import type {
  Catalog,
  ProjectComponentCatalogEntry,
  TopologyDocument,
} from './types'

const native = {
  kind: 'source.signal.fixed',
  version: '1.0.0',
  system_boundary_role: 'source',
  supports_steady: true,
  supports_transient: false,
  ports: [],
  parameters: [],
  artifacts: [],
}

const projectEntry: ProjectComponentCatalogEntry = {
  source: {
    schema_version: 'thermox.artifact_revision/v1',
    artifact_revision_id: 'artifact-revision-7',
    project_id: 'project-a',
    team_id: 'team-a',
    artifact_id: 'gain-definition',
    revision_number: 7,
    parent_artifact_revision_id: 'artifact-revision-6',
    artifact_type: 'thermox.expression_component',
    artifact_schema_version: 'thermox.expression_component/v1',
    content: {
      media_type: 'application/json',
      byte_size: 42,
      checksum: `sha256:${'a'.repeat(64)}`,
    },
    created_by_user_id: 'user-a',
    created_at_epoch_ms: 1,
  },
  catalog_fingerprint: 'fnv1a64:overlay',
  component: {
    ...native,
    kind: 'custom.signal.gain',
    system_boundary_role: '',
    ports: [
      {
        name: 'input',
        domain: 'signal',
        direction: 'in',
        maximum_connections: 1,
      },
    ],
  },
  definition: {
    schema_version: 'thermox.expression_component/v1',
    kind: 'custom.signal.gain',
    version: '1.0.0',
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
        name: 'gain_law',
        expression: 'input.value',
        residual_scale: 1,
      },
    ],
  },
}

const historicalEntry: ProjectComponentCatalogEntry = {
  ...projectEntry,
  source: {
    ...projectEntry.source,
    artifact_revision_id: 'artifact-revision-6',
    revision_number: 6,
    parent_artifact_revision_id: '',
    created_at_epoch_ms: 0,
  },
  component: {
    ...projectEntry.component,
    version: '0.9.0',
  },
  definition: {
    ...projectEntry.definition,
    version: '0.9.0',
  },
}

const catalog: Catalog = {
  schema_version: 'thermox.catalog/v4',
  status: 'succeeded',
  fingerprint: 'fnv1a64:base',
  components: [native],
  unit_dimensions: [],
  property_backends: [],
  connector_domains: [],
}

describe('project component catalog', () => {
  it('merges project definitions into the draggable catalog', () => {
    const merged = mergeProjectComponentCatalog(catalog, [
      historicalEntry,
      projectEntry,
    ])
    expect(merged.fingerprint).toBe('fnv1a64:overlay')
    expect(merged.components).toHaveLength(2)
    expect(
      merged.components.find(
        (component) => component.kind === 'custom.signal.gain',
      )?.source_artifact_revision_id,
    ).toBe('artifact-revision-7')
  })

  it('binds the exact source revision required by topology instances', () => {
    const topology: TopologyDocument = {
      schema_version: 'thermox.topology/v1',
      model: {
        id: 'custom-system',
        name: 'Custom system',
        revision: '1',
        media: [],
        components: [
          {
            id: 'gain-a',
            kind: 'custom.signal.gain',
            version: '1.0.0',
          },
          {
            id: 'gain-b',
            kind: 'custom.signal.gain',
            version: '1.0.0',
          },
        ],
        connections: [],
      },
    }
    expect(
      requiredProjectComponentSources(
        topology,
        [projectEntry],
      ).map((entry) => entry.source.artifact_revision_id),
    ).toEqual(['artifact-revision-7'])
  })

  it('retains historical descriptors for older topology versions', () => {
    const topology: TopologyDocument = {
      schema_version: 'thermox.topology/v1',
      model: {
        id: 'old-system',
        name: 'Old system',
        revision: '1',
        media: [],
        components: [
          {
            id: 'gain',
            kind: 'custom.signal.gain',
            version: '0.9.0',
          },
        ],
        connections: [],
      },
    }
    expect(
      requiredProjectComponentSources(topology, [
        projectEntry,
        historicalEntry,
      ])[0]?.source.artifact_revision_id,
    ).toBe('artifact-revision-6')
  })
})
