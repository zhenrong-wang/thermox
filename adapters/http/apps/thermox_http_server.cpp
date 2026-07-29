#include "thermox/http/http_api.hpp"
#include "thermox/service/in_memory_jobs.hpp"

#ifdef THERMOX_HAS_POSTGRES_JOBS
#include "thermox/postgres/postgres_job_repository.hpp"
#endif

#ifdef THERMOX_HAS_OBJECT_ARTIFACTS
#include "thermox/object_store/result_artifact_store.hpp"
#endif

#ifdef THERMOX_HAS_S3_OBJECT_STORE
#include "thermox/object_store/s3_compatible_object_store.hpp"
#endif

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

struct Options {
    std::string listen_address{"127.0.0.1"};
    unsigned short port{8080};
    std::size_t maximum_body_bytes{10U * 1024U * 1024U};
    std::string local_user_id{"local-user"};
    std::string local_team_id{"local-team"};
    std::string worker_id{"local-worker"};
    std::chrono::milliseconds worker_lease_duration{30000};
    std::chrono::milliseconds worker_heartbeat_interval{10000};
    std::uint32_t worker_maximum_attempts{3};
    std::string postgres_url;
    std::string object_store_driver;
    std::string object_key_prefix{"results"};
    std::string s3_endpoint;
    std::string s3_region{"us-east-1"};
    std::string s3_bucket;
    std::string s3_access_key;
    std::string s3_secret_key;
    std::string s3_addressing_style{"path"};
    bool allow_insecure_remote{false};
};

void usage(std::ostream& out) {
    out << "Usage: thermox_http_server"
        << " [--listen <address>]"
        << " [--port <1-65535>]"
        << " [--max-body-bytes <positive integer>]"
        << " [--local-user-id <id>]"
        << " [--local-team-id <id>]"
        << " [--worker-id <id>]"
        << " [--allow-insecure-remote]\n"
        << "Set THERMOX_POSTGRES_URL to persist simulation "
           "job metadata in PostgreSQL.\n"
        << "Set THERMOX_OBJECT_STORE_DRIVER=s3-compatible and "
           "THERMOX_S3_* variables for durable result content.\n"
        << "Worker lease policy uses THERMOX_WORKER_LEASE_MS, "
           "THERMOX_WORKER_HEARTBEAT_MS, and "
           "THERMOX_WORKER_MAX_ATTEMPTS.\n";
}

template <typename Integer>
Integer parse_integer(
    const std::string& text,
    const std::string& option,
    Integer minimum,
    Integer maximum) {
    Integer value{};
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end ||
        value < minimum || value > maximum) {
        throw std::invalid_argument(
            option + " is outside its supported range");
    }
    return value;
}

