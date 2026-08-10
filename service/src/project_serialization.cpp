#include "thermox/service/projects.hpp"
#include "thermox/service/serialization.hpp"

#include <chrono>
#include <cmath>
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

void performance_map_quality_review_json(
    std::ostringstream& out,
    const PerformanceMapQualityReviewRecord& review) {
    out << "{\"schema_version\": ";
    json_string(out, review.schema_version);
    out << ", \"review_id\": ";
    json_string(out, review.review_id);
    out << ", \"project_id\": ";
    json_string(out, review.project_id);
    out << ", \"team_id\": ";
    json_string(out, review.team_id);
    out << ", \"artifact_revision_id\": ";
    json_string(out, review.artifact_revision_id);
    out << ", \"artifact_checksum\": ";
    json_string(out, review.artifact_checksum);
    out << ", \"supersedes_review_id\": ";
    json_string(out, review.supersedes_review_id);
    out << ", \"disposition\": ";
    json_string(out, to_string(review.disposition));
    out << ", \"reviewed_scope\": ";
    json_string(out, review.reviewed_scope);
    out << ", \"rationale\": ";
    json_string(out, review.rationale);
    out << ", \"quality_schema_version\": ";
    json_string(out, review.quality_schema_version);
    out << ", \"quality_snapshot_checksum\": ";
    json_string(out, review.quality_snapshot_checksum);
    out << ", \"quality_snapshot\": "
        << review.quality_snapshot_json;
    out << ", \"created_by_user_id\": ";
    json_string(out, review.created_by_user_id);
    out << ", \"created_at_epoch_ms\": "
        << epoch_milliseconds(review.created_at) << '}';
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
        << ", \"linear_residual_tolerance\": "
        << solver.linear_residual_tolerance
        << ", \"finite_difference_epsilon\": "
        << solver.finite_difference_epsilon
        << ", \"min_damping\": "
        << solver.min_damping
        << ", \"damping_reduction\": "
        << solver.damping_reduction
        << ", \"sufficient_decrease\": "
        << solver.sufficient_decrease
        << ", \"max_line_search_steps\": "
        << solver.max_line_search_steps
        << ", \"continuation_enabled\": "
        << (solver.continuation_enabled ? "true" : "false")
        << ", \"continuation_initial_step\": "
        << solver.continuation_initial_step
        << ", \"continuation_minimum_step\": "
        << solver.continuation_minimum_step
        << ", \"continuation_step_growth\": "
        << solver.continuation_step_growth
        << ", \"continuation_step_reduction\": "
        << solver.continuation_step_reduction
        << ", \"continuation_maximum_stages\": "
        << solver.continuation_maximum_stages << '}';
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
        << ", \"maximum_order\": "
        << solver.maximum_order
        << ", \"compute_consistent_initial_conditions\": "
        << (solver.compute_consistent_initial_conditions
                ? "true"
                : "false")
        << ", \"nonlinear_solver\": ";
    steady_solver_json(out, solver.nonlinear_solver);
    out << '}';
}

void json_number(std::ostringstream& out, double value) {
    if (std::isfinite(value)) {
        out << value;
    } else {
        out << "null";
    }
}

