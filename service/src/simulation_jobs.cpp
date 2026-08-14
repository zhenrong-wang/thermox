#include "thermox/service/simulation_jobs.hpp"

#include "thermox/service/serialization.hpp"

#include <algorithm>
#include <iomanip>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <limits>
#include <sstream>
#include <string_view>
#include <utility>

namespace thermox::service {

namespace {

std::string fnv1a64(std::string_view value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto character : value) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ULL;
    }
    std::ostringstream encoded;
    encoded << "fnv1a64:" << std::hex << std::setfill('0')
            << std::setw(16) << hash;
    return encoded.str();
}

void append_steady_settings(
    std::ostringstream& stream,
    const SteadySolverSettings& settings) {
    stream << settings.max_iterations << '|'
           << settings.residual_tolerance << '|'
           << settings.step_tolerance << '|'
           << settings.linear_residual_tolerance << '|'
           << to_string(
                  settings.structural_decomposition_policy) << '|'
           << settings.finite_difference_epsilon << '|'
           << settings.min_damping << '|'
           << settings.damping_reduction << '|'
           << settings.sufficient_decrease << '|'
           << settings.max_line_search_steps << '|'
           << settings.continuation_enabled << '|'
           << settings.continuation_initial_step << '|'
           << settings.continuation_minimum_step << '|'
           << settings.continuation_step_growth << '|'
           << settings.continuation_step_reduction << '|'
           << settings.continuation_maximum_stages;
}

void append_calibration_settings(
    std::ostringstream& stream,
    const CalibrationSolverSettings& settings) {
    stream << settings.max_iterations << '|'
           << settings.initial_step_fraction << '|'
           << settings.minimum_step_fraction << '|'
           << settings.step_reduction << '|'
           << settings.minimum_continuation_fraction << '|'
           << settings.continuation_growth << '|';
    append_steady_settings(stream, settings.simulation_solver);
}

void append_reconciliation_settings(
    std::ostringstream& stream,
    const ReconciliationSolverSettings& settings) {
    stream << settings.max_iterations << '|'
           << settings.finite_difference_fraction << '|'
           << settings.constraint_tolerance << '|'
           << settings.step_tolerance << '|'
           << settings.objective_relative_tolerance << '|'
           << settings.minimum_line_search_fraction << '|';
    append_steady_settings(stream, settings.simulation_solver);
}

void append_string(
    std::ostringstream& stream,
    const std::string& value) {
    stream << value.size() << ':' << value << '|';
}

void append_map_payload(
    std::ostringstream& stream,
    const PerformanceMapPayloadInput& map) {
    append_string(stream, map.primary_variable.name);
    append_string(stream, map.primary_variable.dimension);
    append_string(stream, map.family_variable.name);
    append_string(stream, map.family_variable.dimension);
    stream << map.output_variables.size() << '|';
    for (const auto& output : map.output_variables) {
        append_string(stream, output.name);
        append_string(stream, output.dimension);
    }
    stream << map.output_constraints.size() << '|';
    for (const auto& constraint : map.output_constraints) {
        append_string(stream, constraint.output);
        stream << constraint.minimum.has_value() << '|';
        if (constraint.minimum) stream << *constraint.minimum << '|';
        stream << constraint.maximum.has_value() << '|';
        if (constraint.maximum) stream << *constraint.maximum << '|';
        stream << constraint.minimum_inclusive << '|'
               << constraint.maximum_inclusive << '|';
    }
    stream << map.curves.size() << '|';
    for (const auto& curve : map.curves) {
        stream << curve.family_coordinate << '|'
               << curve.samples.size() << '|';
        for (const auto& sample : curve.samples) {
            stream << sample.coordinate << '|'
                   << sample.outputs.size() << '|';
            for (const double output : sample.outputs) {
                stream << output << '|';
            }
        }
    }
    append_string(stream, map.primary_extrapolation);
    append_string(stream, map.family_extrapolation);
}

void append_applicability(
    std::ostringstream& stream,
    const std::vector<CorrelationApplicabilityRangeInput>& ranges) {
    stream << ranges.size() << '|';
    for (const auto& range : ranges) {
        append_string(stream, range.input);
        stream << range.minimum.has_value() << '|';
        if (range.minimum) stream << *range.minimum << '|';
        stream << range.maximum.has_value() << '|';
        if (range.maximum) stream << *range.maximum << '|';
        stream << range.minimum_inclusive << '|'
               << range.maximum_inclusive << '|';
    }
}

