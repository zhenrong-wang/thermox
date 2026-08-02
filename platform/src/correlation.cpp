#include "thermox/platform/correlation.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
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

}  // namespace

CorrelationArtifact::CorrelationArtifact(
    std::string artifact_id,
    std::string artifact_schema_version,
    std::string artifact_revision,
    std::string artifact_checksum_sha256,
    std::vector<CorrelationVariable> inputs,
    CorrelationVariable output,
    std::map<std::string, double> coefficients,
    std::string expression)
    : inputs_(std::move(inputs)),
      output_(std::move(output)),
      coefficients_(std::move(coefficients)),
      expression_(std::move(expression)),
      compiled_(SafeExpression::parse(expression_)) {
    id = std::move(artifact_id);
    schema_version = std::move(artifact_schema_version);
    revision = std::move(artifact_revision);
    checksum_sha256 = std::move(artifact_checksum_sha256);
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
    for (const auto& [name, value] : coefficients_) {
        if (!valid_identifier(name) || !std::isfinite(value) ||
            !declared.insert(name).second) {
            throw std::invalid_argument(
                "correlation artifact '" + id +
                "' has an invalid or duplicate coefficient: " +
                name);
        }
    }
    if (compiled_.symbols() != declared) {
        throw std::invalid_argument(
            "correlation artifact '" + id +
            "' expression symbols must exactly match its inputs "
            "and coefficients");
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

CorrelationEvaluation CorrelationArtifact::evaluate(
    const std::map<std::string, double>& inputs) const {
    std::map<std::string, double> values = coefficients_;
    for (const auto& input : inputs_) {
        const auto found = inputs.find(input.name);
        if (found == inputs.end() || !std::isfinite(found->second)) {
            return {
                0.0, {},
                "correlation input is missing or non-finite: " +
                    input.name};
        }
        values.emplace(input.name, found->second);
    }
    if (inputs.size() != inputs_.size()) {
        return {0.0, {}, "correlation received unknown inputs"};
    }
    const auto evaluated = compiled_.evaluate(values);
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
