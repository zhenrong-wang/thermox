#include "thermox/service/in_memory_projects.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace thermox::service {

namespace {

class InMemoryProjectRepository final
    : public ProjectRepository {
public:
    ProjectRecord create_project(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& name,
        const std::string& description) override {
        std::lock_guard lock(mutex_);
        ProjectRecord record;
        record.project_id = next_id("project", next_project_id_++);
        record.team_id = team_id;
        record.name = name;
        record.description = description;
        record.created_by_user_id = created_by_user_id;
        record.created_at = std::chrono::system_clock::now();
        projects_.emplace(record.project_id, record);
        return record;
    }

    std::optional<ProjectRecord> get_project(
        const std::string& team_id,
        const std::string& project_id) const override {
        std::lock_guard lock(mutex_);
        const auto found = projects_.find(project_id);
        if (found == projects_.end() ||
            found->second.team_id != team_id) {
            return std::nullopt;
        }
        return found->second;
    }

    std::vector<ProjectRecord> list_projects(
        const std::string& team_id) const override {
        std::lock_guard lock(mutex_);
        std::vector<ProjectRecord> records;
        for (const auto& [id, record] : projects_) {
            (void)id;
            if (record.team_id == team_id) {
                records.push_back(record);
            }
        }
        std::sort(
            records.begin(),
            records.end(),
            [](const auto& left, const auto& right) {
                return left.created_at < right.created_at;
            });
        return records;
    }

    ModelRevisionRecord create_model_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& parent_model_revision_id,
        const std::string& model_schema_version,
        const std::string& model_id,
        const std::string& model_revision_label,
        const std::string& canonical_model_json,
        const std::string& checksum) override {
        std::lock_guard lock(mutex_);
        const auto project = projects_.find(project_id);
        if (project == projects_.end() ||
            project->second.team_id != team_id) {
            throw ProjectStateError("project was not found");
        }
        if (!parent_model_revision_id.empty()) {
            const auto parent =
                model_revisions_.find(parent_model_revision_id);
            if (parent == model_revisions_.end() ||
                parent->second.team_id != team_id ||
                parent->second.project_id != project_id) {
                throw ProjectStateError(
                    "parent model revision was not found");
            }
        }

        ModelRevisionRecord record;
        record.model_revision_id =
            next_id("model-revision", next_model_revision_id_++);
        record.project_id = project_id;
        record.team_id = team_id;
        record.revision_number =
            ++project_revision_sequences_[project_id];
        record.parent_model_revision_id =
            parent_model_revision_id;
        record.model_schema_version = model_schema_version;
        record.model_id = model_id;
        record.model_revision_label = model_revision_label;
        record.canonical_model_json = canonical_model_json;
        record.checksum = checksum;
        record.created_by_user_id = created_by_user_id;
        record.created_at = std::chrono::system_clock::now();
        model_revisions_.emplace(
            record.model_revision_id, record);
        return record;
    }

