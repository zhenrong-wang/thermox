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
        descriptor.id, regime_map_artifact_schema_v2, descriptor.version,
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
    registry.register_template({
        "mishima_ishii_vertical_upflow_bubbly_to_slug",
        "1.0.0",
        "Mishima-Ishii vertical-upflow bubbly-to-slug boundary",
        "Two-phase flow-pattern transition",
        "Mishima and Ishii, International Journal of Heat and Mass "
        "Transfer 27(5) (1984) 723-737, DOI "
        "10.1016/0017-9310(84)90142-X",
        "Co-current vertical upward gas-liquid flow; classifies only "
        "the Mishima-Ishii bubble-coalescence boundary at void "
        "fraction alpha = 0.30. It does not distinguish dispersed "
        "bubbly, churn, or annular subregions on the high-alpha side.",
        {{"void_fraction", "dimensionless"}},
        {
            {"bubbly_side", "bubbly", 10,
             {{"bubble_coalescence", 0,
               {{"void_fraction", "dimensionless", 0.0, 0.3,
                 false, false}}}}},
            {"slug_side", "slug_side", 10,
             {{"bubble_coalescence", 0,
               {{"void_fraction", "dimensionless", 0.3, 1.0,
                 true, false}}}}},
        },
    });

    const std::string mixture_superficial_velocity =
        "(liquid_superficial_velocity + vapor_superficial_velocity)";
    const std::string distribution_parameter =
        "(1.2 - 0.2 * sqrt(vapor_density / liquid_density))";
    const std::string slug_drift_velocity =
        "(0.35 * sqrt((liquid_density - vapor_density) * gravity * "
        "diameter / liquid_density))";
    const std::string churn_velocity =
        "(0.75 * sqrt((liquid_density - vapor_density) * gravity * "
        "diameter / liquid_density) * pow((liquid_density - "
        "vapor_density) * gravity * diameter * diameter * diameter / "
        "(liquid_density * pow(liquid_viscosity / liquid_density, 2)), "
        "1.0 / 18.0))";
    const std::string slug_churn_coordinate =
        "void_fraction - (1 - 0.813 * pow(((" +
        distribution_parameter + " - 1) * " +
        mixture_superficial_velocity + " + " + slug_drift_velocity +
        ") / (" + mixture_superficial_velocity + " + " +
        churn_velocity + "), 0.75))";
    const auto slug_churn_common = []() {
        return std::vector<RegimeMapCriterion>{
            {"void_fraction", "dimensionless", 0.3, 1.0,
             true, false},
            {"liquid_density - vapor_density", "density", 0.0,
             std::nullopt, false, true},
            {"liquid_viscosity", "dynamic_viscosity", 0.0,
             std::nullopt, false, true},
            {"diameter", "length", 0.0, std::nullopt, false, true},
            {"gravity", "acceleration", 0.0, std::nullopt,
             false, true},
        };
    };
    auto slug = slug_churn_common();
    slug.push_back({
        slug_churn_coordinate, "dimensionless", std::nullopt, 0.0,
        true, false});
    auto churn = slug_churn_common();
    churn.push_back({
        slug_churn_coordinate, "dimensionless", 0.0, std::nullopt,
        true, true});
    registry.register_template({
        "mishima_ishii_vertical_upflow_slug_to_churn",
        "1.0.0",
        "Mishima-Ishii vertical-upflow slug-to-churn boundary",
        "Two-phase flow-pattern transition",
        "Mishima and Ishii, International Journal of Heat and Mass "
        "Transfer 27(5) (1984) 723-737, DOI "
        "10.1016/0017-9310(84)90142-X; assessment: Khare et al., "
        "BARC/1997/E/010 (1997)",
        "Co-current vertical upward gas-liquid flow in a round tube; "
        "implements the Mishima-Ishii mean-void-fraction transition "
        "with the round-tube distribution parameter. The cited "
        "independent assessment reports insufficient churn data to "
        "validate this boundary and poor overall slug-flow "
        "characterization; use requires case-specific validation.",
        {
            {"void_fraction", "dimensionless"},
            {"liquid_superficial_velocity", "speed"},
            {"vapor_superficial_velocity", "speed"},
            {"liquid_density", "density"},
            {"vapor_density", "density"},
            {"liquid_viscosity", "dynamic_viscosity"},
            {"diameter", "length"},
            {"gravity", "acceleration"},
        },
        {
            {"slug_side", "slug", 10,
             {{"mean_void_fraction", 0, std::move(slug)}}},
            {"churn_side", "churn", 10,
             {{"mean_void_fraction", 0, std::move(churn)}}},
        },
    });

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
             {{"wave_entrainment", 0, std::move(below)}}},
            {"annular_entrainment", "annular", 10,
             {{"wave_entrainment", 0, std::move(annular)}}},
        },
    });

    const auto composite_validity = [&]() {
        return std::vector<RegimeMapCriterion>{
            {"void_fraction", "dimensionless", 0.0, 1.0,
             false, false},
            {"liquid_superficial_velocity", "speed", 0.0,
             std::nullopt, false, true},
            {"vapor_superficial_velocity", "speed", 0.0,
             std::nullopt, false, true},
            {"liquid_density - vapor_density", "density", 0.0,
             std::nullopt, false, true},
            {"liquid_viscosity", "dynamic_viscosity", 0.0,
             std::nullopt, false, true},
            {"surface_tension", "surface_tension", 0.0,
             std::nullopt, false, true},
            {"diameter", "length", 0.0, std::nullopt, false, true},
            {"gravity", "acceleration", 0.0, std::nullopt,
             false, true},
            {"liquid_reynolds_number", "dimensionless", 1635.0,
             std::nullopt, false, true},
            {viscosity_number, "dimensionless", 0.0, 1.0 / 15.0,
             false, false},
        };
    };
    auto composite_bubbly = composite_validity();
    composite_bubbly.push_back({
        "void_fraction", "dimensionless", std::nullopt, 0.3,
        true, false});
    auto composite_slug = composite_validity();
    composite_slug.push_back({
        "void_fraction", "dimensionless", 0.3, std::nullopt,
        true, true});
    composite_slug.push_back({
        slug_churn_coordinate, "dimensionless", std::nullopt, 0.0,
        true, false});
    auto composite_churn = composite_validity();
    composite_churn.push_back({
        "void_fraction", "dimensionless", 0.3, std::nullopt,
        true, true});
    composite_churn.push_back({
        slug_churn_coordinate, "dimensionless", 0.0, std::nullopt,
        true, true});
    auto composite_entrainment = composite_validity();
    composite_entrainment.push_back({
        normalized_vapor_velocity, "dimensionless", 1.0,
        std::nullopt, true, true});
    const std::string film_reversal_coordinate =
        "vapor_superficial_velocity - sqrt((liquid_density - "
        "vapor_density) * gravity * diameter / vapor_density) * "
        "(void_fraction - 0.11)";
    auto composite_film_reversal = composite_validity();
    composite_film_reversal.push_back({
        "void_fraction - 0.11", "dimensionless", 0.0,
        std::nullopt, false, true});
    composite_film_reversal.push_back({
        slug_churn_coordinate, "dimensionless", 0.0,
        std::nullopt, true, true});
    composite_film_reversal.push_back({
        film_reversal_coordinate, "speed", 0.0,
        std::nullopt, true, true});
    registry.register_template({
        "mishima_ishii_vertical_upflow_composite",
        "1.0.0",
        "Mishima-Ishii vertical-upflow composite map",
        "Two-phase flow-pattern map",
        "Mishima and Ishii, International Journal of Heat and Mass "
        "Transfer 27(5) (1984) 723-737, DOI "
        "10.1016/0017-9310(84)90142-X; assessment: Khare et al., "
        "BARC/1997/E/010 (1997)",
        "Co-current vertical upward gas-liquid flow in a round tube. "
        "Combines the cited alpha=0.30 bubbly/slug boundary, "
        "mean-void-fraction slug/churn boundary, and alternative "
        "film-reversal or wave-entrainment annular mechanisms. The "
        "void fraction is supplied by the separately bound closure. "
        "The entire map conservatively requires N_mu < 1/15 and "
        "liquid Reynolds number > 1635. The cited assessment reports "
        "weak slug/churn validation; case-specific validation remains "
        "required.",
        {
            {"void_fraction", "dimensionless"},
            {"liquid_superficial_velocity", "speed"},
            {"vapor_superficial_velocity", "speed"},
            {"liquid_density", "density"},
            {"vapor_density", "density"},
            {"liquid_viscosity", "dynamic_viscosity"},
            {"surface_tension", "surface_tension"},
            {"diameter", "length"},
            {"gravity", "acceleration"},
            {"liquid_reynolds_number", "dimensionless"},
        },
        {
            {"bubbly", "bubbly", 10,
             {{"bubble_coalescence", 0,
               std::move(composite_bubbly)}}},
            {"slug", "slug", 10,
             {{"mean_void_fraction", 0,
               std::move(composite_slug)}}},
            {"churn", "churn", 10,
             {{"mean_void_fraction", 0,
               std::move(composite_churn)}}},
            {"annular", "annular", 20,
             {
                 {"film_reversal", 10,
                  std::move(composite_film_reversal)},
                 {"wave_entrainment", 20,
                  std::move(composite_entrainment)},
             }},
        },
    });
    return registry;
}

