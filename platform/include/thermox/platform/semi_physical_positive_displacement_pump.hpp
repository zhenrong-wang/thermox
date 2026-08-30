#pragma once

#include "thermox/nonlinear_solver.hpp"
#include "thermox/physics/property_package.hpp"

namespace thermox::platform {

// Canonical, artifact-free positive-displacement pump closure shared by
// component graphs and calibration/optimization callers.
struct SemiPhysicalPositiveDisplacementPumpParameters {
    double displacement_volume_per_revolution{0.0};
    double leakage_area{0.0};
    double leakage_discharge_coefficient{1.0};
    double isentropic_efficiency{0.0};
};

struct SemiPhysicalPositiveDisplacementPumpEvaluation {
    double mass_flow{0.0};
    double outlet_enthalpy{0.0};
    double shaft_power{0.0};
    double ideal_displacement_mass_flow{0.0};
    double leakage_mass_flow{0.0};
};

[[nodiscard]] EvaluationStatus
evaluate_semi_physical_positive_displacement_pump(
    const physics::PropertyPackage& properties,
    double inlet_pressure,
    double inlet_enthalpy,
    double outlet_pressure,
    double angular_speed,
    const SemiPhysicalPositiveDisplacementPumpParameters& parameters,
    SemiPhysicalPositiveDisplacementPumpEvaluation& result);

}  // namespace thermox::platform
