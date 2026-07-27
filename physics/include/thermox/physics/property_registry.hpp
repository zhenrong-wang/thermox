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

class PropertyPackageRegistry {
public:
    void register_backend(std::string backend, PropertyPackageFactory factory);
    [[nodiscard]] bool contains(std::string_view backend) const;
    [[nodiscard]] std::shared_ptr<const PropertyPackage> create(
        std::string_view backend, std::string_view substance) const;
    [[nodiscard]] std::vector<std::string> backends() const;

private:
    std::map<std::string, PropertyPackageFactory, std::less<>> factories_;
};

PropertyPackageRegistry make_default_property_package_registry();

}  // namespace thermox::physics
