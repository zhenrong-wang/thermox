#pragma once

#include "thermox/linear_solver.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace thermox {

using ResidualFunction = std::function<void(const std::vector<double>& x, std::vector<double>& residual)>;
using JacobianFunction = std::function<void(const std::vector<double>& x, Matrix& jacobian)>;
using SparseJacobianFunction =
    std::function<void(const std::vector<double>& x, std::vector<SparseTriplet>& jacobian)>;
using SparseJacobianValuesFunction =
    std::function<void(const std::vector<double>& x, std::vector<double>& values)>;
using ContinuationJacobianFunction =
    std::function<void(const std::vector<double>& x,
                       const std::vector<double>& anchor,
                       double parameter,
                       Matrix& jacobian)>;
using ContinuationSparseJacobianFunction =
    std::function<void(const std::vector<double>& x,
                       const std::vector<double>& anchor,
                       double parameter,
                       std::vector<SparseTriplet>& jacobian)>;
using ContinuationSparseJacobianValuesFunction =
    std::function<void(const std::vector<double>& x,
                       const std::vector<double>& anchor,
                       double parameter,
                       std::vector<double>& values)>;

enum class EvaluationStatusCode {
    success,
    recoverable_failure,
    fatal_failure,
};

enum class StructuralDecompositionPolicy {
    automatic,
    monolithic,
    blocks,
    // Use structurally suggested feedback variables to partition each
    // Newton linearization through an exact Schur complement.
    tearing,
};

struct EvaluationStatus {
    EvaluationStatusCode code{EvaluationStatusCode::success};
    std::string message;

    [[nodiscard]] bool ok() const { return code == EvaluationStatusCode::success; }
    static EvaluationStatus success() { return {}; }
    static EvaluationStatus recoverable(std::string message);
    static EvaluationStatus fatal(std::string message);
};

using CheckedResidualFunction =
    std::function<EvaluationStatus(const std::vector<double>& x, std::vector<double>& residual)>;
// Evaluates only the requested residual rows. The output order matches
// residual_indices and its size must remain unchanged.
using CheckedResidualSubsetFunction =
    std::function<EvaluationStatus(
        const std::vector<double>& x,
        const std::vector<std::size_t>& residual_indices,
        std::vector<double>& residual)>;
// Evaluates only requested offsets in the declared fixed CSR pattern. The
// output order matches value_offsets and its size must remain unchanged.
using SparseJacobianValuesSubsetFunction =
    std::function<void(
        const std::vector<double>& x,
        const std::vector<std::size_t>& value_offsets,
        std::vector<double>& values)>;
using ContinuationCheckedResidualFunction =
    std::function<EvaluationStatus(const std::vector<double>& x,
                                   const std::vector<double>& anchor,
                                   double parameter,
                                   std::vector<double>& residual)>;
using ContinuationCheckedResidualSubsetFunction =
    std::function<EvaluationStatus(
        const std::vector<double>& x,
        const std::vector<double>& anchor,
        double parameter,
        const std::vector<std::size_t>& residual_indices,
        std::vector<double>& residual)>;
using ContinuationSparseJacobianValuesSubsetFunction =
    std::function<void(
        const std::vector<double>& x,
        const std::vector<double>& anchor,
        double parameter,
        const std::vector<std::size_t>& value_offsets,
        std::vector<double>& values)>;

struct SolverOptions {
    int max_iterations{50};
    double residual_tolerance{1.0e-9};
    double step_tolerance{1.0e-10};
    // Maximum normalized backward error accepted from any dense, sparse, or
    // custom linear backend solving the dimensionless Newton system.
    double linear_residual_tolerance{1.0e-10};
    // Automatic selects blocks only when the fixed pattern is reducible, the
    // provider certifies root equivalence, and block-local residual and
    // Jacobian evaluation are available.
    StructuralDecompositionPolicy structural_decomposition_policy{
        StructuralDecompositionPolicy::automatic};
    // Relative perturbation applied to the larger of the declared variable
    // scale and current magnitude. Interior columns use a central difference;
    // physical-domain boundaries fall back to the valid one-sided difference.
    double finite_difference_epsilon{1.0e-6};
    double min_damping{1.0e-6};
    double damping_reduction{0.5};
    double sufficient_decrease{1.0e-4};
    int max_line_search_steps{50};
    // Custom backends receive the dimensionless scaled Newton system and must return
    // a step in scaled variable coordinates.
    LinearSolverFunction linear_solver;
    SparseLinearSolverFunction sparse_linear_solver;
    // Reused across Newton iterations and, when supplied by an integrator,
    // across transient stages. Custom one-shot hooks take precedence, and an
    // explicit factorization takes precedence over a resolver.
    SparseFactorizationPtr sparse_factorization;
    // Resolves a reusable factorization for an exact fixed CSR pattern. This
    // prevents symbolic-cache thrashing when structural blocks have different
    // patterns and are revisited by continuation or transient stages.
    SparseFactorizationResolver sparse_factorization_resolver;
};

