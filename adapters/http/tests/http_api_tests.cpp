#include "thermox/http/http_api.hpp"
#include "thermox/service/in_memory_jobs.hpp"
#include "thermox/service/in_memory_projects.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot read " + path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

thermox::http::Request json_post(
    std::string target,
    std::string body) {
    return {
        "POST",
        std::move(target),
        {
            {"Content-Type", "application/json; charset=utf-8"},
            {"Thermox-Command-Schema", "thermox.command/v1"},
        },
        std::move(body),
    };
}

thermox::http::Request authenticated(
    thermox::http::Request request,
    std::string user_id = "user-a",
    std::string team_id = "team-a") {
    request.identity = thermox::service::IdentityContext{
        std::move(user_id),
        std::move(team_id),
        "http-test-request"};
    return request;
}

void test_health_and_routing() {
    thermox::http::Api api;
    const auto health = api.handle({"GET", "/healthz", {}, {}});
    require(health.status == 200, "health endpoint must succeed");
    require(
        health.body.find("\"status\": \"ok\"") != std::string::npos,
        "health response must be structured JSON");
    require(
        health.headers.at("X-Content-Type-Options") == "nosniff",
        "responses must carry safe content headers");

    const auto missing = api.handle({"GET", "/missing", {}, {}});
    require(
        missing.status == 404 &&
            missing.body.find("route_not_found") != std::string::npos,
        "unknown routes must return a structured 404");

    const auto method =
        api.handle({"POST", "/api/v1/catalog", {}, {}});
    require(
        method.status == 405 && method.headers.at("Allow") == "GET",
        "method mismatch must return 405 and Allow");
}

void test_catalog_and_validation() {
    thermox::http::Api api;
    const auto catalog =
        api.handle({"GET", "/api/v1/catalog", {}, {}});
    require(catalog.status == 200, "catalog endpoint must succeed");
    require(
        catalog.body.find("thermox.catalog/v2") != std::string::npos,
        "catalog endpoint must preserve the service schema");

    const std::string model = read_file(
        std::string(THERMOX_SOURCE_DIR) +
        "/core/examples/air_compressor.json");
    const auto validation = api.handle(
        json_post(
            "/api/v1/models/validate?case_id=design",
            model));
    require(
        validation.status == 200 &&
            validation.body.find("\"compiled\": true") !=
                std::string::npos,
        "validation endpoint must call the application service");

    const auto bad_query = api.handle(
        json_post(
            "/api/v1/models/validate?unexpected=1",
            model));
    require(
        bad_query.status == 400 &&
            bad_query.body.find("unknown query parameter") !=
                std::string::npos,
        "unknown query parameters must be rejected");
}

void test_simulation_routes() {
    thermox::http::Api api{
        thermox::service::make_default_simulation_runtime(),
        {
            .enable_synchronous_simulations = true,
        }};
    const std::string steady_model = read_file(
        std::string(THERMOX_SOURCE_DIR) +
        "/core/examples/air_compressor.json");
    const auto steady = api.handle(
        json_post(
            "/api/v1/simulations/steady?case_id=design",
            steady_model));
    require(
        steady.status == 200 &&
            steady.body.find("\"converged\": true") !=
                std::string::npos,
        "steady endpoint must return a solved result");

    const std::string transient_model = read_file(
        std::string(THERMOX_SOURCE_DIR) +
        "/core/examples/lumped_thermal_storage.json");
    const auto transient = api.handle(
        json_post(
            "/api/v1/simulations/transient"
            "?case_id=charge&end_time=0.1",
            transient_model));
    require(
        transient.status == 200 &&
            transient.body.find("\"success\": true") !=
                std::string::npos,
        "transient endpoint must return an integrated result");

    const auto missing_time = api.handle(
        json_post(
            "/api/v1/simulations/transient?case_id=charge",
            transient_model));
    require(
        missing_time.status == 400 &&
            missing_time.body.find("missing required query parameter") !=
                std::string::npos,
        "transient endpoint must require an explicit end time");
}

