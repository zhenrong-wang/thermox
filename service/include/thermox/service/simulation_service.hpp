#pragma once

#include "thermox/service/simulation_runtime.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace thermox::service {

inline constexpr char command_schema_v1[] = "thermox.command/v1";
inline constexpr char result_schema_v2[] = "thermox.result/v2";
inline constexpr char error_schema_v1[] = "thermox.error/v1";
inline constexpr char catalog_schema_v1[] = "thermox.catalog/v1";

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

struct ComponentType {
    std::string kind;
    std::string version;
    std::vector<CatalogPortType> ports;
    std::vector<CatalogParameterType> parameters;
    std::vector<std::string> required_property_capabilities;
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

struct ConnectorVariableType {
    std::string name;
    std::string dimension;
};

struct ConnectorDomainType {
    std::string domain;
    std::string contract_version;
    std::vector<ConnectorVariableType> variables;
};

struct CatalogRequest {
    std::string schema_version{command_schema_v1};
};

struct CatalogResponse {
    OperationStatus status{OperationStatus::invalid_request};
    ServiceError error;
    std::string schema_version{catalog_schema_v1};
    std::string fingerprint;
    std::vector<ComponentType> components;
    std::vector<PropertyBackendType> property_backends;
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

struct SolverSetting {
    std::string name;
    double value{0.0};
};

struct SolverProvenance {
    std::string contract_version;
    std::vector<SolverSetting> settings;
};

struct ExecutionMetadata {
    std::string result_schema_version{result_schema_v2};
    std::string command_schema_version;
    std::string platform_version;
    std::string operation;
    SolverProvenance solver;
    std::string catalog_fingerprint;
    ModelMetadata model;
    std::vector<ComponentProvenance> components;
    std::vector<MediumProvenance> media;
    std::vector<ConnectorProvenance> connector_domains;
};

struct VariableValue {
    std::string name;
    double value_si{0.0};
};

struct FluidPortValue {
    std::string component_id;
    std::string port_name;
    std::string medium_id;
    double mass_flow_kg_s{0.0};
    double pressure_pa{0.0};
    double temperature_k{0.0};
    double density_kg_m3{0.0};
    double enthalpy_j_kg{0.0};
    double entropy_j_kg_k{0.0};
    double vapor_quality{0.0};
};

struct NonlinearDiagnostics {
    bool converged{false};
    int iterations{0};
    double final_residual_norm{0.0};
    double final_step_norm{0.0};
    int function_evaluations{0};
    int jacobian_evaluations{0};
    int linear_solver_evaluations{0};
    std::string message;
};

struct TimeIntegrationDiagnostics {
    bool success{false};
    int accepted_steps{0};
    int rejected_steps{0};
    int nonlinear_solves{0};
    int nonlinear_iterations{0};
    double final_time{0.0};
    double last_step{0.0};
    std::string message;
};

struct StateSample {
    double time{0.0};
    std::vector<double> state;
    std::vector<double> derivative;
};

struct EventValue {
    std::string name;
    double time{0.0};
    std::vector<double> state;
    bool terminal{false};
};

struct ValidateModelRequest {
    std::string schema_version{command_schema_v1};
    std::string model_json;
    std::string case_id;
};

struct CompilationSummary {
    bool compiled{false};
    std::string mode;
    std::size_t variable_count{0};
    std::size_t equation_count{0};
    std::vector<std::string> reduced_connection_equations;
    std::string catalog_fingerprint;
};

struct ValidateModelResponse {
    OperationStatus status{OperationStatus::invalid_request};
    ServiceError error;
    ModelMetadata model;
    std::string canonical_model_json;
    CompilationSummary compilation;
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
};

struct SteadySimulationRequest {
    std::string schema_version{command_schema_v1};
    std::string model_json;
    std::string case_id;
    SteadySolverSettings solver;
};

struct SteadySimulationResponse {
    OperationStatus status{OperationStatus::invalid_request};
    ServiceError error;
    ExecutionMetadata metadata;
    NonlinearDiagnostics diagnostics;
    std::vector<VariableValue> variables;
    std::vector<FluidPortValue> fluid_ports;
    std::vector<std::string> reduced_connection_equations;

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
    bool compute_consistent_initial_conditions{true};
    SteadySolverSettings nonlinear_solver;
};

struct TransientSimulationRequest {
    std::string schema_version{command_schema_v1};
    std::string model_json;
    std::string case_id;
    TransientSolverSettings solver;
};

struct TransientSimulationResponse {
    OperationStatus status{OperationStatus::invalid_request};
    ServiceError error;
    ExecutionMetadata metadata;
    TimeIntegrationDiagnostics diagnostics;
    std::vector<std::string> variable_names;
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
    ~SimulationService();
    SimulationService(SimulationService&&) noexcept;
    SimulationService& operator=(SimulationService&&) noexcept;
    SimulationService(const SimulationService&) = delete;
    SimulationService& operator=(const SimulationService&) = delete;

    [[nodiscard]] ValidateModelResponse validate_model(
        const ValidateModelRequest& request) const;
    [[nodiscard]] CatalogResponse get_catalog(
        const CatalogRequest& request = {}) const;
    [[nodiscard]] SteadySimulationResponse run_steady(
        const SteadySimulationRequest& request) const;
    [[nodiscard]] TransientSimulationResponse run_transient(
        const TransientSimulationRequest& request) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace thermox::service