void append_operating_envelope(
    std::ostringstream& stream,
    const std::vector<ArtifactCoordinateConstraintInput>& envelope) {
    stream << envelope.size() << '|';
    for (const auto& constraint : envelope) {
        append_string(stream, constraint.coordinate);
        append_string(stream, constraint.dimension);
        stream << constraint.minimum.has_value() << '|';
        if (constraint.minimum) stream << *constraint.minimum << '|';
        stream << constraint.maximum.has_value() << '|';
        if (constraint.maximum) stream << *constraint.maximum << '|';
        stream << constraint.minimum_inclusive << '|'
               << constraint.maximum_inclusive << '|';
    }
}

void append_artifacts(
    std::ostringstream& stream,
    const SimulationArtifactBundle& artifacts) {
    stream << artifacts.performance_maps.size() << '|';
    for (const auto& artifact : artifacts.performance_maps) {
        append_string(stream, artifact.id);
        append_string(stream, artifact.schema_version);
        append_string(stream, artifact.revision);
        append_string(stream, artifact.checksum_sha256);
        stream << artifact.map.has_value() << '|';
        if (artifact.map) {
            append_map_payload(stream, *artifact.map);
        }
        stream << artifact.condition_variable.has_value() << '|';
        if (artifact.condition_variable) {
            append_string(stream, artifact.condition_variable->name);
            append_string(
                stream, artifact.condition_variable->dimension);
        }
        stream << artifact.layers.size() << '|';
        for (const auto& layer : artifact.layers) {
            stream << layer.condition_coordinate << '|';
            append_map_payload(stream, layer.map);
        }
        append_string(stream, artifact.condition_extrapolation);
        append_operating_envelope(stream, artifact.operating_envelope);
    }
    stream << artifacts.correlations.size() << '|';
    for (const auto& artifact : artifacts.correlations) {
        append_string(stream, artifact.id);
        append_string(stream, artifact.schema_version);
        append_string(stream, artifact.revision);
        append_string(stream, artifact.checksum_sha256);
        stream << artifact.inputs.size() << '|';
        for (const auto& input : artifact.inputs) {
            append_string(stream, input.name);
            append_string(stream, input.dimension);
        }
        append_string(stream, artifact.output.name);
        append_string(stream, artifact.output.dimension);
        stream << artifact.candidates.size() << '|';
        for (const auto& candidate : artifact.candidates) {
            append_string(stream, candidate.id);
            append_string(stream, candidate.regime);
            stream << candidate.priority << '|'
                   << candidate.coefficients.size() << '|';
            for (const auto& [name, value] : candidate.coefficients) {
                append_string(stream, name);
                stream << value << '|';
            }
            append_string(stream, candidate.expression);
            append_applicability(stream, candidate.applicability);
        }
        append_operating_envelope(stream, artifact.operating_envelope);
    }
    stream << artifacts.regime_maps.size() << '|';
    for (const auto& artifact : artifacts.regime_maps) {
        append_string(stream, artifact.id);
        append_string(stream, artifact.schema_version);
        append_string(stream, artifact.revision);
        append_string(stream, artifact.checksum_sha256);
        stream << artifact.inputs.size() << '|';
        for (const auto& input : artifact.inputs) {
            append_string(stream, input.name);
            append_string(stream, input.dimension);
        }
        stream << artifact.regions.size() << '|';
        for (const auto& region : artifact.regions) {
            append_string(stream, region.id);
            append_string(stream, region.regime);
            stream << region.priority << '|'
                   << region.branches.size() << '|';
            for (const auto& branch : region.branches) {
                append_string(stream, branch.id);
                stream << branch.priority << '|'
                       << branch.criteria.size() << '|';
                for (const auto& criterion : branch.criteria) {
                    append_string(stream, criterion.expression);
                    append_string(stream, criterion.dimension);
                    stream << criterion.minimum.has_value() << '|';
                    if (criterion.minimum) {
                        stream << *criterion.minimum << '|';
                    }
                    stream << criterion.maximum.has_value() << '|';
                    if (criterion.maximum) {
                        stream << *criterion.maximum << '|';
                    }
                    stream << criterion.minimum_inclusive << '|'
                           << criterion.maximum_inclusive << '|';
                }
            }
        }
        append_operating_envelope(stream, artifact.operating_envelope);
    }
    stream << artifacts.references.size() << '|';
    for (const auto& reference : artifacts.references) {
        append_string(stream, reference.id);
        append_string(stream, reference.artifact_type);
        append_string(stream, reference.schema_version);
        append_string(stream, reference.revision);
        append_string(stream, reference.checksum_sha256);
    }
}

