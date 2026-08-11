#include "thermox/equation_system.hpp"
#include "thermox/solver_policy_benchmark.hpp"
#include "thermox/sparse_linear_solver.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

thermox::NonlinearProblem coupled_linear_problem() {
    thermox::EquationSystemBuilder system;
    const auto x = system.add_variable("x", 0.0, 1.0);
    const auto y = system.add_variable("y", 0.0, 2.0);
    const auto z = system.add_variable("z", 0.0, 4.0);
    system.add_linear_equation(
        "balance_x", {{x, 3.0}, {y, 1.0}, {z, -1.0}}, 4.0);
    system.add_linear_equation(
        "balance_y", {{x, 1.0}, {y, 4.0}, {z, 1.0}}, 12.0);
    system.add_linear_equation(
        "balance_z", {{x, -1.0}, {y, 1.0}, {z, 5.0}}, 16.0);
    return system.build();
}

void test_compares_exact_policies_from_one_initial_state() {
    thermox::StructuralPolicyBenchmarkOptions options;
    options.policies = {
        thermox::StructuralDecompositionPolicy::tearing,
        thermox::StructuralDecompositionPolicy::monolithic,
        thermox::StructuralDecompositionPolicy::tearing,
    };
    const auto benchmark = thermox::benchmark_structural_policies(
        coupled_linear_problem(), {}, options);
    require(
        benchmark.entries.size() == 2 &&
            benchmark.entries.front().policy ==
                thermox::StructuralDecompositionPolicy::monolithic,
        "benchmark inserts one deterministic monolithic baseline");
    require(
        benchmark.monolithic_baseline_converged &&
            benchmark.all_policies_executed &&
            benchmark.all_policies_converged &&
            benchmark.all_policies_equivalent_to_monolithic,
        benchmark.message);
    const auto* torn = benchmark.find(
        thermox::StructuralDecompositionPolicy::tearing);
    require(
        torn != nullptr && torn->executed &&
            torn->comparable_to_monolithic &&
            torn->equivalent_to_monolithic &&
            torn->maximum_normalized_solution_difference < 1.0e-12 &&
            torn->solve.diagnostics.structural_tearing_attempts == 1 &&
            torn->solve.diagnostics.structural_tearing_successes == 1,
        "benchmark retains exact tearing correctness and work evidence");
}

void test_records_policy_execution_failure_without_hiding_baseline() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x"};
    problem.residual_names = {"identity"};
    problem.initial_guess = {0.0};
    problem.residual = [](
        const std::vector<double>& state,
        std::vector<double>& residual) {
        residual[0] = state[0] - 1.0;
    };
    const auto benchmark = thermox::benchmark_structural_policies(problem);
    const auto* monolithic = benchmark.find(
        thermox::StructuralDecompositionPolicy::monolithic);
    const auto* torn = benchmark.find(
        thermox::StructuralDecompositionPolicy::tearing);
    require(
        monolithic != nullptr && monolithic->executed &&
            monolithic->solve.diagnostics.converged,
        "monolithic baseline remains available");
    require(
        torn != nullptr && !torn->executed &&
            torn->message.find("incidence pattern") !=
                std::string::npos &&
            !benchmark.all_policies_executed &&
            !benchmark.all_policies_equivalent_to_monolithic,
        "unsupported policy is explicit benchmark evidence");
}

void test_rejects_cross_policy_factorization_cache_contamination() {
    thermox::SolverOptions solver;
    solver.sparse_factorization =
        thermox::make_default_sparse_factorization();
    try {
        (void)thermox::benchmark_structural_policies(
            coupled_linear_problem(), solver);
    } catch (const std::invalid_argument& error) {
        require(
            std::string(error.what()).find("contaminate") !=
                std::string::npos,
            "cache rejection explains controlled-work requirement");
        return;
    }
    throw std::runtime_error(
        "benchmark accepted a shared cross-policy factorization cache");
}

}  // namespace

int main() {
    try {
        test_compares_exact_policies_from_one_initial_state();
        test_records_policy_execution_failure_without_hiding_baseline();
        test_rejects_cross_policy_factorization_cache_contamination();
    } catch (const std::exception& error) {
        std::cerr << "solver policy benchmark tests failed: "
                  << error.what() << '\n';
        return 1;
    }
    std::cout << "solver policy benchmark tests passed\n";
    return 0;
}
