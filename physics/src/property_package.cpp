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
    const auto cp =
        [](const ThermodynamicState& state) {
            return state.cp_j_kg_k;
        };
    const auto cv =
        [](const ThermodynamicState& state) {
            return state.cv_j_kg_k;
        };
    const auto speed_of_sound =
        [](const ThermodynamicState& state) {
            return state.speed_of_sound_m_s;
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
            pressure_lower, pressure_upper, pressure_step,
            cp,
            result.derivatives
                .cp_wrt_pressure_at_enthalpy);
    }
    if (failed == nullptr) {
        failed = partial(
            pressure_lower, pressure_upper, pressure_step,
            cv,
            result.derivatives
                .cv_wrt_pressure_at_enthalpy);
    }
    if (failed == nullptr) {
        failed = partial(
            pressure_lower, pressure_upper, pressure_step,
            speed_of_sound,
            result.derivatives
                .speed_of_sound_wrt_pressure_at_enthalpy);
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
    if (failed == nullptr) {
        failed = partial(
            enthalpy_lower, enthalpy_upper, enthalpy_step,
            cp,
            result.derivatives
                .cp_wrt_enthalpy_at_pressure);
    }
    if (failed == nullptr) {
        failed = partial(
            enthalpy_lower, enthalpy_upper, enthalpy_step,
            cv,
            result.derivatives
                .cv_wrt_enthalpy_at_pressure);
    }
    if (failed == nullptr) {
        failed = partial(
            enthalpy_lower, enthalpy_upper, enthalpy_step,
            speed_of_sound,
            result.derivatives
                .speed_of_sound_wrt_enthalpy_at_pressure);
    }
    if (failed != nullptr) {
        return {
            {}, {}, result.source, failed->status,
            failed->message};
    }
    return result;
}

PhTransportDerivativesResult
state_ph_transport_derivatives_with_fallback(
    const PropertyPackage& properties,
    double pressure,
    double enthalpy) {
    if (!properties.supports(PropertyCapability::state_ph) ||
        !properties.supports(PropertyCapability::transport)) {
        return {
            {}, {}, PropertyDerivativeSource::finite_difference,
            PropertyStatus::unsupported,
            "property package does not provide p-h transport properties"};
    }

    const auto base = properties.state_ph(pressure, enthalpy);
    if (!base.ok()) {
        return {
            {}, {}, PropertyDerivativeSource::finite_difference,
            base.status, base.message};
    }
    const auto valid_transport = [](const ThermodynamicState& state) {
        return std::isfinite(state.viscosity_pa_s) &&
            state.viscosity_pa_s > 0.0 &&
            std::isfinite(state.thermal_conductivity_w_m_k) &&
            state.thermal_conductivity_w_m_k > 0.0;
    };
    if (!valid_transport(base.state)) {
        return {
            base.state, {},
            PropertyDerivativeSource::finite_difference,
            PropertyStatus::backend_error,
            "property package returned non-positive or non-finite "
            "transport properties"};
    }

    PhTransportDerivativesResult result{
        base.state, {}, PropertyDerivativeSource::finite_difference,
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
    const auto usable = [&](const PropertyResult& candidate) {
        return candidate.ok() && valid_transport(candidate.state) &&
            candidate.state.phase == base.state.phase;
    };
    const auto partial = [&](const PropertyResult& lower,
                             const PropertyResult& upper,
                             double step, auto extract,
                             double& derivative) {
        if (usable(lower) && usable(upper)) {
            derivative =
                (extract(upper.state) - extract(lower.state)) /
                (2.0 * step);
            return true;
        }
        if (usable(upper)) {
            derivative =
                (extract(upper.state) - extract(base.state)) / step;
            return true;
        }
        if (usable(lower)) {
            derivative =
                (extract(base.state) - extract(lower.state)) / step;
            return true;
        }
        return false;
    };
    const auto viscosity = [](const ThermodynamicState& state) {
        return state.viscosity_pa_s;
    };
    const auto conductivity = [](const ThermodynamicState& state) {
        return state.thermal_conductivity_w_m_k;
    };
    const bool complete =
        partial(
            pressure_lower, pressure_upper, pressure_step, viscosity,
            result.derivatives
                .viscosity_wrt_pressure_at_enthalpy) &&
        partial(
            enthalpy_lower, enthalpy_upper, enthalpy_step, viscosity,
            result.derivatives
                .viscosity_wrt_enthalpy_at_pressure) &&
        partial(
            pressure_lower, pressure_upper, pressure_step, conductivity,
            result.derivatives
                .thermal_conductivity_wrt_pressure_at_enthalpy) &&
        partial(
            enthalpy_lower, enthalpy_upper, enthalpy_step, conductivity,
            result.derivatives
                .thermal_conductivity_wrt_enthalpy_at_pressure);
    if (!complete) {
        result.status = PropertyStatus::out_of_range;
        result.message =
            "unable to construct a same-phase p-h transport-property "
            "derivative stencil";
        return result;
    }
    const double derivatives[]{
        result.derivatives.viscosity_wrt_pressure_at_enthalpy,
        result.derivatives.viscosity_wrt_enthalpy_at_pressure,
        result.derivatives
            .thermal_conductivity_wrt_pressure_at_enthalpy,
        result.derivatives
            .thermal_conductivity_wrt_enthalpy_at_pressure,
    };
    if (!std::all_of(
            std::begin(derivatives), std::end(derivatives),
            [](double value) { return std::isfinite(value); })) {
        result.status = PropertyStatus::backend_error;
        result.message =
            "p-h transport-property derivative is not finite";
    }
    return result;
}

}  // namespace thermox::physics
