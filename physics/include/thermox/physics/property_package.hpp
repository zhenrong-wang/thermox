#pragma once

#include <string>
#include <string_view>

namespace thermox::physics {

enum class PropertyStatus {
    success,
    saturation_boundary,
    unsupported,
    invalid_input,
    out_of_range,
    no_convergence,
    backend_error,
};

enum class Phase { unknown, liquid, vapor, supercritical, two_phase };

enum class PropertyCapability {
    state_pt,
    state_ph,
    state_ph_derivatives,
    state_ps,
    saturation_p,
    transport,
    surface_tension,
};

struct ThermodynamicState {
    double pressure_pa{0.0};
    double temperature_k{0.0};
    double density_kg_m3{0.0};
    double internal_energy_j_kg{0.0};
    double enthalpy_j_kg{0.0};
    double entropy_j_kg_k{0.0};
    double cv_j_kg_k{0.0};
    double cp_j_kg_k{0.0};
    double speed_of_sound_m_s{0.0};
    double viscosity_pa_s{0.0};
    double thermal_conductivity_w_m_k{0.0};
    double vapor_quality{-1.0};
    Phase phase{Phase::unknown};
};

struct PropertyResult {
    ThermodynamicState state;
    PropertyStatus status{PropertyStatus::backend_error};
    std::string message;

    [[nodiscard]] bool ok() const { return status == PropertyStatus::success; }
};

enum class PropertyDerivativeSource {
    analytic,
    finite_difference,
};

struct PhStateDerivatives {
    double temperature_wrt_pressure_at_enthalpy{0.0};
    double temperature_wrt_enthalpy_at_pressure{0.0};
    double density_wrt_pressure_at_enthalpy{0.0};
    double density_wrt_enthalpy_at_pressure{0.0};
    double internal_energy_wrt_pressure_at_enthalpy{0.0};
    double internal_energy_wrt_enthalpy_at_pressure{0.0};
    double entropy_wrt_pressure_at_enthalpy{0.0};
    double entropy_wrt_enthalpy_at_pressure{0.0};
    double vapor_quality_wrt_pressure_at_enthalpy{0.0};
    double vapor_quality_wrt_enthalpy_at_pressure{0.0};
    double cp_wrt_pressure_at_enthalpy{0.0};
    double cp_wrt_enthalpy_at_pressure{0.0};
};

struct PhDerivativesResult {
    ThermodynamicState state;
    PhStateDerivatives derivatives;
    PropertyDerivativeSource source{PropertyDerivativeSource::analytic};
    PropertyStatus status{PropertyStatus::backend_error};
    std::string message;

    [[nodiscard]] bool ok() const {
        return status == PropertyStatus::success;
    }
};

struct PhTransportDerivatives {
    double viscosity_wrt_pressure_at_enthalpy{0.0};
    double viscosity_wrt_enthalpy_at_pressure{0.0};
    double thermal_conductivity_wrt_pressure_at_enthalpy{0.0};
    double thermal_conductivity_wrt_enthalpy_at_pressure{0.0};
};

struct PhTransportDerivativesResult {
    ThermodynamicState state;
    PhTransportDerivatives derivatives;
    PropertyDerivativeSource source{
        PropertyDerivativeSource::finite_difference};
    PropertyStatus status{PropertyStatus::backend_error};
    std::string message;

    [[nodiscard]] bool ok() const {
        return status == PropertyStatus::success;
    }
};

struct SaturationResult {
    ThermodynamicState liquid;
    ThermodynamicState vapor;
    PropertyStatus status{PropertyStatus::backend_error};
    std::string message;
    // Interfacial property at the saturation state, not a bulk-phase
    // property. Positive and finite when surface_tension is supported.
    double surface_tension_n_m{0.0};

    [[nodiscard]] bool ok() const { return status == PropertyStatus::success; }
};

struct PropertyLimits {
    double minimum_pressure_pa{0.0};
    double maximum_pressure_pa{0.0};
    double minimum_temperature_k{0.0};
    double maximum_temperature_k{0.0};
};

class PropertyPackage {
public:
    virtual ~PropertyPackage() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::string_view version() const noexcept = 0;
    [[nodiscard]] virtual PropertyLimits limits() const noexcept = 0;
    [[nodiscard]] virtual bool supports(PropertyCapability capability) const noexcept = 0;
    [[nodiscard]] virtual PropertyResult state_pt(
        double pressure_pa, double temperature_k) const = 0;
    [[nodiscard]] virtual PropertyResult state_ph(
        double pressure_pa, double enthalpy_j_kg) const = 0;
    [[nodiscard]] virtual PhDerivativesResult state_ph_derivatives(
        double pressure_pa, double enthalpy_j_kg) const;
    [[nodiscard]] virtual PropertyResult state_ps(
        double pressure_pa, double entropy_j_kg_k) const = 0;
    [[nodiscard]] virtual SaturationResult saturation_p(
        double pressure_pa) const = 0;
};

[[nodiscard]] PhDerivativesResult state_ph_derivatives_with_fallback(
    const PropertyPackage& properties,
    double pressure_pa,
    double enthalpy_j_kg);

// Transport-property derivatives are deliberately separate from the
// thermodynamic derivative capability. Providers that expose transport state
// values receive this bounded, phase-aware p-h fallback without claiming that
// their native thermodynamic derivative API differentiates transport models.
[[nodiscard]] PhTransportDerivativesResult
state_ph_transport_derivatives_with_fallback(
    const PropertyPackage& properties,
    double pressure_pa,
    double enthalpy_j_kg);

}  // namespace thermox::physics
