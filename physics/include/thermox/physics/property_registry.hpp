#pragma once

#include "thermox/physics/property_package.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace thermox::physics {

using PropertyPackageFactory =
    std::function<std::shared_ptr<const PropertyPackage>(std::string_view substance)>;

struct PropertyBackendDescriptor {
    std::string backend;
    std::string implementation_name;
    std::string implementation_version;
    std::vector<std::string> supported_substances;
    std::vector<PropertyCapability> capabilities;
};

class PropertyPackageRegistry {
public:
    void register_backend(
        PropertyBackendDescriptor descriptor,
        PropertyPackageFactory factory);
    [[nodiscard]] bool contains(std::string_view backend) const;
    [[nodiscard]] std::shared_ptr<const PropertyPackage> create(
        std::string_view backend, std::string_view substance) const;
    [[nodiscard]] std::vector<std::string> backends() const;
    [[nodiscard]] std::vector<PropertyBackendDescriptor>
    descriptors() const;

private:
    struct Entry {
        PropertyBackendDescriptor descriptor;
        PropertyPackageFactory factory;
    };
    std::map<std::string, Entry, std::less<>> entries_;
};

PropertyPackageRegistry make_default_property_package_registry();

}  // namespace thermox::physics
