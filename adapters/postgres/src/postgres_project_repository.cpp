#include "thermox/postgres/postgres_project_repository.hpp"

#define BOOST_BIND_GLOBAL_PLACEHOLDERS
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <libpq-fe.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace thermox::postgres {

namespace {

struct ConnectionDeleter {
    void operator()(PGconn* connection) const {
        if (connection != nullptr) {
            PQfinish(connection);
        }
    }
};

struct ResultDeleter {
    void operator()(PGresult* result) const {
        if (result != nullptr) {
            PQclear(result);
        }
    }
};

using Connection = std::unique_ptr<PGconn, ConnectionDeleter>;
using Result = std::unique_ptr<PGresult, ResultDeleter>;
using Tree = boost::property_tree::ptree;

std::string write_tree(const Tree& tree) {
    std::ostringstream output;
    boost::property_tree::write_json(output, tree, false);
    return output.str();
}

Tree read_tree(const std::string& payload) {
    std::istringstream input(payload);
    Tree tree;
    boost::property_tree::read_json(input, tree);
    return tree;
}

Tree steady_solver_tree(
    const service::SteadySolverSettings& value) {
    Tree tree;
    tree.put("max_iterations", value.max_iterations);
    tree.put("residual_tolerance", value.residual_tolerance);
    tree.put("step_tolerance", value.step_tolerance);
    tree.put(
        "linear_residual_tolerance",
        value.linear_residual_tolerance);
    tree.put(
        "finite_difference_epsilon",
        value.finite_difference_epsilon);
    tree.put("min_damping", value.min_damping);
    tree.put("damping_reduction", value.damping_reduction);
    tree.put(
        "sufficient_decrease", value.sufficient_decrease);
    tree.put(
        "max_line_search_steps",
        value.max_line_search_steps);
    tree.put(
        "continuation_enabled",
        value.continuation_enabled);
    tree.put(
        "continuation_initial_step",
        value.continuation_initial_step);
    tree.put(
        "continuation_minimum_step",
        value.continuation_minimum_step);
    tree.put(
        "continuation_step_growth",
        value.continuation_step_growth);
    tree.put(
        "continuation_step_reduction",
        value.continuation_step_reduction);
    tree.put(
        "continuation_maximum_stages",
        value.continuation_maximum_stages);
    return tree;
}

service::SteadySolverSettings decode_steady_solver(
    const Tree& tree) {
    service::SteadySolverSettings value;
    value.max_iterations =
        tree.get<int>("max_iterations");
    value.residual_tolerance =
        tree.get<double>("residual_tolerance");
    value.step_tolerance =
        tree.get<double>("step_tolerance");
    value.linear_residual_tolerance =
        tree.get<double>("linear_residual_tolerance");
    value.finite_difference_epsilon =
        tree.get<double>("finite_difference_epsilon");
    value.min_damping = tree.get<double>("min_damping");
    value.damping_reduction =
        tree.get<double>("damping_reduction");
    value.sufficient_decrease =
        tree.get<double>("sufficient_decrease");
    value.max_line_search_steps =
        tree.get<int>("max_line_search_steps");
    value.continuation_enabled = tree.get(
        "continuation_enabled",
        value.continuation_enabled);
    value.continuation_initial_step = tree.get(
        "continuation_initial_step",
        value.continuation_initial_step);
    value.continuation_minimum_step = tree.get(
        "continuation_minimum_step",
        value.continuation_minimum_step);
    value.continuation_step_growth = tree.get(
        "continuation_step_growth",
        value.continuation_step_growth);
    value.continuation_step_reduction = tree.get(
        "continuation_step_reduction",
        value.continuation_step_reduction);
    value.continuation_maximum_stages = tree.get(
        "continuation_maximum_stages",
        value.continuation_maximum_stages);
    return value;
}

Tree transient_solver_tree(
    const service::TransientSolverSettings& value) {
    Tree tree;
    tree.put("start_time", value.start_time);
    tree.put("end_time", value.end_time);
    tree.put("initial_step", value.initial_step);
    tree.put("min_step", value.min_step);
    tree.put("max_step", value.max_step);
    tree.put("absolute_tolerance", value.absolute_tolerance);
    tree.put("relative_tolerance", value.relative_tolerance);
    tree.put("max_steps", value.max_steps);
    tree.put(
        "max_consecutive_rejections",
        value.max_consecutive_rejections);
    tree.put("maximum_order", value.maximum_order);
    tree.put(
        "compute_consistent_initial_conditions",
        value.compute_consistent_initial_conditions);
    tree.add_child(
        "nonlinear_solver",
        steady_solver_tree(value.nonlinear_solver));
    return tree;
}

service::TransientSolverSettings decode_transient_solver(
    const Tree& tree) {
    service::TransientSolverSettings value;
    value.start_time = tree.get<double>("start_time");
    value.end_time = tree.get<double>("end_time");
    value.initial_step = tree.get<double>("initial_step");
    value.min_step = tree.get<double>("min_step");
    value.max_step = tree.get<double>("max_step");
    value.absolute_tolerance =
        tree.get<double>("absolute_tolerance");
    value.relative_tolerance =
        tree.get<double>("relative_tolerance");
    value.max_steps = tree.get<int>("max_steps");
    value.max_consecutive_rejections =
        tree.get<int>("max_consecutive_rejections");
    value.maximum_order = tree.get<int>("maximum_order");
    value.compute_consistent_initial_conditions =
        tree.get<bool>(
            "compute_consistent_initial_conditions");
    value.nonlinear_solver = decode_steady_solver(
        tree.get_child("nonlinear_solver"));
    return value;
}

Tree calibration_solver_tree(
    const service::CalibrationSolverSettings& value) {
    Tree tree;
    tree.put("max_iterations", value.max_iterations);
    tree.put("initial_step_fraction", value.initial_step_fraction);
    tree.put("minimum_step_fraction", value.minimum_step_fraction);
    tree.put("step_reduction", value.step_reduction);
    tree.put("minimum_continuation_fraction",
             value.minimum_continuation_fraction);
    tree.put("continuation_growth", value.continuation_growth);
    tree.add_child("simulation_solver",
                   steady_solver_tree(value.simulation_solver));
    return tree;
}

service::CalibrationSolverSettings decode_calibration_solver(
    const Tree& tree) {
    service::CalibrationSolverSettings value;
    value.max_iterations = tree.get<int>("max_iterations");
    value.initial_step_fraction =
        tree.get<double>("initial_step_fraction");
    value.minimum_step_fraction =
        tree.get<double>("minimum_step_fraction");
    value.step_reduction = tree.get<double>("step_reduction");
    value.minimum_continuation_fraction =
        tree.get<double>("minimum_continuation_fraction");
    value.continuation_growth =
        tree.get<double>("continuation_growth");
    value.simulation_solver = decode_steady_solver(
        tree.get_child("simulation_solver"));
    return value;
}

std::string result_projections_payload(
    const std::vector<service::ResultProjection>& projections) {
    if (projections.empty()) {
        return R"({"items":[]})";
    }
    Tree values;
    for (const auto& projection : projections) {
        Tree value;
        value.put("id", projection.id);
        value.put(
            "scope", service::to_string(projection.scope));
        value.put("component_id", projection.component_id);
        value.put("port_name", projection.port_name);
        value.put("value_name", projection.value_name);
        value.put("dimension", projection.dimension);
        value.put(
            "aggregation",
            service::to_string(projection.aggregation));
        values.push_back({"", value});
    }
    Tree wrapper;
    wrapper.add_child("items", values);
    return write_tree(wrapper);
}

std::vector<service::ResultProjection>
decode_result_projections(const std::string& payload) {
    const auto tree = read_tree(payload);
    std::vector<service::ResultProjection> projections;
    for (const auto& [key, value] : tree) {
        if (!key.empty()) {
            throw std::runtime_error(
                "persisted result projections are not an array");
        }
        service::ResultProjection projection;
        projection.id = value.get<std::string>("id");
        projection.scope =
            service::result_value_scope_from_string(
                value.get<std::string>("scope"));
        projection.component_id =
            value.get<std::string>("component_id", "");
        projection.port_name =
            value.get<std::string>("port_name", "");
        projection.value_name =
            value.get<std::string>("value_name");
        projection.dimension =
            value.get<std::string>("dimension");
        projection.aggregation =
            service::result_aggregation_from_string(
                value.get<std::string>("aggregation"));
        projections.push_back(std::move(projection));
    }
    service::validate_result_projections(projections);
    return projections;
}

std::string acceptance_criteria_payload(
    const std::vector<service::EngineeringAcceptanceCriterion>&
        criteria) {
    if (criteria.empty()) return R"({"items":[]})";
    Tree values;
    for (const auto& criterion : criteria) {
        Tree value;
        value.put("id", criterion.id);
        value.put("projection_id", criterion.projection_id);
        value.put("dimension", criterion.dimension);
        if (criterion.lower_bound_si) {
            value.put("lower_bound_si", *criterion.lower_bound_si);
        }
        if (criterion.upper_bound_si) {
            value.put("upper_bound_si", *criterion.upper_bound_si);
        }
        value.put("lower_inclusive", criterion.lower_inclusive);
        value.put("upper_inclusive", criterion.upper_inclusive);
        values.push_back({"", value});
    }
    Tree wrapper;
    wrapper.add_child("items", values);
    return write_tree(wrapper);
}

std::vector<service::EngineeringAcceptanceCriterion>
decode_acceptance_criteria(const std::string& payload) {
    const auto tree = read_tree(payload);
    std::vector<service::EngineeringAcceptanceCriterion> criteria;
    for (const auto& [key, value] : tree) {
        if (!key.empty()) {
            throw std::runtime_error(
                "persisted acceptance criteria are not an array");
        }
        service::EngineeringAcceptanceCriterion criterion;
        criterion.id = value.get<std::string>("id");
        criterion.projection_id =
            value.get<std::string>("projection_id");
        criterion.dimension = value.get<std::string>("dimension");
        if (const auto lower =
                value.get_optional<double>("lower_bound_si")) {
            criterion.lower_bound_si = *lower;
        }
        if (const auto upper =
                value.get_optional<double>("upper_bound_si")) {
            criterion.upper_bound_si = *upper;
        }
        criterion.lower_inclusive =
            value.get("lower_inclusive", true);
        criterion.upper_inclusive =
            value.get("upper_inclusive", true);
        criteria.push_back(std::move(criterion));
    }
    return criteria;
}

std::string operating_envelopes_payload(
    const std::vector<service::ArtifactOperatingEnvelope>& envelopes) {
    Tree values;
    for (const auto& envelope : envelopes) {
        Tree value;
        value.put("artifact_revision_id", envelope.artifact_revision_id);
        Tree coordinates;
        for (const auto& coordinate : envelope.coordinates) {
            Tree item;
            item.put("coordinate", coordinate.coordinate);
            item.put("dimension", coordinate.dimension);
            if (coordinate.minimum) item.put("minimum", *coordinate.minimum);
            if (coordinate.maximum) item.put("maximum", *coordinate.maximum);
            item.put("minimum_inclusive", coordinate.minimum_inclusive);
            item.put("maximum_inclusive", coordinate.maximum_inclusive);
            coordinates.push_back({"", item});
        }
        value.add_child("coordinates", coordinates);
        values.push_back({"", value});
    }
    Tree wrapper;
    wrapper.add_child("items", values);
    return write_tree(wrapper);
}

std::vector<service::ArtifactOperatingEnvelope>
decode_operating_envelopes(const std::string& payload) {
    const auto tree = read_tree(payload);
    std::vector<service::ArtifactOperatingEnvelope> envelopes;
    for (const auto& [key, value] : tree) {
        if (!key.empty()) {
            throw std::runtime_error(
                "persisted artifact operating envelopes are not an array");
        }
        service::ArtifactOperatingEnvelope envelope;
        envelope.artifact_revision_id =
            value.get<std::string>("artifact_revision_id");
        for (const auto& [coordinate_key, encoded] :
             value.get_child("coordinates")) {
            if (!coordinate_key.empty()) {
                throw std::runtime_error(
                    "persisted operating-envelope coordinates are not "
                    "an array");
            }
            service::ArtifactCoordinateConstraintInput coordinate;
            coordinate.coordinate = encoded.get<std::string>("coordinate");
            coordinate.dimension = encoded.get<std::string>("dimension");
            if (const auto minimum = encoded.get_optional<double>("minimum")) {
                coordinate.minimum = *minimum;
            }
            if (const auto maximum = encoded.get_optional<double>("maximum")) {
                coordinate.maximum = *maximum;
            }
            coordinate.minimum_inclusive =
                encoded.get("minimum_inclusive", true);
            coordinate.maximum_inclusive =
                encoded.get("maximum_inclusive", true);
            envelope.coordinates.push_back(std::move(coordinate));
        }
        envelopes.push_back(std::move(envelope));
    }
    return envelopes;
}

Connection connect(const std::string& connection_string) {
    Connection connection{PQconnectdb(connection_string.c_str())};
    if (!connection ||
        PQstatus(connection.get()) != CONNECTION_OK) {
        const std::string message = connection
            ? PQerrorMessage(connection.get())
            : "allocation failed";
        throw std::runtime_error(
            "could not connect to PostgreSQL: " + message);
    }
    return connection;
}

Result execute(
    PGconn* connection,
    const char* sql,
    const std::vector<const char*>& values = {},
    ExecStatusType expected = PGRES_TUPLES_OK) {
    Result result{PQexecParams(
        connection,
        sql,
        static_cast<int>(values.size()),
        nullptr,
        values.empty() ? nullptr : values.data(),
        nullptr,
        nullptr,
        0)};
    if (!result || PQresultStatus(result.get()) != expected) {
        const std::string message = result
            ? PQresultErrorMessage(result.get())
            : PQerrorMessage(connection);
        throw std::runtime_error(
            "PostgreSQL project repository query failed: " +
            message);
    }
    return result;
}

std::string field(
    const PGresult* result,
    int row,
    int column) {
    if (PQgetisnull(result, row, column)) {
        throw std::runtime_error(
            "PostgreSQL project row contains an unexpected null");
    }
    return PQgetvalue(result, row, column);
}

std::string optional_field(
    const PGresult* result,
    int row,
    int column) {
    return PQgetisnull(result, row, column)
        ? std::string{}
        : std::string(PQgetvalue(result, row, column));
}

std::chrono::system_clock::time_point decode_time(
    const std::string& milliseconds) {
    return std::chrono::system_clock::time_point{
        std::chrono::milliseconds{std::stoll(milliseconds)}};
}

service::ProjectRecord decode_project(
    const PGresult* result,
    int row = 0) {
    service::ProjectRecord record;
    record.project_id = field(result, row, 0);
    record.team_id = field(result, row, 1);
    record.name = field(result, row, 2);
    record.description = field(result, row, 3);
    record.created_by_user_id = field(result, row, 4);
    record.created_at = decode_time(field(result, row, 5));
    return record;
}

service::ModelRevisionRecord decode_model_revision(
    const PGresult* result,
    int row = 0) {
    service::ModelRevisionRecord record;
    record.model_revision_id = field(result, row, 0);
    record.project_id = field(result, row, 1);
    record.team_id = field(result, row, 2);
    record.revision_number =
        std::stoull(field(result, row, 3));
    record.parent_model_revision_id =
        optional_field(result, row, 4);
    record.model_schema_version = field(result, row, 5);
    record.model_id = field(result, row, 6);
    record.model_revision_label = field(result, row, 7);
    record.canonical_model_json = field(result, row, 8);
    record.checksum = field(result, row, 9);
    record.created_by_user_id = field(result, row, 10);
    record.created_at = decode_time(field(result, row, 11));
    return record;
}

service::CaseRevisionRecord decode_case_revision(
    const PGresult* result,
    int row = 0) {
    service::CaseRevisionRecord record;
    record.case_revision_id = field(result, row, 0);
    record.model_revision_id = field(result, row, 1);
    record.project_id = field(result, row, 2);
    record.team_id = field(result, row, 3);
    record.case_id = field(result, row, 4);
    record.revision_number =
        std::stoull(field(result, row, 5));
    record.parent_case_revision_id =
        optional_field(result, row, 6);
    record.mode = field(result, row, 7);
    record.canonical_case_json = field(result, row, 8);
    record.checksum = field(result, row, 9);
    record.created_by_user_id = field(result, row, 10);
    record.created_at = decode_time(field(result, row, 11));
    return record;
}

service::ArtifactRevisionRecord decode_artifact_revision(
    const PGresult* result,
    int row = 0) {
    service::ArtifactRevisionRecord record;
    record.artifact_revision_id = field(result, row, 0);
    record.project_id = field(result, row, 1);
    record.team_id = field(result, row, 2);
    record.artifact_id = field(result, row, 3);
    record.revision_number =
        std::stoull(field(result, row, 4));
    record.parent_artifact_revision_id =
        optional_field(result, row, 5);
    record.artifact_type = field(result, row, 6);
    record.artifact_schema_version = field(result, row, 7);
    record.content.object_key = field(result, row, 8);
    record.content.media_type = field(result, row, 9);
    record.content.byte_size =
        std::stoull(field(result, row, 10));
    record.content.checksum = field(result, row, 11);
    record.created_by_user_id = field(result, row, 12);
    record.created_at = decode_time(field(result, row, 13));
    return record;
}

service::EngineeringReviewDisposition decode_review_disposition(
    const std::string& value) {
    if (value == "approved") {
        return service::EngineeringReviewDisposition::approved;
    }
    if (value == "approved_with_conditions") {
        return service::EngineeringReviewDisposition::
            approved_with_conditions;
    }
    if (value == "rejected") {
        return service::EngineeringReviewDisposition::rejected;
    }
    throw std::runtime_error(
        "persisted performance-map quality review has an unknown "
        "disposition");
}

service::PerformanceMapQualityReviewRecord decode_quality_review(
    const PGresult* result,
    int row = 0) {
    service::PerformanceMapQualityReviewRecord record;
    record.review_id = field(result, row, 0);
    record.project_id = field(result, row, 1);
    record.team_id = field(result, row, 2);
    record.artifact_revision_id = field(result, row, 3);
    record.artifact_checksum = field(result, row, 4);
    record.supersedes_review_id = optional_field(result, row, 5);
    record.disposition =
        decode_review_disposition(field(result, row, 6));
    record.reviewed_scope = field(result, row, 7);
    record.rationale = field(result, row, 8);
    record.quality_schema_version = field(result, row, 9);
    record.quality_snapshot_json = field(result, row, 10);
    record.quality_snapshot_checksum = field(result, row, 11);
    record.created_by_user_id = field(result, row, 12);
    record.created_at = decode_time(field(result, row, 13));
    return record;
}

service::StudyRevisionRecord decode_study_revision(
    const PGresult* result,
    int row = 0) {
    service::StudyRevisionRecord record;
    record.study_revision_id = field(result, row, 0);
    record.study_id = field(result, row, 1);
    record.project_id = field(result, row, 2);
    record.team_id = field(result, row, 3);
    record.revision_number = std::stoull(field(result, row, 4));
    record.parent_study_revision_id =
        optional_field(result, row, 5);
    record.model_revision_id = field(result, row, 6);
    record.case_revision_id = field(result, row, 7);
    record.intent = field(result, row, 8);
    record.result_projections =
        decode_result_projections(field(result, row, 9));
    record.acceptance_criteria =
        decode_acceptance_criteria(field(result, row, 10));
    record.artifact_operating_envelopes =
        decode_operating_envelopes(field(result, row, 11));
    service::validate_engineering_acceptance_criteria(
        record.acceptance_criteria, record.result_projections);
    record.checksum = field(result, row, 12);
    record.created_by_user_id = field(result, row, 13);
    record.created_at = decode_time(field(result, row, 14));
    return record;
}

service::CalibrationRevisionRecord decode_calibration_revision(
    const PGresult* result,
    int row = 0) {
    service::CalibrationRevisionRecord record;
    record.calibration_revision_id = field(result, row, 0);
    record.calibration_id = field(result, row, 1);
    record.project_id = field(result, row, 2);
    record.team_id = field(result, row, 3);
    record.revision_number = std::stoull(field(result, row, 4));
    record.parent_calibration_revision_id =
        optional_field(result, row, 5);
    record.model_revision_id = field(result, row, 6);
    record.definition_json = field(result, row, 7);
    record.solver = decode_calibration_solver(
        read_tree(field(result, row, 8)));
    record.checksum = field(result, row, 9);
    record.created_by_user_id = field(result, row, 10);
    record.created_at = decode_time(field(result, row, 11));
    return record;
}

service::RunConfigurationRevisionRecord
decode_run_configuration_revision(
    const PGresult* result,
    int row = 0) {
    service::RunConfigurationRevisionRecord record;
    record.run_configuration_revision_id =
        field(result, row, 0);
    record.run_configuration_id = field(result, row, 1);
    record.project_id = field(result, row, 2);
    record.team_id = field(result, row, 3);
    record.revision_number =
        std::stoull(field(result, row, 4));
    record.parent_run_configuration_revision_id =
        optional_field(result, row, 5);
    record.study_revision_id = field(result, row, 6);
    record.steady_solver = decode_steady_solver(
        read_tree(field(result, row, 7)));
    record.transient_solver = decode_transient_solver(
        read_tree(field(result, row, 8)));
    record.checksum = field(result, row, 9);
    record.created_by_user_id = field(result, row, 10);
    record.created_at = decode_time(field(result, row, 11));
    return record;
}

constexpr const char project_columns[] =
    "project_id, team_id, name, description, "
    "created_by_user_id, "
    "floor(extract(epoch FROM created_at) * 1000)"
    "::bigint::text";

constexpr const char model_revision_columns[] =
    "model_revision_id, project_id, team_id, "
    "revision_number, parent_model_revision_id, "
    "model_schema_version, model_id, model_revision_label, "
    "canonical_model_payload, checksum, "
    "created_by_user_id, "
    "floor(extract(epoch FROM created_at) * 1000)"
    "::bigint::text";

constexpr const char case_revision_columns[] =
    "case_revision_id, model_revision_id, project_id, team_id, "
    "case_id, revision_number, parent_case_revision_id, mode, "
    "canonical_case_payload, checksum, created_by_user_id, "
    "floor(extract(epoch FROM created_at) * 1000)"
    "::bigint::text";

constexpr const char artifact_revision_columns[] =
    "artifact_revision_id, project_id, team_id, artifact_id, "
    "revision_number, parent_artifact_revision_id, "
    "artifact_type, artifact_schema_version, object_key, "
    "media_type, byte_size, checksum, created_by_user_id, "
    "floor(extract(epoch FROM created_at) * 1000)"
    "::bigint::text";

constexpr const char quality_review_columns[] =
    "review_id, project_id, team_id, artifact_revision_id, "
    "artifact_checksum, supersedes_review_id, disposition, "
    "reviewed_scope, rationale, quality_schema_version, "
    "quality_snapshot_json, quality_snapshot_checksum, "
    "created_by_user_id, "
    "floor(extract(epoch FROM created_at) * 1000)"
    "::bigint::text";

constexpr const char study_revision_columns[] =
    "study_revision_id, study_id, project_id, team_id, "
    "revision_number, parent_study_revision_id, "
    "model_revision_id, case_revision_id, intent, "
    "result_projections_payload, acceptance_criteria_payload, "
    "artifact_operating_envelopes_payload, "
    "checksum, created_by_user_id, "
    "floor(extract(epoch FROM created_at) * 1000)"
    "::bigint::text";

constexpr const char calibration_revision_columns[] =
    "calibration_revision_id, calibration_id, project_id, team_id, "
    "revision_number, parent_calibration_revision_id, "
    "model_revision_id, definition_payload, solver_payload, "
    "checksum, created_by_user_id, "
    "floor(extract(epoch FROM created_at) * 1000)::bigint::text";

constexpr const char run_configuration_revision_columns[] =
    "run_configuration_revision_id, run_configuration_id, "
    "project_id, team_id, revision_number, "
    "parent_run_configuration_revision_id, "
    "study_revision_id, "
    "steady_solver_payload, transient_solver_payload, "
    "checksum, created_by_user_id, "
    "floor(extract(epoch FROM created_at) * 1000)"
    "::bigint::text";

class PostgresProjectRepository final
    : public service::ProjectRepository {
public:
    explicit PostgresProjectRepository(
        std::string connection_string)
        : connection_string_(std::move(connection_string)) {
        if (connection_string_.empty()) {
            throw std::invalid_argument(
                "PostgreSQL connection string must not be empty");
        }
        auto connection = connect(connection_string_);
        const auto schema = execute(
            connection.get(),
            "SELECT to_regclass('thermox_projects')::text, "
            "to_regclass('thermox_model_revisions')::text, "
            "to_regclass('thermox_case_revisions')::text, "
            "to_regclass('thermox_artifact_revisions')::text, "
            "to_regclass("
            "'thermox_performance_map_quality_reviews')::text, "
            "to_regclass('thermox_study_revisions')::text, "
            "to_regclass("
            "'thermox_study_artifact_qualifications')::text, "
            "to_regclass("
            "'thermox_run_configuration_revisions')::text");
        if (PQgetisnull(schema.get(), 0, 0) ||
            PQgetisnull(schema.get(), 0, 1) ||
            PQgetisnull(schema.get(), 0, 2) ||
            PQgetisnull(schema.get(), 0, 3) ||
            PQgetisnull(schema.get(), 0, 4) ||
            PQgetisnull(schema.get(), 0, 5) ||
            PQgetisnull(schema.get(), 0, 6) ||
            PQgetisnull(schema.get(), 0, 7)) {
            throw std::runtime_error(
                "PostgreSQL project schema is not installed; "
                "apply all migrations");
        }
        const auto run_schema = execute(
            connection.get(),
            "SELECT count(*) FROM pg_attribute "
            "WHERE attrelid = to_regclass("
            "'thermox_run_configuration_revisions') "
            "AND attname = 'study_revision_id' "
            "AND NOT attisdropped");
        if (field(run_schema.get(), 0, 0) != "1") {
            throw std::runtime_error(
                "PostgreSQL run-study binding schema is missing; "
                "apply all migrations");
        }
    }

    service::ProjectRecord create_project(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& name,
        const std::string& description) override {
        auto connection = connect(connection_string_);
        const auto result = execute(
            connection.get(),
            "INSERT INTO thermox_projects ("
            "team_id, created_by_user_id, name, description"
            ") VALUES ($1, $2, $3, $4) RETURNING "
            "project_id, team_id, name, description, "
            "created_by_user_id, "
            "floor(extract(epoch FROM created_at) * 1000)"
            "::bigint::text",
            {
                team_id.c_str(),
                created_by_user_id.c_str(),
                name.c_str(),
                description.c_str(),
            });
        return decode_project(result.get());
    }

    std::optional<service::ProjectRecord> get_project(
        const std::string& team_id,
        const std::string& project_id) const override {
        auto connection = connect(connection_string_);
        const auto sql = std::string("SELECT ") +
            project_columns +
            " FROM thermox_projects "
            "WHERE team_id = $1 AND project_id = $2";
        const auto result = execute(
            connection.get(),
            sql.c_str(),
            {team_id.c_str(), project_id.c_str()});
        if (PQntuples(result.get()) == 0) {
            return std::nullopt;
        }
        return decode_project(result.get());
    }

    std::vector<service::ProjectRecord> list_projects(
        const std::string& team_id) const override {
        auto connection = connect(connection_string_);
        const auto sql = std::string("SELECT ") +
            project_columns +
            " FROM thermox_projects WHERE team_id = $1 "
            "ORDER BY created_at, project_id";
        const auto result = execute(
            connection.get(), sql.c_str(), {team_id.c_str()});
        std::vector<service::ProjectRecord> records;
        for (int row = 0; row < PQntuples(result.get()); ++row) {
            records.push_back(
                decode_project(result.get(), row));
        }
        return records;
    }

    service::ModelRevisionRecord create_model_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& parent_model_revision_id,
        const std::string& model_schema_version,
        const std::string& model_id,
        const std::string& model_revision_label,
        const std::string& canonical_model_json,
        const std::string& checksum) override {
        auto connection = connect(connection_string_);
        (void)execute(
            connection.get(), "BEGIN", {}, PGRES_COMMAND_OK);
        const auto number = execute(
            connection.get(),
            "UPDATE thermox_projects "
            "SET next_model_revision_number = "
            "next_model_revision_number + 1 "
            "WHERE team_id = $1 AND project_id = $2 "
            "RETURNING next_model_revision_number - 1",
            {team_id.c_str(), project_id.c_str()});
        if (PQntuples(number.get()) == 0) {
            throw service::ProjectStateError(
                "project was not found");
        }
        if (!parent_model_revision_id.empty()) {
            const auto parent = execute(
                connection.get(),
                "SELECT 1 FROM thermox_model_revisions "
                "WHERE team_id = $1 AND project_id = $2 "
                "AND model_revision_id = $3",
                {
                    team_id.c_str(),
                    project_id.c_str(),
                    parent_model_revision_id.c_str(),
                });
            if (PQntuples(parent.get()) == 0) {
                throw service::ProjectStateError(
                    "parent model revision was not found");
            }
        }
        const auto revision_number =
            field(number.get(), 0, 0);
        const char* parent = parent_model_revision_id.empty()
            ? nullptr
            : parent_model_revision_id.c_str();
        const auto result = execute(
            connection.get(),
            "INSERT INTO thermox_model_revisions ("
            "project_id, team_id, revision_number, "
            "parent_model_revision_id, model_schema_version, "
            "model_id, model_revision_label, "
            "canonical_model_payload, checksum, "
            "created_by_user_id"
            ") VALUES ("
            "$1, $2, $3::bigint, $4, $5, $6, $7, "
            "$8, $9, $10"
            ") RETURNING "
            "model_revision_id, project_id, team_id, "
            "revision_number, parent_model_revision_id, "
            "model_schema_version, model_id, "
            "model_revision_label, "
            "canonical_model_payload, checksum, "
            "created_by_user_id, "
            "floor(extract(epoch FROM created_at) * 1000)"
            "::bigint::text",
            {
                project_id.c_str(),
                team_id.c_str(),
                revision_number.c_str(),
                parent,
                model_schema_version.c_str(),
                model_id.c_str(),
                model_revision_label.c_str(),
                canonical_model_json.c_str(),
                checksum.c_str(),
                created_by_user_id.c_str(),
            });
        (void)execute(
            connection.get(), "COMMIT", {}, PGRES_COMMAND_OK);
        return decode_model_revision(result.get());
    }

    std::optional<service::ModelRevisionRecord>
    get_model_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& model_revision_id) const override {
        auto connection = connect(connection_string_);
        const auto sql = std::string("SELECT ") +
            model_revision_columns +
            " FROM thermox_model_revisions "
            "WHERE team_id = $1 AND project_id = $2 "
            "AND model_revision_id = $3";
        const auto result = execute(
            connection.get(),
            sql.c_str(),
            {
                team_id.c_str(),
                project_id.c_str(),
                model_revision_id.c_str(),
            });
        if (PQntuples(result.get()) == 0) {
            return std::nullopt;
        }
        return decode_model_revision(result.get());
    }

    std::vector<service::ModelRevisionRecord>
    list_model_revisions(
        const std::string& team_id,
        const std::string& project_id) const override {
        auto connection = connect(connection_string_);
        const auto sql = std::string("SELECT ") +
            model_revision_columns +
            " FROM thermox_model_revisions "
            "WHERE team_id = $1 AND project_id = $2 "
            "ORDER BY revision_number";
        const auto result = execute(
            connection.get(),
            sql.c_str(),
            {team_id.c_str(), project_id.c_str()});
        std::vector<service::ModelRevisionRecord> records;
        for (int row = 0; row < PQntuples(result.get()); ++row) {
            records.push_back(
                decode_model_revision(result.get(), row));
        }
        return records;
    }

    service::CaseRevisionRecord create_case_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& model_revision_id,
        const std::string& parent_case_revision_id,
        const std::string& case_id,
        const std::string& mode,
        const std::string& canonical_case_json,
        const std::string& checksum) override {
        auto connection = connect(connection_string_);
        (void)execute(
            connection.get(), "BEGIN", {}, PGRES_COMMAND_OK);
        const auto model = execute(
            connection.get(),
            "SELECT 1 FROM thermox_model_revisions "
            "WHERE team_id = $1 AND project_id = $2 "
            "AND model_revision_id = $3 FOR UPDATE",
            {
                team_id.c_str(),
                project_id.c_str(),
                model_revision_id.c_str(),
            });
        if (PQntuples(model.get()) == 0) {
            throw service::ProjectStateError(
                "model revision was not found");
        }
        if (!parent_case_revision_id.empty()) {
            const auto parent = execute(
                connection.get(),
                "SELECT 1 FROM thermox_case_revisions "
                "WHERE team_id = $1 AND project_id = $2 "
                "AND model_revision_id = $3 AND case_id = $4 "
                "AND case_revision_id = $5",
                {
                    team_id.c_str(),
                    project_id.c_str(),
                    model_revision_id.c_str(),
                    case_id.c_str(),
                    parent_case_revision_id.c_str(),
                });
            if (PQntuples(parent.get()) == 0) {
                throw service::ProjectStateError(
                    "parent case revision was not found");
            }
        }
        const auto number = execute(
            connection.get(),
            "SELECT coalesce(max(revision_number), 0) + 1 "
            "FROM thermox_case_revisions "
            "WHERE team_id = $1 AND project_id = $2 "
            "AND model_revision_id = $3 AND case_id = $4",
            {
                team_id.c_str(),
                project_id.c_str(),
                model_revision_id.c_str(),
                case_id.c_str(),
            });
        const auto revision_number = field(number.get(), 0, 0);
        const char* parent = parent_case_revision_id.empty()
            ? nullptr
            : parent_case_revision_id.c_str();
        const auto result = execute(
            connection.get(),
            "INSERT INTO thermox_case_revisions ("
            "model_revision_id, project_id, team_id, case_id, "
            "revision_number, parent_case_revision_id, mode, "
            "canonical_case_payload, checksum, "
            "created_by_user_id"
            ") VALUES ("
            "$1, $2, $3, $4, $5::bigint, $6, $7, $8, $9, $10"
            ") RETURNING "
            "case_revision_id, model_revision_id, project_id, "
            "team_id, case_id, revision_number, "
            "parent_case_revision_id, mode, "
            "canonical_case_payload, checksum, "
            "created_by_user_id, "
            "floor(extract(epoch FROM created_at) * 1000)"
            "::bigint::text",
            {
                model_revision_id.c_str(),
                project_id.c_str(),
                team_id.c_str(),
                case_id.c_str(),
                revision_number.c_str(),
                parent,
                mode.c_str(),
                canonical_case_json.c_str(),
                checksum.c_str(),
                created_by_user_id.c_str(),
            });
        (void)execute(
            connection.get(), "COMMIT", {}, PGRES_COMMAND_OK);
        return decode_case_revision(result.get());
    }

    std::optional<service::CaseRevisionRecord>
    get_case_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& model_revision_id,
        const std::string& case_revision_id) const override {
        auto connection = connect(connection_string_);
        const auto sql = std::string("SELECT ") +
            case_revision_columns +
            " FROM thermox_case_revisions "
            "WHERE team_id = $1 AND project_id = $2 "
            "AND model_revision_id = $3 "
            "AND case_revision_id = $4";
        const auto result = execute(
            connection.get(),
            sql.c_str(),
            {
                team_id.c_str(),
                project_id.c_str(),
                model_revision_id.c_str(),
                case_revision_id.c_str(),
            });
        if (PQntuples(result.get()) == 0) {
            return std::nullopt;
        }
        return decode_case_revision(result.get());
    }

    std::vector<service::CaseRevisionRecord>
    list_case_revisions(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& model_revision_id) const override {
        auto connection = connect(connection_string_);
        const auto sql = std::string("SELECT ") +
            case_revision_columns +
            " FROM thermox_case_revisions "
            "WHERE team_id = $1 AND project_id = $2 "
            "AND model_revision_id = $3 "
            "ORDER BY case_id, revision_number";
        const auto result = execute(
            connection.get(),
            sql.c_str(),
            {
                team_id.c_str(),
                project_id.c_str(),
                model_revision_id.c_str(),
            });
        std::vector<service::CaseRevisionRecord> records;
        for (int row = 0; row < PQntuples(result.get()); ++row) {
            records.push_back(
                decode_case_revision(result.get(), row));
        }
        return records;
    }

    service::ArtifactRevisionRecord
    create_artifact_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& artifact_id,
        const std::string& parent_artifact_revision_id,
        const std::string& artifact_type,
        const std::string& artifact_schema_version,
        const service::ArtifactContentManifest&
            content) override {
        auto connection = connect(connection_string_);
        (void)execute(
            connection.get(), "BEGIN", {}, PGRES_COMMAND_OK);
        const auto project = execute(
            connection.get(),
            "SELECT 1 FROM thermox_projects "
            "WHERE team_id = $1 AND project_id = $2 "
            "FOR UPDATE",
            {team_id.c_str(), project_id.c_str()});
        if (PQntuples(project.get()) == 0) {
            throw service::ProjectStateError(
                "project was not found");
        }
        if (!parent_artifact_revision_id.empty()) {
            const auto parent = execute(
                connection.get(),
                "SELECT 1 FROM thermox_artifact_revisions "
                "WHERE team_id = $1 AND project_id = $2 "
                "AND artifact_id = $3 "
                "AND artifact_revision_id = $4 "
                "AND artifact_type = $5",
                {
                    team_id.c_str(),
                    project_id.c_str(),
                    artifact_id.c_str(),
                    parent_artifact_revision_id.c_str(),
                    artifact_type.c_str(),
                });
            if (PQntuples(parent.get()) == 0) {
                throw service::ProjectStateError(
                    "parent artifact revision was not found");
            }
        }
        const auto number = execute(
            connection.get(),
            "SELECT coalesce(max(revision_number), 0) + 1 "
            "FROM thermox_artifact_revisions "
            "WHERE team_id = $1 AND project_id = $2 "
            "AND artifact_id = $3",
            {
                team_id.c_str(),
                project_id.c_str(),
                artifact_id.c_str(),
            });
        const auto revision_number = field(number.get(), 0, 0);
        const char* parent =
            parent_artifact_revision_id.empty()
            ? nullptr
            : parent_artifact_revision_id.c_str();
        const auto byte_size =
            std::to_string(content.byte_size);
        const auto result = execute(
            connection.get(),
            "INSERT INTO thermox_artifact_revisions ("
            "project_id, team_id, artifact_id, "
            "revision_number, parent_artifact_revision_id, "
            "artifact_type, artifact_schema_version, "
            "object_key, media_type, byte_size, checksum, "
            "created_by_user_id"
            ") VALUES ("
            "$1, $2, $3, $4::bigint, $5, $6, $7, $8, $9, "
            "$10::bigint, $11, $12"
            ") RETURNING "
            "artifact_revision_id, project_id, team_id, "
            "artifact_id, revision_number, "
            "parent_artifact_revision_id, artifact_type, "
            "artifact_schema_version, object_key, media_type, "
            "byte_size, checksum, created_by_user_id, "
            "floor(extract(epoch FROM created_at) * 1000)"
            "::bigint::text",
            {
                project_id.c_str(),
                team_id.c_str(),
                artifact_id.c_str(),
                revision_number.c_str(),
                parent,
                artifact_type.c_str(),
                artifact_schema_version.c_str(),
                content.object_key.c_str(),
                content.media_type.c_str(),
                byte_size.c_str(),
                content.checksum.c_str(),
                created_by_user_id.c_str(),
            });
        (void)execute(
            connection.get(), "COMMIT", {}, PGRES_COMMAND_OK);
        return decode_artifact_revision(result.get());
    }

    std::optional<service::ArtifactRevisionRecord>
    get_artifact_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& artifact_revision_id) const override {
        auto connection = connect(connection_string_);
        const auto sql = std::string("SELECT ") +
            artifact_revision_columns +
            " FROM thermox_artifact_revisions "
            "WHERE team_id = $1 AND project_id = $2 "
            "AND artifact_revision_id = $3";
        const auto result = execute(
            connection.get(),
            sql.c_str(),
            {
                team_id.c_str(),
                project_id.c_str(),
                artifact_revision_id.c_str(),
            });
        if (PQntuples(result.get()) == 0) {
            return std::nullopt;
        }
        return decode_artifact_revision(result.get());
    }

    std::vector<service::ArtifactRevisionRecord>
    list_artifact_revisions(
        const std::string& team_id,
        const std::string& project_id) const override {
        auto connection = connect(connection_string_);
        const auto sql = std::string("SELECT ") +
            artifact_revision_columns +
            " FROM thermox_artifact_revisions "
            "WHERE team_id = $1 AND project_id = $2 "
            "ORDER BY artifact_id, revision_number";
        const auto result = execute(
            connection.get(),
            sql.c_str(),
            {team_id.c_str(), project_id.c_str()});
        std::vector<service::ArtifactRevisionRecord> records;
        for (int row = 0; row < PQntuples(result.get()); ++row) {
            records.push_back(
                decode_artifact_revision(result.get(), row));
        }
        return records;
    }

    service::PerformanceMapQualityReviewRecord
    create_performance_map_quality_review(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& artifact_revision_id,
        const std::string& artifact_checksum,
        const std::string& supersedes_review_id,
        service::EngineeringReviewDisposition disposition,
        const std::string& reviewed_scope,
        const std::string& rationale,
        const std::string& quality_schema_version,
        const std::string& quality_snapshot_json,
        const std::string& quality_snapshot_checksum) override {
        auto connection = connect(connection_string_);
        (void)execute(
            connection.get(), "BEGIN", {}, PGRES_COMMAND_OK);
        const auto artifact = execute(
            connection.get(),
            "SELECT 1 FROM thermox_artifact_revisions "
            "WHERE team_id = $1 AND project_id = $2 "
            "AND artifact_revision_id = $3 AND checksum = $4 "
            "FOR SHARE",
            {
                team_id.c_str(), project_id.c_str(),
                artifact_revision_id.c_str(),
                artifact_checksum.c_str(),
            });
        if (PQntuples(artifact.get()) == 0) {
            throw service::ProjectStateError(
                "reviewed artifact revision was not found");
        }
        if (!supersedes_review_id.empty()) {
            const auto superseded = execute(
                connection.get(),
                "SELECT 1 FROM "
                "thermox_performance_map_quality_reviews "
                "WHERE team_id = $1 AND project_id = $2 "
                "AND artifact_revision_id = $3 "
                "AND review_id = $4",
                {
                    team_id.c_str(), project_id.c_str(),
                    artifact_revision_id.c_str(),
                    supersedes_review_id.c_str(),
                });
            if (PQntuples(superseded.get()) == 0) {
                throw service::ProjectStateError(
                    "superseded quality review was not found");
            }
        }
        const auto disposition_value =
            service::to_string(disposition);
        const char* supersedes = supersedes_review_id.empty()
            ? nullptr : supersedes_review_id.c_str();
        const auto result = execute(
            connection.get(),
            "INSERT INTO thermox_performance_map_quality_reviews ("
            "project_id, team_id, artifact_revision_id, "
            "artifact_checksum, supersedes_review_id, disposition, "
            "reviewed_scope, rationale, quality_schema_version, "
            "quality_snapshot_json, quality_snapshot_checksum, "
            "created_by_user_id"
            ") VALUES ("
            "$1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12"
            ") RETURNING "
            "review_id, project_id, team_id, artifact_revision_id, "
            "artifact_checksum, supersedes_review_id, disposition, "
            "reviewed_scope, rationale, quality_schema_version, "
            "quality_snapshot_json, quality_snapshot_checksum, "
            "created_by_user_id, "
            "floor(extract(epoch FROM created_at) * 1000)"
            "::bigint::text",
            {
                project_id.c_str(), team_id.c_str(),
                artifact_revision_id.c_str(), artifact_checksum.c_str(),
                supersedes, disposition_value.c_str(),
                reviewed_scope.c_str(), rationale.c_str(),
                quality_schema_version.c_str(),
                quality_snapshot_json.c_str(),
                quality_snapshot_checksum.c_str(),
                created_by_user_id.c_str(),
            });
        (void)execute(
            connection.get(), "COMMIT", {}, PGRES_COMMAND_OK);
        return decode_quality_review(result.get());
    }

    std::vector<service::PerformanceMapQualityReviewRecord>
    list_performance_map_quality_reviews(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& artifact_revision_id) const override {
        auto connection = connect(connection_string_);
        const auto sql = std::string("SELECT ") +
            quality_review_columns +
            " FROM thermox_performance_map_quality_reviews "
            "WHERE team_id = $1 AND project_id = $2 "
            "AND artifact_revision_id = $3 "
            "ORDER BY created_at, review_id";
        const auto result = execute(
            connection.get(), sql.c_str(),
            {
                team_id.c_str(), project_id.c_str(),
                artifact_revision_id.c_str(),
            });
        std::vector<service::PerformanceMapQualityReviewRecord> records;
        for (int row = 0; row < PQntuples(result.get()); ++row) {
            records.push_back(decode_quality_review(result.get(), row));
        }
        return records;
    }

    service::StudyRevisionRecord create_study_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& study_id,
        const std::string& parent_study_revision_id,
        const std::string& model_revision_id,
        const std::string& case_revision_id,
        const std::string& intent,
        const std::vector<std::string>& artifact_revision_ids,
        const std::vector<service::ArtifactQualificationRequirement>&
            artifact_qualification_requirements,
        const std::vector<service::ArtifactOperatingEnvelope>&
            artifact_operating_envelopes,
        const std::vector<service::ResultProjection>& result_projections,
        const std::vector<service::EngineeringAcceptanceCriterion>&
            acceptance_criteria,
        const std::string& checksum) override {
        auto connection = connect(connection_string_);
        (void)execute(
            connection.get(), "BEGIN", {}, PGRES_COMMAND_OK);
        const auto project = execute(
            connection.get(),
            "SELECT 1 FROM thermox_projects "
            "WHERE team_id = $1 AND project_id = $2 FOR UPDATE",
            {team_id.c_str(), project_id.c_str()});
        if (PQntuples(project.get()) == 0) {
            throw service::ProjectStateError(
                "project was not found");
        }
        if (!parent_study_revision_id.empty()) {
            const auto parent = execute(
                connection.get(),
                "SELECT 1 FROM thermox_study_revisions "
                "WHERE team_id = $1 AND project_id = $2 "
                "AND study_id = $3 AND study_revision_id = $4",
                {team_id.c_str(), project_id.c_str(),
                 study_id.c_str(),
                 parent_study_revision_id.c_str()});
            if (PQntuples(parent.get()) == 0) {
                throw service::ProjectStateError(
                    "parent study revision was not found");
            }
        }
        const auto number = execute(
            connection.get(),
            "SELECT coalesce(max(revision_number), 0) + 1 "
            "FROM thermox_study_revisions WHERE team_id = $1 "
            "AND project_id = $2 AND study_id = $3",
            {team_id.c_str(), project_id.c_str(),
             study_id.c_str()});
        const auto revision_number = field(number.get(), 0, 0);
        const char* parent = parent_study_revision_id.empty()
            ? nullptr
            : parent_study_revision_id.c_str();
        const auto projections_payload =
            result_projections_payload(result_projections);
        const auto criteria_payload =
            acceptance_criteria_payload(acceptance_criteria);
        const auto envelopes_payload =
            operating_envelopes_payload(artifact_operating_envelopes);
        const auto sql =
            std::string("INSERT INTO thermox_study_revisions (") +
            "study_id, project_id, team_id, revision_number, "
            "parent_study_revision_id, model_revision_id, "
            "case_revision_id, intent, result_projections_payload, "
            "acceptance_criteria_payload, "
            "artifact_operating_envelopes_payload, checksum, "
            "created_by_user_id) VALUES ("
            "$1, $2, $3, $4::bigint, $5, $6, $7, $8, "
            "($9::jsonb)->'items', ($10::jsonb)->'items', "
            "($11::jsonb)->'items', $12, $13) RETURNING " +
            study_revision_columns;
        const auto result = execute(
            connection.get(),
            sql.c_str(),
            {study_id.c_str(), project_id.c_str(), team_id.c_str(),
             revision_number.c_str(), parent,
             model_revision_id.c_str(), case_revision_id.c_str(),
             intent.c_str(), projections_payload.c_str(),
             criteria_payload.c_str(), envelopes_payload.c_str(),
             checksum.c_str(),
             created_by_user_id.c_str()});
        auto record = decode_study_revision(result.get());
        for (std::size_t index = 0;
             index < artifact_revision_ids.size(); ++index) {
            const auto position = std::to_string(index);
            (void)execute(
                connection.get(),
                "INSERT INTO thermox_study_artifacts ("
                "study_revision_id, project_id, team_id, position, "
                "artifact_revision_id) VALUES ("
                "$1, $2, $3, $4::integer, $5)",
                {record.study_revision_id.c_str(),
                 project_id.c_str(), team_id.c_str(),
                 position.c_str(),
                 artifact_revision_ids[index].c_str()},
                PGRES_COMMAND_OK);
        }
        for (std::size_t index = 0;
             index < artifact_qualification_requirements.size();
             ++index) {
            const auto& requirement =
                artifact_qualification_requirements[index];
            std::string dispositions{"{"};
            for (std::size_t disposition_index = 0;
                 disposition_index <
                     requirement.acceptable_dispositions.size();
                 ++disposition_index) {
                if (disposition_index != 0U) dispositions += ',';
                dispositions += service::to_string(
                    requirement.acceptable_dispositions[
                        disposition_index]);
            }
            dispositions += '}';
            const auto position = std::to_string(index);
            (void)execute(
                connection.get(),
                "INSERT INTO "
                "thermox_study_artifact_qualifications ("
                "study_revision_id, project_id, team_id, position, "
                "artifact_revision_id, review_id, "
                "acceptable_dispositions) VALUES ("
                "$1, $2, $3, $4::integer, $5, $6, $7::text[])",
                {record.study_revision_id.c_str(),
                 project_id.c_str(), team_id.c_str(),
                 position.c_str(),
                 requirement.artifact_revision_id.c_str(),
                 requirement.review_id.c_str(),
                 dispositions.c_str()},
                PGRES_COMMAND_OK);
        }
        (void)execute(
            connection.get(), "COMMIT", {}, PGRES_COMMAND_OK);
        record.artifact_revision_ids = artifact_revision_ids;
        record.artifact_qualification_requirements =
            artifact_qualification_requirements;
        return record;
    }

    std::optional<service::StudyRevisionRecord>
    get_study_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& study_revision_id) const override {
        auto connection = connect(connection_string_);
        const auto sql = std::string("SELECT ") +
            study_revision_columns +
            " FROM thermox_study_revisions WHERE team_id = $1 "
            "AND project_id = $2 AND study_revision_id = $3";
        const auto result = execute(
            connection.get(),
            sql.c_str(),
            {team_id.c_str(), project_id.c_str(),
             study_revision_id.c_str()});
        if (PQntuples(result.get()) == 0) return std::nullopt;
        auto record = decode_study_revision(result.get());
        record.artifact_revision_ids = study_artifact_ids(
            connection.get(), team_id, project_id,
            study_revision_id);
        record.artifact_qualification_requirements =
            study_artifact_qualifications(
                connection.get(), team_id, project_id,
                study_revision_id);
        return record;
    }

    std::vector<service::StudyRevisionRecord>
    list_study_revisions(
        const std::string& team_id,
        const std::string& project_id) const override {
        auto connection = connect(connection_string_);
        const auto sql = std::string("SELECT ") +
            study_revision_columns +
            " FROM thermox_study_revisions WHERE team_id = $1 "
            "AND project_id = $2 ORDER BY study_id, revision_number";
        const auto result = execute(
            connection.get(),
            sql.c_str(),
            {team_id.c_str(), project_id.c_str()});
        std::vector<service::StudyRevisionRecord> records;
        for (int row = 0; row < PQntuples(result.get()); ++row) {
            auto record = decode_study_revision(result.get(), row);
            record.artifact_revision_ids = study_artifact_ids(
                connection.get(), team_id, project_id,
                record.study_revision_id);
            record.artifact_qualification_requirements =
                study_artifact_qualifications(
                    connection.get(), team_id, project_id,
                    record.study_revision_id);
            records.push_back(std::move(record));
        }
        return records;
    }

    service::CalibrationRevisionRecord create_calibration_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& calibration_id,
        const std::string& parent_calibration_revision_id,
        const std::string& model_revision_id,
        const std::vector<std::string>& training_study_revision_ids,
        const std::vector<std::string>& validation_study_revision_ids,
        const std::string& definition_json,
        const service::CalibrationSolverSettings& solver,
        const std::string& checksum) override {
        auto connection = connect(connection_string_);
        (void)execute(connection.get(), "BEGIN", {}, PGRES_COMMAND_OK);
        const auto project = execute(
            connection.get(),
            "SELECT 1 FROM thermox_projects WHERE team_id = $1 "
            "AND project_id = $2 FOR UPDATE",
            {team_id.c_str(), project_id.c_str()});
        if (PQntuples(project.get()) == 0) {
            throw service::ProjectStateError("project was not found");
        }
        if (!parent_calibration_revision_id.empty()) {
            const auto parent = execute(
                connection.get(),
                "SELECT 1 FROM thermox_calibration_revisions "
                "WHERE team_id = $1 AND project_id = $2 "
                "AND calibration_id = $3 "
                "AND calibration_revision_id = $4",
                {team_id.c_str(), project_id.c_str(),
                 calibration_id.c_str(),
                 parent_calibration_revision_id.c_str()});
            if (PQntuples(parent.get()) == 0) {
                throw service::ProjectStateError(
                    "parent calibration revision was not found");
            }
        }
        const auto number = execute(
            connection.get(),
            "SELECT coalesce(max(revision_number), 0) + 1 "
            "FROM thermox_calibration_revisions WHERE team_id = $1 "
            "AND project_id = $2 AND calibration_id = $3",
            {team_id.c_str(), project_id.c_str(), calibration_id.c_str()});
        const auto revision_number = field(number.get(), 0, 0);
        const char* parent = parent_calibration_revision_id.empty()
            ? nullptr : parent_calibration_revision_id.c_str();
        const auto solver_payload =
            write_tree(calibration_solver_tree(solver));
        const auto sql =
            std::string("INSERT INTO thermox_calibration_revisions (") +
            "calibration_id, project_id, team_id, revision_number, "
            "parent_calibration_revision_id, model_revision_id, "
            "definition_payload, solver_payload, checksum, "
            "created_by_user_id) VALUES ("
            "$1, $2, $3, $4::bigint, $5, $6, $7::jsonb, $8::jsonb, "
            "$9, $10) RETURNING " + calibration_revision_columns;
        const auto result = execute(
            connection.get(), sql.c_str(),
            {calibration_id.c_str(), project_id.c_str(), team_id.c_str(),
             revision_number.c_str(), parent, model_revision_id.c_str(),
             definition_json.c_str(), solver_payload.c_str(),
             checksum.c_str(), created_by_user_id.c_str()});
        auto record = decode_calibration_revision(result.get());
        const auto insert_studies = [&](const std::vector<std::string>& ids,
                                        const char* role) {
            for (std::size_t index = 0; index < ids.size(); ++index) {
                const auto position = std::to_string(index);
                (void)execute(
                    connection.get(),
                    "INSERT INTO thermox_calibration_studies ("
                    "calibration_revision_id, project_id, team_id, role, "
                    "position, study_revision_id) VALUES ("
                    "$1, $2, $3, $4, $5::integer, $6)",
                    {record.calibration_revision_id.c_str(),
                     project_id.c_str(), team_id.c_str(), role,
                     position.c_str(), ids[index].c_str()},
                    PGRES_COMMAND_OK);
            }
        };
        insert_studies(training_study_revision_ids, "training");
        insert_studies(validation_study_revision_ids, "validation");
        (void)execute(connection.get(), "COMMIT", {}, PGRES_COMMAND_OK);
        record.training_study_revision_ids = training_study_revision_ids;
        record.validation_study_revision_ids = validation_study_revision_ids;
        return record;
    }

    std::optional<service::CalibrationRevisionRecord>
    get_calibration_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& calibration_revision_id) const override {
        auto connection = connect(connection_string_);
        const auto sql = std::string("SELECT ") +
            calibration_revision_columns +
            " FROM thermox_calibration_revisions WHERE team_id = $1 "
            "AND project_id = $2 AND calibration_revision_id = $3";
        const auto result = execute(
            connection.get(), sql.c_str(),
            {team_id.c_str(), project_id.c_str(),
             calibration_revision_id.c_str()});
        if (PQntuples(result.get()) == 0) return std::nullopt;
        auto record = decode_calibration_revision(result.get());
        record.training_study_revision_ids = calibration_study_ids(
            connection.get(), team_id, project_id,
            calibration_revision_id, "training");
        record.validation_study_revision_ids = calibration_study_ids(
            connection.get(), team_id, project_id,
            calibration_revision_id, "validation");
        return record;
    }

    std::vector<service::CalibrationRevisionRecord>
    list_calibration_revisions(
        const std::string& team_id,
        const std::string& project_id) const override {
        auto connection = connect(connection_string_);
        const auto sql = std::string("SELECT ") +
            calibration_revision_columns +
            " FROM thermox_calibration_revisions WHERE team_id = $1 "
            "AND project_id = $2 ORDER BY calibration_id, revision_number";
        const auto result = execute(
            connection.get(), sql.c_str(),
            {team_id.c_str(), project_id.c_str()});
        std::vector<service::CalibrationRevisionRecord> records;
        for (int row = 0; row < PQntuples(result.get()); ++row) {
            auto record = decode_calibration_revision(result.get(), row);
            record.training_study_revision_ids = calibration_study_ids(
                connection.get(), team_id, project_id,
                record.calibration_revision_id, "training");
            record.validation_study_revision_ids = calibration_study_ids(
                connection.get(), team_id, project_id,
                record.calibration_revision_id, "validation");
            records.push_back(std::move(record));
        }
        return records;
    }

    service::RunConfigurationRevisionRecord
    create_run_configuration_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& run_configuration_id,
        const std::string&
            parent_run_configuration_revision_id,
        const std::string& study_revision_id,
        const service::SteadySolverSettings& steady_solver,
        const service::TransientSolverSettings&
            transient_solver,
        const std::string& checksum) override {
        auto connection = connect(connection_string_);
        (void)execute(
            connection.get(), "BEGIN", {}, PGRES_COMMAND_OK);
        const auto project = execute(
            connection.get(),
            "SELECT 1 FROM thermox_projects "
            "WHERE team_id = $1 AND project_id = $2 "
            "FOR UPDATE",
            {team_id.c_str(), project_id.c_str()});
        if (PQntuples(project.get()) == 0) {
            throw service::ProjectStateError(
                "project was not found");
        }
        if (!parent_run_configuration_revision_id.empty()) {
            const auto parent = execute(
                connection.get(),
                "SELECT 1 FROM "
                "thermox_run_configuration_revisions "
                "WHERE team_id = $1 AND project_id = $2 "
                "AND run_configuration_id = $3 "
                "AND run_configuration_revision_id = $4",
                {
                    team_id.c_str(),
                    project_id.c_str(),
                    run_configuration_id.c_str(),
                    parent_run_configuration_revision_id
                        .c_str(),
                });
            if (PQntuples(parent.get()) == 0) {
                throw service::ProjectStateError(
                    "parent run configuration revision was "
                    "not found");
            }
        }
        const auto number = execute(
            connection.get(),
            "SELECT coalesce(max(revision_number), 0) + 1 "
            "FROM thermox_run_configuration_revisions "
            "WHERE team_id = $1 AND project_id = $2 "
            "AND run_configuration_id = $3",
            {
                team_id.c_str(),
                project_id.c_str(),
                run_configuration_id.c_str(),
            });
        const auto revision_number = field(number.get(), 0, 0);
        const char* parent =
            parent_run_configuration_revision_id.empty()
            ? nullptr
            : parent_run_configuration_revision_id.c_str();
        const auto steady_payload =
            write_tree(steady_solver_tree(steady_solver));
        const auto transient_payload = write_tree(
            transient_solver_tree(transient_solver));
        const auto result = execute(
            connection.get(),
            "INSERT INTO "
            "thermox_run_configuration_revisions ("
            "run_configuration_id, project_id, team_id, "
            "revision_number, "
            "parent_run_configuration_revision_id, "
            "study_revision_id, "
            "steady_solver_payload, "
            "transient_solver_payload, "
            "checksum, created_by_user_id"
            ") VALUES ("
            "$1, $2, $3, $4::bigint, $5, $6, $7, $8, $9, $10"
            ") RETURNING "
            "run_configuration_revision_id, "
            "run_configuration_id, project_id, team_id, "
            "revision_number, "
            "parent_run_configuration_revision_id, "
            "study_revision_id, "
            "steady_solver_payload, "
            "transient_solver_payload, "
            "checksum, created_by_user_id, "
            "floor(extract(epoch FROM created_at) * 1000)"
            "::bigint::text",
            {
                run_configuration_id.c_str(),
                project_id.c_str(),
                team_id.c_str(),
                revision_number.c_str(),
                parent,
                study_revision_id.c_str(),
                steady_payload.c_str(),
                transient_payload.c_str(),
                checksum.c_str(),
                created_by_user_id.c_str(),
            });
        auto record =
            decode_run_configuration_revision(result.get());
        (void)execute(
            connection.get(), "COMMIT", {}, PGRES_COMMAND_OK);
        return record;
    }

    std::optional<service::RunConfigurationRevisionRecord>
    get_run_configuration_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string&
            run_configuration_revision_id) const override {
        auto connection = connect(connection_string_);
        const auto sql = std::string("SELECT ") +
            run_configuration_revision_columns +
            " FROM thermox_run_configuration_revisions "
            "WHERE team_id = $1 AND project_id = $2 "
            "AND run_configuration_revision_id = $3";
        const auto result = execute(
            connection.get(),
            sql.c_str(),
            {
                team_id.c_str(),
                project_id.c_str(),
                run_configuration_revision_id.c_str(),
            });
        if (PQntuples(result.get()) == 0) {
            return std::nullopt;
        }
        return decode_run_configuration_revision(result.get());
    }

    std::vector<service::RunConfigurationRevisionRecord>
    list_run_configuration_revisions(
        const std::string& team_id,
        const std::string& project_id) const override {
        auto connection = connect(connection_string_);
        const auto sql = std::string("SELECT ") +
            run_configuration_revision_columns +
            " FROM thermox_run_configuration_revisions "
            "WHERE team_id = $1 AND project_id = $2 "
            "ORDER BY run_configuration_id, revision_number";
        const auto result = execute(
            connection.get(),
            sql.c_str(),
            {team_id.c_str(), project_id.c_str()});
        std::vector<service::RunConfigurationRevisionRecord>
            records;
        for (int row = 0; row < PQntuples(result.get()); ++row) {
            records.push_back(
                decode_run_configuration_revision(
                    result.get(), row));
        }
        return records;
    }

