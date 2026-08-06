#include "thermox/platform/correlation.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace thermox::platform {
namespace {

bool valid_identifier(const std::string& value) {
    if (value.empty() ||
        (!std::isalpha(static_cast<unsigned char>(value.front())) &&
         value.front() != '_')) {
        return false;
    }
    return std::all_of(
        value.begin() + 1, value.end(),
        [](unsigned char character) {
            return std::isalnum(character) || character == '_';
        });
}

CorrelationApplicabilityAssessment assess_ranges(
    const std::vector<CorrelationApplicabilityRange>& ranges,
    const std::map<std::string, double>& inputs) {
    CorrelationApplicabilityAssessment result;
    for (const auto& range : ranges) {
        const auto found = inputs.find(range.input);
        if (found == inputs.end() || !std::isfinite(found->second)) {
            result.applicable = false;
            result.violations.push_back(
                "applicability input is missing or non-finite: " +
                range.input);
            continue;
        }
        const double value = found->second;
        const bool below = range.minimum &&
            (value < *range.minimum ||
             (value == *range.minimum && !range.minimum_inclusive));
        const bool above = range.maximum &&
            (value > *range.maximum ||
             (value == *range.maximum && !range.maximum_inclusive));
        if (!below && !above) continue;
        result.applicable = false;
        std::ostringstream message;
        message << "correlation input outside applicability: "
                << range.input << '=' << value << " expected "
                << (range.minimum_inclusive ? '[' : '(');
        if (range.minimum) message << *range.minimum;
        else message << "-inf";
        message << ", ";
        if (range.maximum) message << *range.maximum;
        else message << "+inf";
        message << (range.maximum_inclusive ? ']' : ')');
        result.violations.push_back(message.str());
    }
    return result;
}

void validate_template_descriptor(
    const CorrelationTemplateDescriptor& descriptor) {
    if (!valid_identifier(descriptor.id) || descriptor.version.empty() ||
        descriptor.display_name.empty() || descriptor.category.empty() ||
        descriptor.reference.empty() || descriptor.inputs.empty() ||
        !valid_identifier(descriptor.output.name) ||
        descriptor.output.dimension.empty() ||
        descriptor.expression.empty() || descriptor.regime.empty()) {
        throw std::invalid_argument(
            "correlation template has incomplete identity, physics, or presentation metadata: " +
            descriptor.id);
    }
    std::set<std::string> symbols;
    for (const auto& input : descriptor.inputs) {
        if (!valid_identifier(input.name) || input.dimension.empty() ||
            !symbols.insert(input.name).second) {
            throw std::invalid_argument(
                "correlation template has an invalid or duplicate input: " +
                input.name);
        }
    }
    for (const auto& coefficient : descriptor.coefficients) {
        const bool default_below = coefficient.default_value &&
            (*coefficient.default_value < coefficient.lower_bound ||
             (*coefficient.default_value == coefficient.lower_bound &&
              !coefficient.lower_inclusive));
        const bool default_above = coefficient.default_value &&
            (*coefficient.default_value > coefficient.upper_bound ||
             (*coefficient.default_value == coefficient.upper_bound &&
              !coefficient.upper_inclusive));
        if (!valid_identifier(coefficient.name) ||
            coefficient.dimension.empty() ||
            !symbols.insert(coefficient.name).second ||
            (coefficient.default_value &&
             !std::isfinite(*coefficient.default_value)) ||
            std::isnan(coefficient.lower_bound) ||
            std::isnan(coefficient.upper_bound) ||
            coefficient.lower_bound > coefficient.upper_bound ||
            (coefficient.lower_bound == coefficient.upper_bound &&
             (!coefficient.lower_inclusive ||
              !coefficient.upper_inclusive)) ||
            default_below || default_above) {
            throw std::invalid_argument(
                "correlation template has an invalid coefficient descriptor: " +
                coefficient.name);
        }
    }
    const auto expression = SafeExpression::parse(descriptor.expression);
    if (expression.symbols() != symbols) {
        throw std::invalid_argument(
            "correlation template expression symbols must exactly match inputs and coefficients: " +
            descriptor.id);
    }
    std::set<std::string> ranged_inputs;
    for (const auto& range : descriptor.applicability) {
        if (!symbols.contains(range.input) ||
            std::none_of(
                descriptor.inputs.begin(), descriptor.inputs.end(),
                [&](const auto& input) {
                    return input.name == range.input;
                }) ||
            !ranged_inputs.insert(range.input).second ||
            (!range.minimum && !range.maximum) ||
            (range.minimum && !std::isfinite(*range.minimum)) ||
            (range.maximum && !std::isfinite(*range.maximum)) ||
            (range.minimum && range.maximum &&
             (*range.minimum > *range.maximum ||
              (*range.minimum == *range.maximum &&
               (!range.minimum_inclusive ||
                !range.maximum_inclusive))))) {
            throw std::invalid_argument(
                "correlation template has invalid applicability for input: " +
                range.input);
        }
    }
}

}  // namespace

