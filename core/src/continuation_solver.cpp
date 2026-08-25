#include "thermox/continuation_solver.hpp"

#include "thermox/sparse_linear_solver.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace thermox {
namespace {

void validate_continuation_options(
    const ContinuationOptions& options) {
    if (!std::isfinite(options.initial_step) ||
        options.initial_step <= 0.0 ||
        options.initial_step > 1.0 ||
        !std::isfinite(options.minimum_step) ||
        options.minimum_step <= 0.0 ||
        options.minimum_step > options.initial_step ||
        !std::isfinite(options.step_growth) ||
        options.step_growth <= 1.0 ||
        !std::isfinite(options.step_reduction) ||
        options.step_reduction <= 0.0 ||
        options.step_reduction >= 1.0 ||
        options.maximum_stages <= 0) {
        throw std::invalid_argument(
            "invalid continuation options");
    }
}

std::vector<double> scales_or_one(
    const std::vector<double>& supplied,
    std::size_t size) {
    return supplied.empty()
        ? std::vector<double>(size, 1.0)
        : supplied;
}

struct FixedPatternMapping {
    SparsePattern pattern;
    std::vector<std::size_t> target_offsets;
    std::vector<std::size_t> diagonal_offsets;
};

FixedPatternMapping make_fixed_pattern_mapping(
    const SparsePattern& target) {
    std::vector<SparseTriplet> structure;
    structure.reserve(
        target.nonzeros() + target.rows());
    for (std::size_t row = 0; row < target.rows(); ++row) {
        for (std::size_t offset = target.row_offsets()[row];
             offset < target.row_offsets()[row + 1]; ++offset) {
            structure.push_back(
                {row, target.column_indices()[offset], 1.0});
        }
        structure.push_back({row, row, 1.0});
    }
    FixedPatternMapping mapping;
    mapping.pattern = sparse_from_triplets(
                          target.rows(), target.columns(),
                          std::move(structure))
                          .pattern();
    mapping.target_offsets.resize(target.nonzeros());
    mapping.diagonal_offsets.resize(target.rows());
    for (std::size_t row = 0; row < target.rows(); ++row) {
        std::size_t target_offset = target.row_offsets()[row];
        for (std::size_t offset =
                 mapping.pattern.row_offsets()[row];
             offset <
                 mapping.pattern.row_offsets()[row + 1];
             ++offset) {
            const auto column =
                mapping.pattern.column_indices()[offset];
            if (column == row) {
                mapping.diagonal_offsets[row] = offset;
            }
            if (target_offset <
                    target.row_offsets()[row + 1] &&
                column ==
                    target.column_indices()[target_offset]) {
                mapping.target_offsets[target_offset] = offset;
                ++target_offset;
            }
        }
    }
    return mapping;
}

EvaluationStatus evaluate_target_residual(
    const NonlinearProblem& target,
    const std::vector<double>& x,
    const std::vector<double>& anchor,
    double parameter,
    std::vector<double>& residual) {
    if (target.continuation_checked_residual) {
        return target.continuation_checked_residual(
            x, anchor, parameter, residual);
    }
    if (target.checked_residual) {
        return target.checked_residual(x, residual);
    }
    target.residual(x, residual);
    return EvaluationStatus::success();
}

NonlinearProblem make_stage_problem(
    const NonlinearProblem& target,
    const std::vector<double>& anchor,
    const std::vector<double>& warm_start,
    double parameter) {
    const std::size_t size = anchor.size();
    const auto variable_scales =
        scales_or_one(target.variable_scales, size);
    const auto residual_scales =
        scales_or_one(target.residual_scales, size);
    const bool uses_informed_residual =
        static_cast<bool>(
            target.continuation_checked_residual);
    const double target_weight =
        target.continuation_path_is_complete ? 1.0 : parameter;
    const double anchor_weight =
        target.continuation_path_is_complete ? 0.0 : 1.0 - parameter;
    NonlinearProblem stage = target;
    stage.initial_guess = warm_start;
    stage.residual = {};
    stage.checked_residual_subset = {};
    stage.sparse_jacobian_values_subset = {};
    stage.checked_residual =
        [&target, anchor, variable_scales, residual_scales,
         parameter, target_weight, anchor_weight](
            const std::vector<double>& x,
            std::vector<double>& residual) {
            std::vector<double> target_residual(
                residual.size(), 0.0);
            const auto status = evaluate_target_residual(
                target, x, anchor, parameter,
                target_residual);
            if (!status.ok()) return status;
            for (std::size_t row = 0;
                 row < residual.size(); ++row) {
                const double anchor_residual =
                    residual_scales[row] *
                    (x[row] - anchor[row]) /
                    variable_scales[row];
                residual[row] =
                    target_weight * target_residual[row] +
                    anchor_weight * anchor_residual;
            }
            return EvaluationStatus::success();
        };
    if ((uses_informed_residual &&
         target.continuation_checked_residual_subset) ||
        (!uses_informed_residual &&
         target.checked_residual_subset)) {
        stage.checked_residual_subset =
            [&target, anchor, variable_scales,
             residual_scales, parameter, target_weight, anchor_weight,
             uses_informed_residual](
                const std::vector<double>& x,
                const std::vector<std::size_t>& rows,
                std::vector<double>& residual) {
                std::vector<double> target_residual(
                    rows.size(), 0.0);
                const auto status = uses_informed_residual
                    ? target.continuation_checked_residual_subset(
                          x, anchor, parameter, rows,
                          target_residual)
                    : target.checked_residual_subset(
                          x, rows, target_residual);
                if (!status.ok()) return status;
                for (std::size_t output = 0;
                     output < rows.size(); ++output) {
                    const std::size_t row = rows[output];
                    residual[output] =
                        target_weight * target_residual[output] +
                        anchor_weight *
                            residual_scales[row] *
                            (x[row] - anchor[row]) /
                            variable_scales[row];
                }
                return EvaluationStatus::success();
            };
    }

    if (target.structural_jacobian_pattern.has_value()) {
        stage.structural_jacobian_pattern =
            make_fixed_pattern_mapping(
                *target.structural_jacobian_pattern)
                .pattern;
    }

    if (target.sparse_jacobian_pattern.has_value() &&
        (!uses_informed_residual ||
         target.continuation_sparse_jacobian_values)) {
        const auto mapping = make_fixed_pattern_mapping(
            *target.sparse_jacobian_pattern);
        stage.sparse_jacobian_pattern = mapping.pattern;
        if (!target.structural_jacobian_pattern.has_value()) {
            stage.structural_jacobian_pattern = mapping.pattern;
        }
        stage.sparse_jacobian_values =
            [&target, anchor, mapping, variable_scales,
             residual_scales, parameter, target_weight, anchor_weight,
             uses_informed_residual](
                const std::vector<double>& x,
                std::vector<double>& values) {
                std::vector<double> target_values(
                    target.sparse_jacobian_pattern->nonzeros(),
                    0.0);
                if (uses_informed_residual &&
                    target
                        .continuation_sparse_jacobian_values) {
                    target
                        .continuation_sparse_jacobian_values(
                            x, anchor, parameter,
                            target_values);
                } else {
                    target.sparse_jacobian_values(
                        x, target_values);
                }
                std::fill(values.begin(), values.end(), 0.0);
                for (std::size_t offset = 0;
                     offset < target_values.size(); ++offset) {
                    values[mapping.target_offsets[offset]] +=
                        target_weight * target_values[offset];
                }
                for (std::size_t row = 0;
                     row < mapping.diagonal_offsets.size();
                     ++row) {
                    values[mapping.diagonal_offsets[row]] +=
                        anchor_weight *
                        residual_scales[row] /
                        variable_scales[row];
                }
            };
        if ((uses_informed_residual &&
             target
                 .continuation_sparse_jacobian_values_subset) ||
            (!uses_informed_residual &&
             target.sparse_jacobian_values_subset)) {
            const std::size_t missing =
                std::numeric_limits<std::size_t>::max();
            std::vector<std::size_t> target_of_stage(
                mapping.pattern.nonzeros(), missing);
            for (std::size_t target_offset = 0;
                 target_offset < mapping.target_offsets.size();
                 ++target_offset) {
                target_of_stage[
                    mapping.target_offsets[target_offset]] =
                    target_offset;
            }
            std::vector<std::size_t> diagonal_row_of_stage(
                mapping.pattern.nonzeros(), missing);
            for (std::size_t row = 0;
                 row < mapping.diagonal_offsets.size(); ++row) {
                diagonal_row_of_stage[
                    mapping.diagonal_offsets[row]] = row;
            }
            stage.sparse_jacobian_values_subset =
                [&target, target_of_stage,
                 diagonal_row_of_stage, variable_scales,
                 residual_scales, anchor, parameter,
                 target_weight, anchor_weight,
                 uses_informed_residual, missing](
                    const std::vector<double>& x,
                    const std::vector<std::size_t>& offsets,
                    std::vector<double>& values) {
                    if (values.size() != offsets.size()) {
                        throw std::invalid_argument(
                            "continuation subset value count does not match requested offsets");
                    }
                    std::fill(values.begin(), values.end(), 0.0);
                    std::vector<std::size_t> target_offsets;
                    std::vector<std::size_t> target_outputs;
                    for (std::size_t output = 0;
                         output < offsets.size(); ++output) {
                        const std::size_t offset = offsets[output];
                        if (offset >= target_of_stage.size()) {
                            throw std::out_of_range(
                                "continuation subset offset is out of range");
                        }
                        if (target_of_stage[offset] != missing) {
                            target_offsets.push_back(
                                target_of_stage[offset]);
                            target_outputs.push_back(output);
                        }
                        const std::size_t diagonal_row =
                            diagonal_row_of_stage[offset];
                        if (diagonal_row != missing) {
                            values[output] +=
                                anchor_weight *
                                residual_scales[diagonal_row] /
                                variable_scales[diagonal_row];
                        }
                    }
                    std::vector<double> target_values(
                        target_offsets.size(), 0.0);
                    if (uses_informed_residual) {
                        target
                            .continuation_sparse_jacobian_values_subset(
                                x, anchor, parameter,
                                target_offsets, target_values);
                    } else {
                        target.sparse_jacobian_values_subset(
                            x, target_offsets, target_values);
                    }
                    for (std::size_t index = 0;
                         index < target_values.size(); ++index) {
                        values[target_outputs[index]] +=
                            target_weight * target_values[index];
                    }
                };
        }
        stage.sparse_jacobian = {};
        stage.partial_sparse_jacobian = {};
        stage.jacobian = {};
    } else if (target.sparse_jacobian &&
               (!uses_informed_residual ||
                target.continuation_sparse_jacobian)) {
        stage.sparse_jacobian =
            [&target, anchor, variable_scales, residual_scales,
             parameter, target_weight, anchor_weight,
             uses_informed_residual](
                const std::vector<double>& x,
                std::vector<SparseTriplet>& jacobian) {
                if (uses_informed_residual &&
                    target.continuation_sparse_jacobian) {
                    target.continuation_sparse_jacobian(
                        x, anchor, parameter, jacobian);
                } else {
                    target.sparse_jacobian(x, jacobian);
                }
                for (auto& entry : jacobian) {
                    entry.value *= target_weight;
                }
                for (std::size_t row = 0;
                     row < x.size(); ++row) {
                    jacobian.push_back({
                        row, row,
                        anchor_weight *
                            residual_scales[row] /
                            variable_scales[row]});
                }
            };
        stage.sparse_jacobian_pattern.reset();
        stage.sparse_jacobian_values = {};
        stage.partial_sparse_jacobian = {};
        stage.jacobian = {};
    } else if (
        target.partial_sparse_jacobian &&
        (!uses_informed_residual ||
         target.continuation_partial_sparse_jacobian)) {
        stage.partial_sparse_jacobian =
            [&target, anchor, variable_scales, residual_scales,
             parameter, target_weight, anchor_weight,
             uses_informed_residual](
                const std::vector<double>& x,
                std::vector<SparseTriplet>& jacobian) {
                if (uses_informed_residual &&
                    target
                        .continuation_partial_sparse_jacobian) {
                    target
                        .continuation_partial_sparse_jacobian(
                            x, anchor, parameter, jacobian);
                } else {
                    target.partial_sparse_jacobian(
                        x, jacobian);
                }
                for (auto& entry : jacobian) {
                    entry.value *= target_weight;
                }
                for (std::size_t row = 0;
                     row < x.size(); ++row) {
                    if (target.analytic_jacobian_rows[row]) {
                        jacobian.push_back({
                            row, row,
                            anchor_weight *
                                residual_scales[row] /
                                variable_scales[row]});
                    }
                }
            };
        stage.sparse_jacobian_pattern.reset();
        stage.sparse_jacobian_values = {};
        stage.sparse_jacobian = {};
        stage.jacobian = {};
    } else if (
        target.jacobian &&
        (!uses_informed_residual ||
         target.continuation_jacobian)) {
        stage.jacobian =
            [&target, anchor, variable_scales, residual_scales,
             parameter, target_weight, anchor_weight,
             uses_informed_residual](
                const std::vector<double>& x,
                Matrix& jacobian) {
                if (uses_informed_residual &&
                    target.continuation_jacobian) {
                    target.continuation_jacobian(
                        x, anchor, parameter, jacobian);
                } else {
                    target.jacobian(x, jacobian);
                }
                for (auto& row : jacobian) {
                    for (double& value : row) {
                        value *= target_weight;
                    }
                }
                for (std::size_t row = 0;
                     row < x.size(); ++row) {
                    jacobian[row][row] +=
                        anchor_weight *
                        residual_scales[row] /
                        variable_scales[row];
                }
            };
    } else {
        stage.sparse_jacobian_pattern.reset();
        stage.sparse_jacobian_values = {};
        stage.sparse_jacobian = {};
        stage.partial_sparse_jacobian = {};
        stage.analytic_jacobian_rows.clear();
        stage.jacobian = {};
    }
    return stage;
}

void accumulate_diagnostics(
    SolverDiagnostics& aggregate,
    const SolverDiagnostics& stage) {
    aggregate.iterations += stage.iterations;
    aggregate.function_evaluations +=
        stage.function_evaluations;
    aggregate.jacobian_evaluations +=
        stage.jacobian_evaluations;
    aggregate.linear_solver_evaluations +=
        stage.linear_solver_evaluations;
    aggregate.trust_region_trials +=
        stage.trust_region_trials;
    aggregate.trust_region_rejections +=
        stage.trust_region_rejections;
    aggregate.final_trust_region_radius =
        stage.final_trust_region_radius;
    aggregate.symbolic_factorizations +=
        stage.symbolic_factorizations;
    aggregate.numeric_factorizations +=
        stage.numeric_factorizations;
    if (stage.factorization_quality_observations > 0) {
        const bool is_new_minimum =
            aggregate.factorization_quality_observations == 0 ||
            stage.minimum_reciprocal_pivot_ratio <
                aggregate.minimum_reciprocal_pivot_ratio;
        aggregate.factorization_quality_observations +=
            stage.factorization_quality_observations;
        aggregate.last_reciprocal_pivot_ratio =
            stage.last_reciprocal_pivot_ratio;
        if (is_new_minimum) {
            aggregate.minimum_reciprocal_pivot_ratio =
                stage.minimum_reciprocal_pivot_ratio;
            aggregate.minimum_absolute_pivot_at_minimum_ratio =
                stage.minimum_absolute_pivot_at_minimum_ratio;
            aggregate.maximum_absolute_pivot_at_minimum_ratio =
                stage.maximum_absolute_pivot_at_minimum_ratio;
            aggregate.accepted_pivot_count_at_minimum_ratio =
                stage.accepted_pivot_count_at_minimum_ratio;
            aggregate.factorization_size_at_minimum_ratio =
                stage.factorization_size_at_minimum_ratio;
            aggregate.factorization_quality_method =
                stage.factorization_quality_method;
        }
    }
    aggregate.last_linear_backward_error =
        stage.last_linear_backward_error;
    aggregate.maximum_linear_backward_error = std::max(
        aggregate.maximum_linear_backward_error,
        stage.maximum_linear_backward_error);
    aggregate.linear_refinement_attempts +=
        stage.linear_refinement_attempts;
    aggregate.linear_refinement_successes +=
        stage.linear_refinement_successes;
    aggregate.structural_block_solves +=
        stage.structural_block_solves;
    aggregate.largest_linear_system_size = std::max(
        aggregate.largest_linear_system_size,
        stage.largest_linear_system_size);
    aggregate.structural_tearing_attempts +=
        stage.structural_tearing_attempts;
    aggregate.structural_tearing_successes +=
        stage.structural_tearing_successes;
    aggregate.structural_tearing_fallbacks +=
        stage.structural_tearing_fallbacks;
    aggregate.largest_tearing_inner_system_size = std::max(
        aggregate.largest_tearing_inner_system_size,
        stage.largest_tearing_inner_system_size);
    aggregate.largest_tearing_outer_system_size = std::max(
        aggregate.largest_tearing_outer_system_size,
        stage.largest_tearing_outer_system_size);
    aggregate.largest_tearing_inner_nonzero_count = std::max(
        aggregate.largest_tearing_inner_nonzero_count,
        stage.largest_tearing_inner_nonzero_count);
    if (!stage.last_structural_tearing_fallback.empty()) {
        aggregate.last_structural_tearing_fallback =
            stage.last_structural_tearing_fallback;
    }
    if (!stage.failed_structural_block.empty()) {
        aggregate.failed_structural_block =
            stage.failed_structural_block;
    }
    if (stage.linear_solver_backend != "not-used") {
        aggregate.linear_solver_backend =
            stage.linear_solver_backend;
    }
    aggregate.final_residual_norm =
        stage.final_residual_norm;
    aggregate.final_maximum_absolute_normalized_residual =
        stage.final_maximum_absolute_normalized_residual;
    aggregate.limiting_residual = stage.limiting_residual;
    aggregate.final_step_norm = stage.final_step_norm;
}

}  // namespace