void append_components(
    std::ostringstream& stream,
    const SimulationComponentBundle& components) {
    stream << components.expression_components.size() << '|';
    for (const auto& component :
         components.expression_components) {
        append_string(stream, component.schema_version);
        append_string(stream, component.kind);
        append_string(stream, component.version);
        append_string(stream, component.template_kind);
        append_string(stream, component.display_name);
        append_string(stream, component.category);
        append_string(stream, component.model_name);
        append_string(stream, component.system_boundary_role);
        stream << component.ports.size() << '|';
        for (const auto& port : component.ports) {
            append_string(stream, port.name);
            append_string(stream, port.domain);
            append_string(stream, port.direction);
            stream << port.maximum_connections << '|';
        }
        stream << component.parameters.size() << '|';
        for (const auto& parameter : component.parameters) {
            append_string(stream, parameter.name);
            append_string(stream, parameter.dimension);
            stream << parameter.required << '|'
                   << parameter.default_value_si.has_value()
                   << '|';
            if (parameter.default_value_si) {
                stream << *parameter.default_value_si << '|';
            }
            stream << parameter.lower_bound << '|'
                   << parameter.upper_bound << '|'
                   << parameter.lower_inclusive << '|'
                   << parameter.upper_inclusive << '|';
        }
        stream << component.equations.size() << '|';
        for (const auto& equation : component.equations) {
            append_string(stream, equation.name);
            append_string(stream, equation.expression);
            stream << equation.residual_scale << '|';
        }
    }
}

std::string request_fingerprint(
    const SimulationJobRequest& request) {
    std::ostringstream stream;
    stream << std::setprecision(
        std::numeric_limits<double>::max_digits10);
    stream << request.schema_version << '|'
           << to_string(request.mode) << '|'
           << request.case_id.size() << ':' << request.case_id << '|'
           << request.calibration_id.size() << ':'
           << request.calibration_id << '|'
           << request.reconciliation_id.size() << ':'
           << request.reconciliation_id << '|'
           << request.model_json.size() << ':' << request.model_json << '|';
    stream << request.source_revisions.has_value() << '|';
    if (request.source_revisions) {
        append_string(
            stream, request.source_revisions->project_id);
        append_string(
            stream,
            request.source_revisions->model_revision_id);
        append_string(
            stream, request.source_revisions->model_checksum);
        append_string(
            stream,
            request.source_revisions->case_revision_id);
        append_string(
            stream, request.source_revisions->case_checksum);
        append_string(
            stream,
            request.source_revisions
                ->run_configuration_revision_id);
        append_string(
            stream,
            request.source_revisions
                ->run_configuration_checksum);
        append_string(
            stream,
            request.source_revisions->study_revision_id);
        append_string(
            stream, request.source_revisions->study_checksum);
        append_string(
            stream,
            request.source_revisions->calibration_revision_id);
        append_string(
            stream,
            request.source_revisions->calibration_checksum);
        append_string(
            stream,
            request.source_revisions->reconciliation_revision_id);
        append_string(
            stream,
            request.source_revisions->reconciliation_checksum);
    }
    append_steady_settings(stream, request.steady_solver);
    stream << '|'
           << request.transient_solver.start_time << '|'
           << request.transient_solver.end_time << '|'
           << request.transient_solver.initial_step << '|'
           << request.transient_solver.min_step << '|'
           << request.transient_solver.max_step << '|'
           << request.transient_solver.absolute_tolerance << '|'
           << request.transient_solver.relative_tolerance << '|'
           << request.transient_solver.max_steps << '|'
           << request.transient_solver.max_consecutive_rejections << '|'
           << request.transient_solver.maximum_order << '|'
           << request.transient_solver
                  .compute_consistent_initial_conditions
           << '|';
    append_steady_settings(
        stream, request.transient_solver.nonlinear_solver);
    stream << '|';
    append_calibration_settings(stream, request.calibration_solver);
    stream << '|' << request.calibration_predictions.size() << '|';
    for (const auto& prediction : request.calibration_predictions) {
        append_string(stream, prediction.case_id);
        stream << prediction.observations.size() << '|';
        for (const auto& observation : prediction.observations) {
            append_string(stream, observation.id);
            append_string(stream, observation.target);
            append_string(stream, observation.dimension);
            stream << observation.measured_si << '|'
                   << observation.sigma_si << '|';
        }
    }
    stream << '|' << to_string(request.reconciliation_mode) << '|';
    append_reconciliation_settings(
        stream, request.reconciliation_solver);
    stream << '|'
           << request.reconciliation_profile_likelihood.enabled << '|'
           << request.reconciliation_profile_likelihood.objective_increase
           << '|'
           << request.reconciliation_profile_likelihood
                  .maximum_bracket_steps << '|'
           << request.reconciliation_profile_likelihood
                  .maximum_bisection_steps << '|'
           << request.reconciliation_profile_likelihood
                  .maximum_nuisance_iterations << '|'
           << request.reconciliation_profile_likelihood
                  .parameter_ids.size() << '|';
    for (const auto& id :
         request.reconciliation_profile_likelihood.parameter_ids) {
        append_string(stream, id);
    }
    stream << request.reconciliation_held_out_cases.size() << '|';
    for (const auto& held_out :
         request.reconciliation_held_out_cases) {
        append_string(stream, held_out.case_id);
        stream << held_out.observations.size() << '|';
        for (const auto& observation : held_out.observations) {
            append_string(stream, observation.id);
            append_string(stream, observation.target);
            append_string(stream, observation.dimension);
            stream << observation.measured_si << '|'
                   << observation.sigma_si << '|';
        }
    }
    stream << '|';
    append_artifacts(stream, request.artifacts);
    stream << '|';
    append_components(stream, request.components);
    stream << '|' << request.result_projections.size() << '|';
    for (const auto& projection : request.result_projections) {
        append_string(stream, projection.id);
        append_string(stream, to_string(projection.scope));
        append_string(stream, projection.component_id);
        append_string(stream, projection.port_name);
        append_string(stream, projection.value_name);
        append_string(stream, projection.dimension);
        append_string(stream, to_string(projection.aggregation));
    }
    stream << '|' << request.acceptance_criteria.size() << '|';
    for (const auto& criterion : request.acceptance_criteria) {
        append_string(stream, criterion.id);
        append_string(stream, criterion.projection_id);
        append_string(stream, criterion.dimension);
        stream << criterion.lower_bound_si.has_value() << '|';
        if (criterion.lower_bound_si) {
            stream << *criterion.lower_bound_si << '|';
        }
        stream << criterion.upper_bound_si.has_value() << '|';
        if (criterion.upper_bound_si) {
            stream << *criterion.upper_bound_si << '|';
        }
        stream << criterion.lower_inclusive << '|'
               << criterion.upper_inclusive << '|';
    }
    return fnv1a64(stream.str());
}

