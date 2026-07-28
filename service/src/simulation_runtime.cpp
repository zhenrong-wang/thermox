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
    const physics::PropertyPackageRegistry& properties,
    const platform::PerformanceMapRegistry& performance_maps) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto& descriptor : components.descriptors()) {
        hash_text(hash, descriptor.kind);
        hash_text(hash, descriptor.version);
        for (const auto& port : descriptor.ports) {
            hash_text(hash, port.name);
            hash_text(hash, port.domain);
            hash_text(hash, port.direction);
            hash_text(
                hash,
                std::to_string(port.maximum_connections));
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
        for (const auto& artifact : descriptor.artifacts) {
            hash_text(hash, artifact.role);
            hash_text(hash, artifact.artifact_type);
            hash_text(
                hash,
                artifact.required ? "required" : "optional");
        }
        for (const auto capability :
             descriptor.required_property_capabilities) {
            hash_text(
                hash,
                std::to_string(static_cast<int>(capability)));
        }
        for (const auto& variable :
             descriptor.transient_variables) {
            hash_text(hash, variable.port_name);
            hash_text(hash, variable.variable_name);
            hash_text(
                hash,
                std::to_string(
                    static_cast<int>(variable.kind)));
            hash_number(hash, variable.derivative_scale);
        }
        for (const auto& variable :
             descriptor.internal_variables) {
            hash_text(hash, variable.name);
            hash_text(hash, variable.dimension);
            hash_text(
                hash,
                std::to_string(
                    static_cast<int>(variable.kind)));
            hash_number(hash, variable.initial_value);
            hash_number(hash, variable.state_scale);
            hash_number(hash, variable.initial_derivative);
            hash_number(hash, variable.derivative_scale);
            hash_number(hash, variable.lower_bound);
            hash_number(hash, variable.upper_bound);
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
    for (const auto& id : performance_maps.ids()) {
        const auto artifact =
            performance_maps.require_artifact(id);
        hash_text(hash, artifact->id);
        hash_text(hash, artifact->schema_version);
        hash_text(hash, artifact->revision);
        hash_text(hash, artifact->checksum_sha256);
    }
    hash_text(hash, "thermox.connector.fluid/v1:m_dot,p,h");
    hash_text(hash, "thermox.connector.heat/v1:Q_dot,T");
    hash_text(hash, "thermox.connector.shaft/v1:W_dot,omega");
    hash_text(hash, "thermox.connector.electrical/v1:P,frequency");
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
    physics::PropertyPackageRegistry properties,
    platform::PerformanceMapRegistry performance_maps) {
    return detail::NativeRuntimeFactory::create(
        std::move(components), std::move(properties),
        std::move(performance_maps));
}

std::shared_ptr<const SimulationRuntime>
detail::NativeRuntimeFactory::create(
    platform::ComponentRegistry components,
    physics::PropertyPackageRegistry properties,
    platform::PerformanceMapRegistry performance_maps) {
    auto impl = std::make_unique<SimulationRuntime::Impl>();
    impl->fingerprint =
        catalog_fingerprint(
            components, properties, performance_maps);
    impl->components = std::move(components);
    impl->properties = std::move(properties);
    impl->performance_maps = std::move(performance_maps);
    return std::shared_ptr<const SimulationRuntime>(
        new SimulationRuntime(std::move(impl)));
}

std::shared_ptr<const SimulationRuntime>
make_default_simulation_runtime() {
    return make_simulation_runtime(
        platform::make_default_component_registry(),
        physics::make_default_property_package_registry(),
        {});
}

}  // namespace thermox::service
