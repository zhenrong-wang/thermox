#pragma once

#include "thermox/service/simulation_runtime.hpp"

#include <cstddef>
#include <limits>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace thermox::service {

inline constexpr char command_schema_v1[] = "thermox.command/v1";
inline constexpr char result_schema_v3[] = "thermox.result/v3";
inline constexpr char error_schema_v1[] = "thermox.error/v1";
inline constexpr char catalog_schema_v5[] = "thermox.catalog/v5";

enum class OperationStatus {
    succeeded,
    invalid_request,
    invalid_model,
    compilation_failed,
    solver_failed,
    result_failed,
};

std::string to_string(OperationStatus status);

enum class DiagnosticSeverity {
    information,
    warning,
    error,
};

std::string to_string(DiagnosticSeverity severity);

enum class ReadinessState {
    not_evaluated,
    blocked,
    ready,
};

std::string to_string(ReadinessState state);

struct ServiceError {
    std::string schema_version{error_schema_v1};
    std::string code;
    std::string stage;
    std::string message;
};

struct Diagnostic {
    std::string code;
    DiagnosticSeverity severity{DiagnosticSeverity::error};
    std::string stage;
    std::string json_path;
    std::string component_id;
    std::string port_name;
    std::string connection_id;
    std::string message;
    std::vector<std::string> suggestions;
};

struct CatalogPortType {
    std::string name;
    std::string domain;
    std::string direction;
    std::size_t maximum_connections{1};
};

struct CatalogParameterType {
    std::string name;
    std::string dimension;
    bool required{true};
    bool has_default{false};
    double default_value_si{0.0};
    double lower_bound{0.0};
    double upper_bound{0.0};
    bool lower_inclusive{true};
    bool upper_inclusive{true};
};

struct CatalogArtifactType {
    std::string role;
    std::string artifact_type;
    bool required{true};
};

struct CatalogInternalVariableType {
    std::string name;
    std::string dimension;
    std::string kind;
};

struct ComponentType {
    std::string kind;
    std::string version;
    std::string template_kind;
    std::string display_name;
    std::string category;
    std::string model_name;
    std::string system_boundary_role;
    std::vector<CatalogPortType> ports;
    std::vector<CatalogParameterType> parameters;
    std::vector<CatalogArtifactType> artifacts;
    std::vector<CatalogInternalVariableType> internal_variables;
    std::vector<std::string> required_property_capabilities;
    std::vector<std::string>
        required_thermochemistry_capabilities;
    bool supports_steady{true};
    bool supports_transient{false};
};

struct PropertyBackendType {
    std::string backend;
    std::string implementation_name;
    std::string implementation_version;
    std::vector<std::string> supported_substances;
    std::vector<std::string> capabilities;
};

struct ThermochemistryBackendType {
    std::string backend;
    std::string implementation_name;
    std::string implementation_version;
    std::vector<std::string> capabilities;
};

struct ConnectorVariableType {
    std::string name;
    std::string dimension;
    double initial_value_si{0.0};
    double scale_si{1.0};
    bool expand_species{false};
};

struct ConnectorDomainType {
    std::string domain;
    std::string contract_version;
    std::string connection_kind;
    std::vector<ConnectorVariableType> variables;
};

struct NativeExtensionType {
    std::string package_id;
    std::string package_version;
};

struct CatalogDisplayUnitType {
    std::string symbol;
    double scale_from_si{1.0};
    double offset_from_si{0.0};
};

struct CatalogAcceptedUnitType {
    std::string symbol;
    std::vector<std::string> aliases;
    double scale_to_si{1.0};
    double offset_to_si{0.0};
};

struct CatalogDimensionUnitType {
    std::string dimension;
    std::string canonical_unit;
    CatalogDisplayUnitType si_display;
    CatalogDisplayUnitType engineering_display;
    std::vector<CatalogAcceptedUnitType> accepted_units;
};

struct CatalogRequest {
    std::string schema_version{command_schema_v1};
};

struct CatalogResponse {
    OperationStatus status{OperationStatus::invalid_request};
    ServiceError error;
    std::string schema_version{catalog_schema_v5};
    std::string fingerprint;
    std::vector<NativeExtensionType> native_extensions;
    std::vector<CatalogDimensionUnitType> unit_dimensions;
    std::vector<ComponentType> components;
    std::vector<PropertyBackendType> property_backends;
    std::vector<ThermochemistryBackendType>
        thermochemistry_backends;
    std::vector<ConnectorDomainType> connector_domains;

