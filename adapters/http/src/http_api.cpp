#include "thermox/http/http_api.hpp"

#include "thermox/service/in_memory_jobs.hpp"
#include "thermox/service/serialization.hpp"
#include "thermox/service/simulation_service.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace thermox::http {
namespace {

using Query = std::map<std::string, std::string>;

struct Target {
    std::string path;
    Query query;
};

class IdentityRequired : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::string lower_ascii(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
    return value;
}

std::optional<std::string> header(
    const Request& request,
    const std::string& name) {
    const std::string wanted = lower_ascii(name);
    for (const auto& [key, value] : request.headers) {
        if (lower_ascii(key) == wanted) return value;
    }
    return std::nullopt;
}

char hex_digit(char c) {
    if (c >= '0' && c <= '9') return static_cast<char>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<char>(10 + c - 'a');
    if (c >= 'A' && c <= 'F') return static_cast<char>(10 + c - 'A');
    throw std::invalid_argument("invalid percent-encoding");
}

std::string decode_query_component(std::string_view encoded) {
    std::string decoded;
    decoded.reserve(encoded.size());
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        if (encoded[index] == '+') {
            decoded.push_back(' ');
        } else if (encoded[index] == '%') {
            if (index + 2 >= encoded.size()) {
                throw std::invalid_argument("invalid percent-encoding");
            }
            const unsigned char high =
                static_cast<unsigned char>(hex_digit(encoded[index + 1]));
            const unsigned char low =
                static_cast<unsigned char>(hex_digit(encoded[index + 2]));
            const char value = static_cast<char>((high << 4U) | low);
            if (value == '\0') {
                throw std::invalid_argument(
                    "query parameters cannot contain NUL");
            }
            decoded.push_back(value);
            index += 2;
        } else {
            decoded.push_back(encoded[index]);
        }
    }
    return decoded;
}

Target parse_target(const std::string& value) {
    const auto question = value.find('?');
    Target target{
        value.substr(0, question),
        {},
    };
    if (target.path.empty() || target.path.front() != '/') {
        throw std::invalid_argument("request target must use an absolute path");
    }
    if (question == std::string::npos) return target;

    std::string_view query{value.data() + question + 1,
                           value.size() - question - 1};
    while (!query.empty()) {
        const auto separator = query.find('&');
        const std::string_view field = query.substr(0, separator);
        if (field.empty()) {
            throw std::invalid_argument("empty query parameter");
        }
        const auto equals = field.find('=');
        const std::string key =
            decode_query_component(field.substr(0, equals));
        const std::string parameter_value =
            equals == std::string_view::npos
                ? std::string{}
                : decode_query_component(field.substr(equals + 1));
        if (key.empty()) {
            throw std::invalid_argument("empty query parameter name");
        }
        if (!target.query.emplace(key, parameter_value).second) {
            throw std::invalid_argument(
                "duplicate query parameter: " + key);
        }
        if (separator == std::string_view::npos) break;
        query.remove_prefix(separator + 1);
    }
    return target;
}