void CorrelationTemplateRegistry::register_template(
    CorrelationTemplateDescriptor descriptor) {
    validate_template_descriptor(descriptor);
    const auto id = descriptor.id;
    if (!templates_.emplace(id, std::move(descriptor)).second) {
        throw std::invalid_argument(
            "duplicate correlation template id: " + id);
    }
}

const CorrelationTemplateDescriptor&
CorrelationTemplateRegistry::require_template(
    const std::string& id) const {
    const auto found = templates_.find(id);
    if (found == templates_.end()) {
        throw std::invalid_argument(
            "unknown correlation template: " + id);
    }
    return found->second;
}

std::vector<CorrelationTemplateDescriptor>
CorrelationTemplateRegistry::descriptors() const {
    std::vector<CorrelationTemplateDescriptor> result;
    result.reserve(templates_.size());
    for (const auto& [_, descriptor] : templates_) {
        result.push_back(descriptor);
    }
    return result;
}

CorrelationTemplateRegistry
make_default_correlation_template_registry() {
    CorrelationTemplateRegistry registry;
    registry.register_template({
        "zuber_findlay_kinematic_void_fraction",
        "1.0.0",
        "Zuber-Findlay kinematic void fraction",
        "Two-phase drift flux",
        "Zuber and Findlay, Journal of Heat Transfer 87(4), 1965, DOI 10.1115/1.3689137",
        {
            {"vapor_quality", "dimensionless"},
            {"liquid_density", "density"},
            {"vapor_density", "density"},
            {"mass_flux", "mass_flux"},
        },
        {"void_fraction", "dimensionless"},
        {
            {"distribution_parameter", "dimensionless", std::nullopt,
             0.0, std::numeric_limits<double>::infinity(), false, true},
            {"drift_velocity", "speed", std::nullopt,
             0.0, std::numeric_limits<double>::infinity(), true, true},
        },
        "(vapor_quality * mass_flux / vapor_density) / "
        "(distribution_parameter * mass_flux * "
        "(vapor_quality / vapor_density + "
        "(1 - vapor_quality) / liquid_density) + drift_velocity)",
        "upward_cocurrent_user_parameterized",
        {
            {"vapor_quality", 0.0, 1.0, false, false},
            {"mass_flux", 0.0, std::nullopt, false, true},
        },
    });
    const std::string chisholm_reference =
        "Chisholm, International Journal of Heat and Mass Transfer "
        "10 (1967) 1767-1778, DOI 10.1016/0017-9310(67)90047-6; "
        "Lockhart and Martinelli, Chemical Engineering Progress "
        "45(1) (1949) 39-48";
    const std::string chisholm_expression =
        "2 * liquid_fanning_coefficient * "
        "pow(liquid_reynolds_number, liquid_reynolds_exponent) * "
        "liquid_mass_flux * liquid_mass_flux / "
        "(diameter * liquid_density) + "
        "chisholm_parameter * sqrt((2 * liquid_fanning_coefficient * "
        "pow(liquid_reynolds_number, liquid_reynolds_exponent) * "
        "liquid_mass_flux * liquid_mass_flux / "
        "(diameter * liquid_density)) * "
        "(2 * vapor_fanning_coefficient * "
        "pow(vapor_reynolds_number, vapor_reynolds_exponent) * "
        "vapor_mass_flux * vapor_mass_flux / "
        "(diameter * vapor_density))) + "
        "2 * vapor_fanning_coefficient * "
        "pow(vapor_reynolds_number, vapor_reynolds_exponent) * "
        "vapor_mass_flux * vapor_mass_flux / "
        "(diameter * vapor_density)";
    const auto fixed_coefficient = [](
        std::string name, double value) {
        return CorrelationCoefficientDescriptor{
            std::move(name), "dimensionless", value,
            value, value, true, true};
    };
    const auto register_chisholm = [&registry, &chisholm_reference,
                                    &chisholm_expression,
                                    &fixed_coefficient](
        std::string id, std::string display_name,
        std::string regime, double liquid_coefficient,
        double liquid_exponent, double vapor_coefficient,
        double vapor_exponent, double chisholm_parameter,
        CorrelationApplicabilityRange liquid_range,
        CorrelationApplicabilityRange vapor_range) {
        registry.register_template({
            std::move(id), "1.0.0", std::move(display_name),
            "Two-phase friction pressure gradient",
            chisholm_reference,
            {
                {"liquid_mass_flux", "mass_flux"},
                {"vapor_mass_flux", "mass_flux"},
                {"liquid_density", "density"},
                {"vapor_density", "density"},
                {"liquid_reynolds_number", "dimensionless"},
                {"vapor_reynolds_number", "dimensionless"},
                {"diameter", "length"},
            },
            {"friction_pressure_gradient", "pressure_gradient"},
            {
                fixed_coefficient(
                    "liquid_fanning_coefficient", liquid_coefficient),
                fixed_coefficient(
                    "liquid_reynolds_exponent", liquid_exponent),
                fixed_coefficient(
                    "vapor_fanning_coefficient", vapor_coefficient),
                fixed_coefficient(
                    "vapor_reynolds_exponent", vapor_exponent),
                fixed_coefficient(
                    "chisholm_parameter", chisholm_parameter),
            },
            chisholm_expression, std::move(regime),
            {
                std::move(liquid_range), std::move(vapor_range),
                {"liquid_mass_flux", 0.0, std::nullopt,
                 false, true},
                {"vapor_mass_flux", 0.0, std::nullopt,
                 false, true},
                {"liquid_density", 0.0, std::nullopt,
                 false, true},
                {"vapor_density", 0.0, std::nullopt,
                 false, true},
                {"diameter", 0.0, std::nullopt, false, true},
            },
        });
    };
    const auto laminar = [](std::string input) {
        return CorrelationApplicabilityRange{
            std::move(input), 0.0, 2000.0, false, true};
    };
    const auto turbulent = [](std::string input) {
        return CorrelationApplicabilityRange{
            std::move(input), 2000.0, std::nullopt, false, true};
    };
    register_chisholm(
        "chisholm_laminar_laminar_friction_gradient",
        "Chisholm friction gradient (laminar/laminar)",
        "liquid_laminar_vapor_laminar", 16.0, -1.0,
        16.0, -1.0, 5.0,
        laminar("liquid_reynolds_number"),
        laminar("vapor_reynolds_number"));
    register_chisholm(
        "chisholm_laminar_turbulent_friction_gradient",
        "Chisholm friction gradient (laminar/turbulent)",
        "liquid_laminar_vapor_turbulent", 16.0, -1.0,
        0.079, -0.25, 12.0,
        laminar("liquid_reynolds_number"),
        turbulent("vapor_reynolds_number"));
    register_chisholm(
        "chisholm_turbulent_laminar_friction_gradient",
        "Chisholm friction gradient (turbulent/laminar)",
        "liquid_turbulent_vapor_laminar", 0.079, -0.25,
        16.0, -1.0, 10.0,
        turbulent("liquid_reynolds_number"),
        laminar("vapor_reynolds_number"));
    register_chisholm(
        "chisholm_turbulent_turbulent_friction_gradient",
        "Chisholm friction gradient (turbulent/turbulent)",
        "liquid_turbulent_vapor_turbulent", 0.079, -0.25,
        0.079, -0.25, 20.0,
        turbulent("liquid_reynolds_number"),
        turbulent("vapor_reynolds_number"));
    return registry;
}

