#include "thermox/physics/property_registry.hpp"

#include "thermox/physics/co2_package.hpp"
#include "thermox/physics/ideal_gas_package.hpp"
#include "thermox/physics/if97_package.hpp"

#include <stdexcept>
#include <utility>

namespace thermox::physics {

void PropertyPackageRegistry::register_backend(std::string backend,
                                               PropertyPackageFactory factory) {
    if (backend.empty() || !factory)
        throw std::invalid_argument("property backend registration must be non-empty");
    if (!factories_.emplace(std::move(backend), std::move(factory)).second)
        throw std::invalid_argument("duplicate property backend registration");
}

bool PropertyPackageRegistry::contains(std::string_view backend) const {
    return factories_.find(backend) != factories_.end();
}

std::shared_ptr<const PropertyPackage> PropertyPackageRegistry::create(
    std::string_view backend, std::string_view substance) const {
    const auto it = factories_.find(backend);
    if (it == factories_.end())
        throw std::invalid_argument("no property package registered for backend: " +
                                    std::string(backend));
    auto package = it->second(substance);
    if (!package)
        throw std::runtime_error("property package factory returned null for backend: " +
                                 std::string(backend));
    return package;
}

std::vector<std::string> PropertyPackageRegistry::backends() const {
    std::vector<std::string> result;
    result.reserve(factories_.size());
    for (const auto& [backend, _] : factories_) result.push_back(backend);
    return result;
}

PropertyPackageRegistry make_default_property_package_registry() {
    PropertyPackageRegistry registry;
    const auto ideal_gas = [](std::string_view) {
        return std::make_shared<IdealGasPropertyPackage>();
    };
    const auto co2 = [](std::string_view substance) -> std::shared_ptr<const PropertyPackage> {
        if (substance != "CO2" && substance != "CarbonDioxide")
            throw std::invalid_argument("CO2 backend requires substance CO2");
        return std::make_shared<Co2PropertyPackage>();
    };
    const auto if97 = [](std::string_view substance) -> std::shared_ptr<const PropertyPackage> {
        if (substance != "Water" && substance != "Steam")
            throw std::invalid_argument("IF97 backend requires substance Water or Steam");
        return std::make_shared<If97PropertyPackage>();
    };
    registry.register_backend("ideal_gas", ideal_gas);
    registry.register_backend("ideal_gas_mixture", ideal_gas);
    registry.register_backend("co2_span_wagner", co2);
    registry.register_backend("co2", co2);
    registry.register_backend("if97", if97);
    registry.register_backend("water_steam_if97", if97);
    registry.register_backend("coolprop_if97", if97);
    return registry;
}

}  // namespace thermox::physics