void test_production_api_disables_synchronous_execution() {
    thermox::http::Api api;
    const auto response = api.handle(
        json_post(
            "/api/v1/simulations/steady",
            "{}"));
    require(
        response.status == 404 &&
            response.body.find(
                "synchronous simulation routes are disabled") !=
                std::string::npos,
        "production API defaults must not execute simulations "
        "inside the request process");
}

void test_transport_guards() {
    thermox::http::Api api;
    const auto no_content_type = api.handle(
        {"POST", "/api/v1/models/validate", {}, "{}"});
    require(
        no_content_type.status == 415,
        "JSON operations must enforce Content-Type");

    thermox::http::Api limited{
        thermox::service::make_default_simulation_runtime(),
        {.maximum_body_bytes = 4U}};
    const auto too_large = limited.handle(
        json_post("/api/v1/models/validate", "12345"));
    require(
        too_large.status == 413,
        "oversized bodies must be rejected before service execution");

    auto bad_schema = json_post(
        "/api/v1/models/validate", "{}");
    bad_schema.headers["Thermox-Command-Schema"] = "future";
    const auto schema = api.handle(bad_schema);
    require(
        schema.status == 400 &&
            schema.body.find("unsupported Thermox-Command-Schema") !=
                std::string::npos,
        "unsupported command schemas must be explicit");
}

