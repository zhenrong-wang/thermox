#include "thermox/nonlinear_solver.hpp"

#include "thermox/dense_linear_solver.hpp"
#include "thermox/sparse_linear_solver.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace thermox {

namespace {

struct EvaluatedJacobian {
    Matrix dense;
    SparseMatrix sparse;
    bool is_sparse{false};
};

class EvaluationError final : public std::runtime_error {
public:
    EvaluationError(std::string message, bool recoverable)
        : std::runtime_error(std::move(message)), recoverable_(recoverable) {}

    [[nodiscard]] bool recoverable() const { return recoverable_; }

private:
    bool recoverable_{false};
};

bool is_positive_finite(double value) {
    return std::isfinite(value) && value > 0.0;
}

void validate_options(const SolverOptions& options) {
    switch (options.structural_decomposition_policy) {
    case StructuralDecompositionPolicy::automatic:
    case StructuralDecompositionPolicy::monolithic:
    case StructuralDecompositionPolicy::blocks:
    case StructuralDecompositionPolicy::tearing:
        break;
    default:
        throw std::invalid_argument(
            "structural_decomposition_policy is invalid");
    }
    if (options.max_iterations < 0) {
        throw std::invalid_argument("max_iterations must be non-negative");
    }
    if (!is_positive_finite(options.residual_tolerance)) {
        throw std::invalid_argument("residual_tolerance must be positive and finite");
    }
    if (!is_positive_finite(options.step_tolerance)) {
        throw std::invalid_argument("step_tolerance must be positive and finite");
    }
    if (!is_positive_finite(options.linear_residual_tolerance)) {
        throw std::invalid_argument(
            "linear_residual_tolerance must be positive and finite");
    }
    if (!is_positive_finite(options.finite_difference_epsilon)) {
        throw std::invalid_argument("finite_difference_epsilon must be positive and finite");
    }
    if (!is_positive_finite(options.min_damping) || options.min_damping > 1.0) {
        throw std::invalid_argument("min_damping must be in (0, 1]");
    }
    if (!is_positive_finite(options.damping_reduction) || options.damping_reduction >= 1.0) {
        throw std::invalid_argument("damping_reduction must be in (0, 1)");
    }
    if (!std::isfinite(options.sufficient_decrease) || options.sufficient_decrease < 0.0 ||
        options.sufficient_decrease >= 1.0) {
        throw std::invalid_argument("sufficient_decrease must be in [0, 1)");
    }
    if (options.max_line_search_steps <= 0) {
        throw std::invalid_argument("max_line_search_steps must be positive");
    }
}

std::vector<double> defaulted_variable_scales(const NonlinearProblem& problem) {
    const std::size_t n = problem.initial_guess.size();
    if (!problem.variable_scales.empty() && problem.variable_scales.size() != n) {
        throw std::invalid_argument("variable_scales size must match initial_guess size");
    }

    std::vector<double> scales(n, 1.0);
    if (!problem.variable_scales.empty()) {
        scales = problem.variable_scales;
    }
    for (const double scale : scales) {
        if (!is_positive_finite(scale)) {
            throw std::invalid_argument("all variable scales must be positive and finite");
        }
    }
    return scales;
}

std::vector<double> defaulted_residual_scales(const NonlinearProblem& problem) {
    const std::size_t residual_count = problem.residual_names.size();
    if (!problem.residual_scales.empty() && problem.residual_scales.size() != residual_count) {
        throw std::invalid_argument("residual_scales size must match residual_names size");
    }

    std::vector<double> scales(residual_count, 1.0);
    if (!problem.residual_scales.empty()) {
        scales = problem.residual_scales;
    }
    for (const double scale : scales) {
        if (!is_positive_finite(scale)) {
            throw std::invalid_argument("all residual scales must be positive and finite");
        }
    }
    return scales;
}

std::vector<double> defaulted_lower_bounds(const NonlinearProblem& problem) {
    const std::size_t n = problem.initial_guess.size();
    if (!problem.lower_bounds.empty() && problem.lower_bounds.size() != n) {
        throw std::invalid_argument("lower_bounds size must match initial_guess size");
    }
    if (problem.lower_bounds.empty()) {
        return std::vector<double>(n, -std::numeric_limits<double>::infinity());
    }
    return problem.lower_bounds;
}

std::vector<double> defaulted_upper_bounds(const NonlinearProblem& problem) {
    const std::size_t n = problem.initial_guess.size();
    if (!problem.upper_bounds.empty() && problem.upper_bounds.size() != n) {
        throw std::invalid_argument("upper_bounds size must match initial_guess size");
    }
    if (problem.upper_bounds.empty()) {
        return std::vector<double>(n, std::numeric_limits<double>::infinity());
    }
    return problem.upper_bounds;
}

void validate_problem(const NonlinearProblem& problem,
                      const std::vector<double>& lower_bounds,
                      const std::vector<double>& upper_bounds) {
    if (problem.initial_guess.empty()) {
        throw std::invalid_argument("nonlinear problem must have at least one variable");
    }
    if (problem.variable_names.size() != problem.initial_guess.size()) {
        throw std::invalid_argument("variable_names and initial_guess sizes differ");
    }
    if (problem.residual_names.size() != problem.initial_guess.size()) {
        throw std::invalid_argument("Newton solver requires square systems");
    }
    if (!problem.residual && !problem.checked_residual) {
        throw std::invalid_argument("nonlinear problem residual callback is empty");
    }
    if (!problem.analytic_jacobian_rows.empty() &&
        problem.analytic_jacobian_rows.size() != problem.residual_names.size()) {
        throw std::invalid_argument("analytic_jacobian_rows size must match residual_names size");
    }
    if (problem.sparse_jacobian_pattern.has_value()) {
        const auto& pattern = *problem.sparse_jacobian_pattern;
        if (pattern.rows() != problem.residual_names.size() ||
            pattern.columns() != problem.initial_guess.size()) {
            throw std::invalid_argument("sparse_jacobian_pattern shape does not match problem");
        }
        if (!problem.sparse_jacobian_values) {
            throw std::invalid_argument("sparse_jacobian_pattern requires sparse_jacobian_values");
        }
    } else if (problem.sparse_jacobian_values) {
        throw std::invalid_argument("sparse_jacobian_values requires sparse_jacobian_pattern");
    }
    if (problem.structural_jacobian_pattern.has_value()) {
        const auto& pattern = *problem.structural_jacobian_pattern;
        if (pattern.rows() != problem.residual_names.size() ||
            pattern.columns() != problem.initial_guess.size()) {
            throw std::invalid_argument(
                "structural_jacobian_pattern shape does not match problem");
        }
        if (problem.sparse_jacobian_pattern.has_value()) {
            const auto& numeric = *problem.sparse_jacobian_pattern;
            for (std::size_t row = 0; row < numeric.rows(); ++row) {
                const auto structural_begin =
                    pattern.column_indices().begin() +
                    static_cast<std::ptrdiff_t>(
                        pattern.row_offsets()[row]);
                const auto structural_end =
                    pattern.column_indices().begin() +
                    static_cast<std::ptrdiff_t>(
                        pattern.row_offsets()[row + 1]);
                for (std::size_t offset = numeric.row_offsets()[row];
                     offset < numeric.row_offsets()[row + 1]; ++offset) {
                    if (!std::binary_search(
                            structural_begin, structural_end,
                            numeric.column_indices()[offset])) {
                        throw std::invalid_argument(
                            "numeric Jacobian pattern is not contained in structural incidence");
                    }
                }
            }
        }
    }
    if (problem.sparse_jacobian_values_subset &&
        !problem.sparse_jacobian_pattern.has_value()) {
        throw std::invalid_argument(
            "sparse_jacobian_values_subset requires sparse_jacobian_pattern");
    }
    if (problem.partial_sparse_jacobian && problem.analytic_jacobian_rows.empty()) {
        throw std::invalid_argument(
            "partial_sparse_jacobian requires analytic_jacobian_rows metadata");
    }
    for (std::size_t i = 0; i < problem.initial_guess.size(); ++i) {
        if (!std::isfinite(problem.initial_guess[i])) {
            throw std::invalid_argument("initial guesses must be finite");
        }
        if (std::isnan(lower_bounds[i]) || std::isnan(upper_bounds[i])) {
            throw std::invalid_argument("variable bounds must not be NaN");
        }
        if (lower_bounds[i] > upper_bounds[i]) {
            throw std::invalid_argument("lower bound exceeds upper bound");
        }
        if (problem.initial_guess[i] < lower_bounds[i] || problem.initial_guess[i] > upper_bounds[i]) {
            throw std::invalid_argument("initial guess is outside variable bounds");
        }
    }
}

std::vector<double> evaluate_residual(const NonlinearProblem& problem,
                                      const std::vector<double>& x,
                                      SolverDiagnostics& diagnostics) {
    std::vector<double> residual(problem.residual_names.size(), 0.0);
    ++diagnostics.function_evaluations;
    try {
        if (problem.checked_residual) {
            const EvaluationStatus status = problem.checked_residual(x, residual);
            if (!status.ok()) {
                throw EvaluationError(
                    status.message.empty() ? "residual evaluation failed" : status.message,
                    status.code == EvaluationStatusCode::recoverable_failure);
            }
        } else {
            problem.residual(x, residual);
        }
    } catch (const EvaluationError&) {
        throw;
    } catch (const std::exception& ex) {
        throw EvaluationError(std::string("residual evaluation threw: ") + ex.what(), false);
    }
    if (residual.size() != problem.residual_names.size()) {
        throw std::runtime_error("residual callback changed residual vector size");
    }
    return residual;
}

void validate_jacobian_shape(const Matrix& jacobian,
                             std::size_t rows,
                             std::size_t cols) {
    if (jacobian.size() != rows) {
        throw std::runtime_error("jacobian row count does not match residual count");
    }
    for (const auto& row : jacobian) {
        if (row.size() != cols) {
            throw std::runtime_error("jacobian column count does not match variable count");
        }
        for (const double value : row) {
            if (!std::isfinite(value)) {
                throw std::runtime_error("jacobian contains non-finite values");
            }
        }
    }
}

void validate_jacobian_shape(const SparseMatrix& jacobian,
                             std::size_t rows,
                             std::size_t cols) {
    if (jacobian.rows() != rows) {
        throw std::runtime_error("sparse jacobian row count does not match residual count");
    }
    if (jacobian.columns() != cols) {
        throw std::runtime_error("sparse jacobian column count does not match variable count");
    }
}

Matrix finite_difference_jacobian(const NonlinearProblem& problem,
                                  const std::vector<double>& x,
                                  const std::vector<double>& f,
                                  const std::vector<double>& variable_scales,
                                  const std::vector<double>& lower_bounds,
                                  const std::vector<double>& upper_bounds,
                                  double epsilon,
                                  SolverDiagnostics& diagnostics) {
    const std::size_t n = x.size();
    Matrix jacobian(f.size(), std::vector<double>(n, 0.0));

    for (std::size_t col = 0; col < n; ++col) {
        const double magnitude = epsilon * std::max(variable_scales[col], std::abs(x[col]));
        const double positive_value = x[col] + magnitude;
        const double negative_value = x[col] - magnitude;
        const bool positive_available =
            std::isfinite(positive_value) &&
            positive_value <= upper_bounds[col] &&
            positive_value != x[col];
        const bool negative_available =
            std::isfinite(negative_value) &&
            negative_value >= lower_bounds[col] &&
            negative_value != x[col];
        if (!positive_available && !negative_available) {
            continue;
        }

        const auto evaluate_perturbation =
            [&](double step)
            -> std::optional<std::vector<double>> {
            std::vector<double> xp = x;
            xp[col] += step;
            try {
                return evaluate_residual(problem, xp, diagnostics);
            } catch (const EvaluationError& ex) {
                if (!ex.recoverable()) {
                    throw;
                }
                return std::nullopt;
            }
        };

        std::optional<std::vector<double>> positive;
        std::optional<std::vector<double>> negative;
        if (positive_available) {
            positive = evaluate_perturbation(magnitude);
        }
        if (negative_available) {
            negative = evaluate_perturbation(-magnitude);
        }
        if (!positive && !negative) {
            throw EvaluationError(
                "finite-difference perturbations failed in the valid variable domain", true);
        }

        for (std::size_t row = 0; row < f.size(); ++row) {
            if (positive && negative) {
                jacobian[row][col] =
                    ((*positive)[row] - (*negative)[row]) /
                    (2.0 * magnitude);
            } else if (positive) {
                jacobian[row][col] =
                    ((*positive)[row] - f[row]) / magnitude;
            } else {
                jacobian[row][col] =
                    (f[row] - (*negative)[row]) / magnitude;
            }
        }
    }

    return jacobian;
}

EvaluatedJacobian evaluate_jacobian(const NonlinearProblem& problem,
                                    const std::vector<double>& x,
                                    const std::vector<double>& f,
                                    const std::vector<double>& variable_scales,
                                    const std::vector<double>& lower_bounds,
                                    const std::vector<double>& upper_bounds,
                                    const SolverOptions& options,
    SolverDiagnostics& diagnostics) {
    if (problem.sparse_jacobian_pattern.has_value()) {
        std::vector<double> values(problem.sparse_jacobian_pattern->nonzeros(), 0.0);
        problem.sparse_jacobian_values(x, values);
        ++diagnostics.jacobian_evaluations;
        if (values.size() != problem.sparse_jacobian_pattern->nonzeros()) {
            throw std::runtime_error("sparse_jacobian_values changed values vector size");
        }
        auto sparse = SparseMatrix(*problem.sparse_jacobian_pattern, std::move(values));
        validate_jacobian_shape(sparse, f.size(), x.size());
        return EvaluatedJacobian{{}, std::move(sparse), true};
    }

    if (problem.sparse_jacobian) {
        std::vector<SparseTriplet> triplets;
        problem.sparse_jacobian(x, triplets);
        ++diagnostics.jacobian_evaluations;
        auto sparse = sparse_from_triplets(f.size(), x.size(), std::move(triplets));
        validate_jacobian_shape(sparse, f.size(), x.size());
        return EvaluatedJacobian{{}, std::move(sparse), true};
    }

    if (problem.partial_sparse_jacobian) {
        std::vector<SparseTriplet> triplets;
        problem.partial_sparse_jacobian(x, triplets);
        Matrix numerical = finite_difference_jacobian(problem, x, f, variable_scales,
                                                      lower_bounds, upper_bounds,
                                                      options.finite_difference_epsilon,
                                                      diagnostics);
        for (std::size_t row = 0; row < f.size(); ++row) {
            if (problem.analytic_jacobian_rows[row]) {
                continue;
            }
            for (std::size_t col = 0; col < x.size(); ++col) {
                if (numerical[row][col] != 0.0) {
                    triplets.push_back(SparseTriplet{row, col, numerical[row][col]});
                }
            }
        }
        ++diagnostics.jacobian_evaluations;
        auto sparse = sparse_from_triplets(f.size(), x.size(), std::move(triplets));
        validate_jacobian_shape(sparse, f.size(), x.size());
        return EvaluatedJacobian{{}, std::move(sparse), true};
    }

    Matrix jacobian;
    if (problem.jacobian) {
        jacobian.assign(problem.residual_names.size(), std::vector<double>(x.size(), 0.0));
        problem.jacobian(x, jacobian);
        ++diagnostics.jacobian_evaluations;
    } else {
        jacobian = finite_difference_jacobian(problem, x, f, variable_scales, lower_bounds,
                                              upper_bounds, options.finite_difference_epsilon,
                                              diagnostics);
        ++diagnostics.jacobian_evaluations;
    }
    validate_jacobian_shape(jacobian, f.size(), x.size());
    return EvaluatedJacobian{std::move(jacobian), {}, false};
}

std::vector<double> add_scaled_step(const std::vector<double>& x,
                                    const std::vector<double>& step,
                                    const std::vector<double>& lower_bounds,
                                    const std::vector<double>& upper_bounds,
                                    double scale) {
    std::vector<double> candidate = x;
    for (std::size_t i = 0; i < candidate.size(); ++i) {
        candidate[i] = std::clamp(candidate[i] + scale * step[i], lower_bounds[i], upper_bounds[i]);
    }
    return candidate;
}

double scaled_step_norm(const std::vector<double>& step, const std::vector<double>& scales) {
    long double sum = 0.0;
    for (std::size_t i = 0; i < step.size(); ++i) {
        const long double scaled = static_cast<long double>(step[i]) / static_cast<long double>(scales[i]);
        sum += scaled * scaled;
    }
    return std::sqrt(static_cast<double>(sum));
}

double scaled_residual_norm(const std::vector<double>& residual,
                            const std::vector<double>& residual_scales) {
    long double sum = 0.0;
    for (std::size_t i = 0; i < residual.size(); ++i) {
        const long double scaled = static_cast<long double>(residual[i]) /
                                   static_cast<long double>(residual_scales[i]);
        sum += scaled * scaled;
    }
    return std::sqrt(static_cast<double>(sum));
}

void set_final_residual_diagnostics(
    SolverDiagnostics& diagnostics,
    const std::vector<double>& residual,
    const std::vector<double>& residual_scales,
    const std::vector<std::string>& residual_names,
    double residual_norm) {
    diagnostics.final_residual_norm = residual_norm;
    diagnostics.final_maximum_absolute_normalized_residual = 0.0;
    diagnostics.limiting_residual.clear();
    for (std::size_t row = 0; row < residual.size(); ++row) {
        const double ratio =
            std::abs(residual[row] / residual_scales[row]);
        if (diagnostics.limiting_residual.empty() ||
            !std::isfinite(ratio) ||
            ratio > diagnostics.final_maximum_absolute_normalized_residual) {
            diagnostics.final_maximum_absolute_normalized_residual =
                ratio;
            diagnostics.limiting_residual = residual_names[row];
        }
    }
}

std::vector<double> scale_residual(const std::vector<double>& residual,
                                   const std::vector<double>& residual_scales) {
    std::vector<double> scaled = residual;
    for (std::size_t i = 0; i < scaled.size(); ++i) {
        scaled[i] /= residual_scales[i];
    }
    return scaled;
}

void scale_jacobian_rows(Matrix& jacobian, const std::vector<double>& residual_scales) {
    for (std::size_t row = 0; row < jacobian.size(); ++row) {
        for (double& value : jacobian[row]) {
            value /= residual_scales[row];
        }
    }
}

void scale_jacobian_rows(EvaluatedJacobian& jacobian, const std::vector<double>& residual_scales) {
    if (jacobian.is_sparse) {
        jacobian.sparse.scale_rows(residual_scales);
    } else {
        scale_jacobian_rows(jacobian.dense, residual_scales);
    }
}

void scale_jacobian_columns(EvaluatedJacobian& jacobian,
                            const std::vector<double>& variable_scales) {
    if (jacobian.is_sparse) {
        jacobian.sparse.scale_columns(variable_scales);
        return;
    }
    for (auto& row : jacobian.dense) {
        for (std::size_t column = 0; column < row.size(); ++column) {
            row[column] *= variable_scales[column];
        }
    }
}

LinearSolveResult validate_linear_result(LinearSolveResult result, std::size_t expected_size) {
    if (!result.success) {
        return result;
    }
    if (result.x.size() != expected_size) {
        return {false, {}, "linear solver returned step with wrong size"};
    }
    for (const double value : result.x) {
        if (!std::isfinite(value)) {
            return {false, {}, "linear solver returned non-finite step"};
        }
    }
    return result;
}

double normalized_backward_error(
    const Matrix& matrix,
    const std::vector<double>& x,
    const std::vector<double>& rhs) {
    long double matrix_norm = 0.0;
    long double solution_norm = 0.0;
    long double rhs_norm = 0.0;
    long double residual_norm = 0.0;
    for (const double value : x) {
        solution_norm = std::max(
            solution_norm,
            std::abs(static_cast<long double>(value)));
    }
    for (std::size_t row = 0; row < matrix.size(); ++row) {
        long double row_norm = 0.0;
        long double product = 0.0;
        for (std::size_t column = 0;
             column < matrix[row].size(); ++column) {
            row_norm += std::abs(
                static_cast<long double>(matrix[row][column]));
            product +=
                static_cast<long double>(matrix[row][column]) *
                static_cast<long double>(x[column]);
        }
        matrix_norm = std::max(matrix_norm, row_norm);
        rhs_norm = std::max(
            rhs_norm,
            std::abs(static_cast<long double>(rhs[row])));
        residual_norm = std::max(
            residual_norm,
            std::abs(product -
                     static_cast<long double>(rhs[row])));
    }
    const long double denominator =
        matrix_norm * solution_norm + rhs_norm;
    if (denominator == 0.0L) {
        return residual_norm == 0.0L
            ? 0.0
            : std::numeric_limits<double>::infinity();
    }
    return static_cast<double>(residual_norm / denominator);
}

double normalized_backward_error(
    const SparseMatrix& matrix,
    const std::vector<double>& x,
    const std::vector<double>& rhs) {
    long double matrix_norm = 0.0;
    long double solution_norm = 0.0;
    long double rhs_norm = 0.0;
    long double residual_norm = 0.0;
    for (const double value : x) {
        solution_norm = std::max(
            solution_norm,
            std::abs(static_cast<long double>(value)));
    }
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
        long double row_norm = 0.0;
        long double product = 0.0;
        for (std::size_t offset = matrix.row_offsets()[row];
             offset < matrix.row_offsets()[row + 1]; ++offset) {
            const long double value = matrix.values()[offset];
            row_norm += std::abs(value);
            product += value * static_cast<long double>(
                x[matrix.column_indices()[offset]]);
        }
        matrix_norm = std::max(matrix_norm, row_norm);
        rhs_norm = std::max(
            rhs_norm,
            std::abs(static_cast<long double>(rhs[row])));
        residual_norm = std::max(
            residual_norm,
            std::abs(product -
                     static_cast<long double>(rhs[row])));
    }
    const long double denominator =
        matrix_norm * solution_norm + rhs_norm;
    if (denominator == 0.0L) {
        return residual_norm == 0.0L
            ? 0.0
            : std::numeric_limits<double>::infinity();
    }
    return static_cast<double>(residual_norm / denominator);
}

