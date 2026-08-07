#include "thermox/platform/regime_map.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace thermox::platform {
namespace {

bool valid_identifier(std::string_view value) {
    if (value.empty() ||
        (!std::isalpha(static_cast<unsigned char>(value.front())) &&
         value.front() != '_')) {
        return false;
    }
    return std::all_of(
        value.begin(), value.end(), [](unsigned char character) {
            return std::isalnum(character) != 0 || character == '_';
        });
}

bool inside(
    double value,
    const RegimeMapCriterion& criterion) {
    if (criterion.minimum &&
        (value < *criterion.minimum ||
         (value == *criterion.minimum &&
          !criterion.minimum_inclusive))) {
        return false;
    }
    if (criterion.maximum &&
        (value > *criterion.maximum ||
         (value == *criterion.maximum &&
          !criterion.maximum_inclusive))) {
        return false;
    }
    return true;
}

}  // namespace

void RegimeMapTemplateRegistry::register_template(
    RegimeMapTemplateDescriptor descriptor) {
    if (!valid_identifier(descriptor.id) || descriptor.version.empty() ||
        descriptor.display_name.empty() || descriptor.category.empty() ||
        descriptor.reference.empty() || descriptor.scope.empty()) {
        throw std::invalid_argument(
            "regime-map template has incomplete identity, reference, or scope metadata: " +
            descriptor.id);
    }
    RegimeMapArtifact validation{
        descriptor.id, regime_map_artifact_schema_v1, descriptor.version,
        std::string(64, '0'), descriptor.inputs, descriptor.regions};
    validation.validate();
    const auto id = descriptor.id;
    if (!templates_.emplace(id, std::move(descriptor)).second) {
        throw std::invalid_argument(
            "duplicate regime-map template id: " + id);
    }
}

const RegimeMapTemplateDescriptor&
RegimeMapTemplateRegistry::require_template(
    const std::string& id) const {
    const auto found = templates_.find(id);
    if (found == templates_.end()) {
        throw std::invalid_argument(
            "unknown regime-map template: " + id);
    }
    return found->second;
}

std::vector<RegimeMapTemplateDescriptor>
RegimeMapTemplateRegistry::descriptors() const {
    std::vector<RegimeMapTemplateDescriptor> result;
    result.reserve(templates_.size());
    for (const auto& [_, descriptor] : templates_) {
        result.push_back(descriptor);
    }
    return result;
}

RegimeMapTemplateRegistry
make_default_regime_map_template_registry() {
    RegimeMapTemplateRegistry registry;
    const std::string viscosity_number =
        "liquid_viscosity / sqrt(liquid_density * surface_tension * "
        "sqrt(surface_tension / (gravity * "
        "(liquid_density - vapor_density))))";
    const std::string normalized_vapor_velocity =
        "vapor_superficial_velocity / (pow(surface_tension * gravity * "
        "(liquid_density - vapor_density) / "
        "(vapor_density * vapor_density), 0.25) * pow((" +
        viscosity_number + "), -0.2))";
    const auto common_criteria = [&]() {
        return std::vector<RegimeMapCriterion>{
            {"liquid_density - vapor_density", "density", 0.0,
             std::nullopt, false, true},
            {"liquid_reynolds_number", "dimensionless", 1635.0,
             std::nullopt, false, true},
            {viscosity_number, "dimensionless", 0.0, 1.0 / 15.0,
             false, false},
        };
    };
    auto below = common_criteria();
    below.push_back({
        normalized_vapor_velocity, "dimensionless", 0.0, 1.0,
        true, false});
    auto annular = common_criteria();
    annular.push_back({
        normalized_vapor_velocity, "dimensionless", 1.0,
        std::nullopt, true, true});
    registry.register_template({
        "mishima_ishii_vertical_upflow_annular_entrainment",
        "1.0.0",
        "Mishima-Ishii vertical-upflow annular entrainment boundary",
        "Two-phase flow-pattern transition",
        "Mishima and Ishii, International Journal of Heat and Mass "
        "Transfer 27(5) (1984) 723-737, DOI "
        "10.1016/0017-9310(84)90142-X",
        "Co-current vertical upward gas-liquid flow in a round tube; "
        "classifies only the high-velocity entrainment transition to "
        "annular flow. Requires liquid viscosity number N_mu < 1/15 "
        "and liquid Reynolds number > 1635; outside that domain the "
        "map deliberately returns no applicable region.",
        {
            {"vapor_superficial_velocity", "speed"},
            {"liquid_density", "density"},
            {"vapor_density", "density"},
            {"liquid_viscosity", "dynamic_viscosity"},
            {"surface_tension", "surface_tension"},
            {"gravity", "acceleration"},
            {"liquid_reynolds_number", "dimensionless"},
        },
        {
            {"below_annular_entrainment_boundary", "pre_annular", 10,
             std::move(below)},
            {"annular_entrainment", "annular", 10,
             std::move(annular)},
        },
    });
    return registry;
}

