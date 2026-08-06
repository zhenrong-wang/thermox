#include "thermox/http/http_api.hpp"
#include "thermox/service/in_memory_jobs.hpp"
#include "thermox/service/in_memory_projects.hpp"

#include <boost/json.hpp>

#include <cmath>
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
        catalog.body.find("thermox.catalog/v6") != std::string::npos,
        "catalog endpoint must preserve the service schema");
    const auto parsed_catalog = boost::json::parse(catalog.body);
    require(
        parsed_catalog.is_object() &&
            parsed_catalog.as_object()
                .at("native_extensions")
                .is_array() &&
            parsed_catalog.as_object()
                .at("unit_dimensions")
                .is_array() &&
            !parsed_catalog.as_object()
                 .at("unit_dimensions")
                 .as_array()
                 .empty() &&
            parsed_catalog.as_object()
                .at("components")
                .is_array(),
        "catalog endpoint must return valid JSON arrays");

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

void test_correlation_template_instantiation() {
    thermox::http::Api api;
    const auto response = api.handle(json_post(
        "/api/v1/correlation-artifacts/instantiate",
        R"json({
          "schema_version": "thermox.command/v1",
          "artifact_id": "http-chisholm-family",
          "revision": "1",
          "bindings": [
            {
              "template_id":
                "chisholm_laminar_laminar_friction_gradient",
              "candidate_id": "ll"
            },
            {
              "template_id":
                "chisholm_laminar_turbulent_friction_gradient",
              "candidate_id": "lt"
            },
            {
              "template_id":
                "chisholm_turbulent_laminar_friction_gradient",
              "candidate_id": "tl"
            },
            {
              "template_id":
                "chisholm_turbulent_turbulent_friction_gradient",
              "candidate_id": "tt"
            }
          ]
        })json"));
    require(
        response.status == 200,
        "correlation instantiation endpoint must succeed");
    const auto parsed = boost::json::parse(response.body);
    const auto& root = parsed.as_object();
    const auto& artifact = root.at("artifact").as_object();
    require(
        root.at("schema_version").as_string() ==
                "thermox.correlation_instantiation/v1" &&
            artifact.at("artifact_type").as_string() ==
                "thermox.correlation" &&
            artifact.at("checksum_sha256").as_string().size() ==
                64U &&
            artifact.at("payload")
                    .as_object()
                    .at("candidates")
                    .as_array()
                    .size() == 4U,
        "HTTP operation must return a typed, content-addressed "
        "correlation family payload");

    const auto invalid = api.handle(json_post(
        "/api/v1/correlation-artifacts/instantiate",
        R"json({
          "schema_version": "thermox.command/v1",
          "artifact_id": "invalid",
          "revision": "1",
          "bindings": [{"template_id": "unknown"}]
        })json"));
    require(
        invalid.status == 400 &&
            invalid.body.find("correlation_instantiation_failed") !=
                std::string::npos,
        "unknown correlation templates must return a structured 400");

    const auto method = api.handle({
        "GET", "/api/v1/correlation-artifacts/instantiate", {}, {},
    });
    require(
        method.status == 405 && method.headers.at("Allow") == "POST",
        "correlation instantiation must reject unsupported methods");
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
    const auto artifact_detail = api.handle(authenticated({
        "GET",
        "/api/v1/projects/" + project.project_id +
            "/artifact-revisions/" + artifact_revision_id,
        {},
        {},
    }));
    require(
        artifact_detail.status == 200 &&
            artifact_detail.body.find(
                "thermox.artifact_revision_content/v1") !=
                std::string::npos &&
            artifact_detail.body.find("\"artifact\"") !=
                std::string::npos &&
            artifact_detail.body.find("corrected_mass_flow") !=
                std::string::npos &&
            artifact_detail.headers.contains("ETag"),
        "artifact detail must return immutable metadata and the "
        "integrity-checked canonical payload");
    const auto hidden_artifact_detail = api.handle(authenticated(
        {
            "GET",
            "/api/v1/projects/" + project.project_id +
                "/artifact-revisions/" + artifact_revision_id,
            {},
            {},
        },
        "other-user",
        "other-team"));
    require(
        hidden_artifact_detail.status == 404,
        "artifact payload reads must not expose cross-Team "
        "revisions");
    auto component_upload = json_post(
        "/api/v1/projects/" + project.project_id +
            "/artifact-revisions"
            "?artifact_id=http-test-gain"
            "&artifact_type=thermox.expression_component"
            "&artifact_schema_version="
            "thermox.expression_component%2Fv2",
        R"json({
          "kind": "custom.signal.http_gain",
          "version": "1.0.0",
          "template_kind": "signal.gain",
          "display_name": "Signal Gain",
          "category": "Control",
          "model_name": "Linear gain",
          "ports": [
            {"name": "input", "domain": "signal",
             "direction": "in"},
            {"name": "output", "domain": "signal",
             "direction": "out"}
          ],
          "parameters": [{
            "name": "gain",
            "dimension": "dimensionless",
            "required": true,
            "lower_bound": 0.0,
            "upper_bound": 100.0
          }],
          "equations": [{
            "name": "gain_law",
            "expression":
              "output.value - parameter.gain * input.value",
            "residual_scale": 1.0
          }]
        })json");
    const auto component_uploaded = api.handle(
        authenticated(std::move(component_upload)));
    require(
        component_uploaded.status == 201 &&
            component_uploaded.headers.contains("Location"),
        "HTTP artifact authoring must accept safe expression "
        "component definitions");
    const auto component_revision_id =
        component_uploaded.headers.at("Location").substr(
            component_uploaded.headers.at("Location")
                .find_last_of('/') +
            1U);
    const auto component_catalog = api.handle(authenticated({
        "GET",
        "/api/v1/projects/" + project.project_id +
            "/component-catalog",
        {},
        {},
    }));
    const auto parsed_component_catalog =
        boost::json::parse(component_catalog.body);
    require(
        component_catalog.status == 200 &&
            parsed_component_catalog.is_object() &&
            parsed_component_catalog.as_object()
                    .at("components")
                    .as_array()
                    .size() == 1U &&
            component_catalog.body.find(
                "\"schema_version\": "
                "\"thermox.project_component_catalog/v1\"") !=
                std::string::npos &&
            component_catalog.body.find(
                "custom.signal.http_gain") !=
                std::string::npos &&
            component_catalog.body.find(
                "output.value - parameter.gain * input.value") !=
                std::string::npos &&
            component_catalog.body.find(component_revision_id) !=
                std::string::npos,
        "project component catalog must expose editable "
        "definition, descriptor, and immutable source revision");
    require(
        api.handle(authenticated(
            {
                "GET",
                "/api/v1/projects/" + project.project_id +
                    "/component-catalog",
                {},
                {},
            },
            "user-b",
            "team-b"))
                .status == 404,
        "project component catalog must hide cross-Team "
        "project existence");
    auto template_upload = json_post(
        "/api/v1/projects/" + project.project_id +
            "/artifact-revisions"
            "?artifact_id=http-compressor-template"
            "&artifact_type=thermox.assembly_template"
            "&artifact_schema_version=thermox.topology%2Fv1",
        R"json({
          "schema_version": "thermox.topology/v1",
          "model": {
            "id": "compressor_template",
            "media": [{
              "id": "air",
              "backend": "ideal_gas_mixture",
              "substance": "Air"
            }],
            "components": [],
            "assemblies": [{
              "id": "compressor_train",
              "components": [{
                "id": "stage",
                "kind": "compressor.fluid.isentropic_efficiency",
                "media": {"inlet": "air", "outlet": "air"},
                "parameters": {"pressure_ratio": 2.0, "eta_is": 0.86}
              }],
              "ports": [
                {"name": "inlet", "endpoint": "stage.inlet"},
                {"name": "outlet", "endpoint": "stage.outlet"}
              ],
              "parameters": [],
              "connections": []
            }],
            "connections": []
          }
        })json");
    const auto template_uploaded = api.handle(
        authenticated(std::move(template_upload)));
    require(
        template_uploaded.status == 201 &&
            template_uploaded.body.find(
                "thermox.assembly_template") != std::string::npos,
        "HTTP artifact authoring must accept canonical assembly "
        "templates");
    const auto template_revision_id =
        template_uploaded.headers.at("Location").substr(
            template_uploaded.headers.at("Location").find_last_of('/') +
            1U);
    const auto template_detail = api.handle(authenticated({
        "GET",
        "/api/v1/projects/" + project.project_id +
            "/artifact-revisions/" + template_revision_id,
        {},
        {},
    }));
    require(
        template_detail.status == 200 &&
            template_detail.body.find(
                "\"id\": \"compressor_train\"") !=
                std::string::npos,
        "assembly-template revisions must be discoverable through "
        "the standard artifact registry");
    auto study_upload = json_post(
        "/api/v1/projects/" + project.project_id +
            "/study-revisions",
        std::string{
            R"({"schema_version":)"
            R"("thermox.study_revision.create/v1",)"
            R"("study_id":"http-design-study",)"
            R"("model_revision_id":")"} +
            model.model_revision_id +
            R"(","case_revision_id":")" +
            simulation_case.case_revision_id +
            R"(","intent":"steady_state_design",)"
            R"("artifact_revision_ids":[")" +
            artifact_revision_id +
            "\",\"" + component_revision_id +
            R"("],"result_projections":[{)"
            R"("id":"compressor_outlet_temperature",)"
            R"("scope":"port_derived",)"
            R"("component_id":"compressor",)"
            R"("port_name":"outlet","value_name":"T",)"
            R"("dimension":"temperature",)"
            R"("aggregation":"final"}]})");
    const auto study_created = api.handle(
        authenticated(std::move(study_upload)));
    require(
        study_created.status == 201 &&
            study_created.headers.contains("Location") &&
            study_created.body.find(
                "\"intent\": \"steady_state_design\"") !=
                std::string::npos,
        "study routes must persist durable engineering intent");
    const auto study_detail = api.handle(authenticated({
        "GET", study_created.headers.at("Location"), {}, {},
    }));
    const auto study_history = api.handle(authenticated({
        "GET",
        "/api/v1/projects/" + project.project_id +
            "/study-revisions",
        {},
        {},
    }));
    require(
        study_detail.status == 200 &&
            study_history.status == 200 &&
            study_history.body.find("http-design-study") !=
                std::string::npos,
        "study revisions must support detail and history reads");
    require(
        api.handle(authenticated(
            {"GET", study_created.headers.at("Location"), {}, {}},
            "user-b", "team-b")).status == 404,
        "study revision detail must hide cross-Team existence");
    const auto study_revision_id =
        study_created.headers.at("Location").substr(
            study_created.headers.at("Location").find_last_of('/') + 1U);
    auto calibration_upload = json_post(
        "/api/v1/projects/" + project.project_id +
            "/calibration-revisions",
        std::string{
            R"({"schema_version":)"
            R"("thermox.calibration_revision.create/v1",)"
            R"("calibration_id":"http-acceptance-fit",)"
            R"("model_revision_id":")"} +
            model.model_revision_id +
            R"(","training_study_revision_ids":[")" +
            study_revision_id +
            R"("],"validation_study_revision_ids":[],)"
            R"("definition":{"schema_version":"thermox.calibration/v1",)"
            R"("calibration":{"id":"http-acceptance-fit",)"
            R"("parameters":[{"id":"efficiency","scope":"component",)"
            R"("targets":["components.compressor.parameters.eta_is"],)"
            R"("cases":["design"],"bounds":{"lower":0.75,"upper":0.95}}],)"
            R"("observations":[{"id":"shaft-power","case":"design",)"
            R"("target":"compressor.shaft.W_dot",)"
            R"("measured":{"value":35.0,"unit":"MW"},)"
            R"("sigma":{"value":0.5,"unit":"MW"}}]}},)"
            R"("solver":{"max_iterations":11}})");
    const auto calibration_created = api.handle(
        authenticated(std::move(calibration_upload)));
    require(
        calibration_created.status == 201 &&
            calibration_created.headers.contains("Location") &&
            calibration_created.body.find(
                "\"max_iterations\": 11") != std::string::npos &&
            calibration_created.body.find(study_revision_id) !=
                std::string::npos,
        "calibration routes must bind immutable training Studies: " +
            std::to_string(calibration_created.status) + " " +
            calibration_created.body);
    const auto calibration_detail = api.handle(authenticated({
        "GET", calibration_created.headers.at("Location"), {}, {},
    }));
    const auto calibration_history = api.handle(authenticated({
        "GET",
        "/api/v1/projects/" + project.project_id +
            "/calibration-revisions",
        {}, {},
    }));
    require(
        calibration_detail.status == 200 &&
            calibration_history.status == 200 &&
            calibration_history.body.find("http-acceptance-fit") !=
                std::string::npos &&
            api.handle(authenticated(
                {"GET", calibration_created.headers.at("Location"), {}, {}},
                "user-b", "team-b")).status == 404,
        "calibration revisions must support scoped detail and history reads");
    const auto calibration_revision_id =
        calibration_created.headers.at("Location").substr(
            calibration_created.headers.at("Location").find_last_of('/') +
            1U);
    thermox::http::Request calibration_submission{
        "POST",
        "/api/v1/jobs?project_id=" + project.project_id +
            "&calibration_revision_id=" + calibration_revision_id,
        {{"Idempotency-Key", "http-calibration-job-1"}},
        {},
    };
    const auto calibration_queued = api.handle(
        authenticated(calibration_submission));
    require(
        calibration_queued.status == 202 &&
            calibration_queued.body.find(
                "\"mode\": \"calibration\"") !=
                std::string::npos &&
            calibration_queued.body.find(calibration_revision_id) !=
                std::string::npos,
        "calibration revisions must submit provenance-bound jobs");
    const auto calibration_completed =
        job_service->run_next("http-calibration-worker");
    require(
        calibration_completed &&
            calibration_completed->state ==
                thermox::service::SimulationJobState::succeeded,
        "the common worker must execute calibration jobs");
    const auto calibration_result = api.handle(authenticated({
        "GET",
        calibration_queued.headers.at("Location") + "/result",
        {}, {},
    }));
    require(
        calibration_result.status == 200 &&
            calibration_result.body.find("http-acceptance-fit") !=
                std::string::npos,
        "calibration results must cross the durable artifact boundary");
    auto run_upload = json_post(
        "/api/v1/projects/" + project.project_id +
            "/run-configuration-revisions",
        std::string{
            R"({"schema_version":)"
            R"("thermox.run_configuration.create/v3",)"
            R"("run_configuration_id":"http-design-run",)"
            R"("study_revision_id":")"} +
            study_revision_id +
            R"(","steady_solver":{"max_iterations":37}})");
    const auto run_created = api.handle(
        authenticated(std::move(run_upload)));
    require(
        run_created.status == 201 &&
            run_created.headers.contains("Location") &&
            run_created.body.find(
                "\"max_iterations\": 37") !=
                std::string::npos &&
            run_created.body.find(study_revision_id) !=
                std::string::npos,
        "run configuration route must persist its Study binding "
        "and solver policy");
    const auto run_configuration_revision_id =
        run_created.headers.at("Location").substr(
            run_created.headers.at("Location").find_last_of('/') +
            1U);

    auto submission = thermox::http::Request{
        "POST",
        "/api/v1/jobs?project_id=" +
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
            queued.body.find(component_revision_id) !=
                std::string::npos &&
            queued.body.find(
                run_configuration_revision_id) !=
                std::string::npos &&
            queued.body.find(study_revision_id) !=
                std::string::npos &&
            queued.body.find(
                "\"study_checksum\": \"sha256:") !=
                std::string::npos &&
            queued.headers.contains("Location"),
        "authenticated submission must create a Team-owned "
        "revision-backed job");
    const std::string job_id =
        queued.headers.at("Location").substr(
            std::string("/api/v1/jobs/").size());

    auto history_request = thermox::http::Request{
        "GET",
        "/api/v1/jobs?project_id=" +
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
        "/api/v1/jobs?cursor=not-a-cursor";
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
            std::string("/api/v1/jobs/").size());

    thermox::http::Request cancellation{
        "DELETE",
        "/api/v1/jobs/" + cancellable_job_id,
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
        "/api/v1/jobs?state=cancelled",
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
        "/api/v1/jobs/" + job_id,
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
            completed->execution.has_value() &&
            completed->execution->artifacts.size() == 2U &&
            completed->result_summary.has_value() &&
            completed->result_summary->values.size() == 1U &&
            completed->result_summary->values.front().id ==
                "compressor_outlet_temperature",
        "worker must execute the submitted job");

    auto result_request = thermox::http::Request{
        "GET",
        "/api/v1/jobs/" + job_id + "/result",
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
            result.body.find(component_revision_id) !=
                std::string::npos &&
            result.body.find(
                run_configuration_revision_id) !=
                std::string::npos &&
            result.headers.contains("ETag"),
        "job result must publish stored revision provenance");
}

