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

export type ScalarValue =
  | number
  | {
      value: number
      unit: string
    }

export interface CaseDocument {
  schema_version: 'thermox.case/v1'
  case: {
    id: string
    label?: string
    mode: string
    parameter_overrides?: Record<string, ScalarValue>
    fixed_values?: Record<string, ScalarValue>
    initial_guesses?: Record<string, ScalarValue>
    solver_options?: Record<string, ScalarValue>
  }
}

export interface CaseRevision {
  schema_version: 'thermox.case_revision/v1'
  case_revision_id: string
  model_revision_id: string
  project_id: string
  team_id: string
  case_id: string
  revision_number: number
  parent_case_revision_id: string
  mode: string
  checksum: string
  created_by_user_id: string
  created_at_epoch_ms: number
  case_document?: CaseDocument
}

export interface CaseRevisionList {
  schema_version: 'thermox.case_revision_list/v1'
  case_revisions: CaseRevision[]
}

export interface ValidationDiagnostic {
  code: string
  severity: 'information' | 'warning' | 'error'
  stage: string
  json_path: string
  component_id: string
  port_name: string
  connection_id: string
  message: string
  suggestions: string[]
}

export type ReadinessState = 'not_evaluated' | 'blocked' | 'ready'

export interface ReadinessLayer {
  id:
    | 'draft'
    | 'physical'
    | 'topology'
    | 'study'
    | 'compilation'
    | 'execution'
  state: ReadinessState
  diagnostic_codes: string[]
}

export interface EntityReadiness {
  entity_type: 'system' | 'component' | 'connection'
  entity_id: string
  state: ReadinessState
  diagnostic_codes: string[]
}

export interface ReadinessSummary {
  calculatable: boolean
  layers: ReadinessLayer[]
  entities: EntityReadiness[]
}

export interface ProjectModelValidation {
  schema_version: 'thermox.project_model_validation/v1'
  project_id: string
  model_revision_id: string
  model_checksum: string
  case_revision_id: string
  case_checksum: string
  artifact_revisions: ArtifactRevision[]
  validation: {
    schema_version: 'thermox.result/v3'
    status: string
    error: {
      schema_version: string
      code: string
      stage: string
      message: string
    }
    model: {
      schema_version: string
      model_id: string
      model_revision: string
      case_id: string
    }
    canonical_model_json: string
    compilation: {
      compiled: boolean
      mode: string
      variable_count: number
      equation_count: number
      catalog_fingerprint: string
      reduced_connection_equations: string[]
    }
    readiness: ReadinessSummary
    diagnostics: ValidationDiagnostic[]
  }
}

export interface SteadySolverSettings {
  max_iterations: number
  residual_tolerance: number
  step_tolerance: number
  finite_difference_epsilon: number
  min_damping: number
  damping_reduction: number
  sufficient_decrease: number
  max_line_search_steps: number
  continuation_enabled: boolean
  continuation_initial_step: number
  continuation_minimum_step: number
  continuation_step_growth: number
  continuation_step_reduction: number
  continuation_maximum_stages: number
}

export interface TransientSolverSettings {
  start_time: number
  end_time: number
  initial_step: number
  min_step: number
  max_step: number
  absolute_tolerance: number
  relative_tolerance: number
  max_steps: number
  max_consecutive_rejections: number
  maximum_order: number
  compute_consistent_initial_conditions: boolean
  nonlinear_solver: SteadySolverSettings
}

export type ResultValueScope =
  | 'system_balance'
  | 'kpi'
  | 'component_metric'
  | 'component_internal'
  | 'port_primary'
  | 'port_derived'

export type ResultAggregation = 'final' | 'minimum' | 'maximum'

export interface ResultProjection {
  id: string
  scope: ResultValueScope
  component_id: string
  port_name: string
  value_name: string
  dimension: string
  aggregation: ResultAggregation
}

export interface EngineeringAcceptanceCriterion {
  id: string
  projection_id: string
  dimension: string
  lower_bound_si: number | null
  upper_bound_si: number | null
  lower_inclusive: boolean
  upper_inclusive: boolean
}

