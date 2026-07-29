#include "thermox/service/projects.hpp"

#include <chrono>
#include <sstream>

namespace thermox::service {

namespace {

void json_string(
    std::ostringstream& out,
    const std::string& value) {
    out << '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (character < 0x20U) {
                constexpr char digits[] = "0123456789abcdef";
                out << "\\u00"
                    << digits[(character >> 4U) & 0x0fU]
                    << digits[character & 0x0fU];
            } else {
                out << static_cast<char>(character);
            }
        }
    }
    out << '"';
}

std::int64_t epoch_milliseconds(
    std::chrono::system_clock::time_point time) {
    return std::chrono::duration_cast<
        std::chrono::milliseconds>(
        time.time_since_epoch()).count();
}

void project_json(
    std::ostringstream& out,
    const ProjectRecord& project) {
    out << "{\"schema_version\": ";
    json_string(out, project.schema_version);
    out << ", \"project_id\": ";
    json_string(out, project.project_id);
    out << ", \"team_id\": ";
    json_string(out, project.team_id);
    out << ", \"name\": ";
    json_string(out, project.name);
    out << ", \"description\": ";
    json_string(out, project.description);
    out << ", \"created_by_user_id\": ";
    json_string(out, project.created_by_user_id);
    out << ", \"created_at_epoch_ms\": "
        << epoch_milliseconds(project.created_at) << '}';
}

void model_revision_json(
    std::ostringstream& out,
    const ModelRevisionRecord& revision,
    bool include_model) {
    out << "{\"schema_version\": ";
    json_string(out, revision.schema_version);
    out << ", \"model_revision_id\": ";
    json_string(out, revision.model_revision_id);
    out << ", \"project_id\": ";
    json_string(out, revision.project_id);
    out << ", \"team_id\": ";
    json_string(out, revision.team_id);
    out << ", \"revision_number\": "
        << revision.revision_number;
    out << ", \"parent_model_revision_id\": ";
    json_string(out, revision.parent_model_revision_id);
    out << ", \"model_schema_version\": ";
    json_string(out, revision.model_schema_version);
    out << ", \"model_id\": ";
    json_string(out, revision.model_id);
    out << ", \"model_revision_label\": ";
    json_string(out, revision.model_revision_label);
    out << ", \"checksum\": ";
    json_string(out, revision.checksum);
    out << ", \"created_by_user_id\": ";
    json_string(out, revision.created_by_user_id);
    out << ", \"created_at_epoch_ms\": "
        << epoch_milliseconds(revision.created_at);
    if (include_model) {
        out << ", \"model\": "
            << revision.canonical_model_json;
    }
    out << '}';
}

void case_revision_json(
    std::ostringstream& out,
    const CaseRevisionRecord& revision,
    bool include_case) {
    out << "{\"schema_version\": ";
    json_string(out, revision.schema_version);
    out << ", \"case_revision_id\": ";
    json_string(out, revision.case_revision_id);
    out << ", \"model_revision_id\": ";
    json_string(out, revision.model_revision_id);
    out << ", \"project_id\": ";
    json_string(out, revision.project_id);
    out << ", \"team_id\": ";
    json_string(out, revision.team_id);
    out << ", \"case_id\": ";
    json_string(out, revision.case_id);
    out << ", \"revision_number\": "
        << revision.revision_number;
    out << ", \"parent_case_revision_id\": ";
    json_string(out, revision.parent_case_revision_id);
    out << ", \"mode\": ";
    json_string(out, revision.mode);
    out << ", \"checksum\": ";
    json_string(out, revision.checksum);
    out << ", \"created_by_user_id\": ";
    json_string(out, revision.created_by_user_id);
    out << ", \"created_at_epoch_ms\": "
        << epoch_milliseconds(revision.created_at);
    if (include_case) {
        out << ", \"case_document\": "
            << revision.canonical_case_json;
    }
    out << '}';
}

}  // namespace

std::string serialize_project_json(
    const ProjectRecord& project) {
    std::ostringstream out;
    project_json(out, project);
    out << '\n';
    return out.str();
}

std::string serialize_projects_json(
    const std::vector<ProjectRecord>& projects) {
    std::ostringstream out;
    out << "{\"schema_version\": \"thermox.project_list/v1\", "
        << "\"projects\": [";
    for (std::size_t index = 0; index < projects.size();
         ++index) {
        if (index != 0U) {
            out << ", ";
        }
        project_json(out, projects[index]);
    }
    out << "]}\n";
    return out.str();
}

std::string serialize_model_revision_json(
    const ModelRevisionRecord& revision,
    bool include_model) {
    std::ostringstream out;
    model_revision_json(out, revision, include_model);
    out << '\n';
    return out.str();
}

std::string serialize_model_revisions_json(
    const std::vector<ModelRevisionRecord>& revisions) {
    std::ostringstream out;
    out << "{\"schema_version\": "
           "\"thermox.model_revision_list/v1\", "
        << "\"model_revisions\": [";
    for (std::size_t index = 0; index < revisions.size();
         ++index) {
        if (index != 0U) {
            out << ", ";
        }
        model_revision_json(out, revisions[index], false);
    }
    out << "]}\n";
    return out.str();
}

std::string serialize_case_revision_json(
    const CaseRevisionRecord& revision,
    bool include_case) {
    std::ostringstream out;
    case_revision_json(out, revision, include_case);
    out << '\n';
    return out.str();
}

std::string serialize_case_revisions_json(
    const std::vector<CaseRevisionRecord>& revisions) {
    std::ostringstream out;
    out << "{\"schema_version\": "
           "\"thermox.case_revision_list/v1\", "
        << "\"case_revisions\": [";
    for (std::size_t index = 0; index < revisions.size();
         ++index) {
        if (index != 0U) {
            out << ", ";
        }
        case_revision_json(out, revisions[index], false);
    }
    out << "]}\n";
    return out.str();
}

}  // namespace thermox::service
