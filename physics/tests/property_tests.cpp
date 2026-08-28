#include "thermox/equation_system.hpp"
#include "thermox/transient_solver.hpp"
#include "thermox/physics/co2_package.hpp"
#include "thermox/physics/coolprop_heos_package.hpp"
#include "thermox/physics/ideal_gas_package.hpp"
#include "thermox/physics/if97_package.hpp"
#include "thermox/physics/incompressible_package.hpp"
#include "thermox/physics/tabulated_incompressible_package.hpp"
#include "thermox/physics/humid_air.hpp"
#include "thermox/physics/iso2314_equivalent_cooling.hpp"
#include "thermox/physics/thermochemistry.hpp"
#ifdef THERMOX_TEST_HAS_CANTERA
#include "thermox/physics/cantera_thermochemistry.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

class TestThermochemistryPackage final
    : public thermox::physics::ThermochemistryPackage {
public:
    TestThermochemistryPackage(
        std::string mechanism,
        std::string phase)
        : mechanism_(std::move(mechanism)),
          phase_(std::move(phase)) {}

    std::string_view name() const noexcept override {
        return "test-thermochemistry";
    }
    std::string_view version() const noexcept override {
        return "1.0.0";
    }
    std::string_view mechanism() const noexcept override {
        return mechanism_;
    }
    std::string_view phase() const noexcept override {
        return phase_;
    }
    const std::vector<std::string>& species_basis()
        const noexcept override {
        return species_;
    }
    bool supports(
        thermox::physics::ThermochemistryCapability)
        const noexcept override {
        return true;
    }
    thermox::physics::ThermochemicalResult state_pt(
        double,
        double,
        const thermox::physics::SpeciesComposition&)
        const override {
        return {};
    }
    thermox::physics::ThermochemicalResult state_ph(
        double,
        double,
        const thermox::physics::SpeciesComposition&)
        const override {
        return {};
    }
    thermox::physics::ThermochemicalResult state_ps(
        double,
        double,
        const thermox::physics::SpeciesComposition&)
        const override {
        return {};
    }
    thermox::physics::ThermochemicalResult equilibrate_hp(
        double,
        double,
        const thermox::physics::SpeciesComposition&)
        const override {
        return {};
    }
    thermox::physics::HeatingValueResult lower_heating_value(
        double,
        double,
        const thermox::physics::SpeciesComposition&) const override {
        return {};
    }

private:
    std::string mechanism_;
    std::string phase_;
    std::vector<std::string> species_{"O2", "N2", "CH4", "CO2",
                                      "H2O"};
};

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(double actual, double expected, double tolerance,
                  const std::string& message) {
    if (std::abs(actual - expected) > tolerance)
        throw std::runtime_error(message + ": actual=" + std::to_string(actual));
}

void verify_iso2314_equivalent_cooling() {
    using thermox::physics::Iso2314EquivalentCoolingInput;
    using thermox::physics::Iso2314ExtractionState;

    Iso2314EquivalentCoolingInput extraction_case;
    extraction_case.compressor_inlet_mass_flow_kg_s = 100.0;
    extraction_case.compressor_inlet_specific_enthalpy_j_kg = 300000.0;
    extraction_case.compressor_discharge_specific_enthalpy_j_kg = 600000.0;
    extraction_case.extractions = {
        Iso2314ExtractionState{"mid", 10.0, 450000.0},
        Iso2314ExtractionState{"late", 5.0, 540000.0},
    };
    const auto extracted = thermox::physics::
        calculate_iso2314_equivalent_cooling(extraction_case);
    require_near(
        extracted.no_extraction_compressor_power_w,
        30.0e6, 1.0e-9,
        "ISO 2314 no-extraction compressor work");
    require_near(
        extracted.actual_compressor_power_w,
        28.2e6, 1.0e-9,
        "ISO 2314 equation 30 extraction-adjusted compressor work");
    require_near(
        extracted.equivalent_compressor_mass_flow_kg_s,
        94.0, 1.0e-12,
        "ISO 2314 equation 29 equivalent compressor flow");
    require_near(
        extracted.equivalent_extraction_mass_flow_kg_s,
        6.0, 1.0e-12,
        "ISO 2314 equivalent extraction differs from actual extraction");
    require_near(
        extracted.relative_equivalent_flow_difference_md,
        100.0 / 94.0 - 1.0, 1.0e-12,
        "ISO 2314 equation 31 md");
    require_near(
        extracted.equivalent_extraction_energy_w,
        1.8e6, 1.0e-9,
        "ISO 2314 equation 28 equivalent extraction energy");

    auto power_case = extraction_case;
    power_case.extractions.clear();
    power_case.compressor_power_w = 28.2e6;
    const auto from_power = thermox::physics::
        calculate_iso2314_equivalent_cooling(power_case);
    require_near(
        from_power.relative_equivalent_flow_difference_md,
        extracted.relative_equivalent_flow_difference_md,
        1.0e-12,
        "actual compressor power and extraction schedule are equivalent");

    auto md_case = extraction_case;
    md_case.extractions.clear();
    md_case.manufacturer_md =
        extracted.relative_equivalent_flow_difference_md;
    const auto from_md = thermox::physics::
        calculate_iso2314_equivalent_cooling(md_case);
    require_near(
        from_md.actual_compressor_power_w, 28.2e6, 1.0e-8,
        "manufacturer md reproduces the equivalent compressor work");

    bool ambiguous_rejected = false;
    try {
        auto ambiguous = extraction_case;
        ambiguous.compressor_power_w = 28.2e6;
        (void)thermox::physics::calculate_iso2314_equivalent_cooling(
            ambiguous);
    } catch (const thermox::physics::Iso2314EquivalentCoolingError&) {
        ambiguous_rejected = true;
    }
    require(
        ambiguous_rejected,
        "ISO 2314 equivalent-cooling input must reject ambiguous methods");
}

