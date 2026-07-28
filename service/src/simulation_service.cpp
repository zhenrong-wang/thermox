#include "thermox/service/simulation_service.hpp"

#include "serialization_internal.hpp"

#include "thermox/nonlinear_solver.hpp"
#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/model_document.hpp"
#include "thermox/platform/results.hpp"
#include "thermox/physics/property_registry.hpp"
#include "thermox/transient_solver.hpp"

#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace thermox::service {

namespace {

ServiceError make_error(
    std::string code,
    std::string stage,
    std::string message) {
    return {
        error_schema_v1,
        std::move(code),
        std::move(stage),
        std::move(message),
    };
}

bool valid_schema(const std::string& schema) {
    return schema == command_schema_v1;
}

void validate_settings(const SteadySolverSettings& settings) {
    if (settings.max_iterations <= 0 ||
        settings.max_line_search_steps <= 0 ||
        !std::isfinite(settings.residual_tolerance) ||
        settings.residual_tolerance <= 0.0 ||
        !std::isfinite(settings.step_tolerance) ||
        settings.step_tolerance <= 0.0 ||
        !std::isfinite(settings.finite_difference_epsilon) ||
        settings.finite_difference_epsilon <= 0.0 ||
        !std::isfinite(settings.min_damping) ||
        settings.min_damping <= 0.0 ||
        !std::isfinite(settings.damping_reduction) ||
        settings.damping_reduction <= 0.0 ||
        settings.damping_reduction >= 1.0 ||
        !std::isfinite(settings.sufficient_decrease) ||
        settings.sufficient_decrease <= 0.0) {
        throw std::invalid_argument("invalid steady solver settings");
    }
}

SolverOptions to_core(const SteadySolverSettings& settings) {
    validate_settings(settings);
    SolverOptions options;
    options.max_iterations = settings.max_iterations;
    options.residual_tolerance = settings.residual_tolerance;
    options.step_tolerance = settings.step_tolerance;
    options.finite_difference_epsilon =
        settings.finite_difference_epsilon;
    options.min_damping = settings.min_damping;
    options.damping_reduction = settings.damping_reduction;
    options.sufficient_decrease = settings.sufficient_decrease;
    options.max_line_search_steps = settings.max_line_search_steps;
    return options;
}

TimeIntegrationOptions to_core(
    const TransientSolverSettings& settings) {
    if (!std::isfinite(settings.start_time) ||
        !std::isfinite(settings.end_time) ||
        settings.end_time <= settings.start_time ||
        !std::isfinite(settings.initial_step) ||
        settings.initial_step <= 0.0 ||
        !std::isfinite(settings.min_step) ||
        settings.min_step <= 0.0 ||
        !std::isfinite(settings.max_step) ||
        settings.max_step < settings.min_step ||
        !std::isfinite(settings.absolute_tolerance) ||
        settings.absolute_tolerance <= 0.0 ||
        !std::isfinite(settings.relative_tolerance) ||
        settings.relative_tolerance <= 0.0 ||
        settings.max_steps <= 0 ||
        settings.max_consecutive_rejections <= 0) {
        throw std::invalid_argument("invalid transient solver settings");
    }
    TimeIntegrationOptions options;
    options.start_time = settings.start_time;
    options.end_time = settings.end_time;
    options.initial_step = settings.initial_step;
    options.min_step = settings.min_step;
    options.max_step = settings.max_step;
    options.absolute_tolerance = settings.absolute_tolerance;
    options.relative_tolerance = settings.relative_tolerance;
    options.max_steps = settings.max_steps;
    options.max_consecutive_rejections =
        settings.max_consecutive_rejections;
    options.compute_consistent_initial_conditions =
        settings.compute_consistent_initial_conditions;
    options.nonlinear_options = to_core(settings.nonlinear_solver);
    return options;
}

ModelMetadata model_metadata(
    const platform::ModelDocument& document,
    std::string case_id = {}) {
    return {
        document.schema_version,
        document.model_id,
        document.revision,
        std::move(case_id),
    };
}

ExecutionMetadata execution_metadata(
    const platform::ModelDocument& document,
    const std::string& command_schema,
    const std::string& case_id,
    std::string operation,
    std::string solver_contract,
    const platform::ComponentRegistry& components,
    const physics::PropertyPackageRegistry& properties) {
    ExecutionMetadata metadata;
    metadata.command_schema_version = command_schema;
    metadata.operation = std::move(operation);
    metadata.solver_contract = std::move(solver_contract);
    metadata.model = model_metadata(document, case_id);
    for (const auto& component : document.components) {
        metadata.components.push_back({
            component.id,
            component.kind,
            components.require_model(component.kind).descriptor().version,
        });
    }
    for (const auto& medium : document.media) {
        metadata.media.push_back({
            medium.id,
            medium.backend,
            medium.substance,
            std::string(
                properties.create(
                    medium.backend, medium.substance)->name()),
        });
    }
    return metadata;
}

NonlinearDiagnostics copy_diagnostics(
    const SolverDiagnostics& source) {
    return {
        source.converged,
        source.iterations,
        source.final_residual_norm,
        source.final_step_norm,
        source.function_evaluations,
        source.jacobian_evaluations,
        source.linear_solver_evaluations,
        source.message,
    };
}

TimeIntegrationDiagnostics copy_diagnostics(
    const thermox::TimeIntegrationDiagnostics& source) {
    return {
        source.success,
        source.accepted_steps,
        source.rejected_steps,
        source.nonlinear_solves,
        source.nonlinear_iterations,
        source.final_time,
        source.last_step,
        source.message,
    };
}

}  // namespace

