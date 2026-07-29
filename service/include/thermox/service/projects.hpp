#pragma once

#include "thermox/service/identity.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace thermox::service {

inline constexpr char project_schema_v1[] =
    "thermox.project/v1";
inline constexpr char model_revision_schema_v1[] =
    "thermox.model_revision/v1";

struct ProjectRecord {
    std::string schema_version{project_schema_v1};
    std::string project_id;
    std::string team_id;
    std::string name;
    std::string description;
    std::string created_by_user_id;
    std::chrono::system_clock::time_point created_at;
};

struct ModelRevisionRecord {
    std::string schema_version{model_revision_schema_v1};
    std::string model_revision_id;
    std::string project_id;
    std::string team_id;
    std::uint64_t revision_number{0};
    std::string parent_model_revision_id;
    std::string model_schema_version;
    std::string model_id;
    std::string model_revision_label;
    std::string canonical_model_json;
    std::string checksum;
    std::string created_by_user_id;
    std::chrono::system_clock::time_point created_at;
};

class ProjectRequestError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

class ProjectStateError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ProjectRepository {
public:
    virtual ~ProjectRepository() = default;

    virtual ProjectRecord create_project(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& name,
        const std::string& description) = 0;
    virtual std::optional<ProjectRecord> get_project(
        const std::string& team_id,
        const std::string& project_id) const = 0;
    virtual std::vector<ProjectRecord> list_projects(
        const std::string& team_id) const = 0;

    virtual ModelRevisionRecord create_model_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& parent_model_revision_id,
        const std::string& model_schema_version,
        const std::string& model_id,
        const std::string& model_revision_label,
        const std::string& canonical_model_json,
        const std::string& checksum) = 0;
    virtual std::optional<ModelRevisionRecord>
    get_model_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& model_revision_id) const = 0;
    virtual std::vector<ModelRevisionRecord>
    list_model_revisions(
        const std::string& team_id,
        const std::string& project_id) const = 0;
};

struct CreateProjectRequest {
    IdentityContext identity;
    std::string name;
    std::string description;
};

struct CreateModelRevisionRequest {
    IdentityContext identity;
    std::string project_id;
    std::string parent_model_revision_id;
    std::string model_json;
};

class ProjectService {
public:
    explicit ProjectService(
        std::shared_ptr<ProjectRepository> repository);

    [[nodiscard]] ProjectRecord create_project(
        const CreateProjectRequest& request) const;
    [[nodiscard]] std::optional<ProjectRecord> get_project(
        const IdentityContext& identity,
        const std::string& project_id) const;
    [[nodiscard]] std::vector<ProjectRecord> list_projects(
        const IdentityContext& identity) const;
    [[nodiscard]] ModelRevisionRecord create_model_revision(
        const CreateModelRevisionRequest& request) const;
    [[nodiscard]] std::optional<ModelRevisionRecord>
    get_model_revision(
        const IdentityContext& identity,
        const std::string& project_id,
        const std::string& model_revision_id) const;
    [[nodiscard]] std::vector<ModelRevisionRecord>
    list_model_revisions(
        const IdentityContext& identity,
        const std::string& project_id) const;

private:
    std::shared_ptr<ProjectRepository> repository_;
};

std::string serialize_project_json(
    const ProjectRecord& project);
std::string serialize_projects_json(
    const std::vector<ProjectRecord>& projects);
std::string serialize_model_revision_json(
    const ModelRevisionRecord& revision,
    bool include_model = true);
std::string serialize_model_revisions_json(
    const std::vector<ModelRevisionRecord>& revisions);

}  // namespace thermox::service