LinearSolveResult solve_linear_system(const SolverOptions& options,
                                      const SparseFactorizationPtr& factorization,
                                      EvaluatedJacobian jacobian,
                                      std::vector<double> rhs,
                                      std::size_t expected_size,
                                      SolverDiagnostics& diagnostics) {
    ++diagnostics.linear_solver_evaluations;
    diagnostics.largest_linear_system_size = std::max(
        diagnostics.largest_linear_system_size,
        expected_size);

    LinearSolveResult result;
    double backward_error = std::numeric_limits<double>::infinity();
    if (options.sparse_linear_solver) {
        diagnostics.linear_solver_backend =
            "custom-sparse-hook";
        auto sparse = jacobian.is_sparse ? std::move(jacobian.sparse)
                                         : sparse_from_dense(jacobian.dense);
        result = options.sparse_linear_solver(sparse, rhs);
        result = validate_linear_result(
            std::move(result), expected_size);
        if (result.success) {
            backward_error = normalized_backward_error(
                sparse, result.x, rhs);
        }
    } else if (jacobian.is_sparse && !options.linear_solver) {
        diagnostics.linear_solver_backend =
            std::string(factorization->backend_name());
        result = factorization->solve(jacobian.sparse, rhs);
        result = validate_linear_result(
            std::move(result), expected_size);
        if (result.success) {
            backward_error = normalized_backward_error(
                jacobian.sparse, result.x, rhs);
        }
    } else if (options.sparse_factorization &&
               !options.linear_solver) {
        diagnostics.linear_solver_backend =
            std::string(factorization->backend_name());
        auto sparse = jacobian.is_sparse
            ? std::move(jacobian.sparse)
            : sparse_from_dense(jacobian.dense);
        result = factorization->solve(sparse, rhs);
        result = validate_linear_result(
            std::move(result), expected_size);
        if (result.success) {
            backward_error = normalized_backward_error(
                sparse, result.x, rhs);
        }
    } else {
        diagnostics.linear_solver_backend =
            options.linear_solver
            ? "custom-dense-hook"
            : "reference-dense";
        auto dense = jacobian.is_sparse ? jacobian.sparse.to_dense() : std::move(jacobian.dense);
        result = options.linear_solver
            ? options.linear_solver(dense, rhs)
            : solve_dense_linear_system(dense, rhs);
        result = validate_linear_result(
            std::move(result), expected_size);
        if (result.success) {
            backward_error = normalized_backward_error(
                dense, result.x, rhs);
        }
    }
    diagnostics.symbolic_factorizations +=
        result.symbolic_factorizations;
    diagnostics.numeric_factorizations +=
        result.numeric_factorizations;
    if (!result.success) return result;
    diagnostics.last_linear_backward_error = backward_error;
    diagnostics.maximum_linear_backward_error = std::max(
        diagnostics.maximum_linear_backward_error,
        backward_error);
    if (!std::isfinite(backward_error) ||
        backward_error > options.linear_residual_tolerance) {
        std::ostringstream message;
        message << std::scientific << std::setprecision(6)
                << "linear backend normalized backward error "
                << backward_error << " exceeds tolerance "
                << options.linear_residual_tolerance;
        result.success = false;
        result.message = message.str();
        result.x.clear();
    }
    return result;
}

