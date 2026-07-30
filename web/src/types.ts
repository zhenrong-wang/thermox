export interface Project {
  schema_version: 'thermox.project/v1'
  project_id: string
  team_id: string
  name: string
  description: string
  created_by_user_id: string
  created_at_epoch_ms: number
}

export interface ProjectList {
  schema_version: 'thermox.project_list/v1'
  projects: Project[]
}

export interface ModelRevision {
  schema_version: 'thermox.model_revision/v1'
  model_revision_id: string
  project_id: string
  team_id: string
  revision_number: number
  parent_model_revision_id: string
  model_schema_version: string
  model_id: string
  model_revision_label: string
  checksum: string
  created_by_user_id: string
  created_at_epoch_ms: number
  model?: TopologyDocument
}

export interface ModelRevisionList {
  schema_version: 'thermox.model_revision_list/v1'
  model_revisions: ModelRevision[]
}

export interface TopologyDocument {
  schema_version: 'thermox.topology/v1'
  model: {
    id: string
    name: string
    revision: string
    media: MediumDefinition[]
    materials?: MaterialDefinition[]
    components: ComponentDefinition[]
    connections: ConnectionDefinition[]
  }
}

export interface MediumDefinition {
  id: string
  backend: string
  substance: string
  package_version?: string
}

export interface MaterialDefinition {
  id: string
  backend: string
  mechanism: string
  phase: string
  species: string[]
  package_version?: string
}

export interface ComponentDefinition {
  id: string
  label?: string
  kind: string
  version?: string
  media?: Record<string, string>
  materials?: Record<string, string>
  artifacts?: Record<string, string>
  parameters?: Record<string, unknown>
}

export interface ConnectionDefinition {
  id: string
  from: string
  to: string
  kind: string
  contract_version?: string
}

export interface CatalogPort {
  name: string
  domain: string
  direction: 'in' | 'out' | 'bidirectional'
  maximum_connections: number
}

export interface CatalogParameter {
  name: string
  dimension: string
  required: boolean
  default_value_si: number | null
  lower_bound: number | null
  upper_bound: number | null
  lower_inclusive: boolean
  upper_inclusive: boolean
}

export interface CatalogComponent {
  kind: string
  version: string
  system_boundary_role: string
  supports_steady: boolean
  supports_transient: boolean
  ports: CatalogPort[]
  parameters: CatalogParameter[]
  artifacts: Array<{
    role: string
    artifact_type: string
    required: boolean
  }>
}

export interface Catalog {
  schema_version: 'thermox.catalog/v3'
  status: string
  fingerprint: string
  components: CatalogComponent[]
}
