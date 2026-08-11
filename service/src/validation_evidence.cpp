#include "thermox/service/validation_evidence.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace thermox::service {
namespace {

constexpr std::array<ValidationEvidenceBasis, 6> evidence_bases{
    ValidationEvidenceBasis::independent_reference,
    ValidationEvidenceBasis::boundary_constrained,
    ValidationEvidenceBasis::calibrated_reproduction,
    ValidationEvidenceBasis::derived_reference,
    ValidationEvidenceBasis::internal_consistency,
    ValidationEvidenceBasis::assumption_dependent,
};

bool close(double left, double right) {
    const double scale = std::max(
        {1.0, std::abs(left), std::abs(right)});
    return std::abs(left - right) <=
        8.0 * std::numeric_limits<double>::epsilon() * scale;
}

}  // namespace

std::string to_string(ValidationEvidenceLayer layer) {
    switch (layer) {
    case ValidationEvidenceLayer::numerical:
        return "numerical";
    case ValidationEvidenceLayer::property:
        return "property";
    case ValidationEvidenceLayer::component:
        return "component";
    case ValidationEvidenceLayer::system:
        return "system";
    }
    throw ValidationEvidenceError(
        "unknown validation evidence layer");
}

ValidationEvidenceLayer validation_evidence_layer_from_string(
    const std::string& value) {
    if (value == "numerical") {
        return ValidationEvidenceLayer::numerical;
    }
    if (value == "property") {
        return ValidationEvidenceLayer::property;
    }
    if (value == "component") {
        return ValidationEvidenceLayer::component;
    }
    if (value == "system") {
        return ValidationEvidenceLayer::system;
    }
    throw ValidationEvidenceError(
        "unsupported validation evidence layer: " + value);
}

std::string to_string(ValidationEvidenceBasis basis) {
    switch (basis) {
    case ValidationEvidenceBasis::independent_reference:
        return "independent_reference";
    case ValidationEvidenceBasis::boundary_constrained:
        return "boundary_constrained";
    case ValidationEvidenceBasis::calibrated_reproduction:
        return "calibrated_reproduction";
    case ValidationEvidenceBasis::derived_reference:
        return "derived_reference";
    case ValidationEvidenceBasis::internal_consistency:
        return "internal_consistency";
    case ValidationEvidenceBasis::assumption_dependent:
        return "assumption_dependent";
    }
    throw ValidationEvidenceError(
        "unknown validation evidence basis");
}

ValidationEvidenceBasis validation_evidence_basis_from_string(
    const std::string& value) {
    for (const auto basis : evidence_bases) {
        if (to_string(basis) == value) return basis;
    }
    throw ValidationEvidenceError(
        "unsupported validation evidence basis: " + value);
}

std::vector<ValidationObservedValue>
validation_observations_from_result_summary(
    const ResultSummary& summary) {
    std::vector<ValidationObservedValue> observations;
    observations.reserve(summary.values.size());
    for (const auto& value : summary.values) {
        observations.push_back({
            value.id,
            value.dimension,
            value.value_si,
        });
    }
    return observations;
}

void validate_validation_evidence_criteria(
    const std::vector<ValidationEvidenceCriterion>& criteria) {
    if (criteria.empty()) {
        throw ValidationEvidenceError(
            "validation evidence must define at least one criterion");
    }
    if (criteria.size() > 512U) {
        throw ValidationEvidenceError(
            "validation evidence may define at most 512 criteria");
    }
    std::set<std::string> ids;
    for (const auto& criterion : criteria) {
        if (criterion.id.empty() ||
            criterion.observed_value_id.empty() ||
            criterion.dimension.empty() ||
            criterion.source_reference.empty()) {
            throw ValidationEvidenceError(
                "validation criterion identity, observation, dimension, "
                "and source reference must not be empty");
        }
        if (!ids.insert(criterion.id).second) {
            throw ValidationEvidenceError(
                "validation criterion IDs must be unique");
        }
        if (!std::isfinite(criterion.reference_value_si) ||
            !std::isfinite(criterion.absolute_tolerance_si) ||
            !std::isfinite(criterion.relative_tolerance) ||
            criterion.absolute_tolerance_si < 0.0 ||
            criterion.relative_tolerance < 0.0) {
            throw ValidationEvidenceError(
                "validation criterion references and tolerances must "
                "be finite and tolerances must be non-negative");
        }
        (void)to_string(criterion.layer);
        (void)to_string(criterion.basis);
    }
}

