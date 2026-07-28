#pragma once

#include "thermox/physics/property_package.hpp"

namespace thermox::physics {

class Co2PropertyPackage final : public PropertyPackage {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view version() const noexcept override;
    [[nodiscard]] PropertyLimits limits() const noexcept override;
    [[nodiscard]] bool supports(PropertyCapability) const noexcept override;
    [[nodiscard]] PropertyResult state_pt(double, double) const override;
    [[nodiscard]] PropertyResult state_ph(double, double) const override;
    [[nodiscard]] PropertyResult state_ps(double, double) const override;
    [[nodiscard]] SaturationResult saturation_p(double) const override;
};

}  // namespace thermox::physics
