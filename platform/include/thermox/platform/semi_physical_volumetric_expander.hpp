#pragma once

#include "thermox/nonlinear_solver.hpp"
#include "thermox/physics/property_package.hpp"

namespace thermox::platform {

// Stable direct-evaluation contract shared by the graph component and
// calibration/optimization callers. Keeping one canonical implementation
// prevents sidecar studies from drifting away from platform physics.
struct SemiPhysicalVolumetricExpanderParameters {
    double maximum_chamber_volume_per_revolution{0.0};
    double built_in_volume_ratio{0.0};
    double leakage_area{0.0};
    double leakage_discharge_coefficient{1.0};
    double mechanical_loss_at_reference_speed{0.0};
    double mechanical_loss_reference_angular_speed{0.0};
    double proportional_mechanical_loss{0.0};
    double ambient_heat_transfer_conductance{0.0};
    double ambient_temperature{300.0};
};

struct SemiPhysicalVolumetricExpanderEvaluation {
    double mass_flow{0.0};
    double outlet_enthalpy{0.0};
    double shaft_power{0.0};
    double rejected_heat{0.0};
    double built_in_pressure{0.0};
    double internal_mass_flow{0.0};
    double leakage_mass_flow{0.0};
    double indicated_power{0.0};
    double mechanical_loss_power{0.0};
    double ambient_heat_loss{0.0};
};

[[nodiscard]] EvaluationStatus evaluate_semi_physical_volumetric_expander(
    const physics::PropertyPackage& properties,
    double inlet_pressure,
    double inlet_enthalpy,
    double outlet_pressure,
    double angular_speed,
    const SemiPhysicalVolumetricExpanderParameters& parameters,
    SemiPhysicalVolumetricExpanderEvaluation& result);

}  // namespace thermox::platform
