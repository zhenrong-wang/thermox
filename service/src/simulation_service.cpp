#include "thermox/service/simulation_service.hpp"

#include "serialization_internal.hpp"
#include "runtime_internal.hpp"

#include "thermox/nonlinear_solver.hpp"
#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/model_document.hpp"
#include "thermox/platform/results.hpp"
#include "thermox/physics/property_registry.hpp"
#include "thermox/transient_solver.hpp"

#include <cmath>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string_view>
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

std::string_view capability_name(
    physics::PropertyCapability capability) {
    switch (capability) {
        case physics::PropertyCapability::state_pt:
            return "state_pt";
        case physics::PropertyCapability::state_ph:
            return "state_ph";
        case physics::PropertyCapability::state_ps:
            return "state_ps";
        case physics::PropertyCapability::saturation_p:
            return "saturation_p";
        case physics::PropertyCapability::transport:
            return "transport";
    }
    return "unknown";
}

Diagnostic compilation_diagnostic(const std::string& message) {
    Diagnostic diagnostic;
    diagnostic.stage = "compilation";
    diagnostic.message = message;
    diagnostic.code = "model_compilation_failed";
    diagnostic.suggestions = {
        "Review the referenced component, connection, medium, and active case."};
    if (message.find("no component model registered") !=
        std::string::npos) {
        diagnostic.code = "unknown_component_type";
        diagnostic.suggestions = {
            "Select a component kind from the active runtime catalog."};
    } else if (message.find("missing required parameter") !=
               std::string::npos) {
        diagnostic.code = "missing_required_parameter";
        diagnostic.suggestions = {
            "Provide every required parameter listed by the component type."};
    } else if (message.find("supplies unknown parameter") !=
               std::string::npos) {
        diagnostic.code = "unknown_component_parameter";
        diagnostic.suggestions = {
            "Remove the parameter or select it from the component type catalog."};
    } else if (message.find(
                   "requests property package version") !=
               std::string::npos) {
        diagnostic.code = "property_package_version_mismatch";
        diagnostic.suggestions = {
            "Use the property package version resolved by the active runtime catalog."};
    } else if (message.find(
                   "requests connector contract version") !=
               std::string::npos) {
        diagnostic.code = "connector_contract_version_mismatch";
        diagnostic.suggestions = {
            "Use the connector contract version resolved by the active runtime catalog."};
    } else if (message.find("requests version") !=
               std::string::npos) {
        diagnostic.code = "component_version_mismatch";
        diagnostic.suggestions = {
            "Use the component version resolved by the active runtime catalog."};
    } else if (message.find("declares unknown port") !=
                   std::string::npos ||
               message.find("binds a medium to unknown port") !=
                   std::string::npos ||
               message.find("unknown port during graph compilation") !=
                   std::string::npos) {
        diagnostic.code = "unknown_component_port";
        diagnostic.suggestions = {
            "Derive component ports from the active component type catalog."};
    } else if (message.find("missing medium binding") !=
               std::string::npos) {
        diagnostic.code = "missing_medium_binding";
        diagnostic.suggestions = {
            "Bind every fluid port to a medium declared by the model."};
    } else if (message.find("kind '") != std::string::npos &&
               message.find("incompatible with domain") !=
                   std::string::npos) {
        diagnostic.code = "incompatible_connection_kind";
        diagnostic.suggestions = {
            "Use the link contract declared for the connector domain."};
    } else if (message.find("maximum connection count") !=
               std::string::npos) {
        diagnostic.code = "port_cardinality_exceeded";
        diagnostic.suggestions = {
            "Insert an explicit mixer, splitter, junction, or distribution component."};
    } else if (message.find("under-specified") !=
               std::string::npos) {
        diagnostic.code = "under_specified_model";
        diagnostic.suggestions = {
            "Add independent case specifications or component equations."};
    } else if (message.find("over-specified") !=
               std::string::npos) {
        diagnostic.code = "over_specified_model";
        diagnostic.suggestions = {
            "Remove conflicting or redundant user specifications."};
    } else if (message.find("property capability") !=
               std::string::npos) {
        diagnostic.code = "unsupported_property_capability";
        diagnostic.suggestions = {
            "Choose a property backend that supplies the required capability."};
    } else if (message.find("property package registered") !=
               std::string::npos) {
        diagnostic.code = "unknown_property_backend";
        diagnostic.suggestions = {
            "Select a property backend from the active runtime catalog."};
    } else if (message.find("does not support") !=
               std::string::npos ||
               message.find("must use dynamic") != std::string::npos) {
        diagnostic.code = "unsupported_simulation_mode";
        diagnostic.suggestions = {
            "Choose a case mode supported by every component in the graph."};
    }
    return diagnostic;
}

