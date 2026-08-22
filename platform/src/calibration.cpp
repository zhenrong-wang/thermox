#include "thermox/platform/calibration.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string_view>

namespace thermox::platform {

namespace {

using DimensionMap = std::map<std::string, std::string>;

const DimensionMap& primary_dimensions(const std::string& domain) {
    static const DimensionMap fluid{
        {"m_dot", "mass_flow"},
        {"p", "pressure"},
        {"h", "specific_enthalpy"},
    };
    static const DimensionMap material{
        {"p", "pressure"},
        {"h", "specific_enthalpy"},
    };
    static const DimensionMap heat{
        {"Q_dot", "power"},
        {"T", "temperature"},
    };
    static const DimensionMap shaft{
        {"W_dot", "power"},
        {"omega", "angular_speed"},
    };
    static const DimensionMap electrical{
        {"P", "power"},
        {"frequency", "frequency"},
    };
    static const DimensionMap scalar{
        {"value", "dimensionless"},
    };
    if (domain == "fluid") return fluid;
    if (domain == "material") return material;
    if (domain == "heat") return heat;
    if (domain == "shaft") return shaft;
    if (domain == "electrical") return electrical;
    if (domain == "signal" || domain == "control") return scalar;
    throw std::invalid_argument(
        "unsupported port domain in calibration observation: " +
        domain);
}

const DimensionMap& thermodynamic_derived_dimensions() {
    static const DimensionMap dimensions{
        {"T", "temperature"},
        {"vapor_quality", "dimensionless"},
    };
    return dimensions;
}

const ComponentDefinition& require_component(
    const ModelDocument& document,
    std::string_view id,
    const CalibrationObservationDefinition& observation) {
    const auto component = std::find_if(
        document.components.begin(), document.components.end(),
        [&](const auto& candidate) {
            return candidate.id == id;
        });
    if (component == document.components.end()) {
        throw std::invalid_argument(
            "calibration observation '" + observation.id +
            "' references unknown result component: " +
            std::string(id));
    }
    return *component;
}

const PortModelDescriptor& require_port(
    const ComponentModelDescriptor& descriptor,
    std::string_view name,
    const CalibrationObservationDefinition& observation) {
    const auto port = std::find_if(
        descriptor.ports.begin(), descriptor.ports.end(),
        [&](const auto& candidate) {
            return candidate.name == name;
        });
    if (port == descriptor.ports.end()) {
        throw std::invalid_argument(
            "calibration observation '" + observation.id +
            "' references unknown result port: " +
            observation.target);
    }
    return *port;
}

const MaterialDefinition& require_port_material(
    const ModelDocument& document,
    const ComponentDefinition& component,
    const std::string& port_name,
    const CalibrationObservationDefinition& observation) {
    const auto binding =
        component.material_bindings.find(port_name);
    if (binding == component.material_bindings.end()) {
        throw std::invalid_argument(
            "calibration observation '" + observation.id +
            "' material port has no material binding: " +
            observation.target);
    }
    const auto material = std::find_if(
        document.materials.begin(), document.materials.end(),
        [&](const auto& candidate) {
            return candidate.id == binding->second;
        });
    if (material == document.materials.end()) {
        throw std::invalid_argument(
            "calibration observation '" + observation.id +
            "' material port references an unknown material: " +
            observation.target);
    }
    return *material;
}

std::string observation_dimension(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry_registry,
    const CalibrationObservationDefinition& observation) {
    const std::string_view target{observation.target};
    const auto first = target.find('.');
    const auto second =
        first == std::string_view::npos
            ? std::string_view::npos
            : target.find('.', first + 1);
    if (first == std::string_view::npos || first == 0 ||
        first + 1 >= target.size()) {
        throw std::invalid_argument(
            "calibration observation '" + observation.id +
            "' target must use component.port.value or "
            "component.internal_value: " + observation.target);
    }
    const auto& component = require_component(
        document, target.substr(0, first), observation);
    const auto& descriptor =
        registry.require_model(component.kind).descriptor();

    if (second == std::string_view::npos) {
        const auto name = target.substr(first + 1);
        const auto internal = std::find_if(
            descriptor.internal_variables.begin(),
            descriptor.internal_variables.end(),
            [&](const auto& candidate) {
                return candidate.name == name;
            });
        if (internal == descriptor.internal_variables.end()) {
            throw std::invalid_argument(
                "calibration observation '" + observation.id +
                "' references unknown internal result: " +
                observation.target);
        }
        return internal->dimension;
    }
    if (target.find('.', second + 1) != std::string_view::npos ||
        second + 1 >= target.size()) {
        throw std::invalid_argument(
            "calibration observation '" + observation.id +
            "' target must use component.port.value: " +
            observation.target);
    }
    const std::string port_name{
        target.substr(first + 1, second - first - 1)};
    const auto& port = require_port(
        descriptor, port_name, observation);
    const std::string value_name{target.substr(second + 1)};
    const auto& primary = primary_dimensions(port.domain);
    if (const auto value = primary.find(value_name);
        value != primary.end()) {
        return value->second;
    }
    if (port.domain == "material" &&
        value_name.starts_with("m_dot[") &&
        value_name.ends_with("]")) {
        const auto& material = require_port_material(
            document, component, port_name, observation);
        const std::string species = value_name.substr(
            6, value_name.size() - 7);
        if (std::find(
                material.species.begin(),
                material.species.end(),
                species) == material.species.end()) {
            throw std::invalid_argument(
                "calibration observation '" + observation.id +
                "' references unknown material species result: " +
                observation.target);
        }
        return "mass_flow";
    }
    if (port.domain == "fluid" || port.domain == "material") {
        const auto& derived =
            thermodynamic_derived_dimensions();
        if (const auto value = derived.find(value_name);
            value != derived.end()) {
            if (port.domain == "material") {
                const auto& material = require_port_material(
                    document, component, port_name,
                    observation);
                if (!thermochemistry_registry.contains(
                        material.backend)) {
                    throw std::invalid_argument(
                        "calibration observation '" +
                        observation.id +
                        "' requires an unregistered material "
                        "property backend: " +
                        material.backend);
                }
            }
            return value->second;
        }
        if (port.domain == "material" &&
            value_name == "mean_molecular_weight") {
            const auto& material = require_port_material(
                document, component, port_name, observation);
            if (!thermochemistry_registry.contains(
                    material.backend)) {
                throw std::invalid_argument(
                    "calibration observation '" +
                    observation.id +
                    "' requires an unregistered material "
                    "property backend: " +
                    material.backend);
            }
            return "molar_mass";
        }
    }
    throw std::invalid_argument(
        "calibration observation '" + observation.id +
        "' references unknown result value: " +
        observation.target);
}

}  // namespace

void validate_calibration_observation_contracts(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry_registry) {
    for (const auto& calibration : document.calibrations) {
        for (const auto& observation :
             calibration.observations) {
            const auto dimension = observation_dimension(
                document, registry, thermochemistry_registry,
                observation);
            if (dimension != observation.measured.dimension) {
                throw std::invalid_argument(
                    "calibration observation '" +
                    observation.id +
                    "' measured dimension '" +
                    observation.measured.dimension +
                    "' does not match result dimension '" +
                    dimension + "'");
            }
        }
    }
}

void validate_calibration_initial_state_contracts(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::PropertyPackageRegistry& property_registry,
    const EngineeringArtifactRegistry& artifact_registry,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry_registry) {
    constexpr std::string_view case_prefix{"cases."};
    constexpr std::string_view marker{".initial_guesses."};
    std::map<std::string, CompiledTransientModelGraph> compiled_cases;

    for (const auto& calibration : document.calibrations) {
        for (const auto& parameter : calibration.parameters) {
            for (const auto& target : parameter.targets) {
                const std::string_view path{target};
                if (!path.starts_with(case_prefix)) continue;
                const auto split = path.find(
                    marker, case_prefix.size());
                if (split == std::string_view::npos) continue;

                const std::string case_id{path.substr(
                    case_prefix.size(),
                    split - case_prefix.size())};
                const std::string variable{
                    path.substr(split + marker.size())};
                auto graph = compiled_cases.find(case_id);
                if (graph == compiled_cases.end()) {
                    graph = compiled_cases.emplace(
                        case_id,
                        compile_transient_model_graph(
                            document, registry, property_registry,
                            artifact_registry,
                            thermochemistry_registry, case_id))
                                .first;
                }
                const auto& problem = graph->second.problem;
                const auto found = std::find(
                    problem.variable_names.begin(),
                    problem.variable_names.end(), variable);
                if (found == problem.variable_names.end()) {
                    throw std::invalid_argument(
                        "calibration initial-state target references "
                        "unknown transient variable: " + target);
                }
                const auto index = static_cast<std::size_t>(
                    std::distance(problem.variable_names.begin(), found));
                const auto kind = problem.variable_kinds.empty()
                    ? DaeVariableKind::differential
                    : problem.variable_kinds.at(index);
                if (kind != DaeVariableKind::differential) {
                    throw std::invalid_argument(
                        "calibration initial-state target must reference "
                        "a differential state, not an algebraic variable: " +
                        target);
                }
            }
        }
    }
}

ScalarValue& require_calibration_parameter_target(
    ModelDocument& document,
    const std::string& target) {
    constexpr std::string_view marker{".parameters."};
    const auto resolve = [&](std::string_view prefix,
                             auto& owners) -> ScalarValue& {
        const std::string_view path{target};
        const auto split = path.find(marker, prefix.size());
        if (!path.starts_with(prefix) ||
            split == std::string_view::npos) {
            throw std::invalid_argument(
                "invalid calibration parameter target: " +
                target);
        }
        const std::string owner_id{
            path.substr(prefix.size(), split - prefix.size())};
        const std::string parameter_name{
            path.substr(split + marker.size())};
        for (auto& owner : owners) {
            if (owner.id == owner_id) {
                const auto parameter =
                    owner.parameters.find(parameter_name);
                if (parameter != owner.parameters.end()) {
                    return parameter->second;
                }
            }
        }
        throw std::invalid_argument(
            "unresolved calibration parameter target: " +
            target);
    };
    if (std::string_view{target}.starts_with("components.")) {
        return resolve("components.", document.components);
    }
    if (std::string_view{target}.starts_with("connections.")) {
        return resolve("connections.", document.connections);
    }
    constexpr std::string_view case_prefix{"cases."};
    constexpr std::string_view fixed_value_marker{".fixed_values."};
    constexpr std::string_view initial_guess_marker{".initial_guesses."};
    constexpr std::string_view override_marker{".parameter_overrides."};
    const std::string_view path{target};
    if (path.starts_with(case_prefix)) {
        auto split = path.find(
            fixed_value_marker, case_prefix.size());
        auto value_marker = fixed_value_marker;
        enum class CaseValueKind {
            fixed_value,
            initial_guess,
            parameter_override,
        };
        auto value_kind = CaseValueKind::fixed_value;
        if (split == std::string_view::npos) {
            split = path.find(
                initial_guess_marker, case_prefix.size());
            value_marker = initial_guess_marker;
            value_kind = CaseValueKind::initial_guess;
        }
        if (split == std::string_view::npos) {
            split = path.find(override_marker, case_prefix.size());
            value_marker = override_marker;
            value_kind = CaseValueKind::parameter_override;
        }
        if (split == std::string_view::npos ||
            split == case_prefix.size() ||
            split + value_marker.size() >= path.size()) {
            throw std::invalid_argument(
                "invalid case calibration parameter target: " +
                target);
        }
        const std::string case_id{
            path.substr(
                case_prefix.size(), split - case_prefix.size())};
        const std::string variable{
            path.substr(split + value_marker.size())};
        for (auto& operating_case : document.cases) {
            if (operating_case.id != case_id) continue;
            auto& values = value_kind == CaseValueKind::fixed_value
                ? operating_case.fixed_values
                : value_kind == CaseValueKind::initial_guess
                    ? operating_case.initial_guesses
                    : operating_case.parameter_overrides;
            const auto value = values.find(variable);
            if (value != values.end()) {
                return value->second;
            }
        }
        throw std::invalid_argument(
            "unresolved case calibration parameter target: " +
            target);
    }
    throw std::invalid_argument(
        "invalid calibration parameter target: " + target);
}

const ScalarValue& require_calibration_parameter_target(
    const ModelDocument& document,
    const std::string& target) {
    return require_calibration_parameter_target(
        const_cast<ModelDocument&>(document), target);
}

}  // namespace thermox::platform