ContinuationSolveResult solve_continuation(
    const NonlinearProblem& problem,
    const SolverOptions& solver_options,
    const ContinuationOptions& continuation_options) {
    validate_continuation_options(continuation_options);
    if (!problem.residual && !problem.checked_residual) {
        throw std::invalid_argument(
            "continuation target residual callback is empty");
    }
    if (problem.sparse_jacobian_pattern.has_value() &&
        !problem.sparse_jacobian_values) {
        throw std::invalid_argument(
            "continuation target sparse pattern requires "
            "value evaluation");
    }
    if (problem.continuation_checked_residual_subset &&
        !problem.continuation_checked_residual) {
        throw std::invalid_argument(
            "continuation residual subset requires full continuation residual evaluation");
    }
    if (problem.continuation_sparse_jacobian_values_subset &&
        (!problem.sparse_jacobian_pattern.has_value() ||
         !problem.continuation_sparse_jacobian_values)) {
        throw std::invalid_argument(
            "continuation sparse value subset requires a fixed pattern and full continuation value evaluation");
    }
    SolverOptions staged_solver = solver_options;
    if ((problem.sparse_jacobian_pattern.has_value() ||
         problem.sparse_jacobian ||
         problem.partial_sparse_jacobian) &&
        !staged_solver.sparse_factorization &&
        !staged_solver.sparse_factorization_resolver &&
        !staged_solver.sparse_linear_solver &&
        !staged_solver.linear_solver) {
        if (staged_solver.structural_decomposition_policy !=
                StructuralDecompositionPolicy::monolithic &&
            problem.sparse_jacobian_pattern.has_value()) {
            staged_solver.sparse_factorization_resolver =
                make_default_sparse_factorization_resolver();
        } else {
            staged_solver.sparse_factorization =
                make_default_sparse_factorization();
        }
    }

    ContinuationSolveResult result;
    result.continuation.used_informed_path =
        static_cast<bool>(
            problem.continuation_checked_residual);
    result.x = problem.initial_guess;
    double reached = 0.0;
    double step = continuation_options.initial_step;
    int attempts = 0;
    const auto try_target_fallback =
        [&](std::string staged_failure) {
            NonlinearProblem target = problem;
            target.initial_guess = result.x;
            NonlinearSolveResult solved;
            try {
                solved = solve_newton(target, staged_solver);
            } catch (const std::exception& error) {
                solved.x = result.x;
                solved.diagnostics.message = error.what();
            }
            const bool converged =
                solved.diagnostics.converged;
            result.continuation.stages.push_back(
                {reached, 1.0, converged,
                 solved.diagnostics});
            accumulate_diagnostics(
                result.diagnostics, solved.diagnostics);
            if (!converged) {
                ++result.continuation.rejected_stages;
                result.diagnostics.converged = false;
                result.diagnostics.message =
                    std::move(staged_failure) +
                    "; direct target fallback failed: " +
                    solved.diagnostics.message;
                result.continuation.reached_parameter =
                    reached;
                result.continuation.message =
                    result.diagnostics.message;
                return false;
            }
            result.x = std::move(solved.x);
            ++result.continuation.accepted_stages;
            result.continuation.reached_parameter = 1.0;
            result.continuation.converged = true;
            result.diagnostics.converged = true;
            result.diagnostics.message =
                std::move(staged_failure) +
                "; direct target fallback converged";
            result.continuation.message =
                result.diagnostics.message;
            return true;
        };
    while (reached < 1.0 &&
           attempts < continuation_options.maximum_stages) {
        ++attempts;
        const double target_parameter =
            std::min(1.0, reached + step);
        auto stage = make_stage_problem(
            problem, problem.initial_guess, result.x,
            target_parameter);
        auto solved = solve_newton(stage, staged_solver);
        ContinuationStageDiagnostic stage_diagnostic{
            reached, target_parameter,
            solved.diagnostics.converged,
            solved.diagnostics};
        accumulate_diagnostics(
            result.diagnostics, solved.diagnostics);
        result.continuation.stages.push_back(
            std::move(stage_diagnostic));

        if (solved.diagnostics.converged) {
            result.x = std::move(solved.x);
            reached = target_parameter;
            ++result.continuation.accepted_stages;
            step = std::min(
                1.0 - reached,
                step * continuation_options.step_growth);
            continue;
        }

        ++result.continuation.rejected_stages;
        step *= continuation_options.step_reduction;
        if (step < continuation_options.minimum_step) {
            (void)try_target_fallback(
                "continuation step fell below minimum: " +
                solved.diagnostics.message);
            return result;
        }
    }

    result.continuation.reached_parameter = reached;
    result.continuation.converged = reached >= 1.0;
    result.diagnostics.converged =
        result.continuation.converged;
    if (result.continuation.converged) {
        result.diagnostics.message =
            "continuation reached the target problem";
        result.continuation.message =
            result.diagnostics.message;
    } else {
        (void)try_target_fallback(
            "continuation maximum stage count reached");
    }
    return result;
}

}  // namespace thermox
