#include "thermox/service/projects.hpp"

#include "artifact_payload.hpp"
#include "serialization_internal.hpp"

#include "thermox/service/in_memory_projects.hpp"
#include "thermox/platform/model_document.hpp"
#include "thermox/platform/performance_map.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace thermox::service {

namespace {

void require_identity(const IdentityContext& identity) {
    if (identity.user_id.empty() || identity.team_id.empty()) {
        throw ProjectRequestError(
            "user ID and Team ID must not be empty");
    }
}

std::string trim(std::string value) {
    const auto whitespace = [](unsigned char character) {
        return std::isspace(character) != 0;
    };
    std::size_t first = 0;
    while (first < value.size() &&
           whitespace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first &&
           whitespace(
               static_cast<unsigned char>(value[last - 1U]))) {
        --last;
    }
    return value.substr(first, last - first);
}

std::string checksum(std::string_view value) {
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>
        context{EVP_MD_CTX_new(), EVP_MD_CTX_free};
    if (!context ||
        EVP_DigestInit_ex(
            context.get(), EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(
            context.get(), value.data(), value.size()) != 1) {
        throw std::runtime_error(
            "could not initialize model SHA-256 checksum");
    }
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int digest_size = 0;
    if (EVP_DigestFinal_ex(
            context.get(), digest, &digest_size) != 1) {
        throw std::runtime_error(
            "could not finalize model SHA-256 checksum");
    }
    std::ostringstream encoded;
    encoded << "sha256:";
    for (unsigned int index = 0; index < digest_size; ++index) {
        encoded << std::hex << std::setfill('0')
                << std::setw(2)
                << static_cast<unsigned int>(digest[index]);
    }
    return encoded.str();
}

std::string run_mode(const std::string& case_mode) {
    if (case_mode.find("transient") != std::string::npos ||
        case_mode.find("dynamic") != std::string::npos) {
        return "transient";
    }
    if (case_mode.find("steady") != std::string::npos ||
        case_mode == "design" ||
        case_mode == "off_design") {
        return "steady";
    }
    throw ProjectRequestError(
        "case mode is not executable: " + case_mode);
}

void validate_steady_solver(
    const SteadySolverSettings& value) {
    if (value.max_iterations <= 0 ||
        value.max_line_search_steps <= 0 ||
        !std::isfinite(value.residual_tolerance) ||
        value.residual_tolerance <= 0.0 ||
        !std::isfinite(value.step_tolerance) ||
        value.step_tolerance <= 0.0 ||
        !std::isfinite(value.finite_difference_epsilon) ||
        value.finite_difference_epsilon <= 0.0 ||
        !std::isfinite(value.min_damping) ||
        value.min_damping <= 0.0 ||
        !std::isfinite(value.damping_reduction) ||
        value.damping_reduction <= 0.0 ||
        value.damping_reduction >= 1.0 ||
        !std::isfinite(value.sufficient_decrease) ||
        value.sufficient_decrease <= 0.0) {
        throw ProjectRequestError(
            "invalid steady solver settings");
    }
}

void validate_transient_solver(
    const TransientSolverSettings& value) {
    validate_steady_solver(value.nonlinear_solver);
    if (!std::isfinite(value.start_time) ||
        !std::isfinite(value.end_time) ||
        value.end_time <= value.start_time ||
        !std::isfinite(value.initial_step) ||
        value.initial_step <= 0.0 ||
        !std::isfinite(value.min_step) ||
        value.min_step <= 0.0 ||
        !std::isfinite(value.max_step) ||
        value.max_step < value.min_step ||
        !std::isfinite(value.absolute_tolerance) ||
        value.absolute_tolerance <= 0.0 ||
        !std::isfinite(value.relative_tolerance) ||
        value.relative_tolerance <= 0.0 ||
        value.max_steps <= 0 ||
        value.max_consecutive_rejections <= 0) {
        throw ProjectRequestError(
            "invalid transient solver settings");
    }
}

void append_steady(
    std::ostream& out,
    const SteadySolverSettings& value) {
    out << value.max_iterations << '|'
        << value.residual_tolerance << '|'
        << value.step_tolerance << '|'
        << value.finite_difference_epsilon << '|'
        << value.min_damping << '|'
        << value.damping_reduction << '|'
        << value.sufficient_decrease << '|'
        << value.max_line_search_steps;
}

std::string run_configuration_identity(
    const CreateRunConfigurationRevisionRequest& request,
    const std::string& mode,
    const std::vector<std::string>& artifacts) {
    std::ostringstream out;
    out << std::setprecision(17)
        << request.run_configuration_id << '|'
        << request.model_revision_id << '|'
        << request.case_revision_id << '|'
        << mode << '|';
    for (const auto& id : artifacts) {
        out << id.size() << ':' << id << '|';
    }
    append_steady(out, request.steady_solver);
    const auto& transient = request.transient_solver;
    out << '|' << transient.start_time
        << '|' << transient.end_time
        << '|' << transient.initial_step
        << '|' << transient.min_step
        << '|' << transient.max_step
        << '|' << transient.absolute_tolerance
        << '|' << transient.relative_tolerance
        << '|' << transient.max_steps
        << '|' << transient.max_consecutive_rejections
        << '|' <<
            transient.compute_consistent_initial_conditions
        << '|';
    append_steady(out, transient.nonlinear_solver);
    return out.str();
}

}  // namespace

ProjectService::ProjectService(
    std::shared_ptr<ProjectRepository> repository)
    : ProjectService(
          std::move(repository),
          make_in_memory_engineering_artifact_content_store()) {}

ProjectService::ProjectService(
    std::shared_ptr<ProjectRepository> repository,
    std::shared_ptr<EngineeringArtifactContentStore>
        artifact_content)
    : repository_(std::move(repository)),
      artifact_content_(std::move(artifact_content)) {
    if (!repository_) {
        throw std::invalid_argument(
            "project repository must not be null");
    }
    if (!artifact_content_) {
        throw std::invalid_argument(
            "engineering artifact content store must not be "
            "null");
    }
}

ProjectRecord ProjectService::create_project(
    const CreateProjectRequest& request) const {
    require_identity(request.identity);
    const auto name = trim(request.name);
    const auto description = trim(request.description);
    if (name.empty() || name.size() > 200U) {
        throw ProjectRequestError(
            "project name must contain 1 to 200 characters");
    }
    if (description.size() > 4000U) {
        throw ProjectRequestError(
            "project description exceeds 4000 characters");
    }
    return repository_->create_project(
        request.identity.team_id,
        request.identity.user_id,
        name,
        description);
}

std::optional<ProjectRecord> ProjectService::get_project(
    const IdentityContext& identity,
    const std::string& project_id) const {
    require_identity(identity);
    if (project_id.empty()) {
        throw ProjectRequestError(
            "project ID must not be empty");
    }
    return repository_->get_project(
        identity.team_id, project_id);
}

std::vector<ProjectRecord> ProjectService::list_projects(
    const IdentityContext& identity) const {
    require_identity(identity);
    return repository_->list_projects(identity.team_id);
}

ModelRevisionRecord ProjectService::create_model_revision(
    const CreateModelRevisionRequest& request) const {
    require_identity(request.identity);
    if (request.project_id.empty()) {
        throw ProjectRequestError(
            "project ID must not be empty");
    }
    if (request.model_json.empty()) {
        throw ProjectRequestError(
            "model document must not be empty");
    }

    platform::ModelDocument document;
    std::string canonical;
    try {
        document = platform::parse_topology_document_text(
            request.model_json);
        canonical =
            detail::serialize_topology_document_json(document);
    } catch (const std::exception& error) {
        throw ProjectRequestError(
            std::string("invalid model document: ") +
            error.what());
    }
    return repository_->create_model_revision(
        request.identity.team_id,
        request.identity.user_id,
        request.project_id,
        request.parent_model_revision_id,
        document.schema_version,
        document.model_id,
        document.revision,
        canonical,
        checksum(canonical));
}

std::optional<ModelRevisionRecord>
ProjectService::get_model_revision(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string& model_revision_id) const {
    require_identity(identity);
    if (project_id.empty() || model_revision_id.empty()) {
        throw ProjectRequestError(
            "project and model revision IDs must not be empty");
    }
    return repository_->get_model_revision(
        identity.team_id,
        project_id,
        model_revision_id);
}

std::vector<ModelRevisionRecord>
ProjectService::list_model_revisions(
    const IdentityContext& identity,
    const std::string& project_id) const {
    require_identity(identity);
    if (project_id.empty()) {
        throw ProjectRequestError(
            "project ID must not be empty");
    }
    return repository_->list_model_revisions(
        identity.team_id, project_id);
}

CaseRevisionRecord ProjectService::create_case_revision(
    const CreateCaseRevisionRequest& request) const {
    require_identity(request.identity);
    if (request.project_id.empty() ||
        request.model_revision_id.empty()) {
        throw ProjectRequestError(
            "project and model revision IDs must not be empty");
    }
    if (request.case_json.empty()) {
        throw ProjectRequestError(
            "case document must not be empty");
    }

    platform::CaseDefinition simulation_case;
    std::string canonical;
    try {
        simulation_case =
            platform::parse_case_document_text(request.case_json);
        canonical =
            detail::serialize_case_document_json(
                simulation_case);
    } catch (const std::exception& error) {
        throw ProjectRequestError(
            std::string("invalid case document: ") +
            error.what());
    }
    return repository_->create_case_revision(
        request.identity.team_id,
        request.identity.user_id,
        request.project_id,
        request.model_revision_id,
        request.parent_case_revision_id,
        simulation_case.id,
        simulation_case.mode,
        canonical,
        checksum(canonical));
}

std::optional<CaseRevisionRecord>
ProjectService::get_case_revision(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string& model_revision_id,
    const std::string& case_revision_id) const {
    require_identity(identity);
    if (project_id.empty() || model_revision_id.empty() ||
        case_revision_id.empty()) {
        throw ProjectRequestError(
            "project, model revision, and case revision IDs "
            "must not be empty");
    }
    return repository_->get_case_revision(
        identity.team_id,
        project_id,
        model_revision_id,
        case_revision_id);
}

std::vector<CaseRevisionRecord>
ProjectService::list_case_revisions(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string& model_revision_id) const {
    require_identity(identity);
    if (project_id.empty() || model_revision_id.empty()) {
        throw ProjectRequestError(
            "project and model revision IDs must not be empty");
    }
    return repository_->list_case_revisions(
        identity.team_id, project_id, model_revision_id);
}

std::optional<ResolvedModelCase>
ProjectService::resolve_model_case(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string& model_revision_id,
    const std::string& case_revision_id) const {
    require_identity(identity);
    if (project_id.empty() || model_revision_id.empty() ||
        case_revision_id.empty()) {
        throw ProjectRequestError(
            "project, model revision, and case revision IDs "
            "must not be empty");
    }
    const auto model = repository_->get_model_revision(
        identity.team_id, project_id, model_revision_id);
    const auto simulation_case =
        repository_->get_case_revision(
            identity.team_id,
            project_id,
            model_revision_id,
            case_revision_id);
    if (!model || !simulation_case) {
        return std::nullopt;
    }
    try {
        auto document = platform::parse_topology_document_text(
            model->canonical_model_json);
        document.schema_version = "thermox.model/v2";
        document.cases = {
            platform::parse_case_document_text(
                simulation_case->canonical_case_json),
        };
        return ResolvedModelCase{
            project_id,
            model_revision_id,
            model->checksum,
            case_revision_id,
            simulation_case->checksum,
            simulation_case->case_id,
            simulation_case->mode,
            detail::serialize_model_document_json(document),
        };
    } catch (const std::exception& error) {
        throw ProjectStateError(
            std::string(
                "persisted model/case composition failed: ") +
            error.what());
    }
}

ArtifactRevisionRecord
ProjectService::create_artifact_revision(
    const CreateArtifactRevisionRequest& request) const {
    require_identity(request.identity);
    if (request.project_id.empty() ||
        request.artifact_id.empty()) {
        throw ProjectRequestError(
            "project and artifact IDs must not be empty");
    }
    if (request.artifact_type !=
        platform::performance_map_artifact_type) {
        throw ProjectRequestError(
            "unsupported engineering artifact type: " +
            request.artifact_type);
    }
    if (request.artifact_json.empty()) {
        throw ProjectRequestError(
            "artifact payload must not be empty");
    }
    if (!repository_->get_project(
            request.identity.team_id, request.project_id)) {
        throw ProjectStateError("project was not found");
    }
    std::string canonical;
    try {
        canonical =
            detail::canonicalize_performance_map_payload(
                request.artifact_schema_version,
                request.artifact_json);
    } catch (const std::exception& error) {
        throw ProjectRequestError(
            std::string("invalid engineering artifact: ") +
            error.what());
    }
    const auto expected_checksum = checksum(canonical);
    auto content = artifact_content_->put_json(
        request.identity.team_id,
        request.project_id,
        request.artifact_id,
        request.artifact_schema_version,
        canonical);
    if (content.object_key.empty() ||
        content.media_type != "application/json" ||
        content.byte_size != canonical.size()) {
        throw ProjectStateError(
            "artifact content store returned an invalid "
            "manifest");
    }
    if (!content.checksum.empty() &&
        content.checksum != expected_checksum) {
        throw ProjectStateError(
            "artifact content store checksum mismatch");
    }
    content.checksum = expected_checksum;
    return repository_->create_artifact_revision(
        request.identity.team_id,
        request.identity.user_id,
        request.project_id,
        request.artifact_id,
        request.parent_artifact_revision_id,
        request.artifact_type,
        request.artifact_schema_version,
        content);
}

std::optional<ArtifactRevisionRecord>
ProjectService::get_artifact_revision(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string& artifact_revision_id) const {
    require_identity(identity);
    if (project_id.empty() ||
        artifact_revision_id.empty()) {
        throw ProjectRequestError(
            "project and artifact revision IDs must not be "
            "empty");
    }
    return repository_->get_artifact_revision(
        identity.team_id,
        project_id,
        artifact_revision_id);
}

std::vector<ArtifactRevisionRecord>
ProjectService::list_artifact_revisions(
    const IdentityContext& identity,
    const std::string& project_id) const {
    require_identity(identity);
    if (project_id.empty()) {
        throw ProjectRequestError(
            "project ID must not be empty");
    }
    return repository_->list_artifact_revisions(
        identity.team_id, project_id);
}

std::optional<ResolvedEngineeringArtifacts>
ProjectService::resolve_artifact_revisions(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::vector<std::string>&
        artifact_revision_ids) const {
    require_identity(identity);
    if (project_id.empty()) {
        throw ProjectRequestError(
            "project ID must not be empty");
    }
    ResolvedEngineeringArtifacts result;
    std::set<std::string> logical_ids;
    for (const auto& revision_id : artifact_revision_ids) {
        if (revision_id.empty()) {
            throw ProjectRequestError(
                "artifact revision IDs must not be empty");
        }
        const auto revision =
            repository_->get_artifact_revision(
                identity.team_id,
                project_id,
                revision_id);
        if (!revision) {
            return std::nullopt;
        }
        if (!logical_ids.insert(revision->artifact_id).second) {
            throw ProjectRequestError(
                "only one revision may be selected for "
                "artifact ID: " + revision->artifact_id);
        }
        const auto payload =
            artifact_content_->get(revision->content);
        if (!payload) {
            throw ProjectStateError(
                "persisted engineering artifact content was "
                "not found");
        }
        if (payload->size() != revision->content.byte_size ||
            checksum(*payload) != revision->content.checksum) {
            throw ProjectStateError(
                "persisted engineering artifact content "
                "failed integrity verification");
        }
        if (revision->artifact_type !=
            platform::performance_map_artifact_type) {
            throw ProjectStateError(
                "persisted engineering artifact type is not "
                "supported");
        }
        result.snapshot.performance_maps.push_back(
            detail::performance_map_from_payload(
                revision->artifact_id,
                revision->artifact_schema_version,
                revision->artifact_revision_id,
                revision->content.checksum.substr(7),
                *payload));
        result.revisions.push_back(*revision);
    }
    return result;
}

RunConfigurationRevisionRecord
ProjectService::create_run_configuration_revision(
    const CreateRunConfigurationRevisionRequest& request) const {
    require_identity(request.identity);
    if (request.project_id.empty() ||
        request.run_configuration_id.empty() ||
        request.model_revision_id.empty() ||
        request.case_revision_id.empty()) {
        throw ProjectRequestError(
            "project, run configuration, model revision, and "
            "case revision IDs must not be empty");
    }
    const auto model_case = resolve_model_case(
        request.identity,
        request.project_id,
        request.model_revision_id,
        request.case_revision_id);
    if (!model_case) {
        throw ProjectStateError(
            "model/case revision pair was not found");
    }
    auto artifact_ids = request.artifact_revision_ids;
    std::sort(artifact_ids.begin(), artifact_ids.end());
    if (std::adjacent_find(
            artifact_ids.begin(), artifact_ids.end()) !=
        artifact_ids.end()) {
        throw ProjectRequestError(
            "artifact revision IDs must be unique");
    }
    if (!resolve_artifact_revisions(
            request.identity,
            request.project_id,
            artifact_ids)) {
        throw ProjectStateError(
            "artifact revision was not found");
    }
    const auto mode = run_mode(model_case->mode);
    validate_steady_solver(request.steady_solver);
    validate_transient_solver(request.transient_solver);
    return repository_->create_run_configuration_revision(
        request.identity.team_id,
        request.identity.user_id,
        request.project_id,
        request.run_configuration_id,
        request.parent_run_configuration_revision_id,
        request.model_revision_id,
        request.case_revision_id,
        artifact_ids,
        mode,
        request.steady_solver,
        request.transient_solver,
        checksum(run_configuration_identity(
            request, mode, artifact_ids)));
}

std::optional<RunConfigurationRevisionRecord>
ProjectService::get_run_configuration_revision(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string&
        run_configuration_revision_id) const {
    require_identity(identity);
    if (project_id.empty() ||
        run_configuration_revision_id.empty()) {
        throw ProjectRequestError(
            "project and run configuration revision IDs must "
            "not be empty");
    }
    return repository_->get_run_configuration_revision(
        identity.team_id,
        project_id,
        run_configuration_revision_id);
}

std::vector<RunConfigurationRevisionRecord>
ProjectService::list_run_configuration_revisions(
    const IdentityContext& identity,
    const std::string& project_id) const {
    require_identity(identity);
    if (project_id.empty()) {
        throw ProjectRequestError(
            "project ID must not be empty");
    }
    return repository_->list_run_configuration_revisions(
        identity.team_id, project_id);
}

std::optional<ResolvedRunConfiguration>
ProjectService::resolve_run_configuration(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string&
        run_configuration_revision_id) const {
    const auto configuration =
        get_run_configuration_revision(
            identity,
            project_id,
            run_configuration_revision_id);
    if (!configuration) {
        return std::nullopt;
    }
    const auto model_case = resolve_model_case(
        identity,
        project_id,
        configuration->model_revision_id,
        configuration->case_revision_id);
    const auto artifacts = resolve_artifact_revisions(
        identity,
        project_id,
        configuration->artifact_revision_ids);
    if (!model_case || !artifacts) {
        throw ProjectStateError(
            "persisted run configuration dependencies were "
            "not found");
    }
    return ResolvedRunConfiguration{
        *configuration,
        *model_case,
        *artifacts,
    };
}

}  // namespace thermox::service
