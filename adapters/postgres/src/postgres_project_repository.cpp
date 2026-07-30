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
    value.compute_consistent_initial_conditions =
        tree.get<bool>(
            "compute_consistent_initial_conditions");
    value.nonlinear_solver = decode_steady_solver(
        tree.get_child("nonlinear_solver"));
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
    record.model_revision_id = field(result, row, 6);
    record.case_revision_id = field(result, row, 7);
    record.mode = field(result, row, 8);
    record.steady_solver = decode_steady_solver(
        read_tree(field(result, row, 9)));
    record.transient_solver = decode_transient_solver(
        read_tree(field(result, row, 10)));
    record.result_projections =
        decode_result_projections(field(result, row, 11));
    record.checksum = field(result, row, 12);
    record.created_by_user_id = field(result, row, 13);
    record.created_at = decode_time(field(result, row, 14));
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

constexpr const char run_configuration_revision_columns[] =
    "run_configuration_revision_id, run_configuration_id, "
    "project_id, team_id, revision_number, "
    "parent_run_configuration_revision_id, "
    "model_revision_id, case_revision_id, mode, "
    "steady_solver_payload, transient_solver_payload, "
    "result_projections_payload, checksum, created_by_user_id, "
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
            "'thermox_run_configuration_revisions')::text");
        if (PQgetisnull(schema.get(), 0, 0) ||
            PQgetisnull(schema.get(), 0, 1) ||
            PQgetisnull(schema.get(), 0, 2) ||
            PQgetisnull(schema.get(), 0, 3) ||
            PQgetisnull(schema.get(), 0, 4)) {
            throw std::runtime_error(
                "PostgreSQL project schema is not installed; "
                "apply all migrations");
        }
        const auto projection_schema = execute(
            connection.get(),
            "SELECT count(*) FROM pg_attribute "
            "WHERE attrelid = to_regclass("
            "'thermox_run_configuration_revisions') "
            "AND attname = 'result_projections_payload' "
            "AND NOT attisdropped");
        if (field(projection_schema.get(), 0, 0) != "1") {
            throw std::runtime_error(
                "PostgreSQL run projection schema is missing; "
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

    service::RunConfigurationRevisionRecord
    create_run_configuration_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& run_configuration_id,
        const std::string&
            parent_run_configuration_revision_id,
        const std::string& model_revision_id,
        const std::string& case_revision_id,
        const std::vector<std::string>&
            artifact_revision_ids,
        const std::string& mode,
        const service::SteadySolverSettings& steady_solver,
        const service::TransientSolverSettings&
            transient_solver,
        const std::vector<service::ResultProjection>&
            result_projections,
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
        const auto projections_payload =
            result_projections_payload(result_projections);
        const auto result = execute(
            connection.get(),
            "INSERT INTO "
            "thermox_run_configuration_revisions ("
            "run_configuration_id, project_id, team_id, "
            "revision_number, "
            "parent_run_configuration_revision_id, "
            "model_revision_id, case_revision_id, mode, "
            "steady_solver_payload, "
            "transient_solver_payload, "
            "result_projections_payload, checksum, "
            "created_by_user_id"
            ") VALUES ("
            "$1, $2, $3, $4::bigint, $5, $6, $7, $8, "
            "$9, $10, ($11::jsonb)->'items', $12, $13"
            ") RETURNING "
            "run_configuration_revision_id, "
            "run_configuration_id, project_id, team_id, "
            "revision_number, "
            "parent_run_configuration_revision_id, "
            "model_revision_id, case_revision_id, mode, "
            "steady_solver_payload, "
            "transient_solver_payload, "
            "result_projections_payload, checksum, "
            "created_by_user_id, "
            "floor(extract(epoch FROM created_at) * 1000)"
            "::bigint::text",
            {
                run_configuration_id.c_str(),
                project_id.c_str(),
                team_id.c_str(),
                revision_number.c_str(),
                parent,
                model_revision_id.c_str(),
                case_revision_id.c_str(),
                mode.c_str(),
                steady_payload.c_str(),
                transient_payload.c_str(),
                projections_payload.c_str(),
                checksum.c_str(),
                created_by_user_id.c_str(),
            });
        auto record =
            decode_run_configuration_revision(result.get());
        for (std::size_t index = 0;
             index < artifact_revision_ids.size();
             ++index) {
            const auto position = std::to_string(index);
            (void)execute(
                connection.get(),
                "INSERT INTO "
                "thermox_run_configuration_artifacts ("
                "run_configuration_revision_id, project_id, "
                "team_id, position, artifact_revision_id"
                ") VALUES ($1, $2, $3, $4::integer, $5)",
                {
                    record.run_configuration_revision_id
                        .c_str(),
                    project_id.c_str(),
                    team_id.c_str(),
                    position.c_str(),
                    artifact_revision_ids[index].c_str(),
                },
                PGRES_COMMAND_OK);
        }
        (void)execute(
            connection.get(), "COMMIT", {}, PGRES_COMMAND_OK);
        record.artifact_revision_ids =
            artifact_revision_ids;
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
        auto record =
            decode_run_configuration_revision(result.get());
        record.artifact_revision_ids = artifact_ids(
            connection.get(),
            team_id,
            project_id,
            run_configuration_revision_id);
        return record;
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
            auto record =
                decode_run_configuration_revision(
                    result.get(), row);
            record.artifact_revision_ids = artifact_ids(
                connection.get(),
                team_id,
                project_id,
                record.run_configuration_revision_id);
            records.push_back(std::move(record));
        }
        return records;
    }

private:
    static std::vector<std::string> artifact_ids(
        PGconn* connection,
        const std::string& team_id,
        const std::string& project_id,
        const std::string&
            run_configuration_revision_id) {
        const auto result = execute(
            connection,
            "SELECT artifact_revision_id FROM "
            "thermox_run_configuration_artifacts "
            "WHERE team_id = $1 AND project_id = $2 "
            "AND run_configuration_revision_id = $3 "
            "ORDER BY position",
            {
                team_id.c_str(),
                project_id.c_str(),
                run_configuration_revision_id.c_str(),
            });
        std::vector<std::string> ids;
        for (int row = 0; row < PQntuples(result.get()); ++row) {
            ids.push_back(field(result.get(), row, 0));
        }
        return ids;
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