void verify_round_trip(const thermox::physics::PropertyPackage& package,
                       double pressure, double temperature, double tolerance) {
    const auto pt = package.state_pt(pressure, temperature);
    require(pt.ok(), std::string(package.name()) + " PT: " + pt.message);
    const auto ph = package.state_ph(pressure, pt.state.enthalpy_j_kg);
    require(ph.ok(), std::string(package.name()) + " PH: " + ph.message);
    require_near(ph.state.temperature_k, temperature, tolerance,
                 std::string(package.name()) + " PT-PH round trip");
    const auto ps = package.state_ps(pressure, pt.state.entropy_j_kg_k);
    require(ps.ok(), std::string(package.name()) + " PS: " + ps.message);
    require_near(ps.state.temperature_k, temperature, tolerance,
                 std::string(package.name()) + " PT-PS round trip");
    require(pt.state.density_kg_m3 > 0.0, "density must be positive");
    require(pt.state.cp_j_kg_k > 0.0, "cp must be positive");
}

void require_relative_near(
    double actual, double expected, double relative_tolerance,
    double absolute_tolerance, const std::string& message) {
    const double tolerance = std::max(
        absolute_tolerance,
        relative_tolerance * std::abs(expected));
    require_near(actual, expected, tolerance, message);
}

void verify_ph_derivatives(
    const thermox::physics::PropertyPackage& package,
    double pressure, double temperature,
    double relative_tolerance,
    thermox::physics::PropertyDerivativeSource expected_source) {
    require(
        package.supports(
            thermox::physics::PropertyCapability::
                state_ph_derivatives) ==
            (expected_source ==
             thermox::physics::PropertyDerivativeSource::analytic),
        std::string(package.name()) +
            " derivative capability declaration");
    const auto pt = package.state_pt(pressure, temperature);
    require(pt.ok(), "derivative reference PT state");
    const double enthalpy = pt.state.enthalpy_j_kg;
    const auto result =
        thermox::physics::state_ph_derivatives_with_fallback(
            package, pressure, enthalpy);
    require(
        result.ok(),
        std::string(package.name()) +
            " analytic p-h derivatives: " + result.message);
    require(
        result.source == expected_source,
        "property derivatives must report correct provenance");

    const double pressure_step =
        std::max(pressure * 1.0e-5, 1.0);
    const double enthalpy_step =
        std::max(std::abs(enthalpy) * 1.0e-5, 1.0e-2);
    const auto pressure_lower =
        package.state_ph(pressure - pressure_step, enthalpy);
    const auto pressure_upper =
        package.state_ph(pressure + pressure_step, enthalpy);
    const auto enthalpy_lower =
        package.state_ph(pressure, enthalpy - enthalpy_step);
    const auto enthalpy_upper =
        package.state_ph(pressure, enthalpy + enthalpy_step);
    require(
        pressure_lower.ok() && pressure_upper.ok() &&
            enthalpy_lower.ok() && enthalpy_upper.ok(),
        "finite-difference derivative reference states");
    const auto pressure_partial =
        [&](auto extract) {
            return (
                extract(pressure_upper.state) -
                extract(pressure_lower.state)) /
                (2.0 * pressure_step);
        };
    const auto enthalpy_partial =
        [&](auto extract) {
            return (
                extract(enthalpy_upper.state) -
                extract(enthalpy_lower.state)) /
                (2.0 * enthalpy_step);
        };
    const auto extract_temperature =
        [](const thermox::physics::ThermodynamicState& state) {
            return state.temperature_k;
        };
    const auto density =
        [](const thermox::physics::ThermodynamicState& state) {
            return state.density_kg_m3;
        };
    const auto internal_energy =
        [](const thermox::physics::ThermodynamicState& state) {
            return state.internal_energy_j_kg;
        };
    const auto entropy =
        [](const thermox::physics::ThermodynamicState& state) {
            return state.entropy_j_kg_k;
        };
    const auto vapor_quality =
        [](const thermox::physics::ThermodynamicState& state) {
            return state.vapor_quality;
        };
    const auto cp =
        [](const thermox::physics::ThermodynamicState& state) {
            return state.cp_j_kg_k;
        };
    const auto cv =
        [](const thermox::physics::ThermodynamicState& state) {
            return state.cv_j_kg_k;
        };
    const auto speed_of_sound =
        [](const thermox::physics::ThermodynamicState& state) {
            return state.speed_of_sound_m_s;
        };
    require_relative_near(
        result.derivatives
            .temperature_wrt_pressure_at_enthalpy,
        pressure_partial(extract_temperature), relative_tolerance,
        1.0e-12, "dT/dp at constant h");
    require_relative_near(
        result.derivatives
            .temperature_wrt_enthalpy_at_pressure,
        enthalpy_partial(extract_temperature), relative_tolerance,
        1.0e-12, "dT/dh at constant p");
    require_relative_near(
        result.derivatives
            .density_wrt_pressure_at_enthalpy,
        pressure_partial(density), relative_tolerance,
        1.0e-12, "drho/dp at constant h");
    require_relative_near(
        result.derivatives
            .density_wrt_enthalpy_at_pressure,
        enthalpy_partial(density), relative_tolerance,
        1.0e-12, "drho/dh at constant p");
    require_relative_near(
        result.derivatives
            .internal_energy_wrt_pressure_at_enthalpy,
        pressure_partial(internal_energy), relative_tolerance,
        1.0e-10, "du/dp at constant h");
    require_relative_near(
        result.derivatives
            .internal_energy_wrt_enthalpy_at_pressure,
        enthalpy_partial(internal_energy), relative_tolerance,
        1.0e-10, "du/dh at constant p");
    require_relative_near(
        result.derivatives
            .entropy_wrt_pressure_at_enthalpy,
        pressure_partial(entropy), relative_tolerance,
        1.0e-12, "ds/dp at constant h");
    require_relative_near(
        result.derivatives
            .entropy_wrt_enthalpy_at_pressure,
        enthalpy_partial(entropy), relative_tolerance,
        1.0e-12, "ds/dh at constant p");
    require_relative_near(
        result.derivatives
            .vapor_quality_wrt_pressure_at_enthalpy,
        pressure_partial(vapor_quality), relative_tolerance,
        1.0e-12, "dx/dp at constant h");
    require_relative_near(
        result.derivatives
            .vapor_quality_wrt_enthalpy_at_pressure,
        enthalpy_partial(vapor_quality), relative_tolerance,
        1.0e-12, "dx/dh at constant p");
    require_relative_near(
        result.derivatives.cp_wrt_pressure_at_enthalpy,
        pressure_partial(cp), relative_tolerance,
        1.0e-12, "dcp/dp at constant h");
    require_relative_near(
        result.derivatives.cp_wrt_enthalpy_at_pressure,
        enthalpy_partial(cp), relative_tolerance,
        1.0e-12, "dcp/dh at constant p");
    require_relative_near(
        result.derivatives.cv_wrt_pressure_at_enthalpy,
        pressure_partial(cv), relative_tolerance,
        1.0e-12, "dcv/dp at constant h");
    require_relative_near(
        result.derivatives.cv_wrt_enthalpy_at_pressure,
        enthalpy_partial(cv), relative_tolerance,
        1.0e-12, "dcv/dh at constant p");
    require_relative_near(
        result.derivatives
            .speed_of_sound_wrt_pressure_at_enthalpy,
        pressure_partial(speed_of_sound), relative_tolerance,
        1.0e-12, "da/dp at constant h");
    require_relative_near(
        result.derivatives
            .speed_of_sound_wrt_enthalpy_at_pressure,
        enthalpy_partial(speed_of_sound), relative_tolerance,
        1.0e-12, "da/dh at constant p");
}