ValidationEvidenceSummary evaluate_validation_evidence(
    const std::vector<ValidationObservedValue>& observations,
    const std::vector<ValidationEvidenceCriterion>& criteria,
    std::vector<std::string> limitations) {
    validate_validation_evidence_criteria(criteria);
    if (observations.size() > 512U) {
        throw ValidationEvidenceError(
            "validation evidence may define at most 512 observations");
    }
    std::set<std::string> observation_ids;
    for (const auto& observation : observations) {
        if (observation.id.empty() || observation.dimension.empty() ||
            !std::isfinite(observation.value_si)) {
            throw ValidationEvidenceError(
                "validation observations must have finite values and "
                "non-empty identities and dimensions");
        }
        if (!observation_ids.insert(observation.id).second) {
            throw ValidationEvidenceError(
                "validation observation IDs must be unique");
        }
    }
    std::set<std::string> unique_limitations;
    for (const auto& limitation : limitations) {
        if (limitation.empty() ||
            !unique_limitations.insert(limitation).second) {
            throw ValidationEvidenceError(
                "validation limitations must be non-empty and unique");
        }
    }

    ValidationEvidenceSummary summary;
    summary.passed = true;
    summary.limitations = std::move(limitations);
    summary.criteria.reserve(criteria.size());
    summary.classes.reserve(evidence_bases.size());
    for (const auto basis : evidence_bases) {
        summary.classes.push_back({basis, 0U, 0U});
    }
    for (const auto& criterion : criteria) {
        const auto observation = std::find_if(
            observations.begin(), observations.end(),
            [&](const ValidationObservedValue& candidate) {
                return candidate.id == criterion.observed_value_id;
            });
        if (observation == observations.end()) {
            throw ValidationEvidenceError(
                "validation criterion '" + criterion.id +
                "' references missing observation '" +
                criterion.observed_value_id + "'");
        }
        if (observation->dimension != criterion.dimension) {
            throw ValidationEvidenceError(
                "validation criterion '" + criterion.id +
                "' dimension does not match its observation");
        }
        const double signed_error =
            observation->value_si - criterion.reference_value_si;
        const double absolute_error = std::abs(signed_error);
        const double allowed_error =
            criterion.absolute_tolerance_si +
            criterion.relative_tolerance *
                std::abs(criterion.reference_value_si);
        if (!std::isfinite(signed_error) ||
            !std::isfinite(absolute_error) ||
            !std::isfinite(allowed_error)) {
            throw ValidationEvidenceError(
                "validation criterion '" + criterion.id +
                "' overflows its error or combined tolerance");
        }
        const double comparison_slack =
            8.0 * std::numeric_limits<double>::epsilon() *
            std::max({
                1.0,
                absolute_error,
                allowed_error,
                std::abs(criterion.reference_value_si),
            });
        const bool passed =
            absolute_error <= allowed_error + comparison_slack;
        const std::optional<double> relative_error =
            criterion.reference_value_si == 0.0
            ? std::nullopt
            : std::optional<double>{
                  signed_error /
                  std::abs(criterion.reference_value_si)};
        summary.criteria.push_back({
            criterion.id,
            criterion.observed_value_id,
            criterion.layer,
            criterion.basis,
            criterion.dimension,
            observation->value_si,
            criterion.reference_value_si,
            signed_error,
            absolute_error,
            relative_error,
            allowed_error,
            criterion.source_reference,
            criterion.note,
            passed,
        });
        auto& class_summary = *std::find_if(
            summary.classes.begin(), summary.classes.end(),
            [&](const ValidationEvidenceClassSummary& candidate) {
                return candidate.basis == criterion.basis;
            });
        class_summary.passed_count += passed ? 1U : 0U;
        class_summary.failed_count += passed ? 0U : 1U;
        summary.passed_count += passed ? 1U : 0U;
        summary.failed_count += passed ? 0U : 1U;
        summary.passed = summary.passed && passed;
    }
    validate_validation_evidence_summary(summary);
    return summary;
}

