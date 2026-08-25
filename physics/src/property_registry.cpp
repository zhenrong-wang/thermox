#include "thermox/physics/property_registry.hpp"

#include "thermox/physics/co2_package.hpp"
#include "thermox/physics/ideal_gas_package.hpp"
#include "thermox/physics/if97_package.hpp"
#include "thermox/physics/incompressible_package.hpp"
#include "thermox/physics/tabulated_incompressible_package.hpp"
#include "thermox/physics/water_heos_package.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace thermox::physics {

void PropertyPackageRegistry::register_backend(
    PropertyBackendDescriptor descriptor,
    PropertyPackageFactory factory) {
    if (descriptor.backend.empty() ||
        descriptor.implementation_name.empty() ||
        descriptor.implementation_version.empty() || !factory) {
        throw std::invalid_argument("property backend registration must be non-empty");
    }
    const std::string backend = descriptor.backend;
    if (!entries_
             .emplace(
                 backend,
                 Entry{std::move(descriptor), std::move(factory)})
             .second) {
        throw std::invalid_argument("duplicate property backend registration");
    }
}

bool PropertyPackageRegistry::contains(std::string_view backend) const {
    return entries_.find(backend) != entries_.end();
}

std::shared_ptr<const PropertyPackage> PropertyPackageRegistry::create(
    std::string_view backend, std::string_view substance) const {
    const auto it = entries_.find(backend);
    if (it == entries_.end())
        throw std::invalid_argument("no property package registered for backend: " +
                                    std::string(backend));
    const auto& substances = it->second.descriptor.supported_substances;
    if (!substances.empty() &&
        std::find(
            substances.begin(), substances.end(), substance) ==
            substances.end()) {
        throw std::invalid_argument(
            "property backend '" + std::string(backend) +
            "' does not support substance: " +
            std::string(substance));
    }
    auto package = it->second.factory(substance);
    if (!package)
        throw std::runtime_error("property package factory returned null for backend: " +
                                 std::string(backend));
    if (package->name() !=
            it->second.descriptor.implementation_name ||
        package->version() !=
            it->second.descriptor.implementation_version) {
        throw std::runtime_error(
            "property package identity does not match registered descriptor for backend: " +
            std::string(backend));
    }
    constexpr std::array capabilities{
        PropertyCapability::state_pt,
        PropertyCapability::state_ph,
        PropertyCapability::state_ph_derivatives,
        PropertyCapability::state_ps,
        PropertyCapability::saturation_p,
        PropertyCapability::transport,
        PropertyCapability::surface_tension,
    };
    for (const auto capability : capabilities) {
        const bool declared =
            std::find(
                it->second.descriptor.capabilities.begin(),
                it->second.descriptor.capabilities.end(),
                capability) !=
            it->second.descriptor.capabilities.end();
        if (declared != package->supports(capability)) {
            throw std::runtime_error(
                "property package capabilities do not match registered descriptor for backend: " +
                std::string(backend));
        }
    }
    return package;
}

std::vector<std::string> PropertyPackageRegistry::backends() const {
    std::vector<std::string> result;
    result.reserve(entries_.size());
    for (const auto& [backend, _] : entries_) result.push_back(backend);
    return result;
}