    [[nodiscard]] bool succeeded() const {
        return status == OperationStatus::succeeded;
    }
};

struct ModelMetadata {
    std::string schema_version;
    std::string model_id;
    std::string model_revision;
    std::string case_id;
};

struct ComponentProvenance {
    std::string component_id;
    std::string kind;
    std::string requested_version;
    std::string resolved_version;
};

struct MediumProvenance {
    std::string medium_id;
    std::string backend;
    std::string substance;
    std::string package;
    std::string requested_package_version;
    std::string resolved_package_version;
};

struct ConnectorProvenance {
    std::string domain;
    std::string contract_version;
};

struct ArtifactProvenance {
    std::string id;
    std::string artifact_type;
    std::string schema_version;
    std::string revision;
    std::string checksum_sha256;
};

struct SolverSetting {
    std::string name;
    double value{0.0};
};

struct SolverProvenance {
    std::string contract_version;
    std::vector<SolverSetting> settings;
};

struct RevisionProvenance {
    std::string project_id;
    std::string model_revision_id;
    std::string model_checksum;
    std::string case_revision_id;
    std::string case_checksum;
    std::string run_configuration_revision_id;
    std::string run_configuration_checksum;
    std::string study_revision_id;
    std::string study_checksum;
    std::string calibration_revision_id;
    std::string calibration_checksum;
};

struct ExecutionMetadata {
    std::string result_schema_version{result_schema_v3};
    std::string command_schema_version;
    std::string platform_version;
    std::string operation;
    SolverProvenance solver;
    std::string catalog_fingerprint;
    ModelMetadata model;
    std::optional<RevisionProvenance> source_revisions;
    std::vector<ComponentProvenance> components;
    std::vector<MediumProvenance> media;
    std::vector<ArtifactProvenance> artifacts;
    std::vector<ConnectorProvenance> connector_domains;
};

struct MapVariableInput {
    std::string name;
    std::string dimension;
};

struct MapSampleInput {
    double coordinate{0.0};
    std::vector<double> outputs;
};

struct MapCurveInput {
    double family_coordinate{0.0};
    std::vector<MapSampleInput> samples;
};

struct PerformanceMapPayloadInput {
    MapVariableInput primary_variable;
    MapVariableInput family_variable;
    std::vector<MapVariableInput> output_variables;
    std::vector<MapCurveInput> curves;
    std::string primary_extrapolation{"reject"};
    std::string family_extrapolation{"reject"};
};

struct ConditionedMapLayerInput {
    double condition_coordinate{0.0};
    PerformanceMapPayloadInput map;
};

struct PerformanceMapArtifactInput {
    std::string id;
    std::string schema_version;
    std::string revision;
    std::string checksum_sha256;
    std::optional<PerformanceMapPayloadInput> map;
    std::optional<MapVariableInput> condition_variable;
    std::vector<ConditionedMapLayerInput> layers;
    std::string condition_extrapolation{"reject"};
};

struct CorrelationVariableInput {
    std::string name;
    std::string dimension;
};

struct CorrelationApplicabilityRangeInput {
    std::string input;
    std::optional<double> minimum;
    std::optional<double> maximum;
    bool minimum_inclusive{true};
    bool maximum_inclusive{true};
};

struct CorrelationCandidateInput {
    std::string id;
    std::string regime;
    int priority{0};
    std::map<std::string, double> coefficients;
    std::string expression;
    std::vector<CorrelationApplicabilityRangeInput> applicability;
};

struct CorrelationArtifactInput {
    std::string id;
    std::string schema_version;
    std::string revision;
    std::string checksum_sha256;
    std::vector<CorrelationVariableInput> inputs;
    CorrelationVariableInput output;
    std::vector<CorrelationCandidateInput> candidates;
};

struct EngineeringArtifactReference {
    std::string id;
    std::string artifact_type;
    std::string schema_version;
    std::string revision;
    std::string checksum_sha256;
};

struct SimulationArtifactBundle {
    std::vector<PerformanceMapArtifactInput> performance_maps;
    std::vector<CorrelationArtifactInput> correlations;
    std::vector<EngineeringArtifactReference> references;
};

struct ExpressionComponentPortInput {
    std::string name;
    std::string domain;
    std::string direction;
    std::size_t maximum_connections{1};
};