void validate_validation_evidence_summary(
    const ValidationEvidenceSummary& summary) {
    if (summary.schema_version != validation_evidence_schema_v1 ||
        summary.criteria.empty() ||
        summary.classes.size() != evidence_bases.size()) {
        throw ValidationEvidenceError(
            "validation evidence summary schema or shape is invalid");
    }
    std::set<std::string> ids;
    std::set<ValidationEvidenceBasis> class_bases;
    std::size_t passed_count = 0U;
    std::size_t failed_count = 0U;
    for (const auto& result : summary.criteria) {
        if (result.criterion_id.empty() ||
            result.observed_value_id.empty() ||
            result.dimension.empty() ||
            result.source_reference.empty() ||
            !ids.insert(result.criterion_id).second ||
            !std::isfinite(result.actual_value_si) ||
            !std::isfinite(result.reference_value_si) ||
            !std::isfinite(result.signed_error_si) ||
            !std::isfinite(result.absolute_error_si) ||
            !std::isfinite(result.allowed_absolute_error_si) ||
            (result.relative_error &&
             !std::isfinite(*result.relative_error)) ||
            result.absolute_error_si < 0.0 ||
            result.allowed_absolute_error_si < 0.0 ||
            !close(
                result.signed_error_si,
                result.actual_value_si - result.reference_value_si) ||
            !close(
                result.absolute_error_si,
                std::abs(result.signed_error_si))) {
            throw ValidationEvidenceError(
                "validation evidence result is inconsistent");
        }
        (void)to_string(result.layer);
        (void)to_string(result.basis);
        const std::optional<double> expected_relative_error =
            result.reference_value_si == 0.0
            ? std::nullopt
            : std::optional<double>{
                  result.signed_error_si /
                  std::abs(result.reference_value_si)};
        if (result.relative_error.has_value() !=
                expected_relative_error.has_value() ||
            (result.relative_error &&
             !close(
                 *result.relative_error,
                 *expected_relative_error))) {
            throw ValidationEvidenceError(
                "validation evidence relative error is inconsistent");
        }
        const bool expected_pass =
            result.absolute_error_si <=
                result.allowed_absolute_error_si +
                    8.0 * std::numeric_limits<double>::epsilon() *
                    std::max({
                        1.0,
                        result.absolute_error_si,
                        result.allowed_absolute_error_si,
                        std::abs(result.reference_value_si),
                    });
        if (result.passed != expected_pass) {
            throw ValidationEvidenceError(
                "validation evidence result verdict is inconsistent");
        }
        passed_count += result.passed ? 1U : 0U;
        failed_count += result.passed ? 0U : 1U;
    }
    std::size_t class_passed = 0U;
    std::size_t class_failed = 0U;
    for (const auto& class_summary : summary.classes) {
        if (!class_bases.insert(class_summary.basis).second) {
            throw ValidationEvidenceError(
                "validation evidence class summaries must be unique");
        }
        class_passed += class_summary.passed_count;
        class_failed += class_summary.failed_count;
        const auto expected_passed = static_cast<std::size_t>(
            std::count_if(
                summary.criteria.begin(), summary.criteria.end(),
                [&](const ValidationEvidenceResult& result) {
                    return result.basis == class_summary.basis &&
                        result.passed;
                }));
        const auto expected_failed = static_cast<std::size_t>(
            std::count_if(
                summary.criteria.begin(), summary.criteria.end(),
                [&](const ValidationEvidenceResult& result) {
                    return result.basis == class_summary.basis &&
                        !result.passed;
                }));
        if (class_summary.passed_count != expected_passed ||
            class_summary.failed_count != expected_failed) {
            throw ValidationEvidenceError(
                "validation evidence class counts are inconsistent");
        }
    }
    for (const auto basis : evidence_bases) {
        if (!class_bases.contains(basis)) {
            throw ValidationEvidenceError(
                "validation evidence class summary is incomplete");
        }
    }
    std::set<std::string> limitations;
    for (const auto& limitation : summary.limitations) {
        if (limitation.empty() ||
            !limitations.insert(limitation).second) {
            throw ValidationEvidenceError(
                "validation evidence limitations are invalid");
        }
    }
    if (summary.passed_count != passed_count ||
        summary.failed_count != failed_count ||
        class_passed != passed_count ||
        class_failed != failed_count ||
        summary.passed != (failed_count == 0U)) {
        throw ValidationEvidenceError(
            "validation evidence summary counts or verdict are "
            "inconsistent");
    }
}

}  // namespace thermox::service
