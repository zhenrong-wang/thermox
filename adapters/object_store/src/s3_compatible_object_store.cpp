#include "thermox/object_store/s3_compatible_object_store.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <memory>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace thermox::object_store {

namespace {

class CurlGlobal {
public:
    CurlGlobal() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw std::runtime_error(
                "could not initialize libcurl");
        }
    }
    ~CurlGlobal() {
        curl_global_cleanup();
    }
};

void ensure_curl_initialized() {
    static CurlGlobal global;
    (void)global;
}

struct EasyDeleter {
    void operator()(CURL* easy) const {
        if (easy != nullptr) {
            curl_easy_cleanup(easy);
        }
    }
};

struct HeadersDeleter {
    void operator()(curl_slist* headers) const {
        if (headers != nullptr) {
            curl_slist_free_all(headers);
        }
    }
};

using Easy = std::unique_ptr<CURL, EasyDeleter>;
using Headers = std::unique_ptr<curl_slist, HeadersDeleter>;

void check(CURLcode code, std::string_view operation) {
    if (code != CURLE_OK) {
        throw std::runtime_error(
            std::string(operation) + ": " +
            curl_easy_strerror(code));
    }
}

template <typename Value>
void set(
    CURL* easy,
    CURLoption option,
    Value value,
    std::string_view description) {
    check(
        curl_easy_setopt(easy, option, value),
        description);
}

std::string trim(std::string value) {
    const auto whitespace = [](unsigned char character) {
        return std::isspace(character) != 0;
    };
    value.erase(
        value.begin(),
        std::find_if_not(
            value.begin(), value.end(), whitespace));
    value.erase(
        std::find_if_not(
            value.rbegin(), value.rend(), whitespace).base(),
        value.end());
    return value;
}

