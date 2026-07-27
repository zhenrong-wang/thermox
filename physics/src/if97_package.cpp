#include "thermox/physics/if97_package.hpp"

#include "thermox_if97.h"

namespace thermox::physics {
namespace {

Phase map_phase(thermox_if97_phase phase) {
    switch (phase) {
        case THERMOX_IF97_PHASE_LIQUID: return Phase::liquid;
        case THERMOX_IF97_PHASE_VAPOR: return Phase::vapor;
        case THERMOX_IF97_PHASE_SUPERCRITICAL: return Phase::supercritical;
        case THERMOX_IF97_PHASE_TWO_PHASE: return Phase::two_phase;
        default: return Phase::unknown;
    }
}

PropertyResult map_result(thermox_if97_status status, const thermox_if97_state& source) {
    if (status != THERMOX_IF97_OK) {
        switch (status) {
            case THERMOX_IF97_SATURATION:
                return {{}, PropertyStatus::saturation_boundary,
                        "IF97 PT state is ambiguous on the saturation boundary"};
            case THERMOX_IF97_INVALID_INPUT:
                return {{}, PropertyStatus::invalid_input, "invalid IF97 state input"};
            case THERMOX_IF97_OUT_OF_RANGE:
                return {{}, PropertyStatus::out_of_range, "state is outside IF97 limits"};
            case THERMOX_IF97_NO_CONVERGENCE:
                return {{}, PropertyStatus::no_convergence,
                        "IF97 property iteration did not converge"};
            default:
                return {{}, PropertyStatus::backend_error, "unknown IF97 backend error"};
        }
    }
    ThermodynamicState state;
    state.pressure_pa = source.pressure_pa;
    state.temperature_k = source.temperature_k;
    state.density_kg_m3 = source.density_kg_m3;
    state.internal_energy_j_kg = source.internal_energy_j_kg;
    state.enthalpy_j_kg = source.enthalpy_j_kg;
    state.entropy_j_kg_k = source.entropy_j_kg_k;
    state.cv_j_kg_k = source.cv_j_kg_k;
    state.cp_j_kg_k = source.cp_j_kg_k;
    state.speed_of_sound_m_s = source.speed_of_sound_m_s;
    state.viscosity_pa_s = source.viscosity_pa_s;
    state.thermal_conductivity_w_m_k = source.thermal_conductivity_w_m_k;
    state.vapor_quality = source.vapor_quality;
    state.phase = map_phase(source.phase);
    return {state, PropertyStatus::success, {}};
}

}  // namespace

std::string_view If97PropertyPackage::name() const noexcept { return "water-steam-if97"; }
PropertyLimits If97PropertyPackage::limits() const noexcept {
    return {0.0, 100e6, 273.15, 2273.15};
}
bool If97PropertyPackage::supports(PropertyCapability) const noexcept { return true; }
PropertyResult If97PropertyPackage::state_pt(double pressure, double temperature) const {
    thermox_if97_state state{};
    return map_result(thermox_if97_state_pt(pressure, temperature, &state), state);
}
PropertyResult If97PropertyPackage::state_ph(double pressure, double enthalpy) const {
    thermox_if97_state state{};
    return map_result(thermox_if97_state_ph(pressure, enthalpy, &state), state);
}
PropertyResult If97PropertyPackage::state_ps(double pressure, double entropy) const {
    thermox_if97_state state{};
    return map_result(thermox_if97_state_ps(pressure, entropy, &state), state);
}
SaturationResult If97PropertyPackage::saturation_p(double pressure) const {
    thermox_if97_state liquid{};
    thermox_if97_state vapor{};
    const auto status =
        thermox_if97_saturation_p(pressure, &liquid, &vapor);
    if (status != THERMOX_IF97_OK) {
        const auto mapped = map_result(status, liquid);
        return {{}, {}, mapped.status,
                status == THERMOX_IF97_OUT_OF_RANGE
                    ? "pressure is outside the IF97 saturation-pair range"
                    : mapped.message};
    }
    const auto mapped_liquid = map_result(status, liquid);
    const auto mapped_vapor = map_result(status, vapor);
    return {mapped_liquid.state, mapped_vapor.state,
            PropertyStatus::success, {}};
}

}  // namespace thermox::physics