Options parse_options(int argc, char** argv) {
    Options options;
    const auto environment = [](
                                 const char* name,
                                 std::string fallback = {}) {
        const char* value = std::getenv(name);
        return value == nullptr
            ? std::move(fallback)
            : std::string(value);
    };
    options.postgres_url =
        environment("THERMOX_POSTGRES_URL");
    options.object_store_driver =
        environment("THERMOX_OBJECT_STORE_DRIVER");
    options.object_key_prefix = environment(
        "THERMOX_OBJECT_KEY_PREFIX", "results");
    options.s3_endpoint =
        environment("THERMOX_S3_ENDPOINT");
    options.s3_region =
        environment("THERMOX_S3_REGION", "us-east-1");
    options.s3_bucket =
        environment("THERMOX_S3_BUCKET");
    options.s3_access_key =
        environment("THERMOX_S3_ACCESS_KEY");
    options.s3_secret_key =
        environment("THERMOX_S3_SECRET_KEY");
    options.s3_addressing_style =
        environment("THERMOX_S3_ADDRESSING_STYLE", "path");
    options.worker_lease_duration =
        std::chrono::milliseconds{
            parse_integer<long long>(
                environment(
                    "THERMOX_WORKER_LEASE_MS", "30000"),
                "THERMOX_WORKER_LEASE_MS",
                1,
                std::numeric_limits<long long>::max())};
    options.worker_heartbeat_interval =
        std::chrono::milliseconds{
            parse_integer<long long>(
                environment(
                    "THERMOX_WORKER_HEARTBEAT_MS", "10000"),
                "THERMOX_WORKER_HEARTBEAT_MS",
                1,
                std::numeric_limits<long long>::max())};
    options.worker_maximum_attempts =
        parse_integer<std::uint32_t>(
            environment(
                "THERMOX_WORKER_MAX_ATTEMPTS", "3"),
            "THERMOX_WORKER_MAX_ATTEMPTS",
            1,
            std::numeric_limits<std::uint32_t>::max());
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto require_value = [&]() -> std::string {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "missing value for " + argument);
            }
            return argv[++index];
        };
        if (argument == "--listen") {
            options.listen_address = require_value();
        } else if (argument == "--port") {
            options.port = parse_integer<unsigned short>(
                require_value(), argument, 1, 65535);
        } else if (argument == "--max-body-bytes") {
            options.maximum_body_bytes = parse_integer<std::size_t>(
                require_value(), argument, 1,
                std::numeric_limits<std::size_t>::max());
        } else if (argument == "--local-user-id") {
            options.local_user_id = require_value();
        } else if (argument == "--local-team-id") {
            options.local_team_id = require_value();
        } else if (argument == "--worker-id") {
            options.worker_id = require_value();
        } else if (argument == "--allow-insecure-remote") {
            options.allow_insecure_remote = true;
        } else if (argument == "--help" || argument == "-h") {
            usage(std::cout);
            std::exit(0);
        } else {
            throw std::invalid_argument(
                "unknown argument: " + argument);
        }
    }
    if (options.local_user_id.empty() ||
        options.local_team_id.empty() ||
        options.worker_id.empty()) {
        throw std::invalid_argument(
            "local identity and worker IDs must not be empty");
    }
    if (!options.object_store_driver.empty() &&
        options.object_store_driver != "s3-compatible") {
        throw std::invalid_argument(
            "unsupported object store driver: " +
            options.object_store_driver);
    }
    if (options.worker_heartbeat_interval >=
        options.worker_lease_duration) {
        throw std::invalid_argument(
            "worker heartbeat interval must be shorter than "
            "the worker lease");
    }
    return options;
}

thermox::http::Request adapt_request(
    const http::request<http::string_body>& request,
    const thermox::service::IdentityContext& identity) {
    thermox::http::Request adapted;
    adapted.method = std::string(request.method_string());
    adapted.target = std::string(request.target());
    adapted.body = request.body();
    adapted.identity = identity;
    for (const auto& field : request) {
        adapted.headers.emplace(
            std::string(field.name_string()),
            std::string(field.value()));
    }
    return adapted;
}

http::response<http::string_body> adapt_response(
    const thermox::http::Response& response,
    unsigned version,
    bool keep_alive) {
    http::response<http::string_body> adapted{
        static_cast<http::status>(response.status), version};
    for (const auto& [name, value] : response.headers) {
        adapted.set(name, value);
    }
    adapted.keep_alive(keep_alive);
    adapted.body() = response.body;
    adapted.prepare_payload();
    return adapted;
}

