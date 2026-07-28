#include "thermox/physics/co2_package.hpp"

#include "coolprop_backend.hpp"

namespace thermox::physics {

std::string_view Co2PropertyPackage::name() const noexcept {
    return "coolprop-heos-co2";
}

std::string_view Co2PropertyPackage::version() const noexcept {
    return detail::coolprop_version();
}

PropertyLimits Co2PropertyPackage::limits() const noexcept {
    return {0.0, 800e6, 216.592, 2000.0};
}

bool Co2PropertyPackage::supports(PropertyCapability) const noexcept {
    return true;
}

PropertyResult Co2PropertyPackage::state_pt(
    double pressure, double temperature) const {
    return detail::coolprop_state(
        detail::CoolPropFluid::co2, detail::CoolPropFlash::pt,
        pressure, temperature);
}

PropertyResult Co2PropertyPackage::state_ph(
    double pressure, double enthalpy) const {
    return detail::coolprop_state(
        detail::CoolPropFluid::co2, detail::CoolPropFlash::ph,
        pressure, enthalpy);
}

PropertyResult Co2PropertyPackage::state_ps(
    double pressure, double entropy) const {
    return detail::coolprop_state(
        detail::CoolPropFluid::co2, detail::CoolPropFlash::ps,
        pressure, entropy);
}

SaturationResult Co2PropertyPackage::saturation_p(double pressure) const {
    return detail::coolprop_saturation_p(
        detail::CoolPropFluid::co2, pressure);
}

}  // namespace thermox::physics