struct ExpressionComponentParameterInput {
    std::string name;
    std::string dimension{"dimensionless"};
    bool required{true};
    std::optional<double> default_value_si;
    double lower_bound{-std::numeric_limits<double>::infinity()};
    double upper_bound{std::numeric_limits<double>::infinity()};
    bool lower_inclusive{true};
    bool upper_inclusive{true};
};

struct ExpressionComponentEquationInput {
    std::string name;
    std::string expression;
    double residual_scale{1.0};
};

struct ExpressionComponentTransientVariableInput {
    std::string port_name;
    std::string variable_name;
    std::string kind{"algebraic"};
    double derivative_scale{1.0};
};

struct ExpressionComponentInternalVariableInput {
    std::string name;
    std::string kind{"algebraic"};
    double initial_value_si{0.0};
    double state_scale{1.0};
    double initial_derivative_si_s{0.0};
    double derivative_scale{1.0};
    double lower_bound{-std::numeric_limits<double>::infinity()};
    double upper_bound{std::numeric_limits<double>::infinity()};
    std::string dimension{"unspecified"};
};

struct ExpressionComponentInput {
    std::string schema_version{
        "thermox.expression_component/v2"};
    std::string kind;
    std::string version;
    std::string template_kind;
    std::string display_name;
    std::string category;
    std::string model_name;
    std::string system_boundary_role;
    bool supports_steady{true};
    bool supports_transient{false};
    std::vector<ExpressionComponentPortInput> ports;
    std::vector<ExpressionComponentParameterInput> parameters;
    std::vector<ExpressionComponentEquationInput> equations;
    std::vector<ExpressionComponentTransientVariableInput>
        transient_variables;
    std::vector<ExpressionComponentInternalVariableInput>
        internal_variables;
    std::vector<ExpressionComponentEquationInput> transient_equations;
};

struct SimulationComponentBundle {
    std::vector<ExpressionComponentInput> expression_components;
};

class EngineeringArtifactResolver {
public:
    virtual ~EngineeringArtifactResolver() = default;

    [[nodiscard]] virtual
    std::optional<PerformanceMapArtifactInput>
    resolve_performance_map(const std::string& artifact_id)
        const = 0;
    [[nodiscard]] virtual std::optional<CorrelationArtifactInput>
    resolve_correlation(const std::string&) const {
        return std::nullopt;
    }
};

struct ResultValue {
    std::string name;
    std::string dimension;
    double value_si{0.0};
    bool has_derivative{false};
    double derivative_si_s{0.0};
};

struct PortResult {
    std::string port_name;
    std::string domain;
    std::string medium_id;
    std::string phase;
    std::vector<ResultValue> primary_values;
    std::vector<ResultValue> derived_values;
};

struct ComponentResult {
    std::string component_id;
    std::string kind;
    std::vector<PortResult> ports;
    std::vector<ResultValue> internal_values;
    std::vector<ResultValue> metrics;
};

struct GraphResult {
    std::vector<ComponentResult> components;
    std::vector<ResultValue> system_balances;
    std::vector<ResultValue> kpis;
};

struct NonlinearDiagnostics {
    bool converged{false};
    int iterations{0};
    double final_residual_norm{0.0};
    double final_step_norm{0.0};
    int function_evaluations{0};
    int jacobian_evaluations{0};
    int linear_solver_evaluations{0};
    int symbolic_factorizations{0};
    int numeric_factorizations{0};
    std::string linear_solver_backend;
    std::string message;
};

struct ContinuationStageDiagnostics {
    double start_parameter{0.0};
    double target_parameter{0.0};
    bool accepted{false};
    int nonlinear_iterations{0};
    double final_residual_norm{0.0};
    std::string message;
};

struct ContinuationRunDiagnostics {
    bool enabled{false};
    bool converged{false};
    bool used_informed_path{false};
    double reached_parameter{0.0};
    int accepted_stages{0};
    int rejected_stages{0};
    std::string message;
    std::vector<ContinuationStageDiagnostics> stages;
};

struct TimeIntegrationDiagnostics {
    bool success{false};
    int accepted_steps{0};
    int rejected_steps{0};
    int maximum_order_used{0};
    int nonlinear_solves{0};
    int nonlinear_iterations{0};
    int symbolic_factorizations{0};
    int numeric_factorizations{0};
    std::string linear_solver_backend;
    double final_time{0.0};
    double last_step{0.0};
    std::string message;
};

struct StateSample {
    double time{0.0};
    GraphResult graph;
};

struct EventValue {
    std::string name;
    double time{0.0};
    GraphResult graph;
    bool terminal{false};
};