void verify_ph_transport_derivatives(
    const thermox::physics::PropertyPackage& package,
    double pressure, double temperature) {
    const auto pt = package.state_pt(pressure, temperature);
    require(pt.ok(), "transport derivative reference PT state");
    const double enthalpy = pt.state.enthalpy_j_kg;
    const auto result = thermox::physics::
        state_ph_transport_derivatives_with_fallback(
            package, pressure, enthalpy);
    require(
        result.ok(), std::string(package.name()) +
            " p-h transport derivatives: " + result.message);
    require(
        result.source == thermox::physics::
            PropertyDerivativeSource::finite_difference,
        "transport derivatives must report finite-difference provenance");
    require(
        result.state.viscosity_pa_s > 0.0 &&
            result.state.thermal_conductivity_w_m_k > 0.0,
        "transport derivative base state must be physical");

    const double pressure_step =
        std::max(pressure * 1.0e-5, 1.0);
    const double enthalpy_step =
        std::max(std::abs(enthalpy) * 1.0e-5, 1.0e-2);
    const auto pressure_lower =
        package.state_ph(pressure - pressure_step, enthalpy);
    const auto pressure_upper =
        package.state_ph(pressure + pressure_step, enthalpy);
    const auto enthalpy_lower =
        package.state_ph(pressure, enthalpy - enthalpy_step);
    const auto enthalpy_upper =
        package.state_ph(pressure, enthalpy + enthalpy_step);
    require(
        pressure_lower.ok() && pressure_upper.ok() &&
            enthalpy_lower.ok() && enthalpy_upper.ok(),
        "transport derivative reference stencil");
    const auto central = [](double lower, double upper, double step) {
        return (upper - lower) / (2.0 * step);
    };
    require_relative_near(
        result.derivatives.viscosity_wrt_pressure_at_enthalpy,
        central(
            pressure_lower.state.viscosity_pa_s,
            pressure_upper.state.viscosity_pa_s, pressure_step),
        2.0e-3, 1.0e-16, "dmu/dp at constant h");
    require_relative_near(
        result.derivatives.viscosity_wrt_enthalpy_at_pressure,
        central(
            enthalpy_lower.state.viscosity_pa_s,
            enthalpy_upper.state.viscosity_pa_s, enthalpy_step),
        2.0e-3, 1.0e-16, "dmu/dh at constant p");
    require_relative_near(
        result.derivatives
            .thermal_conductivity_wrt_pressure_at_enthalpy,
        central(
            pressure_lower.state.thermal_conductivity_w_m_k,
            pressure_upper.state.thermal_conductivity_w_m_k,
            pressure_step),
        2.0e-3, 1.0e-14, "dk/dp at constant h");
    require_relative_near(
        result.derivatives
            .thermal_conductivity_wrt_enthalpy_at_pressure,
        central(
            enthalpy_lower.state.thermal_conductivity_w_m_k,
            enthalpy_upper.state.thermal_conductivity_w_m_k,
            enthalpy_step),
        2.0e-3, 1.0e-14, "dk/dh at constant p");
}

