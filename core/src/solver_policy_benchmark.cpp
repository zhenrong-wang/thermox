#include "thermox/solver_policy_benchmark.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace thermox {

namespace {

std::vector<StructuralDecompositionPolicy> normalized_policies(
    const StructuralPolicyBenchmarkOptions& options) {
    std::vector<StructuralDecompositionPolicy> policies{
        StructuralDecompositionPolicy::monolithic};
    for (const auto policy : options.policies) {
        if (std::find(policies.begin(), policies.end(), policy) ==
            policies.end()) {
            policies.push_back(policy);
        }
    }
    return policies;
}

double maximum_normalized_difference(
    const NonlinearProblem& problem,
    const std::vector<double>& baseline,
    const std::vector<double>& candidate) {
    if (baseline.size() != candidate.size() ||
        baseline.size() != problem.initial_guess.size()) {
        return std::numeric_limits<double>::infinity();
    }
    double maximum = 0.0;
    for (std::size_t index = 0; index < baseline.size(); ++index) {
        const double scale = problem.variable_scales.empty()
            ? 1.0
            : problem.variable_scales.at(index);
        const double difference =
            std::abs(candidate[index] - baseline[index]) / scale;
        if (!std::isfinite(difference)) {
            return std::numeric_limits<double>::infinity();
        }
        maximum = std::max(maximum, difference);
    }
    return maximum;
}

}  // namespace

const StructuralPolicyBenchmarkEntry*
StructuralPolicyBenchmarkResult::find(
    StructuralDecompositionPolicy policy) const {
    const auto found = std::find_if(
        entries.begin(), entries.end(),
        [policy](const auto& entry) {
            return entry.policy == policy;
        });
    return found == entries.end() ? nullptr : &*found;
}

StructuralPolicyBenchmarkResult benchmark_structural_policies(
    const NonlinearProblem& problem,
    const SolverOptions& solver_options,
    const StructuralPolicyBenchmarkOptions& benchmark_options) {
    if (!std::isfinite(
            benchmark_options.normalized_solution_tolerance) ||
        benchmark_options.normalized_solution_tolerance <= 0.0) {
        throw std::invalid_argument(
            "normalized_solution_tolerance must be finite and positive");
    }
    if (solver_options.sparse_factorization ||
        solver_options.sparse_factorization_resolver) {
        throw std::invalid_argument(
            "structural policy benchmark requires factory-neutral solver "
            "options; a reusable sparse factorization would contaminate "
            "cross-policy work counts");
    }

    StructuralPolicyBenchmarkResult result;
    const auto policies = normalized_policies(benchmark_options);
    result.entries.reserve(policies.size());
    for (const auto policy : policies) {
        StructuralPolicyBenchmarkEntry entry;
        entry.policy = policy;
        SolverOptions policy_options = solver_options;
        policy_options.structural_decomposition_policy = policy;
        try {
            entry.solve = solve_newton(problem, policy_options);
            entry.executed = true;
            entry.message = entry.solve.diagnostics.message;
        } catch (const std::exception& error) {
            entry.message = error.what();
        }
        result.entries.push_back(std::move(entry));
    }

    const auto& baseline = result.entries.front();
    result.monolithic_baseline_converged =
        baseline.executed && baseline.solve.diagnostics.converged;
    result.all_policies_executed = true;
    result.all_policies_converged = true;
    result.all_policies_equivalent_to_monolithic =
        result.monolithic_baseline_converged;
    for (auto& entry : result.entries) {
        result.all_policies_executed =
            result.all_policies_executed && entry.executed;
        result.all_policies_converged =
            result.all_policies_converged && entry.executed &&
            entry.solve.diagnostics.converged;
        if (!result.monolithic_baseline_converged ||
            !entry.executed || !entry.solve.diagnostics.converged) {
            result.all_policies_equivalent_to_monolithic = false;
            continue;
        }
        entry.comparable_to_monolithic = true;
        entry.maximum_normalized_solution_difference =
            maximum_normalized_difference(
                problem, baseline.solve.x, entry.solve.x);
        entry.equivalent_to_monolithic =
            entry.maximum_normalized_solution_difference <=
            benchmark_options.normalized_solution_tolerance;
        result.all_policies_equivalent_to_monolithic =
            result.all_policies_equivalent_to_monolithic &&
            entry.equivalent_to_monolithic;
    }

    if (!result.monolithic_baseline_converged) {
        result.message =
            "monolithic baseline did not converge; policy equivalence "
            "is unavailable";
    } else if (!result.all_policies_executed) {
        result.message =
            "one or more requested policies could not execute";
    } else if (!result.all_policies_converged) {
        result.message =
            "one or more requested policies did not converge";
    } else if (!result.all_policies_equivalent_to_monolithic) {
        result.message =
            "one or more requested policies reached a solution outside "
            "the normalized equivalence tolerance";
    } else {
        result.message =
            "all requested policies converged to a monolithic-equivalent "
            "solution";
    }
    return result;
}

}  // namespace thermox
