#pragma once

#include "thermox/physics/property_package.hpp"

namespace thermox::physics {

struct HumidAirState {
    ThermodynamicState thermodynamic;
    double relative_humidity{0.0};
    double humidity_ratio_kg_water_kg_dry_air{0.0};
    double water_mass_fraction{0.0};
};

struct HumidAirResult {
    HumidAirState state;
    PropertyStatus status{PropertyStatus::backend_error};
    std::string message;

    [[nodiscard]] bool ok() const {
        return status == PropertyStatus::success;
    }
};

// Ambient moist-air state on a per-kilogram-humid-air basis.
//
// Humidity ratio is kg water / kg dry air. It can be converted into
// species composition and held fixed through a non-condensing compressor
// while a composition-aware thermochemistry backend evaluates higher
// temperatures.
[[nodiscard]] HumidAirResult humid_air_state_ptrh(
    double pressure_pa,
    double dry_bulb_temperature_k,
    double relative_humidity);

}  // namespace thermox::physics
