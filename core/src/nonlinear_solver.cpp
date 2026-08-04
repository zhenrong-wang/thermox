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
    if (options.max_iterations < 0) {
        throw std::invalid_argument("max_iterations must be non-negative");
    }
    if (!is_positive_finite(options.residual_tolerance)) {
        throw std::invalid_argument("residual_tolerance must be positive and finite");
    }
    if (!is_positive_finite(options.step_tolerance)) {
        throw std::invalid_argument("step_tolerance must be positive and finite");
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
        std::vector<double> candidate_steps;
        if (x[col] + magnitude <= upper_bounds[col]) {
            candidate_steps.push_back(magnitude);
        }
        if (x[col] - magnitude >= lower_bounds[col]) {
            candidate_steps.push_back(-magnitude);
        }
        if (candidate_steps.empty()) {
            continue;
        }

        std::vector<double> fp;
        double accepted_step = 0.0;
        bool evaluated = false;
        for (const double step : candidate_steps) {
            std::vector<double> xp = x;
            xp[col] += step;
            try {
                fp = evaluate_residual(problem, xp, diagnostics);
                accepted_step = step;
                evaluated = true;
                break;
            } catch (const EvaluationError& ex) {
                if (!ex.recoverable()) {
                    throw;
                }
            }
        }
        if (!evaluated) {
            throw EvaluationError(
                "finite-difference perturbations failed in the valid variable domain", true);
        }
        for (std::size_t row = 0; row < f.size(); ++row) {
            jacobian[row][col] = (fp[row] - f[row]) / accepted_step;
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

LinearSolveResult solve_linear_system(const SolverOptions& options,
                                      const SparseFactorizationPtr& factorization,
                                      EvaluatedJacobian jacobian,
                                      std::vector<double> rhs,
                                      std::size_t expected_size,
                                      SolverDiagnostics& diagnostics) {
    ++diagnostics.linear_solver_evaluations;

    LinearSolveResult result;
    if (options.sparse_linear_solver) {
        diagnostics.linear_solver_backend =
            "custom-sparse-hook";
        auto sparse = jacobian.is_sparse ? std::move(jacobian.sparse)
                                         : sparse_from_dense(jacobian.dense);
        result = options.sparse_linear_solver(std::move(sparse), std::move(rhs));
    } else if (jacobian.is_sparse && !options.linear_solver) {
        diagnostics.linear_solver_backend =
            std::string(factorization->backend_name());
        result = factorization->solve(
            jacobian.sparse, std::move(rhs));
    } else if (options.sparse_factorization &&
               !options.linear_solver) {
        diagnostics.linear_solver_backend =
            std::string(factorization->backend_name());
        auto sparse = jacobian.is_sparse
            ? std::move(jacobian.sparse)
            : sparse_from_dense(jacobian.dense);
        result = factorization->solve(sparse, std::move(rhs));
    } else {
        diagnostics.linear_solver_backend =
            options.linear_solver
            ? "custom-dense-hook"
            : "reference-dense";
        auto dense = jacobian.is_sparse ? jacobian.sparse.to_dense() : std::move(jacobian.dense);
        result = options.linear_solver ? options.linear_solver(std::move(dense), std::move(rhs))
                                       : solve_dense_linear_system(std::move(dense), std::move(rhs));
    }
    diagnostics.symbolic_factorizations +=
        result.symbolic_factorizations;
    diagnostics.numeric_factorizations +=
        result.numeric_factorizations;
    return validate_linear_result(std::move(result), expected_size);
}

}  // namespace

EvaluationStatus EvaluationStatus::recoverable(std::string message) {
    return {EvaluationStatusCode::recoverable_failure, std::move(message)};
}

EvaluationStatus EvaluationStatus::fatal(std::string message) {
    return {EvaluationStatusCode::fatal_failure, std::move(message)};
}

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
    }
    return report;
}

ProblemStructureReport analyze_problem_structure(const NonlinearProblem& problem) {
    if (!problem.sparse_jacobian_pattern.has_value()) {
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
            "no fixed Jacobian pattern is available for structural matching");
        return report;
    }
    const SparsePattern& pattern =
        *problem.sparse_jacobian_pattern;
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

NonlinearSolveResult solve_newton(const NonlinearProblem& problem, const SolverOptions& options) {
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
        throw std::invalid_argument("fixed sparse Jacobian pattern is structurally singular");
    }

    std::vector<double> x = problem.initial_guess;
    SolverDiagnostics diagnostics;
    SparseFactorizationPtr factorization =
        options.sparse_factorization;
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

        const auto linear = solve_linear_system(
            options, factorization, std::move(jacobian),
            std::move(rhs), x.size(), diagnostics);
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
        diagnostics.final_residual_norm =
            scaled_residual_norm(evaluate_residual(problem, x, diagnostics), residual_scales);
    } catch (const std::exception&) {
        diagnostics.final_residual_norm = std::numeric_limits<double>::infinity();
    }
    diagnostics.message = "solver exited unexpectedly";
    return {x, diagnostics};
}

}  // namespace thermox
