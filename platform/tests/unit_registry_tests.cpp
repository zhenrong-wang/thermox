#include "thermox/platform/model_document.hpp"
#include "thermox/platform/unit_registry.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main() {
    try {
        auto units = thermox::platform::make_default_unit_registry();
        const auto pressure = units.convert(2.5, "bar");
        require(
            pressure.dimension == "pressure" &&
                pressure.unit == "Pa" &&
                std::abs(pressure.value_si - 2.5e5) < 1.0e-9,
            "pressure conversion must produce canonical SI");
        const auto temperature = units.convert(20.0, "degC");
        require(
            temperature.dimension == "temperature" &&
                std::abs(temperature.value_si - 293.15) < 1.0e-12,
            "offset conversion must produce canonical SI");
        const auto temperature_difference =
            units.convert(20.0, "delta_degC");
        require(
            temperature_difference.dimension ==
                "temperature_difference" &&
                temperature_difference.value_si == 20.0,
            "temperature differences must not apply an absolute "
            "temperature offset");
        const auto time = units.convert(2.0, "min");
        require(
            time.dimension == "time" &&
                time.unit == "s" &&
                time.value_si == 120.0,
            "time conversion must produce canonical seconds");
        const auto length = units.convert(400.0, "mm");
        require(
            length.dimension == "length" &&
                length.unit == "m" &&
                std::abs(length.value_si - 0.4) < 1.0e-12,
            "length conversion must produce canonical metres");
        const auto inertia = units.convert(4.0, "kg*m^2");
        require(
            inertia.dimension == "moment_of_inertia" &&
                inertia.unit == "kg*m2" &&
                inertia.value_si == 4.0,
            "moment-of-inertia aliases must normalize");
        const auto speed = units.convert(60.0, "rpm");
        require(
            speed.dimension == "angular_speed" &&
                speed.unit == "rad/s" &&
                std::abs(speed.value_si -
                    2.0 * std::acos(-1.0)) < 1.0e-12,
            "rotational speed must normalize to radians per second");
        const auto& pressure_gradient =
            units.require_dimension("pressure_gradient");
        require(
            pressure_gradient.canonical_unit == "Pa/m" &&
                pressure_gradient.engineering_display.symbol ==
                    "kPa/m" &&
                pressure_gradient.engineering_display.scale_from_si ==
                    1.0e-3,
            "pressure-gradient engineering metadata must be registered");
        const auto& surface_tension =
            units.require_dimension("surface_tension");
        require(
            surface_tension.canonical_unit == "N/m" &&
                surface_tension.engineering_display.symbol ==
                    "mN/m" &&
                surface_tension.engineering_display.scale_from_si ==
                    1.0e3,
            "surface-tension engineering metadata must be registered");
        const auto tension = units.convert(72.0, "mN/m");
        require(
            tension.dimension == "surface_tension" &&
                tension.unit == "N/m" &&
                std::abs(tension.value_si - 0.072) < 1.0e-15,
            "surface tension must normalize to N/m");
        require(
            units.require_dimension("acceleration").canonical_unit ==
                "m/s2",
            "regime-map gravity inputs must use a registered SI "
            "acceleration dimension");

        thermox::platform::DimensionUnitDescriptor custom{
            "custom_flux",
            "flux_si",
            {"flux_si", 1.0, 0.0},
            {"kflux", 1.0e-3, 0.0},
            {
                {"flux_si", {}, 1.0, 0.0},
                {"kflux", {"custom-kflux"}, 1.0e3, 0.0},
            },
        };
        units.register_dimension(std::move(custom));
        const auto parsed =
            thermox::platform::parse_scalar_value_document_text(
                R"({"schema_version":"thermox.scalar_value/v1","scalar":{"value":3,"unit":"custom-kflux"}})",
                units);
        require(
            parsed.dimension == "custom_flux" &&
                parsed.unit == "flux_si" &&
                parsed.value_si == 3000.0,
            "custom units must participate in parser normalization");

        bool duplicate_rejected = false;
        try {
            units.register_dimension({
                "conflict",
                "conflict_si",
                {"conflict_si", 1.0, 0.0},
                {"conflict_si", 1.0, 0.0},
                {{"bar", {}, 1.0, 0.0}},
            });
        } catch (const std::invalid_argument&) {
            duplicate_rejected = true;
        }
        require(
            duplicate_rejected,
            "accepted unit aliases must be globally unambiguous");

        bool local_duplicate_rejected = false;
        try {
            thermox::platform::UnitRegistry local_units;
            local_units.register_dimension({
                "local_conflict",
                "local_si",
                {"local_si", 1.0, 0.0},
                {"local_si", 1.0, 0.0},
                {
                    {"local_si", {"repeated"}, 1.0, 0.0},
                    {"local_scaled", {"repeated"}, 2.0, 0.0},
                },
            });
        } catch (const std::invalid_argument&) {
            local_duplicate_rejected = true;
        }
        require(
            local_duplicate_rejected,
            "aliases within one dimension must be unambiguous");

        std::cout << "unit registry tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "unit registry tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