CorrelationArtifact instantiate_correlation_template(
    const CorrelationTemplateDescriptor& descriptor,
    CorrelationArtifactIdentity identity,
    std::map<std::string, double> coefficients,
    std::string candidate_id,
    int priority) {
    validate_template_descriptor(descriptor);
    std::map<std::string, double> resolved;
    for (const auto& coefficient : descriptor.coefficients) {
        const auto supplied = coefficients.find(coefficient.name);
        if (supplied != coefficients.end()) {
            resolved.emplace(coefficient.name, supplied->second);
            coefficients.erase(supplied);
        } else if (coefficient.default_value) {
            resolved.emplace(
                coefficient.name, *coefficient.default_value);
        } else {
            throw std::invalid_argument(
                "correlation template requires coefficient: " +
                coefficient.name);
        }
        const double value = resolved.at(coefficient.name);
        const bool below = value < coefficient.lower_bound ||
            (value == coefficient.lower_bound &&
             !coefficient.lower_inclusive);
        const bool above = value > coefficient.upper_bound ||
            (value == coefficient.upper_bound &&
             !coefficient.upper_inclusive);
        if (!std::isfinite(value) || below || above) {
            throw std::invalid_argument(
                "correlation template coefficient is outside its bounds: " +
                coefficient.name);
        }
    }
    if (!coefficients.empty()) {
        throw std::invalid_argument(
            "correlation template received unknown coefficient: " +
            coefficients.begin()->first);
    }
    CorrelationArtifact artifact{
        std::move(identity.id), correlation_artifact_schema_v1,
        std::move(identity.revision),
        std::move(identity.checksum_sha256), descriptor.inputs,
        descriptor.output,
        {{std::move(candidate_id), descriptor.regime, priority,
          std::move(resolved), descriptor.expression,
          descriptor.applicability}},
    };
    artifact.validate();
    return artifact;
}

