#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace thermox::physics {

enum class Iso2314CoolingDetermination {
    extraction_schedule,
    compressor_power,
    manufacturer_md,
};

std::string to_string(Iso2314CoolingDetermination method);

struct Iso2314ExtractionState {
    std::string id;
    double mass_flow_kg_s{0.0};
    double specific_enthalpy_j_kg{0.0};
};

struct Iso2314EquivalentCoolingInput {
    double compressor_inlet_mass_flow_kg_s{0.0};
    double compressor_inlet_specific_enthalpy_j_kg{0.0};
    double compressor_discharge_specific_enthalpy_j_kg{0.0};
    std::vector<Iso2314ExtractionState> extractions;
    std::optional<double> compressor_power_w;
    std::optional<double> manufacturer_md;
};

struct Iso2314EquivalentCoolingResult {
    Iso2314CoolingDetermination determination{
        Iso2314CoolingDetermination::extraction_schedule};
    double no_extraction_compressor_power_w{0.0};
    double actual_compressor_power_w{0.0};
    double actual_extraction_mass_flow_kg_s{0.0};
    double equivalent_compressor_mass_flow_kg_s{0.0};
    double equivalent_extraction_mass_flow_kg_s{0.0};
    double relative_equivalent_flow_difference_md{0.0};
    double equivalent_extraction_energy_w{0.0};
};

class Iso2314EquivalentCoolingError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Iso2314EquivalentCoolingResult calculate_iso2314_equivalent_cooling(
    const Iso2314EquivalentCoolingInput& input);

}  // namespace thermox::physics