RegimeMapArtifact instantiate_regime_map_template(
    const RegimeMapTemplateDescriptor& descriptor,
    RegimeMapArtifactIdentity identity) {
    RegimeMapArtifact artifact{
        std::move(identity.id), regime_map_artifact_schema_v2,
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
    std::vector<RegimeMapRegion> regions,
    std::vector<OperatingEnvelopeConstraint> operating_envelope)
    : inputs_(std::move(inputs)),
      regions_(std::move(regions)),
      operating_envelope_(std::move(operating_envelope)) {
    id = std::move(artifact_id);
    schema_version = std::move(artifact_schema_version);
    revision = std::move(artifact_revision);
    checksum_sha256 = std::move(artifact_checksum_sha256);
    compiled_criteria_.reserve(regions_.size());
    for (const auto& region : regions_) {
        std::vector<std::vector<SafeExpression>> branches;
        branches.reserve(region.branches.size());
        for (const auto& branch : region.branches) {
            std::vector<SafeExpression> criteria;
            criteria.reserve(branch.criteria.size());
            for (const auto& criterion : branch.criteria) {
                criteria.push_back(
                    SafeExpression::parse(criterion.expression));
            }
            branches.push_back(std::move(criteria));
        }
        compiled_criteria_.push_back(std::move(branches));
    }
}

void RegimeMapArtifact::validate() const {
    if (schema_version != regime_map_artifact_schema_v2) {
        throw std::invalid_argument(
            "regime map '" + id +
            "' has unsupported schema version: " +
            schema_version);
    }
    constexpr std::size_t maximum_inputs = 64;
    constexpr std::size_t maximum_regions = 128;
    constexpr std::size_t maximum_branches_per_region = 32;
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
    std::vector<OperatingEnvelopeVariable> envelope_variables;
    for (const auto& input : inputs_) {
        envelope_variables.push_back({input.name, input.dimension});
    }
    validate_operating_envelope(
        operating_envelope_, envelope_variables, "regime-map");
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
        if (region.branches.empty() ||
            region.branches.size() > maximum_branches_per_region) {
            throw std::invalid_argument(
                "regime-map region '" + region.id +
                "' requires between 1 and 32 alternative branches");
        }
        std::set<std::string> branch_ids;
        for (std::size_t branch_index = 0;
             branch_index < region.branches.size(); ++branch_index) {
            const auto& branch = region.branches[branch_index];
            if (!valid_identifier(branch.id) ||
                !branch_ids.insert(branch.id).second ||
                branch.criteria.empty() ||
                branch.criteria.size() > maximum_criteria_per_region) {
                throw std::invalid_argument(
                    "regime-map region '" + region.id +
                    "' has an invalid branch: " + branch.id);
            }
            for (std::size_t criterion_index = 0;
                 criterion_index < branch.criteria.size();
                 ++criterion_index) {
                const auto& criterion =
                    branch.criteria[criterion_index];
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
                        "regime-map branch '" + branch.id +
                        "' has an invalid criterion interval");
                }
                const auto& symbols = compiled_criteria_[index]
                    [branch_index][criterion_index].symbols();
                if (symbols.empty() ||
                    !std::all_of(
                        symbols.begin(), symbols.end(),
                        [&](const auto& symbol) {
                            return declared_inputs.contains(symbol);
                        })) {
                    throw std::invalid_argument(
                        "regime-map branch '" + branch.id +
                        "' criterion must reference only declared inputs");
                }
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

const std::vector<OperatingEnvelopeConstraint>&
RegimeMapArtifact::operating_envelope() const noexcept {
    return operating_envelope_;
}

RegimeMapEvaluation RegimeMapArtifact::classify(
    const std::map<std::string, double>& inputs) const {
    for (const auto& input : inputs_) {
        const auto found = inputs.find(input.name);
        if (found == inputs.end() || !std::isfinite(found->second)) {
            return {{}, {}, {},
                "regime-map input is missing or non-finite: " +
                    input.name};
        }
    }
    if (inputs.size() != inputs_.size()) {
        return {{}, {}, {}, "regime map received unknown inputs"};
    }
    if (const auto violation = operating_envelope_violation(
            operating_envelope_, inputs, "regime-map")) {
        return {{}, {}, {}, *violation};
    }

    struct Match {
        std::size_t region;
        std::size_t branch;
        bool ambiguous_branch;
    };
    std::vector<Match> applicable;
    int best_priority = std::numeric_limits<int>::min();
    for (std::size_t region_index = 0;
         region_index < regions_.size(); ++region_index) {
        const auto& region = regions_[region_index];
        std::vector<std::size_t> matching_branches;
        int best_branch_priority = std::numeric_limits<int>::min();
        for (std::size_t branch_index = 0;
             branch_index < region.branches.size(); ++branch_index) {
            const auto& branch = region.branches[branch_index];
            bool branch_matches = true;
            for (std::size_t criterion_index = 0;
                 criterion_index < branch.criteria.size();
                 ++criterion_index) {
                std::map<std::string, double> criterion_inputs;
                for (const auto& symbol :
                     compiled_criteria_[region_index][branch_index]
                         [criterion_index].symbols()) {
                    criterion_inputs.emplace(symbol, inputs.at(symbol));
                }
                const auto evaluated = compiled_criteria_[region_index]
                    [branch_index][criterion_index]
                        .evaluate(criterion_inputs);
                if (!evaluated.error.empty()) {
                    return {{}, {}, {},
                        "regime-map branch '" + branch.id +
                            "' criterion failed: " + evaluated.error};
                }
                if (!inside(
                        evaluated.value,
                        branch.criteria[criterion_index])) {
                    branch_matches = false;
                    break;
                }
            }
            if (!branch_matches) continue;
            if (branch.priority > best_branch_priority) {
                matching_branches.clear();
                best_branch_priority = branch.priority;
            }
            if (branch.priority == best_branch_priority) {
                matching_branches.push_back(branch_index);
            }
        }
        if (matching_branches.empty()) continue;
        if (region.priority > best_priority) {
            applicable.clear();
            best_priority = region.priority;
        }
        if (region.priority == best_priority) {
            applicable.push_back({
                region_index, matching_branches.front(),
                matching_branches.size() != 1U});
        }
    }
    if (applicable.empty()) {
        return {{}, {}, {}, "no regime-map region is applicable"};
    }
    if (applicable.size() != 1U) {
        std::string error =
            "regime-map selection is ambiguous at priority " +
            std::to_string(best_priority) + ": ";
        for (std::size_t index = 0; index < applicable.size(); ++index) {
            if (index != 0U) error += ", ";
            error += regions_[applicable[index].region].id;
        }
        return {{}, {}, {}, std::move(error)};
    }
    const auto match = applicable.front();
    const auto& selected = regions_[match.region];
    if (match.ambiguous_branch) {
        return {{}, {}, {},
            "regime-map branch selection is ambiguous in region '" +
                selected.id + "'"};
    }
    const auto& branch = selected.branches[match.branch];
    return {selected.id, branch.id, selected.regime, {}};
}

}  // namespace thermox::platform
