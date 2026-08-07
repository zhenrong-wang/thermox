#include "thermox/platform/unit_registry.hpp"

#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

namespace thermox::platform {

namespace {

DisplayUnitDescriptor display(
    std::string symbol,
    double scale = 1.0,
    double offset = 0.0) {
    return {std::move(symbol), scale, offset};
}

AcceptedUnitDescriptor accepted(
    std::string symbol,
    double scale = 1.0,
    double offset = 0.0,
    std::vector<std::string> aliases = {}) {
    return {
        std::move(symbol),
        std::move(aliases),
        scale,
        offset,
    };
}

DimensionUnitDescriptor dimension(
    std::string name,
    std::string canonical,
    DisplayUnitDescriptor engineering,
    std::vector<AcceptedUnitDescriptor> accepted_units = {}) {
    return {
        std::move(name),
        canonical,
        display(std::move(canonical)),
        std::move(engineering),
        std::move(accepted_units),
    };
}

}  // namespace

void UnitRegistry::register_dimension(
    DimensionUnitDescriptor descriptor) {
    if (descriptor.dimension.empty() ||
        descriptor.canonical_unit.empty() ||
        descriptor.si_display.symbol.empty() ||
        descriptor.engineering_display.symbol.empty()) {
        throw std::invalid_argument(
            "unit dimension and display symbols must not be empty");
    }
    const auto valid_display = [](const DisplayUnitDescriptor& value) {
        return std::isfinite(value.scale_from_si) &&
            value.scale_from_si != 0.0 &&
            std::isfinite(value.offset_from_si);
    };
    if (!valid_display(descriptor.si_display) ||
        !valid_display(descriptor.engineering_display)) {
        throw std::invalid_argument(
            "display unit transforms must be finite with nonzero scale");
    }
    if (dimensions_.contains(descriptor.dimension)) {
        throw std::invalid_argument(
            "duplicate unit dimension: " + descriptor.dimension);
    }
    std::set<std::string> descriptor_symbols;
    for (const auto& unit : descriptor.accepted_units) {
        if (unit.symbol.empty() ||
            !std::isfinite(unit.scale_to_si) ||
            unit.scale_to_si == 0.0 ||
            !std::isfinite(unit.offset_to_si)) {
            throw std::invalid_argument(
                "accepted unit transforms require a symbol, finite "
                "offset, and nonzero finite scale");
        }
        std::vector<std::string> symbols{unit.symbol};
        symbols.insert(
            symbols.end(), unit.aliases.begin(), unit.aliases.end());
        for (const auto& symbol : symbols) {
            if (symbol.empty() ||
                conversions_.contains(symbol) ||
                !descriptor_symbols.insert(symbol).second) {
                throw std::invalid_argument(
                    "duplicate or empty accepted unit alias: " + symbol);
            }
        }
    }
    const auto dimension_name = descriptor.dimension;
    const auto canonical_unit = descriptor.canonical_unit;
    for (const auto& unit : descriptor.accepted_units) {
        const Conversion conversion{
            dimension_name,
            canonical_unit,
            unit.scale_to_si,
            unit.offset_to_si,
        };
        conversions_.emplace(unit.symbol, conversion);
        for (const auto& alias : unit.aliases) {
            conversions_.emplace(alias, conversion);
        }
    }
    dimensions_.emplace(
        dimension_name, std::move(descriptor));
}

ScalarValue UnitRegistry::convert(
    double value,
    const std::string& unit,
    const std::string& field_name) const {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(
            "unit conversion requires a finite value");
    }
    const auto found = conversions_.find(unit);
    if (found == conversions_.end()) {
        throw std::invalid_argument(
            "unsupported unit" +
            (field_name.empty()
                 ? std::string{}
                 : " for field '" + field_name + "'") +
            ": " + unit);
    }
    const auto& conversion = found->second;
    const double value_si =
        value * conversion.scale_to_si +
        conversion.offset_to_si;
    if (!std::isfinite(value_si)) {
        throw std::invalid_argument(
            "unit conversion produced a non-finite SI value");
    }
    return {
        value_si,
        conversion.canonical_unit,
        conversion.dimension,
    };
}

const DimensionUnitDescriptor& UnitRegistry::require_dimension(
    const std::string& dimension_name) const {
    const auto found = dimensions_.find(dimension_name);
    if (found == dimensions_.end()) {
        throw std::invalid_argument(
            "unknown unit dimension: " + dimension_name);
    }
    return found->second;
}

std::vector<DimensionUnitDescriptor> UnitRegistry::descriptors()
    const {
    std::vector<DimensionUnitDescriptor> values;
    values.reserve(dimensions_.size());
    for (const auto& [_, descriptor] : dimensions_) {
        values.push_back(descriptor);
    }
    return values;
}