void test_authored_component_job_workflow() {
    const auto runtime =
        thermox::service::make_default_simulation_runtime();
    const auto jobs =
        thermox::service::make_in_memory_job_repository();
    const auto results =
        thermox::service::make_in_memory_result_artifact_store();
    const auto job_service = std::make_shared<
        thermox::service::SimulationJobService>(
            runtime, jobs, results);
    const auto projects = std::make_shared<
        thermox::service::ProjectService>(
            thermox::service::
                make_in_memory_project_repository());
    const thermox::service::IdentityContext identity{
        "author-user", "author-team", "http-authoring"};
    thermox::http::Api api{
        runtime,
        job_service,
        projects};
    const auto project = projects->create_project({
        identity, "Authored component workflow", {},
    });

    const auto uploaded = api.handle(authenticated(
        json_post(
            "/api/v1/projects/" + project.project_id +
                "/artifact-revisions"
                "?artifact_id=authored-gain"
                "&artifact_type=thermox.expression_component"
                "&artifact_schema_version="
                "thermox.expression_component%2Fv2",
            R"json({
              "kind": "custom.signal.authored_gain",
              "version": "1.0.0",
              "template_kind": "signal.gain",
              "display_name": "Authored Gain",
              "category": "Control",
              "model_name": "Linear gain",
              "ports": [
                {"name": "input", "domain": "signal",
                 "direction": "in"},
                {"name": "output", "domain": "signal",
                 "direction": "out"}
              ],
              "parameters": [{
                "name": "gain",
                "dimension": "dimensionless",
                "required": true,
                "lower_bound": 0.0
              }],
              "equations": [{
                "name": "gain_law",
                "expression":
                  "output.value - parameter.gain * input.value",
                "residual_scale": 1.0
              }]
            })json"),
        identity.user_id,
        identity.team_id));
    require(
        uploaded.status == 201 &&
            uploaded.headers.contains("Location"),
        "the authored workflow must publish a safe component "
        "artifact");
    const auto component_revision_id =
        uploaded.headers.at("Location").substr(
            uploaded.headers.at("Location").find_last_of('/') +
            1U);

    const auto model = projects->create_model_revision({
        identity,
        project.project_id,
        {},
        R"json({
          "schema_version": "thermox.topology/v1",
          "model": {
            "id": "authored_gain_system",
            "media": [],
            "components": [{
              "id": "gain",
              "kind": "custom.signal.authored_gain",
              "version": "1.0.0",
              "parameters": {"gain": 2.0}
            }],
            "connections": []
          }
        })json",
    });
    const auto simulation_case =
        projects->create_case_revision({
            identity,
            project.project_id,
            model.model_revision_id,
            {},
            R"json({
              "schema_version": "thermox.case/v1",
              "case": {
                "id": "design",
                "mode": "steady_state_design",
                "fixed_values": {"gain.input.value": 5.0}
              }
            })json",
        });

    const auto case_location =
        "/api/v1/projects/" + project.project_id +
        "/model-revisions/" + model.model_revision_id +
        "/case-revisions/" +
        simulation_case.case_revision_id;
    const auto validation = api.handle(authenticated(
        json_post(
            case_location + "/validate",
            std::string{
                R"({"schema_version":)"
                R"("thermox.project_model_validation_request/v1",)"
                R"("artifact_revision_ids":[")"} +
                component_revision_id + R"("]})"),
        identity.user_id,
        identity.team_id));
    require(
        validation.status == 200 &&
            validation.body.find("\"compiled\": true") !=
                std::string::npos &&
            validation.body.find(
                "\"readiness\": {\"calculatable\": true") !=
                std::string::npos &&
            validation.body.find(
                "\"id\": \"execution\", \"state\": \"ready\"") !=
                std::string::npos &&
            validation.body.find(component_revision_id) !=
                std::string::npos,
        "the authored component must compile through exact "
        "revision-backed validation");

    const auto authored_study = api.handle(authenticated(
        json_post(
            "/api/v1/projects/" + project.project_id +
                "/study-revisions",
            std::string{
                R"({"schema_version":)"
                R"("thermox.study_revision.create/v1",)"
                R"("study_id":"authored-gain-study",)"
                R"("model_revision_id":")"} +
                model.model_revision_id +
                R"(","case_revision_id":")" +
                simulation_case.case_revision_id +
                R"(","intent":"steady_state_design",)"
                R"("artifact_revision_ids":[")" +
                component_revision_id +
                R"("],"result_projections":[{)"
                R"("id":"gain_output","scope":"port_primary",)"
                R"("component_id":"gain","port_name":"output",)"
                R"("value_name":"value",)"
                R"("dimension":"dimensionless",)"
                R"("aggregation":"final"}],)"
                R"("acceptance_criteria":[{)"
                R"("id":"gain-output-band",)"
                R"("projection_id":"gain_output",)"
                R"("dimension":"dimensionless",)"
                R"("lower_bound_si":9.0,)"
                R"("upper_bound_si":null}]})"),
        identity.user_id,
        identity.team_id));
    require(
        authored_study.status == 201 &&
            authored_study.headers.contains("Location") &&
            authored_study.body.find(
                "\"acceptance_criteria\": [") !=
                std::string::npos,
        "the authored workflow must publish an immutable study");
    const auto authored_study_revision_id =
        authored_study.headers.at("Location").substr(
            authored_study.headers.at("Location").find_last_of('/') + 1U);

    const auto run_created = api.handle(authenticated(
        json_post(
            "/api/v1/projects/" + project.project_id +
                "/run-configuration-revisions",
            std::string{
                R"({"schema_version":)"
                R"("thermox.run_configuration.create/v3",)"
                R"("run_configuration_id":"authored-gain-run",)"
                R"("study_revision_id":")"} +
                authored_study_revision_id + R"("})"),
        identity.user_id,
        identity.team_id));
    require(
        run_created.status == 201 &&
            run_created.headers.contains("Location"),
        "the authored workflow must persist an executable run "
        "configuration");
    const auto run_revision_id =
        run_created.headers.at("Location").substr(
            run_created.headers.at("Location").find_last_of('/') +
            1U);

    auto submission = authenticated(
        {
            "POST",
            "/api/v1/jobs?project_id=" +
                project.project_id +
                "&run_configuration_revision_id=" +
                run_revision_id,
            {},
            {},
        },
        identity.user_id,
        identity.team_id);
    submission.headers["Idempotency-Key"] =
        "authored-component-job";
    const auto queued = api.handle(submission);
    require(
        queued.status == 202 &&
            queued.headers.contains("Location") &&
            queued.body.find(component_revision_id) !=
                std::string::npos,
        "the authored workflow must queue a job with exact "
        "component provenance");
    const auto job_id =
        queued.headers.at("Location").substr(
            std::string("/api/v1/jobs/").size());
    const auto completed =
        job_service->run_next("authored-component-worker");
    require(
        completed &&
            completed->state ==
                thermox::service::SimulationJobState::succeeded &&
            completed->result_summary &&
            completed->result_summary->engineering_acceptance &&
            completed->result_summary->engineering_acceptance->passed &&
            completed->result_summary->values.size() == 1U &&
            std::abs(
                completed->result_summary->values.front()
                    .value_si -
                10.0) < 1.0e-12,
        "the worker must execute authored equations and "
        "project the expected output");

    const auto result = api.handle(authenticated(
        {
            "GET",
            "/api/v1/jobs/" + job_id + "/result",
            {},
            {},
        },
        identity.user_id,
        identity.team_id));
    require(
        result.status == 200 &&
            result.body.find("\"port_name\": \"output\"") !=
                std::string::npos &&
            result.body.find("\"value_si\": 10") !=
                std::string::npos &&
            result.body.find(component_revision_id) !=
                std::string::npos,
        "the authored workflow must expose calculated output "
        "and immutable component provenance");

    submission.headers["Idempotency-Key"] =
        "authored-component-comparison-job";
    const auto candidate_queued = api.handle(submission);
    require(
        candidate_queued.status == 202,
        "a second immutable run must be available for comparison");
    const auto candidate_job_id =
        candidate_queued.headers.at("Location").substr(
            std::string("/api/v1/jobs/").size());
    const auto candidate_completed =
        job_service->run_next("authored-comparison-worker");
    require(
        candidate_completed &&
            candidate_completed->state ==
                thermox::service::SimulationJobState::succeeded,
        "comparison candidate must complete successfully");
    const auto comparison = api.handle(authenticated(
        json_post(
            "/api/v1/job-comparisons",
            std::string{
                R"({"schema_version":)"
                R"("thermox.job_comparison.create/v1",)"
                R"("baseline_job_id":")"} +
                job_id + R"(","candidate_job_id":")" +
                candidate_job_id + R"("})"),
        identity.user_id,
        identity.team_id));
    require(
        comparison.status == 200 &&
            comparison.body.find(
                "\"thermox.job_comparison/v1\"") !=
                std::string::npos &&
            comparison.body.find("\"matched_count\": 1") !=
                std::string::npos &&
            comparison.body.find("\"absolute_delta_si\": 0") !=
                std::string::npos &&
            comparison.body.find(
                "\"accepted_to_accepted\"") !=
                std::string::npos,
        "HTTP comparison must expose service-owned aligned deltas "
        "and acceptance transitions");
    const auto hidden_comparison = api.handle(authenticated(
        json_post(
            "/api/v1/job-comparisons",
            std::string{
                R"({"schema_version":)"
                R"("thermox.job_comparison.create/v1",)"
                R"("baseline_job_id":")"} +
                job_id + R"(","candidate_job_id":")" +
                candidate_job_id + R"("})"),
        "user-b", "team-b"));
    require(
        hidden_comparison.status == 404,
        "HTTP comparison must preserve Team non-disclosure");
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

    auto edit_request = authenticated(json_post(
        revision.headers.at("Location") + "/edits",
        R"json({
          "schema_version": "thermox.graph_edit_batch/v1",
          "operations": [{
            "action": "upsert",
            "entity_type": "component",
            "entity_id": "compressor",
            "entity": {
              "id": "compressor",
              "label": "Main compressor",
              "kind": "compressor.fluid.isentropic_efficiency",
              "version": "1.0.0",
              "media": {
                "inlet": "air",
                "outlet": "air"
              },
              "parameters": {
                "pressure_ratio": 14.0,
                "eta_is": 0.87
              }
            }
          }, {
            "action": "upsert",
            "entity_type": "assembly",
            "entity_id": "booster",
            "entity": {
              "id": "booster",
              "components": [{
                "id": "stage_1",
                "kind": "compressor.fluid.isentropic_efficiency",
                "media": {"inlet": "air", "outlet": "air"},
                "parameters": {"pressure_ratio": 2.0, "eta_is": 0.84}
              }],
              "connections": [],
              "ports": [
                {"name": "inlet", "endpoint": "stage_1.inlet"},
                {"name": "outlet", "endpoint": "stage_1.outlet"}
              ],
              "parameters": []
            }
          }]
        })json"));
    const auto edited = api.handle(edit_request);
    require(
        edited.status == 201 &&
            edited.headers.contains("Location") &&
            edited.headers.at("Location") !=
                revision.headers.at("Location") &&
            edited.body.find(
                "\"parent_model_revision_id\": \"") !=
                std::string::npos &&
            edited.body.find(
                "\"label\": \"Main compressor\"") !=
                std::string::npos &&
            edited.body.find(
                "\"pressure_ratio\": 14") !=
                std::string::npos &&
            edited.body.find(
                "\"endpoint\": \"stage_1.inlet\"") !=
                std::string::npos,
        "graph edits must publish a typed immutable child "
        "revision without losing JSON numeric types");

    auto invalid_edit = authenticated(json_post(
        edited.headers.at("Location") + "/edits",
        R"json({
          "schema_version": "thermox.graph_edit_batch/v1",
          "operations": [{
            "action": "remove",
            "entity_type": "medium",
            "entity_id": "air"
          }]
        })json"));
    require(
        api.handle(invalid_edit).status == 400,
        "graph edits must reject removal of referenced domain "
        "entities");

    auto foreign_edit = authenticated(
        json_post(
            revision.headers.at("Location") + "/edits",
            R"json({
              "schema_version": "thermox.graph_edit_batch/v1",
              "operations": [{
                "action": "remove",
                "entity_type": "component",
                "entity_id": "compressor",
                "cascade": true
              }]
            })json"),
        "user-b",
        "team-b");
    require(
        api.handle(foreign_edit).status == 404,
        "cross-Team graph edits must hide revision existence");

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
            history.body.find("\"revision_number\": 2") !=
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

    auto case_edit_request = authenticated(json_post(
        simulation_case.headers.at("Location") + "/edits",
        R"json({
          "schema_version": "thermox.case_edit_batch/v1",
          "operations": [
            {
              "action": "upsert",
              "field": "label",
              "value": "Hot-day design"
            },
            {
              "action": "upsert",
              "field": "fixed_value",
              "key": "compressor.inlet.p",
              "value": {"value": 2.0, "unit": "bar"}
            }
          ]
        })json"));
    const auto edited_case = api.handle(case_edit_request);
    require(
        edited_case.status == 201 &&
            edited_case.headers.contains("Location") &&
            edited_case.headers.at("Location") !=
                simulation_case.headers.at("Location") &&
            edited_case.body.find(
                "\"label\": \"Hot-day design\"") !=
                std::string::npos &&
            edited_case.body.find(
                "\"value\": 200000") !=
                std::string::npos &&
            edited_case.body.find(
                "\"parent_case_revision_id\": \"") !=
                std::string::npos,
        "typed case edits must publish an SI-normalized "
        "immutable child revision");

    auto invalid_case_edit = authenticated(json_post(
        edited_case.headers.at("Location") + "/edits",
        R"json({
          "schema_version": "thermox.case_edit_batch/v1",
          "operations": [{
            "action": "remove",
            "field": "fixed_value",
            "key": "missing.value"
          }]
        })json"));
    require(
        api.handle(invalid_case_edit).status == 400,
        "invalid case edit batches must be rejected");

    auto validation_request = authenticated(json_post(
        edited_case.headers.at("Location") + "/validate",
        R"json({
          "schema_version":
            "thermox.project_model_validation_request/v1",
          "artifact_revision_ids": []
        })json"));
    const auto validation = api.handle(validation_request);
    require(
        validation.status == 200 &&
            validation.body.find(
                "\"schema_version\": "
                "\"thermox.project_model_validation/v1\"") !=
                std::string::npos &&
            validation.body.find("\"compiled\": true") !=
                std::string::npos &&
            validation.body.find(project_id) !=
                std::string::npos &&
            validation.body.find(
                "\"case_revision_id\": \"") !=
                std::string::npos,
        "revision-backed validation must compile an exact "
        "Team-scoped topology/case pair and return provenance");

    auto invalid_validation = authenticated(json_post(
        simulation_case.headers.at("Location") + "/validate",
        R"json({
          "schema_version":
            "thermox.project_model_validation_request/v1",
          "artifact_revision_ids": [42]
        })json"));
    require(
        api.handle(invalid_validation).status == 400,
        "revision validation must retain artifact ID JSON "
        "types");

    auto foreign_validation = authenticated(
        json_post(
            simulation_case.headers.at("Location") +
                "/validate",
            R"json({
              "schema_version":
                "thermox.project_model_validation_request/v1"
            })json"),
        "user-b",
        "team-b");
    require(
        api.handle(foreign_validation).status == 404,
        "revision validation must hide cross-Team model "
        "existence");

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
            case_history.body.find("\"revision_number\": 2") !=
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
        test_correlation_template_instantiation();
        test_simulation_routes();
        test_production_api_disables_synchronous_execution();
        test_transport_guards();
        test_tenant_scoped_asynchronous_jobs();
        test_authored_component_job_workflow();
        test_team_scoped_projects_and_model_revisions();
        std::cout << "http api tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "http api tests failed: " << error.what() << '\n';
        return 1;
    }
}
