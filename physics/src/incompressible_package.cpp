#include "thermox/physics/incompressible_package.hpp"

#include "coolprop_backend.hpp"

#include <stdexcept>
#include <utility>

namespace thermox::physics {

namespace {

std::string canonical_substance(std::string substance) {
    if (substance == "SolarSalt") return "NaK";
    if (substance == "NaK") return substance;
    throw std::invalid_argument(
        "CoolProp incompressible backend does not support substance: " +
        substance);
}

}  // namespace

IncompressiblePropertyPackage::IncompressiblePropertyPackage(
    std::string substance)
    : substance_(canonical_substance(std::move(substance))),
      // CoolProp's NaK correlation is the 60 mass-% NaNO3 / 40 mass-%
      // KNO3 heat-transfer salt fitted over 573.15--873.15 K. Pressure
      // dependence is retained by CoolProp; 100 MPa is a conservative
      // platform guard, not a claimed correlation accuracy limit.
      limits_{0.0, 100e6, 573.15, 873.15} {}

std::string_view IncompressiblePropertyPackage::name() const noexcept {
    return "coolprop-incompressible";
}

std::string_view IncompressiblePropertyPackage::version() const noexcept {
    return detail::coolprop_version();
}

PropertyLimits IncompressiblePropertyPackage::limits() const noexcept {
    return limits_;
}

bool IncompressiblePropertyPackage::supports(
    PropertyCapability capability) const noexcept {
    switch (capability) {
        case PropertyCapability::state_pt:
        case PropertyCapability::state_ph:
        case PropertyCapability::state_ps:
        case PropertyCapability::transport:
            return true;
        case PropertyCapability::state_ph_derivatives:
        case PropertyCapability::saturation_p:
        case PropertyCapability::surface_tension:
            return false;
    }
    return false;
}

PropertyResult IncompressiblePropertyPackage::state_pt(
    double pressure, double temperature) const {
    return detail::coolprop_incompressible_state(
        substance_, detail::CoolPropFlash::pt, pressure,
        temperature);
}

PropertyResult IncompressiblePropertyPackage::state_ph(
    double pressure, double enthalpy) const {
    return detail::coolprop_incompressible_state(
        substance_, detail::CoolPropFlash::ph, pressure,
        enthalpy);
}

PropertyResult IncompressiblePropertyPackage::state_ps(
    double pressure, double entropy) const {
    return detail::coolprop_incompressible_state(
        substance_, detail::CoolPropFlash::ps, pressure,
        entropy);
}

SaturationResult IncompressiblePropertyPackage::saturation_p(
    double) const {
    return {
        {}, {}, PropertyStatus::unsupported,
        "incompressible liquids do not expose a saturation pair"};
}

std::string_view IncompressiblePropertyPackage::substance() const noexcept {
    return substance_;
}

}  // namespace thermox::physics