std::string escape_json(std::string_view value) {
    std::ostringstream out;
    for (const unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20U) {
                    constexpr char hex[] = "0123456789abcdef";
                    out << "\\u00" << hex[(c >> 4U) & 0x0fU]
                        << hex[c & 0x0fU];
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

Response json_response(int status, std::string body) {
    Response response;
    response.status = status;
    response.headers = {
        {"Cache-Control", "no-store"},
        {"Content-Type", "application/json; charset=utf-8"},
        {"X-Content-Type-Options", "nosniff"},
    };
    response.body = std::move(body);
    return response;
}

Response error_response(
    int status,
    const std::string& code,
    const std::string& message) {
    std::ostringstream out;
    out << "{\n"
        << "  \"schema_version\": \""
        << service::error_schema_v1 << "\",\n"
        << "  \"code\": \"" << escape_json(code) << "\",\n"
        << "  \"stage\": \"transport\",\n"
        << "  \"message\": \"" << escape_json(message) << "\"\n"
        << "}\n";
    return json_response(status, out.str());
}

int operation_status(const service::OperationStatus status) {
    switch (status) {
        case service::OperationStatus::succeeded:
            return 200;
        case service::OperationStatus::invalid_request:
            return 400;
        case service::OperationStatus::invalid_model:
        case service::OperationStatus::compilation_failed:
        case service::OperationStatus::solver_failed:
            return 422;
        case service::OperationStatus::result_failed:
            return 500;
    }
    return 500;
}

bool is_json_content_type(const std::string& value) {
    const auto separator = value.find(';');
    return lower_ascii(value.substr(0, separator)) == "application/json";
}

void require_json_request(
    const Request& request,
    std::size_t maximum_body_bytes) {
    if (request.body.empty()) {
        throw std::invalid_argument("request body must contain a model document");
    }
    if (request.body.size() > maximum_body_bytes) {
        throw std::length_error("request body exceeds the configured limit");
    }
    const auto content_type = header(request, "content-type");
    if (!content_type || !is_json_content_type(*content_type)) {
        throw std::domain_error(
            "Content-Type must be application/json");
    }
    const auto schema = header(request, "thermox-command-schema");
    if (schema && *schema != service::command_schema_v1) {
        throw std::invalid_argument(
            "unsupported Thermox-Command-Schema: " + *schema);
    }
}

void reject_unknown_query(
    const Query& query,
    std::initializer_list<std::string_view> allowed) {
    for (const auto& [name, unused] : query) {
        (void) unused;
        const bool known = std::find(
            allowed.begin(), allowed.end(), name) != allowed.end();
        if (!known) {
            throw std::invalid_argument(
                "unknown query parameter: " + name);
        }
    }
}

std::string optional_query(
    const Query& query,
    const std::string& name) {
    const auto found = query.find(name);
    return found == query.end() ? std::string{} : found->second;
}

double required_positive_double(
    const Query& query,
    const std::string& name) {
    const auto found = query.find(name);
    if (found == query.end() || found->second.empty()) {
        throw std::invalid_argument(
            "missing required query parameter: " + name);
    }
    double value = 0.0;
    const char* begin = found->second.data();
    const char* end = begin + found->second.size();
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end ||
        !std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(
            name + " must be a positive finite number");
    }
    return value;
}

const service::IdentityContext& require_identity(
    const Request& request) {
    if (!request.identity ||
        request.identity->user_id.empty() ||
        request.identity->team_id.empty()) {
        throw IdentityRequired(
            "this operation requires a verified identity context");
    }
    return *request.identity;
}

std::string required_header(
    const Request& request,
    const std::string& name) {
    const auto value = header(request, name);
    if (!value || value->empty()) {
        throw std::invalid_argument(
            "missing required header: " + name);
    }
    return *value;
}

Response job_record_response(
    const service::SimulationJobRecord& record,
    int status) {
    auto response = json_response(
        status,
        service::serialize_job_record_json(record));
    response.headers["ETag"] =
        "\"revision-" + std::to_string(record.revision) + "\"";
    response.headers["Location"] =
        "/api/v1/simulations/" + record.job_id;
    return response;
}

std::shared_ptr<service::SimulationJobService>
make_local_job_service(
    const std::shared_ptr<const service::SimulationRuntime>& runtime) {
    return std::make_shared<service::SimulationJobService>(
        runtime,
        service::make_in_memory_job_repository(),
        service::make_in_memory_result_artifact_store());
}

}  // namespace

struct Api::Impl {
    explicit Impl(
        std::shared_ptr<const service::SimulationRuntime> runtime,
        std::shared_ptr<service::SimulationJobService> job_service,
        ApiOptions api_options)
        : simulation(std::move(runtime)),
          jobs(std::move(job_service)),
          options(api_options) {
        if (options.maximum_body_bytes == 0U) {
            throw std::invalid_argument(
                "maximum_body_bytes must be positive");
        }
        if (!jobs) {
            throw std::invalid_argument(
                "simulation job service must not be null");
        }
    }