void validate_request(const SimulationJobRequest& request) {
    if (request.schema_version != job_schema_v16) {
        throw JobRequestError(
            "unsupported job schema version: " +
            request.schema_version);
    }
    if (request.idempotency_key.empty()) {
        throw JobRequestError("idempotency key must not be empty");
    }
    if (request.identity.user_id.empty()) {
        throw JobRequestError("identity user ID must not be empty");
    }
    if (request.identity.team_id.empty()) {
        throw JobRequestError("identity team ID must not be empty");
    }
    if (request.model_json.empty()) {
        throw JobRequestError("model JSON must not be empty");
    }
    if (request.source_revisions) {
        const auto& source = *request.source_revisions;
        if (source.project_id.empty() ||
            source.model_revision_id.empty() ||
            source.model_checksum.empty()) {
            throw JobRequestError(
                "revision-backed jobs require complete source "
                "revision provenance");
        }
        if (request.mode == SimulationJobMode::calibration) {
            if (source.calibration_revision_id.empty() ||
                source.calibration_checksum.empty() ||
                !source.case_revision_id.empty() ||
                !source.case_checksum.empty() ||
                !source.reconciliation_revision_id.empty() ||
                !source.reconciliation_checksum.empty()) {
                throw JobRequestError(
                    "calibration jobs require calibration provenance "
                    "and no single-case provenance");
            }
        } else if (request.mode ==
                   SimulationJobMode::reconciliation) {
            if (source.reconciliation_revision_id.empty() ||
                source.reconciliation_checksum.empty() ||
                !source.case_revision_id.empty() ||
                !source.case_checksum.empty() ||
                !source.calibration_revision_id.empty() ||
                !source.calibration_checksum.empty()) {
                throw JobRequestError(
                    "reconciliation jobs require reconciliation "
                    "provenance and no case or calibration provenance");
            }
        } else if (source.case_revision_id.empty() ||
                   source.case_checksum.empty() ||
                   !source.calibration_revision_id.empty() ||
                   !source.calibration_checksum.empty() ||
                   !source.reconciliation_revision_id.empty() ||
                   !source.reconciliation_checksum.empty()) {
            throw JobRequestError(
                "simulation jobs require case provenance and no "
                "calibration provenance");
        }
        if (source.run_configuration_revision_id.empty() !=
            source.run_configuration_checksum.empty()) {
            throw JobRequestError(
                "run configuration revision provenance "
                "requires both revision ID and checksum");
        }
        if (source.study_revision_id.empty() !=
            source.study_checksum.empty()) {
            throw JobRequestError(
                "study revision provenance requires both "
                "revision ID and checksum");
        }
        if (!source.run_configuration_revision_id.empty() &&
            source.study_revision_id.empty()) {
            throw JobRequestError(
                "run-backed jobs require Study revision "
                "provenance");
        }
    }
    try {
        validate_result_projections(request.result_projections);
        validate_engineering_acceptance_criteria(
            request.acceptance_criteria,
            request.result_projections);
    } catch (const ResultProjectionError& error) {
        throw JobRequestError(error.what());
    }
    if (request.mode == SimulationJobMode::calibration &&
        (!request.case_id.empty() || request.calibration_id.empty() ||
         !request.result_projections.empty())) {
        throw JobRequestError(
            "calibration jobs require a calibration ID and do not "
            "accept a case ID or result projections");
    }
    if (request.mode != SimulationJobMode::calibration &&
        (!request.calibration_id.empty() ||
         !request.calibration_predictions.empty())) {
        throw JobRequestError(
            "simulation jobs do not accept calibration inputs");
    }
    if (request.mode == SimulationJobMode::reconciliation &&
        (!request.case_id.empty() || request.reconciliation_id.empty() ||
         !request.result_projections.empty() ||
         !request.acceptance_criteria.empty())) {
        throw JobRequestError(
            "reconciliation jobs require a reconciliation ID and do "
            "not accept a case ID, result projections, or acceptance "
            "criteria");
    }
    if (request.mode != SimulationJobMode::reconciliation &&
        (!request.reconciliation_id.empty() ||
         !request.reconciliation_held_out_cases.empty() ||
         request.reconciliation_profile_likelihood.enabled)) {
        throw JobRequestError(
            "non-reconciliation jobs do not accept reconciliation "
            "inputs");
    }
    if (request.mode == SimulationJobMode::steady &&
        std::any_of(
            request.result_projections.begin(),
            request.result_projections.end(),
            [](const auto& projection) {
                return projection.aggregation !=
                    ResultAggregation::final;
            })) {
        throw JobRequestError(
            "steady jobs only support final result projection "
            "aggregation");
    }
}

