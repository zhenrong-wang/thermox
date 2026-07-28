#pragma once

#include "thermox/physics/thermochemistry.hpp"

namespace thermox::physics {

// Available from the optional thermox_cantera_backend target when Thermox
// is configured with THERMOX_ENABLE_CANTERA=ON.
void register_cantera_thermochemistry_backend(
    ThermochemistryPackageRegistry& registry);

}  // namespace thermox::physics
