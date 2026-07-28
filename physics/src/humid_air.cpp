#include "thermox/physics/humid_air.hpp"

#include "CoolProp/HumidAirProp.h"

#include <cmath>
#include <exception>
#include <utility>

namespace thermox::physics {

HumidAirResult humid_air_state_ptrh(
    double pressure,
    double temperature,
    double relative_humidity) {
    if (!std::isfinite(pressure) ||
        !std::isfinite(temperature) ||
        !std::isfinite(relative_humidity) ||
        pressure <= 0.0 || temperature <= 0.0 ||
        relative_humidity < 0.0 ||
        relative_humidity > 1.0) {
        return {
            {},
            PropertyStatus::invalid_input,
            "humid-air pressure and temperature must be "
            "positive and relative humidity must be in [0, 1]",
        };
    }
    try {
        const auto property =
            [&](const char* output) {
                return HumidAir::HAPropsSI(
                    output, "P", pressure, "T", temperature,
                    "R", relative_humidity);
            };
        HumidAirState result;
        auto& state = result.thermodynamic;
        result.relative_humidity = relative_humidity;
        result.humidity_ratio_kg_water_kg_dry_air =
            property("W");
        result.water_mass_fraction =
            result.humidity_ratio_kg_water_kg_dry_air /
            (1.0 +
             result.humidity_ratio_kg_water_kg_dry_air);
        state.pressure_pa = pressure;
        state.temperature_k = temperature;
        state.enthalpy_j_kg = property("Hha");
        state.internal_energy_j_kg = property("Uha");
        state.entropy_j_kg_k = property("Sha");
        state.cp_j_kg_k = property("Cha");
        state.density_kg_m3 = 1.0 / property("Vha");
        state.viscosity_pa_s = property("M");
        state.thermal_conductivity_w_m_k = property("K");
        state.phase = Phase::vapor;
        if (!std::isfinite(state.enthalpy_j_kg) ||
            !std::isfinite(state.entropy_j_kg_k) ||
            !std::isfinite(state.density_kg_m3) ||
            !std::isfinite(state.cp_j_kg_k) ||
            !std::isfinite(
                result.humidity_ratio_kg_water_kg_dry_air)) {
            return {
                {},
                PropertyStatus::backend_error,
                "CoolProp returned a non-finite humid-air "
                "property",
            };
        }
        return {
            std::move(result),
            PropertyStatus::success,
            {},
        };
    } catch (const std::exception& error) {
        return {
            {},
            PropertyStatus::backend_error,
            error.what(),
        };
    }
}

}  // namespace thermox::physics
