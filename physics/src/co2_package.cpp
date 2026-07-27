#include "thermox/physics/co2_package.hpp"

#include "thermox_co2.h"

namespace thermox::physics {
namespace {

Phase map_phase(thermox_co2_phase phase) {
    switch (phase) {
        case THERMOX_CO2_PHASE_LIQUID: return Phase::liquid;
        case THERMOX_CO2_PHASE_VAPOR: return Phase::vapor;
        case THERMOX_CO2_PHASE_SUPERCRITICAL: return Phase::supercritical;
        case THERMOX_CO2_PHASE_TWO_PHASE: return Phase::two_phase;
        default: return Phase::unknown;
    }
}

PropertyResult map_result(thermox_co2_status status, const thermox_co2_state& source) {
    if (status != THERMOX_CO2_OK) {
        switch (status) {
            case THERMOX_CO2_SATURATION:
                return {{}, PropertyStatus::saturation_boundary,
                        "CO2 PT state is ambiguous on the saturation boundary"};
            case THERMOX_CO2_INVALID_INPUT:
                return {{}, PropertyStatus::invalid_input, "invalid CO2 state input"};
            case THERMOX_CO2_OUT_OF_RANGE:
                return {{}, PropertyStatus::out_of_range, "CO2 state is outside EOS limits"};
            case THERMOX_CO2_NO_CONVERGENCE:
                return {{}, PropertyStatus::no_convergence,
                        "CO2 property iteration did not converge"};
            default:
                return {{}, PropertyStatus::backend_error, "unknown CO2 backend error"};
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

std::string_view Co2PropertyPackage::name() const noexcept { return "co2-span-wagner"; }
PropertyLimits Co2PropertyPackage::limits() const noexcept {
    return {0.0, 800e6, 216.0, 1100.0};
}
PropertyResult Co2PropertyPackage::state_pt(double pressure, double temperature) const {
    thermox_co2_state state{};
    return map_result(thermox_co2_state_pt(pressure, temperature, &state), state);
}
PropertyResult Co2PropertyPackage::state_ph(double pressure, double enthalpy) const {
    thermox_co2_state state{};
    return map_result(thermox_co2_state_ph(pressure, enthalpy, &state), state);
}
PropertyResult Co2PropertyPackage::state_ps(double pressure, double entropy) const {
    thermox_co2_state state{};
    return map_result(thermox_co2_state_ps(pressure, entropy, &state), state);
}

}  // namespace thermox::physics