void verify_solver_bridge(const thermox::physics::PropertyPackage& package,
                          double pressure, double target_temperature,
                          double initial_temperature, double tolerance) {
    const auto target = package.state_pt(pressure, target_temperature);
    require(target.ok(), "target property state failed");
    thermox::EquationSystemBuilder builder;
    const auto temperature =
        builder.add_variable("temperature", initial_temperature, 500.0,
                             package.limits().minimum_temperature_k,
                             package.limits().maximum_temperature_k);
    builder.add_equation(
        "enthalpy_target",
        [&package, pressure, temperature, target](const std::vector<double>& x) {
            const auto state = package.state_pt(pressure, x.at(temperature));
            return state.ok()
                       ? state.state.enthalpy_j_kg - target.state.enthalpy_j_kg
                       : 1e12;
        },
        std::max(1.0, std::abs(target.state.enthalpy_j_kg)));
    thermox::SolverOptions options;
    options.residual_tolerance = 1e-9;
    const auto solved = thermox::solve_newton(builder.build(), options);
    require(solved.diagnostics.converged,
            std::string(package.name()) + " solver bridge: " +
                solved.diagnostics.message);
    require_near(solved.x.at(temperature), target_temperature, tolerance,
                 std::string(package.name()) + " solved temperature");
}

void verify_transient_bridge(const thermox::physics::PropertyPackage& package) {
    thermox::DaeProblem problem;
    problem.variable_names = {"temperature"};
    problem.residual_names = {"lumped_energy_balance"};
    problem.variable_kinds = {thermox::DaeVariableKind::differential};
    problem.initial_state = {340.0};
    problem.initial_derivative = {0.0};
    problem.variable_scales = {300.0};
    problem.derivative_scales = {10.0};
    problem.residual_scales = {1e4};
    problem.lower_bounds = {300.0};
    problem.upper_bounds = {500.0};
    problem.residual =
        [&package](double, const std::vector<double>& state,
                   const std::vector<double>& derivative,
                   std::vector<double>& residual) {
            const auto properties = package.state_pt(1e5, state[0]);
            if (!properties.ok())
                return thermox::EvaluationStatus::recoverable(properties.message);
            residual[0] = properties.state.cp_j_kg_k * derivative[0] +
                          100.0 * (state[0] - 300.0);
            return thermox::EvaluationStatus::success();
        };
    thermox::TimeIntegrationOptions options;
    options.end_time = 0.5;
    options.initial_step = 0.05;
    options.max_step = 0.1;
    options.absolute_tolerance = 1e-5;
    options.relative_tolerance = 1e-4;
    const auto result = thermox::integrate_dae(problem, options);
    require(result.diagnostics.success,
            std::string(package.name()) + " transient bridge: " +
                result.diagnostics.message);
    require(result.trajectory.back().state[0] < problem.initial_state[0],
            "property-backed thermal inventory must cool");
    require(result.trajectory.back().state[0] > 300.0,
            "property-backed thermal inventory must remain above ambient");
}

void verify_saturation_pairs(
    const thermox::physics::PropertyPackage& package,
    double pressure) {
    require(
        package.supports(
            thermox::physics::PropertyCapability::saturation_p),
        std::string(package.name()) +
            " should advertise saturation_p");
    require(
        package.supports(
            thermox::physics::PropertyCapability::surface_tension),
        std::string(package.name()) +
            " should advertise surface_tension");
    const auto saturation = package.saturation_p(pressure);
    require(
        saturation.ok(),
        std::string(package.name()) +
            " saturation: " + saturation.message);
    require(
        std::isfinite(saturation.surface_tension_n_m) &&
            saturation.surface_tension_n_m > 0.0,
        std::string(package.name()) +
            " saturation surface tension must be positive and finite");
    require_near(
        saturation.liquid.pressure_pa, pressure,
        pressure * 1.0e-6,
        std::string(package.name()) +
            " saturated-liquid pressure");
    require_near(
        saturation.vapor.pressure_pa, pressure,
        pressure * 1.0e-6,
        std::string(package.name()) +
            " saturated-vapor pressure");
    require_near(
        saturation.liquid.temperature_k,
        saturation.vapor.temperature_k, 1.0e-8,
        std::string(package.name()) +
            " saturation temperature agreement");
    require(
        saturation.liquid.phase ==
            thermox::physics::Phase::liquid,
        "saturated-liquid phase classification");
    require(
        saturation.vapor.phase ==
            thermox::physics::Phase::vapor,
        "saturated-vapor phase classification");
    require_near(
        saturation.liquid.vapor_quality, 0.0, 0.0,
        "saturated-liquid quality");
    require_near(
        saturation.vapor.vapor_quality, 1.0, 0.0,
        "saturated-vapor quality");
    require(
        saturation.liquid.enthalpy_j_kg <
            saturation.vapor.enthalpy_j_kg,
        "saturation latent heat must be positive");
    const auto liquid_ph = package.state_ph(
        pressure, saturation.liquid.enthalpy_j_kg);
    const auto vapor_ph = package.state_ph(
        pressure, saturation.vapor.enthalpy_j_kg);
    require(liquid_ph.ok() && vapor_ph.ok(),
            "PH must reconstruct exact saturation endpoints");
    require_near(liquid_ph.state.vapor_quality, 0.0, 1.0e-10,
                 "PH saturated-liquid quality");
    require_near(vapor_ph.state.vapor_quality, 1.0, 1.0e-10,
                 "PH saturated-vapor quality");

    const auto mixture_ph = package.state_ph(
        pressure,
        0.5 * (saturation.liquid.enthalpy_j_kg +
               saturation.vapor.enthalpy_j_kg));
    require(mixture_ph.ok(), "PH must represent a two-phase mixture");
    require_near(mixture_ph.state.vapor_quality, 0.5, 1.0e-8,
                 "PH two-phase quality");
    require(
        mixture_ph.state.phase == thermox::physics::Phase::two_phase,
        "PH two-phase classification");
    require(
        mixture_ph.state.density_kg_m3 > 0.0 &&
            std::isfinite(mixture_ph.state.internal_energy_j_kg),
        "PH two-phase core properties");
}

