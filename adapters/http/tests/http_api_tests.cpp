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
    thermox::http::Api api{
        runtime,
        job_service,
        std::make_shared<thermox::service::ProjectService>(
            thermox::service::
                make_in_memory_project_repository())};
    const std::string model = read_file(
        std::string(THERMOX_SOURCE_DIR) +
        "/core/examples/air_compressor.json");

    auto submission = json_post(
        "/api/v1/simulations?mode=steady&case_id=design",
        model);
    submission.headers["Idempotency-Key"] = "http-job-1";
    const auto anonymous = api.handle(submission);
    require(
        anonymous.status == 401 &&
            anonymous.body.find("identity_required") !=
                std::string::npos,
        "stateful job submission must require trusted identity");

    const auto queued =
        api.handle(authenticated(submission));
    require(
        queued.status == 202 &&
            queued.body.find("\"state\": \"queued\"") !=
                std::string::npos &&
            queued.body.find("\"team_id\": \"team-a\"") !=
                std::string::npos &&
            queued.headers.contains("Location"),
        "authenticated submission must create a team-owned job");
    const std::string job_id =
        queued.headers.at("Location").substr(
            std::string("/api/v1/simulations/").size());

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
                thermox::service::SimulationJobState::succeeded,
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
            result.headers.contains("ETag"),
        "job result route must publish the stored result artifact");
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
        "/core/examples/air_compressor.json");
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
                "\"thermox.model/v2\"") !=
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