UnitRegistry make_default_unit_registry() {
    UnitRegistry registry;
    registry.register_dimension(dimension(
        "pressure", "Pa", display("bar", 1.0e-5),
        {
            accepted("Pa"),
            accepted("kPa", 1.0e3),
            accepted("MPa", 1.0e6),
            accepted("bar", 1.0e5),
        }));
    registry.register_dimension(dimension(
        "temperature", "K", display("°C", 1.0, -273.15),
        {
            accepted("K"),
            accepted("degC", 1.0, 273.15, {"C"}),
        }));
    registry.register_dimension(dimension(
        "angle", "rad", display("deg", 180.0 / std::acos(-1.0)),
        {
            accepted("rad"),
            accepted("deg", std::acos(-1.0) / 180.0),
        }));
    registry.register_dimension(dimension(
        "length", "m", display("mm", 1.0e3),
        {
            accepted("m"),
            accepted("cm", 1.0e-2),
            accepted("mm", 1.0e-3),
        }));
    registry.register_dimension(dimension(
        "mass_flow", "kg/s", display("kg/s"),
        {
            accepted("kg/s"),
            accepted("kg/h", 1.0 / 3600.0),
        }));
    registry.register_dimension(dimension(
        "specific_heat", "J/kg/K", display("kJ/kg/K", 1.0e-3),
        {
            accepted("J/kg/K", 1.0, 0.0, {"J/(kg*K)"}),
            accepted(
                "kJ/kg/K", 1.0e3, 0.0, {"kJ/(kg*K)"}),
        }));
    registry.register_dimension(dimension(
        "specific_enthalpy", "J/kg", display("kJ/kg", 1.0e-3),
        {
            accepted("J/kg"),
            accepted("kJ/kg", 1.0e3),
        }));
    registry.register_dimension(dimension(
        "power", "W", display("MW", 1.0e-6),
        {
            accepted("W"),
            accepted("kW", 1.0e3),
            accepted("MW", 1.0e6),
        }));
    registry.register_dimension(dimension(
        "thermal_capacity", "J/K", display("MJ/K", 1.0e-6),
        {
            accepted("J/K"),
            accepted("kJ/K", 1.0e3),
            accepted("MJ/K", 1.0e6),
        }));
    registry.register_dimension(dimension(
        "thermal_conductance", "W/K", display("kW/K", 1.0e-3),
        {
            accepted("W/K"),
            accepted("kW/K", 1.0e3),
            accepted("MW/K", 1.0e6),
        }));
    registry.register_dimension(dimension(
        "volume", "m3", display("m3"),
        {
            accepted("m3", 1.0, 0.0, {"m^3"}),
            accepted("L", 1.0e-3),
        }));
    registry.register_dimension(dimension(
        "mass", "kg", display("kg"), {accepted("kg")}));
    registry.register_dimension(dimension(
        "molar_mass", "kg/mol", display("g/mol", 1.0e3),
        {
            accepted("kg/mol"),
            accepted("g/mol", 1.0e-3),
        }));
    registry.register_dimension(dimension(
        "energy", "J", display("MJ", 1.0e-6),
        {
            accepted("J"),
            accepted("kJ", 1.0e3),
            accepted("MJ", 1.0e6),
        }));
    registry.register_dimension(dimension(
        "time", "s", display("s"),
        {
            accepted("s"),
            accepted("min", 60.0),
            accepted("h", 3600.0),
        }));
    registry.register_dimension(dimension(
        "moment_of_inertia", "kg*m2", display("kg·m²"),
        {
            accepted("kg*m2", 1.0, 0.0, {"kg*m^2"}),
        }));
    registry.register_dimension(dimension(
        "angular_speed", "rad/s",
        display("rpm", 60.0 / (2.0 * std::acos(-1.0))),
        {
            accepted("rad/s"),
            accepted("rpm", 2.0 * std::acos(-1.0) / 60.0),
        }));
    registry.register_dimension(dimension(
        "frequency", "Hz", display("Hz"),
        {
            accepted("Hz"),
        }));
    registry.register_dimension({
        "dimensionless",
        "dimensionless",
        display("1"),
        display("1"),
        {
            accepted("dimensionless", 1.0, 0.0, {"1"}),
            accepted("%", 0.01),
        },
    });

    const auto display_only =
        [&](std::string name,
            std::string canonical,
            DisplayUnitDescriptor engineering) {
            registry.register_dimension(dimension(
                std::move(name),
                std::move(canonical),
                std::move(engineering)));
        };
    display_only(
        "electrical_power", "W", display("MW", 1.0e-6));
    display_only(
        "specific_internal_energy", "J/kg",
        display("kJ/kg", 1.0e-3));
    display_only(
        "specific_entropy", "J/kg/K",
        display("kJ/kg/K", 1.0e-3));
    display_only(
        "specific_heat_capacity", "J/kg/K",
        display("kJ/kg/K", 1.0e-3));
    display_only("density", "kg/m3", display("kg/m³"));
    display_only("area", "m2", display("m²"));
    display_only(
        "mass_flux", "kg/m2/s", display("kg/(m²·s)"));
    display_only(
        "pressure_gradient", "Pa/m", display("kPa/m", 1.0e-3));
    display_only(
        "dynamic_viscosity", "Pa*s", display("Pa·s"));
    display_only(
        "thermal_conductivity", "W/m/K", display("W/m/K"));
    registry.register_dimension(dimension(
        "surface_tension", "N/m", display("mN/m", 1.0e3),
        {
            accepted("N/m"),
            accepted("mN/m", 1.0e-3),
        }));
    display_only("speed", "m/s", display("m/s"));
    return registry;
}

}  // namespace thermox::platform
