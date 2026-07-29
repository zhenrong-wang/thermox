#pragma once

#include "thermox/service/simulation_runtime.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <string>

namespace thermox::http {

struct Request {
    std::string method;
    std::string target;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct Response {
    int status{500};
    std::map<std::string, std::string> headers;
    std::string body;
};

struct ApiOptions {
    std::size_t maximum_body_bytes{10U * 1024U * 1024U};
};

// Framework-neutral HTTP application adapter. A network host is responsible
// only for parsing an HTTP message into Request and writing Response.
class Api {
public:
    Api();
    explicit Api(
        std::shared_ptr<const service::SimulationRuntime> runtime,
        ApiOptions options = {});
    ~Api();
    Api(Api&&) noexcept;
    Api& operator=(Api&&) noexcept;
    Api(const Api&) = delete;
    Api& operator=(const Api&) = delete;

    [[nodiscard]] Response handle(const Request& request) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace thermox::http
