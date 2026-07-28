#include "thermox/service/native_runtime.hpp"

#include "runtime_internal.hpp"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>

namespace thermox::service {

namespace {

void hash_text(std::uint64_t& hash, std::string_view text) {
    constexpr std::uint64_t prime = 1099511628211ULL;
    for (const unsigned char value : text) {
        hash ^= value;
        hash *= prime;
    }
    hash ^= 0xffU;
    hash *= prime;
}

void hash_number(std::uint64_t& hash, double value) {
    std::ostringstream out;
    out << std::setprecision(17) << value;
    hash_text(hash, out.str());
}

std::string catalog_fingerprint(
    const platform::ComponentRegistry& components,
    const physics::PropertyPackageRegistry& properties) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto& descriptor : components.descriptors()) {
        hash_text(hash, descriptor.kind);
        hash_text(hash, descriptor.version);
        for (const auto& port : descriptor.ports) {
            hash_text(hash, port.name);
            hash_text(hash, port.domain);
            hash_text(hash, port.direction);
        }
        for (const auto& parameter : descriptor.parameters) {
            hash_text(hash, parameter.name);
            hash_text(hash, parameter.dimension);
            hash_text(hash, parameter.required ? "required" : "optional");
            hash_text(
                hash,
                parameter.default_value.has_value()
                    ? "has_default"
                    : "no_default");
            if (parameter.default_value.has_value()) {
                hash_number(hash, *parameter.default_value);
            }
            hash_number(hash, parameter.lower_bound);
            hash_number(hash, parameter.upper_bound);
            hash_text(
                hash,
                parameter.lower_inclusive ? "inclusive" : "exclusive");
            hash_text(
                hash,
                parameter.upper_inclusive ? "inclusive" : "exclusive");
        }
        for (const auto capability :
             descriptor.required_property_capabilities) {
            hash_text(
                hash,
                std::to_string(static_cast<int>(capability)));
        }
        hash_text(
            hash,
            descriptor.supports_steady ? "steady" : "not_steady");
        hash_text(
            hash,
            descriptor.supports_transient
                ? "transient"
                : "not_transient");
    }
    for (const auto& descriptor : properties.descriptors()) {
        hash_text(hash, descriptor.backend);
        hash_text(hash, descriptor.implementation_name);
        hash_text(hash, descriptor.implementation_version);
        for (const auto& substance :
             descriptor.supported_substances) {
            hash_text(hash, substance);
        }
        for (const auto capability : descriptor.capabilities) {
            hash_text(
                hash,
                std::to_string(static_cast<int>(capability)));
        }
    }
    hash_text(hash, "thermox.connector.fluid/v1:m_dot,p,h");
    hash_text(hash, "thermox.connector.heat/v1:Q_dot,T");
    hash_text(hash, "thermox.connector.shaft/v1:W_dot,omega");
    hash_text(hash, "thermox.connector.signal/v1:value");
    hash_text(hash, "thermox.connector.control/v1:value");
    std::ostringstream out;
    out << "fnv1a64:" << std::hex << std::setw(16)
        << std::setfill('0') << hash;
    return out.str();
}

}  // namespace

SimulationRuntime::SimulationRuntime(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

SimulationRuntime::~SimulationRuntime() = default;

std::shared_ptr<const SimulationRuntime> make_simulation_runtime(
    platform::ComponentRegistry components,
    physics::PropertyPackageRegistry properties) {
    return detail::NativeRuntimeFactory::create(
        std::move(components), std::move(properties));
}

std::shared_ptr<const SimulationRuntime>
detail::NativeRuntimeFactory::create(
    platform::ComponentRegistry components,
    physics::PropertyPackageRegistry properties) {
    auto impl = std::make_unique<SimulationRuntime::Impl>();
    impl->fingerprint =
        catalog_fingerprint(components, properties);
    impl->components = std::move(components);
    impl->properties = std::move(properties);
    return std::shared_ptr<const SimulationRuntime>(
        new SimulationRuntime(std::move(impl)));
}

std::shared_ptr<const SimulationRuntime>
make_default_simulation_runtime() {
    return make_simulation_runtime(
        platform::make_default_component_registry(),
        physics::make_default_property_package_registry());
}

}  // namespace thermox::service
