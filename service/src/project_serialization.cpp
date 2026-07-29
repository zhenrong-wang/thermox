#include "thermox/service/projects.hpp"

#include <chrono>
#include <iomanip>
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

void artifact_revision_json(
    std::ostringstream& out,
    const ArtifactRevisionRecord& revision) {
    out << "{\"schema_version\": ";
    json_string(out, revision.schema_version);
    out << ", \"artifact_revision_id\": ";
    json_string(out, revision.artifact_revision_id);
    out << ", \"project_id\": ";
    json_string(out, revision.project_id);
    out << ", \"team_id\": ";
    json_string(out, revision.team_id);
    out << ", \"artifact_id\": ";
    json_string(out, revision.artifact_id);
    out << ", \"revision_number\": "
        << revision.revision_number;
    out << ", \"parent_artifact_revision_id\": ";
    json_string(out, revision.parent_artifact_revision_id);
    out << ", \"artifact_type\": ";
    json_string(out, revision.artifact_type);
    out << ", \"artifact_schema_version\": ";
    json_string(out, revision.artifact_schema_version);
    out << ", \"content\": {\"media_type\": ";
    json_string(out, revision.content.media_type);
    out << ", \"byte_size\": "
        << revision.content.byte_size;
    out << ", \"checksum\": ";
    json_string(out, revision.content.checksum);
    out << "}, \"created_by_user_id\": ";
    json_string(out, revision.created_by_user_id);
    out << ", \"created_at_epoch_ms\": "
        << epoch_milliseconds(revision.created_at) << '}';
}

void steady_solver_json(
    std::ostringstream& out,
    const SteadySolverSettings& solver) {
    out << "{\"max_iterations\": "
        << solver.max_iterations
        << ", \"residual_tolerance\": "
        << solver.residual_tolerance
        << ", \"step_tolerance\": "
        << solver.step_tolerance
        << ", \"finite_difference_epsilon\": "
        << solver.finite_difference_epsilon
        << ", \"min_damping\": "
        << solver.min_damping
        << ", \"damping_reduction\": "
        << solver.damping_reduction
        << ", \"sufficient_decrease\": "
        << solver.sufficient_decrease
        << ", \"max_line_search_steps\": "
        << solver.max_line_search_steps << '}';
}

void transient_solver_json(
    std::ostringstream& out,
    const TransientSolverSettings& solver) {
    out << "{\"start_time\": " << solver.start_time
        << ", \"end_time\": " << solver.end_time
        << ", \"initial_step\": " << solver.initial_step
        << ", \"min_step\": " << solver.min_step
        << ", \"max_step\": " << solver.max_step
        << ", \"absolute_tolerance\": "
        << solver.absolute_tolerance
        << ", \"relative_tolerance\": "
        << solver.relative_tolerance
        << ", \"max_steps\": " << solver.max_steps
        << ", \"max_consecutive_rejections\": "
        << solver.max_consecutive_rejections
        << ", \"compute_consistent_initial_conditions\": "
        << (solver.compute_consistent_initial_conditions
                ? "true"
                : "false")
        << ", \"nonlinear_solver\": ";
    steady_solver_json(out, solver.nonlinear_solver);
    out << '}';
}

void run_configuration_revision_json(
    std::ostringstream& out,
    const RunConfigurationRevisionRecord& revision) {
    out << "{\"schema_version\": ";
    json_string(out, revision.schema_version);
    out << ", \"run_configuration_revision_id\": ";
    json_string(out, revision.run_configuration_revision_id);
    out << ", \"run_configuration_id\": ";
    json_string(out, revision.run_configuration_id);
    out << ", \"project_id\": ";
    json_string(out, revision.project_id);
    out << ", \"team_id\": ";
    json_string(out, revision.team_id);
    out << ", \"revision_number\": "
        << revision.revision_number;
    out << ", \"parent_run_configuration_revision_id\": ";
    json_string(
        out,
        revision.parent_run_configuration_revision_id);
    out << ", \"model_revision_id\": ";
    json_string(out, revision.model_revision_id);
    out << ", \"case_revision_id\": ";
    json_string(out, revision.case_revision_id);
    out << ", \"artifact_revision_ids\": [";
    for (std::size_t index = 0;
         index < revision.artifact_revision_ids.size();
         ++index) {
        if (index != 0U) {
            out << ", ";
        }
        json_string(out, revision.artifact_revision_ids[index]);
    }
    out << "], \"mode\": ";
    json_string(out, revision.mode);
    out << ", \"steady_solver\": ";
    steady_solver_json(out, revision.steady_solver);
    out << ", \"transient_solver\": ";
    transient_solver_json(out, revision.transient_solver);
    out << ", \"checksum\": ";
    json_string(out, revision.checksum);
    out << ", \"created_by_user_id\": ";
    json_string(out, revision.created_by_user_id);
    out << ", \"created_at_epoch_ms\": "
        << epoch_milliseconds(revision.created_at) << '}';
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

std::string serialize_artifact_revision_json(
    const ArtifactRevisionRecord& revision) {
    std::ostringstream out;
    artifact_revision_json(out, revision);
    out << '\n';
    return out.str();
}

std::string serialize_artifact_revisions_json(
    const std::vector<ArtifactRevisionRecord>& revisions) {
    std::ostringstream out;
    out << "{\"schema_version\": "
           "\"thermox.artifact_revision_list/v1\", "
        << "\"artifact_revisions\": [";
    for (std::size_t index = 0; index < revisions.size();
         ++index) {
        if (index != 0U) {
            out << ", ";
        }
        artifact_revision_json(out, revisions[index]);
    }
    out << "]}\n";
    return out.str();
}

std::string serialize_run_configuration_revision_json(
    const RunConfigurationRevisionRecord& revision) {
    std::ostringstream out;
    out << std::setprecision(17);
    run_configuration_revision_json(out, revision);
    out << '\n';
    return out.str();
}

std::string serialize_run_configuration_revisions_json(
    const std::vector<RunConfigurationRevisionRecord>&
        revisions) {
    std::ostringstream out;
    out << std::setprecision(17);
    out << "{\"schema_version\": "
           "\"thermox.run_configuration_revision_list/v1\", "
        << "\"run_configuration_revisions\": [";
    for (std::size_t index = 0; index < revisions.size();
         ++index) {
        if (index != 0U) {
            out << ", ";
        }
        run_configuration_revision_json(
            out, revisions[index]);
    }
    out << "]}\n";
    return out.str();
}

}  // namespace thermox::service
