#include "thermox/platform/two_phase_flow_groups.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

namespace thermox::platform {

TwoPhaseFlowGroups calculate_two_phase_flow_groups(
    const TwoPhaseFlowGroupInputs& inputs) {
    const std::array positive{
        inputs.total_mass_flux_kg_m2_s,
        inputs.liquid_density_kg_m3,
        inputs.vapor_density_kg_m3,
        inputs.liquid_viscosity_pa_s,
        inputs.vapor_viscosity_pa_s,
        inputs.hydraulic_diameter_m,
        inputs.surface_tension_n_m,
        inputs.gravity_m_s2,
    };
    for (const double value : positive) {
        if (!std::isfinite(value) || value <= 0.0) {
            throw std::invalid_argument(
                "two-phase flow-group inputs must be finite and positive");
        }
    }
    if (!std::isfinite(inputs.vapor_quality) ||
        inputs.vapor_quality <= 0.0 ||
        inputs.vapor_quality >= 1.0) {
        throw std::invalid_argument(
            "two-phase flow groups require 0 < vapor_quality < 1");
    }
    if (inputs.liquid_density_kg_m3 <=
        inputs.vapor_density_kg_m3) {
        throw std::invalid_argument(
            "two-phase flow groups require liquid density greater "
            "than vapor density");
    }

    TwoPhaseFlowGroups groups;
    groups.gravity_m_s2 = inputs.gravity_m_s2;
    groups.liquid_mass_flux_kg_m2_s =
        (1.0 - inputs.vapor_quality) *
        inputs.total_mass_flux_kg_m2_s;
    groups.vapor_mass_flux_kg_m2_s =
        inputs.vapor_quality * inputs.total_mass_flux_kg_m2_s;
    groups.liquid_superficial_velocity_m_s =
        groups.liquid_mass_flux_kg_m2_s /
        inputs.liquid_density_kg_m3;
    groups.vapor_superficial_velocity_m_s =
        groups.vapor_mass_flux_kg_m2_s /
        inputs.vapor_density_kg_m3;
    groups.liquid_reynolds_number =
        groups.liquid_mass_flux_kg_m2_s *
        inputs.hydraulic_diameter_m /
        inputs.liquid_viscosity_pa_s;
    groups.vapor_reynolds_number =
        groups.vapor_mass_flux_kg_m2_s *
        inputs.hydraulic_diameter_m /
        inputs.vapor_viscosity_pa_s;
    const double gravity_velocity = std::sqrt(
        inputs.gravity_m_s2 * inputs.hydraulic_diameter_m);
    groups.liquid_froude_number =
        groups.liquid_superficial_velocity_m_s / gravity_velocity;
    groups.vapor_froude_number =
        groups.vapor_superficial_velocity_m_s / gravity_velocity;
    groups.liquid_weber_number =
        inputs.liquid_density_kg_m3 *
        groups.liquid_superficial_velocity_m_s *
        groups.liquid_superficial_velocity_m_s *
        inputs.hydraulic_diameter_m /
        inputs.surface_tension_n_m;
    groups.vapor_weber_number =
        inputs.vapor_density_kg_m3 *
        groups.vapor_superficial_velocity_m_s *
        groups.vapor_superficial_velocity_m_s *
        inputs.hydraulic_diameter_m /
        inputs.surface_tension_n_m;
    groups.bond_number =
        inputs.gravity_m_s2 *
        (inputs.liquid_density_kg_m3 -
         inputs.vapor_density_kg_m3) *
        inputs.hydraulic_diameter_m *
        inputs.hydraulic_diameter_m /
        inputs.surface_tension_n_m;
    groups.density_ratio_liquid_to_vapor =
        inputs.liquid_density_kg_m3 /
        inputs.vapor_density_kg_m3;
    groups.viscosity_ratio_liquid_to_vapor =
        inputs.liquid_viscosity_pa_s /
        inputs.vapor_viscosity_pa_s;

    const std::array results{
        groups.liquid_mass_flux_kg_m2_s,
        groups.vapor_mass_flux_kg_m2_s,
        groups.liquid_superficial_velocity_m_s,
        groups.vapor_superficial_velocity_m_s,
        groups.liquid_reynolds_number,
        groups.vapor_reynolds_number,
        groups.liquid_froude_number,
        groups.vapor_froude_number,
        groups.liquid_weber_number,
        groups.vapor_weber_number,
        groups.bond_number,
        groups.density_ratio_liquid_to_vapor,
        groups.viscosity_ratio_liquid_to_vapor,
    };
    for (const double value : results) {
        if (!std::isfinite(value) || value <= 0.0) {
            throw std::overflow_error(
                "two-phase flow-group calculation produced an "
                "invalid result");
        }
    }
    return groups;
}

}  // namespace thermox::platform