void component_type_json(
    std::ostringstream& out,
    const ComponentType& component) {
    out << "{\"kind\": ";
    json_string(out, component.kind);
    out << ", \"version\": ";
    json_string(out, component.version);
    out << ", \"template_kind\": ";
    json_string(out, component.template_kind);
    out << ", \"display_name\": ";
    json_string(out, component.display_name);
    out << ", \"category\": ";
    json_string(out, component.category);
    out << ", \"model_name\": ";
    json_string(out, component.model_name);
    out << ", \"system_boundary_role\": ";
    json_string(out, component.system_boundary_role);
    out << ", \"supports_steady\": "
        << (component.supports_steady ? "true" : "false")
        << ", \"supports_transient\": "
        << (component.supports_transient ? "true" : "false")
        << ", \"ports\": [";
    for (std::size_t index = 0;
         index < component.ports.size(); ++index) {
        if (index != 0U) out << ", ";
        const auto& port = component.ports[index];
        out << "{\"name\": ";
        json_string(out, port.name);
        out << ", \"domain\": ";
        json_string(out, port.domain);
        out << ", \"direction\": ";
        json_string(out, port.direction);
        out << ", \"maximum_connections\": "
            << port.maximum_connections << '}';
    }
    out << "], \"parameters\": [";
    for (std::size_t index = 0;
         index < component.parameters.size(); ++index) {
        if (index != 0U) out << ", ";
        const auto& parameter = component.parameters[index];
        out << "{\"name\": ";
        json_string(out, parameter.name);
        out << ", \"dimension\": ";
        json_string(out, parameter.dimension);
        out << ", \"required\": "
            << (parameter.required ? "true" : "false")
            << ", \"default_value_si\": ";
        if (parameter.has_default) {
            json_number(out, parameter.default_value_si);
        } else {
            out << "null";
        }
        out << ", \"lower_bound\": ";
        json_number(out, parameter.lower_bound);
        out << ", \"upper_bound\": ";
        json_number(out, parameter.upper_bound);
        out << ", \"lower_inclusive\": "
            << (parameter.lower_inclusive ? "true" : "false")
            << ", \"upper_inclusive\": "
            << (parameter.upper_inclusive ? "true" : "false")
            << '}';
    }
    out << "], \"artifacts\": [";
    for (std::size_t index = 0;
         index < component.artifacts.size(); ++index) {
        if (index != 0U) out << ", ";
        const auto& artifact = component.artifacts[index];
        out << "{\"role\": ";
        json_string(out, artifact.role);
        out << ", \"artifact_type\": ";
        json_string(out, artifact.artifact_type);
        out << ", \"required\": "
            << (artifact.required ? "true" : "false") << '}';
    }
    out << "], \"internal_variables\": [";
    for (std::size_t index = 0;
         index < component.internal_variables.size(); ++index) {
        if (index != 0U) out << ", ";
        const auto& variable =
            component.internal_variables[index];
        out << "{\"name\": ";
        json_string(out, variable.name);
        out << ", \"dimension\": ";
        json_string(out, variable.dimension);
        out << ", \"kind\": ";
        json_string(out, variable.kind);
        out << '}';
    }
    const auto strings =
        [&](const char* name,
            const std::vector<std::string>& values) {
            out << "], \"" << name << "\": [";
            for (std::size_t index = 0;
                 index < values.size(); ++index) {
                if (index != 0U) out << ", ";
                json_string(out, values[index]);
            }
        };
    strings(
        "required_property_capabilities",
        component.required_property_capabilities);
    strings(
        "required_thermochemistry_capabilities",
        component.required_thermochemistry_capabilities);
    out << "]}";
}

