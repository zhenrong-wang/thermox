#include "thermox/continuation_solver.hpp"

#include "thermox/sparse_linear_solver.hpp"

#include <algorithm>
#include <cmath>
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
    NonlinearProblem stage = target;
    stage.initial_guess = warm_start;
    stage.residual = {};
    stage.checked_residual =
        [&target, anchor, variable_scales, residual_scales,
         parameter](
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
                    parameter * target_residual[row] +
                    (1.0 - parameter) * anchor_residual;
            }
            return EvaluationStatus::success();
        };

    if (target.sparse_jacobian_pattern.has_value() &&
        (!uses_informed_residual ||
         target.continuation_sparse_jacobian_values)) {
        const auto mapping = make_fixed_pattern_mapping(
            *target.sparse_jacobian_pattern);
        stage.sparse_jacobian_pattern = mapping.pattern;
        stage.sparse_jacobian_values =
            [&target, anchor, mapping, variable_scales,
             residual_scales, parameter,
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
                        parameter * target_values[offset];
                }
                for (std::size_t row = 0;
                     row < mapping.diagonal_offsets.size();
                     ++row) {
                    values[mapping.diagonal_offsets[row]] +=
                        (1.0 - parameter) *
                        residual_scales[row] /
                        variable_scales[row];
                }
            };
        stage.sparse_jacobian = {};
        stage.partial_sparse_jacobian = {};
        stage.jacobian = {};
    } else if (target.sparse_jacobian &&
               (!uses_informed_residual ||
                target.continuation_sparse_jacobian)) {
        stage.sparse_jacobian =
            [&target, anchor, variable_scales, residual_scales,
             parameter, uses_informed_residual](
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
                    entry.value *= parameter;
                }
                for (std::size_t row = 0;
                     row < x.size(); ++row) {
                    jacobian.push_back({
                        row, row,
                        (1.0 - parameter) *
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
             parameter, uses_informed_residual](
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
                    entry.value *= parameter;
                }
                for (std::size_t row = 0;
                     row < x.size(); ++row) {
                    if (target.analytic_jacobian_rows[row]) {
                        jacobian.push_back({
                            row, row,
                            (1.0 - parameter) *
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
             parameter, uses_informed_residual](
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
                        value *= parameter;
                    }
                }
                for (std::size_t row = 0;
                     row < x.size(); ++row) {
                    jacobian[row][row] +=
                        (1.0 - parameter) *
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
    aggregate.symbolic_factorizations +=
        stage.symbolic_factorizations;
    aggregate.numeric_factorizations +=
        stage.numeric_factorizations;
    if (stage.linear_solver_backend != "not-used") {
        aggregate.linear_solver_backend =
            stage.linear_solver_backend;
    }
    aggregate.final_residual_norm =
        stage.final_residual_norm;
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
    SolverOptions staged_solver = solver_options;
    if ((problem.sparse_jacobian_pattern.has_value() ||
         problem.sparse_jacobian ||
         problem.partial_sparse_jacobian) &&
        !staged_solver.sparse_factorization &&
        !staged_solver.sparse_linear_solver &&
        !staged_solver.linear_solver) {
        staged_solver.sparse_factorization =
            make_default_sparse_factorization();
    }

    ContinuationSolveResult result;
    result.continuation.used_informed_path =
        static_cast<bool>(
            problem.continuation_checked_residual);
    result.x = problem.initial_guess;
    double reached = 0.0;
    double step = continuation_options.initial_step;
    int attempts = 0;
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
            result.diagnostics.converged = false;
            result.diagnostics.message =
                "continuation step fell below minimum: " +
                solved.diagnostics.message;
            result.continuation.reached_parameter = reached;
            result.continuation.message =
                result.diagnostics.message;
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
        result.diagnostics.message =
            "continuation maximum stage count reached";
        result.continuation.message =
            result.diagnostics.message;
    }
    return result;
}

}  // namespace thermox