CorrelationArtifact instantiate_correlation_family(
    const CorrelationTemplateRegistry& registry,
    CorrelationArtifactIdentity identity,
    std::vector<CorrelationTemplateCandidateBinding> bindings) {
    if (bindings.empty()) {
        throw std::invalid_argument(
            "correlation family requires at least one template binding");
    }
    std::vector<CorrelationVariable> inputs;
    CorrelationVariable output;
    std::vector<CorrelationCandidate> candidates;
    for (std::size_t index = 0; index < bindings.size(); ++index) {
        auto& binding = bindings[index];
        const auto& descriptor =
            registry.require_template(binding.template_id);
        const std::string candidate_id =
            binding.candidate_id.empty()
            ? binding.template_id
            : binding.candidate_id;
        const auto artifact = instantiate_correlation_template(
            descriptor, identity, std::move(binding.coefficients),
            candidate_id, binding.priority);
        if (index == 0U) {
            inputs = artifact.inputs();
            output = artifact.output();
        } else {
            const auto& other_inputs = artifact.inputs();
            const bool same_inputs = inputs.size() == other_inputs.size() &&
                std::equal(
                    inputs.begin(), inputs.end(), other_inputs.begin(),
                    [](const auto& left, const auto& right) {
                        return left.name == right.name &&
                            left.dimension == right.dimension;
                    });
            if (!same_inputs || output.name != artifact.output().name ||
                output.dimension != artifact.output().dimension) {
                throw std::invalid_argument(
                    "correlation family templates must share identical "
                    "input and output contracts");
            }
        }
        candidates.push_back(artifact.candidates().front());
    }
    CorrelationArtifact artifact{
        std::move(identity.id), correlation_artifact_schema_v1,
        std::move(identity.revision),
        std::move(identity.checksum_sha256), std::move(inputs),
        std::move(output), std::move(candidates)};
    artifact.validate();
    return artifact;
}

