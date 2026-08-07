#include "thermox/physics/water_heos_package.hpp"

#include "coolprop_backend.hpp"

namespace thermox::physics {

std::string_view WaterHeosPropertyPackage::name() const noexcept {
    return "coolprop-heos-water";
}

std::string_view WaterHeosPropertyPackage::version() const noexcept {
    return detail::coolprop_version();
}

PropertyLimits WaterHeosPropertyPackage::limits() const noexcept {
    return {611.6548008968684, 100e6, 273.16, 2000.0};
}

bool WaterHeosPropertyPackage::supports(
    PropertyCapability capability) const noexcept {
    switch (capability) {
        case PropertyCapability::state_pt:
        case PropertyCapability::state_ph:
        case PropertyCapability::state_ph_derivatives:
        case PropertyCapability::state_ps:
        case PropertyCapability::saturation_p:
        case PropertyCapability::transport:
        case PropertyCapability::surface_tension:
            return true;
    }
    return false;
}

PropertyResult WaterHeosPropertyPackage::state_pt(
    double pressure, double temperature) const {
    return detail::coolprop_state(
        detail::CoolPropFluid::water_heos, detail::CoolPropFlash::pt,
        pressure, temperature);
}

PropertyResult WaterHeosPropertyPackage::state_ph(
    double pressure, double enthalpy) const {
    return detail::coolprop_state(
        detail::CoolPropFluid::water_heos, detail::CoolPropFlash::ph,
        pressure, enthalpy);
}

PhDerivativesResult WaterHeosPropertyPackage::state_ph_derivatives(
    double pressure, double enthalpy) const {
    return detail::coolprop_state_ph_derivatives(
        detail::CoolPropFluid::water_heos, pressure, enthalpy);
}

PropertyResult WaterHeosPropertyPackage::state_ps(
    double pressure, double entropy) const {
    return detail::coolprop_state(
        detail::CoolPropFluid::water_heos, detail::CoolPropFlash::ps,
        pressure, entropy);
}

SaturationResult WaterHeosPropertyPackage::saturation_p(
    double pressure) const {
    return detail::coolprop_saturation_p(
        detail::CoolPropFluid::water_heos, pressure);
}

}  // namespace thermox::physics