LinearSolveResult solve_linear_system_by_tearing(
    const SolverOptions& options,
    EvaluatedJacobian jacobian,
    const std::vector<double>& rhs,
    const std::vector<std::size_t>& tear_rows,
    const std::vector<std::size_t>& tear_columns,
    SolverDiagnostics& diagnostics) {
    const std::size_t size = rhs.size();
    if (tear_rows.size() != tear_columns.size() ||
        tear_rows.empty() || tear_rows.size() >= size) {
        return {false, {}, "invalid structural tearing partition"};
    }
    if (options.sparse_linear_solver && !options.linear_solver) {
        return {
            false, {},
            "structural tearing requires the reference or custom dense linear backend"};
    }

    std::vector<bool> is_tear_row(size, false);
    std::vector<bool> is_tear_column(size, false);
    for (const auto row : tear_rows) {
        if (row >= size || is_tear_row[row]) {
            return {false, {}, "duplicate or out-of-range tear residual"};
        }
        is_tear_row[row] = true;
    }
    for (const auto column : tear_columns) {
        if (column >= size || is_tear_column[column]) {
            return {false, {}, "duplicate or out-of-range tear variable"};
        }
        is_tear_column[column] = true;
    }
    std::vector<std::size_t> inner_rows;
    std::vector<std::size_t> inner_columns;
    for (std::size_t index = 0; index < size; ++index) {
        if (!is_tear_row[index]) inner_rows.push_back(index);
        if (!is_tear_column[index]) inner_columns.push_back(index);
    }
    if (inner_rows.size() != inner_columns.size()) {
        return {false, {}, "structural tearing inner partition is not square"};
    }

    const Matrix dense = jacobian.is_sparse
        ? jacobian.sparse.to_dense()
        : std::move(jacobian.dense);
    const auto solve_dense_partition =
        [&](Matrix matrix, std::vector<double> partition_rhs,
            std::string_view label) {
            ++diagnostics.linear_solver_evaluations;
            diagnostics.largest_linear_system_size = std::max(
                diagnostics.largest_linear_system_size,
                matrix.size());
            auto solved = options.linear_solver
                ? options.linear_solver(
                    std::move(matrix), std::move(partition_rhs))
                : [&]() {
                    DenseLinearFactorization factorization;
                    ++diagnostics.numeric_factorizations;
                    if (!factorization.factorize(
                            std::move(matrix))) {
                        return LinearSolveResult{
                            false, {}, factorization.message()};
                    }
                    return factorization.solve(
                        std::move(partition_rhs));
                }();
            solved = validate_linear_result(
                std::move(solved),
                label == "inner" ? inner_rows.size()
                                   : tear_rows.size());
            diagnostics.symbolic_factorizations +=
                solved.symbolic_factorizations;
            diagnostics.numeric_factorizations +=
                solved.numeric_factorizations;
            if (!solved.success) {
                solved.message = std::string(label) +
                    " tearing solve failed: " + solved.message;
            }
            return solved;
        };

    Matrix inner(
        inner_rows.size(),
        std::vector<double>(inner_columns.size(), 0.0));
    Matrix inner_to_tear(
        inner_rows.size(),
        std::vector<double>(tear_columns.size(), 0.0));
    Matrix tear_to_inner(
        tear_rows.size(),
        std::vector<double>(inner_columns.size(), 0.0));
    Matrix schur(
        tear_rows.size(),
        std::vector<double>(tear_columns.size(), 0.0));
    std::vector<double> inner_rhs(inner_rows.size(), 0.0);
    std::vector<double> tear_rhs(tear_rows.size(), 0.0);
    for (std::size_t row = 0; row < inner_rows.size(); ++row) {
        inner_rhs[row] = rhs[inner_rows[row]];
        for (std::size_t column = 0;
             column < inner_columns.size(); ++column) {
            inner[row][column] =
                dense[inner_rows[row]][inner_columns[column]];
        }
        for (std::size_t column = 0;
             column < tear_columns.size(); ++column) {
            inner_to_tear[row][column] =
                dense[inner_rows[row]][tear_columns[column]];
        }
    }
    for (std::size_t row = 0; row < tear_rows.size(); ++row) {
        tear_rhs[row] = rhs[tear_rows[row]];
        for (std::size_t column = 0;
             column < inner_columns.size(); ++column) {
            tear_to_inner[row][column] =
                dense[tear_rows[row]][inner_columns[column]];
        }
        for (std::size_t column = 0;
             column < tear_columns.size(); ++column) {
            schur[row][column] =
                dense[tear_rows[row]][tear_columns[column]];
        }
    }

    LinearSolveResult inner_solution;
    Matrix eliminated_columns(
        inner_columns.size(),
        std::vector<double>(tear_columns.size(), 0.0));
    if (!options.linear_solver) {
        DenseLinearFactorization inner_factorization;
        ++diagnostics.numeric_factorizations;
        if (!inner_factorization.factorize(inner)) {
            return {
                false, {}, "inner tearing factorization failed: " +
                    inner_factorization.message()};
        }
        Matrix right_hand_sides;
        right_hand_sides.reserve(1U + tear_columns.size());
        right_hand_sides.push_back(inner_rhs);
        for (std::size_t tear = 0;
             tear < tear_columns.size(); ++tear) {
            std::vector<double> column(inner_rows.size(), 0.0);
            for (std::size_t row = 0;
                 row < inner_rows.size(); ++row) {
                column[row] = inner_to_tear[row][tear];
            }
            right_hand_sides.push_back(std::move(column));
        }
        ++diagnostics.linear_solver_evaluations;
        diagnostics.largest_linear_system_size = std::max(
            diagnostics.largest_linear_system_size,
            inner.size());
        auto solutions = inner_factorization.solve_multiple(
            right_hand_sides);
        for (auto& solution : solutions) {
            solution = validate_linear_result(
                std::move(solution), inner_rows.size());
            if (!solution.success) {
                solution.message = "inner tearing solve failed: " +
                    solution.message;
                return solution;
            }
        }
        inner_solution = std::move(solutions.front());
        for (std::size_t tear = 0;
             tear < tear_columns.size(); ++tear) {
            for (std::size_t row = 0;
                 row < inner_columns.size(); ++row) {
                eliminated_columns[row][tear] =
                    solutions[tear + 1U].x[row];
            }
        }
    } else {
        inner_solution = solve_dense_partition(
            inner, inner_rhs, "inner");
        if (!inner_solution.success) return inner_solution;
        for (std::size_t tear = 0;
             tear < tear_columns.size(); ++tear) {
            std::vector<double> column(inner_rows.size(), 0.0);
            for (std::size_t row = 0;
                 row < inner_rows.size(); ++row) {
                column[row] = inner_to_tear[row][tear];
            }
            auto eliminated = solve_dense_partition(
                inner, std::move(column), "inner");
            if (!eliminated.success) return eliminated;
            for (std::size_t row = 0;
                 row < inner_columns.size(); ++row) {
                eliminated_columns[row][tear] =
                    eliminated.x[row];
            }
        }
    }
    for (std::size_t row = 0; row < tear_rows.size(); ++row) {
        for (std::size_t inner_column = 0;
             inner_column < inner_columns.size(); ++inner_column) {
            tear_rhs[row] -= tear_to_inner[row][inner_column] *
                inner_solution.x[inner_column];
            for (std::size_t tear = 0;
                 tear < tear_columns.size(); ++tear) {
                schur[row][tear] -=
                    tear_to_inner[row][inner_column] *
                    eliminated_columns[inner_column][tear];
            }
        }
    }
    auto tear_solution = solve_dense_partition(
        std::move(schur), std::move(tear_rhs), "outer");
    if (!tear_solution.success) return tear_solution;

    LinearSolveResult result;
    result.success = true;
    result.x.assign(size, 0.0);
    for (std::size_t tear = 0; tear < tear_columns.size(); ++tear) {
        result.x[tear_columns[tear]] = tear_solution.x[tear];
    }
    for (std::size_t row = 0; row < inner_columns.size(); ++row) {
        double value = inner_solution.x[row];
        for (std::size_t tear = 0; tear < tear_columns.size(); ++tear) {
            value -= eliminated_columns[row][tear] *
                tear_solution.x[tear];
        }
        result.x[inner_columns[row]] = value;
    }
    const double backward_error = normalized_backward_error(
        dense, result.x, rhs);
    diagnostics.linear_solver_backend = options.linear_solver
        ? "structural-schur/custom-dense-hook"
        : "structural-schur/reference-dense";
    diagnostics.last_linear_backward_error = backward_error;
    diagnostics.maximum_linear_backward_error = std::max(
        diagnostics.maximum_linear_backward_error, backward_error);
    if (!std::isfinite(backward_error) ||
        backward_error > options.linear_residual_tolerance) {
        std::ostringstream message;
        message << std::scientific << std::setprecision(6)
                << "reconstructed tearing step normalized backward error "
                << backward_error << " exceeds tolerance "
                << options.linear_residual_tolerance;
        return {false, {}, message.str()};
    }
    return result;
}

}  // namespace