CorrelationArtifact::CorrelationArtifact(
    std::string artifact_id,
    std::string artifact_schema_version,
    std::string artifact_revision,
    std::string artifact_checksum_sha256,
    std::vector<CorrelationVariable> inputs,
    CorrelationVariable output,
    std::vector<CorrelationCandidate> candidates)
    : inputs_(std::move(inputs)),
      output_(std::move(output)),
      candidates_(std::move(candidates)) {
    id = std::move(artifact_id);
    schema_version = std::move(artifact_schema_version);
    revision = std::move(artifact_revision);
    checksum_sha256 = std::move(artifact_checksum_sha256);
    compiled_candidates_.reserve(candidates_.size());
    for (const auto& candidate : candidates_) {
        compiled_candidates_.push_back(
            SafeExpression::parse(candidate.expression));
    }
}

void CorrelationArtifact::validate() const {
    if (schema_version != correlation_artifact_schema_v1) {
        throw std::invalid_argument(
            "correlation artifact '" + id +
            "' has unsupported schema version: " +
            schema_version);
    }
    if (inputs_.empty()) {
        throw std::invalid_argument(
            "correlation artifact '" + id +
            "' requires at least one input");
    }
    if (!valid_identifier(output_.name) ||
        output_.dimension.empty()) {
        throw std::invalid_argument(
            "correlation artifact '" + id +
            "' has an invalid output descriptor");
    }
    std::set<std::string> declared;
    for (const auto& input : inputs_) {
        if (!valid_identifier(input.name) ||
            input.dimension.empty() ||
            !declared.insert(input.name).second) {
            throw std::invalid_argument(
                "correlation artifact '" + id +
                "' has an invalid or duplicate input: " +
                input.name);
        }
    }
    const auto validate_ranges = [&](const auto& ranges) {
        std::set<std::string> ranged_inputs;
        for (const auto& range : ranges) {
            if (!declared.contains(range.input) ||
                !ranged_inputs.insert(range.input).second) {
                throw std::invalid_argument(
                    "correlation artifact '" + id +
                    "' has an unknown or duplicate applicability input: " +
                    range.input);
            }
            if ((!range.minimum && !range.maximum) ||
                (range.minimum && !std::isfinite(*range.minimum)) ||
                (range.maximum && !std::isfinite(*range.maximum)) ||
                (range.minimum && range.maximum &&
                 (*range.minimum > *range.maximum ||
                  (*range.minimum == *range.maximum &&
                   (!range.minimum_inclusive ||
                    !range.maximum_inclusive))))) {
                throw std::invalid_argument(
                    "correlation artifact '" + id +
                    "' has an invalid applicability range for input: " +
                    range.input);
            }
        }
    };
    if (candidates_.empty()) {
        throw std::invalid_argument(
            "correlation family artifact '" + id +
            "' requires at least one candidate");
    }
    std::set<std::string> candidate_ids;
    for (std::size_t index = 0; index < candidates_.size(); ++index) {
        const auto& candidate = candidates_[index];
        if (!valid_identifier(candidate.id) ||
            candidate.regime.empty() ||
            !candidate_ids.insert(candidate.id).second) {
            throw std::invalid_argument(
                "correlation family artifact '" + id +
                "' has an invalid or duplicate candidate: " +
                candidate.id);
        }
        auto candidate_symbols = declared;
        for (const auto& [name, value] : candidate.coefficients) {
            if (!valid_identifier(name) || !std::isfinite(value) ||
                !candidate_symbols.insert(name).second) {
                throw std::invalid_argument(
                    "correlation candidate '" + candidate.id +
                    "' has an invalid or duplicate coefficient: " + name);
            }
        }
        validate_ranges(candidate.applicability);
        if (compiled_candidates_.at(index).symbols() !=
            candidate_symbols) {
            throw std::invalid_argument(
                "correlation candidate '" + candidate.id +
                "' expression symbols must exactly match its inputs "
                "and coefficients");
        }
    }
}