RegimeMapArtifact instantiate_regime_map_template(
    const RegimeMapTemplateDescriptor& descriptor,
    RegimeMapArtifactIdentity identity) {
    RegimeMapArtifact artifact{
        std::move(identity.id), regime_map_artifact_schema_v1,
        std::move(identity.revision),
        std::move(identity.checksum_sha256), descriptor.inputs,
        descriptor.regions};
    artifact.validate();
    return artifact;
}

RegimeMapArtifact::RegimeMapArtifact(
    std::string artifact_id,
    std::string artifact_schema_version,
    std::string artifact_revision,
    std::string artifact_checksum_sha256,
    std::vector<RegimeMapVariable> inputs,
    std::vector<RegimeMapRegion> regions)
    : inputs_(std::move(inputs)),
      regions_(std::move(regions)) {
    id = std::move(artifact_id);
    schema_version = std::move(artifact_schema_version);
    revision = std::move(artifact_revision);
    checksum_sha256 = std::move(artifact_checksum_sha256);
    compiled_criteria_.reserve(regions_.size());
    for (const auto& region : regions_) {
        std::vector<SafeExpression> criteria;
        criteria.reserve(region.criteria.size());
        for (const auto& criterion : region.criteria) {
            criteria.push_back(
                SafeExpression::parse(criterion.expression));
        }
        compiled_criteria_.push_back(std::move(criteria));
    }
}

void RegimeMapArtifact::validate() const {
    if (schema_version != regime_map_artifact_schema_v1) {
        throw std::invalid_argument(
            "regime map '" + id +
            "' has unsupported schema version: " +
            schema_version);
    }
    constexpr std::size_t maximum_inputs = 64;
    constexpr std::size_t maximum_regions = 128;
    constexpr std::size_t maximum_criteria_per_region = 32;
    if (inputs_.empty() || inputs_.size() > maximum_inputs) {
        throw std::invalid_argument(
            "regime map '" + id +
            "' requires between 1 and 64 inputs");
    }
    std::set<std::string> declared_inputs;
    for (const auto& input : inputs_) {
        if (!valid_identifier(input.name) || input.dimension.empty() ||
            !declared_inputs.insert(input.name).second) {
            throw std::invalid_argument(
                "regime map '" + id +
                "' has an invalid or duplicate input: " + input.name);
        }
    }
    if (regions_.empty() || regions_.size() > maximum_regions) {
        throw std::invalid_argument(
            "regime map '" + id +
            "' requires between 1 and 128 regions");
    }
    std::set<std::string> region_ids;
    for (std::size_t index = 0; index < regions_.size(); ++index) {
        const auto& region = regions_[index];
        if (!valid_identifier(region.id) || region.regime.empty() ||
            !region_ids.insert(region.id).second) {
            throw std::invalid_argument(
                "regime map '" + id +
                "' has an invalid or duplicate region: " + region.id);
        }
        if (region.criteria.empty() ||
            region.criteria.size() > maximum_criteria_per_region) {
            throw std::invalid_argument(
                "regime-map region '" + region.id +
                "' requires between 1 and 32 criteria");
        }
        for (std::size_t criterion_index = 0;
             criterion_index < region.criteria.size();
             ++criterion_index) {
            const auto& criterion = region.criteria[criterion_index];
            if (criterion.expression.empty() ||
                criterion.dimension.empty() ||
                (!criterion.minimum && !criterion.maximum) ||
                (criterion.minimum &&
                 !std::isfinite(*criterion.minimum)) ||
                (criterion.maximum &&
                 !std::isfinite(*criterion.maximum)) ||
                (criterion.minimum && criterion.maximum &&
                 (*criterion.minimum > *criterion.maximum ||
                  (*criterion.minimum == *criterion.maximum &&
                   (!criterion.minimum_inclusive ||
                    !criterion.maximum_inclusive))))) {
                throw std::invalid_argument(
                    "regime-map region '" + region.id +
                    "' has an invalid criterion interval");
            }
            const auto& symbols =
                compiled_criteria_[index][criterion_index].symbols();
            if (symbols.empty() ||
                !std::all_of(
                    symbols.begin(), symbols.end(),
                    [&](const auto& symbol) {
                        return declared_inputs.contains(symbol);
                    })) {
                throw std::invalid_argument(
                    "regime-map region '" + region.id +
                    "' criterion must reference only declared inputs");
            }
        }
    }
}