struct SimulationService::Impl {
    platform::ComponentRegistry components{
        platform::make_default_component_registry()};
    physics::PropertyPackageRegistry properties{
        physics::make_default_property_package_registry()};
};

std::string to_string(OperationStatus status) {
    switch (status) {
        case OperationStatus::succeeded: return "succeeded";
        case OperationStatus::invalid_request: return "invalid_request";
        case OperationStatus::invalid_model: return "invalid_model";
        case OperationStatus::compilation_failed:
            return "compilation_failed";
        case OperationStatus::solver_failed: return "solver_failed";
        case OperationStatus::result_failed: return "result_failed";
    }
    return "unknown";
}

SimulationService::SimulationService()
    : impl_(std::make_unique<Impl>()) {}

SimulationService::~SimulationService() = default;
SimulationService::SimulationService(SimulationService&&) noexcept =
    default;
SimulationService& SimulationService::operator=(
    SimulationService&&) noexcept = default;

ValidateModelResponse SimulationService::validate_model(
    const ValidateModelRequest& request) const {
    ValidateModelResponse response;
    if (!valid_schema(request.schema_version)) {
        response.error = make_error(
            "unsupported_command_schema",
            "request",
            "unsupported command schema_version: " +
                request.schema_version);
        return response;
    }
    if (request.model_json.empty()) {
        response.error = make_error(
            "missing_model", "request", "model_json must not be empty");
        return response;
    }
    try {
        const auto document =
            platform::parse_model_document_text(request.model_json);
        response.model = model_metadata(document);
        response.canonical_model_json =
            detail::serialize_model_document_json(document);
        response.status = OperationStatus::succeeded;
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_model;
        response.error = make_error(
            "invalid_model", "validation", ex.what());
    }
    return response;
}