void verify_surface_tension_contracts(
    const thermox::physics::PropertyPackage& co2,
    const thermox::physics::PropertyPackage& if97,
    const thermox::physics::PropertyPackage& water_heos) {
    const auto co2_low = co2.saturation_p(1.0e6);
    const auto co2_high = co2.saturation_p(7.2e6);
    const auto water_low = if97.saturation_p(1.0e5);
    const auto water_high = if97.saturation_p(20.0e6);
    const auto heos_water = water_heos.saturation_p(1.0e5);
    require(
        co2_low.ok() && co2_high.ok() && water_low.ok() &&
            water_high.ok() && heos_water.ok(),
        "surface-tension reference saturation states must resolve");
    require(
        co2_low.surface_tension_n_m >
                co2_high.surface_tension_n_m &&
            water_low.surface_tension_n_m >
                water_high.surface_tension_n_m,
        "surface tension must decrease toward each critical point");
    require_relative_near(
        water_low.surface_tension_n_m,
        heos_water.surface_tension_n_m,
        5.0e-4, 1.0e-8,
        "IF97 and HEOS water surface tension agreement");
}

void verify_if97_derivatives_near_vapor_boundary(
    const thermox::physics::PropertyPackage& package) {
    const auto saturation = package.saturation_p(1.0e6);
    require(saturation.ok(), "IF97 saturation derivative reference");
    const double enthalpy =
        saturation.vapor.enthalpy_j_kg - 100.0;
    const auto result =
        thermox::physics::state_ph_derivatives_with_fallback(
            package, 1.0e6, enthalpy);
    require(result.ok(),
            "IF97 derivative fallback near vapor boundary: " +
                result.message);
    require(result.state.phase == thermox::physics::Phase::two_phase,
            "IF97 derivative reference remains inside saturation dome");
    require(
        std::isfinite(result.derivatives.density_wrt_pressure_at_enthalpy) &&
            std::isfinite(result.derivatives.density_wrt_enthalpy_at_pressure) &&
            std::isfinite(result.derivatives.internal_energy_wrt_pressure_at_enthalpy) &&
            std::isfinite(result.derivatives.internal_energy_wrt_enthalpy_at_pressure) &&
            std::isfinite(result.derivatives.entropy_wrt_pressure_at_enthalpy) &&
            std::isfinite(result.derivatives.entropy_wrt_enthalpy_at_pressure) &&
            std::isfinite(result.derivatives.vapor_quality_wrt_pressure_at_enthalpy) &&
            std::isfinite(result.derivatives.vapor_quality_wrt_enthalpy_at_pressure) &&
            std::isfinite(result.derivatives.cp_wrt_pressure_at_enthalpy) &&
            std::isfinite(result.derivatives.cp_wrt_enthalpy_at_pressure),
        "IF97 one-sided boundary derivatives must remain finite");
    require_relative_near(
        result.derivatives
            .vapor_quality_wrt_enthalpy_at_pressure,
        1.0 / (
            saturation.vapor.enthalpy_j_kg -
            saturation.liquid.enthalpy_j_kg),
        1.0e-5, 1.0e-12,
        "IF97 two-phase quality derivative at fixed pressure");
}

void verify_heos_two_phase_derivative_fallback(
    const thermox::physics::PropertyPackage& package) {
    const auto saturation = package.saturation_p(1.0e6);
    require(saturation.ok(), "HEOS saturation derivative reference");
    const auto result =
        thermox::physics::state_ph_derivatives_with_fallback(
            package, 1.0e6,
            0.5 * (saturation.liquid.enthalpy_j_kg +
                   saturation.vapor.enthalpy_j_kg));
    require(result.ok(),
            "HEOS two-phase derivative fallback: " + result.message);
    require(result.source ==
                thermox::physics::PropertyDerivativeSource::finite_difference &&
            result.state.phase == thermox::physics::Phase::two_phase,
            "HEOS two-phase state must use the bounded derivative fallback");
    require(
        std::isfinite(
            result.derivatives
                .vapor_quality_wrt_pressure_at_enthalpy) &&
            std::isfinite(
                result.derivatives
                    .vapor_quality_wrt_enthalpy_at_pressure) &&
            result.derivatives
                    .vapor_quality_wrt_enthalpy_at_pressure > 0.0,
        "HEOS two-phase fallback must retain usable quality "
        "partials");
}