EvaluationStatus EvaluationStatus::recoverable(std::string message) {
    return {EvaluationStatusCode::recoverable_failure, std::move(message)};
}

EvaluationStatus EvaluationStatus::fatal(std::string message) {
    return {EvaluationStatusCode::fatal_failure, std::move(message)};
}

namespace {

std::vector<std::size_t> suggest_feedback_rows(
    const std::vector<std::vector<std::size_t>>& dependencies,
    const std::vector<std::size_t>& members,
    const std::vector<int>& row_match) {
    std::vector<bool> active(dependencies.size(), false);
    for (const std::size_t member : members) active[member] = true;

    const auto find_cyclic_rows = [&]() {
        std::vector<int> discovery(dependencies.size(), -1);
        std::vector<int> low_link(dependencies.size(), -1);
        std::vector<bool> on_stack(dependencies.size(), false);
        std::vector<std::size_t> stack;
        std::vector<std::size_t> cyclic;
        int next_discovery = 0;
        const auto visit = [&](auto&& self, std::size_t node) -> void {
            discovery[node] = next_discovery;
            low_link[node] = next_discovery;
            ++next_discovery;
            stack.push_back(node);
            on_stack[node] = true;
            for (const std::size_t adjacent : dependencies[node]) {
                if (!active[adjacent]) continue;
                if (discovery[adjacent] < 0) {
                    self(self, adjacent);
                    low_link[node] = std::min(
                        low_link[node], low_link[adjacent]);
                } else if (on_stack[adjacent]) {
                    low_link[node] = std::min(
                        low_link[node], discovery[adjacent]);
                }
            }
            if (low_link[node] != discovery[node]) return;
            std::vector<std::size_t> component;
            while (!stack.empty()) {
                const auto member = stack.back();
                stack.pop_back();
                on_stack[member] = false;
                component.push_back(member);
                if (member == node) break;
            }
            if (component.size() > 1U) {
                cyclic.insert(
                    cyclic.end(), component.begin(), component.end());
            }
        };
        for (const std::size_t member : members) {
            if (active[member] && discovery[member] < 0)
                visit(visit, member);
        }
        std::sort(cyclic.begin(), cyclic.end());
        return cyclic;
    };

    std::vector<std::size_t> tears;
    while (true) {
        const auto cyclic = find_cyclic_rows();
        if (cyclic.empty()) break;
        std::size_t selected = cyclic.front();
        std::size_t selected_score = 0U;
        for (const std::size_t candidate : cyclic) {
            std::size_t incoming = 0U;
            std::size_t outgoing = 0U;
            for (const std::size_t member : members) {
                if (!active[member]) continue;
                incoming += static_cast<std::size_t>(std::binary_search(
                    dependencies[member].begin(),
                    dependencies[member].end(), candidate));
            }
            for (const std::size_t adjacent : dependencies[candidate]) {
                outgoing += static_cast<std::size_t>(active[adjacent]);
            }
            const std::size_t score = incoming * outgoing;
            const auto variable = static_cast<std::size_t>(
                row_match[candidate]);
            const auto selected_variable = static_cast<std::size_t>(
                row_match[selected]);
            if (score > selected_score ||
                (score == selected_score &&
                 variable < selected_variable)) {
                selected = candidate;
                selected_score = score;
            }
        }
        active[selected] = false;
        tears.push_back(selected);
    }
    return tears;
}

}  // namespace

bool ProblemStructureReport::valid_for_newton() const {
    return square && !has_duplicate_variable_names && !has_duplicate_residual_names &&
           (!has_complete_sparse_pattern || structurally_nonsingular);
}