const std::vector<CorrelationVariable>&
CorrelationArtifact::inputs() const noexcept {
    return inputs_;
}

const CorrelationVariable&
CorrelationArtifact::output() const noexcept {
    return output_;
}

const std::vector<CorrelationCandidate>&
CorrelationArtifact::candidates() const noexcept {
    return candidates_;
}

CorrelationApplicabilityAssessment
CorrelationArtifact::assess_applicability(
    const std::map<std::string, double>& inputs) const {
    CorrelationApplicabilityAssessment family;
    family.applicable = false;
    for (const auto& candidate : candidates_) {
        const auto assessment = assess_ranges(
            candidate.applicability, inputs);
        if (assessment.applicable) {
            family.applicable = true;
            family.violations.clear();
            return family;
        }
        for (const auto& violation : assessment.violations) {
            family.violations.push_back(
                candidate.id + ": " + violation);
        }
    }
    return family;
}

CorrelationEvaluation CorrelationArtifact::evaluate(
    const std::map<std::string, double>& inputs) const {
    for (const auto& input : inputs_) {
        const auto found = inputs.find(input.name);
        if (found == inputs.end() || !std::isfinite(found->second)) {
            return {
                0.0, {},
                "correlation input is missing or non-finite: " +
                    input.name, {}, {}};
        }
    }
    if (inputs.size() != inputs_.size()) {
        return {0.0, {}, "correlation received unknown inputs", {}, {}};
    }
    std::vector<std::size_t> applicable;
    int best_priority = std::numeric_limits<int>::min();
    std::vector<std::string> exclusions;
    for (std::size_t index = 0; index < candidates_.size(); ++index) {
        const auto assessment = assess_ranges(
            candidates_[index].applicability, inputs);
        if (!assessment.applicable) {
            std::string exclusion = candidates_[index].id + ": ";
            for (std::size_t item = 0;
                 item < assessment.violations.size(); ++item) {
                if (item != 0U) exclusion += "; ";
                exclusion += assessment.violations[item];
            }
            exclusions.push_back(std::move(exclusion));
            continue;
        }
        if (candidates_[index].priority > best_priority) {
            applicable.clear();
            best_priority = candidates_[index].priority;
        }
        if (candidates_[index].priority == best_priority) {
            applicable.push_back(index);
        }
    }
    if (applicable.empty()) {
        std::string error = "no correlation candidate is applicable";
        for (const auto& exclusion : exclusions) {
            error += "; " + exclusion;
        }
        return {0.0, {}, std::move(error), {}, {}};
    }
    if (applicable.size() != 1U) {
        std::string error =
            "correlation candidate selection is ambiguous at priority " +
            std::to_string(best_priority) + ": ";
        for (std::size_t item = 0; item < applicable.size(); ++item) {
            if (item != 0U) error += ", ";
            error += candidates_[applicable[item]].id;
        }
        return {0.0, {}, std::move(error), {}, {}};
    }
    const auto index = applicable.front();
    const auto& candidate = candidates_[index];
    std::map<std::string, double> values = candidate.coefficients;
    values.insert(inputs.begin(), inputs.end());
    const auto evaluated = compiled_candidates_[index].evaluate(values);
    CorrelationEvaluation result;
    result.value = evaluated.value;
    result.error = evaluated.error;
    result.selected_candidate = candidate.id;
    result.selected_regime = candidate.regime;
    if (!result.error.empty()) return result;
    for (const auto& input : inputs_) {
        const auto derivative = evaluated.derivatives.find(input.name);
        result.input_derivatives.emplace(
            input.name, derivative == evaluated.derivatives.end()
                ? 0.0 : derivative->second);
    }
    return result;
}

}  // namespace thermox::platform
