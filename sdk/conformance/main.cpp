#include "thermox/service/native_extension_sdk.hpp"
#include "thermox/service/thermal_feasibility.hpp"
#include "thermox/service/validation_evidence.hpp"
#include "thermox/equation_system.hpp"
#include "thermox/solver_policy_benchmark.hpp"

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
    platform::EngineeringArtifactRegistry engineering_artifacts;
    auto correlation_templates =
        platform::make_default_correlation_template_registry();
    auto regime_map_templates =
        platform::make_default_regime_map_template_registry();
    physics::ThermochemistryPackageRegistry thermochemistry;
    auto units = platform::make_default_unit_registry();

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
                    {"flow", 0.0, 1.0e6,
                     "thermal_bus_flux", false},
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
    extension.register_correlation_templates =
        [](platform::CorrelationTemplateRegistry& registry) {
            registry.register_template({
                "example_linear_response",
                "1.0.0",
                "Example linear response",
                "SDK conformance",
                "example extension-owned reference",
                {{"input", "dimensionless"}},
                {"output", "dimensionless"},
                {{"gain", "dimensionless", 1.0}},
                "gain * input",
                "extension_test",
                {{"input", 0.0, 1.0, true, true}},
            });
        };
    extension.register_units =
        [](platform::UnitRegistry& registry) {
            registry.register_dimension({
                "thermal_bus_flux",
                "bus_W",
                {"bus_W", 1.0, 0.0},
                {"bus_MW", 1.0e-6, 0.0},
                {
                    {"bus_W", {}, 1.0, 0.0},
                    {"bus_MW", {}, 1.0e6, 0.0},
                },
            });
        };
    service::apply_native_extension(
        extension,
        components,
        properties,
        engineering_artifacts,
        correlation_templates,
        regime_map_templates,
        thermochemistry,
        units);

    auto runtime = service::make_simulation_runtime(
        std::move(components),
        std::move(properties),
        std::move(engineering_artifacts),
        std::move(correlation_templates),
        std::move(regime_map_templates),
        std::move(thermochemistry),
        std::move(units));
    service::SimulationService simulation{runtime};
    const auto catalog = simulation.get_catalog();
    require(catalog.succeeded(), "catalog discovery failed");
    require(
        std::any_of(
            catalog.unit_dimensions.begin(),
            catalog.unit_dimensions.end(),
            [](const auto& dimension) {
                return dimension.dimension ==
                        "thermal_bus_flux" &&
                    dimension.engineering_display.symbol ==
                        "bus_MW";
            }),
        "extension unit dimension missing from catalog");
    require(
        std::any_of(
            catalog.correlation_templates.begin(),
            catalog.correlation_templates.end(),
            [](const auto& descriptor) {
                return descriptor.id == "example_linear_response";
            }),
        "extension correlation template missing from catalog");
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
            "unit": "bus_MW"
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

    EquationSystemBuilder policy_fixture;
    const auto fixture_x =
        policy_fixture.add_variable("fixture_x", 0.0);
    policy_fixture.add_linear_equation(
        "fixture_identity", {{fixture_x, 1.0}}, 1.0);
    const auto policy_benchmark = benchmark_structural_policies(
        policy_fixture.build());
    require(
        policy_benchmark.all_policies_equivalent_to_monolithic,
        "installed structural policy benchmark contract failed");

    const auto evidence = service::evaluate_validation_evidence(
        {{"fixture_value", "dimensionless", 1.0}},
        {{
            "fixture_identity_evidence",
            "fixture_value",
            service::ValidationEvidenceLayer::numerical,
            service::ValidationEvidenceBasis::internal_consistency,
            "dimensionless",
            1.0,
            0.0,
            0.0,
            "SDK conformance fixture",
            "Verifies the installed validation evidence contract.",
        }});
    require(
        evidence.passed && evidence.passed_count == 1U,
        "installed validation evidence contract failed");

    const auto thermal_feasibility =
        service::audit_counterflow_thermal_feasibility(
            service::GraphResult{});
    require(
        thermal_feasibility.passed &&
            thermal_feasibility.checked_count == 0U,
        "installed thermal feasibility contract failed");
}
