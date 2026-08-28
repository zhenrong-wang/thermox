#include "thermox/physics/coolprop_heos_package.hpp"

#include "coolprop_backend.hpp"

#include <stdexcept>
#include <utility>

namespace thermox::physics {

CoolPropHeosPropertyPackage::CoolPropHeosPropertyPackage(
    std::string substance)
    : substance_(std::move(substance)) {
    if (substance_.empty()) {
        throw std::invalid_argument(
            "CoolProp HEOS substance must be non-empty");
    }
    limits_ = detail::coolprop_limits({"HEOS", substance_});
}

std::string_view CoolPropHeosPropertyPackage::name() const noexcept {
    return "coolprop-heos";
}

std::string_view CoolPropHeosPropertyPackage::version() const noexcept {
    return detail::coolprop_version();
}

PropertyLimits CoolPropHeosPropertyPackage::limits() const noexcept {
    return limits_;
}

bool CoolPropHeosPropertyPackage::supports(
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

PropertyResult CoolPropHeosPropertyPackage::state_pt(
    double pressure, double temperature) const {
    return detail::coolprop_state(
        {"HEOS", substance_}, detail::CoolPropFlash::pt,
        pressure, temperature);
}

PropertyResult CoolPropHeosPropertyPackage::state_ph(
    double pressure, double enthalpy) const {
    return detail::coolprop_state(
        {"HEOS", substance_}, detail::CoolPropFlash::ph,
        pressure, enthalpy);
}

PhDerivativesResult
CoolPropHeosPropertyPackage::state_ph_derivatives(
    double pressure, double enthalpy) const {
    return detail::coolprop_state_ph_derivatives(
        {"HEOS", substance_}, pressure, enthalpy);
}

PropertyResult CoolPropHeosPropertyPackage::state_ps(
    double pressure, double entropy) const {
    return detail::coolprop_state(
        {"HEOS", substance_}, detail::CoolPropFlash::ps,
        pressure, entropy);
}

SaturationResult CoolPropHeosPropertyPackage::saturation_p(
    double pressure) const {
    return detail::coolprop_saturation_p(
        {"HEOS", substance_}, pressure);
}

std::string_view CoolPropHeosPropertyPackage::substance() const noexcept {
    return substance_;
}

}  // namespace thermox::physics
