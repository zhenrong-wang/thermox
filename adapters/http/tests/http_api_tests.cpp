#include "thermox/http/http_api.hpp"

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
    thermox::http::Api api;
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

}  // namespace

int main() {
    try {
        test_health_and_routing();
        test_catalog_and_validation();
        test_simulation_routes();
        test_transport_guards();
        std::cout << "http api tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "http api tests failed: " << error.what() << '\n';
        return 1;
    }
}