struct ValidateModelRequest {
    std::string schema_version{command_schema_v1};
    std::string model_json;
    std::string case_id;
    SimulationArtifactBundle artifacts;
    SimulationComponentBundle components;
};

struct CompilationSummary {
    bool compiled{false};
    std::string mode;
    std::size_t variable_count{0};
    std::size_t equation_count{0};
    std::vector<std::string> reduced_connection_equations;
    std::string catalog_fingerprint;
};

struct ReadinessLayer {
    std::string id;
    ReadinessState state{ReadinessState::not_evaluated};
    std::vector<std::string> diagnostic_codes;
};

struct EntityReadiness {
    std::string entity_type;
    std::string entity_id;
    ReadinessState state{ReadinessState::not_evaluated};
    std::vector<std::string> diagnostic_codes;
};

struct ReadinessSummary {
    bool calculatable{false};
    std::vector<ReadinessLayer> layers{
        {"draft", ReadinessState::not_evaluated, {}},
        {"physical", ReadinessState::not_evaluated, {}},
        {"topology", ReadinessState::not_evaluated, {}},
        {"study", ReadinessState::not_evaluated, {}},
        {"compilation", ReadinessState::not_evaluated, {}},
        {"execution", ReadinessState::not_evaluated, {}},
    };
    std::vector<EntityReadiness> entities;
};

struct ValidateModelResponse {
    OperationStatus status{OperationStatus::invalid_request};
    ServiceError error;
    ModelMetadata model;
    std::string canonical_model_json;
    CompilationSummary compilation;
    ReadinessSummary readiness;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const {
        return status == OperationStatus::succeeded;
    }
};

struct SteadySolverSettings {
    int max_iterations{50};
    double residual_tolerance{1.0e-10};
    double step_tolerance{1.0e-10};
    double finite_difference_epsilon{1.0e-6};
    double min_damping{1.0e-6};
    double damping_reduction{0.5};
    double sufficient_decrease{1.0e-4};
    int max_line_search_steps{50};
    bool continuation_enabled{false};
    double continuation_initial_step{0.25};
    double continuation_minimum_step{1.0 / 64.0};
    double continuation_step_growth{1.5};
    double continuation_step_reduction{0.5};
    int continuation_maximum_stages{100};
};

struct SteadySimulationRequest {
    std::string schema_version{command_schema_v1};
    std::string model_json;
    std::string case_id;
    SteadySolverSettings solver;
    SimulationArtifactBundle artifacts;
    SimulationComponentBundle components;
};

struct SteadySimulationResponse {
    OperationStatus status{OperationStatus::invalid_request};
    ServiceError error;
    ExecutionMetadata metadata;
    NonlinearDiagnostics diagnostics;
    ContinuationRunDiagnostics continuation;
    GraphResult graph;
    std::vector<std::string> reduced_connection_equations;

    [[nodiscard]] bool succeeded() const {
        return status == OperationStatus::succeeded;
    }
};

struct CalibrationSolverSettings {
    int max_iterations{20};
    double initial_step_fraction{0.1};
    double minimum_step_fraction{1.0e-4};
    double step_reduction{0.5};
    double minimum_continuation_fraction{1.0 / 64.0};
    double continuation_growth{1.5};
    SteadySolverSettings simulation_solver;
};

struct CalibrationRequest {
    std::string schema_version{command_schema_v1};
    std::string model_json;
    std::string calibration_id;
    CalibrationSolverSettings solver;
    SimulationArtifactBundle artifacts;
    SimulationComponentBundle components;
};

struct CalibrationParameterEstimate {
    std::string id;
    std::string scope;
    std::string dimension;
    double initial_value_si{0.0};
    double fitted_value_si{0.0};
    double lower_bound_si{0.0};
    double upper_bound_si{0.0};
    std::vector<std::string> targets;
};

struct CalibrationObservationResidual {
    std::string id;
    std::string case_id;
    std::string target;
    std::string dimension;
    double measured_si{0.0};
    double predicted_si{0.0};
    double sigma_si{0.0};
    double residual_si{0.0};
    double normalized_residual{0.0};
};

struct CalibrationDiagnostics {
    bool converged{false};
    int iterations{0};
    int objective_evaluations{0};
    double initial_objective{0.0};
    double final_objective{0.0};
    std::string message;
};

