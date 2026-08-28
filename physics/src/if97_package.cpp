#include "thermox/physics/if97_package.hpp"

#include "coolprop_backend.hpp"

namespace thermox::physics {

std::string_view If97PropertyPackage::name() const noexcept {
    return "coolprop-if97";
}

std::string_view If97PropertyPackage::version() const noexcept {
    return detail::coolprop_version();
}

PropertyLimits If97PropertyPackage::limits() const noexcept {
    return {611.657, 100e6, 273.15, 2273.15};
}

bool If97PropertyPackage::supports(
    PropertyCapability capability) const noexcept {
    switch (capability) {
        case PropertyCapability::state_pt:
        case PropertyCapability::state_ph:
        case PropertyCapability::state_ps:
        case PropertyCapability::saturation_p:
        case PropertyCapability::transport:
        case PropertyCapability::surface_tension:
            return true;
        case PropertyCapability::state_ph_derivatives:
            return false;
    }
    return false;
}

PropertyResult If97PropertyPackage::state_pt(
    double pressure, double temperature) const {
    return detail::coolprop_state(
        {"IF97", "Water"}, detail::CoolPropFlash::pt,
        pressure, temperature);
}

PropertyResult If97PropertyPackage::state_ph(
    double pressure, double enthalpy) const {
    return detail::coolprop_state(
        {"IF97", "Water"}, detail::CoolPropFlash::ph,
        pressure, enthalpy);
}

PropertyResult If97PropertyPackage::state_ps(
    double pressure, double entropy) const {
    return detail::coolprop_state(
        {"IF97", "Water"}, detail::CoolPropFlash::ps,
        pressure, entropy);
}

SaturationResult If97PropertyPackage::saturation_p(double pressure) const {
    return detail::coolprop_saturation_p(
        {"IF97", "Water"}, pressure);
}

}  // namespace thermox::physics
