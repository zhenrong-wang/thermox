#include "thermox/service/native_runtime.hpp"

#include "runtime_internal.hpp"

#ifdef THERMOX_HAS_CANTERA_BACKEND
#include "thermox/physics/cantera_thermochemistry.hpp"
#endif

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace thermox::service {

namespace {

physics::ThermochemistryPackageRegistry
make_default_thermochemistry_registry() {
    physics::ThermochemistryPackageRegistry registry;
#ifdef THERMOX_HAS_CANTERA_BACKEND
    physics::register_cantera_thermochemistry_backend(registry);
#endif
    return registry;
}

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
    const platform::EngineeringArtifactRegistry& engineering_artifacts,
    const platform::CorrelationTemplateRegistry& correlation_templates,
    const platform::RegimeMapTemplateRegistry& regime_map_templates,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry,
    const platform::UnitRegistry& units) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto& descriptor : components.descriptors()) {
        hash_text(hash, descriptor.kind);
        hash_text(hash, descriptor.version);
        hash_text(hash, descriptor.template_kind);
        hash_text(hash, descriptor.display_name);
        hash_text(hash, descriptor.category);
        hash_text(hash, descriptor.model_name);
        hash_text(
            hash,
            components.require_model(descriptor.kind)
                .implementation_fingerprint());
        hash_text(hash, descriptor.system_boundary_role);
        for (const auto& port : descriptor.ports) {
            hash_text(hash, port.name);
            hash_text(hash, port.domain);
            hash_text(hash, port.direction);
            hash_text(
                hash,
                std::to_string(port.maximum_connections));
        }
        for (const auto& group : descriptor.port_groups) {
            hash_text(hash, group.name);
            hash_text(hash, group.port_name_prefix);
            hash_text(hash, group.domain);
            hash_text(hash, group.direction);
            hash_text(hash, std::to_string(group.minimum_count));
            hash_text(hash, std::to_string(group.maximum_count));
            hash_text(
                hash, std::to_string(group.maximum_connections));
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
        for (const auto capability :
             descriptor.required_thermochemistry_capabilities) {
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
        for (const auto& mode : descriptor.supported_modes) {
            hash_text(hash, mode);
        }
        hash_text(hash, descriptor.default_mode);
        for (const auto& event : descriptor.events) {
            hash_text(hash, event.name);
            hash_text(hash, event.dimension);
            hash_text(hash, event.direction);
            hash_text(hash, event.terminal ? "terminal" : "nonterminal");
            hash_text(hash, std::to_string(event.priority));
            hash_number(hash, event.hysteresis_si);
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
    for (const auto& id : engineering_artifacts.ids()) {
        const auto artifact =
            engineering_artifacts.require_artifact(id);
        hash_text(hash, artifact->id);
        hash_text(hash, artifact->artifact_type());
        hash_text(hash, artifact->schema_version);
        hash_text(hash, artifact->revision);
        hash_text(hash, artifact->checksum_sha256);
    }
    for (const auto& descriptor : correlation_templates.descriptors()) {
        hash_text(hash, descriptor.id);
        hash_text(hash, descriptor.version);
        hash_text(hash, descriptor.display_name);
        hash_text(hash, descriptor.category);
        hash_text(hash, descriptor.reference);
        for (const auto& input : descriptor.inputs) {
            hash_text(hash, input.name);
            hash_text(hash, input.dimension);
        }
        hash_text(hash, descriptor.output.name);
        hash_text(hash, descriptor.output.dimension);
        for (const auto& coefficient : descriptor.coefficients) {
            hash_text(hash, coefficient.name);
            hash_text(hash, coefficient.dimension);
            hash_number(hash, coefficient.default_value.has_value());
            if (coefficient.default_value) {
                hash_number(hash, *coefficient.default_value);
            }
            hash_number(hash, coefficient.lower_bound);
            hash_number(hash, coefficient.upper_bound);
            hash_number(hash, coefficient.lower_inclusive);
            hash_number(hash, coefficient.upper_inclusive);
        }
        hash_text(hash, descriptor.expression);
        hash_text(hash, descriptor.regime);
        for (const auto& range : descriptor.applicability) {
            hash_text(hash, range.input);
            hash_number(hash, range.minimum.has_value());
            if (range.minimum) hash_number(hash, *range.minimum);
            hash_number(hash, range.maximum.has_value());
            if (range.maximum) hash_number(hash, *range.maximum);
            hash_number(hash, range.minimum_inclusive);
            hash_number(hash, range.maximum_inclusive);
        }
    }
    for (const auto& descriptor :
         correlation_templates.family_descriptors()) {
        hash_text(hash, descriptor.id);
        hash_text(hash, descriptor.version);
        hash_text(hash, descriptor.display_name);
        hash_text(hash, descriptor.category);
        hash_text(hash, descriptor.reference);
        hash_text(hash, descriptor.scope);
        for (const auto& binding : descriptor.bindings) {
            hash_text(hash, binding.template_id);
            hash_text(hash, binding.candidate_id);
            hash_number(hash, binding.priority);
            for (const auto& [name, value] : binding.coefficients) {
                hash_text(hash, name);
                hash_number(hash, value);
            }
            for (const auto& regime : binding.flow_regimes) {
                hash_text(hash, regime);
            }
            hash_number(
                hash,
                binding.fallback_for_unmapped_flow_regime);
        }
    }
    for (const auto& descriptor : regime_map_templates.descriptors()) {
        hash_text(hash, descriptor.id);
        hash_text(hash, descriptor.version);
        hash_text(hash, descriptor.display_name);
        hash_text(hash, descriptor.category);
        hash_text(hash, descriptor.reference);
        hash_text(hash, descriptor.scope);
        for (const auto& input : descriptor.inputs) {
            hash_text(hash, input.name);
            hash_text(hash, input.dimension);
        }
        for (const auto& region : descriptor.regions) {
            hash_text(hash, region.id);
            hash_text(hash, region.regime);
            hash_text(hash, std::to_string(region.priority));
            for (const auto& branch : region.branches) {
                hash_text(hash, branch.id);
                hash_text(hash, std::to_string(branch.priority));
                for (const auto& criterion : branch.criteria) {
                    hash_text(hash, criterion.expression);
                    hash_text(hash, criterion.dimension);
                    hash_number(hash, criterion.minimum.has_value());
                    if (criterion.minimum) {
                        hash_number(hash, *criterion.minimum);
                    }
                    hash_number(hash, criterion.maximum.has_value());
                    if (criterion.maximum) {
                        hash_number(hash, *criterion.maximum);
                    }
                    hash_number(hash, criterion.minimum_inclusive);
                    hash_number(hash, criterion.maximum_inclusive);
                }
            }
        }
    }
    for (const auto& descriptor :
         thermochemistry.descriptors()) {
        hash_text(hash, descriptor.backend);
        hash_text(hash, descriptor.implementation_name);
        hash_text(hash, descriptor.implementation_version);
        for (const auto capability : descriptor.capabilities) {
            hash_text(
                hash,
                std::to_string(static_cast<int>(capability)));
        }
    }
    for (const auto& descriptor :
         components.connector_domain_descriptors()) {
        hash_text(hash, descriptor.domain);
        hash_text(hash, descriptor.contract_version);
        hash_text(hash, descriptor.connection_kind);
        for (const auto& variable : descriptor.variables) {
            hash_text(hash, variable.name);
            hash_number(hash, variable.initial_value);
            hash_number(hash, variable.scale);
            hash_text(hash, variable.dimension);
            hash_text(
                hash,
                variable.expand_species
                    ? "expand_species"
                    : "fixed_variable");
        }
    }
    for (const auto& extension :
         components.runtime_extension_descriptors()) {
        hash_text(hash, extension.package_id);
        hash_text(hash, extension.package_version);
    }
    for (const auto& descriptor : units.descriptors()) {
        hash_text(hash, descriptor.dimension);
        hash_text(hash, descriptor.canonical_unit);
        hash_text(hash, descriptor.si_display.symbol);
        hash_number(hash, descriptor.si_display.scale_from_si);
        hash_number(hash, descriptor.si_display.offset_from_si);
        hash_text(hash, descriptor.engineering_display.symbol);
        hash_number(
            hash, descriptor.engineering_display.scale_from_si);
        hash_number(
            hash, descriptor.engineering_display.offset_from_si);
        for (const auto& unit : descriptor.accepted_units) {
            hash_text(hash, unit.symbol);
            hash_number(hash, unit.scale_to_si);
            hash_number(hash, unit.offset_to_si);
            for (const auto& alias : unit.aliases) {
                hash_text(hash, alias);
            }
        }
    }
    std::ostringstream out;
    out << "fnv1a64:" << std::hex << std::setw(16)
        << std::setfill('0') << hash;
    return out.str();
}

}  // namespace

SimulationRuntime::SimulationRuntime(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

SimulationRuntime::~SimulationRuntime() = default;

void apply_native_extension(
    const NativeExtensionPackage& extension,
    platform::ComponentRegistry& components,
    physics::PropertyPackageRegistry& properties,
    platform::EngineeringArtifactRegistry& engineering_artifacts,
    platform::CorrelationTemplateRegistry& correlation_templates,
    platform::RegimeMapTemplateRegistry& regime_map_templates,
    physics::ThermochemistryPackageRegistry& thermochemistry,
    platform::UnitRegistry& units) {
    if (extension.package_id.empty() ||
        extension.package_version.empty()) {
        throw std::invalid_argument(
            "native extension package requires a non-empty ID "
            "and version");
    }
    if (!extension.register_components &&
        !extension.register_properties &&
        !extension.register_engineering_artifacts &&
        !extension.register_correlation_templates &&
        !extension.register_regime_map_templates &&
        !extension.register_thermochemistry &&
        !extension.register_units) {
        throw std::invalid_argument(
            "native extension package must register at least "
            "one capability");
    }
    if (extension.register_components) {
        extension.register_components(components);
    }
    if (extension.register_properties) {
        extension.register_properties(properties);
    }
    if (extension.register_engineering_artifacts) {
        extension.register_engineering_artifacts(
            engineering_artifacts);
    }
    if (extension.register_correlation_templates) {
        extension.register_correlation_templates(correlation_templates);
    }
    if (extension.register_regime_map_templates) {
        extension.register_regime_map_templates(regime_map_templates);
    }
    if (extension.register_thermochemistry) {
        extension.register_thermochemistry(thermochemistry);
    }
    if (extension.register_units) {
        extension.register_units(units);
    }
    components.register_runtime_extension(
        {
            extension.package_id,
            extension.package_version,
        });
}

std::shared_ptr<const SimulationRuntime> make_simulation_runtime(
    platform::ComponentRegistry components,
    physics::PropertyPackageRegistry properties,
    platform::EngineeringArtifactRegistry engineering_artifacts,
    platform::CorrelationTemplateRegistry correlation_templates,
    platform::RegimeMapTemplateRegistry regime_map_templates,
    physics::ThermochemistryPackageRegistry thermochemistry,
    platform::UnitRegistry units) {
    return detail::NativeRuntimeFactory::create(
        std::move(components), std::move(properties),
        std::move(engineering_artifacts),
        std::move(correlation_templates),
        std::move(regime_map_templates),
        std::move(thermochemistry), std::move(units));
}

std::shared_ptr<const SimulationRuntime>
detail::NativeRuntimeFactory::create(
    platform::ComponentRegistry components,
    physics::PropertyPackageRegistry properties,
    platform::EngineeringArtifactRegistry engineering_artifacts,
    platform::CorrelationTemplateRegistry correlation_templates,
    platform::RegimeMapTemplateRegistry regime_map_templates,
    physics::ThermochemistryPackageRegistry thermochemistry,
    platform::UnitRegistry units) {
    auto impl = std::make_unique<SimulationRuntime::Impl>();
    impl->fingerprint =
        catalog_fingerprint(
            components, properties, engineering_artifacts,
            correlation_templates, regime_map_templates,
            thermochemistry, units);
    impl->components = std::move(components);
    impl->properties = std::move(properties);
    impl->engineering_artifacts =
        std::move(engineering_artifacts);
    impl->correlation_templates = std::move(correlation_templates);
    impl->regime_map_templates = std::move(regime_map_templates);
    impl->thermochemistry = std::move(thermochemistry);
    impl->units = std::move(units);
    return std::shared_ptr<const SimulationRuntime>(
        new SimulationRuntime(std::move(impl)));
}

std::shared_ptr<const SimulationRuntime>
detail::NativeRuntimeFactory::overlay(
    const std::shared_ptr<const SimulationRuntime>& base,
    std::vector<platform::ExpressionComponentDefinition>
        expression_components) {
    if (!base) {
        throw std::invalid_argument(
            "runtime overlay requires a base runtime");
    }
    if (expression_components.empty()) return base;
    auto components = base->impl_->components;
    for (auto& definition : expression_components) {
        platform::register_expression_component(
            components, std::move(definition));
    }
    return create(
        std::move(components),
        base->impl_->properties,
        base->impl_->engineering_artifacts,
        base->impl_->correlation_templates,
        base->impl_->regime_map_templates,
        base->impl_->thermochemistry,
        base->impl_->units);
}

std::shared_ptr<const SimulationRuntime>
make_default_simulation_runtime() {
    return make_simulation_runtime(
        platform::make_default_component_registry(),
        physics::make_default_property_package_registry(),
        {}, platform::make_default_correlation_template_registry(),
        platform::make_default_regime_map_template_registry(),
        make_default_thermochemistry_registry(),
        platform::make_default_unit_registry());
}

}  // namespace thermox::service