struct IterationDiagnostic {
    int iteration{0};
    double residual_norm{0.0};
    double accepted_residual_norm{0.0};
    double step_norm{0.0};
    double damping{1.0};
};

struct SolverDiagnostics {
    bool converged{false};
    int iterations{0};
    double final_residual_norm{0.0};
    double final_maximum_absolute_normalized_residual{0.0};
    std::string limiting_residual;
    double final_step_norm{0.0};
    int function_evaluations{0};
    int jacobian_evaluations{0};
    int linear_solver_evaluations{0};
    int symbolic_factorizations{0};
    int numeric_factorizations{0};
    int factorization_quality_observations{0};
    double last_reciprocal_pivot_ratio{0.0};
    double minimum_reciprocal_pivot_ratio{0.0};
    double minimum_absolute_pivot_at_minimum_ratio{0.0};
    double maximum_absolute_pivot_at_minimum_ratio{0.0};
    std::size_t accepted_pivot_count_at_minimum_ratio{0};
    std::size_t factorization_size_at_minimum_ratio{0};
    std::string factorization_quality_method;
    double last_linear_backward_error{0.0};
    double maximum_linear_backward_error{0.0};
    int linear_refinement_attempts{0};
    int linear_refinement_successes{0};
    int structural_block_solves{0};
    std::size_t largest_linear_system_size{0};
    int structural_tearing_attempts{0};
    int structural_tearing_successes{0};
    int structural_tearing_fallbacks{0};
    std::size_t largest_tearing_inner_system_size{0};
    std::size_t largest_tearing_outer_system_size{0};
    std::size_t largest_tearing_inner_nonzero_count{0};
    std::string last_structural_tearing_fallback;
    std::string failed_structural_block;
    std::string linear_solver_backend{"not-used"};
    std::string message;
    std::vector<IterationDiagnostic> history;
};

struct NonlinearProblem {
    std::vector<std::string> variable_names;
    std::vector<std::string> residual_names;
    std::vector<double> initial_guess;
    std::vector<double> variable_scales;
    std::vector<double> residual_scales;
    std::vector<double> lower_bounds;
    std::vector<double> upper_bounds;
    // A provider/compiler assertion that dependency-ordered block solving is
    // root-equivalent to its monolithic formulation (for example, a fully
    // linear assembled system). Automatic policy never infers this from
    // sparsity alone.
    bool automatic_structural_decomposition_safe{false};
    ResidualFunction residual;
    CheckedResidualFunction checked_residual;
    CheckedResidualSubsetFunction checked_residual_subset;
    JacobianFunction jacobian;
    SparseJacobianFunction sparse_jacobian;
    // Complete structural incidence supplied by the compiler. It may be an
    // over-approximation and does not imply fixed-CSR value evaluation.
    std::optional<SparsePattern> structural_jacobian_pattern;
    std::optional<SparsePattern> sparse_jacobian_pattern;
    SparseJacobianValuesFunction sparse_jacobian_values;
    SparseJacobianValuesSubsetFunction
        sparse_jacobian_values_subset;
    SparseJacobianFunction partial_sparse_jacobian;
    std::vector<bool> analytic_jacobian_rows;
    // Optional component- or model-informed path. At parameter 1
    // these callbacks must represent the ordinary target problem.
    // When this flag is true, the informed callbacks already define the
    // complete homotopy and must not be blended again with the solver's
    // generic diagonal anchor problem.
    bool continuation_path_is_complete{false};
    ContinuationCheckedResidualFunction continuation_checked_residual;
    ContinuationCheckedResidualSubsetFunction
        continuation_checked_residual_subset;
    ContinuationJacobianFunction continuation_jacobian;
    ContinuationSparseJacobianFunction continuation_sparse_jacobian;
    ContinuationSparseJacobianValuesFunction
        continuation_sparse_jacobian_values;
    ContinuationSparseJacobianValuesSubsetFunction
        continuation_sparse_jacobian_values_subset;
    ContinuationSparseJacobianFunction
        continuation_partial_sparse_jacobian;
};

