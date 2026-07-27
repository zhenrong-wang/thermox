#include "thermox/equation_system.hpp"
#include "thermox/transient_solver.hpp"
#include "thermox/physics/co2_package.hpp"
#include "thermox/physics/ideal_gas_package.hpp"
#include "thermox/physics/if97_package.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

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

}  // namespace

int main() {
    const thermox::physics::IdealGasPropertyPackage ideal_gas;
    const thermox::physics::Co2PropertyPackage co2;
    const thermox::physics::If97PropertyPackage if97;
    verify_round_trip(ideal_gas, 2e5, 600.0, 1e-10);
    verify_round_trip(co2, 1e5, 320.0, 0.1);
    verify_round_trip(if97, 25e6, 873.0, 0.2);
    verify_solver_bridge(ideal_gas, 2e5, 700.0, 500.0, 1e-5);
    verify_solver_bridge(co2, 1e5, 340.0, 300.0, 0.02);
    verify_solver_bridge(if97, 6e6, 811.0, 700.0, 0.05);
    verify_transient_bridge(co2);
    require(co2.state_pt(-1.0, 300.0).status ==
                thermox::physics::PropertyStatus::invalid_input,
            "CO2 invalid input status");
    require(if97.state_pt(1e5, 250.0).status ==
                thermox::physics::PropertyStatus::out_of_range,
            "IF97 range status");
}
