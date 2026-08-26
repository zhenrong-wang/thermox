#pragma once

#include "thermox/service/simulation_jobs.hpp"
#include "thermox/service/projects.hpp"
#include "thermox/service/validation_campaign_service.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace thermox::http {

struct Request {
    std::string method;
    std::string target;
    std::map<std::string, std::string> headers;
    std::string body;
    std::optional<service::IdentityContext> identity;
};

struct Response {
    int status{500};
    std::map<std::string, std::string> headers;
    std::string body;
};

struct ApiOptions {
    std::size_t maximum_body_bytes{10U * 1024U * 1024U};
    bool enable_synchronous_simulations{false};
};

// Framework-neutral HTTP application adapter. A network host is responsible
// only for parsing an HTTP message into Request and writing Response.
class Api {
public:
    Api();
    explicit Api(
        std::shared_ptr<const service::SimulationRuntime> runtime,
        ApiOptions options = {});
    Api(
        std::shared_ptr<const service::SimulationRuntime> runtime,
        std::shared_ptr<service::SimulationJobService> jobs,
        std::shared_ptr<service::ProjectService> projects,
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
