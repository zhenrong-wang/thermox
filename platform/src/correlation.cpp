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

}  // namespace

CorrelationArtifact::CorrelationArtifact(
    std::string artifact_id,
    std::string artifact_schema_version,
    std::string artifact_revision,
    std::string artifact_checksum_sha256,
    std::vector<CorrelationVariable> inputs,
    CorrelationVariable output,
    std::map<std::string, double> coefficients,
    std::string expression,
    std::vector<CorrelationApplicabilityRange> applicability)
    : inputs_(std::move(inputs)),
      output_(std::move(output)),
      coefficients_(std::move(coefficients)),
      expression_(std::move(expression)),
      applicability_(std::move(applicability)),
      compiled_(SafeExpression::parse(expression_)) {
    id = std::move(artifact_id);
    schema_version = std::move(artifact_schema_version);
    revision = std::move(artifact_revision);
    checksum_sha256 = std::move(artifact_checksum_sha256);
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
    if (schema_version != correlation_artifact_schema_v1 &&
        schema_version != correlation_artifact_schema_v2) {
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
    for (const auto& [name, value] : coefficients_) {
        if (!valid_identifier(name) || !std::isfinite(value) ||
            !declared.insert(name).second) {
            throw std::invalid_argument(
                "correlation artifact '" + id +
                "' has an invalid or duplicate coefficient: " +
                name);
        }
    }
    const auto validate_ranges = [&](const auto& ranges) {
        std::set<std::string> ranged_inputs;
        for (const auto& range : ranges) {
            if (!declared.contains(range.input) ||
                coefficients_.contains(range.input) ||
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
    if (schema_version == correlation_artifact_schema_v1) {
        validate_ranges(applicability_);
        if (!compiled_ || compiled_->symbols() != declared) {
            throw std::invalid_argument(
                "correlation artifact '" + id +
                "' expression symbols must exactly match its inputs "
                "and coefficients");
        }
        return;
    }
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

const std::map<std::string, double>&
CorrelationArtifact::coefficients() const noexcept {
    return coefficients_;
}

const std::string& CorrelationArtifact::expression()
    const noexcept {
    return expression_;
}

const std::vector<CorrelationApplicabilityRange>&
CorrelationArtifact::applicability() const noexcept {
    return applicability_;
}

const std::vector<CorrelationCandidate>&
CorrelationArtifact::candidates() const noexcept {
    return candidates_;
}

CorrelationApplicabilityAssessment
CorrelationArtifact::assess_applicability(
    const std::map<std::string, double>& inputs) const {
    if (schema_version == correlation_artifact_schema_v2) {
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
    return assess_ranges(applicability_, inputs);
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
    if (schema_version == correlation_artifact_schema_v2) {
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
    const auto applicability = assess_applicability(inputs);
    if (!applicability.applicable) {
        std::string error;
        for (const auto& violation : applicability.violations) {
            if (!error.empty()) error += "; ";
            error += violation;
        }
        return {0.0, {}, std::move(error), {}, {}};
    }
    std::map<std::string, double> values = coefficients_;
    values.insert(inputs.begin(), inputs.end());
    const auto evaluated = compiled_->evaluate(values);
    CorrelationEvaluation result;
    result.value = evaluated.value;
    result.error = evaluated.error;
    if (!result.error.empty()) return result;
    for (const auto& input : inputs_) {
        const auto derivative =
            evaluated.derivatives.find(input.name);
        result.input_derivatives.emplace(
            input.name,
            derivative == evaluated.derivatives.end()
                ? 0.0
                : derivative->second);
    }
    return result;
}

}  // namespace thermox::platform