    std::optional<ModelRevisionRecord> get_model_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& model_revision_id) const override {
        std::lock_guard lock(mutex_);
        const auto found =
            model_revisions_.find(model_revision_id);
        if (found == model_revisions_.end() ||
            found->second.team_id != team_id ||
            found->second.project_id != project_id) {
            return std::nullopt;
        }
        return found->second;
    }

    std::vector<ModelRevisionRecord> list_model_revisions(
        const std::string& team_id,
        const std::string& project_id) const override {
        std::lock_guard lock(mutex_);
        const auto project = projects_.find(project_id);
        if (project == projects_.end() ||
            project->second.team_id != team_id) {
            return {};
        }
        std::vector<ModelRevisionRecord> records;
        for (const auto& [id, record] : model_revisions_) {
            (void)id;
            if (record.team_id == team_id &&
                record.project_id == project_id) {
                records.push_back(record);
            }
        }
        std::sort(
            records.begin(),
            records.end(),
            [](const auto& left, const auto& right) {
                return left.revision_number <
                    right.revision_number;
            });
        return records;
    }

    CaseRevisionRecord create_case_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& model_revision_id,
        const std::string& parent_case_revision_id,
        const std::string& case_id,
        const std::string& mode,
        const std::string& canonical_case_json,
        const std::string& checksum) override {
        std::lock_guard lock(mutex_);
        const auto model =
            model_revisions_.find(model_revision_id);
        if (model == model_revisions_.end() ||
            model->second.team_id != team_id ||
            model->second.project_id != project_id) {
            throw ProjectStateError(
                "model revision was not found");
        }
        if (!parent_case_revision_id.empty()) {
            const auto parent =
                case_revisions_.find(parent_case_revision_id);
            if (parent == case_revisions_.end() ||
                parent->second.team_id != team_id ||
                parent->second.project_id != project_id ||
                parent->second.model_revision_id !=
                    model_revision_id ||
                parent->second.case_id != case_id) {
                throw ProjectStateError(
                    "parent case revision was not found");
            }
        }

        const std::string sequence_key =
            model_revision_id + '\0' + case_id;
        CaseRevisionRecord record;
        record.case_revision_id =
            next_id("case-revision", next_case_revision_id_++);
        record.model_revision_id = model_revision_id;
        record.project_id = project_id;
        record.team_id = team_id;
        record.case_id = case_id;
        record.revision_number =
            ++case_revision_sequences_[sequence_key];
        record.parent_case_revision_id =
            parent_case_revision_id;
        record.mode = mode;
        record.canonical_case_json = canonical_case_json;
        record.checksum = checksum;
        record.created_by_user_id = created_by_user_id;
        record.created_at = std::chrono::system_clock::now();
        case_revisions_.emplace(record.case_revision_id, record);
        return record;
    }

    std::optional<CaseRevisionRecord> get_case_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& model_revision_id,
        const std::string& case_revision_id) const override {
        std::lock_guard lock(mutex_);
        const auto found =
            case_revisions_.find(case_revision_id);
        if (found == case_revisions_.end() ||
            found->second.team_id != team_id ||
            found->second.project_id != project_id ||
            found->second.model_revision_id !=
                model_revision_id) {
            return std::nullopt;
        }
        return found->second;
    }

    std::vector<CaseRevisionRecord> list_case_revisions(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& model_revision_id) const override {
        std::lock_guard lock(mutex_);
        const auto model =
            model_revisions_.find(model_revision_id);
        if (model == model_revisions_.end() ||
            model->second.team_id != team_id ||
            model->second.project_id != project_id) {
            return {};
        }
        std::vector<CaseRevisionRecord> records;
        for (const auto& [id, record] : case_revisions_) {
            (void)id;
            if (record.team_id == team_id &&
                record.project_id == project_id &&
                record.model_revision_id ==
                    model_revision_id) {
                records.push_back(record);
            }
        }
        std::sort(
            records.begin(),
            records.end(),
            [](const auto& left, const auto& right) {
                if (left.case_id != right.case_id) {
                    return left.case_id < right.case_id;
                }
                return left.revision_number <
                    right.revision_number;
            });
        return records;
    }

private:
    static std::string next_id(
        const std::string& prefix,
        std::uint64_t value) {
        std::ostringstream id;
        id << prefix << '-' << std::setfill('0')
           << std::setw(8) << value;
        return id.str();
    }

    mutable std::mutex mutex_;
    std::uint64_t next_project_id_{1};
    std::uint64_t next_model_revision_id_{1};
    std::uint64_t next_case_revision_id_{1};
    std::unordered_map<std::string, ProjectRecord> projects_;
    std::unordered_map<std::string, ModelRevisionRecord>
        model_revisions_;
    std::unordered_map<std::string, CaseRevisionRecord>
        case_revisions_;
    std::unordered_map<std::string, std::uint64_t>
        project_revision_sequences_;
    std::unordered_map<std::string, std::uint64_t>
        case_revision_sequences_;
};

}  // namespace

std::shared_ptr<ProjectRepository>
make_in_memory_project_repository() {
    return std::make_shared<InMemoryProjectRepository>();
}

}  // namespace thermox::service
