#pragma once

#include "thermox/physics/property_package.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace thermox::physics {

struct IncompressiblePropertySample {
    double temperature_k{0.0};
    double density_kg_m3{0.0};
    double cp_j_kg_k{0.0};
    double viscosity_pa_s{0.0};
    double thermal_conductivity_w_m_k{0.0};
};

// A single-phase liquid provider constructed from an authoritative property
// table. Values are linearly interpolated; enthalpy and entropy are analytic
// integrals of the piecewise-linear heat capacity, keeping PT/PH/PS flashes
// mutually consistent.
class TabulatedIncompressiblePropertyPackage final
    : public PropertyPackage {
public:
    TabulatedIncompressiblePropertyPackage(
        std::string implementation_name,
        std::string implementation_version,
        std::vector<IncompressiblePropertySample> samples,
        double maximum_pressure_pa = 100e6,
        double reference_pressure_pa = 101325.0);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view version() const noexcept override;
    [[nodiscard]] PropertyLimits limits() const noexcept override;
    [[nodiscard]] bool supports(PropertyCapability) const noexcept override;
    [[nodiscard]] PropertyResult state_pt(double, double) const override;
    [[nodiscard]] PropertyResult state_ph(double, double) const override;
    [[nodiscard]] PropertyResult state_ps(double, double) const override;
    [[nodiscard]] SaturationResult saturation_p(double) const override;

private:
    [[nodiscard]] PropertyResult state_at(
        double pressure_pa, double temperature_k) const;
    [[nodiscard]] double enthalpy_at(double temperature_k) const;
    [[nodiscard]] double entropy_at(double temperature_k) const;
    [[nodiscard]] std::size_t interval(double temperature_k) const;

    std::string name_;
    std::string version_;
    std::vector<IncompressiblePropertySample> samples_;
    PropertyLimits limits_;
    double reference_pressure_pa_{101325.0};
};

[[nodiscard]] std::shared_ptr<const PropertyPackage>
make_sandia_solar_salt_property_package();

}  // namespace thermox::physics
