#include "thermox/http/http_api.hpp"

#include "thermox/service/in_memory_jobs.hpp"
#include "thermox/service/in_memory_projects.hpp"
#include "thermox/service/serialization.hpp"
#include "thermox/service/simulation_service.hpp"

#include <boost/json.hpp>
#include <boost/json/src.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <limits>
#include <map>
#include <optional>
#include <set>
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

class PreconditionRequired : public std::runtime_error {
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
        throw std::invalid_argument(
            "request body must contain a JSON document");
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

double optional_positive_double(
    const Query& query,
    const std::string& name,
    double default_value) {
    const auto found = query.find(name);
    if (found == query.end()) return default_value;
    if (found->second.empty()) {
        throw std::invalid_argument(
            name + " must not be empty");
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

std::vector<service::StructuralDecompositionPolicy>
structural_policy_list(std::string_view value) {
    std::vector<service::StructuralDecompositionPolicy> policies;
    while (!value.empty()) {
        const auto separator = value.find(',');
        const auto token = value.substr(0, separator);
        if (token.empty()) {
            throw std::invalid_argument(
                "policies must be a comma-separated list without "
                "empty entries");
        }
        policies.push_back(
            service::structural_decomposition_policy_from_string(
                token));
        if (separator == std::string_view::npos) break;
        value.remove_prefix(separator + 1);
    }
    if (policies.empty()) {
        throw std::invalid_argument(
            "policies must contain at least one policy");
    }
    return policies;
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

std::size_t optional_history_limit(const Query& query) {
    const auto value = optional_query(query, "limit");
    if (value.empty()) {
        return 50;
    }
    std::size_t limit = 0;
    const auto parsed = std::from_chars(
        value.data(), value.data() + value.size(), limit);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != value.data() + value.size() ||
        limit == 0 || limit > 200) {
        throw std::invalid_argument(
            "limit must be an integer between 1 and 200");
    }
    return limit;
}

std::optional<service::SimulationJobState> optional_job_state(
    const Query& query) {
    const auto value = optional_query(query, "state");
    if (value.empty()) {
        return std::nullopt;
    }
    if (value == "queued") {
        return service::SimulationJobState::queued;
    }
    if (value == "running") {
        return service::SimulationJobState::running;
    }
    if (value == "succeeded") {
        return service::SimulationJobState::succeeded;
    }
    if (value == "failed") {
        return service::SimulationJobState::failed;
    }
    if (value == "cancelled") {
        return service::SimulationJobState::cancelled;
    }
    throw std::invalid_argument(
        "state must be queued, running, succeeded, failed, or "
        "cancelled");
}

std::string hex_encode(std::string_view value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(value.size() * 2U);
    for (const unsigned char byte : value) {
        encoded.push_back(digits[byte >> 4U]);
        encoded.push_back(digits[byte & 0x0fU]);
    }
    return encoded;
}

std::string hex_decode(std::string_view value) {
    if (value.empty() || value.size() % 2U != 0U) {
        throw std::invalid_argument(
            "simulation history cursor is invalid");
    }
    std::string decoded;
    decoded.reserve(value.size() / 2U);
    for (std::size_t index = 0; index < value.size(); index += 2U) {
        try {
            const auto high = static_cast<unsigned char>(
                hex_digit(value[index]));
            const auto low = static_cast<unsigned char>(
                hex_digit(value[index + 1U]));
            decoded.push_back(
                static_cast<char>((high << 4U) | low));
        } catch (const std::invalid_argument&) {
            throw std::invalid_argument(
                "simulation history cursor is invalid");
        }
    }
    return decoded;
}

std::string encode_job_cursor(
    const service::SimulationJobCursor& cursor) {
    const auto microseconds =
        std::chrono::duration_cast<std::chrono::microseconds>(
            cursor.created_at.time_since_epoch())
            .count();
    return hex_encode(
        std::to_string(microseconds) + "|" + cursor.job_id);
}

service::SimulationJobCursor decode_job_cursor(
    const std::string& encoded) {
    const auto decoded = hex_decode(encoded);
    const auto separator = decoded.find('|');
    if (separator == std::string::npos ||
        separator == 0 ||
        separator + 1U >= decoded.size()) {
        throw std::invalid_argument(
            "simulation history cursor is invalid");
    }
    std::int64_t microseconds = 0;
    const auto timestamp = std::string_view{decoded}.substr(
        0, separator);
    const auto parsed = std::from_chars(
        timestamp.data(),
        timestamp.data() + timestamp.size(),
        microseconds);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != timestamp.data() + timestamp.size() ||
        microseconds < 0) {
        throw std::invalid_argument(
            "simulation history cursor is invalid");
    }
    const auto job_id = decoded.substr(separator + 1U);
    if (!job_id.starts_with("job-") ||
        !std::all_of(
            job_id.begin(),
            job_id.end(),
            [](const unsigned char character) {
                return std::isalnum(character) ||
                    character == '-';
            })) {
        throw std::invalid_argument(
            "simulation history cursor is invalid");
    }
    return {
        std::chrono::system_clock::time_point{
            std::chrono::microseconds{microseconds}},
        job_id,
    };
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

std::uint64_t required_revision_precondition(
    const Request& request) {
    const auto value = header(request, "if-match");
    if (!value || value->empty()) {
        throw PreconditionRequired(
            "If-Match is required for simulation cancellation");
    }
    constexpr std::string_view prefix{"\"revision-"};
    if (!value->starts_with(prefix) ||
        !value->ends_with('"') ||
        value->size() <= prefix.size() + 1U) {
        throw std::invalid_argument(
            "If-Match must use a simulation revision ETag");
    }
    const std::string_view revision{
        value->data() + prefix.size(),
        value->size() - prefix.size() - 1U};
    std::uint64_t parsed_revision = 0;
    const auto parsed = std::from_chars(
        revision.data(),
        revision.data() + revision.size(),
        parsed_revision);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != revision.data() + revision.size() ||
        parsed_revision == 0) {
        throw std::invalid_argument(
            "If-Match must use a simulation revision ETag");
    }
    return parsed_revision;
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
        "/api/v1/jobs/" + record.job_id;
    return response;
}

void skip_json_whitespace(
    const std::string& input,
    std::size_t& position) {
    while (position < input.size() &&
           std::isspace(static_cast<unsigned char>(
               input[position]))) {
        ++position;
    }
}

void scan_json_string(
    const std::string& input,
    std::size_t& position) {
    if (position >= input.size() ||
        input[position] != '"') {
        throw std::invalid_argument(
            "project fields must be JSON strings");
    }
    ++position;
    while (position < input.size()) {
        const char character = input[position++];
        if (character == '"') {
            return;
        }
        if (static_cast<unsigned char>(character) < 0x20U) {
            throw std::invalid_argument(
                "project JSON string contains a control byte");
        }
        if (character == '\\') {
            if (position >= input.size()) {
                break;
            }
            const char escape = input[position++];
            if (escape == 'u') {
                for (int digit = 0; digit < 4; ++digit) {
                    if (position >= input.size() ||
                        !std::isxdigit(
                            static_cast<unsigned char>(
                                input[position++]))) {
                        throw std::invalid_argument(
                            "invalid Unicode escape in project "
                            "JSON");
                    }
                }
            } else if (
                std::string_view{"\"\\/bfnrt"}.find(escape) ==
                std::string_view::npos) {
                throw std::invalid_argument(
                    "invalid escape in project JSON");
            }
        }
    }
    throw std::invalid_argument(
        "unterminated string in project JSON");
}

void require_json_string_object_shape(
    const std::string& input) {
    std::size_t position = 0;
    skip_json_whitespace(input, position);
    if (position >= input.size() || input[position++] != '{') {
        throw std::invalid_argument(
            "project request must be a JSON object");
    }
    skip_json_whitespace(input, position);
    if (position < input.size() && input[position] == '}') {
        ++position;
    } else {
        for (;;) {
            scan_json_string(input, position);
            skip_json_whitespace(input, position);
            if (position >= input.size() ||
                input[position++] != ':') {
                throw std::invalid_argument(
                    "invalid project JSON object");
            }
            skip_json_whitespace(input, position);
            scan_json_string(input, position);
            skip_json_whitespace(input, position);
            if (position < input.size() &&
                input[position] == ',') {
                ++position;
                skip_json_whitespace(input, position);
                continue;
            }
            if (position < input.size() &&
                input[position] == '}') {
                ++position;
                break;
            }
            throw std::invalid_argument(
                "invalid project JSON object");
        }
    }
    skip_json_whitespace(input, position);
    if (position != input.size()) {
        throw std::invalid_argument(
            "trailing content after project JSON object");
    }
}

service::CreateProjectRequest parse_create_project_request(
    const Request& request) {
    require_json_string_object_shape(request.body);
    boost::property_tree::ptree tree;
    std::istringstream input(request.body);
    try {
        boost::property_tree::read_json(input, tree);
    } catch (const std::exception& error) {
        throw std::invalid_argument(
            std::string("invalid project JSON: ") +
            error.what());
    }
    std::set<std::string> fields;
    for (const auto& [key, value] : tree) {
        (void)value;
        if (key != "schema_version" &&
            key != "name" && key != "description") {
            throw std::invalid_argument(
                "unknown project field: " + key);
        }
        if (!fields.insert(key).second) {
            throw std::invalid_argument(
                "duplicate project field: " + key);
        }
    }
    const auto schema =
        tree.get<std::string>("schema_version", "");
    if (schema != "thermox.project.create/v1") {
        throw std::invalid_argument(
            "unsupported project create schema_version");
    }
    service::CreateProjectRequest command;
    command.name = tree.get<std::string>("name", "");
    command.description =
        tree.get<std::string>("description", "");
    return command;
}

const boost::json::value& require_json_field(
    const boost::json::object& object,
    std::string_view name) {
    const auto* value = object.if_contains(name);
    if (value == nullptr) {
        throw std::invalid_argument(
            "missing JSON field: " + std::string(name));
    }
    return *value;
}

std::string require_json_string(
    const boost::json::object& object,
    std::string_view name) {
    const auto& value = require_json_field(object, name);
    if (!value.is_string()) {
        throw std::invalid_argument(
            "JSON field must be a string: " +
            std::string(name));
    }
    return std::string(value.as_string());
}

double require_json_number(
    const boost::json::value& value,
    std::string_view name) {
    if (value.is_double()) return value.as_double();
    if (value.is_int64()) {
        return static_cast<double>(value.as_int64());
    }
    if (value.is_uint64()) {
        return static_cast<double>(value.as_uint64());
    }
    throw std::invalid_argument(
        "JSON field must be a number: " + std::string(name));
}

service::InstantiateCorrelationRequest
parse_correlation_instantiation_request(const Request& request) {
    boost::json::value value;
    try {
        value = boost::json::parse(request.body);
    } catch (const std::exception& error) {
        throw std::invalid_argument(
            std::string("invalid correlation instantiation JSON: ") +
            error.what());
    }
    if (!value.is_object()) {
        throw std::invalid_argument(
            "correlation instantiation request must be a JSON object");
    }
    const auto& root = value.as_object();
    for (const auto& field : root) {
        if (field.key() != "schema_version" &&
            field.key() != "artifact_id" &&
            field.key() != "revision" &&
            field.key() != "family_template_id" &&
            field.key() != "bindings") {
            throw std::invalid_argument(
                "unknown correlation instantiation field: " +
                std::string(field.key()));
        }
    }
    service::InstantiateCorrelationRequest command;
    command.schema_version =
        require_json_string(root, "schema_version");
    command.artifact_id =
        require_json_string(root, "artifact_id");
    command.revision = require_json_string(root, "revision");
    if (const auto* family = root.if_contains("family_template_id")) {
        if (!family->is_string()) {
            throw std::invalid_argument(
                "correlation family_template_id must be a string");
        }
        command.family_template_id =
            std::string(family->as_string());
    }
    const auto* bindings = root.if_contains("bindings");
    if (bindings == nullptr) return command;
    if (!bindings->is_array()) {
        throw std::invalid_argument(
            "correlation template bindings must be an array");
    }
    for (const auto& item : bindings->as_array()) {
        if (!item.is_object()) {
            throw std::invalid_argument(
                "each correlation template binding must be an object");
        }
        const auto& object = item.as_object();
        for (const auto& field : object) {
            if (field.key() != "template_id" &&
                field.key() != "coefficients" &&
                field.key() != "candidate_id" &&
                field.key() != "priority" &&
                field.key() != "flow_regimes" &&
                field.key() !=
                    "fallback_for_unmapped_flow_regime") {
                throw std::invalid_argument(
                    "unknown correlation template binding field: " +
                    std::string(field.key()));
            }
        }
        service::CorrelationTemplateBindingInput binding;
        binding.template_id =
            require_json_string(object, "template_id");
        if (const auto* candidate =
                object.if_contains("candidate_id")) {
            if (!candidate->is_string()) {
                throw std::invalid_argument(
                    "correlation candidate_id must be a string");
            }
            binding.candidate_id =
                std::string(candidate->as_string());
        }
        if (const auto* priority = object.if_contains("priority")) {
            if (!priority->is_int64()) {
                throw std::invalid_argument(
                    "correlation priority must be an integer");
            }
            const auto parsed = priority->as_int64();
            if (parsed < std::numeric_limits<int>::min() ||
                parsed > std::numeric_limits<int>::max()) {
                throw std::invalid_argument(
                    "correlation priority is outside integer range");
            }
            binding.priority = static_cast<int>(parsed);
        }
        if (const auto* routes = object.if_contains("flow_regimes")) {
            if (!routes->is_array()) {
                throw std::invalid_argument(
                    "correlation flow_regimes must be an array");
            }
            for (const auto& route : routes->as_array()) {
                if (!route.is_string()) {
                    throw std::invalid_argument(
                        "correlation flow_regimes entries must be strings");
                }
                binding.flow_regimes.emplace_back(route.as_string());
            }
        }
        if (const auto* fallback = object.if_contains(
                "fallback_for_unmapped_flow_regime")) {
            if (!fallback->is_bool()) {
                throw std::invalid_argument(
                    "correlation fallback_for_unmapped_flow_regime must "
                    "be boolean");
            }
            binding.fallback_for_unmapped_flow_regime =
                fallback->as_bool();
        }
        if (const auto* coefficients =
                object.if_contains("coefficients")) {
            if (!coefficients->is_object()) {
                throw std::invalid_argument(
                    "correlation coefficients must be an object");
            }
            for (const auto& coefficient :
                 coefficients->as_object()) {
                binding.coefficients.emplace(
                    std::string(coefficient.key()),
                    require_json_number(
                        coefficient.value(), coefficient.key()));
            }
        }
        command.bindings.push_back(std::move(binding));
    }
    return command;
}

service::InstantiateRegimeMapRequest
parse_regime_map_instantiation_request(const Request& request) {
    boost::json::value value;
    try {
        value = boost::json::parse(request.body);
    } catch (const std::exception& error) {
        throw std::invalid_argument(
            std::string("invalid regime-map instantiation JSON: ") +
            error.what());
    }
    if (!value.is_object()) {
        throw std::invalid_argument(
            "regime-map instantiation request must be a JSON object");
    }
    const auto& root = value.as_object();
    for (const auto& field : root) {
        if (field.key() != "schema_version" &&
            field.key() != "artifact_id" &&
            field.key() != "revision" &&
            field.key() != "template_id") {
            throw std::invalid_argument(
                "unknown regime-map instantiation field: " +
                std::string(field.key()));
        }
    }
    service::InstantiateRegimeMapRequest command;
    command.schema_version =
        require_json_string(root, "schema_version");
    command.artifact_id =
        require_json_string(root, "artifact_id");
    command.revision = require_json_string(root, "revision");
    command.template_id =
        require_json_string(root, "template_id");
    return command;
}

std::pair<std::string, std::string>
parse_job_comparison_request(const Request& request) {
    boost::json::value value;
    try {
        value = boost::json::parse(request.body);
    } catch (const std::exception& error) {
        throw std::invalid_argument(
            std::string("invalid job comparison JSON: ") +
            error.what());
    }
    if (!value.is_object()) {
        throw std::invalid_argument(
            "job comparison request must be a JSON object");
    }
    const auto& root = value.as_object();
    for (const auto& field : root) {
        if (field.key() != "schema_version" &&
            field.key() != "baseline_job_id" &&
            field.key() != "candidate_job_id") {
            throw std::invalid_argument(
                "unknown job comparison field: " +
                std::string(field.key()));
        }
    }
    if (require_json_string(root, "schema_version") !=
        "thermox.job_comparison.create/v1") {
        throw std::invalid_argument(
            "unsupported job comparison schema_version");
    }
    return {
        require_json_string(root, "baseline_job_id"),
        require_json_string(root, "candidate_job_id"),
    };
}

service::CreatePerformanceMapQualityReviewRequest
parse_performance_map_quality_review_request(
    const Request& request) {
    boost::json::value value;
    try {
        value = boost::json::parse(request.body);
    } catch (const std::exception& error) {
        throw std::invalid_argument(
            std::string("invalid performance-map quality review JSON: ") +
            error.what());
    }
    if (!value.is_object()) {
        throw std::invalid_argument(
            "performance-map quality review must be a JSON object");
    }
    const auto& root = value.as_object();
    for (const auto& field : root) {
        if (field.key() != "schema_version" &&
            field.key() != "supersedes_review_id" &&
            field.key() != "disposition" &&
            field.key() != "reviewed_scope" &&
            field.key() != "rationale") {
            throw std::invalid_argument(
                "unknown performance-map quality review field: " +
                std::string(field.key()));
        }
    }
    if (require_json_string(root, "schema_version") !=
        "thermox.performance_map_quality_review.create/v1") {
        throw std::invalid_argument(
            "unsupported performance-map quality review "
            "schema_version");
    }
    service::CreatePerformanceMapQualityReviewRequest command;
    const auto disposition =
        require_json_string(root, "disposition");
    if (disposition == "approved") {
        command.disposition = service::
            EngineeringReviewDisposition::approved;
    } else if (disposition == "approved_with_conditions") {
        command.disposition = service::
            EngineeringReviewDisposition::approved_with_conditions;
    } else if (disposition == "rejected") {
        command.disposition = service::
            EngineeringReviewDisposition::rejected;
    } else {
        throw std::invalid_argument(
            "unknown performance-map quality review disposition");
    }
    command.reviewed_scope =
        require_json_string(root, "reviewed_scope");
    command.rationale = require_json_string(root, "rationale");
    if (const auto* supersedes =
            root.if_contains("supersedes_review_id")) {
        if (!supersedes->is_string()) {
            throw std::invalid_argument(
                "supersedes_review_id must be a string");
        }
        command.supersedes_review_id =
            std::string(supersedes->as_string());
    }
    return command;
}

service::ApplyGraphEditsRequest parse_graph_edit_request(
    const Request& request) {
    boost::json::value value;
    try {
        value = boost::json::parse(request.body);
    } catch (const std::exception& error) {
        throw std::invalid_argument(
            std::string("invalid graph edit JSON: ") +
            error.what());
    }
    if (!value.is_object()) {
        throw std::invalid_argument(
            "graph edit request must be a JSON object");
    }
    const auto& root = value.as_object();
    for (const auto& field : root) {
        if (field.key() != "schema_version" &&
            field.key() != "operations") {
            throw std::invalid_argument(
                "unknown graph edit request field: " +
                std::string(field.key()));
        }
    }
    if (require_json_string(root, "schema_version") !=
        "thermox.graph_edit_batch/v1") {
        throw std::invalid_argument(
            "unsupported graph edit schema_version");
    }
    const auto& operations_value =
        require_json_field(root, "operations");
    if (!operations_value.is_array()) {
        throw std::invalid_argument(
            "graph edit operations must be an array");
    }

    service::ApplyGraphEditsRequest command;
    for (const auto& item : operations_value.as_array()) {
        if (!item.is_object()) {
            throw std::invalid_argument(
                "each graph edit operation must be an object");
        }
        const auto& object = item.as_object();
        for (const auto& field : object) {
            if (field.key() != "action" &&
                field.key() != "entity_type" &&
                field.key() != "entity_id" &&
                field.key() != "entity" &&
                field.key() != "cascade") {
                throw std::invalid_argument(
                    "unknown graph edit operation field: " +
                    std::string(field.key()));
            }
        }

        service::GraphEditOperation operation;
        const auto action =
            require_json_string(object, "action");
        if (action == "upsert") {
            operation.action =
                service::GraphEditAction::upsert;
        } else if (action == "remove") {
            operation.action =
                service::GraphEditAction::remove;
        } else {
            throw std::invalid_argument(
                "graph edit action must be upsert or remove");
        }

        const auto entity_type =
            require_json_string(object, "entity_type");
        std::string fragment_key;
        std::string fragment_schema;
        if (entity_type == "medium") {
            operation.entity_type =
                service::GraphEntityType::medium;
            fragment_key = "medium";
            fragment_schema = "thermox.medium_definition/v1";
        } else if (entity_type == "material") {
            operation.entity_type =
                service::GraphEntityType::material;
            fragment_key = "material";
            fragment_schema =
                "thermox.material_definition/v1";
        } else if (entity_type == "component") {
            operation.entity_type =
                service::GraphEntityType::component;
            fragment_key = "component";
            fragment_schema =
                "thermox.component_definition/v1";
        } else if (entity_type == "assembly") {
            operation.entity_type =
                service::GraphEntityType::assembly;
            fragment_key = "assembly";
            fragment_schema =
                "thermox.assembly_definition/v1";
        } else if (entity_type == "connection") {
            operation.entity_type =
                service::GraphEntityType::connection;
            fragment_key = "connection";
            fragment_schema =
                "thermox.connection_definition/v1";
        } else {
            throw std::invalid_argument(
                "unknown graph edit entity_type: " +
                entity_type);
        }
        operation.entity_id =
            require_json_string(object, "entity_id");

        if (const auto* cascade =
                object.if_contains("cascade")) {
            if (!cascade->is_bool()) {
                throw std::invalid_argument(
                    "graph edit cascade must be a boolean");
            }
            operation.cascade = cascade->as_bool();
        }
        const auto* entity = object.if_contains("entity");
        if (operation.action ==
            service::GraphEditAction::upsert) {
            if (entity == nullptr || !entity->is_object()) {
                throw std::invalid_argument(
                    "graph upsert entity must be an object");
            }
            if (operation.cascade) {
                throw std::invalid_argument(
                    "graph upsert does not support cascade");
            }
            boost::json::object fragment;
            fragment["schema_version"] = fragment_schema;
            fragment[fragment_key] = *entity;
            operation.entity_json =
                boost::json::serialize(fragment);
        } else {
            if (entity != nullptr) {
                throw std::invalid_argument(
                    "graph removal must not contain entity");
            }
            if (operation.cascade &&
                operation.entity_type !=
                    service::GraphEntityType::component &&
                operation.entity_type !=
                    service::GraphEntityType::assembly) {
                throw std::invalid_argument(
                    "cascade is only valid for component or "
                    "assembly removal");
            }
        }
        command.operations.push_back(std::move(operation));
    }
    return command;
}

service::ApplyCaseEditsRequest parse_case_edit_request(
    const Request& request) {
    boost::json::value value;
    try {
        value = boost::json::parse(request.body);
    } catch (const std::exception& error) {
        throw std::invalid_argument(
            std::string("invalid case edit JSON: ") +
            error.what());
    }
    if (!value.is_object()) {
        throw std::invalid_argument(
            "case edit request must be a JSON object");
    }
    const auto& root = value.as_object();
    for (const auto& field : root) {
        if (field.key() != "schema_version" &&
            field.key() != "operations") {
            throw std::invalid_argument(
                "unknown case edit request field: " +
                std::string(field.key()));
        }
    }
    if (require_json_string(root, "schema_version") !=
        "thermox.case_edit_batch/v1") {
        throw std::invalid_argument(
            "unsupported case edit schema_version");
    }
    const auto& operations =
        require_json_field(root, "operations");
    if (!operations.is_array()) {
        throw std::invalid_argument(
            "case edit operations must be an array");
    }

    service::ApplyCaseEditsRequest command;
    for (const auto& item : operations.as_array()) {
        if (!item.is_object()) {
            throw std::invalid_argument(
                "each case edit operation must be an object");
        }
        const auto& object = item.as_object();
        for (const auto& field : object) {
            if (field.key() != "action" &&
                field.key() != "field" &&
                field.key() != "key" &&
                field.key() != "value") {
                throw std::invalid_argument(
                    "unknown case edit operation field: " +
                    std::string(field.key()));
            }
        }

        service::CaseEditOperation operation;
        const auto action =
            require_json_string(object, "action");
        if (action == "upsert") {
            operation.action =
                service::CaseEditAction::upsert;
        } else if (action == "remove") {
            operation.action =
                service::CaseEditAction::remove;
        } else {
            throw std::invalid_argument(
                "case edit action must be upsert or remove");
        }

        const auto field =
            require_json_string(object, "field");
        if (field == "label") {
            operation.field =
                service::CaseEditField::label;
        } else if (field == "mode") {
            operation.field =
                service::CaseEditField::mode;
        } else if (field == "parameter_override") {
            operation.field =
                service::CaseEditField::parameter_override;
        } else if (field == "fixed_value") {
            operation.field =
                service::CaseEditField::fixed_value;
        } else if (field == "initial_guess") {
            operation.field =
                service::CaseEditField::initial_guess;
        } else if (field == "solver_option") {
            operation.field =
                service::CaseEditField::solver_option;
        } else {
            throw std::invalid_argument(
                "unknown case edit field: " + field);
        }

        const auto* key = object.if_contains("key");
        if (key != nullptr) {
            if (!key->is_string()) {
                throw std::invalid_argument(
                    "case edit key must be a string");
            }
            operation.key = std::string(key->as_string());
        }
        const auto* edit_value =
            object.if_contains("value");
        if (operation.action ==
            service::CaseEditAction::remove) {
            if (edit_value != nullptr) {
                throw std::invalid_argument(
                    "case removal must not contain value");
            }
        } else if (
            operation.field == service::CaseEditField::label ||
            operation.field == service::CaseEditField::mode) {
            if (edit_value == nullptr ||
                !edit_value->is_string()) {
                throw std::invalid_argument(
                    "case metadata value must be a string");
            }
            operation.string_value =
                std::string(edit_value->as_string());
        } else {
            if (edit_value == nullptr ||
                (!edit_value->is_number() &&
                 !edit_value->is_object())) {
                throw std::invalid_argument(
                    "case scalar value must be a number or "
                    "quantity object");
            }
            boost::json::object scalar;
            scalar["schema_version"] =
                "thermox.scalar_value/v1";
            scalar["scalar"] = *edit_value;
            operation.scalar_json =
                boost::json::serialize(scalar);
        }
        command.operations.push_back(std::move(operation));
    }
    return command;
}

service::ValidateProjectModelRequest
parse_project_model_validation_request(
    const Request& request) {
    boost::json::value value;
    try {
        value = boost::json::parse(request.body);
    } catch (const std::exception& error) {
        throw std::invalid_argument(
            std::string(
                "invalid project model validation JSON: ") +
            error.what());
    }
    if (!value.is_object()) {
        throw std::invalid_argument(
            "project model validation request must be a JSON "
            "object");
    }
    const auto& root = value.as_object();
    for (const auto& field : root) {
        if (field.key() != "schema_version" &&
            field.key() != "artifact_revision_ids") {
            throw std::invalid_argument(
                "unknown project model validation field: " +
                std::string(field.key()));
        }
    }
    if (require_json_string(root, "schema_version") !=
        "thermox.project_model_validation_request/v1") {
        throw std::invalid_argument(
            "unsupported project model validation "
            "schema_version");
    }

    service::ValidateProjectModelRequest command;
    const auto* artifacts =
        root.if_contains("artifact_revision_ids");
    if (artifacts == nullptr) {
        return command;
    }
    if (!artifacts->is_array()) {
        throw std::invalid_argument(
            "artifact_revision_ids must be an array");
    }
    for (const auto& artifact : artifacts->as_array()) {
        if (!artifact.is_string()) {
            throw std::invalid_argument(
                "artifact revision IDs must be strings");
        }
        command.artifact_revision_ids.emplace_back(
            artifact.as_string());
    }
    return command;
}

void parse_steady_solver(
    const boost::property_tree::ptree& tree,
    service::SteadySolverSettings& solver) {
    const std::set<std::string> allowed = {
        "max_iterations",
        "residual_tolerance",
        "step_tolerance",
        "linear_residual_tolerance",
        "structural_decomposition_policy",
        "finite_difference_epsilon",
        "min_damping",
        "damping_reduction",
        "sufficient_decrease",
        "max_line_search_steps",
        "continuation_enabled",
        "continuation_initial_step",
        "continuation_minimum_step",
        "continuation_step_growth",
        "continuation_step_reduction",
        "continuation_maximum_stages",
    };
    for (const auto& [key, unused] : tree) {
        (void)unused;
        if (!allowed.contains(key)) {
            throw std::invalid_argument(
                "unknown steady solver field: " + key);
        }
    }
    solver.max_iterations =
        tree.get("max_iterations", solver.max_iterations);
    solver.residual_tolerance = tree.get(
        "residual_tolerance", solver.residual_tolerance);
    solver.step_tolerance =
        tree.get("step_tolerance", solver.step_tolerance);
    solver.linear_residual_tolerance = tree.get(
        "linear_residual_tolerance",
        solver.linear_residual_tolerance);
    solver.structural_decomposition_policy =
        service::structural_decomposition_policy_from_string(
            tree.get(
                "structural_decomposition_policy",
                service::to_string(
                    solver.structural_decomposition_policy)));
    solver.finite_difference_epsilon = tree.get(
        "finite_difference_epsilon",
        solver.finite_difference_epsilon);
    solver.min_damping =
        tree.get("min_damping", solver.min_damping);
    solver.damping_reduction = tree.get(
        "damping_reduction", solver.damping_reduction);
    solver.sufficient_decrease = tree.get(
        "sufficient_decrease", solver.sufficient_decrease);
    solver.max_line_search_steps = tree.get(
        "max_line_search_steps",
        solver.max_line_search_steps);
    solver.continuation_enabled = tree.get(
        "continuation_enabled",
        solver.continuation_enabled);
    solver.continuation_initial_step = tree.get(
        "continuation_initial_step",
        solver.continuation_initial_step);
    solver.continuation_minimum_step = tree.get(
        "continuation_minimum_step",
        solver.continuation_minimum_step);
    solver.continuation_step_growth = tree.get(
        "continuation_step_growth",
        solver.continuation_step_growth);
    solver.continuation_step_reduction = tree.get(
        "continuation_step_reduction",
        solver.continuation_step_reduction);
    solver.continuation_maximum_stages = tree.get(
        "continuation_maximum_stages",
        solver.continuation_maximum_stages);
}

void parse_transient_solver(
    const boost::property_tree::ptree& tree,
    service::TransientSolverSettings& solver) {
    const std::set<std::string> allowed = {
        "start_time",
        "end_time",
        "initial_step",
        "min_step",
        "max_step",
        "absolute_tolerance",
        "relative_tolerance",
        "max_steps",
        "max_consecutive_rejections",
        "maximum_order",
        "compute_consistent_initial_conditions",
        "required_output_times",
        "nonlinear_solver",
    };
    for (const auto& [key, unused] : tree) {
        (void)unused;
        if (!allowed.contains(key)) {
            throw std::invalid_argument(
                "unknown transient solver field: " + key);
        }
    }
    solver.start_time =
        tree.get("start_time", solver.start_time);
    solver.end_time = tree.get("end_time", solver.end_time);
    solver.initial_step =
        tree.get("initial_step", solver.initial_step);
    solver.min_step = tree.get("min_step", solver.min_step);
    solver.max_step = tree.get("max_step", solver.max_step);
    solver.absolute_tolerance = tree.get(
        "absolute_tolerance", solver.absolute_tolerance);
    solver.relative_tolerance = tree.get(
        "relative_tolerance", solver.relative_tolerance);
    solver.max_steps =
        tree.get("max_steps", solver.max_steps);
    solver.max_consecutive_rejections = tree.get(
        "max_consecutive_rejections",
        solver.max_consecutive_rejections);
    solver.maximum_order = tree.get(
        "maximum_order", solver.maximum_order);
    solver.compute_consistent_initial_conditions = tree.get(
        "compute_consistent_initial_conditions",
        solver.compute_consistent_initial_conditions);
    if (const auto output_times =
            tree.get_child_optional("required_output_times")) {
        solver.required_output_times.clear();
        for (const auto& [key, item] : *output_times) {
            if (!key.empty()) {
                throw std::invalid_argument(
                    "required_output_times must be an array");
            }
            solver.required_output_times.push_back(
                item.get_value<double>());
        }
    }
    if (const auto nonlinear =
            tree.get_child_optional("nonlinear_solver")) {
        parse_steady_solver(*nonlinear, solver.nonlinear_solver);
    }
}

std::vector<service::ResultProjection> parse_result_projections(
    const boost::property_tree::ptree& tree) {
    const std::set<std::string> fields = {
        "id", "scope", "component_id", "port_name",
        "value_name", "dimension", "aggregation", "window",
    };
    std::vector<service::ResultProjection> result;
    for (const auto& [key, value] : tree) {
        if (!key.empty()) {
            throw std::invalid_argument(
                "result_projections must be an array");
        }
        for (const auto& [field, unused] : value) {
            (void)unused;
            if (!fields.contains(field)) {
                throw std::invalid_argument(
                    "unknown result projection field: " + field);
            }
        }
        service::ResultProjection projection;
        projection.id = value.get<std::string>("id", "");
        projection.component_id =
            value.get<std::string>("component_id", "");
        projection.port_name =
            value.get<std::string>("port_name", "");
        projection.value_name =
            value.get<std::string>("value_name", "");
        projection.dimension =
            value.get<std::string>("dimension", "");
        try {
            projection.scope = service::result_value_scope_from_string(
                value.get<std::string>("scope", ""));
            projection.aggregation =
                service::result_aggregation_from_string(
                    value.get<std::string>("aggregation", "final"));
            if (const auto encoded =
                    value.get_child_optional("window")) {
                const std::set<std::string> window_fields = {
                    "anchor", "start_time", "end_time",
                    "event_name", "event_occurrence",
                };
                for (const auto& [field, unused] : *encoded) {
                    (void)unused;
                    if (!window_fields.contains(field)) {
                        throw std::invalid_argument(
                            "unknown result projection window field: " +
                            field);
                    }
                }
                service::ResultWindow window;
                window.anchor =
                    service::result_window_anchor_from_string(
                        encoded->get<std::string>("anchor", "simulation"));
                window.start_time =
                    encoded->get<double>("start_time");
                window.end_time = encoded->get<double>("end_time");
                window.event_name =
                    encoded->get<std::string>("event_name", "");
                window.event_occurrence =
                    encoded->get<std::size_t>("event_occurrence", 0U);
                projection.window = std::move(window);
            }
        } catch (const service::ResultProjectionError& error) {
            throw std::invalid_argument(error.what());
        }
        result.push_back(std::move(projection));
    }
    return result;
}

std::vector<service::EngineeringAcceptanceCriterion>
parse_acceptance_criteria(
    const boost::property_tree::ptree& tree) {
    const std::set<std::string> fields = {
        "id", "projection_id", "dimension",
        "lower_bound_si", "upper_bound_si",
        "lower_inclusive", "upper_inclusive",
    };
    std::vector<service::EngineeringAcceptanceCriterion> result;
    for (const auto& [key, value] : tree) {
        if (!key.empty()) {
            throw std::invalid_argument(
                "acceptance_criteria must be an array");
        }
        for (const auto& [field, unused] : value) {
            (void)unused;
            if (!fields.contains(field)) {
                throw std::invalid_argument(
                    "unknown acceptance criterion field: " + field);
            }
        }
        service::EngineeringAcceptanceCriterion criterion;
        criterion.id = value.get<std::string>("id", "");
        criterion.projection_id =
            value.get<std::string>("projection_id", "");
        criterion.dimension =
            value.get<std::string>("dimension", "");
        if (const auto lower =
                value.get_child_optional("lower_bound_si");
            lower && lower->data() != "null") {
            criterion.lower_bound_si =
                lower->get_value<double>();
        }
        if (const auto upper =
                value.get_child_optional("upper_bound_si");
            upper && upper->data() != "null") {
            criterion.upper_bound_si =
                upper->get_value<double>();
        }
        criterion.lower_inclusive =
            value.get("lower_inclusive", true);
        criterion.upper_inclusive =
            value.get("upper_inclusive", true);
        result.push_back(std::move(criterion));
    }
    return result;
}

service::CreateRunConfigurationRevisionRequest
parse_create_run_configuration_request(
    const Request& request) {
    boost::property_tree::ptree tree;
    std::istringstream input(request.body);
    try {
        boost::property_tree::read_json(input, tree);
    } catch (const std::exception& error) {
        throw std::invalid_argument(
            std::string("invalid run configuration JSON: ") +
            error.what());
    }
    const std::set<std::string> allowed = {
        "schema_version",
        "run_configuration_id",
        "parent_run_configuration_revision_id",
        "study_revision_id",
        "steady_solver",
        "transient_solver",
    };
    for (const auto& [key, unused] : tree) {
        (void)unused;
        if (!allowed.contains(key)) {
            throw std::invalid_argument(
                "unknown run configuration field: " + key);
        }
    }
    if (tree.get<std::string>("schema_version", "") !=
        "thermox.run_configuration.create/v3") {
        throw std::invalid_argument(
            "unsupported run configuration create "
            "schema_version");
    }
    service::CreateRunConfigurationRevisionRequest command;
    command.run_configuration_id =
        tree.get<std::string>("run_configuration_id", "");
    command.parent_run_configuration_revision_id =
        tree.get<std::string>(
            "parent_run_configuration_revision_id", "");
    command.study_revision_id =
        tree.get<std::string>("study_revision_id", "");
    if (const auto steady =
            tree.get_child_optional("steady_solver")) {
        parse_steady_solver(*steady, command.steady_solver);
    }
    if (const auto transient =
            tree.get_child_optional("transient_solver")) {
        parse_transient_solver(
            *transient, command.transient_solver);
    }
    return command;
}

service::CreateStudyRevisionRequest parse_create_study_request(
    const Request& request) {
    boost::property_tree::ptree tree;
    std::istringstream input(request.body);
    try {
        boost::property_tree::read_json(input, tree);
    } catch (const std::exception& error) {
        throw std::invalid_argument(
            std::string("invalid study JSON: ") + error.what());
    }
    const std::set<std::string> allowed = {
        "schema_version", "study_id", "parent_study_revision_id",
        "model_revision_id", "case_revision_id", "intent",
        "artifact_revision_ids", "result_projections",
        "acceptance_criteria", "artifact_qualification_requirements",
        "artifact_operating_envelopes",
    };
    for (const auto& [key, unused] : tree) {
        (void)unused;
        if (!allowed.contains(key)) {
            throw std::invalid_argument(
                "unknown study field: " + key);
        }
    }
    if (tree.get<std::string>("schema_version", "") !=
        "thermox.study_revision.create/v4") {
        throw std::invalid_argument(
            "unsupported study create schema_version");
    }
    service::CreateStudyRevisionRequest command;
    command.study_id = tree.get<std::string>("study_id", "");
    command.parent_study_revision_id = tree.get<std::string>(
        "parent_study_revision_id", "");
    command.model_revision_id =
        tree.get<std::string>("model_revision_id", "");
    command.case_revision_id =
        tree.get<std::string>("case_revision_id", "");
    command.intent = tree.get<std::string>("intent", "");
    if (const auto artifacts =
            tree.get_child_optional("artifact_revision_ids")) {
        for (const auto& [key, value] : *artifacts) {
            if (!key.empty()) {
                throw std::invalid_argument(
                    "artifact_revision_ids must be an array");
            }
            command.artifact_revision_ids.push_back(
                value.get_value<std::string>());
        }
    }
    if (const auto requirements = tree.get_child_optional(
            "artifact_qualification_requirements")) {
        for (const auto& [array_key, value] : *requirements) {
            if (!array_key.empty()) {
                throw std::invalid_argument(
                    "artifact_qualification_requirements must be an "
                    "array");
            }
            const std::set<std::string> requirement_fields = {
                "artifact_revision_id", "review_id",
                "acceptable_dispositions",
            };
            for (const auto& [key, unused] : value) {
                (void)unused;
                if (!requirement_fields.contains(key)) {
                    throw std::invalid_argument(
                        "unknown artifact qualification requirement "
                        "field: " + key);
                }
            }
            service::ArtifactQualificationRequirement requirement;
            requirement.artifact_revision_id =
                value.get<std::string>("artifact_revision_id", "");
            requirement.review_id =
                value.get<std::string>("review_id", "");
            const auto dispositions = value.get_child_optional(
                "acceptable_dispositions");
            if (!dispositions) {
                throw std::invalid_argument(
                    "artifact qualification requirement needs "
                    "acceptable_dispositions");
            }
            for (const auto& [key, disposition_node] : *dispositions) {
                if (!key.empty()) {
                    throw std::invalid_argument(
                        "acceptable_dispositions must be an array");
                }
                const auto disposition =
                    disposition_node.get_value<std::string>();
                if (disposition == "approved") {
                    requirement.acceptable_dispositions.push_back(
                        service::EngineeringReviewDisposition::approved);
                } else if (disposition ==
                           "approved_with_conditions") {
                    requirement.acceptable_dispositions.push_back(
                        service::EngineeringReviewDisposition::
                            approved_with_conditions);
                } else {
                    throw std::invalid_argument(
                        "acceptable engineering review disposition must "
                        "be approved or approved_with_conditions");
                }
            }
            command.artifact_qualification_requirements.push_back(
                std::move(requirement));
        }
    }
    if (const auto envelopes = tree.get_child_optional(
            "artifact_operating_envelopes")) {
        for (const auto& [array_key, value] : *envelopes) {
            if (!array_key.empty()) {
                throw std::invalid_argument(
                    "artifact_operating_envelopes must be an array");
            }
            for (const auto& [key, unused] : value) {
                (void)unused;
                if (key != "artifact_revision_id" &&
                    key != "coordinates") {
                    throw std::invalid_argument(
                        "unknown artifact operating-envelope field: " +
                        key);
                }
            }
            service::ArtifactOperatingEnvelope envelope;
            envelope.artifact_revision_id =
                value.get<std::string>("artifact_revision_id", "");
            const auto coordinates = value.get_child_optional("coordinates");
            if (!coordinates) {
                throw std::invalid_argument(
                    "artifact operating envelope needs coordinates");
            }
            for (const auto& [key, encoded] : *coordinates) {
                if (!key.empty()) {
                    throw std::invalid_argument(
                        "operating-envelope coordinates must be an array");
                }
                for (const auto& [field, unused] : encoded) {
                    (void)unused;
                    if (field != "coordinate" && field != "dimension" &&
                        field != "minimum" && field != "maximum" &&
                        field != "minimum_inclusive" &&
                        field != "maximum_inclusive") {
                        throw std::invalid_argument(
                            "unknown operating-envelope coordinate field: " +
                            field);
                    }
                }
                service::ArtifactCoordinateConstraintInput coordinate;
                coordinate.coordinate =
                    encoded.get<std::string>("coordinate", "");
                coordinate.dimension =
                    encoded.get<std::string>("dimension", "");
                if (const auto minimum =
                        encoded.get_optional<double>("minimum")) {
                    coordinate.minimum = *minimum;
                }
                if (const auto maximum =
                        encoded.get_optional<double>("maximum")) {
                    coordinate.maximum = *maximum;
                }
                coordinate.minimum_inclusive =
                    encoded.get("minimum_inclusive", true);
                coordinate.maximum_inclusive =
                    encoded.get("maximum_inclusive", true);
                envelope.coordinates.push_back(std::move(coordinate));
            }
            command.artifact_operating_envelopes.push_back(
                std::move(envelope));
        }
    }
    if (const auto projections =
            tree.get_child_optional("result_projections")) {
        command.result_projections =
            parse_result_projections(*projections);
    }
    if (const auto criteria =
            tree.get_child_optional("acceptance_criteria")) {
        command.acceptance_criteria =
            parse_acceptance_criteria(*criteria);
    }
    return command;
}

service::CreateCalibrationRevisionRequest
parse_create_calibration_request(const Request& request) {
    boost::property_tree::ptree tree;
    std::istringstream input(request.body);
    try {
        boost::property_tree::read_json(input, tree);
    } catch (const std::exception& error) {
        throw std::invalid_argument(
            std::string("invalid calibration revision JSON: ") +
            error.what());
    }
    const std::set<std::string> allowed = {
        "schema_version", "calibration_id",
        "parent_calibration_revision_id", "model_revision_id",
        "training_study_revision_ids", "validation_study_revision_ids",
        "definition", "solver",
    };
    for (const auto& [key, unused] : tree) {
        (void)unused;
        if (!allowed.contains(key)) {
            throw std::invalid_argument(
                "unknown calibration revision field: " + key);
        }
    }
    if (tree.get<std::string>("schema_version", "") !=
        "thermox.calibration_revision.create/v1") {
        throw std::invalid_argument(
            "unsupported calibration revision create schema_version");
    }
    service::CreateCalibrationRevisionRequest command;
    command.calibration_id =
        tree.get<std::string>("calibration_id", "");
    command.parent_calibration_revision_id = tree.get<std::string>(
        "parent_calibration_revision_id", "");
    command.model_revision_id =
        tree.get<std::string>("model_revision_id", "");
    const auto parse_ids = [&](const char* name,
                               std::vector<std::string>& target) {
        if (const auto values = tree.get_child_optional(name)) {
            for (const auto& [key, value] : *values) {
                if (!key.empty()) {
                    throw std::invalid_argument(
                        std::string(name) + " must be an array");
                }
                target.push_back(value.get_value<std::string>());
            }
        }
    };
    parse_ids("training_study_revision_ids",
              command.training_study_revision_ids);
    parse_ids("validation_study_revision_ids",
              command.validation_study_revision_ids);
    const auto definition = tree.get_child_optional("definition");
    if (!definition) {
        throw std::invalid_argument("definition is required");
    }
    const auto typed_root = boost::json::parse(request.body);
    if (!typed_root.is_object()) {
        throw std::invalid_argument(
            "calibration revision body must be an object");
    }
    const auto* typed_definition =
        typed_root.as_object().if_contains("definition");
    if (typed_definition == nullptr || !typed_definition->is_object()) {
        throw std::invalid_argument("definition must be an object");
    }
    command.definition_json = boost::json::serialize(*typed_definition);
    if (const auto solver = tree.get_child_optional("solver")) {
        const std::set<std::string> solver_allowed = {
            "max_iterations", "finite_difference_fraction",
            "initial_trust_region_radius",
            "minimum_trust_region_radius",
            "maximum_trust_region_radius", "acceptance_ratio",
            "gradient_tolerance", "step_tolerance",
            "objective_relative_tolerance",
            "minimum_continuation_fraction", "continuation_growth",
            "steady_simulation_solver", "transient_simulation_solver",
        };
        for (const auto& [key, unused] : *solver) {
            (void)unused;
            if (!solver_allowed.contains(key)) {
                throw std::invalid_argument(
                    "unknown calibration solver field: " + key);
            }
        }
        auto& value = command.solver;
        value.max_iterations =
            solver->get("max_iterations", value.max_iterations);
        value.finite_difference_fraction = solver->get(
            "finite_difference_fraction",
            value.finite_difference_fraction);
        value.initial_trust_region_radius = solver->get(
            "initial_trust_region_radius",
            value.initial_trust_region_radius);
        value.minimum_trust_region_radius = solver->get(
            "minimum_trust_region_radius",
            value.minimum_trust_region_radius);
        value.maximum_trust_region_radius = solver->get(
            "maximum_trust_region_radius",
            value.maximum_trust_region_radius);
        value.acceptance_ratio = solver->get(
            "acceptance_ratio", value.acceptance_ratio);
        value.gradient_tolerance = solver->get(
            "gradient_tolerance", value.gradient_tolerance);
        value.step_tolerance = solver->get(
            "step_tolerance", value.step_tolerance);
        value.objective_relative_tolerance = solver->get(
            "objective_relative_tolerance",
            value.objective_relative_tolerance);
        value.minimum_continuation_fraction = solver->get(
            "minimum_continuation_fraction",
            value.minimum_continuation_fraction);
        value.continuation_growth = solver->get(
            "continuation_growth", value.continuation_growth);
        if (const auto simulation =
                solver->get_child_optional("steady_simulation_solver")) {
            parse_steady_solver(
                *simulation, value.steady_simulation_solver);
        }
        if (const auto simulation = solver->get_child_optional(
                "transient_simulation_solver")) {
            parse_transient_solver(
                *simulation, value.transient_simulation_solver);
        }
    }
    return command;
}

service::CreateReconciliationRevisionRequest
parse_create_reconciliation_request(const Request& request) {
    boost::property_tree::ptree tree;
    std::istringstream input(request.body);
    try {
        boost::property_tree::read_json(input, tree);
    } catch (const std::exception& error) {
        throw std::invalid_argument(
            std::string("invalid reconciliation revision JSON: ") +
            error.what());
    }
    const std::set<std::string> allowed = {
        "schema_version", "reconciliation_id",
        "parent_reconciliation_revision_id", "model_revision_id",
        "constraint_study_revision_ids", "held_out_study_revision_ids",
        "definition", "mode", "solver", "profile_likelihood",
        "joint_confidence_region",
    };
    for (const auto& [key, unused] : tree) {
        (void)unused;
        if (!allowed.contains(key)) {
            throw std::invalid_argument(
                "unknown reconciliation revision field: " + key);
        }
    }
    if (tree.get<std::string>("schema_version", "") !=
        "thermox.reconciliation_revision.create/v1") {
        throw std::invalid_argument(
            "unsupported reconciliation revision create schema_version");
    }
    service::CreateReconciliationRevisionRequest command;
    command.reconciliation_id =
        tree.get<std::string>("reconciliation_id", "");
    command.parent_reconciliation_revision_id = tree.get<std::string>(
        "parent_reconciliation_revision_id", "");
    command.model_revision_id =
        tree.get<std::string>("model_revision_id", "");
    const auto parse_ids = [&](const char* name,
                               std::vector<std::string>& target) {
        if (const auto values = tree.get_child_optional(name)) {
            for (const auto& [key, value] : *values) {
                if (!key.empty()) {
                    throw std::invalid_argument(
                        std::string(name) + " must be an array");
                }
                target.push_back(value.get_value<std::string>());
            }
        }
    };
    parse_ids("constraint_study_revision_ids",
              command.constraint_study_revision_ids);
    parse_ids("held_out_study_revision_ids",
              command.held_out_study_revision_ids);
    const auto mode = tree.get<std::string>("mode", "hard_equalities");
    if (mode == "hard_equalities") {
        command.mode = service::ReconciliationMode::hard_equalities;
    } else if (mode == "weighted_measurements") {
        command.mode = service::ReconciliationMode::weighted_measurements;
    } else {
        throw std::invalid_argument("unsupported reconciliation mode");
    }
    const auto typed_root = boost::json::parse(request.body);
    if (!typed_root.is_object()) {
        throw std::invalid_argument(
            "reconciliation revision body must be an object");
    }
    const auto* typed_definition =
        typed_root.as_object().if_contains("definition");
    if (typed_definition == nullptr || !typed_definition->is_object()) {
        throw std::invalid_argument("definition must be an object");
    }
    command.definition_json = boost::json::serialize(*typed_definition);
    if (const auto solver = tree.get_child_optional("solver")) {
        const std::set<std::string> solver_allowed = {
            "max_iterations", "finite_difference_fraction",
            "constraint_tolerance", "step_tolerance",
            "objective_relative_tolerance",
            "minimum_line_search_fraction", "simulation_solver",
        };
        for (const auto& [key, unused] : *solver) {
            (void)unused;
            if (!solver_allowed.contains(key)) {
                throw std::invalid_argument(
                    "unknown reconciliation solver field: " + key);
            }
        }
        auto& value = command.solver;
        value.max_iterations =
            solver->get("max_iterations", value.max_iterations);
        value.finite_difference_fraction = solver->get(
            "finite_difference_fraction", value.finite_difference_fraction);
        value.constraint_tolerance = solver->get(
            "constraint_tolerance", value.constraint_tolerance);
        value.step_tolerance =
            solver->get("step_tolerance", value.step_tolerance);
        value.objective_relative_tolerance = solver->get(
            "objective_relative_tolerance",
            value.objective_relative_tolerance);
        value.minimum_line_search_fraction = solver->get(
            "minimum_line_search_fraction",
            value.minimum_line_search_fraction);
        if (const auto simulation =
                solver->get_child_optional("simulation_solver")) {
            parse_steady_solver(*simulation, value.simulation_solver);
        }
    }
    if (const auto profile = tree.get_child_optional("profile_likelihood")) {
        const std::set<std::string> profile_allowed = {
            "enabled", "objective_increase", "maximum_bracket_steps",
            "maximum_bisection_steps", "maximum_nuisance_iterations",
            "parameter_ids",
        };
        for (const auto& [key, unused] : *profile) {
            (void)unused;
            if (!profile_allowed.contains(key)) {
                throw std::invalid_argument(
                    "unknown profile likelihood field: " + key);
            }
        }
        auto& value = command.profile_likelihood;
        value.enabled = profile->get("enabled", value.enabled);
        value.objective_increase = profile->get(
            "objective_increase", value.objective_increase);
        value.maximum_bracket_steps = profile->get(
            "maximum_bracket_steps", value.maximum_bracket_steps);
        value.maximum_bisection_steps = profile->get(
            "maximum_bisection_steps", value.maximum_bisection_steps);
        value.maximum_nuisance_iterations = profile->get(
            "maximum_nuisance_iterations",
            value.maximum_nuisance_iterations);
        if (const auto parameters =
                profile->get_child_optional("parameter_ids")) {
            value.parameter_ids.clear();
            for (const auto& [key, parameter] : *parameters) {
                if (!key.empty()) {
                    throw std::invalid_argument(
                        "profile_likelihood.parameter_ids must be an array");
                }
                value.parameter_ids.push_back(
                    parameter.get_value<std::string>());
            }
        }
    }
    if (const auto region =
            tree.get_child_optional("joint_confidence_region")) {
        const std::set<std::string> region_allowed = {
            "enabled", "objective_increase", "parameter_ids",
        };
        for (const auto& [key, unused] : *region) {
            (void)unused;
            if (!region_allowed.contains(key)) {
                throw std::invalid_argument(
                    "unknown joint confidence region field: " + key);
            }
        }
        auto& value = command.joint_confidence_region;
        value.enabled = region->get("enabled", value.enabled);
        value.objective_increase = region->get(
            "objective_increase", value.objective_increase);
        if (const auto parameters =
                region->get_child_optional("parameter_ids")) {
            value.parameter_ids.clear();
            for (const auto& [key, parameter] : *parameters) {
                if (!key.empty()) {
                    throw std::invalid_argument(
                        "joint_confidence_region.parameter_ids must be "
                        "an array");
                }
                value.parameter_ids.push_back(
                    parameter.get_value<std::string>());
            }
        }
    }
    return command;
}

Response project_response(
    const service::ProjectRecord& project,
    int status) {
    auto response = json_response(
        status, service::serialize_project_json(project));
    response.headers["Location"] =
        "/api/v1/projects/" + project.project_id;
    return response;
}

Response model_revision_response(
    const service::ModelRevisionRecord& revision,
    int status) {
    auto response = json_response(
        status,
        service::serialize_model_revision_json(revision));
    response.headers["Location"] =
        "/api/v1/projects/" + revision.project_id +
        "/model-revisions/" + revision.model_revision_id;
    response.headers["ETag"] =
        "\"" + revision.checksum + "\"";
    return response;
}

Response case_revision_response(
    const service::CaseRevisionRecord& revision,
    int status) {
    auto response = json_response(
        status,
        service::serialize_case_revision_json(revision));
    response.headers["Location"] =
        "/api/v1/projects/" + revision.project_id +
        "/model-revisions/" + revision.model_revision_id +
        "/case-revisions/" + revision.case_revision_id;
    response.headers["ETag"] =
        "\"" + revision.checksum + "\"";
    return response;
}

Response artifact_revision_response(
    const service::ArtifactRevisionRecord& revision,
    int status) {
    auto response = json_response(
        status,
        service::serialize_artifact_revision_json(revision));
    response.headers["ETag"] =
        "\"" + revision.content.checksum + "\"";
    response.headers["Location"] =
        "/api/v1/projects/" + revision.project_id +
        "/artifact-revisions/" +
        revision.artifact_revision_id;
    return response;
}

Response artifact_revision_content_response(
    const service::ArtifactRevisionContent& content) {
    auto response = json_response(
        200,
        service::serialize_artifact_revision_content_json(content));
    response.headers["ETag"] =
        "\"" + content.revision.content.checksum + "\"";
    return response;
}

Response performance_map_quality_review_response(
    const service::PerformanceMapQualityReviewRecord& review,
    int status) {
    auto response = json_response(
        status,
        service::serialize_performance_map_quality_review_json(review));
    response.headers["ETag"] =
        "\"" + review.quality_snapshot_checksum + "\"";
    response.headers["Location"] =
        "/api/v1/projects/" + review.project_id +
        "/artifact-revisions/" + review.artifact_revision_id +
        "/quality-reviews/" + review.review_id;
    return response;
}

Response run_configuration_revision_response(
    const service::RunConfigurationRevisionRecord& revision,
    int status) {
    auto response = json_response(
        status,
        service::
            serialize_run_configuration_revision_json(
                revision));
    response.headers["ETag"] =
        "\"" + revision.checksum + "\"";
    response.headers["Location"] =
        "/api/v1/projects/" + revision.project_id +
        "/run-configuration-revisions/" +
        revision.run_configuration_revision_id;
    return response;
}

Response study_revision_response(
    const service::StudyRevisionRecord& revision,
    int status) {
    auto response = json_response(
        status,
        service::serialize_study_revision_json(revision));
    response.headers["ETag"] =
        "\"" + revision.checksum + "\"";
    response.headers["Location"] =
        "/api/v1/projects/" + revision.project_id +
        "/study-revisions/" + revision.study_revision_id;
    return response;
}

Response calibration_revision_response(
    const service::CalibrationRevisionRecord& revision,
    int status) {
    auto response = json_response(
        status,
        service::serialize_calibration_revision_json(revision));
    response.headers["ETag"] = "\"" + revision.checksum + "\"";
    response.headers["Location"] =
        "/api/v1/projects/" + revision.project_id +
        "/calibration-revisions/" + revision.calibration_revision_id;
    return response;
}

Response reconciliation_revision_response(
    const service::ReconciliationRevisionRecord& revision,
    int status) {
    auto response = json_response(
        status,
        service::serialize_reconciliation_revision_json(revision));
    response.headers["ETag"] = "\"" + revision.checksum + "\"";
    response.headers["Location"] =
        "/api/v1/projects/" + revision.project_id +
        "/reconciliation-revisions/" +
        revision.reconciliation_revision_id;
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
        std::shared_ptr<service::ProjectService> project_service,
        ApiOptions api_options)
        : simulation(runtime),
          jobs(std::move(job_service)),
          projects(std::move(project_service)),
          project_validation(projects, runtime),
          project_components(projects, std::move(runtime)),
          options(api_options) {
        if (options.maximum_body_bytes == 0U) {
            throw std::invalid_argument(
                "maximum_body_bytes must be positive");
        }
        if (!jobs) {
            throw std::invalid_argument(
                "simulation job service must not be null");
        }
        if (!projects) {
            throw std::invalid_argument(
                "project service must not be null");
        }
    }

    service::SimulationService simulation;
    std::shared_ptr<service::SimulationJobService> jobs;
    std::shared_ptr<service::ProjectService> projects;
    service::ProjectModelValidationService project_validation;
    service::ProjectComponentCatalogService project_components;
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
          std::make_shared<service::ProjectService>(
              service::make_in_memory_project_repository()),
          options) {}

Api::Api(
    std::shared_ptr<const service::SimulationRuntime> runtime,
    std::shared_ptr<service::SimulationJobService> jobs,
    std::shared_ptr<service::ProjectService> projects,
    ApiOptions options)
    : impl_(std::make_unique<Impl>(
          std::move(runtime),
          std::move(jobs),
          std::move(projects),
          options)) {}

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