private:
    static std::vector<std::string> calibration_study_ids(
        PGconn* connection,
        const std::string& team_id,
        const std::string& project_id,
        const std::string& calibration_revision_id,
        const char* role) {
        const auto result = execute(
            connection,
            "SELECT study_revision_id FROM thermox_calibration_studies "
            "WHERE team_id = $1 AND project_id = $2 "
            "AND calibration_revision_id = $3 AND role = $4 "
            "ORDER BY position",
            {team_id.c_str(), project_id.c_str(),
             calibration_revision_id.c_str(), role});
        std::vector<std::string> ids;
        for (int row = 0; row < PQntuples(result.get()); ++row) {
            ids.push_back(field(result.get(), row, 0));
        }
        return ids;
    }

    static std::vector<std::string> study_artifact_ids(
        PGconn* connection,
        const std::string& team_id,
        const std::string& project_id,
        const std::string& study_revision_id) {
        const auto result = execute(
            connection,
            "SELECT artifact_revision_id FROM "
            "thermox_study_artifacts WHERE team_id = $1 "
            "AND project_id = $2 AND study_revision_id = $3 "
            "ORDER BY position",
            {team_id.c_str(), project_id.c_str(),
             study_revision_id.c_str()});
        std::vector<std::string> ids;
        for (int row = 0; row < PQntuples(result.get()); ++row) {
            ids.push_back(field(result.get(), row, 0));
        }
        return ids;
    }

    static std::vector<service::ArtifactQualificationRequirement>
    study_artifact_qualifications(
        PGconn* connection,
        const std::string& team_id,
        const std::string& project_id,
        const std::string& study_revision_id) {
        const auto result = execute(
            connection,
            "SELECT q.artifact_revision_id, q.review_id, "
            "d.disposition FROM "
            "thermox_study_artifact_qualifications q "
            "CROSS JOIN LATERAL unnest(q.acceptable_dispositions) "
            "WITH ORDINALITY AS d(disposition, disposition_position) "
            "WHERE q.team_id = $1 AND q.project_id = $2 "
            "AND q.study_revision_id = $3 "
            "ORDER BY q.position, d.disposition_position",
            {team_id.c_str(), project_id.c_str(),
             study_revision_id.c_str()});
        std::vector<service::ArtifactQualificationRequirement>
            requirements;
        for (int row = 0; row < PQntuples(result.get()); ++row) {
            const auto artifact_revision_id = field(result.get(), row, 0);
            const auto review_id = field(result.get(), row, 1);
            if (requirements.empty() ||
                requirements.back().artifact_revision_id !=
                    artifact_revision_id) {
                requirements.push_back({
                    artifact_revision_id,
                    review_id,
                    {},
                });
            }
            requirements.back().acceptable_dispositions.push_back(
                decode_review_disposition(field(result.get(), row, 2)));
        }
        return requirements;
    }

    std::string connection_string_;
};

}  // namespace

std::shared_ptr<service::ProjectRepository>
make_postgres_project_repository(
    std::string connection_string) {
    return std::make_shared<PostgresProjectRepository>(
        std::move(connection_string));
}

}  // namespace thermox::postgres