std::string lowercase(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

void validate_header_value(
    const std::string& value,
    std::string_view name) {
    if (value.find_first_of("\r\n") != std::string::npos) {
        throw std::invalid_argument(
            std::string(name) +
            " contains an invalid header character");
    }
}

void validate_metadata_name(const std::string& name) {
    if (name.empty() ||
        !std::all_of(
            name.begin(),
            name.end(),
            [](unsigned char character) {
                return std::islower(character) ||
                    std::isdigit(character) ||
                    character == '-';
            })) {
        throw std::invalid_argument(
            "object metadata names must contain only lowercase "
            "letters, digits, and hyphens");
    }
}

std::string escaped_segment(
    CURL* easy,
    std::string_view segment) {
    if (segment.size() >
        static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        throw std::invalid_argument(
            "object key segment is too large");
    }
    char* escaped = curl_easy_escape(
        easy,
        segment.data(),
        static_cast<int>(segment.size()));
    if (escaped == nullptr) {
        throw std::runtime_error(
            "could not URL-encode object key");
    }
    std::string result(escaped);
    curl_free(escaped);
    return result;
}

std::string escaped_key(
    CURL* easy,
    const std::string& key) {
    if (key.empty() || key.front() == '/') {
        throw std::invalid_argument(
            "object key must be a non-empty relative key");
    }
    std::string result;
    std::size_t begin = 0;
    while (begin <= key.size()) {
        const auto end = key.find('/', begin);
        const auto segment = key.substr(
            begin,
            end == std::string::npos
                ? std::string::npos
                : end - begin);
        if (segment.empty() || segment == "." ||
            segment == "..") {
            throw std::invalid_argument(
                "object key contains an unsafe path segment");
        }
        if (!result.empty()) {
            result.push_back('/');
        }
        result += escaped_segment(easy, segment);
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return result;
}

std::string normalize_endpoint(std::string endpoint) {
    while (!endpoint.empty() && endpoint.back() == '/') {
        endpoint.pop_back();
    }
    if (!endpoint.starts_with("http://") &&
        !endpoint.starts_with("https://")) {
        throw std::invalid_argument(
            "S3 endpoint must use http or https");
    }
    if (endpoint.find_first_of("?#") != std::string::npos) {
        throw std::invalid_argument(
            "S3 endpoint must not contain a query or fragment");
    }
    return endpoint;
}

void validate_bucket(
    const std::string& bucket,
    S3AddressingStyle style) {
    if (bucket.empty()) {
        throw std::invalid_argument(
            "S3 bucket must not be empty");
    }
    if (style == S3AddressingStyle::virtual_hosted &&
        !std::all_of(
            bucket.begin(),
            bucket.end(),
            [](unsigned char character) {
                return std::isalnum(character) ||
                    character == '-' || character == '.';
            })) {
        throw std::invalid_argument(
            "virtual-hosted S3 bucket is not DNS compatible");
    }
}

std::string object_url(
    CURL* easy,
    const S3CompatibleOptions& options,
    const std::string& key) {
    const auto encoded_key = escaped_key(easy, key);
    if (options.addressing_style ==
        S3AddressingStyle::path) {
        return options.endpoint + "/" +
            escaped_segment(easy, options.bucket) + "/" +
            encoded_key;
    }

    const auto scheme_end = options.endpoint.find("://");
    const auto authority_begin = scheme_end + 3;
    const auto path_begin =
        options.endpoint.find('/', authority_begin);
    const auto authority = options.endpoint.substr(
        authority_begin,
        path_begin == std::string::npos
            ? std::string::npos
            : path_begin - authority_begin);
    if (authority.empty() ||
        authority.find('@') != std::string::npos) {
        throw std::invalid_argument(
            "S3 endpoint authority is invalid");
    }
    const auto base_path =
        path_begin == std::string::npos
        ? std::string{}
        : options.endpoint.substr(path_begin);
    return options.endpoint.substr(0, authority_begin) +
        options.bucket + "." + authority + base_path + "/" +
        encoded_key;
}

std::size_t write_body(
    char* data,
    std::size_t size,
    std::size_t count,
    void* context) {
    const auto bytes = size * count;
    try {
        static_cast<std::string*>(context)->append(data, bytes);
        return bytes;
    } catch (...) {
        return 0;
    }
}

struct Upload {
    const std::string* content;
    std::size_t offset{0};
};

std::size_t read_body(
    char* destination,
    std::size_t size,
    std::size_t count,
    void* context) {
    auto& upload = *static_cast<Upload*>(context);
    const auto capacity = size * count;
    const auto remaining =
        upload.content->size() - upload.offset;
    const auto bytes = std::min(capacity, remaining);
    std::copy_n(
        upload.content->data() + upload.offset,
        bytes,
        destination);
    upload.offset += bytes;
    return bytes;
}

std::size_t receive_header(
    char* data,
    std::size_t size,
    std::size_t count,
    void* context) {
    const auto bytes = size * count;
    const std::string line(data, bytes);
    const auto separator = line.find(':');
    if (separator == std::string::npos) {
        return bytes;
    }
    const auto name =
        lowercase(trim(line.substr(0, separator)));
    const auto value = trim(line.substr(separator + 1));
    try {
        auto& object = *static_cast<Object*>(context);
        constexpr std::string_view metadata_prefix =
            "x-amz-meta-";
        if (name.starts_with(metadata_prefix)) {
            object.metadata.emplace(
                name.substr(metadata_prefix.size()), value);
        } else if (name == "content-type") {
            object.media_type = value;
        }
        return bytes;
    } catch (...) {
        return 0;
    }
}

std::string response_excerpt(const std::string& response) {
    constexpr std::size_t maximum = 512;
    if (response.size() <= maximum) {
        return response;
    }
    return response.substr(0, maximum) + "...";
}

class S3CompatibleObjectStore final : public ObjectStore {
public:
    explicit S3CompatibleObjectStore(
        S3CompatibleOptions options)
        : options_(std::move(options)) {
        ensure_curl_initialized();
        options_.endpoint =
            normalize_endpoint(std::move(options_.endpoint));
        validate_bucket(
            options_.bucket, options_.addressing_style);
        if (options_.region.empty() ||
            options_.access_key.empty() ||
            options_.secret_key.empty()) {
            throw std::invalid_argument(
                "S3 region and credentials must not be empty");
        }
        if (options_.connect_timeout.count() <= 0 ||
            options_.request_timeout.count() <= 0) {
            throw std::invalid_argument(
                "S3 timeouts must be positive");
        }
    }

    void put(
        const std::string& key,
        const Object& object) override {
        Easy easy{curl_easy_init()};
        if (!easy) {
            throw std::runtime_error(
                "could not allocate an S3 request");
        }
        configure(easy.get(), key);
        validate_header_value(
            object.media_type, "object media type");

        curl_slist* raw_headers = nullptr;
        const auto append = [&](const std::string& header) {
            curl_slist* updated = curl_slist_append(
                raw_headers, header.c_str());
            if (updated == nullptr) {
                throw std::runtime_error(
                    "could not allocate S3 headers");
            }
            raw_headers = updated;
        };
        try {
            append("Expect:");
            append("Content-Type: " + object.media_type);
            for (const auto& [name, value] : object.metadata) {
                validate_metadata_name(name);
                validate_header_value(value, name);
                append("x-amz-meta-" + name + ": " + value);
            }
        } catch (...) {
            curl_slist_free_all(raw_headers);
            throw;
        }
        Headers headers{raw_headers};
        Upload upload{&object.content};
        std::string response;
        set(
            easy.get(), CURLOPT_HTTPHEADER, headers.get(),
            "could not set S3 PUT headers");
        set(
            easy.get(), CURLOPT_UPLOAD, 1L,
            "could not configure S3 PUT");
        set(
            easy.get(), CURLOPT_READFUNCTION, read_body,
            "could not set S3 upload callback");
        set(
            easy.get(), CURLOPT_READDATA, &upload,
            "could not set S3 upload body");
        set(
            easy.get(),
            CURLOPT_INFILESIZE_LARGE,
            static_cast<curl_off_t>(object.content.size()),
            "could not set S3 upload size");
        set(
            easy.get(), CURLOPT_WRITEFUNCTION, write_body,
            "could not set S3 response callback");
        set(
            easy.get(), CURLOPT_WRITEDATA, &response,
            "could not set S3 response body");
        perform(easy.get(), response, "PUT", false);
    }

    std::optional<Object> get(
        const std::string& key) const override {
        Easy easy{curl_easy_init()};
        if (!easy) {
            throw std::runtime_error(
                "could not allocate an S3 request");
        }
        configure(easy.get(), key);
        Object object;
        set(
            easy.get(), CURLOPT_WRITEFUNCTION, write_body,
            "could not set S3 download callback");
        set(
            easy.get(), CURLOPT_WRITEDATA, &object.content,
            "could not set S3 download body");
        set(
            easy.get(), CURLOPT_HEADERFUNCTION, receive_header,
            "could not set S3 header callback");
        set(
            easy.get(), CURLOPT_HEADERDATA, &object,
            "could not set S3 response metadata");
        const auto status =
            perform(easy.get(), object.content, "GET", true);
        if (status == 404) {
            return std::nullopt;
        }
        return object;
    }

private:
    void configure(
        CURL* easy,
        const std::string& key) const {
        const auto url = object_url(easy, options_, key);
        const auto credentials =
            options_.access_key + ":" + options_.secret_key;
        const auto signature =
            "aws:amz:" + options_.region + ":s3";
        set(
            easy, CURLOPT_URL, url.c_str(),
            "could not set S3 URL");
        set(
            easy, CURLOPT_USERPWD, credentials.c_str(),
            "could not set S3 credentials");
        set(
            easy, CURLOPT_AWS_SIGV4, signature.c_str(),
            "could not configure S3 Signature V4");
        set(
            easy, CURLOPT_HTTPAUTH,
            static_cast<long>(CURLAUTH_AWS_SIGV4),
            "could not select S3 authentication");
        set(
            easy, CURLOPT_CONNECTTIMEOUT_MS,
            static_cast<long>(
                options_.connect_timeout.count()),
            "could not set S3 connect timeout");
        set(
            easy, CURLOPT_TIMEOUT_MS,
            static_cast<long>(
                options_.request_timeout.count()),
            "could not set S3 request timeout");
        set(
            easy, CURLOPT_NOSIGNAL, 1L,
            "could not configure S3 signal handling");
    }

    static long perform(
        CURL* easy,
        const std::string& response,
        std::string_view operation,
        bool allow_not_found) {
        check(
            curl_easy_perform(easy),
            std::string("S3 ") + std::string(operation));
        long status = 0;
        check(
            curl_easy_getinfo(
                easy, CURLINFO_RESPONSE_CODE, &status),
            "could not read S3 response status");
        if (status >= 200 && status < 300) {
            return status;
        }
        if (allow_not_found && status == 404) {
            return status;
        }
        throw std::runtime_error(
            "S3 " + std::string(operation) +
            " failed with HTTP " + std::to_string(status) +
            ": " + response_excerpt(response));
    }

    S3CompatibleOptions options_;
};

}  // namespace

std::shared_ptr<ObjectStore> make_s3_compatible_object_store(
    S3CompatibleOptions options) {
    return std::make_shared<S3CompatibleObjectStore>(
        std::move(options));
}

}  // namespace thermox::object_store
