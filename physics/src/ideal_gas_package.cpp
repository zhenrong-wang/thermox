#include "thermox/physics/ideal_gas_package.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace thermox::physics {

IdealGasPropertyPackage::IdealGasPropertyPackage(
    double cp, double gas_constant, double reference_pressure, double reference_temperature)
    : cp_(cp),
      gas_constant_(gas_constant),
      reference_pressure_(reference_pressure),
      reference_temperature_(reference_temperature) {
    if (!(cp_ > gas_constant_ && gas_constant_ > 0.0) ||
        reference_pressure_ <= 0.0 || reference_temperature_ <= 0.0) {
        throw std::invalid_argument("invalid ideal-gas property constants");
    }
}

std::string_view IdealGasPropertyPackage::name() const noexcept { return "ideal-gas"; }
std::string_view IdealGasPropertyPackage::version() const noexcept { return "1.0.0"; }

PropertyLimits IdealGasPropertyPackage::limits() const noexcept {
    return {std::numeric_limits<double>::min(), std::numeric_limits<double>::max(),
            std::numeric_limits<double>::min(), std::numeric_limits<double>::max()};
}

bool IdealGasPropertyPackage::supports(PropertyCapability capability) const noexcept {
    switch (capability) {
        case PropertyCapability::state_pt:
        case PropertyCapability::state_ph:
        case PropertyCapability::state_ph_derivatives:
        case PropertyCapability::state_ps:
            return true;
        case PropertyCapability::saturation_p:
        case PropertyCapability::transport:
        case PropertyCapability::surface_tension:
            return false;
    }
    return false;
}

PropertyResult IdealGasPropertyPackage::state_pt(double pressure, double temperature) const {
    if (!std::isfinite(pressure) || !std::isfinite(temperature) ||
        pressure <= 0.0 || temperature <= 0.0) {
        return {{}, PropertyStatus::invalid_input,
                "ideal-gas pressure and temperature must be finite and positive"};
    }
    const double cv = cp_ - gas_constant_;
    const double gamma = cp_ / cv;
    ThermodynamicState state;
    state.pressure_pa = pressure;
    state.temperature_k = temperature;
    state.density_kg_m3 = pressure / (gas_constant_ * temperature);
    state.internal_energy_j_kg = cv * temperature;
    state.enthalpy_j_kg = cp_ * temperature;
    state.entropy_j_kg_k = cp_ * std::log(temperature / reference_temperature_) -
                           gas_constant_ * std::log(pressure / reference_pressure_);
    state.cv_j_kg_k = cv;
    state.cp_j_kg_k = cp_;
    state.speed_of_sound_m_s = std::sqrt(gamma * gas_constant_ * temperature);
    state.vapor_quality = 1.0;
    state.phase = Phase::vapor;
    return {state, PropertyStatus::success, {}};
}

PropertyResult IdealGasPropertyPackage::state_ph(double pressure, double enthalpy) const {
    if (!std::isfinite(enthalpy) || enthalpy <= 0.0) {
        return {{}, PropertyStatus::invalid_input,
                "ideal-gas enthalpy must be finite and positive"};
    }
    return state_pt(pressure, enthalpy / cp_);
}

PhDerivativesResult
IdealGasPropertyPackage::state_ph_derivatives(
    double pressure, double enthalpy) const {
    const auto state = state_ph(pressure, enthalpy);
    if (!state.ok()) {
        return {{}, {}, PropertyDerivativeSource::analytic,
                state.status, state.message};
    }
    const double cv = cp_ - gas_constant_;
    PhStateDerivatives derivatives;
    derivatives.temperature_wrt_pressure_at_enthalpy = 0.0;
    derivatives.temperature_wrt_enthalpy_at_pressure =
        1.0 / cp_;
    derivatives.density_wrt_pressure_at_enthalpy =
        state.state.density_kg_m3 / pressure;
    derivatives.density_wrt_enthalpy_at_pressure =
        -state.state.density_kg_m3 / enthalpy;
    derivatives.internal_energy_wrt_pressure_at_enthalpy =
        0.0;
    derivatives.internal_energy_wrt_enthalpy_at_pressure =
        cv / cp_;
    derivatives.entropy_wrt_pressure_at_enthalpy =
        -gas_constant_ / pressure;
    derivatives.entropy_wrt_enthalpy_at_pressure =
        1.0 / state.state.temperature_k;
    derivatives.speed_of_sound_wrt_pressure_at_enthalpy = 0.0;
    derivatives.speed_of_sound_wrt_enthalpy_at_pressure =
        state.state.speed_of_sound_m_s / (2.0 * enthalpy);
    return {
        state.state, derivatives,
        PropertyDerivativeSource::analytic,
        PropertyStatus::success, {}};
}

PropertyResult IdealGasPropertyPackage::state_ps(double pressure, double entropy) const {
    if (!std::isfinite(pressure) || !std::isfinite(entropy) || pressure <= 0.0) {
        return {{}, PropertyStatus::invalid_input,
                "ideal-gas pressure must be finite and positive and entropy finite"};
    }
    const double temperature =
        reference_temperature_ *
        std::exp((entropy + gas_constant_ * std::log(pressure / reference_pressure_)) / cp_);
    return state_pt(pressure, temperature);
}

SaturationResult IdealGasPropertyPackage::saturation_p(double) const {
    return {{}, {}, PropertyStatus::unsupported,
            "ideal-gas properties do not define a saturation curve"};
}

}  // namespace thermox::physics
