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
    input_schedules?: Record<string, {
      interpolation: 'linear' | 'previous'
      points: Array<{ time: ScalarValue; value: ScalarValue }>
    }>
    component_modes?: Record<string, string>
    state_events?: Array<{
      id: string
      target: string
      threshold: ScalarValue
      direction: 'any' | 'rising' | 'falling'
      terminal: boolean
      priority?: number
      hysteresis?: ScalarValue
      actions?: Array<
        | { type: 'set_input'; target: string; value: ScalarValue; source?: never }
        | { type: 'set_input'; target: string; source: string; value?: never }
        | { type: 'set_mode'; target: string; mode: string }
        | { type: 'set_state'; target: string; value: ScalarValue; source?: never }
        | { type: 'set_state'; target: string; source: string; value?: never }
      >
    }>
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

export interface PerformanceMapOutputQuality {
  name: string
  minimum: number
  maximum: number
  maximum_absolute_primary_slope: number
  maximum_absolute_primary_slope_jump: number
  maximum_absolute_family_slope: number
  declared_constraint: {
    minimum: number | null
    maximum: number | null
    minimum_inclusive: boolean
    maximum_inclusive: boolean
  } | null
  minimum_lower_margin: number | null
  minimum_upper_margin: number | null
}

export interface PerformanceMapLayerQuality {
  layer_index: number
  condition_coordinate: number | null
  curve_count: number
  sample_count: number
  family_domain: { minimum: number; maximum: number }
  common_primary_domain: { minimum: number; maximum: number } | null
  minimum_adjacent_primary_overlap: number
  outputs: PerformanceMapOutputQuality[]
  advisory_codes: string[]
}

export interface PerformanceMapQuality {
  schema_version: 'thermox.performance_map_quality/v1'
  artifact_id: string
  conditioned: boolean
  condition_domain: { minimum: number; maximum: number } | null
  common_family_domain: { minimum: number; maximum: number } | null
  minimum_adjacent_family_overlap: number
  minimum_adjacent_primary_overlap: number
  layers: PerformanceMapLayerQuality[]
  condition_outputs: Array<{
    name: string
    maximum_absolute_condition_slope: number
  }>
  advisory_codes: string[]
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
    schema_version: 'thermox.result/v6'
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
      largest_structural_block_size: number
      structural_blocks: Array<{
        variable_names: string[]
        equation_names: string[]
        suggested_tear_variable_names: string[]
        acyclic_after_suggested_tears: boolean
        structural_nonzero_count: number
        suggested_inner_variable_count: number
        suggested_inner_nonzero_count: number
        suggested_tear_coupling_nonzero_count: number
        suggested_dense_schur_entry_count: number
      }>
      catalog_fingerprint: string
      reduced_connection_equations: string[]
    }
    readiness: ReadinessSummary
    performance_map_quality: PerformanceMapQuality[]
    diagnostics: ValidationDiagnostic[]
  }
}

export interface SteadySolverSettings {
  max_iterations: number
  residual_tolerance: number
  step_tolerance: number
  linear_residual_tolerance: number
  structural_decomposition_policy:
    | 'automatic'
    | 'monolithic'
    | 'blocks'
    | 'tearing'
  finite_difference_epsilon: number
  min_damping: number
  damping_reduction: number
  sufficient_decrease: number
  max_line_search_steps: number
  globalization_policy: 'line_search' | 'trust_region'
  trust_region_initial_radius: number
  trust_region_minimum_radius: number
  trust_region_maximum_radius: number
  trust_region_acceptance_threshold: number
  max_trust_region_steps: number
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
  required_output_times: number[]
  nonlinear_solver: SteadySolverSettings
}

export type ResultValueScope =
  | 'system_balance'
  | 'kpi'
  | 'component_metric'
  | 'component_internal'
  | 'port_primary'
  | 'port_derived'

export type ResultAggregation =
  | 'final'
  | 'minimum'
  | 'maximum'
  | 'mean'
  | 'root_mean_square'
  | 'change'

export interface ResultWindow {
  anchor: 'simulation' | 'event'
  start_time: number
  end_time: number
  event_name: string
  event_occurrence: number
}

export interface ResultProjection {
  id: string
  scope: ResultValueScope
  component_id: string
  port_name: string
  value_name: string
  dimension: string
  aggregation: ResultAggregation
  window?: ResultWindow
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
  lower_margin_si: number | null
  upper_margin_si: number | null
  limiting_margin_si: number
  limiting_bound: 'lower' | 'upper'
  passed: boolean
}