ProblemStructureReport analyze_incidence_structure(
    const std::vector<std::string>& variable_names,
    const std::vector<std::string>& residual_names,
    const std::vector<std::vector<std::size_t>>&
        residual_variable_incidence) {
    ProblemStructureReport report;
    report.variable_count = variable_names.size();
    report.residual_count = residual_names.size();
    report.square = report.variable_count == report.residual_count;
    if (!report.square) {
        report.messages.push_back("Newton solve requires equal variable and residual counts");
    }

    auto find_duplicates = [](const std::vector<std::string>& names) {
        std::set<std::string> seen;
        std::set<std::string> duplicates;
        for (const auto& name : names) {
            if (!seen.insert(name).second) {
                duplicates.insert(name);
            }
        }
        return std::vector<std::string>(duplicates.begin(), duplicates.end());
    };
    report.duplicate_variable_names = find_duplicates(variable_names);
    report.duplicate_residual_names = find_duplicates(residual_names);
    report.has_duplicate_variable_names = !report.duplicate_variable_names.empty();
    report.has_duplicate_residual_names = !report.duplicate_residual_names.empty();
    if (report.has_duplicate_variable_names) {
        report.messages.push_back("variable names must be unique");
    }
    if (report.has_duplicate_residual_names) {
        report.messages.push_back("residual names must be unique");
    }

    if (residual_variable_incidence.size() != report.residual_count) {
        throw std::invalid_argument(
            "incidence row count does not match residual name count");
    }
    report.has_complete_sparse_pattern = true;
    std::vector<std::vector<std::size_t>> variable_residual_incidence(
        report.variable_count);
    for (std::size_t row = 0; row < report.residual_count; ++row) {
        std::set<std::size_t> unique_columns;
        for (const std::size_t column :
             residual_variable_incidence.at(row)) {
            if (column >= report.variable_count) {
                throw std::invalid_argument(
                    "incidence variable index is out of range");
            }
            if (unique_columns.insert(column).second) {
                variable_residual_incidence.at(column).push_back(row);
            }
        }
    }

    std::vector<int> column_match(report.variable_count, -1);
    auto augment = [&](auto&& self, std::size_t row, std::vector<bool>& visited) -> bool {
        for (const std::size_t column :
             residual_variable_incidence.at(row)) {
            if (visited[column]) {
                continue;
            }
            visited[column] = true;
            if (column_match[column] < 0 ||
                self(self, static_cast<std::size_t>(column_match[column]), visited)) {
                column_match[column] = static_cast<int>(row);
                return true;
            }
        }
        return false;
    };

    std::vector<bool> row_matched(report.residual_count, false);
    for (std::size_t row = 0; row < report.residual_count; ++row) {
        std::vector<bool> visited(report.variable_count, false);
        (void)augment(augment, row, visited);
    }
    for (const int row : column_match) {
        if (row >= 0) {
            row_matched[static_cast<std::size_t>(row)] = true;
        }
    }
    for (std::size_t row = 0; row < row_matched.size(); ++row) {
        if (!row_matched[row]) {
            report.unmatched_residual_names.push_back(
                row < residual_names.size()
                    ? residual_names[row]
                    : "residual[" + std::to_string(row) + "]");
        }
    }
    for (std::size_t column = 0; column < column_match.size(); ++column) {
        if (column_match[column] < 0) {
            report.unmatched_variable_names.push_back(
                column < variable_names.size()
                    ? variable_names[column]
                    : "variable[" + std::to_string(column) + "]");
        }
    }

    std::vector<int> row_match(report.residual_count, -1);
    for (std::size_t column = 0; column < column_match.size(); ++column) {
        if (column_match[column] >= 0) {
            row_match.at(
                static_cast<std::size_t>(column_match[column])) =
                static_cast<int>(column);
        }
    }

    std::vector<bool> under_variables(report.variable_count, false);
    std::vector<bool> under_residuals(report.residual_count, false);
    std::vector<std::pair<bool, std::size_t>> queue;
    for (std::size_t column = 0; column < column_match.size(); ++column) {
        if (column_match[column] < 0) {
            under_variables[column] = true;
            queue.emplace_back(true, column);
        }
    }
    for (std::size_t cursor = 0; cursor < queue.size(); ++cursor) {
        const auto [is_variable, index] = queue[cursor];
        if (is_variable) {
            for (const std::size_t row :
                 variable_residual_incidence.at(index)) {
                if (row_match[row] == static_cast<int>(index) ||
                    under_residuals[row]) {
                    continue;
                }
                under_residuals[row] = true;
                queue.emplace_back(false, row);
            }
        } else if (row_match[index] >= 0) {
            const auto column =
                static_cast<std::size_t>(row_match[index]);
            if (!under_variables[column]) {
                under_variables[column] = true;
                queue.emplace_back(true, column);
            }
        }
    }

    std::vector<bool> over_variables(report.variable_count, false);
    std::vector<bool> over_residuals(report.residual_count, false);
    queue.clear();
    for (std::size_t row = 0; row < row_match.size(); ++row) {
        if (row_match[row] < 0) {
            over_residuals[row] = true;
            queue.emplace_back(false, row);
        }
    }
    for (std::size_t cursor = 0; cursor < queue.size(); ++cursor) {
        const auto [is_variable, index] = queue[cursor];
        if (!is_variable) {
            for (const std::size_t column :
                 residual_variable_incidence.at(index)) {
                if (row_match[index] == static_cast<int>(column) ||
                    over_variables[column]) {
                    continue;
                }
                over_variables[column] = true;
                queue.emplace_back(true, column);
            }
        } else if (column_match[index] >= 0) {
            const auto row =
                static_cast<std::size_t>(column_match[index]);
            if (!over_residuals[row]) {
                over_residuals[row] = true;
                queue.emplace_back(false, row);
            }
        }
    }

    const auto append_regions =
        [&](StructuralRegionKind kind,
            const std::vector<bool>& selected_variables,
            const std::vector<bool>& selected_residuals) {
            std::vector<bool> seen_variables(report.variable_count, false);
            std::vector<bool> seen_residuals(report.residual_count, false);
            const auto append_component =
                [&](bool seed_is_variable, std::size_t seed_index) {
                    StructuralRegion region;
                    region.kind = kind;
                    std::vector<std::pair<bool, std::size_t>> pending{
                        {seed_is_variable, seed_index}};
                    if (seed_is_variable) {
                        seen_variables[seed_index] = true;
                    } else {
                        seen_residuals[seed_index] = true;
                    }
                    for (std::size_t cursor = 0;
                         cursor < pending.size(); ++cursor) {
                        const auto [is_variable, index] =
                            pending[cursor];
                        if (is_variable) {
                            region.variable_names.push_back(
                                variable_names.at(index));
                            for (const std::size_t row :
                                 variable_residual_incidence.at(index)) {
                                if (selected_residuals[row] &&
                                    !seen_residuals[row]) {
                                    seen_residuals[row] = true;
                                    pending.emplace_back(false, row);
                                }
                            }
                        } else {
                            region.residual_names.push_back(
                                residual_names.at(index));
                            for (const std::size_t column :
                                 residual_variable_incidence.at(index)) {
                                if (selected_variables[column] &&
                                    !seen_variables[column]) {
                                    seen_variables[column] = true;
                                    pending.emplace_back(true, column);
                                }
                            }
                        }
                    }
                    report.structural_regions.push_back(
                        std::move(region));
                };
            for (std::size_t row = 0;
                 row < report.residual_count; ++row) {
                if (selected_residuals[row] &&
                    !seen_residuals[row]) {
                    append_component(false, row);
                }
            }
            for (std::size_t column = 0;
                 column < report.variable_count; ++column) {
                if (selected_variables[column] &&
                    !seen_variables[column]) {
                    append_component(true, column);
                }
            }
        };
    append_regions(
        StructuralRegionKind::underdetermined,
        under_variables, under_residuals);
    append_regions(
        StructuralRegionKind::overdetermined,
        over_variables, over_residuals);
    std::vector<bool> well_variables(report.variable_count, false);
    std::vector<bool> well_residuals(report.residual_count, false);
    for (std::size_t column = 0;
         column < report.variable_count; ++column) {
        well_variables[column] =
            !under_variables[column] && !over_variables[column];
    }
    for (std::size_t row = 0;
         row < report.residual_count; ++row) {
        well_residuals[row] =
            !under_residuals[row] && !over_residuals[row];
    }
    append_regions(
        StructuralRegionKind::well_determined,
        well_variables, well_residuals);

    report.structurally_nonsingular =
        report.square && report.unmatched_residual_names.empty() &&
        report.unmatched_variable_names.empty();
    if (!report.structurally_nonsingular) {
        report.messages.push_back("fixed Jacobian pattern is structurally singular");
        return report;
    }

    // Collapse the matched equation-variable dependency graph into strongly
    // connected components. Each component is an irreducible square block;
    // topologically ordering the condensation graph gives block-triangular
    // solve order with upstream dependencies first.
    std::vector<std::vector<std::size_t>> dependencies(
        report.residual_count);
    for (std::size_t consumer = 0;
         consumer < report.residual_count; ++consumer) {
        for (const std::size_t column :
             residual_variable_incidence.at(consumer)) {
            const auto producer = static_cast<std::size_t>(
                column_match.at(column));
            if (producer != consumer) {
                dependencies.at(producer).push_back(consumer);
            }
        }
    }
    for (auto& adjacent : dependencies) {
        std::sort(adjacent.begin(), adjacent.end());
        adjacent.erase(
            std::unique(adjacent.begin(), adjacent.end()),
            adjacent.end());
    }

    std::vector<int> discovery(report.residual_count, -1);
    std::vector<int> low_link(report.residual_count, -1);
    std::vector<bool> on_stack(report.residual_count, false);
    std::vector<std::size_t> stack;
    std::vector<std::vector<std::size_t>> components;
    int next_discovery = 0;
    const auto visit = [&](auto&& self, std::size_t row) -> void {
        discovery[row] = next_discovery;
        low_link[row] = next_discovery;
        ++next_discovery;
        stack.push_back(row);
        on_stack[row] = true;
        for (const std::size_t adjacent : dependencies[row]) {
            if (discovery[adjacent] < 0) {
                self(self, adjacent);
                low_link[row] =
                    std::min(low_link[row], low_link[adjacent]);
            } else if (on_stack[adjacent]) {
                low_link[row] =
                    std::min(low_link[row], discovery[adjacent]);
            }
        }
        if (low_link[row] != discovery[row]) return;
        std::vector<std::size_t> component;
        while (!stack.empty()) {
            const std::size_t member = stack.back();
            stack.pop_back();
            on_stack[member] = false;
            component.push_back(member);
            if (member == row) break;
        }
        std::sort(component.begin(), component.end());
        components.push_back(std::move(component));
    };
    for (std::size_t row = 0; row < report.residual_count; ++row) {
        if (discovery[row] < 0) visit(visit, row);
    }

    std::vector<std::size_t> component_of(
        report.residual_count, 0U);
    std::vector<std::size_t> minimum_row(
        components.size(), 0U);
    for (std::size_t component = 0;
         component < components.size(); ++component) {
        minimum_row[component] = components[component].front();
        for (const std::size_t row : components[component]) {
            component_of[row] = component;
        }
    }
    std::vector<std::vector<std::size_t>> block_dependencies(
        components.size());
    std::vector<std::size_t> indegree(components.size(), 0U);
    for (std::size_t producer = 0;
         producer < dependencies.size(); ++producer) {
        for (const std::size_t consumer : dependencies[producer]) {
            const std::size_t from = component_of[producer];
            const std::size_t to = component_of[consumer];
            if (from != to) {
                block_dependencies[from].push_back(to);
            }
        }
    }
    for (auto& adjacent : block_dependencies) {
        std::sort(adjacent.begin(), adjacent.end());
        adjacent.erase(
            std::unique(adjacent.begin(), adjacent.end()),
            adjacent.end());
        for (const std::size_t target : adjacent) {
            ++indegree[target];
        }
    }

    std::vector<std::size_t> ready;
    for (std::size_t component = 0;
         component < components.size(); ++component) {
        if (indegree[component] == 0U) ready.push_back(component);
    }
    const auto pop_ready = [&]() {
        const auto selected = std::min_element(
            ready.begin(), ready.end(),
            [&minimum_row](std::size_t left, std::size_t right) {
                return minimum_row[left] < minimum_row[right];
            });
        const std::size_t component = *selected;
        ready.erase(selected);
        return component;
    };
    while (!ready.empty()) {
        const std::size_t component = pop_ready();
        StructuralBlock block;
        for (const std::size_t row : components[component]) {
            block.residual_indices.push_back(row);
            block.variable_indices.push_back(
                static_cast<std::size_t>(row_match[row]));
            block.residual_names.push_back(residual_names[row]);
            block.variable_names.push_back(
                variable_names.at(
                    static_cast<std::size_t>(row_match[row])));
        }
        const auto tear_rows = suggest_feedback_rows(
            dependencies, components[component], row_match);
        for (const std::size_t row : tear_rows) {
            const auto variable = static_cast<std::size_t>(
                row_match[row]);
            block.suggested_tear_variable_indices.push_back(variable);
            block.suggested_tear_variable_names.push_back(
                variable_names.at(variable));
        }
        block.acyclic_after_suggested_tears = true;
        report.structural_blocks.push_back(std::move(block));
        for (const std::size_t target :
             block_dependencies[component]) {
            if (--indegree[target] == 0U) ready.push_back(target);
        }
    }
    return report;
}

