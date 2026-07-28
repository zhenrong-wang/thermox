#pragma once

#include "thermox/physics/property_package.hpp"

#include <string_view>

namespace thermox::physics::detail {

enum class CoolPropFluid { co2, water };
enum class CoolPropFlash { pt, ph, ps };

[[nodiscard]] PropertyResult coolprop_state(
    CoolPropFluid fluid, CoolPropFlash flash, double first, double second);
[[nodiscard]] SaturationResult coolprop_saturation_p(
    CoolPropFluid fluid, double pressure_pa);
[[nodiscard]] std::string_view coolprop_version() noexcept;

}  // namespace thermox::physics::detail
