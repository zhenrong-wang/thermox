#include "thermox/physics/property_package.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace thermox::physics {
namespace {

PhDerivativesResult failure(
    PropertyStatus status,
    std::string message,
    PropertyDerivativeSource source =
        PropertyDerivativeSource::analytic) {
    return {{}, {}, source, status, std::move(message)};
}

}  // namespace

PhDerivativesResult PropertyPackage::state_ph_derivatives(
    double, double) const {
    return failure(
        PropertyStatus::unsupported,
        "property package does not provide analytic p-h derivatives");
}

PhDerivativesResult state_ph_derivatives_with_fallback(
    const PropertyPackage& properties,
    double pressure,
    double enthalpy) {
    if (properties.supports(
            PropertyCapability::state_ph_derivatives)) {
        auto analytic = properties.state_ph_derivatives(
            pressure, enthalpy);
        if (analytic.ok() ||
            analytic.status != PropertyStatus::saturation_boundary) {
            return analytic;
        }
    }

    const auto base = properties.state_ph(pressure, enthalpy);
    if (!base.ok()) {
        return {
            {}, {}, PropertyDerivativeSource::finite_difference,
            base.status, base.message};
    }

    PhDerivativesResult result{
        base.state, {},
        PropertyDerivativeSource::finite_difference,
        PropertyStatus::success, {}};
    const double pressure_step =
        std::max(std::abs(pressure) * 1.0e-6, 1.0);
    const double enthalpy_step =
        std::max(std::abs(enthalpy) * 1.0e-6, 1.0e-3);
    const auto pressure_lower = properties.state_ph(
        pressure - pressure_step, enthalpy);
    const auto pressure_upper = properties.state_ph(
        pressure + pressure_step, enthalpy);
    const auto enthalpy_lower = properties.state_ph(
        pressure, enthalpy - enthalpy_step);
    const auto enthalpy_upper = properties.state_ph(
        pressure, enthalpy + enthalpy_step);
    const auto partial =
        [&](const PropertyResult& lower,
            const PropertyResult& upper, double step,
            auto extract, double& derivative)
            -> const PropertyResult* {
            const bool lower_same_phase = lower.ok() &&
                lower.state.phase == base.state.phase;
            const bool upper_same_phase = upper.ok() &&
                upper.state.phase == base.state.phase;
            if (lower_same_phase && upper_same_phase) {
                derivative =
                    (extract(upper.state) -
                     extract(lower.state)) /
                    (2.0 * step);
                return nullptr;
            }
            if (upper_same_phase) {
                derivative =
                    (extract(upper.state) -
                     extract(base.state)) /
                    step;
                return nullptr;
            }
            if (lower_same_phase) {
                derivative =
                    (extract(base.state) -
                     extract(lower.state)) /
                    step;
                return nullptr;
            }
            if (lower.ok() && upper.ok()) {
                derivative =
                    (extract(upper.state) -
                     extract(lower.state)) /
                    (2.0 * step);
                return nullptr;
            }
            if (upper.ok()) {
                derivative =
                    (extract(upper.state) -
                     extract(base.state)) /
                    step;
                return nullptr;
            }
            if (lower.ok()) {
                derivative =
                    (extract(base.state) -
                     extract(lower.state)) /
                    step;
                return nullptr;
            }
            return !upper.ok() ? &upper : &lower;
        };
    const auto temperature =
        [](const ThermodynamicState& state) {
            return state.temperature_k;
        };
    const auto density =
        [](const ThermodynamicState& state) {
            return state.density_kg_m3;
        };
    const auto internal_energy =
        [](const ThermodynamicState& state) {
            return state.internal_energy_j_kg;
        };
    const auto entropy =
        [](const ThermodynamicState& state) {
            return state.entropy_j_kg_k;
        };
    const auto vapor_quality =
        [](const ThermodynamicState& state) {
            return state.vapor_quality;
        };
    const PropertyResult* failed = nullptr;
    failed = partial(
        pressure_lower, pressure_upper, pressure_step,
        temperature,
        result.derivatives
            .temperature_wrt_pressure_at_enthalpy);
    if (failed == nullptr) {
        failed = partial(
            pressure_lower, pressure_upper, pressure_step,
            density,
            result.derivatives
                .density_wrt_pressure_at_enthalpy);
    }
    if (failed == nullptr) {
        failed = partial(
            pressure_lower, pressure_upper, pressure_step,
            internal_energy,
            result.derivatives
                .internal_energy_wrt_pressure_at_enthalpy);
    }
    if (failed == nullptr) {
        failed = partial(
            pressure_lower, pressure_upper, pressure_step,
            entropy,
            result.derivatives
                .entropy_wrt_pressure_at_enthalpy);
    }
    if (failed == nullptr) {
        failed = partial(
            pressure_lower, pressure_upper, pressure_step,
            vapor_quality,
            result.derivatives
                .vapor_quality_wrt_pressure_at_enthalpy);
    }
    if (failed == nullptr) {
        failed = partial(
            enthalpy_lower, enthalpy_upper, enthalpy_step,
            temperature,
            result.derivatives
                .temperature_wrt_enthalpy_at_pressure);
    }
    if (failed == nullptr) {
        failed = partial(
            enthalpy_lower, enthalpy_upper, enthalpy_step,
            density,
            result.derivatives
                .density_wrt_enthalpy_at_pressure);
    }
    if (failed == nullptr) {
        failed = partial(
            enthalpy_lower, enthalpy_upper, enthalpy_step,
            internal_energy,
            result.derivatives
                .internal_energy_wrt_enthalpy_at_pressure);
    }
    if (failed == nullptr) {
        failed = partial(
            enthalpy_lower, enthalpy_upper, enthalpy_step,
            entropy,
            result.derivatives
                .entropy_wrt_enthalpy_at_pressure);
    }
    if (failed == nullptr) {
        failed = partial(
            enthalpy_lower, enthalpy_upper, enthalpy_step,
            vapor_quality,
            result.derivatives
                .vapor_quality_wrt_enthalpy_at_pressure);
    }
    if (failed != nullptr) {
        return {
            {}, {}, result.source, failed->status,
            failed->message};
    }
    return result;
}

}  // namespace thermox::physics
