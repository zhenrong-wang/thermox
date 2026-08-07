#pragma once

namespace thermox::platform {

struct TwoPhaseFlowGroupInputs {
    double total_mass_flux_kg_m2_s{0.0};
    double vapor_quality{0.0};
    double liquid_density_kg_m3{0.0};
    double vapor_density_kg_m3{0.0};
    double liquid_viscosity_pa_s{0.0};
    double vapor_viscosity_pa_s{0.0};
    double hydraulic_diameter_m{0.0};
    double surface_tension_n_m{0.0};
    double gravity_m_s2{9.80665};
};

// Base nondimensional groups only. Published maps may form their own
// transformed coordinates from these values and must document that choice.
struct TwoPhaseFlowGroups {
    double liquid_mass_flux_kg_m2_s{0.0};
    double vapor_mass_flux_kg_m2_s{0.0};
    double liquid_superficial_velocity_m_s{0.0};
    double vapor_superficial_velocity_m_s{0.0};
    double liquid_reynolds_number{0.0};
    double vapor_reynolds_number{0.0};
    double liquid_froude_number{0.0};
    double vapor_froude_number{0.0};
    double liquid_weber_number{0.0};
    double vapor_weber_number{0.0};
    double bond_number{0.0};
    double density_ratio_liquid_to_vapor{0.0};
    double viscosity_ratio_liquid_to_vapor{0.0};
};

[[nodiscard]] TwoPhaseFlowGroups calculate_two_phase_flow_groups(
    const TwoPhaseFlowGroupInputs& inputs);

}  // namespace thermox::platform