export interface EngineeringAcceptanceResult {
  criterion_id: string
  projection_id: string
  dimension: string
  actual_value_si: number
  lower_bound_si: number | null
  upper_bound_si: number | null
  lower_inclusive: boolean
  upper_inclusive: boolean
  passed: boolean
}

export interface EngineeringAcceptanceSummary {
  passed: boolean
  passed_count: number
  failed_count: number
  criteria: EngineeringAcceptanceResult[]
}

export interface StudyRevision {
  schema_version: 'thermox.study_revision/v1'
  study_revision_id: string
  study_id: string
  project_id: string
  team_id: string
  revision_number: number
  parent_study_revision_id: string
  model_revision_id: string
  case_revision_id: string
  intent: string
  artifact_revision_ids: string[]
  result_projections: ResultProjection[]
  acceptance_criteria: EngineeringAcceptanceCriterion[]
  checksum: string
  created_by_user_id: string
  created_at_epoch_ms: number
}

export interface StudyRevisionList {
  schema_version: 'thermox.study_revision_list/v1'
  study_revisions: StudyRevision[]
}

export interface CreateStudyRevision {
  schema_version: 'thermox.study_revision.create/v1'
  study_id: string
  parent_study_revision_id: string
  model_revision_id: string
  case_revision_id: string
  intent: string
  artifact_revision_ids: string[]
  result_projections: ResultProjection[]
  acceptance_criteria: EngineeringAcceptanceCriterion[]
}

export interface CalibrationDocument {
  schema_version: 'thermox.calibration/v1'
  calibration: {
    id: string
    label?: string
    parameters: Array<{
      id: string
      label?: string
      scope: string
      targets: string[]
      cases?: string[]
      bounds?: { lower?: ScalarValue; upper?: ScalarValue }
      prior?: { mean: ScalarValue; sigma: ScalarValue }
    }>
    observations: Array<{
      id: string
      label?: string
      case: string
      target: string
      measured: ScalarValue
      sigma: ScalarValue
    }>
  }
}

export interface CalibrationSolverSettings {
  max_iterations: number
  initial_step_fraction: number
  minimum_step_fraction: number
  step_reduction: number
  minimum_continuation_fraction: number
  continuation_growth: number
  simulation_solver: SteadySolverSettings
}

export interface CalibrationRevision {
  schema_version: 'thermox.calibration_revision/v1'
  calibration_revision_id: string
  calibration_id: string
  project_id: string
  team_id: string
  revision_number: number
  parent_calibration_revision_id: string
  model_revision_id: string
  training_study_revision_ids: string[]
  validation_study_revision_ids: string[]
  definition: CalibrationDocument
  solver: CalibrationSolverSettings
  checksum: string
  created_by_user_id: string
  created_at_epoch_ms: number
}

export interface CalibrationRevisionList {
  schema_version: 'thermox.calibration_revision_list/v1'
  calibration_revisions: CalibrationRevision[]
}

export interface CreateCalibrationRevision {
  schema_version: 'thermox.calibration_revision.create/v1'
  calibration_id: string
  parent_calibration_revision_id: string
  model_revision_id: string
  training_study_revision_ids: string[]
  validation_study_revision_ids: string[]
  definition: CalibrationDocument
  solver?: Omit<Partial<CalibrationSolverSettings>, 'simulation_solver'> & {
    simulation_solver?: Partial<SteadySolverSettings>
  }
}

export interface RunConfigurationRevision {
  schema_version: 'thermox.run_configuration_revision/v3'
  run_configuration_revision_id: string
  run_configuration_id: string
  project_id: string
  team_id: string
  revision_number: number
  parent_run_configuration_revision_id: string
  study_revision_id: string
  steady_solver: SteadySolverSettings
  transient_solver: TransientSolverSettings
  checksum: string
  created_by_user_id: string
  created_at_epoch_ms: number
}

export interface RunConfigurationRevisionList {
  schema_version: 'thermox.run_configuration_revision_list/v1'
  run_configuration_revisions: RunConfigurationRevision[]
}

export interface CreateRunConfiguration {
  schema_version: 'thermox.run_configuration.create/v3'
  run_configuration_id: string
  parent_run_configuration_revision_id: string
  study_revision_id: string
  steady_solver: SteadySolverSettings
  transient_solver: TransientSolverSettings
}