void validate_identity(const IdentityContext& identity) {
    if (identity.user_id.empty()) {
        throw JobRequestError("identity user ID must not be empty");
    }
    if (identity.team_id.empty()) {
        throw JobRequestError("identity team ID must not be empty");
    }
}

ServiceError unhandled_worker_error(const std::exception& error) {
    return {
        error_schema_v1,
        "worker_execution_failed",
        "worker",
        error.what(),
    };
}

}  // namespace

std::string to_string(SimulationJobMode mode) {
    switch (mode) {
        case SimulationJobMode::steady:
            return "steady";
        case SimulationJobMode::transient:
            return "transient";
        case SimulationJobMode::calibration:
            return "calibration";
        case SimulationJobMode::reconciliation:
            return "reconciliation";
    }
    return "unknown";
}

std::string to_string(SimulationJobState state) {
    switch (state) {
        case SimulationJobState::queued:
            return "queued";
        case SimulationJobState::running:
            return "running";
        case SimulationJobState::succeeded:
            return "succeeded";
        case SimulationJobState::failed:
            return "failed";
        case SimulationJobState::cancelled:
            return "cancelled";
    }
    return "unknown";
}

bool is_terminal(SimulationJobState state) {
    return state == SimulationJobState::succeeded ||
        state == SimulationJobState::failed ||
        state == SimulationJobState::cancelled;
}

struct SimulationJobService::Impl {
    Impl(
        std::shared_ptr<const SimulationRuntime> runtime,
        std::shared_ptr<const EngineeringArtifactResolver>
            engineering_artifacts,
        std::shared_ptr<SimulationJobRepository> job_repository,
        std::shared_ptr<ResultArtifactStore> artifact_store)
        : simulation(
              std::move(runtime),
              std::move(engineering_artifacts)),
          jobs(std::move(job_repository)),
          artifacts(std::move(artifact_store)) {
        if (!jobs) {
            throw std::invalid_argument(
                "job repository must not be null");
        }
        if (!artifacts) {
            throw std::invalid_argument(
                "result artifact store must not be null");
        }
    }

    SimulationService simulation;
    std::shared_ptr<SimulationJobRepository> jobs;
    std::shared_ptr<ResultArtifactStore> artifacts;
};