export interface EngineeringAcceptanceSummary {
  passed: boolean
  passed_count: number
  failed_count: number
  criteria: EngineeringAcceptanceResult[]
}

export interface StudyTrajectoryValidationBinding {
  id: string
  artifact_revision_id: string
  signal_id: string
  projection_id: string
  comparison: 'absolute' | 'projected_change'
  time_offset_si: number
  baseline_time_si: number
  absolute_tolerance_si: number
  relative_tolerance: number
  uncertainty_multiplier: number
  maximum_interpolation_gap_si: number
}

export interface ValidationSeriesArtifact {
  schema_version: 'thermox.validation_series/v1'
  id: string
  source: {
    reference: string
    checksum_sha256: string
    evidence_basis: string
    acquisition: 'measured' | 'computational' | 'derived' | 'digitized'
    note?: string
    limitations: string[]
  }
  time_unit: string
  signals: Array<{
    id: string
    dimension: string
    unit: string
    samples: Array<{
      time: number
      value: number
      standard_uncertainty?: number
    }>
  }>
}

export interface ValidationSeriesCatalogEntry {
  source: ArtifactRevision
  definition: ValidationSeriesArtifact
}

export interface StudyRevision {
  schema_version: 'thermox.study_revision/v5'
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
  artifact_qualification_requirements: ArtifactQualificationRequirement[]
  artifact_operating_envelopes: ArtifactOperatingEnvelope[]
  result_projections: ResultProjection[]
  acceptance_criteria: EngineeringAcceptanceCriterion[]
  trajectory_validation_bindings: StudyTrajectoryValidationBinding[]
  checksum: string
  created_by_user_id: string
  created_at_epoch_ms: number
}

export interface ArtifactCoordinateConstraint {
  coordinate: string
  dimension: string
  minimum?: number
  maximum?: number
  minimum_inclusive: boolean
  maximum_inclusive: boolean
}

export interface ArtifactOperatingEnvelope {
  artifact_revision_id: string
  coordinates: ArtifactCoordinateConstraint[]
}

export interface ArtifactQualificationRequirement {
  artifact_revision_id: string
  review_id: string
  acceptable_dispositions: EngineeringReviewDisposition[]
}

export interface StudyRevisionList {
  schema_version: 'thermox.study_revision_list/v1'
  study_revisions: StudyRevision[]
}

export interface CreateStudyRevision {
  schema_version: 'thermox.study_revision.create/v5'
  study_id: string
  parent_study_revision_id: string
  model_revision_id: string
  case_revision_id: string
  intent: string
  artifact_revision_ids: string[]
  artifact_qualification_requirements: ArtifactQualificationRequirement[]
  artifact_operating_envelopes: ArtifactOperatingEnvelope[]
  result_projections: ResultProjection[]
  acceptance_criteria: EngineeringAcceptanceCriterion[]
  trajectory_validation_bindings: StudyTrajectoryValidationBinding[]
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
      time?: ScalarValue
    }>
  }
}

export interface CalibrationSolverSettings {
  max_iterations: number
  finite_difference_fraction: number
  initial_trust_region_radius: number
  minimum_trust_region_radius: number
  maximum_trust_region_radius: number
  acceptance_ratio: number
  gradient_tolerance: number
  step_tolerance: number
  objective_relative_tolerance: number
  minimum_continuation_fraction: number
  continuation_growth: number
  steady_simulation_solver: SteadySolverSettings
  transient_simulation_solver: TransientSolverSettings
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
  solver?: Omit<Partial<CalibrationSolverSettings>, 'steady_simulation_solver' | 'transient_simulation_solver'> & {
    steady_simulation_solver?: Partial<SteadySolverSettings>
    transient_simulation_solver?: Partial<TransientSolverSettings>
  }
}

export type ReconciliationMode =
  | 'hard_equalities'
  | 'weighted_measurements'

export interface ReconciliationSolverSettings {
  max_iterations: number
  finite_difference_fraction: number
  constraint_tolerance: number
  step_tolerance: number
  objective_relative_tolerance: number
  minimum_line_search_fraction: number
  simulation_solver: SteadySolverSettings
}

export interface ProfileLikelihoodSettings {
  enabled: boolean
  objective_increase: number
  maximum_bracket_steps: number
  maximum_bisection_steps: number
  maximum_nuisance_iterations: number
  parameter_ids: string[]
}

