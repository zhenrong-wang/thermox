#include "thermox/examples/brayton_cycle.hpp"
#include "thermox/examples/ideal_gas.hpp"

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(double actual, double expected, double tolerance, const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message + ": actual=" + std::to_string(actual) +
                                 " expected=" + std::to_string(expected));
    }
}

void test_ideal_gas() {
    const thermox::examples::IdealGas air;
    require_near(air.enthalpy_from_temperature(300.0), 301350.0, 1.0e-9, "ideal gas h(T)");
    require_near(air.isentropic_temperature_out(300.0, 1.0), 300.0, 1.0e-12,
                 "isentropic pressure ratio one");
}

void test_brayton_cycle() {
    thermox::examples::BraytonCycleInput input;
    input.pressure_ratio = 12.0;
    input.turbine_inlet_temperature_k = 1400.0;
    const auto result = thermox::examples::solve_brayton_cycle(input);
    require(result.diagnostics.converged, result.diagnostics.message);
    require(result.net_power_w > 0.0, "Brayton net power should be positive");
    require(result.thermal_efficiency > 0.25 && result.thermal_efficiency < 0.5,
            "Brayton efficiency should be plausible");
    require_near(result.compressor_outlet_temperature_k, 634.5790109, 1.0e-3,
                 "Brayton compressor outlet temperature");
}

}  // namespace

int main() {
    try {
        test_ideal_gas();
        test_brayton_cycle();
        std::cout << "thermox_example_tests passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "thermox_example_tests failed: " << ex.what() << "\n";
        return 1;
    }
}