void verify_water_reference_points(
    const thermox::physics::PropertyPackage& package) {
    const auto region_1 = package.state_pt(3e6, 300.0);
    require(region_1.ok(), "water region 1 reference point");
    require_near(region_1.state.density_kg_m3, 997.85294, 0.01,
                 "IF97 region 1 density");
    require_near(region_1.state.enthalpy_j_kg, 115331.273, 0.2,
                 "IF97 region 1 enthalpy");
    require_near(region_1.state.entropy_j_kg_k, 392.294792, 0.002,
                 "IF97 region 1 entropy");
    require_near(region_1.state.cp_j_kg_k, 4173.01218, 0.02,
                 "IF97 region 1 cp");

    const auto region_2 = package.state_pt(30e6, 700.0);
    require(region_2.ok(), "water region 2 reference point");
    require_near(region_2.state.density_kg_m3, 184.180169, 0.01,
                 "IF97 region 2 density");
    require_near(region_2.state.enthalpy_j_kg, 2631494.74, 0.5,
                 "IF97 region 2 enthalpy");
    require_near(region_2.state.entropy_j_kg_k, 5175.40298, 0.01,
                 "IF97 region 2 entropy");

    const auto region_3 = package.state_pt(25.5837018e6, 650.0);
    require(region_3.ok(), "water region 3 reference point");
    require_near(region_3.state.density_kg_m3, 500.0, 0.02,
                 "IF97 region 3 density");
    require_near(region_3.state.enthalpy_j_kg, 1863430.19, 5.0,
                 "IF97 region 3 enthalpy");

    const auto region_5 = package.state_pt(0.5e6, 1500.0);
    require(region_5.ok(), "water region 5 reference point");
    require_near(region_5.state.enthalpy_j_kg, 5219768.55, 1.0,
                 "IF97 region 5 enthalpy");
    require_near(region_5.state.entropy_j_kg_k, 9654.08875, 0.02,
                 "IF97 region 5 entropy");
}

void verify_co2_cycle_points(
    const thermox::physics::PropertyPackage& package) {
    const auto compressor_inlet = package.state_pt(8e6, 305.0);
    require(compressor_inlet.ok(), "sCO2 compressor inlet");
    require_near(compressor_inlet.state.density_kg_m3, 656.7657, 0.1,
                 "sCO2 near-critical density");
    require_near(compressor_inlet.state.cp_j_kg_k, 7312.5, 2.0,
                 "sCO2 near-critical cp");
    require(
        compressor_inlet.state.phase ==
            thermox::physics::Phase::supercritical,
        "sCO2 compressor inlet phase");

    verify_round_trip(package, 8e6, 305.0, 0.02);
    verify_round_trip(package, 20e6, 700.0, 0.02);

    const auto subcritical_liquid = package.state_pt(4e6, 278.0);
    const auto subcritical_vapor = package.state_pt(4e6, 279.0);
    require(subcritical_liquid.ok() && subcritical_vapor.ok(),
            "CO2 subcritical phase points");
    require(
        subcritical_liquid.state.phase ==
            thermox::physics::Phase::liquid,
        "CO2 liquid phase below saturation temperature");
    require(
        subcritical_vapor.state.phase ==
            thermox::physics::Phase::vapor,
        "CO2 vapor phase above saturation temperature");
}

void verify_thermochemistry_contracts() {
    const thermox::physics::SpeciesComposition air{
        thermox::physics::CompositionBasis::mole_fraction,
        {"O2", "N2"},
        {0.21, 0.79},
    };
    require_near(
        air.fraction("O2"), 0.21, 0.0,
        "composition exposes named fractions");
    bool invalid_sum_rejected = false;
    try {
        (void)thermox::physics::SpeciesComposition{
            thermox::physics::CompositionBasis::mass_fraction,
            {"O2", "N2"},
            {0.2, 0.7},
        };
    } catch (const std::invalid_argument&) {
        invalid_sum_rejected = true;
    }
    require(
        invalid_sum_rejected,
        "composition must reject fractions that do not sum to one");

    thermox::physics::ThermochemistryPackageRegistry registry;
    registry.register_backend(
        {
            "test",
            "test-thermochemistry",
            "1.0.0",
            {
                thermox::physics::ThermochemistryCapability::
                    state_pt,
                thermox::physics::ThermochemistryCapability::
                    state_ph,
                thermox::physics::ThermochemistryCapability::
                    equilibrium_hp,
            },
        },
        [](std::string_view mechanism,
           std::string_view phase) {
            return std::make_shared<
                const TestThermochemistryPackage>(
                std::string(mechanism), std::string(phase));
        });
    const auto package =
        registry.create("test", "gri30.yaml", "gri30");
    require(
        package->mechanism() == "gri30.yaml" &&
            package->phase() == "gri30" &&
            package->species_basis().size() == 5,
        "thermochemistry registry preserves mechanism, phase, "
        "and species basis");
}

