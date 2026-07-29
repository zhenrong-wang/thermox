#include "thermox/service/projects.hpp"

#include "serialization_internal.hpp"

#include "thermox/platform/model_document.hpp"

#include <openssl/evp.h>

#include <cctype>
#include <iomanip>
#include <memory>
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

}  // namespace

ProjectService::ProjectService(
    std::shared_ptr<ProjectRepository> repository)
    : repository_(std::move(repository)) {
    if (!repository_) {
        throw std::invalid_argument(
            "project repository must not be null");
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

}  // namespace thermox::service