void test_tenant_scoped_asynchronous_jobs() {
    const auto runtime =
        thermox::service::make_default_simulation_runtime();
    const auto jobs =
        thermox::service::make_in_memory_job_repository();
    const auto artifacts =
        thermox::service::make_in_memory_result_artifact_store();
    const auto job_service = std::make_shared<
        thermox::service::SimulationJobService>(
            runtime, jobs, artifacts);
    const auto project_service = std::make_shared<
        thermox::service::ProjectService>(
            thermox::service::
                make_in_memory_project_repository());
    const thermox::service::IdentityContext identity{
        "user-a", "team-a", "http-job-source"};
    thermox::http::Api api{
        runtime,
        job_service,
        project_service};
    const auto project = project_service->create_project({
        identity, "Job source", {},
    });
    const auto model = project_service->create_model_revision({
        identity,
        project.project_id,
        {},
        read_file(
            std::string(THERMOX_SOURCE_DIR) +
            "/core/examples/air_compressor.topology.json"),
    });
    const auto simulation_case =
        project_service->create_case_revision({
            identity,
            project.project_id,
            model.model_revision_id,
            {},
            read_file(
                std::string(THERMOX_SOURCE_DIR) +
                "/core/examples/"
                "air_compressor.design.case.json"),
        });
    auto artifact_upload = json_post(
        "/api/v1/projects/" + project.project_id +
            "/artifact-revisions"
            "?artifact_id=http-test-map"
            "&artifact_type=thermox.performance_map"
            "&artifact_schema_version="
            "thermox.performance_map%2Fv1",
        R"json({
          "primary_variable": {
            "name": "corrected_mass_flow",
            "dimension": "mass_flow"
          },
          "family_variable": {
            "name": "corrected_speed",
            "dimension": "angular_speed"
          },
          "output_variables": [
            {"name": "pressure_ratio",
             "dimension": "dimensionless"},
            {"name": "isentropic_efficiency",
             "dimension": "dimensionless"}
          ],
          "curves": [
            {"family_coordinate": 250.0, "samples": [
              {"coordinate": 70.0,
               "outputs": [10.0, 0.85]},
              {"coordinate": 120.0,
               "outputs": [10.0, 0.85]}
            ]},
            {"family_coordinate": 400.0, "samples": [
              {"coordinate": 70.0,
               "outputs": [10.0, 0.85]},
              {"coordinate": 120.0,
               "outputs": [10.0, 0.85]}
            ]}
          ]
        })json");
    const auto uploaded = api.handle(
        authenticated(std::move(artifact_upload)));
    require(
        uploaded.status == 201 &&
            uploaded.headers.contains("Location") &&
            uploaded.body.find("sha256:") !=
                std::string::npos,
        "artifact upload must publish immutable revision "
        "metadata");
    const auto artifact_revision_id =
        uploaded.headers.at("Location").substr(
            uploaded.headers.at("Location").find_last_of('/') +
            1U);
    auto run_upload = json_post(
        "/api/v1/projects/" + project.project_id +
            "/run-configuration-revisions",
        std::string{
            R"({"schema_version":)"
            R"("thermox.run_configuration.create/v2",)"
            R"("run_configuration_id":"http-design-run",)"
            R"("model_revision_id":")"} +
            model.model_revision_id +
            R"(","case_revision_id":")" +
            simulation_case.case_revision_id +
            R"(","artifact_revision_ids":[")" +
            artifact_revision_id +
            R"("],"steady_solver":{"max_iterations":37},)"
            R"("result_projections":[{)"
            R"("id":"compressor_outlet_temperature",)"
            R"("scope":"port_derived",)"
            R"("component_id":"compressor",)"
            R"("port_name":"outlet",)"
            R"("value_name":"T",)"
            R"("dimension":"temperature",)"
            R"("aggregation":"final"}]})");
    const auto run_created = api.handle(
        authenticated(std::move(run_upload)));
    require(
        run_created.status == 201 &&
            run_created.headers.contains("Location") &&
            run_created.body.find(
                "\"max_iterations\": 37") !=
                std::string::npos &&
            run_created.body.find(
                "\"id\": "
                "\"compressor_outlet_temperature\"") !=
                std::string::npos,
        "run configuration route must persist bindings and "
        "solver and projection policy");
    const auto run_configuration_revision_id =
        run_created.headers.at("Location").substr(
            run_created.headers.at("Location").find_last_of('/') +
            1U);

    auto submission = thermox::http::Request{
        "POST",
        "/api/v1/simulations?project_id=" +
            project.project_id +
            "&run_configuration_revision_id=" +
            run_configuration_revision_id,
        {},
        {}};
    submission.headers["Idempotency-Key"] = "http-job-1";
    const auto anonymous = api.handle(submission);
    require(
        anonymous.status == 401 &&
            anonymous.body.find("identity_required") !=
                std::string::npos,
        "stateful job submission must require trusted identity");

    auto cross_team_submission = authenticated(
        submission, "user-b", "team-b");
    require(
        api.handle(cross_team_submission).status == 404,
        "cross-team revision submission must not reveal "
        "project or revision existence");

    const auto queued =
        api.handle(authenticated(submission));
    require(
        queued.status == 202 &&
            queued.body.find("\"state\": \"queued\"") !=
                std::string::npos &&
            queued.body.find("\"team_id\": \"team-a\"") !=
                std::string::npos &&
            queued.body.find(model.checksum) !=
                std::string::npos &&
            queued.body.find(simulation_case.checksum) !=
                std::string::npos &&
            queued.body.find(artifact_revision_id) !=
                std::string::npos &&
            queued.body.find(
                run_configuration_revision_id) !=
                std::string::npos &&
            queued.headers.contains("Location"),
        "authenticated submission must create a Team-owned "
        "revision-backed job");
    const std::string job_id =
        queued.headers.at("Location").substr(
            std::string("/api/v1/simulations/").size());

    auto history_request = thermox::http::Request{
        "GET",
        "/api/v1/simulations?project_id=" +
            project.project_id +
            "&run_configuration_revision_id=" +
            run_configuration_revision_id +
            "&state=queued&limit=1",
        {},
        {}};
    const auto history = api.handle(
        authenticated(history_request));
    require(
        history.status == 200 &&
            history.body.find("\"schema_version\": "
                              "\"thermox.job_list/v1\"") !=
                std::string::npos &&
            history.body.find(job_id) != std::string::npos &&
            history.body.find("\"next_cursor\": null") !=
                std::string::npos,
        "Team history route must delegate filtered listing to "
        "the job service");
    const auto other_history = api.handle(authenticated(
        history_request, "user-b", "team-b"));
    require(
        other_history.status == 200 &&
            other_history.body.find(job_id) ==
                std::string::npos,
        "Team history route must not reveal cross-Team jobs");
    auto bad_history_request = history_request;
    bad_history_request.target =
        "/api/v1/simulations?cursor=not-a-cursor";
    require(
        api.handle(authenticated(bad_history_request)).status == 400,
        "malformed history cursors must be rejected at the "
        "transport boundary");

    auto cancellable_submission = submission;
    cancellable_submission.headers["Idempotency-Key"] =
        "http-job-cancellable";
    const auto cancellable =
        api.handle(authenticated(cancellable_submission));
    require(
        cancellable.status == 202 &&
            cancellable.headers.contains("Location") &&
            cancellable.headers.at("ETag") == "\"revision-1\"",
        "a second queued job must expose its concurrency ETag");
    const auto cancellable_job_id =
        cancellable.headers.at("Location").substr(
            std::string("/api/v1/simulations/").size());

    thermox::http::Request cancellation{
        "DELETE",
        "/api/v1/simulations/" + cancellable_job_id,
        {{"If-Match", "\"revision-1\""}},
        {},
    };
    require(
        api.handle(authenticated(
            cancellation, "user-b", "team-b")).status == 404,
        "cross-Team cancellation must not reveal job existence");
    auto missing_precondition = cancellation;
    missing_precondition.headers.clear();
    require(
        api.handle(authenticated(missing_precondition)).status == 428,
        "cancellation must require an optimistic concurrency "
        "precondition");
    auto malformed_precondition = cancellation;
    malformed_precondition.headers["If-Match"] = "revision-1";
    const auto malformed_response =
        api.handle(authenticated(malformed_precondition));
    require(
        malformed_response.status == 400,
        "cancellation must reject malformed revision ETags, got " +
            std::to_string(malformed_response.status));
    auto stale_precondition = cancellation;
    stale_precondition.headers["If-Match"] = "\"revision-999\"";
    require(
        api.handle(authenticated(stale_precondition)).status == 412,
        "cancellation must distinguish stale revisions from "
        "state conflicts");
    const auto cancelled =
        api.handle(authenticated(cancellation));
    require(
        cancelled.status == 200 &&
            cancelled.headers.at("ETag") == "\"revision-2\"" &&
            cancelled.body.find("\"state\": \"cancelled\"") !=
                std::string::npos,
        "valid cancellation must publish a new terminal job "
        "revision");
    require(
        api.handle(authenticated(cancellation)).status == 412,
        "repeating cancellation with a stale ETag must fail its "
        "precondition");
    auto terminal_cancellation = cancellation;
    terminal_cancellation.headers["If-Match"] = "\"revision-2\"";
    require(
        api.handle(authenticated(terminal_cancellation)).status ==
            409,
        "cancelling a terminal job at its current revision must "
        "report a state conflict");

    auto cancelled_history_request = thermox::http::Request{
        "GET",
        "/api/v1/simulations?state=cancelled",
        {},
        {}};
    const auto cancelled_history = api.handle(
        authenticated(cancelled_history_request));
    require(
        cancelled_history.status == 200 &&
            cancelled_history.body.find(cancellable_job_id) !=
                std::string::npos,
        "cancelled jobs must remain visible in run history");

    auto other_lookup = thermox::http::Request{
        "GET",
        "/api/v1/simulations/" + job_id,
        {},
        {}};
    other_lookup = authenticated(
        std::move(other_lookup), "user-b", "team-b");
    require(
        api.handle(other_lookup).status == 404,
        "cross-team status lookup must not reveal job existence");

    const auto completed =
        job_service->run_next("http-test-worker");
    require(
        completed.has_value() &&
            completed->state ==
                thermox::service::SimulationJobState::succeeded &&
            completed->result_summary.has_value() &&
            completed->result_summary->values.size() == 1U &&
            completed->result_summary->values.front().id ==
                "compressor_outlet_temperature",
        "worker must execute the submitted job");

    auto result_request = thermox::http::Request{
        "GET",
        "/api/v1/simulations/" + job_id + "/result",
        {},
        {}};
    const auto result = api.handle(
        authenticated(std::move(result_request)));
    require(
        result.status == 200 &&
            result.body.find("\"converged\": true") !=
                std::string::npos &&
            result.body.find(model.model_revision_id) !=
                std::string::npos &&
            result.body.find(
                simulation_case.case_revision_id) !=
                std::string::npos &&
            result.body.find(artifact_revision_id) !=
                std::string::npos &&
            result.body.find(
                run_configuration_revision_id) !=
                std::string::npos &&
            result.headers.contains("ETag"),
        "job result must publish stored revision provenance");
}

