#pragma once

#include "thermox/physics/property_package.hpp"

#include <string>
#include <string_view>

namespace thermox::physics {

// CoolProp-backed single-phase incompressible liquids. The package is
// substance-parameterized so additional validated heat-transfer fluids can be
// registered without fluid-specific component models.
class IncompressiblePropertyPackage final : public PropertyPackage {
public:
    explicit IncompressiblePropertyPackage(std::string substance);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view version() const noexcept override;
    [[nodiscard]] PropertyLimits limits() const noexcept override;
    [[nodiscard]] bool supports(PropertyCapability) const noexcept override;
    [[nodiscard]] PropertyResult state_pt(double, double) const override;
    [[nodiscard]] PropertyResult state_ph(double, double) const override;
    [[nodiscard]] PropertyResult state_ps(double, double) const override;
    [[nodiscard]] SaturationResult saturation_p(double) const override;

    [[nodiscard]] std::string_view substance() const noexcept;

private:
    std::string substance_;
    PropertyLimits limits_;
};

}  // namespace thermox::physics