SteadySimulationResponse SimulationService::run_steady(
    const SteadySimulationRequest& request) const {
    SteadySimulationResponse response;
    if (!valid_schema(request.schema_version)) {
        response.error = make_error(
            "unsupported_command_schema",
            "request",
            "unsupported command schema_version: " +
                request.schema_version);
        return response;
    }
    if (request.model_json.empty()) {
        response.error = make_error(
            "missing_model", "request", "model_json must not be empty");
        return response;
    }

    platform::ModelDocument document;
    SolverOptions options;
    try {
        options = to_core(request.solver);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_request;
        response.error = make_error(
            "invalid_solver_settings", "request", ex.what());
        return response;
    }
    try {
        document =
            platform::parse_model_document_text(request.model_json);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_model;
        response.error = make_error(
            "invalid_model", "validation", ex.what());
        return response;
    }

    platform::CompiledModelGraph graph;
    try {
        graph = platform::compile_model_graph(
            document,
            impl_->components,
            impl_->properties,
            request.case_id);
        response.metadata = execution_metadata(
            document,
            request.schema_version,
            graph.case_id.value_or(""),
            "steady",
            "thermox.newton/v1",
            impl_->components,
            impl_->properties);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::compilation_failed;
        response.error = make_error(
            "compilation_failed", "compilation", ex.what());
        return response;
    }

    NonlinearSolveResult result;
    try {
        result = solve_newton(graph.problem, options);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::solver_failed;
        response.error = make_error(
            "nonlinear_solver_exception", "solve", ex.what());
        return response;
    }
    response.diagnostics = copy_diagnostics(result.diagnostics);
    for (std::size_t index = 0;
         index < graph.problem.variable_names.size() &&
         index < result.x.size();
         ++index) {
        response.variables.push_back(
            {graph.problem.variable_names[index], result.x[index]});
    }
    response.reduced_connection_equations =
        graph.reduced_connection_equations;

    if (!result.diagnostics.converged) {
        response.status = OperationStatus::solver_failed;
        response.error = make_error(
            "nonlinear_solver_failed",
            "solve",
            result.diagnostics.message);
        return response;
    }

    try {
        const auto ports = platform::evaluate_fluid_port_results(
            document, graph, result.x, impl_->properties);
        for (const auto& port : ports) {
            response.fluid_ports.push_back({
                port.component_id,
                port.port_name,
                port.medium_id,
                port.mass_flow_kg_s,
                port.state.pressure_pa,
                port.state.temperature_k,
                port.state.density_kg_m3,
                port.state.enthalpy_j_kg,
                port.state.entropy_j_kg_k,
                port.state.vapor_quality,
            });
        }
    } catch (const std::exception& ex) {
        response.status = OperationStatus::result_failed;
        response.error = make_error(
            "result_evaluation_failed", "result", ex.what());
        return response;
    }

    response.status = OperationStatus::succeeded;
    return response;
}

TransientSimulationResponse SimulationService::run_transient(
    const TransientSimulationRequest& request) const {
    TransientSimulationResponse response;
    if (!valid_schema(request.schema_version)) {
        response.error = make_error(
            "unsupported_command_schema",
            "request",
            "unsupported command schema_version: " +
                request.schema_version);
        return response;
    }
    if (request.model_json.empty()) {
        response.error = make_error(
            "missing_model", "request", "model_json must not be empty");
        return response;
    }

    platform::ModelDocument document;
    TimeIntegrationOptions options;
    try {
        options = to_core(request.solver);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_request;
        response.error = make_error(
            "invalid_solver_settings", "request", ex.what());
        return response;
    }
    try {
        document =
            platform::parse_model_document_text(request.model_json);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_model;
        response.error = make_error(
            "invalid_model", "validation", ex.what());
        return response;
    }

    platform::CompiledTransientModelGraph graph;
    try {
        graph = platform::compile_transient_model_graph(
            document,
            impl_->components,
            impl_->properties,
            request.case_id);
        response.metadata = execution_metadata(
            document,
            request.schema_version,
            graph.case_id.value_or(""),
            "transient",
            "thermox.dae-bdf1/v1",
            impl_->components,
            impl_->properties);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::compilation_failed;
        response.error = make_error(
            "compilation_failed", "compilation", ex.what());
        return response;
    }

    DaeSolveResult result;
    try {
        result = integrate_dae(graph.problem, options);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::solver_failed;
        response.error = make_error(
            "transient_solver_exception", "solve", ex.what());
        return response;
    }
    response.diagnostics = copy_diagnostics(result.diagnostics);
    response.variable_names = graph.problem.variable_names;
    for (const auto& sample : result.trajectory) {
        response.trajectory.push_back(
            {sample.time, sample.state, sample.derivative});
    }
    for (const auto& event : result.events) {
        response.events.push_back(
            {event.name, event.time, event.state, event.terminal});
    }
    if (!result.diagnostics.success) {
        response.status = OperationStatus::solver_failed;
        response.error = make_error(
            "transient_solver_failed",
            "solve",
            result.diagnostics.message);
        return response;
    }

    response.status = OperationStatus::succeeded;
    return response;
}

}  // namespace thermox::service