export type SimulationJobState =
  | 'queued'
  | 'running'
  | 'succeeded'
  | 'failed'
  | 'cancelled'

export interface RevisionProvenance {
  project_id: string
  run_configuration_revision_id: string
  run_configuration_checksum: string
  study_revision_id: string
  study_checksum: string
  model_revision_id: string
  model_checksum: string
  case_revision_id: string
  case_checksum: string
  calibration_revision_id: string
  calibration_checksum: string
}

export interface ResultSummaryValue {
  id: string
  dimension: string
  value_si: number
  aggregation: ResultAggregation
  sample_time: number | null
}

export interface SimulationJob {
  schema_version: 'thermox.job/v10'
  job_id: string
  owner: {
    team_id: string
    submitted_by_user_id: string
  }
  revision: number
  created_at_unix_ms: number
  state: SimulationJobState
  request: {
    schema_version: 'thermox.job/v10'
    mode: 'steady' | 'transient' | 'calibration'
    case_id: string
    calibration_id: string
    source_revisions: RevisionProvenance | null
    engineering_artifacts: Array<{
      id: string
      artifact_type: string
      schema_version: string
      revision: string
      checksum_sha256: string
    }>
    result_projections: ResultProjection[]
    acceptance_criteria: EngineeringAcceptanceCriterion[]
    validation_prediction_count: number
    fingerprint: string
  }
  worker_id: string | null
  attempt: number
  lease_expires_at_unix_ms: number | null
  execution: {
    result_schema_version: string
    command_schema_version: string
    platform_version: string
    operation: string
    solver: {
      contract_version: string
      settings: Array<{
        name: string
        value: number
      }>
    }
    catalog_fingerprint: string
    source_revisions: RevisionProvenance | null
    model: {
      schema_version: string
      model_id: string
      model_revision: string
      case_id: string
    }
    components: Array<{
      component_id: string
      kind: string
      requested_version: string
      resolved_version: string
    }>
    media: Array<{
      medium_id: string
      backend: string
      substance: string
      package: string
      requested_package_version: string
      resolved_package_version: string
    }>
    artifacts: Array<{
      id: string
      artifact_type: string
      schema_version: string
      revision: string
      checksum_sha256: string
    }>
    connector_domains: Array<{
      domain: string
      contract_version: string
    }>
  } | null
  error: {
    schema_version: string
    code: string
    stage: string
    message: string
  } | null
  result_artifact: {
    artifact_id: string
    media_type: string
    schema_version: string
    byte_size: number
    checksum: string
  } | null
  result_summary: {
    schema_version: 'thermox.result_summary/v1'
    mode: 'steady' | 'transient'
    values: ResultSummaryValue[]
    engineering_acceptance: EngineeringAcceptanceSummary | null
  } | null
}

export interface SimulationJobPage {
  schema_version: 'thermox.job_list/v1'
  jobs: SimulationJob[]
  next_cursor: string | null
}

export type ComparedValueStatus =
  | 'matched'
  | 'baseline_only'
  | 'candidate_only'
  | 'dimension_mismatch'
  | 'aggregation_mismatch'

export interface ComparedResultValue {
  id: string
  status: ComparedValueStatus
  baseline_dimension: string
  candidate_dimension: string
  baseline_aggregation: ResultAggregation | null
  candidate_aggregation: ResultAggregation | null
  baseline_value_si: number | null
  candidate_value_si: number | null
  absolute_delta_si: number | null
  relative_delta: number | null
}

export interface SimulationJobComparison {
  schema_version: 'thermox.job_comparison/v1'
  team_id: string
  project_id: string
  baseline_job_id: string
  candidate_job_id: string
  baseline_study_revision_id: string
  candidate_study_revision_id: string
  mode: 'steady' | 'transient'
  coverage: {
    matched_count: number
    incompatible_count: number
    baseline_only_count: number
    candidate_only_count: number
  }
  engineering_acceptance: {
    baseline_passed: boolean | null
    candidate_passed: boolean | null
    transition: string
  }
  values: ComparedResultValue[]
}

