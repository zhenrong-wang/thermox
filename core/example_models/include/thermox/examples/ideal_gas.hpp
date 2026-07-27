#pragma once

namespace thermox::examples {

struct IdealGas {
    double cp{1004.5};        // J/(kg K)
    double gamma{1.4};        // cp/cv
    double gas_constant{287.0};

    double enthalpy_from_temperature(double temperature_k) const;
    double isentropic_temperature_out(double inlet_temperature_k,
                                      double pressure_ratio) const;
};

}  // namespace thermox::examples
