#include "thermox/service/native_extension_sdk.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {

class ConformancePropertyPackage final
    : public thermox::physics::PropertyPackage {
public:
    std::string_view name() const noexcept override {
        return "sdk-conformance-property";
    }

    std::string_view version() const noexcept override {
        return "1.0.0";
    }

    thermox::physics::PropertyLimits limits()
        const noexcept override {
        return {1.0, 1.0e8, 1.0, 5000.0};
    }

    bool supports(
        thermox::physics::PropertyCapability)
        const noexcept override {
        return false;
    }

    thermox::physics::PropertyResult state_pt(
        double, double) const override {
        return unsupported();
    }

    thermox::physics::PropertyResult state_ph(
        double, double) const override {
        return unsupported();
    }

    thermox::physics::PropertyResult state_ps(
        double, double) const override {
        return unsupported();
    }

    thermox::physics::SaturationResult saturation_p(
        double) const override {
        return {
            {},
            {},
            thermox::physics::PropertyStatus::unsupported,
            "not used by the conformance model",
        };
    }

private:
    static thermox::physics::PropertyResult unsupported() {
        return {
            {},
            thermox::physics::PropertyStatus::unsupported,
            "not used by the conformance model",
        };
    }
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main() {
    using namespace thermox;

    auto components =
        platform::make_default_component_registry();
    auto properties =
        physics::make_default_property_package_registry();
    platform::PerformanceMapRegistry performance_maps;
    physics::ThermochemistryPackageRegistry thermochemistry;

    service::NativeExtensionPackage extension;
    extension.package_id = "example.thermal_bus";
    extension.package_version = "1.0.0";
    extension.register_components =
        [](platform::ComponentRegistry& registry) {
            registry.register_connector_domain({
                "thermal_bus",
                "example.connector.thermal_bus/v1",
                "thermal_bus_link",
                {
                    {"potential", 300.0, 100.0,
                     "temperature", false},
                    {"flow", 0.0, 1.0e6, "power", false},
                },
            });
            platform::ComponentModelDescriptor descriptor;
            descriptor.kind = "example.thermal_bus.bridge";
            descriptor.version = "1.0.0";
            descriptor.ports = {
                {"emit", "thermal_bus", "out"},
                {"receive", "thermal_bus", "in"},
            };
            registry.register_model(
                std::make_shared<
                    platform::MetadataComponentModel>(
                    std::move(descriptor)));
        };
    extension.register_properties =
        [](physics::PropertyPackageRegistry& registry) {
            registry.register_backend(
                {
                    "sdk_conformance",
                    "sdk-conformance-property",
                    "1.0.0",
                    {"ConformanceFluid"},
                    {},
                },
                [](std::string_view) {
                    return std::make_shared<
                        const ConformancePropertyPackage>();
                });
        };
    service::apply_native_extension(
        extension,
        components,
        properties,
        performance_maps,
        thermochemistry);

    auto runtime = service::make_simulation_runtime(
        std::move(components),
        std::move(properties),
        std::move(performance_maps),
        std::move(thermochemistry));
    service::SimulationService simulation{runtime};
    const auto catalog = simulation.get_catalog();
    require(catalog.succeeded(), "catalog discovery failed");
    require(
        std::any_of(
            catalog.native_extensions.begin(),
            catalog.native_extensions.end(),
            [](const auto& registered) {
                return registered.package_id ==
                        "example.thermal_bus" &&
                    registered.package_version == "1.0.0";
            }),
        "native extension identity is absent from catalog");
    require(
        std::any_of(
            catalog.connector_domains.begin(),
            catalog.connector_domains.end(),
            [](const auto& domain) {
                return domain.domain == "thermal_bus" &&
                    domain.connection_kind ==
                        "thermal_bus_link";
            }),
        "custom connector is absent from catalog");
    require(
        std::any_of(
            catalog.property_backends.begin(),
            catalog.property_backends.end(),
            [](const auto& backend) {
                return backend.backend == "sdk_conformance";
            }),
        "custom property backend is absent from catalog");

    const std::string model = R"json({
      "schema_version": "thermox.model/v2",
      "model": {
        "id": "sdk_conformance",
        "media": [],
        "components": [{
          "id": "bridge",
          "kind": "example.thermal_bus.bridge",
          "version": "1.0.0"
        }],
        "connections": [{
          "id": "loop",
          "from": "bridge.emit",
          "to": "bridge.receive",
          "kind": "thermal_bus_link",
          "contract_version":
            "example.connector.thermal_bus/v1"
        }]
      },
      "cases": [{
        "id": "design",
        "mode": "steady_state_design",
        "fixed_values": {
          "bridge.emit.potential": {
            "value": 350.0,
            "unit": "K"
          },
          "bridge.emit.flow": {
            "value": 2.0,
            "unit": "MW"
          }
        }
      }]
    })json";

    service::ValidateModelRequest validation_request;
    validation_request.model_json = model;
    validation_request.case_id = "design";
    const auto validation =
        simulation.validate_model(validation_request);
    require(
        validation.succeeded() &&
            validation.compilation.compiled,
        "custom model did not compile");

    service::SteadySimulationRequest run;
    run.model_json = model;
    run.case_id = "design";
    require(
        simulation.run_steady(run).succeeded(),
        "custom model did not solve");
}