const std::vector<RegimeMapVariable>&
RegimeMapArtifact::inputs() const noexcept {
    return inputs_;
}

const std::vector<RegimeMapRegion>&
RegimeMapArtifact::regions() const noexcept {
    return regions_;
}

RegimeMapEvaluation RegimeMapArtifact::classify(
    const std::map<std::string, double>& inputs) const {
    for (const auto& input : inputs_) {
        const auto found = inputs.find(input.name);
        if (found == inputs.end() || !std::isfinite(found->second)) {
            return {{}, {},
                "regime-map input is missing or non-finite: " +
                    input.name};
        }
    }
    if (inputs.size() != inputs_.size()) {
        return {{}, {}, "regime map received unknown inputs"};
    }

    std::vector<std::size_t> applicable;
    int best_priority = std::numeric_limits<int>::min();
    for (std::size_t region_index = 0;
         region_index < regions_.size(); ++region_index) {
        bool matches = true;
        const auto& region = regions_[region_index];
        for (std::size_t criterion_index = 0;
             criterion_index < region.criteria.size();
             ++criterion_index) {
            std::map<std::string, double> criterion_inputs;
            for (const auto& symbol :
                 compiled_criteria_[region_index][criterion_index]
                     .symbols()) {
                criterion_inputs.emplace(symbol, inputs.at(symbol));
            }
            const auto evaluated =
                compiled_criteria_[region_index][criterion_index]
                    .evaluate(criterion_inputs);
            if (!evaluated.error.empty()) {
                return {{}, {},
                    "regime-map region '" + region.id +
                        "' criterion failed: " + evaluated.error};
            }
            if (!inside(
                    evaluated.value,
                    region.criteria[criterion_index])) {
                matches = false;
                break;
            }
        }
        if (!matches) continue;
        if (region.priority > best_priority) {
            applicable.clear();
            best_priority = region.priority;
        }
        if (region.priority == best_priority) {
            applicable.push_back(region_index);
        }
    }
    if (applicable.empty()) {
        return {{}, {}, "no regime-map region is applicable"};
    }
    if (applicable.size() != 1U) {
        std::string error =
            "regime-map selection is ambiguous at priority " +
            std::to_string(best_priority) + ": ";
        for (std::size_t index = 0; index < applicable.size(); ++index) {
            if (index != 0U) error += ", ";
            error += regions_[applicable[index]].id;
        }
        return {{}, {}, std::move(error)};
    }
    const auto& selected = regions_[applicable.front()];
    return {selected.id, selected.regime, {}};
}

}  // namespace thermox::platform