std::vector<PropertyBackendDescriptor>
PropertyPackageRegistry::descriptors() const {
    std::vector<PropertyBackendDescriptor> result;
    result.reserve(entries_.size());
    for (const auto& [_, entry] : entries_) {
        result.push_back(entry.descriptor);
    }
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
    const auto water_heos = [](std::string_view substance)
        -> std::shared_ptr<const PropertyPackage> {
        if (substance != "Water" && substance != "Steam")
            throw std::invalid_argument(
                "HEOS water backend requires substance Water or Steam");
        return std::make_shared<WaterHeosPropertyPackage>();
    };
    const auto incompressible = [](std::string_view substance)
        -> std::shared_ptr<const PropertyPackage> {
        return std::make_shared<IncompressiblePropertyPackage>(
            std::string{substance});
    };
    const auto solar_salt_table = [](std::string_view substance)
        -> std::shared_ptr<const PropertyPackage> {
        if (substance != "SolarSalt") {
            throw std::invalid_argument(
                "Sandia solar-salt table requires substance SolarSalt");
        }
        return make_sandia_solar_salt_property_package();
    };
    const std::vector all_flashes{
        PropertyCapability::state_pt,
        PropertyCapability::state_ph,
        PropertyCapability::state_ph_derivatives,
        PropertyCapability::state_ps,
        PropertyCapability::saturation_p,
        PropertyCapability::transport,
        PropertyCapability::surface_tension,
    };
    const std::vector ideal_capabilities{
        PropertyCapability::state_pt,
        PropertyCapability::state_ph,
        PropertyCapability::state_ph_derivatives,
        PropertyCapability::state_ps,
    };
    const std::vector if97_capabilities{
        PropertyCapability::state_pt,
        PropertyCapability::state_ph,
        PropertyCapability::state_ps,
        PropertyCapability::saturation_p,
        PropertyCapability::transport,
        PropertyCapability::surface_tension,
    };
    const auto ideal_identity = ideal_gas("Air");
    const auto co2_identity = co2("CO2");
    const auto if97_identity = if97("Water");
    const auto water_heos_identity = water_heos("Water");
    const auto incompressible_identity = incompressible("SolarSalt");
    const auto solar_salt_table_identity =
        solar_salt_table("SolarSalt");
    const std::string ideal_name{ideal_identity->name()};
    const std::string ideal_version{ideal_identity->version()};
    const std::string co2_name{co2_identity->name()};
    const std::string co2_version{co2_identity->version()};
    const std::string if97_name{if97_identity->name()};
    const std::string if97_version{if97_identity->version()};
    const std::string water_heos_name{water_heos_identity->name()};
    const std::string water_heos_version{water_heos_identity->version()};
    const std::string incompressible_name{
        incompressible_identity->name()};
    const std::string incompressible_version{
        incompressible_identity->version()};
    const std::string solar_salt_table_name{
        solar_salt_table_identity->name()};
    const std::string solar_salt_table_version{
        solar_salt_table_identity->version()};
    registry.register_backend(
        {"ideal_gas", ideal_name, ideal_version, {"Air"},
         ideal_capabilities},
        ideal_gas);
    registry.register_backend(
        {"ideal_gas_mixture", ideal_name, ideal_version, {"Air"},
         ideal_capabilities},
        ideal_gas);
    registry.register_backend(
        {"co2_span_wagner", co2_name, co2_version,
         {"CO2", "CarbonDioxide"}, all_flashes},
        co2);
    registry.register_backend(
        {"co2", co2_name, co2_version,
         {"CO2", "CarbonDioxide"}, all_flashes},
        co2);
    registry.register_backend(
        {"if97", if97_name, if97_version,
         {"Water", "Steam"}, if97_capabilities},
        if97);
    registry.register_backend(
        {"water_steam_if97", if97_name, if97_version,
         {"Water", "Steam"}, if97_capabilities},
        if97);
    registry.register_backend(
        {"coolprop_if97", if97_name, if97_version,
         {"Water", "Steam"}, if97_capabilities},
        if97);
    registry.register_backend(
        {"coolprop_heos", water_heos_name, water_heos_version,
         {"Water", "Steam"}, all_flashes},
        water_heos);
    registry.register_backend(
        {"coolprop_incompressible", incompressible_name,
         incompressible_version, {"SolarSalt", "NaK"},
         {PropertyCapability::state_pt,
          PropertyCapability::state_ph,
          PropertyCapability::state_ps,
         PropertyCapability::transport}},
        incompressible);
    registry.register_backend(
        {"sandia_solar_salt_table", solar_salt_table_name,
         solar_salt_table_version, {"SolarSalt"},
         {PropertyCapability::state_pt,
          PropertyCapability::state_ph,
          PropertyCapability::state_ps,
          PropertyCapability::transport}},
        solar_salt_table);
    return registry;
}

}  // namespace thermox::physics
