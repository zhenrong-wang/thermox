#pragma once

#include "thermox/physics/property_package.hpp"

#include <string>

namespace thermox::physics {

// A pure-fluid CoolProp HEOS package selected by the substance declared in a
// model document. The registry intentionally leaves its supported-substance
// list open: CoolProp validates the identifier when the package is created.
class CoolPropHeosPropertyPackage final : public PropertyPackage {
public:
    explicit CoolPropHeosPropertyPackage(std::string substance);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view version() const noexcept override;
    [[nodiscard]] PropertyLimits limits() const noexcept override;
    [[nodiscard]] bool supports(
        PropertyCapability capability) const noexcept override;
    [[nodiscard]] PropertyResult state_pt(double, double) const override;
    [[nodiscard]] PropertyResult state_ph(double, double) const override;
    [[nodiscard]] PhDerivativesResult state_ph_derivatives(
        double, double) const override;
    [[nodiscard]] PropertyResult state_ps(double, double) const override;
    [[nodiscard]] SaturationResult saturation_p(double) const override;

    [[nodiscard]] std::string_view substance() const noexcept;

private:
    std::string substance_;
    PropertyLimits limits_;
};

}  // namespace thermox::physics