void expression_component_json(
    std::ostringstream& out,
    const ExpressionComponentInput& definition) {
    out << "{\"schema_version\": ";
    json_string(out, definition.schema_version);
    out << ", \"kind\": ";
    json_string(out, definition.kind);
    out << ", \"version\": ";
    json_string(out, definition.version);
    out << ", \"template_kind\": ";
    json_string(out, definition.template_kind);
    out << ", \"display_name\": ";
    json_string(out, definition.display_name);
    out << ", \"category\": ";
    json_string(out, definition.category);
    out << ", \"model_name\": ";
    json_string(out, definition.model_name);
    out << ", \"system_boundary_role\": ";
    json_string(out, definition.system_boundary_role);
    out << ", \"supports_steady\": "
        << (definition.supports_steady ? "true" : "false");
    out << ", \"supports_transient\": "
        << (definition.supports_transient ? "true" : "false");
    out << ", \"ports\": [";
    for (std::size_t index = 0;
         index < definition.ports.size(); ++index) {
        if (index != 0U) out << ", ";
        const auto& port = definition.ports[index];
        out << "{\"name\": ";
        json_string(out, port.name);
        out << ", \"domain\": ";
        json_string(out, port.domain);
        out << ", \"direction\": ";
        json_string(out, port.direction);
        out << ", \"maximum_connections\": "
            << port.maximum_connections << '}';
    }
    out << "], \"parameters\": [";
    for (std::size_t index = 0;
         index < definition.parameters.size(); ++index) {
        if (index != 0U) out << ", ";
        const auto& parameter = definition.parameters[index];
        out << "{\"name\": ";
        json_string(out, parameter.name);
        out << ", \"dimension\": ";
        json_string(out, parameter.dimension);
        out << ", \"required\": "
            << (parameter.required ? "true" : "false")
            << ", \"default_value_si\": ";
        if (parameter.default_value_si) {
            json_number(out, *parameter.default_value_si);
        } else {
            out << "null";
        }
        out << ", \"lower_bound\": ";
        json_number(out, parameter.lower_bound);
        out << ", \"upper_bound\": ";
        json_number(out, parameter.upper_bound);
        out << ", \"lower_inclusive\": "
            << (parameter.lower_inclusive ? "true" : "false")
            << ", \"upper_inclusive\": "
            << (parameter.upper_inclusive ? "true" : "false")
            << '}';
    }
    out << "], \"equations\": [";
    for (std::size_t index = 0;
         index < definition.equations.size(); ++index) {
        if (index != 0U) out << ", ";
        const auto& equation = definition.equations[index];
        out << "{\"name\": ";
        json_string(out, equation.name);
        out << ", \"expression\": ";
        json_string(out, equation.expression);
        out << ", \"residual_scale\": ";
        json_number(out, equation.residual_scale);
        out << '}';
    }
    out << "], \"transient_variables\": [";
    for (std::size_t index = 0;
         index < definition.transient_variables.size(); ++index) {
        if (index != 0U) out << ", ";
        const auto& variable = definition.transient_variables[index];
        out << "{\"port_name\": ";
        json_string(out, variable.port_name);
        out << ", \"variable_name\": ";
        json_string(out, variable.variable_name);
        out << ", \"kind\": ";
        json_string(out, variable.kind);
        out << ", \"derivative_scale\": ";
        json_number(out, variable.derivative_scale);
        out << '}';
    }
    out << "], \"internal_variables\": [";
    for (std::size_t index = 0;
         index < definition.internal_variables.size(); ++index) {
        if (index != 0U) out << ", ";
        const auto& variable = definition.internal_variables[index];
        out << "{\"name\": ";
        json_string(out, variable.name);
        out << ", \"kind\": ";
        json_string(out, variable.kind);
        out << ", \"initial_value_si\": ";
        json_number(out, variable.initial_value_si);
        out << ", \"state_scale\": ";
        json_number(out, variable.state_scale);
        out << ", \"initial_derivative_si_s\": ";
        json_number(out, variable.initial_derivative_si_s);
        out << ", \"derivative_scale\": ";
        json_number(out, variable.derivative_scale);
        out << ", \"lower_bound\": ";
        json_number(out, variable.lower_bound);
        out << ", \"upper_bound\": ";
        json_number(out, variable.upper_bound);
        out << ", \"dimension\": ";
        json_string(out, variable.dimension);
        out << '}';
    }
    out << "], \"transient_equations\": [";
    for (std::size_t index = 0;
         index < definition.transient_equations.size(); ++index) {
        if (index != 0U) out << ", ";
        const auto& equation = definition.transient_equations[index];
        out << "{\"name\": ";
        json_string(out, equation.name);
        out << ", \"expression\": ";
        json_string(out, equation.expression);
        out << ", \"residual_scale\": ";
        json_number(out, equation.residual_scale);
        out << '}';
    }
    out << "]}";
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
    out << ", \"study_revision_id\": ";
    json_string(out, revision.study_revision_id);
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

void study_revision_json(
    std::ostringstream& out,
    const StudyRevisionRecord& revision) {
    out << "{\"schema_version\": ";
    json_string(out, revision.schema_version);
    out << ", \"study_revision_id\": ";
    json_string(out, revision.study_revision_id);
    out << ", \"study_id\": ";
    json_string(out, revision.study_id);
    out << ", \"project_id\": ";
    json_string(out, revision.project_id);
    out << ", \"team_id\": ";
    json_string(out, revision.team_id);
    out << ", \"revision_number\": "
        << revision.revision_number;
    out << ", \"parent_study_revision_id\": ";
    json_string(out, revision.parent_study_revision_id);
    out << ", \"model_revision_id\": ";
    json_string(out, revision.model_revision_id);
    out << ", \"case_revision_id\": ";
    json_string(out, revision.case_revision_id);
    out << ", \"intent\": ";
    json_string(out, revision.intent);
    out << ", \"artifact_revision_ids\": [";
    for (std::size_t index = 0;
         index < revision.artifact_revision_ids.size(); ++index) {
        if (index != 0U) out << ", ";
        json_string(out, revision.artifact_revision_ids[index]);
    }
    out << "], \"artifact_qualification_requirements\": [";
    for (std::size_t index = 0;
         index < revision.artifact_qualification_requirements.size();
         ++index) {
        if (index != 0U) out << ", ";
        const auto& requirement =
            revision.artifact_qualification_requirements[index];
        out << "{\"artifact_revision_id\": ";
        json_string(out, requirement.artifact_revision_id);
        out << ", \"review_id\": ";
        json_string(out, requirement.review_id);
        out << ", \"acceptable_dispositions\": [";
        for (std::size_t disposition_index = 0;
             disposition_index <
                 requirement.acceptable_dispositions.size();
             ++disposition_index) {
            if (disposition_index != 0U) out << ", ";
            json_string(
                out,
                to_string(requirement.acceptable_dispositions[
                    disposition_index]));
        }
        out << "]}";
    }
    out << "], \"artifact_operating_envelopes\": [";
    for (std::size_t index = 0;
         index < revision.artifact_operating_envelopes.size(); ++index) {
        if (index != 0U) out << ", ";
        const auto& envelope = revision.artifact_operating_envelopes[index];
        out << "{\"artifact_revision_id\": ";
        json_string(out, envelope.artifact_revision_id);
        out << ", \"coordinates\": [";
        for (std::size_t coordinate_index = 0;
             coordinate_index < envelope.coordinates.size();
             ++coordinate_index) {
            if (coordinate_index != 0U) out << ", ";
            const auto& coordinate = envelope.coordinates[coordinate_index];
            out << "{\"coordinate\": ";
            json_string(out, coordinate.coordinate);
            out << ", \"dimension\": ";
            json_string(out, coordinate.dimension);
            out << ", \"minimum\": ";
            if (coordinate.minimum) out << *coordinate.minimum;
            else out << "null";
            out << ", \"maximum\": ";
            if (coordinate.maximum) out << *coordinate.maximum;
            else out << "null";
            out << ", \"minimum_inclusive\": "
                << (coordinate.minimum_inclusive ? "true" : "false")
                << ", \"maximum_inclusive\": "
                << (coordinate.maximum_inclusive ? "true" : "false")
                << '}';
        }
        out << "]}";
    }
    out << "], \"result_projections\": [";
    for (std::size_t index = 0;
         index < revision.result_projections.size(); ++index) {
        if (index != 0U) out << ", ";
        const auto& projection = revision.result_projections[index];
        out << "{\"id\": ";
        json_string(out, projection.id);
        out << ", \"scope\": ";
        json_string(out, to_string(projection.scope));
        out << ", \"component_id\": ";
        json_string(out, projection.component_id);
        out << ", \"port_name\": ";
        json_string(out, projection.port_name);
        out << ", \"value_name\": ";
        json_string(out, projection.value_name);
        out << ", \"dimension\": ";
        json_string(out, projection.dimension);
        out << ", \"aggregation\": ";
        json_string(out, to_string(projection.aggregation));
        out << '}';
    }
    out << "], \"acceptance_criteria\": [";
    for (std::size_t index = 0;
         index < revision.acceptance_criteria.size(); ++index) {
        if (index != 0U) out << ", ";
        const auto& criterion = revision.acceptance_criteria[index];
        out << "{\"id\": ";
        json_string(out, criterion.id);
        out << ", \"projection_id\": ";
        json_string(out, criterion.projection_id);
        out << ", \"dimension\": ";
        json_string(out, criterion.dimension);
        out << ", \"lower_bound_si\": ";
        if (criterion.lower_bound_si) {
            out << *criterion.lower_bound_si;
        } else {
            out << "null";
        }
        out << ", \"upper_bound_si\": ";
        if (criterion.upper_bound_si) {
            out << *criterion.upper_bound_si;
        } else {
            out << "null";
        }
        out << ", \"lower_inclusive\": "
            << (criterion.lower_inclusive ? "true" : "false")
            << ", \"upper_inclusive\": "
            << (criterion.upper_inclusive ? "true" : "false")
            << '}';
    }
    out << "], \"checksum\": ";
    json_string(out, revision.checksum);
    out << ", \"created_by_user_id\": ";
    json_string(out, revision.created_by_user_id);
    out << ", \"created_at_epoch_ms\": "
        << epoch_milliseconds(revision.created_at) << '}';
}

void calibration_solver_json(
    std::ostringstream& out,
    const CalibrationSolverSettings& solver) {
    out << "{\"max_iterations\": " << solver.max_iterations
        << ", \"initial_step_fraction\": "
        << solver.initial_step_fraction
        << ", \"minimum_step_fraction\": "
        << solver.minimum_step_fraction
        << ", \"step_reduction\": " << solver.step_reduction
        << ", \"minimum_continuation_fraction\": "
        << solver.minimum_continuation_fraction
        << ", \"continuation_growth\": "
        << solver.continuation_growth
        << ", \"simulation_solver\": ";
    steady_solver_json(out, solver.simulation_solver);
    out << '}';
}

void calibration_revision_json(
    std::ostringstream& out,
    const CalibrationRevisionRecord& revision) {
    out << "{\"schema_version\": ";
    json_string(out, revision.schema_version);
    out << ", \"calibration_revision_id\": ";
    json_string(out, revision.calibration_revision_id);
    out << ", \"calibration_id\": ";
    json_string(out, revision.calibration_id);
    out << ", \"project_id\": ";
    json_string(out, revision.project_id);
    out << ", \"team_id\": ";
    json_string(out, revision.team_id);
    out << ", \"revision_number\": " << revision.revision_number;
    out << ", \"parent_calibration_revision_id\": ";
    json_string(out, revision.parent_calibration_revision_id);
    out << ", \"model_revision_id\": ";
    json_string(out, revision.model_revision_id);
    const auto ids = [&](const char* name,
                         const std::vector<std::string>& values) {
        out << ", \"" << name << "\": [";
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index != 0U) out << ", ";
            json_string(out, values[index]);
        }
        out << ']';
    };
    ids("training_study_revision_ids",
        revision.training_study_revision_ids);
    ids("validation_study_revision_ids",
        revision.validation_study_revision_ids);
    out << ", \"definition\": " << revision.definition_json;
    out << ", \"solver\": ";
    calibration_solver_json(out, revision.solver);
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