const platform::CaseDefinition* selected_case(
    const platform::ModelDocument& document,
    const std::string& case_id) {
    if (case_id.empty()) {
        return document.cases.empty() ? nullptr : &document.cases.front();
    }
    for (const auto& simulation_case : document.cases) {
        if (simulation_case.id == case_id) {
            return &simulation_case;
        }
    }
    return nullptr;
}

bool transient_case(const platform::CaseDefinition* simulation_case) {
    return simulation_case != nullptr &&
        (simulation_case->mode == "dynamic_initialization" ||
         simulation_case->mode == "dynamic_transient");
}

std::string validation_mode(
    const platform::CaseDefinition* simulation_case) {
    if (simulation_case == nullptr ||
        simulation_case->mode == "steady_state_design" ||
        simulation_case->mode == "steady_state_off_design") {
        return "steady";
    }
    if (transient_case(simulation_case)) {
        return "transient";
    }
    throw std::invalid_argument(
        "unsupported simulation case mode: " +
        simulation_case->mode);
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
    SolverProvenance solver,
    const std::string& catalog_fingerprint,
    const platform::ComponentRegistry& components,
    const physics::PropertyPackageRegistry& properties) {
    ExecutionMetadata metadata;
    metadata.command_schema_version = command_schema;
    metadata.platform_version = THERMOX_PLATFORM_VERSION;
    metadata.operation = std::move(operation);
    metadata.solver = std::move(solver);
    metadata.catalog_fingerprint = catalog_fingerprint;
    metadata.model = model_metadata(document, case_id);
    for (const auto& component : document.components) {
        metadata.components.push_back({
            component.id,
            component.kind,
            component.version,
            components.require_model(component.kind).descriptor().version,
        });
    }
    for (const auto& medium : document.media) {
        const auto package = properties.create(
            medium.backend, medium.substance);
        metadata.media.push_back({
            medium.id,
            medium.backend,
            medium.substance,
            std::string(package->name()),
            medium.package_version,
            std::string(package->version()),
        });
    }
    metadata.connector_domains = {
        {"fluid", "thermox.connector.fluid/v1"},
        {"heat", "thermox.connector.heat/v1"},
        {"shaft", "thermox.connector.shaft/v1"},
        {"signal", "thermox.connector.signal/v1"},
        {"control", "thermox.connector.control/v1"},
    };
    return metadata;
}

SolverProvenance solver_provenance(
    const SteadySolverSettings& settings) {
    return {
        "thermox.newton/v1",
        {
            {"max_iterations",
             static_cast<double>(settings.max_iterations)},
            {"residual_tolerance", settings.residual_tolerance},
            {"step_tolerance", settings.step_tolerance},
            {"finite_difference_epsilon",
             settings.finite_difference_epsilon},
            {"min_damping", settings.min_damping},
            {"damping_reduction", settings.damping_reduction},
            {"sufficient_decrease", settings.sufficient_decrease},
            {"max_line_search_steps",
             static_cast<double>(
                 settings.max_line_search_steps)},
        },
    };
}

SolverProvenance solver_provenance(
    const TransientSolverSettings& settings) {
    auto provenance = SolverProvenance{
        "thermox.dae-bdf1/v1",
        {
            {"start_time", settings.start_time},
            {"end_time", settings.end_time},
            {"initial_step", settings.initial_step},
            {"min_step", settings.min_step},
            {"max_step", settings.max_step},
            {"absolute_tolerance", settings.absolute_tolerance},
            {"relative_tolerance", settings.relative_tolerance},
            {"max_steps", static_cast<double>(settings.max_steps)},
            {"max_consecutive_rejections",
             static_cast<double>(
                 settings.max_consecutive_rejections)},
            {"compute_consistent_initial_conditions",
             settings.compute_consistent_initial_conditions
                 ? 1.0
                 : 0.0},
        },
    };
    const auto nonlinear =
        solver_provenance(settings.nonlinear_solver);
    for (const auto& setting : nonlinear.settings) {
        provenance.settings.push_back({
            "nonlinear." + setting.name,
            setting.value,
        });
    }
    return provenance;
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

ResultValue copy_result_value(
    const platform::ResultValue& source) {
    return {
        source.name,
        source.dimension,
        source.value_si,
        source.has_derivative,
        source.derivative_si_s,
    };
}

std::vector<ResultValue> copy_result_values(
    const std::vector<platform::ResultValue>& source) {
    std::vector<ResultValue> values;
    values.reserve(source.size());
    for (const auto& value : source) {
        values.push_back(copy_result_value(value));
    }
    return values;
}

GraphResult copy_graph_result(
    const platform::GraphResult& source) {
    GraphResult graph;
    graph.system_balances =
        copy_result_values(source.system_balances);
    graph.kpis = copy_result_values(source.kpis);
    graph.components.reserve(source.components.size());
    for (const auto& source_component : source.components) {
        ComponentResult component;
        component.component_id =
            source_component.component_id;
        component.kind = source_component.kind;
        component.internal_values =
            copy_result_values(
                source_component.internal_values);
        component.metrics =
            copy_result_values(source_component.metrics);
        component.ports.reserve(source_component.ports.size());
        for (const auto& source_port :
             source_component.ports) {
            component.ports.push_back({
                source_port.port_name,
                source_port.domain,
                source_port.medium_id,
                source_port.phase,
                copy_result_values(
                    source_port.primary_values),
                copy_result_values(
                    source_port.derived_values),
            });
        }
        graph.components.push_back(std::move(component));
    }
    return graph;
}

}  // namespace

