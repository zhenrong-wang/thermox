#pragma once

#include "thermox/nonlinear_solver.hpp"

#include <string>
#include <vector>

namespace thermox {

struct StructuralPolicyBenchmarkOptions {
    // Monolithic is always executed first as the comparison baseline even
    // when omitted here. Duplicate policies are executed only once.
    std::vector<StructuralDecompositionPolicy> policies{
        StructuralDecompositionPolicy::monolithic,
        StructuralDecompositionPolicy::tearing,
    };
    // Maximum absolute solution difference after division by each declared
    // variable scale (or one when scales are omitted).
    double normalized_solution_tolerance{1.0e-8};
};

struct StructuralPolicyBenchmarkEntry {
    StructuralDecompositionPolicy policy{
        StructuralDecompositionPolicy::monolithic};
    bool executed{false};
    NonlinearSolveResult solve;
    bool comparable_to_monolithic{false};
    bool equivalent_to_monolithic{false};
    double maximum_normalized_solution_difference{0.0};
    std::string message;
};

struct StructuralPolicyBenchmarkResult {
    bool monolithic_baseline_converged{false};
    bool all_policies_executed{false};
    bool all_policies_converged{false};
    bool all_policies_equivalent_to_monolithic{false};
    std::vector<StructuralPolicyBenchmarkEntry> entries;
    std::string message;

    [[nodiscard]] const StructuralPolicyBenchmarkEntry* find(
        StructuralDecompositionPolicy policy) const;
};

// Runs each policy from the exact same NonlinearProblem initial state. This is
// an explicit audit utility: it records convergence/equivalence evidence and
// never selects or mutates the caller's production solver policy.
[[nodiscard]] StructuralPolicyBenchmarkResult
benchmark_structural_policies(
    const NonlinearProblem& problem,
    const SolverOptions& solver_options = {},
    const StructuralPolicyBenchmarkOptions& benchmark_options = {});

}  // namespace thermox