void serve_connection(
    tcp::socket socket,
    const thermox::http::Api& api,
    const thermox::service::IdentityContext& identity,
    std::size_t maximum_body_bytes) {
    beast::flat_buffer buffer;
    for (;;) {
        http::request_parser<http::string_body> parser;
        parser.body_limit(maximum_body_bytes);
        beast::error_code error;
        http::read(socket, buffer, parser, error);
        if (error == http::error::end_of_stream) break;
        if (error) {
            throw beast::system_error(error);
        }

        auto request = parser.release();
        const bool keep_alive = request.keep_alive();
        auto response = adapt_response(
            api.handle(adapt_request(request, identity)),
            request.version(), keep_alive);
        http::write(socket, response, error);
        if (error) throw beast::system_error(error);
        if (!keep_alive) break;
    }
    beast::error_code ignored;
    socket.shutdown(tcp::socket::shutdown_send, ignored);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        const auto address =
            asio::ip::make_address(options.listen_address);
        if (!address.is_loopback() &&
            !options.allow_insecure_remote) {
            throw std::invalid_argument(
                "non-loopback listen addresses require "
                "--allow-insecure-remote because authentication "
                "is not configured");
        }
        asio::io_context context{1};
        tcp::acceptor acceptor{
            context, {address, options.port}};
        const auto runtime =
            thermox::service::make_default_simulation_runtime();
        std::shared_ptr<
            thermox::service::SimulationJobRepository> jobs;
        if (options.postgres_url.empty()) {
            jobs =
                thermox::service::make_in_memory_job_repository();
        } else {
#ifdef THERMOX_HAS_POSTGRES_JOBS
            jobs =
                thermox::postgres::make_postgres_job_repository(
                    options.postgres_url);
#else
            throw std::runtime_error(
                "THERMOX_POSTGRES_URL was set, but this build "
                "does not include the PostgreSQL adapter");
#endif
        }
        std::shared_ptr<
            thermox::service::ResultArtifactStore> artifacts;
        if (options.object_store_driver.empty()) {
            artifacts =
                thermox::service::
                    make_in_memory_result_artifact_store();
        } else {
#if defined(THERMOX_HAS_OBJECT_ARTIFACTS) && \
    defined(THERMOX_HAS_S3_OBJECT_STORE)
            thermox::object_store::S3AddressingStyle style;
            if (options.s3_addressing_style == "path") {
                style = thermox::object_store::
                    S3AddressingStyle::path;
            } else if (
                options.s3_addressing_style ==
                "virtual-hosted") {
                style = thermox::object_store::
                    S3AddressingStyle::virtual_hosted;
            } else {
                throw std::invalid_argument(
                    "THERMOX_S3_ADDRESSING_STYLE must be path "
                    "or virtual-hosted");
            }
            auto objects = thermox::object_store::
                make_s3_compatible_object_store({
                    .endpoint = options.s3_endpoint,
                    .region = options.s3_region,
                    .bucket = options.s3_bucket,
                    .access_key = options.s3_access_key,
                    .secret_key = options.s3_secret_key,
                    .addressing_style = style,
                });
            artifacts = thermox::object_store::
                make_object_result_artifact_store(
                    std::move(objects),
                    options.object_key_prefix);
#else
            throw std::runtime_error(
                "object storage was configured, but this build "
                "does not include the S3-compatible driver");
#endif
        }
        const auto job_service = std::make_shared<
            thermox::service::SimulationJobService>(
                runtime, jobs, artifacts);
        thermox::http::Api api{
            runtime, job_service,
            {.maximum_body_bytes = options.maximum_body_bytes}};
        const thermox::service::IdentityContext identity{
            options.local_user_id,
            options.local_team_id,
            "local-http"};
        const thermox::service::SimulationWorkerSettings
            worker_settings{
                options.worker_lease_duration,
                options.worker_heartbeat_interval,
                options.worker_maximum_attempts,
            };
        std::jthread worker(
            [
                job_service,
                worker_id = options.worker_id,
                worker_settings
            ](
                const std::stop_token& stop) {
                while (!stop.stop_requested()) {
                    try {
                        if (job_service->run_next(
                                worker_id,
                                worker_settings)) {
                            continue;
                        }
                    } catch (const std::exception& error) {
                        std::cerr << "HTTP worker failed: "
                                  << error.what() << '\n';
                    }
                    if (!stop.stop_requested()) {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(100));
                    }
                }
            });

        std::cout << "Thermox HTTP listening on "
                  << address.to_string() << ':' << options.port
                  << " (job metadata: "
                  << (options.postgres_url.empty()
                          ? "memory"
                          : "postgresql")
                  << ", result content: "
                  << (options.object_store_driver.empty()
                          ? "memory"
                          : options.object_store_driver)
                  << ")\n";
        for (;;) {
            try {
                serve_connection(
                    acceptor.accept(), api, identity,
                    options.maximum_body_bytes);
            } catch (const std::exception& error) {
                std::cerr << "HTTP connection failed: "
                          << error.what() << '\n';
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "thermox_http_server error: "
                  << error.what() << '\n';
        usage(std::cerr);
        return 1;
    }
}