ProblemStructureReport analyze_problem_structure(const NonlinearProblem& problem) {
    const SparsePattern* structural_pattern =
        problem.structural_jacobian_pattern.has_value()
        ? &*problem.structural_jacobian_pattern
        : problem.sparse_jacobian_pattern.has_value()
            ? &*problem.sparse_jacobian_pattern
            : nullptr;
    if (structural_pattern == nullptr) {
        ProblemStructureReport report;
        report.variable_count = problem.initial_guess.size();
        report.residual_count = problem.residual_names.size();
        report.square =
            report.variable_count == report.residual_count;
        if (!report.square) {
            report.messages.push_back(
                "Newton solve requires equal variable and residual counts");
        }
        const auto find_duplicates =
            [](const std::vector<std::string>& names) {
                std::set<std::string> seen;
                std::set<std::string> duplicates;
                for (const auto& name : names) {
                    if (!seen.insert(name).second) {
                        duplicates.insert(name);
                    }
                }
                return std::vector<std::string>(
                    duplicates.begin(), duplicates.end());
            };
        report.duplicate_variable_names =
            find_duplicates(problem.variable_names);
        report.duplicate_residual_names =
            find_duplicates(problem.residual_names);
        report.has_duplicate_variable_names =
            !report.duplicate_variable_names.empty();
        report.has_duplicate_residual_names =
            !report.duplicate_residual_names.empty();
        if (report.has_duplicate_variable_names) {
            report.messages.push_back(
                "variable names must be unique");
        }
        if (report.has_duplicate_residual_names) {
            report.messages.push_back(
                "residual names must be unique");
        }
        report.messages.push_back(
            "no declared Jacobian incidence is available for structural matching");
        return report;
    }
    const SparsePattern& pattern = *structural_pattern;
    if (pattern.rows() != problem.residual_names.size() ||
        pattern.columns() != problem.initial_guess.size()) {
        ProblemStructureReport report;
        report.variable_count = problem.initial_guess.size();
        report.residual_count = problem.residual_names.size();
        report.square =
            report.variable_count == report.residual_count;
        report.has_complete_sparse_pattern = true;
        report.messages.push_back(
            "fixed Jacobian pattern shape does not match problem dimensions");
        return report;
    }
    std::vector<std::vector<std::size_t>> incidence(pattern.rows());
    for (std::size_t row = 0; row < pattern.rows(); ++row) {
        incidence[row].assign(
            pattern.column_indices().begin() +
                static_cast<std::ptrdiff_t>(
                    pattern.row_offsets()[row]),
            pattern.column_indices().begin() +
                static_cast<std::ptrdiff_t>(
                    pattern.row_offsets()[row + 1]));
    }
    return analyze_incidence_structure(
        problem.variable_names, problem.residual_names, incidence);
}

JacobianVerificationReport verify_problem_jacobian(
    const NonlinearProblem& problem,
    const std::vector<double>& point,
    const JacobianVerificationOptions& options) {
    if (!is_positive_finite(options.finite_difference_epsilon) ||
        !std::isfinite(options.absolute_tolerance) ||
        options.absolute_tolerance < 0.0 ||
        !std::isfinite(options.relative_tolerance) ||
        options.relative_tolerance < 0.0) {
        throw std::invalid_argument(
            "Jacobian verification tolerances must be finite and "
            "non-negative, with a positive finite-difference epsilon");
    }

    const auto lower_bounds = defaulted_lower_bounds(problem);
    const auto upper_bounds = defaulted_upper_bounds(problem);
    validate_problem(problem, lower_bounds, upper_bounds);
    const auto variable_scales = defaulted_variable_scales(problem);
    const auto& x = point.empty() ? problem.initial_guess : point;
    if (x.size() != problem.initial_guess.size()) {
        throw std::invalid_argument(
            "Jacobian verification point size must match the problem");
    }
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (!std::isfinite(x[i]) || x[i] < lower_bounds[i] ||
            x[i] > upper_bounds[i]) {
            throw std::invalid_argument(
                "Jacobian verification point is outside the variable domain");
        }
    }

    Matrix provided(
        problem.residual_names.size(),
        std::vector<double>(x.size(), 0.0));
    std::vector<bool> compared_rows(
        problem.residual_names.size(), false);
    if (problem.sparse_jacobian_pattern.has_value()) {
        std::vector<double> values(
            problem.sparse_jacobian_pattern->nonzeros(), 0.0);
        problem.sparse_jacobian_values(x, values);
        provided =
            SparseMatrix(
                *problem.sparse_jacobian_pattern,
                std::move(values))
                .to_dense();
        std::fill(compared_rows.begin(), compared_rows.end(), true);
    } else if (problem.sparse_jacobian) {
        std::vector<SparseTriplet> triplets;
        problem.sparse_jacobian(x, triplets);
        provided = sparse_from_triplets(
                       problem.residual_names.size(),
                       x.size(),
                       std::move(triplets))
                       .to_dense();
        std::fill(compared_rows.begin(), compared_rows.end(), true);
    } else if (problem.partial_sparse_jacobian) {
        std::vector<SparseTriplet> triplets;
        problem.partial_sparse_jacobian(x, triplets);
        provided = sparse_from_triplets(
                       problem.residual_names.size(),
                       x.size(),
                       std::move(triplets))
                       .to_dense();
        compared_rows = problem.analytic_jacobian_rows;
    } else if (problem.jacobian) {
        problem.jacobian(x, provided);
        std::fill(compared_rows.begin(), compared_rows.end(), true);
    }

    JacobianVerificationReport report;
    report.compared_rows = static_cast<std::size_t>(
        std::count(compared_rows.begin(), compared_rows.end(), true));
    report.analytic_derivatives_available = report.compared_rows != 0;
    if (!report.analytic_derivatives_available) {
        report.message = "problem provides no analytic Jacobian rows";
        return report;
    }
    validate_jacobian_shape(
        provided, problem.residual_names.size(), x.size());

    SolverDiagnostics diagnostics;
    const auto residual = evaluate_residual(problem, x, diagnostics);
    const Matrix numerical = finite_difference_jacobian(
        problem,
        x,
        residual,
        variable_scales,
        lower_bounds,
        upper_bounds,
        options.finite_difference_epsilon,
        diagnostics);
    for (std::size_t row = 0; row < numerical.size(); ++row) {
        if (!compared_rows[row]) continue;
        for (std::size_t column = 0; column < x.size(); ++column) {
            ++report.compared_entries;
            const double absolute_error =
                std::abs(provided[row][column] -
                         numerical[row][column]);
            const double reference = std::max(
                std::abs(provided[row][column]),
                std::abs(numerical[row][column]));
            const double relative_error =
                reference == 0.0 ? 0.0 : absolute_error / reference;
            report.maximum_absolute_error =
                std::max(report.maximum_absolute_error, absolute_error);
            report.maximum_relative_error =
                std::max(report.maximum_relative_error, relative_error);
            const double allowed =
                options.absolute_tolerance +
                options.relative_tolerance * reference;
            if (absolute_error <= allowed) continue;
            ++report.mismatch_count;
            if (report.mismatches.size() >=
                options.maximum_reported_mismatches) {
                continue;
            }
            report.mismatches.push_back({
                row,
                column,
                problem.residual_names[row],
                problem.variable_names[column],
                provided[row][column],
                numerical[row][column],
                absolute_error,
                relative_error,
            });
        }
    }
    report.passed = report.mismatch_count == 0;
    report.message = report.passed
        ? "provided Jacobian agrees with finite differences"
        : "provided Jacobian differs from finite differences";
    return report;
}

double l2_norm(const std::vector<double>& values) {
    long double sum = 0.0;
    for (const double value : values) {
        sum += static_cast<long double>(value) * static_cast<long double>(value);
    }
    return std::sqrt(static_cast<double>(sum));
}

