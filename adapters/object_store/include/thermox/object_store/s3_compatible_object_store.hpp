#pragma once

#include "thermox/object_store/object_store.hpp"

#include <chrono>
#include <memory>
#include <string>

namespace thermox::object_store {

enum class S3AddressingStyle {
    path,
    virtual_hosted,
};

struct S3CompatibleOptions {
    std::string endpoint;
    std::string region{"us-east-1"};
    std::string bucket;
    std::string access_key;
    std::string secret_key;
    S3AddressingStyle addressing_style{S3AddressingStyle::path};
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds request_timeout{30000};
};

// Uses the portable S3 REST contract and AWS Signature V4. MinIO is the
// first exercised provider; AWS S3 and other S3-compatible endpoints use
// the same driver with different options.
std::shared_ptr<ObjectStore> make_s3_compatible_object_store(
    S3CompatibleOptions options);

}  // namespace thermox::object_store