export interface JointConfidenceRegionSettings {
  enabled: boolean
  objective_increase: number
  parameter_ids: string[]
}

export interface ReconciliationRevision {
  schema_version: 'thermox.reconciliation_revision/v1'
  reconciliation_revision_id: string
  reconciliation_id: string
  project_id: string
  team_id: string
  revision_number: number
  parent_reconciliation_revision_id: string
  model_revision_id: string
  constraint_study_revision_ids: string[]
  held_out_study_revision_ids: string[]
  definition: CalibrationDocument
  mode: ReconciliationMode
  solver: ReconciliationSolverSettings
  profile_likelihood: ProfileLikelihoodSettings
  joint_confidence_region: JointConfidenceRegionSettings
  checksum: string
  created_by_user_id: string
  created_at_epoch_ms: number
}

export interface ReconciliationRevisionList {
  schema_version: 'thermox.reconciliation_revision_list/v1'
  reconciliation_revisions: ReconciliationRevision[]
}

export interface CreateReconciliationRevision {
  schema_version: 'thermox.reconciliation_revision.create/v1'
  reconciliation_id: string
  parent_reconciliation_revision_id: string
  model_revision_id: string
  constraint_study_revision_ids: string[]
  held_out_study_revision_ids: string[]
  definition: CalibrationDocument
  mode: ReconciliationMode
  solver?: Omit<Partial<ReconciliationSolverSettings>, 'simulation_solver'> & {
    simulation_solver?: Partial<SteadySolverSettings>
  }
  profile_likelihood?: Partial<ProfileLikelihoodSettings>
  joint_confidence_region?: Partial<JointConfidenceRegionSettings>
}

export interface ReconciliationResult {
  schema_version: 'thermox.result/v6'
  status: 'succeeded'
  calculation_intent: 'data_reconciliation'
  reconciliation_mode: ReconciliationMode
  reconciliation_id: string
  diagnostics: {
    converged: boolean
    iterations: number
    model_evaluations: number
    final_maximum_absolute_normalized_constraint: number
    adjustable_quantity_count: number
    measurement_count: number
    degrees_of_freedom: number
    weighted_sum_squares: number
    reduced_chi_square_available: boolean
    reduced_chi_square: number
    locally_identifiable: boolean
    active_bound_count: number
    locally_bound_limited: boolean
    message: string
  }
  inferred_parameters: Array<{
    id: string
    scope: string
    dimension: string
    initial_value_si: number
    inferred_value_si: number
    lower_bound_si: number
    upper_bound_si: number
    targets: string[]
  }>
  hard_constraints: Array<{
    id: string
    case_id: string
    target: string
    dimension: string
    required_si: number
    solved_si: number
    residual_si: number
    normalized_residual: number
  }>
  weighted_measurements: Array<{
    id: string
    case_id: string
    target: string
    dimension: string
    measured_si: number
    reconciled_si: number
    residual_si: number
    normalized_residual: number
  }>
  parameter_uncertainties: Array<{
    parameter_id: string
    dimension: string
    standard_uncertainty_si: number | null
    bound_active: boolean
    interpretation: string
  }>
  profile_likelihood_intervals: Array<{
    parameter_id: string
    dimension: string
    estimate_si: number
    succeeded: boolean
    message: string
  }>
  joint_confidence_region: {
    parameter_ids: string[]
    dimensions: string[]
    center_si: number[]
    covariance_si: number[][]
    requested_objective_increase: number
    succeeded: boolean
    interpretation: string
    message: string
  } | null
  held_out_results: Array<{
    case_id: string
    weighted_sum_squares: number
    observations: Array<{
      id: string
      target: string
      dimension: string
      measured_si: number
      predicted_si: number
      sigma_si: number
      residual_si: number
      normalized_residual: number
    }>
  }>
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
  reconciliation_revision_id: string
  reconciliation_checksum: string
}

export interface ResultSummaryValue {
  id: string
  dimension: string
  value_si: number
  aggregation: ResultAggregation
  sample_time: number | null
  window: {
    start_time: number
    end_time: number
    anchor_event_name: string | null
    anchor_event_occurrence: number
  } | null
}