export interface GraphResultValue {
  name: string
  dimension: string
  value_si: number
  derivative_si_s?: number
}

export interface GraphPortResult {
  port_name: string
  domain: string
  medium_id: string
  phase: string
  primary_values: GraphResultValue[]
  derived_values: GraphResultValue[]
}

export interface GraphComponentResult {
  component_id: string
  kind: string
  ports: GraphPortResult[]
  internal_values: GraphResultValue[]
  metrics: GraphResultValue[]
}

export interface GraphResult {
  components: GraphComponentResult[]
  system_balances: GraphResultValue[]
  kpis: GraphResultValue[]
}

export interface SteadySimulationResult {
  schema_version: 'thermox.result/v3'
  status: 'succeeded'
  error: {
    schema_version: string
    code: string
    stage: string
    message: string
  }
  metadata: NonNullable<SimulationJob['execution']>
  diagnostics: {
    converged: boolean
    iterations: number
    final_residual_norm: number
    final_step_norm: number
    function_evaluations: number
    jacobian_evaluations: number
    linear_solver_evaluations: number
    symbolic_factorizations: number
    numeric_factorizations: number
    linear_solver_backend: string
    message: string
  }
  continuation: {
    enabled: boolean
    converged: boolean
    used_informed_path: boolean
    reached_parameter: number
    accepted_stages: number
    rejected_stages: number
    message: string
    stages: Array<{
      start_parameter: number
      target_parameter: number
      accepted: boolean
      nonlinear_iterations: number
      final_residual_norm: number
      message: string
    }>
  }
  graph: GraphResult
  reduced_connection_equations: string[]
}

export interface TransientGraphSample {
  time: number
  graph: GraphResult
}

export interface TransientSimulationResult {
  schema_version: 'thermox.result/v3'
  status: 'succeeded'
  error: {
    schema_version: string
    code: string
    stage: string
    message: string
  }
  metadata: NonNullable<SimulationJob['execution']>
  diagnostics: {
    success: boolean
    accepted_steps: number
    rejected_steps: number
    maximum_order_used: number
    nonlinear_solves: number
    nonlinear_iterations: number
    symbolic_factorizations: number
    numeric_factorizations: number
    linear_solver_backend: string
    final_time: number
    last_step: number
    message: string
  }
  trajectory: TransientGraphSample[]
  events: Array<TransientGraphSample & {
    name: string
    terminal: boolean
  }>
}

export type SimulationResult =
  | SteadySimulationResult
  | TransientSimulationResult

export type CaseScalarField =
  | 'parameter_override'
  | 'fixed_value'
  | 'initial_guess'
  | 'solver_option'

export type CaseEditOperation =
  | {
      action: 'upsert'
      field: 'label' | 'mode'
      value: string
    }
  | {
      action: 'remove'
      field: 'label'
    }
  | {
      action: 'upsert'
      field: CaseScalarField
      key: string
      value: ScalarValue
    }
  | {
      action: 'remove'
      field: CaseScalarField
      key: string
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
    assemblies?: AssemblyDefinition[]
    connections: ConnectionDefinition[]
  }
}

