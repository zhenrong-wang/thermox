#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace thermox::platform {

inline constexpr char operating_envelope_violation_code[] =
    "artifact_operating_envelope_violation";

struct OperatingEnvelopeVariable {
    std::string name;
    std::string dimension;
};

struct OperatingEnvelopeConstraint {
    std::string coordinate;
    std::string dimension;
    std::optional<double> minimum;
    std::optional<double> maximum;
    bool minimum_inclusive{true};
    bool maximum_inclusive{true};

    bool operator==(const OperatingEnvelopeConstraint&) const = default;
};

// Validates a policy envelope against an artifact's typed input contract.
// Source-artifact applicability and Study-owned execution policy remain
// distinct: this primitive represents the latter.
void validate_operating_envelope(
    const std::vector<OperatingEnvelopeConstraint>& constraints,
    const std::vector<OperatingEnvelopeVariable>& variables,
    std::string_view artifact_kind);

// Returns a stable, dimensioned diagnostic for the first violation.
[[nodiscard]] std::optional<std::string> operating_envelope_violation(
    const std::vector<OperatingEnvelopeConstraint>& constraints,
    const std::map<std::string, double>& coordinates,
    std::string_view artifact_kind);

}  // namespace thermox::platform
