#pragma once

#include "thermox/physics/property_package.hpp"

#include <string_view>

namespace thermox::physics::detail {

struct CoolPropBackendRef {
    std::string_view backend;
    std::string_view substance;
};

enum class CoolPropFlash { pt, ph, ps };

[[nodiscard]] PropertyResult coolprop_state(
    CoolPropBackendRef selection,
    CoolPropFlash flash,
    double first,
    double second);
[[nodiscard]] PhDerivativesResult coolprop_state_ph_derivatives(
    CoolPropBackendRef selection,
    double pressure_pa,
    double enthalpy_j_kg);
[[nodiscard]] SaturationResult coolprop_saturation_p(
    CoolPropBackendRef selection, double pressure_pa);
[[nodiscard]] PropertyLimits coolprop_limits(
    CoolPropBackendRef selection);
[[nodiscard]] PropertyResult coolprop_incompressible_state(
    std::string_view substance,
    CoolPropFlash flash,
    double first,
    double second);
[[nodiscard]] std::string_view coolprop_version() noexcept;

}  // namespace thermox::physics::detail
