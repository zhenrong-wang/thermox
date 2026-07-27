#include "thermox/examples/ideal_gas.hpp"

#include <cmath>
#include <stdexcept>

namespace thermox::examples {

double IdealGas::enthalpy_from_temperature(double temperature_k) const {
    return cp * temperature_k;
}

double IdealGas::isentropic_temperature_out(double inlet_temperature_k,
                                            double pressure_ratio) const {
    if (inlet_temperature_k <= 0.0) {
        throw std::invalid_argument("inlet temperature must be positive");
    }
    if (pressure_ratio <= 0.0) {
        throw std::invalid_argument("pressure ratio must be positive");
    }
    return inlet_temperature_k * std::pow(pressure_ratio, (gamma - 1.0) / gamma);
}

}  // namespace thermox::examples
