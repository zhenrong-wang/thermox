#pragma once

#include "thermox/physics/property_package.hpp"

namespace thermox::physics {

class IdealGasPropertyPackage final : public PropertyPackage {
public:
    explicit IdealGasPropertyPackage(double cp_j_kg_k = 1004.5,
                                     double gas_constant_j_kg_k = 287.0,
                                     double reference_pressure_pa = 101325.0,
                                     double reference_temperature_k = 298.15);
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] PropertyLimits limits() const noexcept override;
    [[nodiscard]] PropertyResult state_pt(double, double) const override;
    [[nodiscard]] PropertyResult state_ph(double, double) const override;
    [[nodiscard]] PropertyResult state_ps(double, double) const override;

private:
    double cp_;
    double gas_constant_;
    double reference_pressure_;
    double reference_temperature_;
};

}  // namespace thermox::physics