struct NonlinearSolveResult {
    std::vector<double> x;
    SolverDiagnostics diagnostics;
};

enum class StructuralRegionKind {
    underdetermined,
    overdetermined,
    well_determined,
};

struct StructuralRegion {
    StructuralRegionKind kind{StructuralRegionKind::well_determined};
    std::vector<std::string> variable_names;
    std::vector<std::string> residual_names;
};

struct StructuralBlock {
    std::vector<std::size_t> variable_indices;
    std::vector<std::size_t> residual_indices;
    std::vector<std::string> variable_names;
    std::vector<std::string> residual_names;
    // Deterministic structural feedback-variable suggestion. Removing these
    // matched variables makes the block dependency graph acyclic. This is a
    // diagnostic only; it does not establish that numerical tearing is safe.
    std::vector<std::size_t> suggested_tear_variable_indices;
    std::vector<std::string> suggested_tear_variable_names;
    bool acyclic_after_suggested_tears{false};
    // Raw structural costs for the suggested A/B/C/D partition. These are
    // incidence counts, not estimates of numerical rank or conditioning.
    std::size_t structural_nonzero_count{0};
    std::size_t suggested_inner_variable_count{0};
    std::size_t suggested_inner_nonzero_count{0};
    std::size_t suggested_tear_coupling_nonzero_count{0};
    std::size_t suggested_dense_schur_entry_count{0};
};

struct ProblemStructureReport {
    std::size_t variable_count{0};
    std::size_t residual_count{0};
    bool square{false};
    bool has_duplicate_variable_names{false};
    bool has_duplicate_residual_names{false};
    bool has_complete_sparse_pattern{false};
    bool structurally_nonsingular{false};
    std::vector<std::string> duplicate_variable_names;
    std::vector<std::string> duplicate_residual_names;
    std::vector<std::string> unmatched_variable_names;
    std::vector<std::string> unmatched_residual_names;
    std::vector<StructuralRegion> structural_regions;
    // Irreducible square blocks in dependency-first block-triangular order.
    // Available when the complete incidence pattern is structurally
    // nonsingular.
    std::vector<StructuralBlock> structural_blocks;
    std::vector<std::string> messages;

    [[nodiscard]] bool valid_for_newton() const;
};

ProblemStructureReport analyze_incidence_structure(
    const std::vector<std::string>& variable_names,
    const std::vector<std::string>& residual_names,
    const std::vector<std::vector<std::size_t>>&
        residual_variable_incidence);

struct JacobianVerificationOptions {
    double finite_difference_epsilon{1.0e-6};
    double absolute_tolerance{1.0e-6};
    double relative_tolerance{1.0e-4};
    std::size_t maximum_reported_mismatches{32};
};

struct JacobianMismatch {
    std::size_t residual{0};
    std::size_t variable{0};
    std::string residual_name;
    std::string variable_name;
    double provided_derivative{0.0};
    double numerical_derivative{0.0};
    double absolute_error{0.0};
    double relative_error{0.0};
};

struct JacobianVerificationReport {
    bool analytic_derivatives_available{false};
    bool passed{false};
    std::size_t compared_rows{0};
    std::size_t compared_entries{0};
    std::size_t mismatch_count{0};
    double maximum_absolute_error{0.0};
    double maximum_relative_error{0.0};
    std::vector<JacobianMismatch> mismatches;
    std::string message;
};

ProblemStructureReport analyze_problem_structure(const NonlinearProblem& problem);
JacobianVerificationReport verify_problem_jacobian(
    const NonlinearProblem& problem,
    const std::vector<double>& point = {},
    const JacobianVerificationOptions& options = {});
NonlinearSolveResult solve_newton(const NonlinearProblem& problem, const SolverOptions& options = {});

double l2_norm(const std::vector<double>& values);

}  // namespace thermox
