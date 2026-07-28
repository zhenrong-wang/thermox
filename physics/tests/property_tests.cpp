#include "thermox/equation_system.hpp"
#include "thermox/transient_solver.hpp"
#include "thermox/physics/co2_package.hpp"
#include "thermox/physics/ideal_gas_package.hpp"
#include "thermox/physics/if97_package.hpp"
#include "thermox/physics/humid_air.hpp"
#include "thermox/physics/thermochemistry.hpp"

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
    thermox::physics::ThermochemicalResult equilibrate_hp(
        double,
        double,
        const thermox::physics::SpeciesComposition&)
        const override {
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
    const auto saturation = package.saturation_p(pressure);
    require(
        saturation.ok(),
        std::string(package.name()) +
            " saturation: " + saturation.message);
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

}  // namespace

int main() {
    const thermox::physics::IdealGasPropertyPackage ideal_gas;
    const thermox::physics::Co2PropertyPackage co2;
    const thermox::physics::If97PropertyPackage if97;
    verify_round_trip(ideal_gas, 2e5, 600.0, 1e-10);
    verify_round_trip(co2, 1e5, 320.0, 0.1);
    verify_round_trip(if97, 25e6, 873.0, 0.2);
    verify_co2_cycle_points(co2);
    verify_thermochemistry_contracts();
    verify_humid_air_ambient_state();
    verify_water_reference_points(if97);
    verify_solver_bridge(ideal_gas, 2e5, 700.0, 500.0, 1e-5);
    verify_solver_bridge(co2, 1e5, 340.0, 300.0, 0.02);
    verify_solver_bridge(if97, 6e6, 811.0, 700.0, 0.05);
    verify_transient_bridge(co2);
    verify_saturation_pairs(co2, 1e6);
    verify_saturation_pairs(co2, 7.2e6);
    verify_saturation_pairs(if97, 1e5);
    verify_saturation_pairs(if97, 20e6);
    require(!ideal_gas.supports(
                thermox::physics::PropertyCapability::saturation_p),
            "ideal gas should not advertise saturation_p");
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