struct SimulationService::Impl {
    explicit Impl(std::shared_ptr<const SimulationRuntime> runtime_value)
        : runtime(std::move(runtime_value)) {}

    std::shared_ptr<const SimulationRuntime> runtime;
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

std::string to_string(DiagnosticSeverity severity) {
    switch (severity) {
        case DiagnosticSeverity::information:
            return "information";
        case DiagnosticSeverity::warning:
            return "warning";
        case DiagnosticSeverity::error:
            return "error";
    }
    return "unknown";
}

SimulationService::SimulationService()
    : SimulationService(make_default_simulation_runtime()) {}

SimulationService::SimulationService(
    std::shared_ptr<const SimulationRuntime> runtime)
    : impl_(std::make_unique<Impl>(std::move(runtime))) {
    if (!impl_->runtime) {
        throw std::invalid_argument(
            "simulation service runtime must not be null");
    }
}

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
        const auto* simulation_case =
            selected_case(document, request.case_id);
        if (!request.case_id.empty() && simulation_case == nullptr) {
            throw std::invalid_argument(
                "unknown case id during graph compilation: " +
                request.case_id);
        }
        response.model.case_id =
            simulation_case == nullptr ? "" : simulation_case->id;
        response.compilation.catalog_fingerprint =
            impl_->runtime->impl_->fingerprint;
        const std::string mode = validation_mode(simulation_case);
        if (mode == "transient") {
            const auto graph =
                platform::compile_transient_model_graph(
                    document,
                    impl_->runtime->impl_->components,
                    impl_->runtime->impl_->properties,
                    impl_->runtime->impl_->performance_maps,
                    request.case_id);
            response.compilation.compiled = true;
            response.compilation.mode = "transient";
            response.compilation.variable_count =
                graph.problem.variable_names.size();
            response.compilation.equation_count =
                graph.problem.residual_names.size();
        } else {
            const auto graph = platform::compile_model_graph(
                document,
                impl_->runtime->impl_->components,
                impl_->runtime->impl_->properties,
                impl_->runtime->impl_->performance_maps,
                request.case_id);
            response.compilation.compiled = true;
            response.compilation.mode = "steady";
            response.compilation.variable_count =
                graph.problem.variable_names.size();
            response.compilation.equation_count =
                graph.problem.residual_names.size();
            response.compilation.reduced_connection_equations =
                graph.reduced_connection_equations;
        }
        response.status = OperationStatus::succeeded;
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_model;
        const std::string message = ex.what();
        const bool compilation =
            !response.canonical_model_json.empty();
        if (compilation) {
            const auto diagnostic =
                compilation_diagnostic(message);
            response.error = make_error(
                diagnostic.code, diagnostic.stage, message);
            response.diagnostics.push_back(diagnostic);
        } else {
            response.error = make_error(
                "invalid_model", "parsing", message);
            response.diagnostics.push_back({
                "invalid_model_document",
                DiagnosticSeverity::error,
                "parsing",
                {},
                {},
                {},
                {},
                message,
                {"Correct the model document and submit it again."},
            });
        }
    }
    return response;
}