std::string serialize_artifact_revision_content_json(
    const ArtifactRevisionContent& content) {
    std::ostringstream out;
    out << "{\"schema_version\": ";
    json_string(out, content.schema_version);
    out << ", \"revision\": ";
    artifact_revision_json(out, content.revision);
    out << ", \"artifact\": "
        << content.canonical_artifact_json << "}\n";
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

std::string serialize_performance_map_quality_review_json(
    const PerformanceMapQualityReviewRecord& review) {
    std::ostringstream out;
    out << std::setprecision(17);
    performance_map_quality_review_json(out, review);
    out << '\n';
    return out.str();
}

std::string serialize_performance_map_quality_reviews_json(
    const std::vector<PerformanceMapQualityReviewRecord>& reviews) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"schema_version\": "
           "\"thermox.performance_map_quality_review_list/v1\", "
           "\"reviews\": [";
    for (std::size_t index = 0; index < reviews.size(); ++index) {
        if (index != 0U) out << ", ";
        performance_map_quality_review_json(out, reviews[index]);
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

std::string serialize_study_revision_json(
    const StudyRevisionRecord& revision) {
    std::ostringstream out;
    study_revision_json(out, revision);
    out << '\n';
    return out.str();
}

std::string serialize_study_revisions_json(
    const std::vector<StudyRevisionRecord>& revisions) {
    std::ostringstream out;
    out << "{\"schema_version\": "
           "\"thermox.study_revision_list/v1\", "
        << "\"study_revisions\": [";
    for (std::size_t index = 0; index < revisions.size();
         ++index) {
        if (index != 0U) out << ", ";
        study_revision_json(out, revisions[index]);
    }
    out << "]}\n";
    return out.str();
}

std::string serialize_calibration_revision_json(
    const CalibrationRevisionRecord& revision) {
    std::ostringstream out;
    out << std::setprecision(17);
    calibration_revision_json(out, revision);
    out << '\n';
    return out.str();
}

std::string serialize_calibration_revisions_json(
    const std::vector<CalibrationRevisionRecord>& revisions) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"schema_version\": "
           "\"thermox.calibration_revision_list/v1\", "
           "\"calibration_revisions\": [";
    for (std::size_t index = 0; index < revisions.size(); ++index) {
        if (index != 0U) out << ", ";
        calibration_revision_json(out, revisions[index]);
    }
    out << "]}\n";
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

std::string serialize_project_model_validation_json(
    const ProjectModelValidationResponse& response) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": ";
    json_string(out, response.schema_version);
    out << ",\n  \"project_id\": ";
    json_string(out, response.project_id);
    out << ",\n  \"model_revision_id\": ";
    json_string(out, response.model_revision_id);
    out << ",\n  \"model_checksum\": ";
    json_string(out, response.model_checksum);
    out << ",\n  \"case_revision_id\": ";
    json_string(out, response.case_revision_id);
    out << ",\n  \"case_checksum\": ";
    json_string(out, response.case_checksum);
    out << ",\n  \"artifact_revisions\": [";
    for (std::size_t index = 0;
         index < response.artifact_revisions.size();
         ++index) {
        if (index != 0U) out << ", ";
        artifact_revision_json(
            out, response.artifact_revisions[index]);
    }
    out << "],\n  \"validation\": "
        << serialize_validate_response_json(response.validation)
        << "}\n";
    return out.str();
}

std::string serialize_project_component_catalog_json(
    const ProjectComponentCatalogResponse& response) {
    std::ostringstream out;
    out << std::setprecision(17);
    out << "{\"schema_version\": ";
    json_string(out, response.schema_version);
    out << ", \"project_id\": ";
    json_string(out, response.project_id);
    out << ", \"components\": [";
    for (std::size_t index = 0;
         index < response.components.size(); ++index) {
        if (index != 0U) out << ", ";
        const auto& entry = response.components[index];
        out << "{\"source\": ";
        artifact_revision_json(out, entry.source);
        out << ", \"catalog_fingerprint\": ";
        json_string(out, entry.catalog_fingerprint);
        out << ", \"component\": ";
        component_type_json(out, entry.component);
        out << ", \"definition\": ";
        expression_component_json(out, entry.definition);
        out << '}';
    }
    out << "]}\n";
    return out.str();
}

}  // namespace thermox::service
