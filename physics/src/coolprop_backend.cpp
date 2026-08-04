#include "coolprop_backend.hpp"

#include "CoolProp/AbstractState.h"
#include "CoolProp/CoolProp.h"
#include "CoolProp/Exceptions.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <string>

namespace thermox::physics::detail {
namespace {

using StatePtr = std::unique_ptr<CoolProp::AbstractState>;

CoolProp::AbstractState& backend_state(CoolPropFluid fluid) {
    thread_local StatePtr co2{
        CoolProp::AbstractState::factory("HEOS", "CO2")};
    thread_local StatePtr water{
        CoolProp::AbstractState::factory("IF97", "Water")};
    thread_local StatePtr water_heos{
        CoolProp::AbstractState::factory("HEOS", "Water")};
    switch (fluid) {
        case CoolPropFluid::co2:
            return *co2;
        case CoolPropFluid::water_if97:
            return *water;
        case CoolPropFluid::water_heos:
            return *water_heos;
    }
    return *water;
}

Phase map_phase(CoolProp::phases phase) {
    switch (phase) {
        case CoolProp::iphase_liquid:
            return Phase::liquid;
        case CoolProp::iphase_gas:
            return Phase::vapor;
        case CoolProp::iphase_supercritical:
        case CoolProp::iphase_supercritical_gas:
        case CoolProp::iphase_supercritical_liquid:
        case CoolProp::iphase_critical_point:
            return Phase::supercritical;
        case CoolProp::iphase_twophase:
            return Phase::two_phase;
        default:
            return Phase::unknown;
    }
}

template <typename Getter>
double optional_output(Getter&& getter) {
    try {
        const double value = getter();
        return std::isfinite(value) ? value : 0.0;
    } catch (const CoolProp::CoolPropBaseError&) {
        return 0.0;
    }
}

ThermodynamicState read_state(CoolProp::AbstractState& source) {
    ThermodynamicState state;
    state.pressure_pa = source.p();
    state.temperature_k = source.T();
    state.density_kg_m3 = source.rhomass();
    state.internal_energy_j_kg = source.umass();
    state.enthalpy_j_kg = source.hmass();
    state.entropy_j_kg_k = source.smass();
    state.cv_j_kg_k =
        optional_output([&source] { return source.cvmass(); });
    state.cp_j_kg_k =
        optional_output([&source] { return source.cpmass(); });
    state.speed_of_sound_m_s =
        optional_output([&source] { return source.speed_sound(); });
    state.viscosity_pa_s =
        optional_output([&source] { return source.viscosity(); });
    state.thermal_conductivity_w_m_k =
        optional_output([&source] { return source.conductivity(); });
    state.vapor_quality = source.Q();
    state.phase = map_phase(source.phase());
    return state;
}

bool contains_case_insensitive(std::string text, std::string_view needle) {
    std::transform(
        text.begin(), text.end(), text.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return text.find(needle) != std::string::npos;
}

PropertyResult error_result(PropertyStatus status, const std::string& message) {
    return {{}, status, message};
}

PropertyResult map_exception(CoolProp::CoolPropBaseError& error) {
    const std::string message = error.what();
    if (contains_case_insensitive(message, "out of range") ||
        contains_case_insensitive(message, "outside the range") ||
        contains_case_insensitive(message, "below the minimum") ||
        contains_case_insensitive(message, "above the maximum")) {
        return error_result(PropertyStatus::out_of_range, message);
    }
    if (contains_case_insensitive(message, "saturation") ||
        contains_case_insensitive(message, "two-phase")) {
        return error_result(PropertyStatus::saturation_boundary, message);
    }
    switch (error.code()) {
        case CoolProp::CoolPropBaseError::eOutOfRange:
        case CoolProp::CoolPropBaseError::eValue:
        case CoolProp::CoolPropBaseError::eInput:
            return error_result(PropertyStatus::out_of_range, message);
        case CoolProp::CoolPropBaseError::eSolution:
        case CoolProp::CoolPropBaseError::eMultipleSolutions:
            return error_result(PropertyStatus::no_convergence, message);
        case CoolProp::CoolPropBaseError::eNotImplemented:
        case CoolProp::CoolPropBaseError::eNotAvailable:
            return error_result(PropertyStatus::unsupported, message);
        default:
            return error_result(PropertyStatus::backend_error, message);
    }
}

bool valid_input(double first, double second) {
    return std::isfinite(first) && std::isfinite(second) && first > 0.0;
}

bool outside_limits(
    CoolPropFluid fluid, CoolPropFlash flash,
    double pressure, double second) {
    const double maximum_pressure =
        fluid == CoolPropFluid::co2 ? 800e6 : 100e6;
    if (pressure > maximum_pressure) return true;
    if (flash != CoolPropFlash::pt) return false;
    if (fluid == CoolPropFluid::co2)
        return second < 216.592 || second > 2000.0;
    if (fluid == CoolPropFluid::water_heos)
        return second < 273.16 || second > 2000.0;
    return second < 273.15 || second > 2273.15;
}

CoolProp::input_pairs input_pair(CoolPropFlash flash) {
    switch (flash) {
        case CoolPropFlash::pt:
            return CoolProp::PT_INPUTS;
        case CoolPropFlash::ph:
            return CoolProp::HmassP_INPUTS;
        case CoolPropFlash::ps:
            return CoolProp::PSmass_INPUTS;
    }
    return CoolProp::PT_INPUTS;
}

void update(
    CoolProp::AbstractState& state, CoolPropFlash flash,
    double first, double second) {
    if (flash == CoolPropFlash::ph) {
        state.update(input_pair(flash), second, first);
    } else {
        state.update(input_pair(flash), first, second);
    }
}

}  // namespace

PropertyResult coolprop_state(
    CoolPropFluid fluid, CoolPropFlash flash, double first, double second) {
    if (!valid_input(first, second)) {
        return error_result(
            PropertyStatus::invalid_input,
            "property inputs must be finite and pressure must be positive");
    }
    if (outside_limits(fluid, flash, first, second)) {
        return error_result(
            PropertyStatus::out_of_range,
            "property inputs are outside the backend validity limits");
    }
    try {
        auto& state = backend_state(fluid);
        update(state, flash, first, second);
        return {read_state(state), PropertyStatus::success, {}};
    } catch (CoolProp::CoolPropBaseError& error) {
        return map_exception(error);
    } catch (const std::exception& error) {
        const std::string message = error.what();
        if (contains_case_insensitive(message, "out of range") ||
            contains_case_insensitive(message, "outside the range") ||
            contains_case_insensitive(message, "below the minimum") ||
            contains_case_insensitive(message, "above the maximum")) {
            return error_result(
                PropertyStatus::out_of_range, message);
        }
        return error_result(PropertyStatus::backend_error, message);
    }
}

PhDerivativesResult coolprop_state_ph_derivatives(
    CoolPropFluid fluid, double pressure, double enthalpy) {
    if (!valid_input(pressure, enthalpy)) {
        return {
            {}, {}, PropertyDerivativeSource::analytic,
            PropertyStatus::invalid_input,
            "property inputs must be finite and pressure must be positive"};
    }
    if (outside_limits(
            fluid, CoolPropFlash::ph, pressure, enthalpy)) {
        return {
            {}, {}, PropertyDerivativeSource::analytic,
            PropertyStatus::out_of_range,
            "property inputs are outside the backend validity limits"};
    }
    try {
        auto& state = backend_state(fluid);
        update(
            state, CoolPropFlash::ph, pressure, enthalpy);
        const auto thermodynamic_state = read_state(state);
        if (thermodynamic_state.phase == Phase::two_phase) {
            return {
                thermodynamic_state, {},
                PropertyDerivativeSource::analytic,
                PropertyStatus::saturation_boundary,
                "single-phase p-h derivatives are undefined in the "
                "two-phase region"};
        }
        PhStateDerivatives derivatives;
        derivatives.temperature_wrt_pressure_at_enthalpy =
            state.first_partial_deriv(
                CoolProp::iT, CoolProp::iP,
                CoolProp::iHmass);
        derivatives.temperature_wrt_enthalpy_at_pressure =
            state.first_partial_deriv(
                CoolProp::iT, CoolProp::iHmass,
                CoolProp::iP);
        derivatives.density_wrt_pressure_at_enthalpy =
            state.first_partial_deriv(
                CoolProp::iDmass, CoolProp::iP,
                CoolProp::iHmass);
        derivatives.density_wrt_enthalpy_at_pressure =
            state.first_partial_deriv(
                CoolProp::iDmass, CoolProp::iHmass,
                CoolProp::iP);
        derivatives.internal_energy_wrt_pressure_at_enthalpy =
            state.first_partial_deriv(
                CoolProp::iUmass, CoolProp::iP,
                CoolProp::iHmass);
        derivatives.internal_energy_wrt_enthalpy_at_pressure =
            state.first_partial_deriv(
                CoolProp::iUmass, CoolProp::iHmass,
                CoolProp::iP);
        const double values[]{
            derivatives.temperature_wrt_pressure_at_enthalpy,
            derivatives.temperature_wrt_enthalpy_at_pressure,
            derivatives.density_wrt_pressure_at_enthalpy,
            derivatives.density_wrt_enthalpy_at_pressure,
            derivatives.internal_energy_wrt_pressure_at_enthalpy,
            derivatives.internal_energy_wrt_enthalpy_at_pressure};
        if (!std::all_of(
                std::begin(values), std::end(values),
                [](double value) { return std::isfinite(value); })) {
            return {
                thermodynamic_state, {},
                PropertyDerivativeSource::analytic,
                PropertyStatus::backend_error,
                "property backend returned non-finite p-h derivatives"};
        }
        return {
            thermodynamic_state, derivatives,
            PropertyDerivativeSource::analytic,
            PropertyStatus::success, {}};
    } catch (CoolProp::CoolPropBaseError& error) {
        const auto mapped = map_exception(error);
        return {
            {}, {}, PropertyDerivativeSource::analytic,
            mapped.status, mapped.message};
    } catch (const std::exception& error) {
        return {
            {}, {}, PropertyDerivativeSource::analytic,
            PropertyStatus::backend_error, error.what()};
    }
}

SaturationResult coolprop_saturation_p(
    CoolPropFluid fluid, double pressure_pa) {
    if (!std::isfinite(pressure_pa) || pressure_pa <= 0.0) {
        return {{}, {}, PropertyStatus::invalid_input,
                "saturation pressure must be finite and positive"};
    }
    try {
        auto& state = backend_state(fluid);
        const double triple_pressure = state.p_triple();
        const double critical_pressure = state.p_critical();
        if (pressure_pa < triple_pressure || pressure_pa >= critical_pressure) {
            return {{}, {}, PropertyStatus::out_of_range,
                    "pressure must be between the triple and critical pressures"};
        }

        state.update(CoolProp::PQ_INPUTS, pressure_pa, 0.0);
        auto liquid = read_state(state);
        liquid.vapor_quality = 0.0;
        liquid.phase = Phase::liquid;

        state.update(CoolProp::PQ_INPUTS, pressure_pa, 1.0);
        auto vapor = read_state(state);
        vapor.vapor_quality = 1.0;
        vapor.phase = Phase::vapor;
        return {liquid, vapor, PropertyStatus::success, {}};
    } catch (CoolProp::CoolPropBaseError& error) {
        const auto mapped = map_exception(error);
        return {{}, {}, mapped.status, mapped.message};
    } catch (const std::exception& error) {
        return {{}, {}, PropertyStatus::backend_error, error.what()};
    }
}

std::string_view coolprop_version() noexcept {
    static const std::string version = []() noexcept {
        try {
            return CoolProp::get_global_param_string("version");
        } catch (...) {
            return std::string{"unknown"};
        }
    }();
    return version;
}

}  // namespace thermox::physics::detail