CatalogResponse SimulationService::get_catalog(
    const CatalogRequest& request) const {
    CatalogResponse response;
    if (!valid_schema(request.schema_version)) {
        response.error = make_error(
            "unsupported_command_schema",
            "request",
            "unsupported command schema_version: " +
                request.schema_version);
        return response;
    }
    response.fingerprint = impl_->runtime->impl_->fingerprint;
    for (const auto& descriptor :
         impl_->runtime->impl_->components.descriptors()) {
        ComponentType component;
        component.kind = descriptor.kind;
        component.version = descriptor.version;
        component.supports_steady = descriptor.supports_steady;
        component.supports_transient =
            descriptor.supports_transient;
        for (const auto& port : descriptor.ports) {
            component.ports.push_back(
                {port.name, port.domain, port.direction,
                 port.maximum_connections});
        }
        for (const auto& parameter : descriptor.parameters) {
            component.parameters.push_back({
                parameter.name,
                parameter.dimension,
                parameter.required,
                parameter.default_value.has_value(),
                parameter.default_value.value_or(0.0),
                parameter.lower_bound,
                parameter.upper_bound,
                parameter.lower_inclusive,
                parameter.upper_inclusive,
            });
        }
        for (const auto& artifact : descriptor.artifacts) {
            component.artifacts.push_back({
                artifact.role,
                artifact.artifact_type,
                artifact.required,
            });
        }
        for (const auto& variable :
             descriptor.internal_variables) {
            component.internal_variables.push_back({
                variable.name,
                variable.dimension,
                variable.kind == DaeVariableKind::differential
                    ? "differential"
                    : "algebraic",
            });
        }
        for (const auto capability :
             descriptor.required_property_capabilities) {
            component.required_property_capabilities.push_back(
                std::string(capability_name(capability)));
        }
        response.components.push_back(std::move(component));
    }
    for (const auto& descriptor :
         impl_->runtime->impl_->properties.descriptors()) {
        PropertyBackendType backend;
        backend.backend = descriptor.backend;
        backend.implementation_name =
            descriptor.implementation_name;
        backend.implementation_version =
            descriptor.implementation_version;
        backend.supported_substances =
            descriptor.supported_substances;
        for (const auto capability : descriptor.capabilities) {
            backend.capabilities.push_back(
                std::string(capability_name(capability)));
        }
        response.property_backends.push_back(std::move(backend));
    }
    response.connector_domains = {
        {"fluid", "thermox.connector.fluid/v1",
         {{"m_dot", "mass_flow"},
          {"p", "pressure"},
          {"h", "specific_enthalpy"}}},
        {"heat", "thermox.connector.heat/v1",
         {{"Q_dot", "power"}, {"T", "temperature"}}},
        {"shaft", "thermox.connector.shaft/v1",
         {{"W_dot", "power"}, {"omega", "angular_speed"}}},
        {"signal", "thermox.connector.signal/v1",
         {{"value", "dimensionless"}}},
        {"control", "thermox.connector.control/v1",
         {{"value", "dimensionless"}}},
    };
    response.status = OperationStatus::succeeded;
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
            impl_->runtime->impl_->components,
            impl_->runtime->impl_->properties,
            impl_->runtime->impl_->performance_maps,
            request.case_id);
        response.metadata = execution_metadata(
            document,
            request.schema_version,
            graph.case_id.value_or(""),
            "steady",
            solver_provenance(request.solver),
            impl_->runtime->impl_->fingerprint,
            impl_->runtime->impl_->components,
            impl_->runtime->impl_->properties);
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
        const platform::GraphResultEvaluator evaluator(
            document, graph,
            impl_->runtime->impl_->properties);
        response.graph =
            copy_graph_result(evaluator.evaluate(result.x));
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
            impl_->runtime->impl_->components,
            impl_->runtime->impl_->properties,
            impl_->runtime->impl_->performance_maps,
            request.case_id);
        response.metadata = execution_metadata(
            document,
            request.schema_version,
            graph.case_id.value_or(""),
            "transient",
            solver_provenance(request.solver),
            impl_->runtime->impl_->fingerprint,
            impl_->runtime->impl_->components,
            impl_->runtime->impl_->properties);
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
    if (!result.diagnostics.success) {
        response.status = OperationStatus::solver_failed;
        response.error = make_error(
            "transient_solver_failed",
            "solve",
            result.diagnostics.message);
        return response;
    }

    try {
        const platform::GraphResultEvaluator evaluator(
            document, graph,
            impl_->runtime->impl_->properties);
        response.trajectory.reserve(result.trajectory.size());
        for (const auto& sample : result.trajectory) {
            response.trajectory.push_back({
                sample.time,
                copy_graph_result(
                    evaluator.evaluate(
                        sample.state, sample.derivative)),
            });
        }
        response.events.reserve(result.events.size());
        for (const auto& event : result.events) {
            response.events.push_back({
                event.name,
                event.time,
                copy_graph_result(
                    evaluator.evaluate(event.state)),
                event.terminal,
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

}  // namespace thermox::service
