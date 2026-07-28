#include "thermox/physics/if97_package.hpp"

#include "coolprop_backend.hpp"

namespace thermox::physics {

std::string_view If97PropertyPackage::name() const noexcept {
    return "coolprop-heos-water";
}

std::string_view If97PropertyPackage::version() const noexcept {
    return detail::coolprop_version();
}

PropertyLimits If97PropertyPackage::limits() const noexcept {
    return {611.654, 100e6, 273.16, 2000.0};
}

bool If97PropertyPackage::supports(PropertyCapability) const noexcept {
    return true;
}

PropertyResult If97PropertyPackage::state_pt(
    double pressure, double temperature) const {
    return detail::coolprop_state(
        detail::CoolPropFluid::water, detail::CoolPropFlash::pt,
        pressure, temperature);
}

PropertyResult If97PropertyPackage::state_ph(
    double pressure, double enthalpy) const {
    return detail::coolprop_state(
        detail::CoolPropFluid::water, detail::CoolPropFlash::ph,
        pressure, enthalpy);
}

PropertyResult If97PropertyPackage::state_ps(
    double pressure, double entropy) const {
    return detail::coolprop_state(
        detail::CoolPropFluid::water, detail::CoolPropFlash::ps,
        pressure, entropy);
}

SaturationResult If97PropertyPackage::saturation_p(double pressure) const {
    return detail::coolprop_saturation_p(
        detail::CoolPropFluid::water, pressure);
}

}  // namespace thermox::physics