#ifdef THERMOX_TEST_HAS_CANTERA
void verify_cantera_lower_heating_value() {
    thermox::physics::ThermochemistryPackageRegistry registry;
    thermox::physics::register_cantera_thermochemistry_backend(
        registry);
    const auto package =
        registry.create("cantera", "gri30.yaml", "gri30");
    require(
        package->supports(
            thermox::physics::ThermochemistryCapability::
                lower_heating_value),
        "Cantera advertises lower-heating-value support");

    const thermox::physics::SpeciesComposition methane{
        thermox::physics::CompositionBasis::mass_fraction,
        {"CH4"},
        {1.0},
    };
    const auto result = package->lower_heating_value(
        101325.0, 298.15, methane);
    require(result.ok(), result.message);
    require_near(
        result.lower_heating_value_j_kg,
        50.025e6,
        5.0e3,
        "Cantera methane LHV at 298.15 K");

    const auto invalid = package->lower_heating_value(
        -1.0, 298.15, methane);
    require(
        invalid.status ==
            thermox::physics::PropertyStatus::invalid_input,
        "heating-value service rejects an invalid reference pressure");
}
#endif

void verify_humid_air_ambient_state() {
    const auto humid =
        thermox::physics::humid_air_state_ptrh(
            101325.0, 300.0, 0.8);
    require(humid.ok(), humid.message);
    require(
        humid.state.humidity_ratio_kg_water_kg_dry_air >
                0.015 &&
            humid.state.humidity_ratio_kg_water_kg_dry_air <
                0.025,
        "humid-air humidity ratio is physically plausible");
    require_near(
        humid.state.water_mass_fraction,
        humid.state.humidity_ratio_kg_water_kg_dry_air /
            (1.0 +
             humid.state.humidity_ratio_kg_water_kg_dry_air),
        1.0e-15,
        "humidity ratio converts to humid-air mass fraction");
    require(
        humid.state.thermodynamic.density_kg_m3 > 1.0 &&
            humid.state.thermodynamic.density_kg_m3 < 1.3 &&
            humid.state.thermodynamic.cp_j_kg_k > 1000.0,
        "humid-air bulk properties are physically plausible");
    require(
        thermox::physics::humid_air_state_ptrh(
            101325.0, 300.0, 1.1)
                .status ==
            thermox::physics::PropertyStatus::invalid_input,
        "humid-air service rejects invalid relative humidity");
}

void verify_solar_salt_properties() {
    const thermox::physics::IncompressiblePropertyPackage salt{
        "SolarSalt"};
    require(
        salt.substance() == "NaK",
        "SolarSalt alias must resolve to the canonical CoolProp fluid");
    require(
        salt.supports(
            thermox::physics::PropertyCapability::transport) &&
            !salt.supports(
                thermox::physics::PropertyCapability::saturation_p),
        "solar salt must expose single-phase transport without a "
        "fabricated saturation contract");

    const double temperature = 823.15;
    const auto state = salt.state_pt(101325.0, temperature);
    require(state.ok(), state.message);
    // Formula-level checks against the published Zavoico correlations
    // embedded in CoolProp's NaK definition, evaluated about its
    // 273.15 K base. These are provider regressions, not independent
    // experimental validation.
    require_near(
        state.state.density_kg_m3,
        2090.0 - 0.636 * (temperature - 273.15), 1.0e-8,
        "solar-salt density correlation");
    require_near(
        state.state.cp_j_kg_k,
        1443.0 + 0.172 * (temperature - 273.15), 1.0e-8,
        "solar-salt heat-capacity correlation");
    require(
        state.state.viscosity_pa_s > 0.0 &&
            state.state.thermal_conductivity_w_m_k > 0.0 &&
            state.state.phase == thermox::physics::Phase::liquid,
        "solar salt must return physical transport and liquid phase");

    const auto ph = salt.state_ph(
        state.state.pressure_pa, state.state.enthalpy_j_kg);
    const auto ps = salt.state_ps(
        state.state.pressure_pa, state.state.entropy_j_kg_k);
    require(ph.ok() && ps.ok(), "solar-salt PH/PS round trips");
    require_near(
        ph.state.temperature_k, temperature, 1.0e-8,
        "solar-salt PH temperature round trip");
    require_near(
        ps.state.temperature_k, temperature, 1.0e-8,
        "solar-salt PS temperature round trip");
    require(
        salt.state_pt(101325.0, 550.0).status ==
            thermox::physics::PropertyStatus::out_of_range,
        "solar-salt temperature range must remain explicit");
    require(
        salt.saturation_p(101325.0).status ==
            thermox::physics::PropertyStatus::unsupported,
        "solar salt must reject saturation queries");
}

void verify_sandia_solar_salt_table() {
    const auto salt =
        thermox::physics::make_sandia_solar_salt_property_package();
    const double table_temperature =
        (550.0 - 32.0) * (5.0 / 9.0) + 273.15;
    const auto table_state = salt->state_pt(
        101325.0, table_temperature);
    require(table_state.ok(), table_state.message);
    require_near(
        table_state.state.density_kg_m3,
        118.98 * 16.01846337396014, 1.0e-10,
        "Sandia table density conversion");
    require_near(
        table_state.state.cp_j_kg_k,
        0.358 * 4186.8, 1.0e-10,
        "Sandia table heat-capacity conversion");

    const auto solar_two_cold = salt->state_pt(
        101325.0, 290.0 + 273.15);
    require(
        solar_two_cold.ok() &&
            solar_two_cold.state.phase ==
                thermox::physics::Phase::liquid,
        "Sandia table must cover the Solar Two 290 degC cold state");
    const auto ph = salt->state_ph(
        solar_two_cold.state.pressure_pa,
        solar_two_cold.state.enthalpy_j_kg);
    const auto ps = salt->state_ps(
        solar_two_cold.state.pressure_pa,
        solar_two_cold.state.entropy_j_kg_k);
    require(ph.ok() && ps.ok(), "Sandia table PH/PS round trips");
    require_near(
        ph.state.temperature_k, solar_two_cold.state.temperature_k,
        1.0e-10, "Sandia table PH temperature round trip");
    require_near(
        ps.state.temperature_k, solar_two_cold.state.temperature_k,
        1.0e-10, "Sandia table PS temperature round trip");
    require(
        salt->state_pt(101325.0, 250.0 + 273.15).status ==
            thermox::physics::PropertyStatus::out_of_range,
        "Sandia table must reject temperatures below 500 degF");
}

}  // namespace