    service::SimulationService simulation;
    std::shared_ptr<service::SimulationJobService> jobs;
    ApiOptions options;
};

Api::Api()
    : Api(service::make_default_simulation_runtime()) {}

Api::Api(
    std::shared_ptr<const service::SimulationRuntime> runtime,
    ApiOptions options)
    : Api(
          runtime,
          make_local_job_service(runtime),
          options) {}

Api::Api(
    std::shared_ptr<const service::SimulationRuntime> runtime,
    std::shared_ptr<service::SimulationJobService> jobs,
    ApiOptions options)
    : impl_(std::make_unique<Impl>(
          std::move(runtime), std::move(jobs), options)) {}

Api::~Api() = default;
Api::Api(Api&&) noexcept = default;
Api& Api::operator=(Api&&) noexcept = default;

Response Api::handle(const Request& request) const {
    try {
        const Target target = parse_target(request.target);
        const std::string method = lower_ascii(request.method);

        if (target.path == "/healthz") {
            reject_unknown_query(target.query, {});
            if (method != "get") {
                auto response = error_response(
                    405, "method_not_allowed",
                    "healthz only supports GET");
                response.headers["Allow"] = "GET";
                return response;
            }
            return json_response(
                200,
                "{\n  \"status\": \"ok\"\n}\n");
        }

        if (target.path == "/api/v1/catalog") {
            reject_unknown_query(target.query, {});
            if (method != "get") {
                auto response = error_response(
                    405, "method_not_allowed",
                    "catalog only supports GET");
                response.headers["Allow"] = "GET";
                return response;
            }
            const auto result = impl_->simulation.get_catalog();
            return json_response(
                operation_status(result.status),
                service::serialize_catalog_response_json(result));
        }

        if (target.path == "/api/v1/models/validate") {
            if (method != "post") {
                auto response = error_response(
                    405, "method_not_allowed",
                    "model validation only supports POST");
                response.headers["Allow"] = "POST";
                return response;
            }
            reject_unknown_query(target.query, {"case_id"});
            require_json_request(
                request, impl_->options.maximum_body_bytes);
            service::ValidateModelRequest command;
            command.model_json = request.body;
            command.case_id = optional_query(target.query, "case_id");
            const auto result =
                impl_->simulation.validate_model(command);
            return json_response(
                operation_status(result.status),
                service::serialize_validate_response_json(result));
        }

        if (target.path == "/api/v1/simulations/steady") {
            if (!impl_->options.enable_synchronous_simulations) {
                return error_response(
                    404,
                    "route_not_found",
                    "synchronous simulation routes are disabled; "
                    "submit an asynchronous simulation job");
            }
            if (method != "post") {
                auto response = error_response(
                    405, "method_not_allowed",
                    "steady simulation only supports POST");
                response.headers["Allow"] = "POST";
                return response;
            }
            reject_unknown_query(target.query, {"case_id"});
            require_json_request(
                request, impl_->options.maximum_body_bytes);
            service::SteadySimulationRequest command;
            command.model_json = request.body;
            command.case_id = optional_query(target.query, "case_id");
            const auto result = impl_->simulation.run_steady(command);
            return json_response(
                operation_status(result.status),
                service::serialize_steady_response_json(result));
        }

        if (target.path == "/api/v1/simulations/transient") {
            if (!impl_->options.enable_synchronous_simulations) {
                return error_response(
                    404,
                    "route_not_found",
                    "synchronous simulation routes are disabled; "
                    "submit an asynchronous simulation job");
            }
            if (method != "post") {
                auto response = error_response(
                    405, "method_not_allowed",
                    "transient simulation only supports POST");
                response.headers["Allow"] = "POST";
                return response;
            }
            reject_unknown_query(
                target.query, {"case_id", "end_time"});
            require_json_request(
                request, impl_->options.maximum_body_bytes);
            service::TransientSimulationRequest command;
            command.model_json = request.body;
            command.case_id = optional_query(target.query, "case_id");
            command.solver.end_time =
                required_positive_double(target.query, "end_time");
            const auto result =
                impl_->simulation.run_transient(command);
            return json_response(
                operation_status(result.status),
                service::serialize_transient_response_json(result));
        }

        if (target.path == "/api/v1/simulations") {
            if (method != "post") {
                auto response = error_response(
                    405, "method_not_allowed",
                    "simulation submission only supports POST");
                response.headers["Allow"] = "POST";
                return response;
            }
            reject_unknown_query(
                target.query, {"mode", "case_id", "end_time"});
            require_json_request(
                request, impl_->options.maximum_body_bytes);
            const auto& identity = require_identity(request);
            const std::string mode =
                optional_query(target.query, "mode");
            if (mode != "steady" && mode != "transient") {
                throw std::invalid_argument(
                    "mode must be steady or transient");
            }
            service::SimulationJobRequest command;
            command.identity = identity;
            command.idempotency_key =
                required_header(request, "idempotency-key");
            command.mode =
                mode == "steady"
                    ? service::SimulationJobMode::steady
                    : service::SimulationJobMode::transient;
            command.model_json = request.body;
            command.case_id =
                optional_query(target.query, "case_id");
            if (command.mode ==
                service::SimulationJobMode::transient) {
                command.transient_solver.end_time =
                    required_positive_double(
                        target.query, "end_time");
            } else if (target.query.contains("end_time")) {
                throw std::invalid_argument(
                    "end_time is only valid for transient jobs");
            }
            const auto record = impl_->jobs->submit(command);
            return job_record_response(
                record,
                service::is_terminal(record.state) ? 200 : 202);
        }

        constexpr std::string_view job_prefix =
            "/api/v1/simulations/";
        if (target.path.starts_with(job_prefix)) {
            reject_unknown_query(target.query, {});
            if (method != "get") {
                auto response = error_response(
                    405, "method_not_allowed",
                    "simulation status and results only support GET");
                response.headers["Allow"] = "GET";
                return response;
            }
            const auto& identity = require_identity(request);
            std::string suffix =
                target.path.substr(job_prefix.size());
            bool result_requested = false;
            constexpr std::string_view result_suffix = "/result";
            if (suffix.ends_with(result_suffix)) {
                result_requested = true;
                suffix.resize(
                    suffix.size() - result_suffix.size());
            }
            if (suffix.empty() ||
                suffix.find('/') != std::string::npos) {
                return error_response(
                    404, "route_not_found",
                    "no route matches the request target");
            }
            if (!result_requested) {
                const auto record =
                    impl_->jobs->get(identity, suffix);
                if (!record) {
                    return error_response(
                        404, "simulation_not_found",
                        "simulation job was not found");
                }
                return job_record_response(*record, 200);
            }
            const auto result =
                impl_->jobs->get_result(identity, suffix);
            if (!result) {
                return error_response(
                    404, "simulation_not_found",
                    "simulation job was not found");
            }
            auto response = json_response(
                200, result->content);
            response.headers["ETag"] =
                "\"" + result->manifest.checksum + "\"";
            return response;
        }

        return error_response(
            404, "route_not_found",
            "no route matches the request target");
    } catch (const IdentityRequired& error) {
        return error_response(401, "identity_required", error.what());
    } catch (const service::JobConflictError& error) {
        return error_response(409, "job_conflict", error.what());
    } catch (const service::JobStateError& error) {
        return error_response(409, "job_state_conflict", error.what());
    } catch (const service::JobRequestError& error) {
        return error_response(400, "invalid_job_request", error.what());
    } catch (const std::length_error& error) {
        return error_response(413, "body_too_large", error.what());
    } catch (const std::domain_error& error) {
        return error_response(
            415, "unsupported_media_type", error.what());
    } catch (const std::invalid_argument& error) {
        return error_response(400, "invalid_http_request", error.what());
    } catch (const std::exception&) {
        return error_response(
            500, "transport_failure",
            "the HTTP adapter could not complete the request");
    }
}

}  // namespace thermox::http