void test_team_scoped_projects_and_model_revisions() {
    thermox::http::Api api;
    auto invalid_create = authenticated(json_post(
        "/api/v1/projects",
        R"({"schema_version":"thermox.project.create/v1",)"
        R"("name":42})"));
    require(
        api.handle(invalid_create).status == 400,
        "project request fields must retain JSON types");

    auto create = json_post(
        "/api/v1/projects",
        R"({"schema_version":"thermox.project.create/v1",)"
        R"("name":"Cycle workspace",)"
        R"("description":"Team-owned models"})");
    create = authenticated(std::move(create));
    const auto created = api.handle(create);
    require(
        created.status == 201 &&
            created.headers.contains("Location") &&
            created.body.find("\"team_id\": \"team-a\"") !=
                std::string::npos &&
            created.body.find(
                "\"created_by_user_id\": \"user-a\"") !=
                std::string::npos,
        "project creation must expose Team ownership and actor "
        "audit metadata");
    const auto project_location =
        created.headers.at("Location");
    const auto project_id = project_location.substr(
        std::string("/api/v1/projects/").size());

    auto other_lookup = authenticated(
        {"GET", project_location, {}, {}},
        "user-b",
        "team-b");
    require(
        api.handle(other_lookup).status == 404,
        "cross-Team project lookup must hide existence");

    const std::string model = read_file(
        std::string(THERMOX_SOURCE_DIR) +
        "/core/examples/air_compressor.topology.json");
    auto revision_request = json_post(
        project_location + "/model-revisions",
        model);
    revision_request =
        authenticated(std::move(revision_request));
    const auto revision = api.handle(revision_request);
    require(
        revision.status == 201 &&
            revision.headers.contains("Location") &&
            revision.headers.contains("ETag") &&
            revision.body.find(
                "\"model_schema_version\": "
                "\"thermox.topology/v1\"") !=
                std::string::npos &&
            revision.body.find("\"model\": {") !=
                std::string::npos,
        "model revision creation must publish immutable "
        "canonical content and checksum metadata");

    auto history_request = authenticated({
        "GET",
        "/api/v1/projects/" + project_id +
            "/model-revisions",
        {},
        {},
    });
    const auto history = api.handle(history_request);
    require(
        history.status == 200 &&
            history.body.find("\"revision_number\": 1") !=
                std::string::npos &&
            history.body.find("\"model\": {") ==
                std::string::npos,
        "revision history must return metadata without "
        "duplicating model bodies");

    const std::string case_document = read_file(
        std::string(THERMOX_SOURCE_DIR) +
        "/core/examples/air_compressor.design.case.json");
    auto case_request = authenticated(json_post(
        revision.headers.at("Location") + "/case-revisions",
        case_document));
    const auto simulation_case = api.handle(case_request);
    require(
        simulation_case.status == 201 &&
            simulation_case.headers.contains("Location") &&
            simulation_case.headers.contains("ETag") &&
            simulation_case.body.find(
                "\"case_id\": \"design\"") !=
                std::string::npos &&
            simulation_case.body.find(
                "\"case_document\": {") !=
                std::string::npos,
        "case revision creation must bind canonical operating "
        "data to the exact model revision");

    auto case_history_request = authenticated({
        "GET",
        revision.headers.at("Location") + "/case-revisions",
        {},
        {},
    });
    const auto case_history = api.handle(case_history_request);
    require(
        case_history.status == 200 &&
            case_history.body.find("\"revision_number\": 1") !=
                std::string::npos &&
            case_history.body.find("\"case_document\": {") ==
                std::string::npos,
        "case history must return metadata without duplicating "
        "case documents");
}

}  // namespace

int main() {
    try {
        test_health_and_routing();
        test_catalog_and_validation();
        test_simulation_routes();
        test_production_api_disables_synchronous_execution();
        test_transport_guards();
        test_tenant_scoped_asynchronous_jobs();
        test_team_scoped_projects_and_model_revisions();
        std::cout << "http api tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "http api tests failed: " << error.what() << '\n';
        return 1;
    }
}