export interface SimulationJob {
  schema_version: 'thermox.job/v19'
  job_id: string
  owner: {
    team_id: string
    submitted_by_user_id: string
  }
  revision: number
  created_at_unix_ms: number
  state: SimulationJobState
  request: {
    schema_version: 'thermox.job/v19'
    mode: 'steady' | 'transient' | 'calibration' | 'reconciliation'
    case_id: string
    calibration_id: string
    reconciliation_id: string
    reconciliation_mode: 'hard_equalities' | 'weighted_measurements'
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
    trajectory_validation_count: number
    trajectory_validations: Array<{
      artifact_revision_id: string
      artifact_id: string
      source_reference: string
      source_checksum_sha256: string
      evidence_basis: string
      acquisition: 'measured' | 'computational' | 'derived' | 'digitized'
      note: string
      limitations: string[]
      bindings: Array<{
        signal_id: string
        projection_id: string
        comparison: 'absolute' | 'projected_change'
        time_offset_si: number
        baseline_time_si: number
        absolute_tolerance_si: number
        relative_tolerance: number
        uncertainty_multiplier: number
        maximum_interpolation_gap_si: number
      }>
    }>
    validation_prediction_count: number
    reconciliation_held_out_case_count: number
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
    schema_version: 'thermox.result_summary/v5'
    mode: 'steady' | 'transient'
    values: ResultSummaryValue[]
    engineering_acceptance: EngineeringAcceptanceSummary | null
    trajectory_validation: {
      passed: boolean
      validation_count: number
      passed_count: number
      failed_count: number
      exact_alignment_count: number
      interpolated_alignment_count: number
    } | null
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
  | 'window_mismatch'

export interface ComparedResultValue {
  id: string
  status: ComparedValueStatus
  baseline_dimension: string
  candidate_dimension: string
  baseline_aggregation: ResultAggregation | null
  candidate_aggregation: ResultAggregation | null
  baseline_window: ResultSummaryValue['window']
  candidate_window: ResultSummaryValue['window']
  baseline_value_si: number | null
  candidate_value_si: number | null
  absolute_delta_si: number | null
  relative_delta: number | null
}

export interface SimulationJobComparison {
  schema_version: 'thermox.job_comparison/v3'
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
  trajectory_validation: {
    baseline_passed: boolean | null
    candidate_passed: boolean | null
    baseline_sample_count: number
    candidate_sample_count: number
    compatibility: string
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

export interface CounterflowApproachResult {
  component_id: string
  component_kind: string
  hot_in_minus_cold_out_k: number
  hot_out_minus_cold_in_k: number
  minimum_approach_k: number
  sample_time: number | null
  passed: boolean
}

export interface ThermalFeasibilitySummary {
  schema_version: 'thermox.thermal_feasibility/v1'
  scope: 'steady' | 'trajectory'
  required_minimum_approach_k: number
  passed: boolean
  checked_count: number
  passed_count: number
  failed_count: number
  counterflow_approaches: CounterflowApproachResult[]
}

export interface SteadySimulationResult {
  schema_version: 'thermox.result/v6'
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
    final_maximum_absolute_normalized_residual: number
    limiting_residual: string
    final_step_norm: number
    last_linear_backward_error: number
    maximum_linear_backward_error: number
    linear_refinement_attempts: number
    linear_refinement_successes: number
    structural_block_solves: number
    largest_linear_system_size: number
    structural_tearing_attempts: number
    structural_tearing_successes: number
    structural_tearing_fallbacks: number
    largest_tearing_inner_system_size: number
    largest_tearing_outer_system_size: number
    largest_tearing_inner_nonzero_count: number
    last_structural_tearing_fallback: string
    failed_structural_block: string
    function_evaluations: number
    jacobian_evaluations: number
    linear_solver_evaluations: number
    trust_region_trials: number
    trust_region_rejections: number
    final_trust_region_radius: number
    symbolic_factorizations: number
    numeric_factorizations: number
    factorization_quality_observations: number
    last_reciprocal_pivot_ratio: number
    minimum_reciprocal_pivot_ratio: number
    minimum_absolute_pivot_at_minimum_ratio: number
    maximum_absolute_pivot_at_minimum_ratio: number
    accepted_pivot_count_at_minimum_ratio: number
    factorization_size_at_minimum_ratio: number
    factorization_quality_method: string
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
      final_maximum_absolute_normalized_residual: number
      limiting_residual: string
      maximum_linear_backward_error: number
      linear_refinement_attempts: number
      linear_refinement_successes: number
      factorization_quality_observations: number
      last_reciprocal_pivot_ratio: number
      minimum_reciprocal_pivot_ratio: number
      minimum_absolute_pivot_at_minimum_ratio: number
      maximum_absolute_pivot_at_minimum_ratio: number
      accepted_pivot_count_at_minimum_ratio: number
      factorization_size_at_minimum_ratio: number
      factorization_quality_method: string
      structural_block_solves: number
      largest_linear_system_size: number
      structural_tearing_attempts: number
      structural_tearing_successes: number
      structural_tearing_fallbacks: number
      largest_tearing_inner_system_size: number
      largest_tearing_outer_system_size: number
      largest_tearing_inner_nonzero_count: number
      last_structural_tearing_fallback: string
      failed_structural_block: string
      message: string
    }>
  }
  graph: GraphResult
  thermal_feasibility: ThermalFeasibilitySummary
  reduced_connection_equations: string[]
}

export interface TransientGraphSample {
  time: number
  graph_before_discontinuity: GraphResult | null
  graph: GraphResult
}

export interface TrajectoryValidationResult {
  schema_version: 'thermox.trajectory_validation/v1'
  artifact_id: string
  exact_alignment_count: number
  interpolated_alignment_count: number
  maximum_alignment_gap_si: number
  evidence: {
    schema_version: 'thermox.validation_evidence/v1'
    passed: boolean
    passed_count: number
    failed_count: number
    evidence_classes: Array<{
      basis: string
      passed_count: number
      failed_count: number
    }>
    criteria: Array<{
      criterion_id: string
      observed_value_id: string
      layer: string
      basis: string
      dimension: string
      actual_value_si: number
      reference_value_si: number
      signed_error_si: number
      absolute_error_si: number
      relative_error: number | null
      allowed_absolute_error_si: number
      source_reference: string
      note: string
      passed: boolean
    }>
    limitations: string[]
  }
}

export interface TransientSimulationResult {
  schema_version: 'thermox.result/v6'
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
    factorization_quality_observations: number
    last_reciprocal_pivot_ratio: number
    minimum_reciprocal_pivot_ratio: number
    minimum_absolute_pivot_at_minimum_ratio: number
    maximum_absolute_pivot_at_minimum_ratio: number
    accepted_pivot_count_at_minimum_ratio: number
    factorization_size_at_minimum_ratio: number
    factorization_quality_method: string
    linear_solver_backend: string
    final_time: number
    last_step: number
    last_error_norm: number
    maximum_accepted_error_norm: number
    maximum_error_ratio: number
    limiting_error_variable: string
    maximum_absolute_normalized_residual: number
    limiting_nonlinear_residual: string
    maximum_linear_backward_error: number
    linear_refinement_attempts: number
    linear_refinement_successes: number
    structural_block_solves: number
    largest_linear_system_size: number
    structural_tearing_attempts: number
    structural_tearing_successes: number
    structural_tearing_fallbacks: number
    largest_tearing_inner_system_size: number
    largest_tearing_outer_system_size: number
    largest_tearing_inner_nonzero_count: number
    last_structural_tearing_fallback: string
    message: string
  }
  thermal_feasibility: ThermalFeasibilitySummary
  trajectory: TransientGraphSample[]
  trajectory_validations: TrajectoryValidationResult[]
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

export type EngineeringReviewDisposition =
  | 'approved'
  | 'approved_with_conditions'
  | 'rejected'

export interface PerformanceMapQualityReview {
  schema_version: 'thermox.performance_map_quality_review/v1'
  review_id: string
  project_id: string
  team_id: string
  artifact_revision_id: string
  artifact_checksum: string
  supersedes_review_id: string
  disposition: EngineeringReviewDisposition
  reviewed_scope: string
  rationale: string
  quality_schema_version: 'thermox.performance_map_quality/v1'
  quality_snapshot_checksum: string
  quality_snapshot: PerformanceMapQuality
  created_by_user_id: string
  created_at_epoch_ms: number
}

export interface PerformanceMapQualityReviewList {
  schema_version: 'thermox.performance_map_quality_review_list/v1'
  reviews: PerformanceMapQualityReview[]
}

export interface AssemblyTemplateCatalogEntry {
  source: ArtifactRevision
  definition: TopologyDocument
}

export interface CorrelationArtifactDefinition {
  schema_version: 'thermox.correlation/v2'
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
    flow_regimes: string[]
    fallback_for_unmapped_flow_regime: boolean
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
  output_constraints?: Array<{
    output: string
    minimum?: number
    maximum?: number
    minimum_inclusive: boolean
    maximum_inclusive: boolean
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
  schema_version: 'thermox.project_component_catalog/v2'
  project_id: string
  components: ProjectComponentCatalogEntry[]
}

export interface ExpressionComponentDefinition {
  schema_version: 'thermox.expression_component/v5'
  kind: string
  version: string
  template_kind: string
  display_name: string
  category: string
  model_name: string
  system_boundary_role: string
  supports_steady: boolean
  supports_transient: boolean
  default_mode: string
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
  transient_variables: Array<{
    port_name: string
    variable_name: string
    kind: 'algebraic' | 'differential'
    derivative_scale: number
  }>
  internal_variables: Array<{
    name: string
    kind: 'algebraic' | 'differential'
    initial_value_si: number
    state_scale: number
    initial_derivative_si_s: number
    derivative_scale: number
    lower_bound: number | null
    upper_bound: number | null
    dimension: string
  }>
  transient_equations: Array<{
    name: string
    expression: string
    residual_scale: number
  }>
  modes: Array<{
    name: string
    equations: Array<{
      name: string
      expression: string
      residual_scale: number
    }>
    transient_equations: Array<{
      name: string
      expression: string
      residual_scale: number
    }>
  }>
  events: Array<{
    name: string
    expression: string
    dimension: string
    direction: 'any' | 'rising' | 'falling'
    terminal: boolean
    priority: number
    hysteresis_si: number
    actions: Array<{
      type: 'set_state' | 'set_mode'
      target: string
      expression: string
      mode: string
    }>
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
  port_counts?: Record<string, number>
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

export interface CatalogPortGroup {
  name: string
  port_name_prefix: string
  domain: string
  direction: 'in' | 'out' | 'bidirectional'
  minimum_count: number
  maximum_count: number
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
  supported_modes: string[]
  default_mode: string
  events: Array<{
    name: string
    dimension: string
    direction: 'any' | 'rising' | 'falling'
    terminal: boolean
    priority: number
    hysteresis_si: number
  }>
  ports: CatalogPort[]
  port_groups?: CatalogPortGroup[]
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

export interface CatalogCorrelationTemplate {
  id: string
  version: string
  display_name: string
  category: string
  reference: string
  inputs: Array<{ name: string; dimension: string }>
  output: { name: string; dimension: string }
  coefficients: Array<{
    name: string
    dimension: string
    has_default: boolean
    default_value_si: number | null
    lower_bound: number | null
    upper_bound: number | null
    lower_inclusive: boolean
    upper_inclusive: boolean
  }>
  expression: string
  regime: string
  applicability: Array<{
    input: string
    has_minimum: boolean
    minimum_si: number | null
    has_maximum: boolean
    maximum_si: number | null
    minimum_inclusive: boolean
    maximum_inclusive: boolean
  }>
}

export interface CatalogCorrelationFamilyTemplate {
  id: string
  version: string
  display_name: string
  category: string
  reference: string
  scope: string
  bindings: Array<{
    template_id: string
    coefficients: Record<string, number>
    candidate_id: string
    priority: number
    flow_regimes: string[]
    fallback_for_unmapped_flow_regime: boolean
  }>
}

export interface Catalog {
  schema_version: 'thermox.catalog/v13'
  status: string
  fingerprint: string
  components: CatalogComponent[]
  unit_dimensions: CatalogUnitDimension[]
  property_backends: PropertyBackend[]
  thermochemistry_backends: ThermochemistryBackend[]
  connector_domains: ConnectorDomain[]
  correlation_templates: CatalogCorrelationTemplate[]
  correlation_family_templates: CatalogCorrelationFamilyTemplate[]
  regime_map_templates: CatalogRegimeMapTemplate[]
}

export interface CatalogRegimeMapTemplate {
  id: string
  version: string
  display_name: string
  category: string
  reference: string
  scope: string
  inputs: Array<{ name: string; dimension: string }>
  regions: Array<{
    id: string
    regime: string
    priority: number
    branches: Array<{
      id: string
      priority: number
      criteria: Array<{
        expression: string
        dimension: string
        has_minimum: boolean
        minimum_si: number | null
        has_maximum: boolean
        maximum_si: number | null
        minimum_inclusive: boolean
        maximum_inclusive: boolean
      }>
    }>
  }>
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
