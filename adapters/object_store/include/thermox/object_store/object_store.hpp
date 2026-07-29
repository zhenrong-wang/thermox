#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>

namespace thermox::object_store {

struct Object {
    std::string content;
    std::string media_type;
    std::map<std::string, std::string> metadata;
};

// Provider-neutral byte-object boundary. Provider drivers own transport,
// authentication, bucket/container addressing, and provider error mapping.
class ObjectStore {
public:
    virtual ~ObjectStore() = default;

    // A successful put guarantees that a subsequent get can read the
    // object. Repeating a put for the same key and bytes is idempotent.
    virtual void put(
        const std::string& key,
        const Object& object) = 0;
    virtual std::optional<Object> get(
        const std::string& key) const = 0;
};

}  // namespace thermox::object_store