namespace {

template <typename T>
std::vector<T> select_values(
    const std::vector<T>& source,
    const std::vector<std::size_t>& indices) {
    if (source.empty()) return {};
    std::vector<T> selected;
    selected.reserve(indices.size());
    for (const std::size_t index : indices) {
        selected.push_back(source.at(index));
    }
    return selected;
}

std::vector<double> lift_block_state(
    const std::vector<double>& base_state,
    const std::vector<std::size_t>& variable_indices,
    const std::vector<double>& block_state) {
    std::vector<double> state = base_state;
    for (std::size_t index = 0;
         index < variable_indices.size(); ++index) {
        state.at(variable_indices[index]) = block_state.at(index);
    }
    return state;
}

NonlinearProblem make_structural_block_problem(
    const NonlinearProblem& source,
    const StructuralBlock& block,
    const std::vector<double>& base_state) {
    NonlinearProblem restricted;
    restricted.variable_names = block.variable_names;
    restricted.residual_names = block.residual_names;
    restricted.initial_guess = select_values(
        base_state, block.variable_indices);
    restricted.variable_scales = select_values(
        source.variable_scales, block.variable_indices);
    restricted.residual_scales = select_values(
        source.residual_scales, block.residual_indices);
    restricted.lower_bounds = select_values(
        source.lower_bounds, block.variable_indices);
    restricted.upper_bounds = select_values(
        source.upper_bounds, block.variable_indices);
    restricted.checked_residual =
        [&source, base_state,
         variable_indices = block.variable_indices,
         residual_indices = block.residual_indices](
            const std::vector<double>& block_state,
            std::vector<double>& block_residual) {
            const auto state = lift_block_state(
                base_state, variable_indices, block_state);
            if (source.checked_residual_subset) {
                return source.checked_residual_subset(
                    state, residual_indices, block_residual);
            }
            std::vector<double> residual(
                source.residual_names.size(), 0.0);
            EvaluationStatus status = EvaluationStatus::success();
            if (source.checked_residual) {
                status = source.checked_residual(state, residual);
            } else {
                source.residual(state, residual);
            }
            if (!status.ok()) return status;
            if (residual.size() != source.residual_names.size()) {
                throw std::runtime_error(
                    "residual callback changed residual vector size");
            }
            for (std::size_t row = 0;
                 row < residual_indices.size(); ++row) {
                block_residual.at(row) =
                    residual.at(residual_indices[row]);
            }
            return EvaluationStatus::success();
        };

    const auto& source_pattern =
        *source.sparse_jacobian_pattern;
    const std::size_t missing =
        std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> local_column(
        source.initial_guess.size(), missing);
    for (std::size_t column = 0;
         column < block.variable_indices.size(); ++column) {
        local_column.at(block.variable_indices[column]) = column;
    }
    std::vector<std::size_t> row_offsets{0U};
    std::vector<std::size_t> column_indices;
    std::vector<std::size_t> source_offsets;
    for (const std::size_t source_row : block.residual_indices) {
        std::vector<std::pair<std::size_t, std::size_t>> entries;
        for (std::size_t offset =
                 source_pattern.row_offsets()[source_row];
             offset < source_pattern.row_offsets()[source_row + 1];
             ++offset) {
            const std::size_t column = local_column.at(
                source_pattern.column_indices()[offset]);
            if (column != missing) {
                entries.emplace_back(column, offset);
            }
        }
        std::sort(entries.begin(), entries.end());
        for (const auto& [column, source_offset] : entries) {
            column_indices.push_back(column);
            source_offsets.push_back(source_offset);
        }
        row_offsets.push_back(column_indices.size());
    }
    restricted.sparse_jacobian_pattern = SparsePattern(
        block.residual_indices.size(),
        block.variable_indices.size(),
        std::move(row_offsets),
        std::move(column_indices));
    restricted.sparse_jacobian_values =
        [&source, base_state,
         variable_indices = block.variable_indices,
         source_offsets = std::move(source_offsets)](
            const std::vector<double>& block_state,
            std::vector<double>& block_values) {
            const auto state = lift_block_state(
                base_state, variable_indices, block_state);
            if (source.sparse_jacobian_values_subset) {
                source.sparse_jacobian_values_subset(
                    state, source_offsets, block_values);
                return;
            }
            std::vector<double> source_values(
                source.sparse_jacobian_pattern->nonzeros(), 0.0);
            source.sparse_jacobian_values(state, source_values);
            if (source_values.size() !=
                source.sparse_jacobian_pattern->nonzeros()) {
                throw std::runtime_error(
                    "sparse_jacobian_values changed values vector size");
            }
            for (std::size_t offset = 0;
                 offset < source_offsets.size(); ++offset) {
                block_values.at(offset) =
                    source_values.at(source_offsets[offset]);
            }
        };
    return restricted;
}

void accumulate_block_diagnostics(
    SolverDiagnostics& aggregate,
    const SolverDiagnostics& block) {
    aggregate.iterations += block.iterations;
    aggregate.function_evaluations += block.function_evaluations;
    aggregate.jacobian_evaluations += block.jacobian_evaluations;
    aggregate.linear_solver_evaluations +=
        block.linear_solver_evaluations;
    aggregate.symbolic_factorizations +=
        block.symbolic_factorizations;
    aggregate.numeric_factorizations +=
        block.numeric_factorizations;
    aggregate.last_linear_backward_error =
        block.last_linear_backward_error;
    aggregate.maximum_linear_backward_error = std::max(
        aggregate.maximum_linear_backward_error,
        block.maximum_linear_backward_error);
    aggregate.largest_linear_system_size = std::max(
        aggregate.largest_linear_system_size,
        block.largest_linear_system_size);
    aggregate.final_step_norm = std::max(
        aggregate.final_step_norm, block.final_step_norm);
    if (block.linear_solver_backend != "not-used") {
        aggregate.linear_solver_backend =
            block.linear_solver_backend;
    }
    aggregate.history.insert(
        aggregate.history.end(),
        block.history.begin(), block.history.end());
}

void prepend_failed_attempt_diagnostics(
    SolverDiagnostics& final,
    const SolverDiagnostics& attempt) {
    final.iterations += attempt.iterations;
    final.function_evaluations += attempt.function_evaluations;
    final.jacobian_evaluations += attempt.jacobian_evaluations;
    final.linear_solver_evaluations +=
        attempt.linear_solver_evaluations;
    final.symbolic_factorizations +=
        attempt.symbolic_factorizations;
    final.numeric_factorizations +=
        attempt.numeric_factorizations;
    final.maximum_linear_backward_error = std::max(
        final.maximum_linear_backward_error,
        attempt.maximum_linear_backward_error);
    final.structural_block_solves +=
        attempt.structural_block_solves;
    final.largest_linear_system_size = std::max(
        final.largest_linear_system_size,
        attempt.largest_linear_system_size);
    final.history.insert(
        final.history.begin(),
        attempt.history.begin(), attempt.history.end());
}

NonlinearSolveResult solve_newton_monolithic(
    const NonlinearProblem& problem,
    const SolverOptions& options) {
    validate_options(options);
    const auto variable_scales = defaulted_variable_scales(problem);
    const auto residual_scales = defaulted_residual_scales(problem);
    const auto lower_bounds = defaulted_lower_bounds(problem);
    const auto upper_bounds = defaulted_upper_bounds(problem);
    validate_problem(problem, lower_bounds, upper_bounds);
    const auto structure = analyze_problem_structure(problem);
    if (structure.has_duplicate_variable_names) {
        throw std::invalid_argument("variable_names must be unique");
    }
    if (structure.has_duplicate_residual_names) {
        throw std::invalid_argument("residual_names must be unique");
    }
    if (structure.has_complete_sparse_pattern && !structure.structurally_nonsingular) {
        throw std::invalid_argument("declared Jacobian incidence is structurally singular");
    }
    std::vector<std::size_t> tear_rows;
    std::vector<std::size_t> tear_columns;
    if (options.structural_decomposition_policy ==
            StructuralDecompositionPolicy::tearing) {
        if (!structure.has_complete_sparse_pattern) {
            throw std::invalid_argument(
                "structural tearing requires a declared Jacobian incidence pattern");
        }
        for (const auto& block : structure.structural_blocks) {
            for (const std::size_t tear_column :
                 block.suggested_tear_variable_indices) {
                const auto match = std::find(
                    block.variable_indices.begin(),
                    block.variable_indices.end(), tear_column);
                if (match == block.variable_indices.end()) {
                    throw std::logic_error(
                        "structural tear variable is not in its block");
                }
                const auto offset = static_cast<std::size_t>(
                    std::distance(block.variable_indices.begin(), match));
                tear_columns.push_back(tear_column);
                tear_rows.push_back(block.residual_indices.at(offset));
            }
        }
    }

    std::vector<double> x = problem.initial_guess;
    SolverDiagnostics diagnostics;
    SparseFactorizationPtr factorization =
        options.sparse_factorization;
    if (!factorization &&
        options.sparse_factorization_resolver &&
        problem.sparse_jacobian_pattern.has_value()) {
        factorization = options.sparse_factorization_resolver(
            *problem.sparse_jacobian_pattern);
    }
    if (!factorization && !options.sparse_linear_solver &&
        !options.linear_solver) {
        factorization = make_default_sparse_factorization();
    }

    for (int iteration = 0; iteration <= options.max_iterations; ++iteration) {
        std::vector<double> residual;
        try {
            residual = evaluate_residual(problem, x, diagnostics);
        } catch (const EvaluationError& ex) {
            diagnostics.converged = false;
            diagnostics.iterations = iteration;
            diagnostics.final_residual_norm = std::numeric_limits<double>::infinity();
            diagnostics.message =
                std::string(ex.recoverable() ? "recoverable" : "fatal") +
                " residual evaluation failure: " + ex.what();
            return {x, diagnostics};
        }
        const double residual_norm = scaled_residual_norm(residual, residual_scales);
        set_final_residual_diagnostics(
            diagnostics, residual, residual_scales,
            problem.residual_names, residual_norm);

        if (!std::isfinite(residual_norm)) {
            diagnostics.converged = false;
            diagnostics.iterations = iteration;
            diagnostics.final_residual_norm = residual_norm;
            diagnostics.message = "residual norm is not finite";
            return {x, diagnostics};
        }

        if (residual_norm <= options.residual_tolerance) {
            diagnostics.converged = true;
            diagnostics.iterations = iteration;
            diagnostics.final_residual_norm = residual_norm;
            diagnostics.final_step_norm = 0.0;
            diagnostics.message = "converged by residual tolerance";
            return {x, diagnostics};
        }

        if (iteration == options.max_iterations) {
            diagnostics.converged = false;
            diagnostics.iterations = iteration;
            diagnostics.final_residual_norm = residual_norm;
            diagnostics.message = "maximum iterations reached";
            return {x, diagnostics};
        }

        EvaluatedJacobian jacobian;
        try {
            jacobian = evaluate_jacobian(problem, x, residual, variable_scales, lower_bounds,
                                         upper_bounds, options, diagnostics);
        } catch (const std::exception& ex) {
            diagnostics.converged = false;
            diagnostics.iterations = iteration;
            diagnostics.final_residual_norm = residual_norm;
            diagnostics.message = std::string("Jacobian evaluation failed: ") + ex.what();
            return {x, diagnostics};
        }
        scale_jacobian_rows(jacobian, residual_scales);
        scale_jacobian_columns(jacobian, variable_scales);
        const auto scaled_residual = scale_residual(residual, residual_scales);
        std::vector<double> rhs(scaled_residual.size(), 0.0);
        for (std::size_t i = 0; i < scaled_residual.size(); ++i) {
            rhs[i] = -scaled_residual[i];
        }

        LinearSolveResult linear;
        if (tear_columns.empty()) {
            linear = solve_linear_system(
                options, factorization, std::move(jacobian),
                std::move(rhs), x.size(), diagnostics);
        } else {
            linear = solve_linear_system_by_tearing(
                options, jacobian, rhs,
                tear_rows, tear_columns, diagnostics);
            if (!linear.success) {
                const std::string tearing_failure = linear.message;
                linear = solve_linear_system(
                    options, factorization, std::move(jacobian),
                    std::move(rhs), x.size(), diagnostics);
                diagnostics.linear_solver_backend =
                    "structural-schur-fallback/" +
                    diagnostics.linear_solver_backend;
                if (!linear.success) {
                    linear.message = "structural tearing failed (" +
                        tearing_failure + "); full solve also failed: " +
                        linear.message;
                }
            }
        }
        if (!linear.success) {
            diagnostics.converged = false;
            diagnostics.iterations = iteration;
            diagnostics.final_residual_norm = residual_norm;
            diagnostics.message = "linear solve failed: " + linear.message;
            return {x, diagnostics};
        }

        std::vector<double> physical_step(linear.x.size(), 0.0);
        for (std::size_t i = 0; i < physical_step.size(); ++i) {
            physical_step[i] = linear.x[i] * variable_scales[i];
        }
        const double full_step_norm = scaled_step_norm(physical_step, variable_scales);
        if (!std::isfinite(full_step_norm)) {
            diagnostics.converged = false;
            diagnostics.iterations = iteration;
            diagnostics.final_residual_norm = residual_norm;
            diagnostics.message = "newton step norm is not finite";
            return {x, diagnostics};
        }

        double damping = 1.0;
        std::vector<double> accepted_x = x;
        std::vector<double> accepted_residual;
        double accepted_norm = std::numeric_limits<double>::infinity();
        double accepted_step_norm = 0.0;
        bool accepted = false;

        for (int line_search_step = 0; line_search_step < options.max_line_search_steps &&
                                       damping >= options.min_damping;
             ++line_search_step) {
            auto candidate_x =
                add_scaled_step(x, physical_step, lower_bounds, upper_bounds, damping);
            std::vector<double> actual_step(candidate_x.size(), 0.0);
            for (std::size_t i = 0; i < candidate_x.size(); ++i) {
                actual_step[i] = candidate_x[i] - x[i];
            }

            std::vector<double> candidate_residual;
            try {
                candidate_residual = evaluate_residual(problem, candidate_x, diagnostics);
            } catch (const EvaluationError& ex) {
                if (!ex.recoverable()) {
                    diagnostics.converged = false;
                    diagnostics.iterations = iteration;
                    diagnostics.final_residual_norm = residual_norm;
                    diagnostics.message =
                        std::string("fatal residual evaluation failure during line search: ") +
                        ex.what();
                    return {x, diagnostics};
                }
                damping *= options.damping_reduction;
                continue;
            }
            const double candidate_norm = scaled_residual_norm(candidate_residual, residual_scales);
            const double armijo_limit = residual_norm * (1.0 - options.sufficient_decrease * damping);
            if (std::isfinite(candidate_norm) && candidate_norm <= armijo_limit &&
                candidate_norm < residual_norm) {
                accepted_x = std::move(candidate_x);
                accepted_residual = std::move(candidate_residual);
                accepted_norm = candidate_norm;
                accepted_step_norm = scaled_step_norm(actual_step, variable_scales);
                accepted = true;
                break;
            }
            damping *= options.damping_reduction;
        }

        diagnostics.history.push_back(IterationDiagnostic{
            iteration, residual_norm, accepted ? accepted_norm : residual_norm,
            accepted ? accepted_step_norm : damping * full_step_norm, accepted ? damping : 0.0});

        if (!accepted) {
            diagnostics.converged = false;
            diagnostics.iterations = iteration + 1;
            diagnostics.final_residual_norm = residual_norm;
            diagnostics.final_step_norm = 0.0;
            std::size_t dominant = 0;
            double dominant_magnitude = 0.0;
            for (std::size_t row = 0; row < residual.size(); ++row) {
                const double magnitude =
                    std::abs(residual[row] / residual_scales[row]);
                if (magnitude > dominant_magnitude) {
                    dominant = row;
                    dominant_magnitude = magnitude;
                }
            }
            std::ostringstream magnitude;
            magnitude << std::scientific << std::setprecision(6)
                      << dominant_magnitude;
            diagnostics.message =
                "line search failed to reduce residual; dominant "
                "scaled residual '" +
                problem.residual_names.at(dominant) + "'=" +
                magnitude.str();
            return {x, diagnostics};
        }

        x = std::move(accepted_x);
        diagnostics.final_step_norm = accepted_step_norm;
        set_final_residual_diagnostics(
            diagnostics, accepted_residual, residual_scales,
            problem.residual_names, accepted_norm);
        if (accepted_step_norm <= options.step_tolerance &&
            accepted_norm <= options.residual_tolerance * 10.0) {
            diagnostics.converged = true;
            diagnostics.iterations = iteration + 1;
            diagnostics.final_residual_norm = accepted_norm;
            diagnostics.message = "converged by step tolerance";
            return {x, diagnostics};
        }
    }

    diagnostics.converged = false;
    diagnostics.iterations = options.max_iterations;
    try {
        const auto residual =
            evaluate_residual(problem, x, diagnostics);
        set_final_residual_diagnostics(
            diagnostics, residual, residual_scales,
            problem.residual_names,
            scaled_residual_norm(residual, residual_scales));
    } catch (const std::exception&) {
        diagnostics.final_residual_norm = std::numeric_limits<double>::infinity();
        diagnostics.final_maximum_absolute_normalized_residual =
            std::numeric_limits<double>::infinity();
        diagnostics.limiting_residual.clear();
    }
    diagnostics.message = "solver exited unexpectedly";
    return {x, diagnostics};
}

NonlinearSolveResult solve_newton_by_structural_blocks(
    const NonlinearProblem& problem,
    const SolverOptions& options,
    const ProblemStructureReport& structure) {
    NonlinearSolveResult result;
    result.x = problem.initial_guess;
    SolverOptions block_options = options;
    block_options.structural_decomposition_policy =
        StructuralDecompositionPolicy::monolithic;
    auto factorization_resolver =
        options.sparse_factorization_resolver;
    if (!factorization_resolver &&
        !options.sparse_factorization &&
        !options.sparse_linear_solver &&
        !options.linear_solver) {
        factorization_resolver =
            make_default_sparse_factorization_resolver();
    }
    // The monolithic solver may accept a step-tolerance termination at up to
    // ten times its residual tolerance. Allocate that allowance across the
    // blocks so their combined L2 residual still satisfies the caller's
    // whole-system tolerance.
    block_options.residual_tolerance =
        options.residual_tolerance /
        (10.0 * std::sqrt(static_cast<double>(
                    structure.structural_blocks.size())));
    for (const auto& block : structure.structural_blocks) {
        auto restricted = make_structural_block_problem(
            problem, block, result.x);
        if (!options.sparse_factorization &&
            factorization_resolver &&
            restricted.sparse_jacobian_pattern.has_value()) {
            block_options.sparse_factorization =
                factorization_resolver(
                    *restricted.sparse_jacobian_pattern);
        }
        auto solved = solve_newton_monolithic(
            restricted, block_options);
        ++result.diagnostics.structural_block_solves;
        accumulate_block_diagnostics(
            result.diagnostics, solved.diagnostics);
        for (std::size_t index = 0;
             index < block.variable_indices.size(); ++index) {
            result.x.at(block.variable_indices[index]) =
                solved.x.at(index);
        }
        if (!solved.diagnostics.converged) {
            result.diagnostics.converged = false;
            result.diagnostics.failed_structural_block =
                block.residual_names.empty()
                ? "unnamed"
                : block.residual_names.front();
            result.diagnostics.message =
                "structural block '" +
                result.diagnostics.failed_structural_block +
                "' failed: " + solved.diagnostics.message;
            return result;
        }
    }

    const auto residual_scales =
        defaulted_residual_scales(problem);
    try {
        const auto residual = evaluate_residual(
            problem, result.x, result.diagnostics);
        const double norm = scaled_residual_norm(
            residual, residual_scales);
        set_final_residual_diagnostics(
            result.diagnostics, residual, residual_scales,
            problem.residual_names, norm);
        result.diagnostics.converged =
            std::isfinite(norm) &&
            norm <= options.residual_tolerance;
        result.diagnostics.message =
            result.diagnostics.converged
            ? "converged by dependency-ordered structural blocks"
            : "structural block solve failed whole-system residual check";
    } catch (const std::exception& error) {
        result.diagnostics.converged = false;
        result.diagnostics.final_residual_norm =
            std::numeric_limits<double>::infinity();
        result.diagnostics.message =
            std::string(
                "whole-system residual evaluation failed after "
                "structural block solve: ") + error.what();
    }
    return result;
}

}  // namespace