        if (target.path ==
            "/api/v1/correlation-artifacts/instantiate") {
            reject_unknown_query(target.query, {});
            if (method != "post") {
                auto response = error_response(
                    405, "method_not_allowed",
                    "correlation instantiation only supports POST");
                response.headers["Allow"] = "POST";
                return response;
            }
            require_json_request(
                request, impl_->options.maximum_body_bytes);
            const auto result = impl_->simulation
                .instantiate_correlation(
                    parse_correlation_instantiation_request(
                        request));
            return json_response(
                operation_status(result.status),
                service::
                    serialize_correlation_instantiation_response_json(
                        result));
        }

        if (target.path ==
            "/api/v1/regime-map-artifacts/instantiate") {
            reject_unknown_query(target.query, {});
            if (method != "post") {
                auto response = error_response(
                    405, "method_not_allowed",
                    "regime-map instantiation only supports POST");
                response.headers["Allow"] = "POST";
                return response;
            }
            require_json_request(
                request, impl_->options.maximum_body_bytes);
            const auto result = impl_->simulation
                .instantiate_regime_map(
                    parse_regime_map_instantiation_request(request));
            return json_response(
                operation_status(result.status),
                service::
                    serialize_regime_map_instantiation_response_json(
                        result));
        }

        if (target.path == "/api/v1/projects") {
            reject_unknown_query(target.query, {});
            const auto& identity = require_identity(request);
            if (method == "get") {
                return json_response(
                    200,
                    service::serialize_projects_json(
                        impl_->projects->list_projects(identity)));
            }
            if (method == "post") {
                require_json_request(
                    request, impl_->options.maximum_body_bytes);
                auto command =
                    parse_create_project_request(request);
                command.identity = identity;
                return project_response(
                    impl_->projects->create_project(command),
                    201);
            }
            auto response = error_response(
                405,
                "method_not_allowed",
                "projects only support GET and POST");
            response.headers["Allow"] = "GET, POST";
            return response;
        }