export interface AssemblyDefinition {
  id: string
  label?: string
  ports: Array<{
    name: string
    endpoint: string
  }>
  parameters?: Array<{
    name: string
    target: string
  }>
  components: ComponentDefinition[]
  assemblies?: AssemblyDefinition[]
  connections: ConnectionDefinition[]
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

export interface ArtifactRevision {
  schema_version: 'thermox.artifact_revision/v1'
  artifact_revision_id: string
  project_id: string
  team_id: string
  artifact_id: string
  revision_number: number
  parent_artifact_revision_id: string
  artifact_type: string
  artifact_schema_version: string
  content: {
    media_type: string
    byte_size: number
    checksum: string
  }
  created_by_user_id: string
  created_at_epoch_ms: number
}

export interface ArtifactRevisionList {
  schema_version: 'thermox.artifact_revision_list/v1'
  artifact_revisions: ArtifactRevision[]
}

export interface ArtifactRevisionContent<T = unknown> {
  schema_version: 'thermox.artifact_revision_content/v1'
  revision: ArtifactRevision
  artifact: T
}

export interface AssemblyTemplateCatalogEntry {
  source: ArtifactRevision
  definition: TopologyDocument
}

export interface CorrelationArtifactDefinition {
  schema_version: 'thermox.correlation/v1'
  inputs: Array<{
    name: string
    dimension: string
  }>
  output: {
    name: string
    dimension: string
  }
  candidates: Array<{
    id: string
    regime: string
    priority: number
    coefficients: Record<string, number>
    expression: string
    applicability?: Array<{
      input: string
      minimum?: number
      maximum?: number
      minimum_inclusive: boolean
      maximum_inclusive: boolean
    }>
  }>
}

export type MapExtrapolationPolicy = 'reject' | 'clamp' | 'linear'

export interface PerformanceMapArtifactDefinition {
  primary_variable: {
    name: string
    dimension: string
  }
  family_variable: {
    name: string
    dimension: string
  }
  output_variables: Array<{
    name: string
    dimension: string
  }>
  curves: Array<{
    family_coordinate: number
    samples: Array<{
      coordinate: number
      outputs: number[]
    }>
  }>
  primary_extrapolation: MapExtrapolationPolicy
  family_extrapolation: MapExtrapolationPolicy
}

export interface ProjectComponentCatalogEntry {
  source: ArtifactRevision
  catalog_fingerprint: string
  component: CatalogComponent
  definition: ExpressionComponentDefinition
}

export interface ProjectComponentCatalog {
  schema_version: 'thermox.project_component_catalog/v1'
  project_id: string
  components: ProjectComponentCatalogEntry[]
}

export interface ExpressionComponentDefinition {
  schema_version: 'thermox.expression_component/v2'
  kind: string
  version: string
  template_kind: string
  display_name: string
  category: string
  model_name: string
  system_boundary_role: string
  ports: Array<{
    name: string
    domain: string
    direction: 'in' | 'out' | 'bidirectional'
    maximum_connections: number
  }>
  parameters: Array<{
    name: string
    dimension: string
    required: boolean
    default_value_si: number | null
    lower_bound: number | null
    upper_bound: number | null
    lower_inclusive: boolean
    upper_inclusive: boolean
  }>
  equations: Array<{
    name: string
    expression: string
    residual_scale: number
  }>
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
  template_kind: string
  display_name: string
  category: string
  model_name: string
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
  source_artifact_id?: string
  source_artifact_revision_id?: string
}

export interface PropertyBackend {
  backend: string
  implementation_name: string
  implementation_version: string
  supported_substances: string[]
  capabilities: string[]
}

export interface ThermochemistryBackend {
  backend: string
  implementation_name: string
  implementation_version: string
  capabilities: string[]
}

export interface ConnectorDomain {
  domain: string
  contract_version: string
  connection_kind: string
  variables: Array<{
    name: string
    dimension: string
    initial_value_si: number
    scale_si: number
    expand_species: boolean
  }>
}

export interface CatalogDisplayUnit {
  symbol: string
  scale_from_si: number
  offset_from_si: number
}

export interface CatalogUnitDimension {
  dimension: string
  canonical_unit: string
  si_display: CatalogDisplayUnit
  engineering_display: CatalogDisplayUnit
  accepted_units: Array<{
    symbol: string
    aliases: string[]
    scale_to_si: number
    offset_to_si: number
  }>
}

export interface Catalog {
  schema_version: 'thermox.catalog/v5'
  status: string
  fingerprint: string
  components: CatalogComponent[]
  unit_dimensions: CatalogUnitDimension[]
  property_backends: PropertyBackend[]
  thermochemistry_backends: ThermochemistryBackend[]
  connector_domains: ConnectorDomain[]
}

export type GraphEntityType =
  | 'medium'
  | 'material'
  | 'component'
  | 'assembly'
  | 'connection'

export interface GraphUpsertOperation {
  action: 'upsert'
  entity_type: GraphEntityType
  entity_id: string
  entity: Record<string, unknown>
}

export interface GraphRemoveOperation {
  action: 'remove'
  entity_type: GraphEntityType
  entity_id: string
  cascade?: boolean
}

export type GraphEditOperation =
  | GraphUpsertOperation
  | GraphRemoveOperation