struct CalibrationResponse {
    OperationStatus status{OperationStatus::invalid_request};
    ServiceError error;
    ExecutionMetadata metadata;
    std::string calibration_id;
    CalibrationDiagnostics diagnostics;
    std::vector<CalibrationParameterEstimate> parameters;
    std::vector<CalibrationObservationResidual> observations;
    std::string fitted_model_json;

    [[nodiscard]] bool succeeded() const {
        return status == OperationStatus::succeeded;
    }
};

struct StudyObservation {
    std::string id;
    std::string target;
    std::string dimension;
    double measured_si{0.0};
    double sigma_si{0.0};
};

struct StudyPredictionCase {
    std::string case_id;
    std::vector<StudyObservation> observations;
};

struct EngineeringStudyRequest {
    std::string schema_version{command_schema_v1};
    std::string model_json;
    std::string calibration_id;
    CalibrationSolverSettings calibration_solver;
    SteadySolverSettings prediction_solver;
    std::vector<StudyPredictionCase> prediction_cases;
    SimulationArtifactBundle artifacts;
    SimulationComponentBundle components;
};

struct StudyCaseResult {
    std::string case_id;
    SteadySimulationResponse simulation;
    std::vector<CalibrationObservationResidual> observations;
    double weighted_sum_squares{0.0};
};

struct EngineeringStudyDiagnostics {
    std::size_t prediction_case_count{0};
    std::size_t observation_count{0};
    double weighted_sum_squares{0.0};
    double rms_normalized_residual{0.0};
    double maximum_absolute_normalized_residual{0.0};
};

struct EngineeringStudyResponse {
    OperationStatus status{OperationStatus::invalid_request};
    ServiceError error;
    CalibrationResponse calibration;
    std::vector<StudyCaseResult> predictions;
    EngineeringStudyDiagnostics diagnostics;

    [[nodiscard]] bool succeeded() const {
        return status == OperationStatus::succeeded;
    }
};

struct TransientSolverSettings {
    double start_time{0.0};
    double end_time{1.0};
    double initial_step{1.0e-3};
    double min_step{1.0e-9};
    double max_step{0.1};
    double absolute_tolerance{1.0e-7};
    double relative_tolerance{1.0e-5};
    int max_steps{100000};
    int max_consecutive_rejections{20};
    int maximum_order{2};
    bool compute_consistent_initial_conditions{true};
    SteadySolverSettings nonlinear_solver = [] {
        SteadySolverSettings settings;
        settings.residual_tolerance = 1.0e-8;
        return settings;
    }();
};

struct TransientSimulationRequest {
    std::string schema_version{command_schema_v1};
    std::string model_json;
    std::string case_id;
    TransientSolverSettings solver;
    SimulationArtifactBundle artifacts;
    SimulationComponentBundle components;
};

struct TransientSimulationResponse {
    OperationStatus status{OperationStatus::invalid_request};
    ServiceError error;
    ExecutionMetadata metadata;
    TimeIntegrationDiagnostics diagnostics;
    std::vector<StateSample> trajectory;
    std::vector<EventValue> events;

    [[nodiscard]] bool succeeded() const {
        return status == OperationStatus::succeeded;
    }
};

class SimulationService {
public:
    SimulationService();
    explicit SimulationService(
        std::shared_ptr<const SimulationRuntime> runtime);
    SimulationService(
        std::shared_ptr<const SimulationRuntime> runtime,
        std::shared_ptr<const EngineeringArtifactResolver>
            artifact_resolver);
    ~SimulationService();
    SimulationService(SimulationService&&) noexcept;
    SimulationService& operator=(SimulationService&&) noexcept;
    SimulationService(const SimulationService&) = delete;
    SimulationService& operator=(const SimulationService&) = delete;

    [[nodiscard]] ValidateModelResponse validate_model(
        const ValidateModelRequest& request) const;
    [[nodiscard]] CatalogResponse get_catalog(
        const CatalogRequest& request = {}) const;
    [[nodiscard]] CatalogResponse get_catalog(
        const SimulationComponentBundle& components,
        const CatalogRequest& request = {}) const;
    [[nodiscard]] SteadySimulationResponse run_steady(
        const SteadySimulationRequest& request) const;
    [[nodiscard]] CalibrationResponse run_calibration(
        const CalibrationRequest& request) const;
    [[nodiscard]] EngineeringStudyResponse run_engineering_study(
        const EngineeringStudyRequest& request) const;
    [[nodiscard]] TransientSimulationResponse run_transient(
        const TransientSimulationRequest& request) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace thermox::service