        constexpr std::string_view project_prefix =
            "/api/v1/projects/";
        if (target.path.starts_with(project_prefix)) {
            const auto& identity = require_identity(request);
            const std::string suffix =
                target.path.substr(project_prefix.size());
            const auto separator = suffix.find('/');
            const std::string project_id =
                suffix.substr(0, separator);
            if (project_id.empty()) {
                return error_response(
                    404,
                    "route_not_found",
                    "no route matches the request target");
            }
            if (separator == std::string::npos) {
                reject_unknown_query(target.query, {});
                if (method != "get") {
                    auto response = error_response(
                        405,
                        "method_not_allowed",
                        "project detail only supports GET");
                    response.headers["Allow"] = "GET";
                    return response;
                }
                const auto project =
                    impl_->projects->get_project(
                        identity, project_id);
                if (!project) {
                    return error_response(
                        404,
                        "project_not_found",
                        "project was not found");
                }
                return project_response(*project, 200);
            }

            constexpr std::string_view revisions_segment =
                "/model-revisions";
            const std::string remainder =
                suffix.substr(separator);
            constexpr std::string_view artifacts_segment =
                "/artifact-revisions";
            constexpr std::string_view component_catalog_segment =
                "/component-catalog";
            constexpr std::string_view run_configurations_segment =
                "/run-configuration-revisions";
            constexpr std::string_view studies_segment =
                "/study-revisions";
            constexpr std::string_view calibrations_segment =
                "/calibration-revisions";
            constexpr std::string_view reconciliations_segment =
                "/reconciliation-revisions";
            if (remainder == component_catalog_segment) {
                reject_unknown_query(target.query, {});
                if (method != "get") {
                    auto response = error_response(
                        405,
                        "method_not_allowed",
                        "project component catalog only "
                        "supports GET");
                    response.headers["Allow"] = "GET";
                    return response;
                }
                return json_response(
                    200,
                    service::
                        serialize_project_component_catalog_json(
                            impl_->project_components.get(
                                identity, project_id)));
            }
            if (remainder == run_configurations_segment) {
                if (!impl_->projects
                         ->get_project(identity, project_id)) {
                    return error_response(
                        404,
                        "project_not_found",
                        "project was not found");
                }
                reject_unknown_query(target.query, {});
                if (method == "get") {
                    return json_response(
                        200,
                        service::
                            serialize_run_configuration_revisions_json(
                                impl_->projects
                                    ->list_run_configuration_revisions(
                                        identity, project_id)));
                }
                if (method == "post") {
                    require_json_request(
                        request,
                        impl_->options.maximum_body_bytes);
                    auto command =
                        parse_create_run_configuration_request(
                            request);
                    command.identity = identity;
                    command.project_id = project_id;
                    return run_configuration_revision_response(
                        impl_->projects
                            ->create_run_configuration_revision(
                                command),
                        201);
                }
                auto response = error_response(
                    405,
                    "method_not_allowed",
                    "run configuration revisions only support "
                    "GET and POST");
                response.headers["Allow"] = "GET, POST";
                return response;
            }
            if (remainder == studies_segment) {
                if (!impl_->projects
                         ->get_project(identity, project_id)) {
                    return error_response(
                        404,
                        "project_not_found",
                        "project was not found");
                }
                reject_unknown_query(target.query, {});
                if (method == "get") {
                    return json_response(
                        200,
                        service::serialize_study_revisions_json(
                            impl_->projects->list_study_revisions(
                                identity, project_id)));
                }
                if (method == "post") {
                    require_json_request(
                        request,
                        impl_->options.maximum_body_bytes);
                    auto command =
                        parse_create_study_request(request);
                    command.identity = identity;
                    command.project_id = project_id;
                    return study_revision_response(
                        impl_->projects->create_study_revision(
                            command),
                        201);
                }
                auto response = error_response(
                    405,
                    "method_not_allowed",
                    "study revisions only support GET and POST");
                response.headers["Allow"] = "GET, POST";
                return response;
            }
            if (remainder == calibrations_segment) {
                if (!impl_->projects->get_project(identity, project_id)) {
                    return error_response(
                        404, "project_not_found", "project was not found");
                }
                reject_unknown_query(target.query, {});
                if (method == "get") {
                    return json_response(
                        200,
                        service::serialize_calibration_revisions_json(
                            impl_->projects->list_calibration_revisions(
                                identity, project_id)));
                }
                if (method == "post") {
                    require_json_request(
                        request, impl_->options.maximum_body_bytes);
                    auto command =
                        parse_create_calibration_request(request);
                    command.identity = identity;
                    command.project_id = project_id;
                    return calibration_revision_response(
                        impl_->projects->create_calibration_revision(command),
                        201);
                }
                auto response = error_response(
                    405, "method_not_allowed",
                    "calibration revisions only support GET and POST");
                response.headers["Allow"] = "GET, POST";
                return response;
            }
            if (remainder == reconciliations_segment) {
                if (!impl_->projects->get_project(identity, project_id)) {
                    return error_response(
                        404, "project_not_found", "project was not found");
                }
                reject_unknown_query(target.query, {});
                if (method == "get") {
                    return json_response(
                        200,
                        service::serialize_reconciliation_revisions_json(
                            impl_->projects
                                ->list_reconciliation_revisions(
                                    identity, project_id)));
                }
                if (method == "post") {
                    require_json_request(
                        request, impl_->options.maximum_body_bytes);
                    auto command =
                        parse_create_reconciliation_request(request);
                    command.identity = identity;
                    command.project_id = project_id;
                    return reconciliation_revision_response(
                        impl_->projects
                            ->create_reconciliation_revision(command),
                        201);
                }
                auto response = error_response(
                    405, "method_not_allowed",
                    "reconciliation revisions only support GET and POST");
                response.headers["Allow"] = "GET, POST";
                return response;
            }
            const std::string study_detail_prefix =
                std::string(studies_segment) + "/";
            if (remainder.starts_with(study_detail_prefix)) {
                reject_unknown_query(target.query, {});
                if (method != "get") {
                    auto response = error_response(
                        405,
                        "method_not_allowed",
                        "study revision detail only supports GET");
                    response.headers["Allow"] = "GET";
                    return response;
                }
                const auto revision_id = remainder.substr(
                    study_detail_prefix.size());
                if (revision_id.empty() ||
                    revision_id.find('/') != std::string::npos) {
                    return error_response(
                        404,
                        "route_not_found",
                        "no route matches the request target");
                }
                const auto revision = impl_->projects
                    ->get_study_revision(
                        identity, project_id, revision_id);
                if (!revision) {
                    return error_response(
                        404,
                        "study_revision_not_found",
                        "study revision was not found");
                }
                return study_revision_response(*revision, 200);
            }
            const std::string calibration_detail_prefix =
                std::string(calibrations_segment) + "/";
            if (remainder.starts_with(calibration_detail_prefix)) {
                reject_unknown_query(target.query, {});
                if (method != "get") {
                    auto response = error_response(
                        405, "method_not_allowed",
                        "calibration revision detail only supports GET");
                    response.headers["Allow"] = "GET";
                    return response;
                }
                const auto revision_id = remainder.substr(
                    calibration_detail_prefix.size());
                if (revision_id.empty() ||
                    revision_id.find('/') != std::string::npos) {
                    return error_response(
                        404, "route_not_found",
                        "no route matches the request target");
                }
                const auto revision = impl_->projects
                    ->get_calibration_revision(
                        identity, project_id, revision_id);
                if (!revision) {
                    return error_response(
                        404, "calibration_revision_not_found",
                        "calibration revision was not found");
                }
                return calibration_revision_response(*revision, 200);
            }
            const std::string reconciliation_detail_prefix =
                std::string(reconciliations_segment) + "/";
            if (remainder.starts_with(reconciliation_detail_prefix)) {
                reject_unknown_query(target.query, {});
                if (method != "get") {
                    auto response = error_response(
                        405, "method_not_allowed",
                        "reconciliation revision detail only supports GET");
                    response.headers["Allow"] = "GET";
                    return response;
                }
                const auto revision_id = remainder.substr(
                    reconciliation_detail_prefix.size());
                if (revision_id.empty() ||
                    revision_id.find('/') != std::string::npos) {
                    return error_response(
                        404, "route_not_found",
                        "no route matches the request target");
                }
                const auto revision = impl_->projects
                    ->get_reconciliation_revision(
                        identity, project_id, revision_id);
                if (!revision) {
                    return error_response(
                        404, "reconciliation_revision_not_found",
                        "reconciliation revision was not found");
                }
                return reconciliation_revision_response(*revision, 200);
            }
            const std::string run_configuration_detail_prefix =
                std::string(run_configurations_segment) + "/";
            if (remainder.starts_with(
                    run_configuration_detail_prefix)) {
                reject_unknown_query(target.query, {});
                if (method != "get") {
                    auto response = error_response(
                        405,
                        "method_not_allowed",
                        "run configuration revision detail "
                        "only supports GET");
                    response.headers["Allow"] = "GET";
                    return response;
                }
                const auto revision_id = remainder.substr(
                    run_configuration_detail_prefix.size());
                if (revision_id.empty() ||
                    revision_id.find('/') !=
                        std::string::npos) {
                    return error_response(
                        404,
                        "route_not_found",
                        "no route matches the request target");
                }
                const auto revision = impl_->projects
                    ->get_run_configuration_revision(
                        identity, project_id, revision_id);
                if (!revision) {
                    return error_response(
                        404,
                        "run_configuration_revision_not_found",
                        "run configuration revision was not "
                        "found");
                }
                return run_configuration_revision_response(
                    *revision, 200);
            }
            if (remainder == artifacts_segment) {
                if (!impl_->projects
                         ->get_project(identity, project_id)) {
                    return error_response(
                        404,
                        "project_not_found",
                        "project was not found");
                }
                if (method == "get") {
                    reject_unknown_query(target.query, {});
                    return json_response(
                        200,
                        service::
                            serialize_artifact_revisions_json(
                                impl_->projects
                                    ->list_artifact_revisions(
                                        identity, project_id)));
                }
                if (method == "post") {
                    reject_unknown_query(
                        target.query,
                        {
                            "artifact_id",
                            "artifact_type",
                            "artifact_schema_version",
                            "parent_revision_id",
                        });
                    require_json_request(
                        request,
                        impl_->options.maximum_body_bytes);
                    service::CreateArtifactRevisionRequest
                        command;
                    command.identity = identity;
                    command.project_id = project_id;
                    command.artifact_id = optional_query(
                        target.query, "artifact_id");
                    command.artifact_type = optional_query(
                        target.query, "artifact_type");
                    command.artifact_schema_version =
                        optional_query(
                            target.query,
                            "artifact_schema_version");
                    command.parent_artifact_revision_id =
                        optional_query(
                            target.query,
                            "parent_revision_id");
                    command.artifact_json = request.body;
                    return artifact_revision_response(
                        impl_->projects
                            ->create_artifact_revision(command),
                        201);
                }
                auto response = error_response(
                    405,
                    "method_not_allowed",
                    "artifact revisions only support GET and "
                    "POST");
                response.headers["Allow"] = "GET, POST";
                return response;
            }
            const std::string artifact_detail_prefix =
                std::string(artifacts_segment) + "/";
            constexpr std::string_view quality_reviews_suffix =
                "/quality-reviews";
            constexpr std::string_view quality_review_detail_marker =
                "/quality-reviews/";
            const auto quality_review_detail =
                remainder.find(quality_review_detail_marker);
            if (remainder.starts_with(artifact_detail_prefix) &&
                quality_review_detail != std::string::npos) {
                const auto artifact_revision_id = remainder.substr(
                    artifact_detail_prefix.size(),
                    quality_review_detail -
                        artifact_detail_prefix.size());
                const auto review_id = remainder.substr(
                    quality_review_detail +
                    quality_review_detail_marker.size());
                if (artifact_revision_id.empty() || review_id.empty() ||
                    review_id.find('/') != std::string::npos) {
                    return error_response(
                        404,
                        "route_not_found",
                        "no route matches the request target");
                }
                reject_unknown_query(target.query, {});
                if (method != "get") {
                    auto response = error_response(
                        405,
                        "method_not_allowed",
                        "performance-map quality review detail only "
                        "supports GET");
                    response.headers["Allow"] = "GET";
                    return response;
                }
                const auto reviews = impl_->projects
                    ->list_performance_map_quality_reviews(
                        identity, project_id, artifact_revision_id);
                const auto review = std::find_if(
                    reviews.begin(), reviews.end(),
                    [&](const auto& candidate) {
                        return candidate.review_id == review_id;
                    });
                if (review == reviews.end()) {
                    return error_response(
                        404,
                        "performance_map_quality_review_not_found",
                        "performance-map quality review was not found");
                }
                return performance_map_quality_review_response(
                    *review, 200);
            }
            if (remainder.starts_with(artifact_detail_prefix) &&
                remainder.ends_with(quality_reviews_suffix)) {
                const auto artifact_revision_id = remainder.substr(
                    artifact_detail_prefix.size(),
                    remainder.size() - artifact_detail_prefix.size() -
                        quality_reviews_suffix.size());
                if (artifact_revision_id.empty() ||
                    artifact_revision_id.find('/') !=
                        std::string::npos) {
                    return error_response(
                        404,
                        "route_not_found",
                        "no route matches the request target");
                }
                const auto artifact = impl_->projects
                    ->get_artifact_revision(
                        identity, project_id, artifact_revision_id);
                if (!artifact) {
                    return error_response(
                        404,
                        "artifact_revision_not_found",
                        "artifact revision was not found");
                }
                reject_unknown_query(target.query, {});
                if (method == "get") {
                    return json_response(
                        200,
                        service::
                            serialize_performance_map_quality_reviews_json(
                                impl_->projects
                                    ->list_performance_map_quality_reviews(
                                        identity,
                                        project_id,
                                        artifact_revision_id)));
                }
                if (method == "post") {
                    require_json_request(
                        request,
                        impl_->options.maximum_body_bytes);
                    auto command =
                        parse_performance_map_quality_review_request(
                            request);
                    command.identity = identity;
                    command.project_id = project_id;
                    command.artifact_revision_id = artifact_revision_id;
                    return performance_map_quality_review_response(
                        impl_->projects
                            ->create_performance_map_quality_review(
                                command),
                        201);
                }
                auto response = error_response(
                    405,
                    "method_not_allowed",
                    "performance-map quality reviews only support "
                    "GET and POST");
                response.headers["Allow"] = "GET, POST";
                return response;
            }
            if (remainder.starts_with(
                    artifact_detail_prefix)) {
                reject_unknown_query(target.query, {});
                if (method != "get") {
                    auto response = error_response(
                        405,
                        "method_not_allowed",
                        "artifact revision detail only supports "
                        "GET");
                    response.headers["Allow"] = "GET";
                    return response;
                }
                const auto artifact_revision_id =
                    remainder.substr(
                        artifact_detail_prefix.size());
                if (artifact_revision_id.empty() ||
                    artifact_revision_id.find('/') !=
                        std::string::npos) {
                    return error_response(
                        404,
                        "route_not_found",
                        "no route matches the request target");
                }
                const auto artifact =
                    impl_->projects->get_artifact_revision_content(
                        identity,
                        project_id,
                        artifact_revision_id);
                if (!artifact) {
                    return error_response(
                        404,
                        "artifact_revision_not_found",
                        "artifact revision was not found");
                }
                return artifact_revision_content_response(*artifact);
            }
            if (remainder == revisions_segment) {
                if (!impl_->projects
                         ->get_project(identity, project_id)) {
                    return error_response(
                        404,
                        "project_not_found",
                        "project was not found");
                }
                if (method == "get") {
                    reject_unknown_query(target.query, {});
                    return json_response(
                        200,
                        service::serialize_model_revisions_json(
                            impl_->projects
                                ->list_model_revisions(
                                    identity, project_id)));
                }
                if (method == "post") {
                    reject_unknown_query(
                        target.query, {"parent_revision_id"});
                    require_json_request(
                        request,
                        impl_->options.maximum_body_bytes);
                    service::CreateModelRevisionRequest command;
                    command.identity = identity;
                    command.project_id = project_id;
                    command.parent_model_revision_id =
                        optional_query(
                            target.query,
                            "parent_revision_id");
                    command.model_json = request.body;
                    return model_revision_response(
                        impl_->projects
                            ->create_model_revision(command),
                        201);
                }
                auto response = error_response(
                    405,
                    "method_not_allowed",
                    "model revisions only support GET and POST");
                response.headers["Allow"] = "GET, POST";
                return response;
            }

            const std::string detail_prefix =
                std::string(revisions_segment) + "/";
            if (remainder.starts_with(detail_prefix)) {
                const auto model_path =
                    remainder.substr(detail_prefix.size());
                const auto nested_separator =
                    model_path.find('/');
                const auto revision_id =
                    model_path.substr(0, nested_separator);
                if (revision_id.empty()) {
                    return error_response(
                        404,
                        "route_not_found",
                        "no route matches the request target");
                }
                const auto revision =
                    impl_->projects->get_model_revision(
                        identity, project_id, revision_id);
                if (!revision) {
                    return error_response(
                        404,
                        "model_revision_not_found",
                        "model revision was not found");
                }
                if (nested_separator == std::string::npos) {
                    reject_unknown_query(target.query, {});
                    if (method != "get") {
                        auto response = error_response(
                            405,
                            "method_not_allowed",
                            "model revision detail only supports "
                            "GET");
                        response.headers["Allow"] = "GET";
                        return response;
                    }
                    return model_revision_response(*revision, 200);
                }

                constexpr std::string_view edits_segment =
                    "/edits";
                constexpr std::string_view cases_segment =
                    "/case-revisions";
                const auto case_path =
                    model_path.substr(nested_separator);
                if (case_path == edits_segment) {
                    reject_unknown_query(target.query, {});
                    if (method != "post") {
                        auto response = error_response(
                            405,
                            "method_not_allowed",
                            "graph edits only support POST");
                        response.headers["Allow"] = "POST";
                        return response;
                    }
                    require_json_request(
                        request,
                        impl_->options.maximum_body_bytes);
                    auto command =
                        parse_graph_edit_request(request);
                    command.identity = identity;
                    command.project_id = project_id;
                    command.base_model_revision_id =
                        revision_id;
                    return model_revision_response(
                        impl_->projects->apply_graph_edits(
                            command),
                        201);
                }
                if (case_path == cases_segment) {
                    if (method == "get") {
                        reject_unknown_query(target.query, {});
                        return json_response(
                            200,
                            service::serialize_case_revisions_json(
                                impl_->projects
                                    ->list_case_revisions(
                                        identity,
                                        project_id,
                                        revision_id)));
                    }
                    if (method == "post") {
                        reject_unknown_query(
                            target.query,
                            {"parent_revision_id"});
                        require_json_request(
                            request,
                            impl_->options.maximum_body_bytes);
                        service::CreateCaseRevisionRequest command;
                        command.identity = identity;
                        command.project_id = project_id;
                        command.model_revision_id = revision_id;
                        command.parent_case_revision_id =
                            optional_query(
                                target.query,
                                "parent_revision_id");
                        command.case_json = request.body;
                        return case_revision_response(
                            impl_->projects
                                ->create_case_revision(command),
                            201);
                    }
                    auto response = error_response(
                        405,
                        "method_not_allowed",
                        "case revisions only support GET and "
                        "POST");
                    response.headers["Allow"] = "GET, POST";
                    return response;
                }

                const std::string case_detail_prefix =
                    std::string(cases_segment) + "/";
                if (case_path.starts_with(case_detail_prefix)) {
                    reject_unknown_query(target.query, {});
                    const auto case_suffix =
                        case_path.substr(
                            case_detail_prefix.size());
                    constexpr std::string_view
                        case_edits_suffix{"/edits"};
                    if (case_suffix.ends_with(
                            case_edits_suffix)) {
                        const auto case_revision_id =
                            case_suffix.substr(
                                0,
                                case_suffix.size() -
                                    case_edits_suffix.size());
                        if (case_revision_id.empty() ||
                            case_revision_id.find('/') !=
                                std::string::npos) {
                            return error_response(
                                404,
                                "route_not_found",
                                "no route matches the request "
                                "target");
                        }
                        if (method != "post") {
                            auto response = error_response(
                                405,
                                "method_not_allowed",
                                "case edits only support POST");
                            response.headers["Allow"] = "POST";
                            return response;
                        }
                        require_json_request(
                            request,
                            impl_->options.maximum_body_bytes);
                        auto command =
                            parse_case_edit_request(request);
                        command.identity = identity;
                        command.project_id = project_id;
                        command.model_revision_id = revision_id;
                        command.base_case_revision_id =
                            case_revision_id;
                        return case_revision_response(
                            impl_->projects->apply_case_edits(
                                command),
                            201);
                    }
                    constexpr std::string_view
                        validation_suffix{"/validate"};
                    if (case_suffix.ends_with(
                            validation_suffix)) {
                        const auto case_revision_id =
                            case_suffix.substr(
                                0,
                                case_suffix.size() -
                                    validation_suffix.size());
                        if (case_revision_id.empty() ||
                            case_revision_id.find('/') !=
                                std::string::npos) {
                            return error_response(
                                404,
                                "route_not_found",
                                "no route matches the request "
                                "target");
                        }
                        if (method != "post") {
                            auto response = error_response(
                                405,
                                "method_not_allowed",
                                "project model validation only "
                                "supports POST");
                            response.headers["Allow"] = "POST";
                            return response;
                        }
                        require_json_request(
                            request,
                            impl_->options.maximum_body_bytes);
                        auto command =
                            parse_project_model_validation_request(
                                request);
                        command.identity = identity;
                        command.project_id = project_id;
                        command.model_revision_id = revision_id;
                        command.case_revision_id =
                            case_revision_id;
                        const auto result =
                            impl_->project_validation.validate(
                                command);
                        return json_response(
                            operation_status(
                                result.validation.status),
                            service::
                                serialize_project_model_validation_json(
                                    result));
                    }
                    if (method != "get") {
                        auto response = error_response(
                            405,
                            "method_not_allowed",
                            "case revision detail only supports "
                            "GET");
                        response.headers["Allow"] = "GET";
                        return response;
                    }
                    const auto case_revision_id = case_suffix;
                    if (case_revision_id.empty() ||
                        case_revision_id.find('/') !=
                            std::string::npos) {
                        return error_response(
                            404,
                            "route_not_found",
                            "no route matches the request target");
                    }
                    const auto simulation_case =
                        impl_->projects->get_case_revision(
                            identity,
                            project_id,
                            revision_id,
                            case_revision_id);
                    if (!simulation_case) {
                        return error_response(
                            404,
                            "case_revision_not_found",
                            "case revision was not found");
                    }
                    return case_revision_response(
                        *simulation_case, 200);
                }
            }
            return error_response(
                404,
                "route_not_found",
                "no route matches the request target");
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

        if (target.path ==
            "/api/v1/simulations/structural-policy-audit") {
            if (!impl_->options.enable_synchronous_simulations) {
                return error_response(
                    404,
                    "route_not_found",
                    "synchronous simulation routes are disabled; "
                    "structural policy audit is unavailable");
            }
            if (method != "post") {
                auto response = error_response(
                    405, "method_not_allowed",
                    "structural policy audit only supports POST");
                response.headers["Allow"] = "POST";
                return response;
            }
            reject_unknown_query(
                target.query,
                {"case_id", "policies",
                 "normalized_solution_tolerance"});
            require_json_request(
                request, impl_->options.maximum_body_bytes);
            service::StructuralPolicyAuditRequest command;
            command.model_json = request.body;
            command.case_id = optional_query(
                target.query, "case_id");
            const auto policies = optional_query(
                target.query, "policies");
            if (!policies.empty()) {
                command.policies =
                    structural_policy_list(policies);
            }
            command.normalized_solution_tolerance =
                optional_positive_double(
                    target.query,
                    "normalized_solution_tolerance",
                    command.normalized_solution_tolerance);
            const auto result = impl_->simulation
                .run_structural_policy_audit(command);
            return json_response(
                operation_status(result.status),
                service::
                    serialize_structural_policy_audit_response_json(
                        result));
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

        if (target.path == "/api/v1/jobs") {
            if (method == "get") {
                reject_unknown_query(
                    target.query,
                    {
                        "project_id",
                        "run_configuration_revision_id",
                        "calibration_revision_id",
                        "reconciliation_revision_id",
                        "state",
                        "limit",
                        "cursor",
                    });
                const auto& identity = require_identity(request);
                service::SimulationJobQuery query;
                query.project_id =
                    optional_query(target.query, "project_id");
                query.run_configuration_revision_id =
                    optional_query(
                        target.query,
                        "run_configuration_revision_id");
                query.calibration_revision_id =
                    optional_query(
                        target.query,
                        "calibration_revision_id");
                query.reconciliation_revision_id =
                    optional_query(
                        target.query,
                        "reconciliation_revision_id");
                query.state = optional_job_state(target.query);
                query.limit = optional_history_limit(target.query);
                const auto cursor =
                    optional_query(target.query, "cursor");
                if (!cursor.empty()) {
                    query.before = decode_job_cursor(cursor);
                }
                const auto page =
                    impl_->jobs->list(identity, query);
                const auto next_cursor = page.next
                    ? encode_job_cursor(*page.next)
                    : std::string{};
                return json_response(
                    200,
                    service::serialize_job_page_json(
                        page, next_cursor));
            }
            if (method != "post") {
                auto response = error_response(
                    405, "method_not_allowed",
                    "jobs only support GET and POST");
                response.headers["Allow"] = "GET, POST";
                return response;
            }
            reject_unknown_query(
                target.query,
                {
                    "project_id",
                    "run_configuration_revision_id",
                    "calibration_revision_id",
                    "reconciliation_revision_id",
                });
            if (!request.body.empty()) {
                throw std::invalid_argument(
                    "revision-backed job submission "
                    "must not contain a request body");
            }
            const auto& identity = require_identity(request);
            const auto project_id =
                optional_query(target.query, "project_id");
            const auto run_configuration_revision_id =
                optional_query(
                    target.query,
                    "run_configuration_revision_id");
            const auto calibration_revision_id =
                optional_query(
                    target.query,
                    "calibration_revision_id");
            const auto reconciliation_revision_id =
                optional_query(
                    target.query,
                    "reconciliation_revision_id");
            const auto execution_revision_count =
                static_cast<int>(!run_configuration_revision_id.empty()) +
                static_cast<int>(!calibration_revision_id.empty()) +
                static_cast<int>(!reconciliation_revision_id.empty());
            if (project_id.empty() || execution_revision_count != 1) {
                throw std::invalid_argument(
                    "project_id and exactly one execution revision "
                    "ID are required");
            }
            if (!reconciliation_revision_id.empty()) {
                const auto resolved =
                    impl_->projects->resolve_reconciliation(
                        identity, project_id,
                        reconciliation_revision_id);
                if (!resolved) {
                    return error_response(
                        404, "reconciliation_revision_not_found",
                        "reconciliation revision was not found");
                }
                service::SimulationJobRequest command;
                command.identity = identity;
                command.idempotency_key =
                    required_header(request, "idempotency-key");
                command.mode =
                    service::SimulationJobMode::reconciliation;
                command.model_json = resolved->executable_model_json;
                command.reconciliation_id =
                    resolved->reconciliation.reconciliation_id;
                command.source_revisions =
                    service::RevisionProvenance{
                        project_id,
                        resolved->model.model_revision_id,
                        resolved->model.checksum,
                        {}, {}, {}, {}, {}, {}, {}, {},
                        resolved->reconciliation
                            .reconciliation_revision_id,
                        resolved->reconciliation.checksum,
                    };
                command.reconciliation_mode =
                    resolved->reconciliation.mode;
                command.reconciliation_solver =
                    resolved->reconciliation.solver;
                command.reconciliation_profile_likelihood =
                    resolved->reconciliation.profile_likelihood;
                command.reconciliation_joint_confidence_region =
                    resolved->reconciliation.joint_confidence_region;
                command.reconciliation_held_out_cases =
                    resolved->held_out_cases;
                command.artifacts = resolved->artifacts.snapshot;
                command.components = resolved->artifacts.components;
                const auto record = impl_->jobs->submit(command);
                return job_record_response(
                    record,
                    service::is_terminal(record.state) ? 200 : 202);
            }
            if (!calibration_revision_id.empty()) {
                const auto resolved =
                    impl_->projects->resolve_calibration(
                        identity, project_id,
                        calibration_revision_id);
                if (!resolved) {
                    return error_response(
                        404, "calibration_revision_not_found",
                        "calibration revision was not found");
                }
                service::SimulationJobRequest command;
                command.identity = identity;
                command.idempotency_key =
                    required_header(request, "idempotency-key");
                command.mode =
                    service::SimulationJobMode::calibration;
                command.model_json = resolved->executable_model_json;
                command.calibration_id =
                    resolved->calibration.calibration_id;
                command.source_revisions =
                    service::RevisionProvenance{
                        project_id,
                        resolved->model.model_revision_id,
                        resolved->model.checksum,
                        {}, {}, {}, {}, {}, {},
                        resolved->calibration
                            .calibration_revision_id,
                        resolved->calibration.checksum,
                        {},
                        {},
                    };
                command.calibration_solver =
                    resolved->calibration.solver;
                command.calibration_predictions =
                    resolved->validation_predictions;
                command.artifacts = resolved->artifacts.snapshot;
                command.components = resolved->artifacts.components;
                const auto record = impl_->jobs->submit(command);
                return job_record_response(
                    record,
                    service::is_terminal(record.state) ? 200 : 202);
            }
            const auto resolved =
                impl_->projects->resolve_run_configuration(
                    identity,
                    project_id,
                    run_configuration_revision_id);
            if (!resolved) {
                return error_response(
                    404,
                    "run_configuration_revision_not_found",
                    "run configuration revision was not found");
            }
            service::SimulationJobRequest command;
            command.identity = identity;
            command.idempotency_key =
                required_header(request, "idempotency-key");
            command.mode =
                (resolved->study.intent.find("dynamic") ==
                         std::string::npos &&
                     resolved->study.intent.find("transient") ==
                         std::string::npos)
                ? service::SimulationJobMode::steady
                : service::SimulationJobMode::transient;
            command.model_json =
                resolved->model_case.executable_model_json;
            command.case_id = resolved->model_case.case_id;
            command.source_revisions =
                service::RevisionProvenance{
                    resolved->model_case.project_id,
                    resolved->model_case.model_revision_id,
                    resolved->model_case.model_checksum,
                    resolved->model_case.case_revision_id,
                    resolved->model_case.case_checksum,
                    resolved->configuration
                        .run_configuration_revision_id,
                    resolved->configuration.checksum,
                    resolved->study.study_revision_id,
                    resolved->study.checksum,
                    {},
                    {},
                    {},
                    {},
                };
            command.artifacts =
                resolved->artifacts.snapshot;
            command.components =
                resolved->artifacts.components;
            command.steady_solver =
                resolved->configuration.steady_solver;
            command.transient_solver =
                resolved->configuration.transient_solver;
            command.result_projections =
                resolved->study.result_projections;
            command.acceptance_criteria =
                resolved->study.acceptance_criteria;
            const auto record = impl_->jobs->submit(command);
            return job_record_response(
                record,
                service::is_terminal(record.state) ? 200 : 202);
        }

        if (target.path == "/api/v1/job-comparisons") {
            if (method != "post") {
                auto response = error_response(
                    405, "method_not_allowed",
                    "job comparison only supports POST");
                response.headers["Allow"] = "POST";
                return response;
            }
            reject_unknown_query(target.query, {});
            require_json_request(
                request, impl_->options.maximum_body_bytes);
            const auto& identity = require_identity(request);
            const auto [baseline_job_id, candidate_job_id] =
                parse_job_comparison_request(request);
            const auto comparison = impl_->jobs->compare(
                identity, baseline_job_id, candidate_job_id);
            if (!comparison) {
                return error_response(
                    404, "comparison_job_not_found",
                    "one or more comparison jobs were not found");
            }
            return json_response(
                200,
                service::serialize_job_comparison_json(
                    *comparison));
        }

        constexpr std::string_view job_prefix =
            "/api/v1/jobs/";
        if (target.path.starts_with(job_prefix)) {
            reject_unknown_query(target.query, {});
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
            if (result_requested && method != "get") {
                auto response = error_response(
                    405, "method_not_allowed",
                    "simulation results only support GET");
                response.headers["Allow"] = "GET";
                return response;
            }
            if (!result_requested &&
                method != "get" && method != "delete") {
                auto response = error_response(
                    405, "method_not_allowed",
                    "simulation jobs only support GET and DELETE");
                response.headers["Allow"] = "GET, DELETE";
                return response;
            }
            if (method == "delete") {
                if (!request.body.empty()) {
                    throw std::invalid_argument(
                        "simulation cancellation must not contain "
                        "a request body");
                }
                std::uint64_t expected_revision = 0;
                try {
                    expected_revision =
                        required_revision_precondition(request);
                } catch (const PreconditionRequired& error) {
                    return error_response(
                        428,
                        "precondition_required",
                        error.what());
                }
                if (!impl_->jobs->get(identity, suffix)) {
                    return error_response(
                        404,
                        "simulation_not_found",
                        "simulation job was not found");
                }
                try {
                    const auto cancelled = impl_->jobs->cancel(
                        identity, suffix, expected_revision);
                    return job_record_response(cancelled, 200);
                } catch (const service::JobConflictError& error) {
                    return error_response(
                        412,
                        "revision_precondition_failed",
                        error.what());
                }
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
    } catch (const service::ProjectRequestError& error) {
        return error_response(
            400, "invalid_project_request", error.what());
    } catch (const service::ProjectStateError& error) {
        return error_response(
            404, "project_not_found", error.what());
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
