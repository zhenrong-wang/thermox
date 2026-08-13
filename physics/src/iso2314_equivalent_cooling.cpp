#include "thermox/physics/iso2314_equivalent_cooling.hpp"

#include <cmath>
#include <set>

namespace thermox::physics {

namespace {

void require_finite(double value, const std::string& field) {
    if (!std::isfinite(value)) {
        throw Iso2314EquivalentCoolingError(field + " must be finite");
    }
}

}  // namespace

std::string to_string(Iso2314CoolingDetermination method) {
    switch (method) {
        case Iso2314CoolingDetermination::extraction_schedule:
            return "extraction_schedule";
        case Iso2314CoolingDetermination::compressor_power:
            return "compressor_power";
        case Iso2314CoolingDetermination::manufacturer_md:
            return "manufacturer_md";
    }
    throw Iso2314EquivalentCoolingError(
        "unknown ISO 2314 cooling determination method");
}

Iso2314EquivalentCoolingResult calculate_iso2314_equivalent_cooling(
    const Iso2314EquivalentCoolingInput& input) {
    require_finite(
        input.compressor_inlet_mass_flow_kg_s,
        "compressor_inlet_mass_flow_kg_s");
    require_finite(
        input.compressor_inlet_specific_enthalpy_j_kg,
        "compressor_inlet_specific_enthalpy_j_kg");
    require_finite(
        input.compressor_discharge_specific_enthalpy_j_kg,
        "compressor_discharge_specific_enthalpy_j_kg");
    if (input.compressor_inlet_mass_flow_kg_s <= 0.0) {
        throw Iso2314EquivalentCoolingError(
            "compressor inlet mass flow must be positive");
    }
    const double enthalpy_rise =
        input.compressor_discharge_specific_enthalpy_j_kg -
        input.compressor_inlet_specific_enthalpy_j_kg;
    if (enthalpy_rise <= 0.0) {
        throw Iso2314EquivalentCoolingError(
            "compressor discharge enthalpy must exceed inlet enthalpy");
    }

    const int determination_count =
        (!input.extractions.empty() ? 1 : 0) +
        (input.compressor_power_w.has_value() ? 1 : 0) +
        (input.manufacturer_md.has_value() ? 1 : 0);
    if (determination_count > 1) {
        throw Iso2314EquivalentCoolingError(
            "declare only one of extraction schedule, compressor power, "
            "or manufacturer md");
    }

    Iso2314EquivalentCoolingResult result;
    result.no_extraction_compressor_power_w =
        input.compressor_inlet_mass_flow_kg_s * enthalpy_rise;

    if (input.compressor_power_w.has_value()) {
        require_finite(*input.compressor_power_w, "compressor_power_w");
        if (*input.compressor_power_w <= 0.0 ||
            *input.compressor_power_w >
                result.no_extraction_compressor_power_w) {
            throw Iso2314EquivalentCoolingError(
                "compressor power must be positive and no greater than "
                "the no-extraction compressor power");
        }
        result.determination =
            Iso2314CoolingDetermination::compressor_power;
        result.actual_compressor_power_w = *input.compressor_power_w;
    } else if (input.manufacturer_md.has_value()) {
        require_finite(*input.manufacturer_md, "manufacturer_md");
        if (*input.manufacturer_md < 0.0) {
            throw Iso2314EquivalentCoolingError(
                "manufacturer md must be non-negative");
        }
        result.determination =
            Iso2314CoolingDetermination::manufacturer_md;
        result.equivalent_compressor_mass_flow_kg_s =
            input.compressor_inlet_mass_flow_kg_s /
            (1.0 + *input.manufacturer_md);
        result.actual_compressor_power_w =
            result.equivalent_compressor_mass_flow_kg_s * enthalpy_rise;
    } else {
        result.determination =
            Iso2314CoolingDetermination::extraction_schedule;
        result.actual_compressor_power_w =
            result.no_extraction_compressor_power_w;
        std::set<std::string> ids;
        for (const auto& extraction : input.extractions) {
            if (extraction.id.empty() || !ids.insert(extraction.id).second) {
                throw Iso2314EquivalentCoolingError(
                    "extraction IDs must be non-empty and unique");
            }
            require_finite(
                extraction.mass_flow_kg_s,
                "extraction mass flow");
            require_finite(
                extraction.specific_enthalpy_j_kg,
                "extraction specific enthalpy");
            if (extraction.mass_flow_kg_s < 0.0) {
                throw Iso2314EquivalentCoolingError(
                    "extraction mass flow must be non-negative");
            }
            if (extraction.specific_enthalpy_j_kg <
                    input.compressor_inlet_specific_enthalpy_j_kg ||
                extraction.specific_enthalpy_j_kg >
                    input.compressor_discharge_specific_enthalpy_j_kg) {
                throw Iso2314EquivalentCoolingError(
                    "extraction enthalpy must lie between compressor inlet "
                    "and discharge enthalpy");
            }
            result.actual_extraction_mass_flow_kg_s +=
                extraction.mass_flow_kg_s;
            result.actual_compressor_power_w -=
                extraction.mass_flow_kg_s *
                (input.compressor_discharge_specific_enthalpy_j_kg -
                 extraction.specific_enthalpy_j_kg);
        }
        if (result.actual_extraction_mass_flow_kg_s >
            input.compressor_inlet_mass_flow_kg_s) {
            throw Iso2314EquivalentCoolingError(
                "total extraction flow must not exceed compressor inlet flow");
        }
        if (result.actual_compressor_power_w <= 0.0) {
            throw Iso2314EquivalentCoolingError(
                "extraction schedule produces non-positive compressor power");
        }
    }

    result.equivalent_compressor_mass_flow_kg_s =
        result.actual_compressor_power_w / enthalpy_rise;
    result.equivalent_extraction_mass_flow_kg_s =
        input.compressor_inlet_mass_flow_kg_s -
        result.equivalent_compressor_mass_flow_kg_s;
    result.relative_equivalent_flow_difference_md =
        input.compressor_inlet_mass_flow_kg_s /
            result.equivalent_compressor_mass_flow_kg_s -
        1.0;
    result.equivalent_extraction_energy_w =
        result.equivalent_extraction_mass_flow_kg_s *
        input.compressor_inlet_specific_enthalpy_j_kg;
    return result;
}

}  // namespace thermox::physics
