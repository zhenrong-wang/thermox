#include "thermox/host/host_runtime.hpp"
#include "thermox/http/http_api.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

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
    bool allow_insecure_remote{false};
};

void usage(std::ostream& out) {
    out << "Usage: thermox_api_server"
        << " [--listen <address>]"
        << " [--port <1-65535>]"
        << " [--max-body-bytes <positive integer>]"
        << " [--local-user-id <id>]"
        << " [--local-team-id <id>]"
        << " [--allow-insecure-remote]\n"
        << "The API role requires durable storage configured "
           "through THERMOX_* environment variables.\n";
}

template <typename Integer>
Integer parse_integer(
    const std::string& text,
    const std::string& option,
    Integer minimum,
    Integer maximum) {
    Integer value{};
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size() ||
        value < minimum || value > maximum) {
        throw std::invalid_argument(
            option + " is outside its supported range");
    }
    return value;
}

Options parse_options(int argc, char** argv) {
    Options options;
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
            options.maximum_body_bytes =
                parse_integer<std::size_t>(
                    require_value(),
                    argument,
                    1,
                    std::numeric_limits<std::size_t>::max());
        } else if (argument == "--local-user-id") {
            options.local_user_id = require_value();
        } else if (argument == "--local-team-id") {
            options.local_team_id = require_value();
        } else if (argument == "--allow-insecure-remote") {
            options.allow_insecure_remote = true;
        } else if (
            argument == "--help" || argument == "-h") {
            usage(std::cout);
            std::exit(0);
        } else {
            throw std::invalid_argument(
                "unknown argument: " + argument);
        }
    }
    if (options.local_user_id.empty() ||
        options.local_team_id.empty()) {
        throw std::invalid_argument(
            "local identity IDs must not be empty");
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
        if (error == http::error::end_of_stream) {
            break;
        }
        if (error) {
            throw beast::system_error(error);
        }

        auto request = parser.release();
        const bool keep_alive = request.keep_alive();
        auto response = adapt_response(
            api.handle(adapt_request(request, identity)),
            request.version(),
            keep_alive);
        http::write(socket, response, error);
        if (error) {
            throw beast::system_error(error);
        }
        if (!keep_alive) {
            break;
        }
    }
    beast::error_code ignored;
    socket.shutdown(tcp::socket::shutdown_send, ignored);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        const auto persistence =
            thermox::host::persistence_from_environment();
        thermox::host::require_durable(persistence);
        const auto address =
            asio::ip::make_address(options.listen_address);
        if (!address.is_loopback() &&
            !options.allow_insecure_remote) {
            throw std::invalid_argument(
                "non-loopback listen addresses require "
                "--allow-insecure-remote because authentication "
                "is not configured");
        }

        const auto runtime =
            thermox::service::make_default_simulation_runtime();
        const auto job_service = std::make_shared<
            thermox::service::SimulationJobService>(
                runtime,
                thermox::host::make_job_repository(persistence),
                thermox::host::make_result_artifact_store(
                    persistence));
        const auto project_service = std::make_shared<
            thermox::service::ProjectService>(
                thermox::host::make_project_repository(
                    persistence),
                thermox::host::
                    make_engineering_artifact_content_store(
                        persistence));
        thermox::http::Api api{
            runtime,
            job_service,
            project_service,
            {
                .maximum_body_bytes =
                    options.maximum_body_bytes,
                .enable_synchronous_simulations = false,
            }};
        const thermox::service::IdentityContext identity{
            options.local_user_id,
            options.local_team_id,
            "local-http"};

        asio::io_context context{1};
        tcp::acceptor acceptor{
            context, {address, options.port}};
        std::cout << "Thermox API listening on "
                  << address.to_string() << ':' << options.port
                  << " ("
                  << thermox::host::persistence_description(
                         persistence)
                  << ")\n";
        for (;;) {
            try {
                serve_connection(
                    acceptor.accept(),
                    api,
                    identity,
                    options.maximum_body_bytes);
            } catch (const std::exception& error) {
                std::cerr << "HTTP connection failed: "
                          << error.what() << '\n';
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "thermox_api_server error: "
                  << error.what() << '\n';
        usage(std::cerr);
        return 1;
    }
}
