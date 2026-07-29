#include "thermox/http/http_api.hpp"

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
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
};

void usage(std::ostream& out) {
    out << "Usage: thermox_http_server"
        << " [--listen <address>]"
        << " [--port <1-65535>]"
        << " [--max-body-bytes <positive integer>]\n";
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
        } else if (argument == "--help" || argument == "-h") {
            usage(std::cout);
            std::exit(0);
        } else {
            throw std::invalid_argument(
                "unknown argument: " + argument);
        }
    }
    return options;
}

thermox::http::Request adapt_request(
    const http::request<http::string_body>& request) {
    thermox::http::Request adapted;
    adapted.method = std::string(request.method_string());
    adapted.target = std::string(request.target());
    adapted.body = request.body();
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
            api.handle(adapt_request(request)),
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
        asio::io_context context{1};
        tcp::acceptor acceptor{
            context, {address, options.port}};
        thermox::http::Api api{
            thermox::service::make_default_simulation_runtime(),
            {.maximum_body_bytes = options.maximum_body_bytes}};

        std::cout << "Thermox HTTP listening on "
                  << address.to_string() << ':' << options.port << '\n';
        for (;;) {
            try {
                serve_connection(
                    acceptor.accept(), api,
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
