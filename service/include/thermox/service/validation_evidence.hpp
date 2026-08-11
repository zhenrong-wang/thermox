#pragma once

#include "thermox/service/result_projection.hpp"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace thermox::service {

inline constexpr char validation_evidence_schema_v1[] =
    "thermox.validation_evidence/v1";

enum class ValidationEvidenceLayer {
    numerical,
    property,
    component,
    system,
};

std::string to_string(ValidationEvidenceLayer layer);
ValidationEvidenceLayer validation_evidence_layer_from_string(
    const std::string& value);

// Classification describes how much independence a passing comparison has.
// It never changes the numerical pass/fail calculation.
enum class ValidationEvidenceBasis {
    independent_reference,
    boundary_constrained,
    calibrated_reproduction,
    derived_reference,
    internal_consistency,
    assumption_dependent,
};

std::string to_string(ValidationEvidenceBasis basis);
ValidationEvidenceBasis validation_evidence_basis_from_string(
    const std::string& value);

struct ValidationObservedValue {
    std::string id;
    std::string dimension;
    double value_si{0.0};
};

struct ValidationEvidenceCriterion {
    std::string id;
    std::string observed_value_id;
    ValidationEvidenceLayer layer{
        ValidationEvidenceLayer::system};
    ValidationEvidenceBasis basis{
        ValidationEvidenceBasis::internal_consistency};
    std::string dimension;
    double reference_value_si{0.0};
    double absolute_tolerance_si{0.0};
    double relative_tolerance{0.0};
    std::string source_reference;
    std::string note;
};

struct ValidationEvidenceResult {
    std::string criterion_id;
    std::string observed_value_id;
    ValidationEvidenceLayer layer{
        ValidationEvidenceLayer::system};
    ValidationEvidenceBasis basis{
        ValidationEvidenceBasis::internal_consistency};
    std::string dimension;
    double actual_value_si{0.0};
    double reference_value_si{0.0};
    double signed_error_si{0.0};
    double absolute_error_si{0.0};
    std::optional<double> relative_error;
    double allowed_absolute_error_si{0.0};
    std::string source_reference;
    std::string note;
    bool passed{false};
};

struct ValidationEvidenceClassSummary {
    ValidationEvidenceBasis basis{
        ValidationEvidenceBasis::internal_consistency};
    std::size_t passed_count{0};
    std::size_t failed_count{0};
};

struct ValidationEvidenceSummary {
    std::string schema_version{validation_evidence_schema_v1};
    bool passed{false};
    std::size_t passed_count{0};
    std::size_t failed_count{0};
    std::vector<ValidationEvidenceClassSummary> classes;
    std::vector<ValidationEvidenceResult> criteria;
    std::vector<std::string> limitations;
};

class ValidationEvidenceError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::vector<ValidationObservedValue>
validation_observations_from_result_summary(
    const ResultSummary& summary);

void validate_validation_evidence_criteria(
    const std::vector<ValidationEvidenceCriterion>& criteria);

ValidationEvidenceSummary evaluate_validation_evidence(
    const std::vector<ValidationObservedValue>& observations,
    const std::vector<ValidationEvidenceCriterion>& criteria,
    std::vector<std::string> limitations = {});

void validate_validation_evidence_summary(
    const ValidationEvidenceSummary& summary);

}  // namespace thermox::service