SimulationJobService::SimulationJobService(
    std::shared_ptr<SimulationJobRepository> jobs,
    std::shared_ptr<ResultArtifactStore> artifacts)
    : SimulationJobService(
          make_default_simulation_runtime(),
          nullptr,
          std::move(jobs),
          std::move(artifacts)) {}

SimulationJobService::SimulationJobService(
    std::shared_ptr<const SimulationRuntime> runtime,
    std::shared_ptr<SimulationJobRepository> jobs,
    std::shared_ptr<ResultArtifactStore> artifacts)
    : SimulationJobService(
          std::move(runtime),
          nullptr,
          std::move(jobs),
          std::move(artifacts)) {}

SimulationJobService::SimulationJobService(
    std::shared_ptr<const SimulationRuntime> runtime,
    std::shared_ptr<const EngineeringArtifactResolver>
        engineering_artifacts,
    std::shared_ptr<SimulationJobRepository> jobs,
    std::shared_ptr<ResultArtifactStore> artifacts)
    : impl_(std::make_unique<Impl>(
          std::move(runtime),
          std::move(engineering_artifacts),
          std::move(jobs),
          std::move(artifacts))) {}

SimulationJobService::~SimulationJobService() = default;
SimulationJobService::SimulationJobService(
    SimulationJobService&&) noexcept = default;
SimulationJobService& SimulationJobService::operator=(
    SimulationJobService&&) noexcept = default;

SimulationJobRecord SimulationJobService::submit(
    const SimulationJobRequest& request) {
    validate_request(request);
    if (request.mode != SimulationJobMode::calibration &&
        request.mode != SimulationJobMode::reconciliation) {
        ValidateModelRequest validation_request;
        validation_request.model_json = request.model_json;
        validation_request.case_id = request.case_id;
        validation_request.artifacts = request.artifacts;
        validation_request.components = request.components;
        const auto validation =
            impl_->simulation.validate_model(validation_request);
        if (!validation.readiness.calculatable) {
            std::string reason = validation.error.message;
            if (reason.empty() && !validation.diagnostics.empty()) {
                reason = validation.diagnostics.front().message;
            }
            throw JobRequestError(
                "simulation request is not calculation-ready" +
                (reason.empty() ? std::string{} : ": " + reason));
        }
    }
    return impl_->jobs->create_or_get(
        request, request_fingerprint(request));
}

std::optional<SimulationJobRecord> SimulationJobService::get(
    const IdentityContext& identity,
    const std::string& job_id) const {
    validate_identity(identity);
    if (job_id.empty()) {
        throw JobRequestError("job ID must not be empty");
    }
    return impl_->jobs->get(identity.team_id, job_id);
}

SimulationJobPage SimulationJobService::list(
    const IdentityContext& identity,
    const SimulationJobQuery& query) const {
    validate_identity(identity);
    if (query.limit == 0 || query.limit > 200) {
        throw JobRequestError(
            "simulation history limit must be between 1 and 200");
    }
    if (query.before && query.before->job_id.empty()) {
        throw JobRequestError(
            "simulation history cursor job ID must not be empty");
    }
    return impl_->jobs->list(identity.team_id, query);
}

std::optional<ResultArtifact> SimulationJobService::get_result(
    const IdentityContext& identity,
    const std::string& job_id) const {
    const auto record = get(identity, job_id);
    if (!record) {
        return std::nullopt;
    }
    if (record->state != SimulationJobState::succeeded ||
        !record->result_artifact) {
        throw JobStateError(
            "result is only available for a succeeded job");
    }
    const auto content = impl_->artifacts->get(
        *record->result_artifact);
    if (!content) {
        throw JobStateError(
            "succeeded job references a missing result artifact");
    }
    if (content->size() !=
        record->result_artifact->byte_size) {
        throw JobStateError(
            "result artifact size does not match its manifest");
    }
    return ResultArtifact{
        *record->result_artifact,
        *content,
    };
}

