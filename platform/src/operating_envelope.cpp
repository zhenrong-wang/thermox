#include "thermox/platform/operating_envelope.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <stdexcept>

namespace thermox::platform {

void validate_operating_envelope(
    const std::vector<OperatingEnvelopeConstraint>& constraints,
    const std::vector<OperatingEnvelopeVariable>& variables,
    std::string_view artifact_kind) {
    std::set<std::string> coordinates;
    for (const auto& constraint : constraints) {
        const auto variable = std::find_if(
            variables.begin(), variables.end(), [&](const auto& value) {
                return value.name == constraint.coordinate;
            });
        if (!coordinates.insert(constraint.coordinate).second) {
            throw std::invalid_argument(
                std::string(artifact_kind) +
                " operating-envelope coordinates must be unique");
        }
        if (variable == variables.end() ||
            variable->dimension != constraint.dimension) {
            throw std::invalid_argument(
                std::string(artifact_kind) +
                " operating-envelope coordinate does not match its "
                "declared variable");
        }
        if ((!constraint.minimum && !constraint.maximum) ||
            (constraint.minimum &&
             !std::isfinite(*constraint.minimum)) ||
            (constraint.maximum &&
             !std::isfinite(*constraint.maximum)) ||
            (constraint.minimum && constraint.maximum &&
             (*constraint.minimum > *constraint.maximum ||
              (*constraint.minimum == *constraint.maximum &&
               (!constraint.minimum_inclusive ||
                !constraint.maximum_inclusive))))) {
            throw std::invalid_argument(
                std::string(artifact_kind) +
                " operating-envelope interval is invalid");
        }
    }
}

std::optional<std::string> operating_envelope_violation(
    const std::vector<OperatingEnvelopeConstraint>& constraints,
    const std::map<std::string, double>& coordinates,
    std::string_view artifact_kind) {
    for (const auto& constraint : constraints) {
        const auto found = coordinates.find(constraint.coordinate);
        if (found == coordinates.end() || !std::isfinite(found->second)) {
            continue;
        }
        const double value = found->second;
        const bool below = constraint.minimum &&
            (value < *constraint.minimum ||
             (value == *constraint.minimum &&
              !constraint.minimum_inclusive));
        const bool above = constraint.maximum &&
            (value > *constraint.maximum ||
             (value == *constraint.maximum &&
              !constraint.maximum_inclusive));
        if (!below && !above) continue;

        std::ostringstream message;
        message << operating_envelope_violation_code << ": "
                << artifact_kind << " operating envelope rejected "
                << "coordinate '" << constraint.coordinate
                << "' value " << value << " outside "
                << (constraint.minimum_inclusive ? '[' : '(');
        if (constraint.minimum) message << *constraint.minimum;
        else message << "-inf";
        message << ", ";
        if (constraint.maximum) message << *constraint.maximum;
        else message << "+inf";
        message << (constraint.maximum_inclusive ? ']' : ')')
                << ' ' << constraint.dimension;
        return message.str();
    }
    return std::nullopt;
}

}  // namespace thermox::platform