int main() {
    const thermox::physics::IdealGasPropertyPackage ideal_gas;
    const thermox::physics::Co2PropertyPackage co2;
    const thermox::physics::If97PropertyPackage if97;
    const thermox::physics::CoolPropHeosPropertyPackage water_heos{"Water"};
    const thermox::physics::CoolPropHeosPropertyPackage r245fa{"R245fa"};
    verify_round_trip(ideal_gas, 2e5, 600.0, 1e-10);
    verify_round_trip(co2, 1e5, 320.0, 0.1);
    verify_round_trip(if97, 25e6, 873.0, 0.2);
    verify_round_trip(water_heos, 25e6, 873.0, 0.2);
    verify_round_trip(r245fa, 400e3, 336.0, 0.1);
    verify_ph_derivatives(
        ideal_gas, 2e5, 600.0, 1.0e-8,
        thermox::physics::PropertyDerivativeSource::analytic);
    verify_ph_derivatives(
        co2, 20e6, 700.0, 2.0e-4,
        thermox::physics::PropertyDerivativeSource::analytic);
    verify_ph_derivatives(
        if97, 6e6, 700.0, 2.0e-3,
        thermox::physics::PropertyDerivativeSource::
            finite_difference);
    verify_ph_derivatives(
        water_heos, 6e6, 700.0, 2.0e-4,
        thermox::physics::PropertyDerivativeSource::analytic);
    verify_ph_derivatives(
        r245fa, 400e3, 336.0, 2.0e-4,
        thermox::physics::PropertyDerivativeSource::analytic);
    verify_ph_transport_derivatives(co2, 20e6, 700.0);
    verify_ph_transport_derivatives(if97, 6e6, 700.0);
    verify_ph_transport_derivatives(water_heos, 6e6, 700.0);
    verify_ph_transport_derivatives(r245fa, 400e3, 336.0);
    require(
        thermox::physics::
            state_ph_transport_derivatives_with_fallback(
                ideal_gas, 2e5, 600000.0)
                .status ==
            thermox::physics::PropertyStatus::unsupported,
        "transport derivatives must reject providers without the "
        "transport capability");
    verify_co2_cycle_points(co2);
    verify_thermochemistry_contracts();
#ifdef THERMOX_TEST_HAS_CANTERA
    verify_cantera_lower_heating_value();
#endif
    verify_iso2314_equivalent_cooling();
    verify_humid_air_ambient_state();
    verify_solar_salt_properties();
    verify_sandia_solar_salt_table();
    verify_water_reference_points(if97);
    verify_solver_bridge(ideal_gas, 2e5, 700.0, 500.0, 1e-5);
    verify_solver_bridge(co2, 1e5, 340.0, 300.0, 0.02);
    verify_solver_bridge(if97, 6e6, 811.0, 700.0, 0.05);
    verify_transient_bridge(co2);
    verify_saturation_pairs(co2, 1e6);
    verify_saturation_pairs(co2, 7.2e6);
    verify_saturation_pairs(if97, 1e5);
    verify_saturation_pairs(if97, 20e6);
    verify_saturation_pairs(water_heos, 1e6);
    verify_saturation_pairs(r245fa, 150e3);
    verify_surface_tension_contracts(co2, if97, water_heos);
    verify_if97_derivatives_near_vapor_boundary(if97);
    verify_heos_two_phase_derivative_fallback(water_heos);
    require(!ideal_gas.supports(
                thermox::physics::PropertyCapability::saturation_p),
            "ideal gas should not advertise saturation_p");
    require(!ideal_gas.supports(
                thermox::physics::PropertyCapability::surface_tension),
            "ideal gas should not advertise surface_tension");
    require(ideal_gas.saturation_p(1e5).status ==
                thermox::physics::PropertyStatus::unsupported,
            "ideal-gas saturation should be unsupported");
    require(co2.state_pt(-1.0, 300.0).status ==
                thermox::physics::PropertyStatus::invalid_input,
            "CO2 invalid input status");
    require(if97.state_pt(1e5, 250.0).status ==
                thermox::physics::PropertyStatus::out_of_range,
            "IF97 range status");
    require(co2.saturation_p(1e5).status ==
                thermox::physics::PropertyStatus::out_of_range,
            "CO2 saturation below triple point");
    require(co2.saturation_p(8e6).status ==
                thermox::physics::PropertyStatus::out_of_range,
            "CO2 saturation above critical point");
    require(if97.saturation_p(23e6).status ==
                thermox::physics::PropertyStatus::out_of_range,
            "IF97 saturation above critical point");
}