std::optional<SimulationJobRecord> SimulationJobService::run_next(
    const std::string& worker_id,
    const SimulationWorkerSettings& settings) {
    if (worker_id.empty()) {
        throw JobRequestError("worker ID must not be empty");
    }
    if (settings.lease_duration.count() <= 0 ||
        settings.heartbeat_interval.count() <= 0 ||
        settings.heartbeat_interval >= settings.lease_duration ||
        settings.maximum_attempts == 0) {
        throw JobRequestError(
            "worker lease settings are invalid");
    }
    const ServiceError exhausted_error{
        error_schema_v1,
        "worker_attempts_exhausted",
        "worker",
        "simulation exceeded the maximum number of worker "
        "lease attempts",
    };
    (void)impl_->jobs->recover_expired(
        settings.maximum_attempts, exhausted_error);
    auto claimed = impl_->jobs->claim_next(
        worker_id, settings.lease_duration);
    if (!claimed) {
        return std::nullopt;
    }

    struct HeartbeatState {
        std::mutex mutex;
        std::condition_variable_any changed;
        std::atomic<bool> lost{false};
    };
    const auto heartbeat_state =
        std::make_shared<HeartbeatState>();
    std::jthread heartbeat(
        [
            jobs = impl_->jobs,
            heartbeat_state,
            job_id = claimed->job_id,
            revision = claimed->revision,
            worker_id,
            settings
        ](const std::stop_token& stop) {
            std::unique_lock lock(heartbeat_state->mutex);
            while (!stop.stop_requested()) {
                heartbeat_state->changed.wait_for(
                    lock,
                    stop,
                    settings.heartbeat_interval,
                    [] { return false; });
                if (stop.stop_requested()) {
                    break;
                }
                lock.unlock();
                bool renewed = false;
                try {
                    renewed = jobs->renew_lease(
                        job_id,
                        revision,
                        worker_id,
                        settings.lease_duration);
                } catch (...) {
                    renewed = false;
                }
                lock.lock();
                if (!renewed) {
                    heartbeat_state->lost = true;
                    break;
                }
            }
        });
    const auto require_lease = [&]() {
        if (heartbeat_state->lost.load()) {
            throw JobStateError(
                "worker lost its simulation job lease");
        }
    };

    try {
        if (claimed->request.mode == SimulationJobMode::steady) {
            SteadySimulationRequest request;
            request.model_json = claimed->request.model_json;
            request.case_id = claimed->request.case_id;
            request.solver = claimed->request.steady_solver;
            request.artifacts = claimed->request.artifacts;
            request.components = claimed->request.components;
            auto response = impl_->simulation.run_steady(request);
            response.metadata.source_revisions =
                claimed->request.source_revisions;
            require_lease();
            if (!response.succeeded()) {
                return impl_->jobs->publish_failure(
                    claimed->job_id,
                    claimed->revision,
                    response.error,
                    response.metadata);
            }
            auto summary =
                claimed->request.result_projections.empty()
                ? std::optional<ResultSummary>{}
                : std::optional<ResultSummary>{
                      project_steady_result(
                          response.graph,
                          claimed->request.result_projections)};
            if (summary &&
                !claimed->request.acceptance_criteria.empty()) {
                summary->engineering_acceptance =
                    evaluate_engineering_acceptance(
                        *summary,
                        claimed->request.acceptance_criteria);
            }
            const auto content =
                serialize_steady_response_json(response);
            const auto manifest = impl_->artifacts->put_json(
                claimed->job_id,
                response.metadata.result_schema_version,
                content);
            require_lease();
            return impl_->jobs->publish_success(
                claimed->job_id,
                claimed->revision,
                response.metadata,
                manifest,
                summary);
        }

        if (claimed->request.mode ==
            SimulationJobMode::calibration) {
            if (!claimed->request.calibration_predictions.empty()) {
                EngineeringStudyRequest request;
                request.model_json = claimed->request.model_json;
                request.calibration_id = claimed->request.calibration_id;
                request.calibration_solver =
                    claimed->request.calibration_solver;
                request.prediction_solver = claimed->request
                    .calibration_solver.simulation_solver;
                request.prediction_cases =
                    claimed->request.calibration_predictions;
                request.artifacts = claimed->request.artifacts;
                request.components = claimed->request.components;
                auto response =
                    impl_->simulation.run_engineering_study(request);
                response.calibration.metadata.source_revisions =
                    claimed->request.source_revisions;
                for (auto& prediction : response.predictions) {
                    prediction.simulation.metadata.source_revisions =
                        claimed->request.source_revisions;
                }
                require_lease();
                if (!response.succeeded()) {
                    return impl_->jobs->publish_failure(
                        claimed->job_id,
                        claimed->revision,
                        response.error,
                        response.calibration.metadata);
                }
                const auto content =
                    serialize_engineering_study_response_json(response);
                const auto manifest = impl_->artifacts->put_json(
                    claimed->job_id,
                    response.calibration.metadata.result_schema_version,
                    content);
                require_lease();
                return impl_->jobs->publish_success(
                    claimed->job_id,
                    claimed->revision,
                    response.calibration.metadata,
                    manifest,
                    std::nullopt);
            }
            CalibrationRequest request;
            request.model_json = claimed->request.model_json;
            request.calibration_id = claimed->request.calibration_id;
            request.solver = claimed->request.calibration_solver;
            request.artifacts = claimed->request.artifacts;
            request.components = claimed->request.components;
            auto response = impl_->simulation.run_calibration(request);
            response.metadata.source_revisions =
                claimed->request.source_revisions;
            require_lease();
            if (!response.succeeded()) {
                return impl_->jobs->publish_failure(
                    claimed->job_id,
                    claimed->revision,
                    response.error,
                    response.metadata);
            }
            const auto content =
                serialize_calibration_response_json(response);
            const auto manifest = impl_->artifacts->put_json(
                claimed->job_id,
                response.metadata.result_schema_version,
                content);
            require_lease();
            return impl_->jobs->publish_success(
                claimed->job_id,
                claimed->revision,
                response.metadata,
                manifest,
                std::nullopt);
        }

        if (claimed->request.mode ==
            SimulationJobMode::reconciliation) {
            DataReconciliationRequest request;
            request.model_json = claimed->request.model_json;
            request.reconciliation_id =
                claimed->request.reconciliation_id;
            request.mode = claimed->request.reconciliation_mode;
            request.solver = claimed->request.reconciliation_solver;
            request.profile_likelihood = claimed->request
                .reconciliation_profile_likelihood;
            request.held_out_cases =
                claimed->request.reconciliation_held_out_cases;
            request.artifacts = claimed->request.artifacts;
            request.components = claimed->request.components;
            auto response =
                impl_->simulation.run_data_reconciliation(request);
            response.metadata.source_revisions =
                claimed->request.source_revisions;
            require_lease();
            if (!response.succeeded()) {
                return impl_->jobs->publish_failure(
                    claimed->job_id,
                    claimed->revision,
                    response.error,
                    response.metadata);
            }
            const auto content =
                serialize_data_reconciliation_response_json(response);
            const auto manifest = impl_->artifacts->put_json(
                claimed->job_id,
                response.metadata.result_schema_version,
                content);
            require_lease();
            return impl_->jobs->publish_success(
                claimed->job_id,
                claimed->revision,
                response.metadata,
                manifest,
                std::nullopt);
        }

        TransientSimulationRequest request;
        request.model_json = claimed->request.model_json;
        request.case_id = claimed->request.case_id;
        request.solver = claimed->request.transient_solver;
        request.artifacts = claimed->request.artifacts;
        request.components = claimed->request.components;
        auto response =
            impl_->simulation.run_transient(request);
        response.metadata.source_revisions =
            claimed->request.source_revisions;
        require_lease();
        if (!response.succeeded()) {
            return impl_->jobs->publish_failure(
                claimed->job_id,
                claimed->revision,
                response.error,
                response.metadata);
        }
        auto summary =
            claimed->request.result_projections.empty()
            ? std::optional<ResultSummary>{}
            : std::optional<ResultSummary>{
                  project_transient_result(
                      response.trajectory,
                      claimed->request.result_projections)};
        if (summary &&
            !claimed->request.acceptance_criteria.empty()) {
            summary->engineering_acceptance =
                evaluate_engineering_acceptance(
                    *summary,
                    claimed->request.acceptance_criteria);
        }
        const auto content =
            serialize_transient_response_json(response);
        const auto manifest = impl_->artifacts->put_json(
            claimed->job_id,
            response.metadata.result_schema_version,
            content);
        require_lease();
        return impl_->jobs->publish_success(
            claimed->job_id,
            claimed->revision,
            response.metadata,
            manifest,
            summary);
    } catch (const JobConflictError&) {
        throw;
    } catch (const JobStateError&) {
        throw;
    } catch (const ResultProjectionError& error) {
        return impl_->jobs->publish_failure(
            claimed->job_id,
            claimed->revision,
            {
                error_schema_v1,
                "result_projection_failed",
                "result",
                error.what(),
            },
            std::nullopt);
    } catch (const std::exception& error) {
        return impl_->jobs->publish_failure(
            claimed->job_id,
            claimed->revision,
            unhandled_worker_error(error),
            std::nullopt);
    }
}

SimulationJobRecord SimulationJobService::cancel(
    const IdentityContext& identity,
    const std::string& job_id,
    std::uint64_t expected_revision) {
    validate_identity(identity);
    if (job_id.empty()) {
        throw JobRequestError("job ID must not be empty");
    }
    return impl_->jobs->cancel(
        identity.team_id, job_id, expected_revision);
}

}  // namespace thermox::service