NonlinearSolveResult solve_newton(
    const NonlinearProblem& problem,
    const SolverOptions& options) {
    if (options.structural_decomposition_policy ==
            StructuralDecompositionPolicy::monolithic ||
        options.structural_decomposition_policy ==
            StructuralDecompositionPolicy::tearing ||
        !problem.sparse_jacobian_pattern.has_value()) {
        return solve_newton_monolithic(problem, options);
    }
    validate_options(options);
    const auto lower_bounds = defaulted_lower_bounds(problem);
    const auto upper_bounds = defaulted_upper_bounds(problem);
    (void)defaulted_variable_scales(problem);
    (void)defaulted_residual_scales(problem);
    validate_problem(problem, lower_bounds, upper_bounds);
    const auto structure = analyze_problem_structure(problem);
    if (!structure.valid_for_newton()) {
        return solve_newton_monolithic(problem, options);
    }
    if (structure.structural_blocks.size() <= 1U) {
        return solve_newton_monolithic(problem, options);
    }
    if (options.structural_decomposition_policy ==
            StructuralDecompositionPolicy::automatic &&
        (!problem.automatic_structural_decomposition_safe ||
         !problem.checked_residual_subset ||
         !problem.sparse_jacobian_values_subset)) {
        return solve_newton_monolithic(problem, options);
    }
    auto block_result = solve_newton_by_structural_blocks(
        problem, options, structure);
    if (block_result.diagnostics.converged ||
        options.structural_decomposition_policy ==
            StructuralDecompositionPolicy::blocks) {
        return block_result;
    }
    auto monolithic_result = solve_newton_monolithic(
        problem, options);
    prepend_failed_attempt_diagnostics(
        monolithic_result.diagnostics,
        block_result.diagnostics);
    monolithic_result.diagnostics.message =
        "automatic structural solve fell back to monolithic after: " +
        block_result.diagnostics.message + "; " +
        monolithic_result.diagnostics.message;
    return monolithic_result;
}

}  // namespace thermox
