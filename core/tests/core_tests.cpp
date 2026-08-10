#include "thermox/dense_linear_solver.hpp"
#include "thermox/continuation_solver.hpp"
#include "thermox/equation_system.hpp"
#include "thermox/nonlinear_solver.hpp"
#include "thermox/sparse_linear_solver.hpp"
#include "thermox/sparse_matrix.hpp"
#include "thermox/variable_registry.hpp"

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(double actual, double expected, double tolerance, const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message + ": actual=" + std::to_string(actual) +
                                 " expected=" + std::to_string(expected));
    }
}

void require_contains(const std::string& haystack,
                      const std::string& needle,
                      const std::string& message) {
    if (haystack.find(needle) == std::string::npos) {
        throw std::runtime_error(message + ": text='" + haystack + "' missing='" + needle + "'");
    }
}

void require_throws_invalid_argument(const std::function<void()>& action,
                                     const std::string& message) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(message);
}

void test_dense_linear_solver() {
    thermox::Matrix a{{3.0, 2.0}, {1.0, 2.0}};
    std::vector<double> b{5.0, 5.0};
    const auto result = thermox::solve_dense_linear_system(a, b);
    require(result.success, result.message);
    require_near(result.x[0], 0.0, 1.0e-12, "dense solve x0");
    require_near(result.x[1], 2.5, 1.0e-12, "dense solve x1");

    const auto tiny = thermox::solve_dense_linear_system({{1.0e-20}}, {2.0e-20});
    require(tiny.success, "dense solver should be invariant to uniform matrix scaling");
    require_near(tiny.x[0], 2.0, 1.0e-12, "tiny scaled dense solve");
}

void test_sparse_linear_solver() {
    auto matrix = thermox::sparse_from_triplets(
        3,
        3,
        {
            {0, 0, 0.0}, {0, 1, 2.0}, {0, 2, 1.0},
            {1, 0, 1.0}, {1, 1, -2.0}, {1, 2, -3.0},
            {2, 0, -1.0}, {2, 1, 1.0}, {2, 2, 2.0},
        });
    const auto result = thermox::solve_sparse_linear_system(std::move(matrix), {5.0, -4.0, 4.0});
    require(result.success, result.message);
    require_near(result.x.at(0), -9.0, 1.0e-12, "sparse solve x0 with pivoting");
    require_near(result.x.at(1), 5.0, 1.0e-12, "sparse solve x1 with pivoting");
    require_near(result.x.at(2), -5.0, 1.0e-12, "sparse solve x2 with pivoting");

    const auto nonsquare = thermox::solve_sparse_linear_system(
        thermox::sparse_from_triplets(1, 2, {{0, 0, 1.0}}), {1.0});
    require(!nonsquare.success, "sparse solver rejects nonsquare matrices");
    require_contains(nonsquare.message, "square", "sparse nonsquare failure message");

    const auto singular = thermox::solve_sparse_linear_system(
        thermox::sparse_from_triplets(2, 2, {{0, 0, 1.0}, {1, 0, 2.0}}), {1.0, 2.0});
    require(!singular.success, "sparse solver rejects singular matrices");
    require_contains(singular.message, "singular sparse matrix", "sparse singular failure message");

    const auto tiny = thermox::solve_sparse_linear_system(
        thermox::sparse_from_triplets(1, 1, {{0, 0, 1.0e-20}}), {2.0e-20});
    require(tiny.success, "sparse solver should be invariant to uniform matrix scaling");
    require_near(tiny.x[0], 2.0, 1.0e-12, "tiny scaled sparse solve");
}

void test_reusable_sparse_factorization() {
    auto factorization =
        thermox::make_default_sparse_factorization();
    require(
        factorization != nullptr,
        "default sparse factorization factory");
    auto matrix = thermox::sparse_from_triplets(
        2, 2,
        {
            {0, 0, 3.0}, {0, 1, 1.0},
            {1, 0, 1.0}, {1, 1, 2.0},
        });
    const auto first = factorization->solve(
        matrix, {7.0, 5.0});
    require(first.success, first.message);
    require_near(first.x[0], 1.8, 1.0e-12,
                 "reusable sparse first x0");
    require_near(first.x[1], 1.6, 1.0e-12,
                 "reusable sparse first x1");
    require(first.numeric_factorizations == 1,
            "first sparse solve performs numeric factorization");

    matrix = thermox::SparseMatrix(
        matrix.pattern(), {4.0, 1.0, 1.0, 3.0});
    const auto second = factorization->solve(
        matrix, {9.0, 7.0});
    require(second.success, second.message);
    require_near(second.x[0], 20.0 / 11.0, 1.0e-12,
                 "reusable sparse second x0");
    require_near(second.x[1], 19.0 / 11.0, 1.0e-12,
                 "reusable sparse second x1");
    require(second.numeric_factorizations == 1,
            "changed values refresh numeric factorization");

    const bool umfpack =
        factorization->backend_name() == "umfpack";
    require(first.symbolic_factorizations ==
                (umfpack ? 1 : 0),
            "first solve reports backend symbolic analysis");
    require(second.symbolic_factorizations == 0,
            "same pattern reuses symbolic analysis");

    const auto changed_pattern =
        thermox::sparse_from_triplets(
            2, 2, {{0, 0, 2.0}, {1, 1, 4.0}});
    const auto third = factorization->solve(
        changed_pattern, {6.0, 8.0});
    require(third.success, third.message);
    require_near(third.x[0], 3.0, 1.0e-12,
                 "changed-pattern sparse x0");
    require_near(third.x[1], 2.0, 1.0e-12,
                 "changed-pattern sparse x1");
    require(third.symbolic_factorizations ==
                (umfpack ? 1 : 0),
            "changed pattern invalidates symbolic analysis");
}

void test_newton_reuses_sparse_symbolic_factorization() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x"};
    problem.residual_names = {"square_root"};
    problem.initial_guess = {1.0};
    problem.sparse_jacobian_pattern =
        thermox::sparse_from_triplets(
            1, 1, {{0, 0, 1.0}})
            .pattern();
    problem.residual = [](
                           const std::vector<double>& x,
                           std::vector<double>& residual) {
        residual[0] = x[0] * x[0] - 2.0;
    };
    problem.sparse_jacobian_values = [](
                                           const std::vector<double>& x,
                                           std::vector<double>& values) {
        values[0] = 2.0 * x[0];
    };
    thermox::SolverOptions options;
    options.sparse_factorization =
        thermox::make_default_sparse_factorization();
    const bool umfpack =
        options.sparse_factorization->backend_name() == "umfpack";
    const auto result = thermox::solve_newton(problem, options);
    require(result.diagnostics.converged,
            result.diagnostics.message);
    require(result.diagnostics.linear_solver_evaluations > 1,
            "nonlinear regression exercises repeated factorization");
    require(
        result.diagnostics.numeric_factorizations ==
            result.diagnostics.linear_solver_evaluations,
        "every Newton matrix receives numeric factorization");
    require(
        result.diagnostics.symbolic_factorizations ==
            (umfpack ? 1 : 0),
        "Newton reuses one symbolic analysis for fixed pattern");
    require(
        result.diagnostics.linear_solver_backend ==
            options.sparse_factorization->backend_name(),
        "Newton reports selected sparse backend");
}

void test_continuation_recovers_difficult_initial_guess() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x"};
    problem.residual_names = {"quadratic_target"};
    problem.initial_guess = {1.0};
    problem.variable_scales = {10.0};
    problem.residual_scales = {100.0};
    problem.lower_bounds = {0.0};
    problem.upper_bounds = {100.0};
    problem.residual = [](
                           const std::vector<double>& x,
                           std::vector<double>& residual) {
        residual[0] = x[0] * x[0] - 100.0;
    };
    problem.sparse_jacobian_pattern =
        thermox::sparse_from_triplets(
            1, 1, {{0, 0, 1.0}})
            .pattern();
    problem.sparse_jacobian_values = [](
                                           const std::vector<double>& x,
                                           std::vector<double>& values) {
        values[0] = 2.0 * x[0];
    };

    thermox::SolverOptions solver;
    solver.max_iterations = 4;
    solver.residual_tolerance = 1.0e-10;
    const auto direct = thermox::solve_newton(problem, solver);
    require(
        !direct.diagnostics.converged,
        "bounded direct solve should expose difficult start");

    thermox::ContinuationOptions continuation;
    continuation.initial_step = 0.05;
    continuation.minimum_step = 1.0 / 1024.0;
    continuation.step_growth = 1.5;
    const auto result = thermox::solve_continuation(
        problem, solver, continuation);
    require(result.diagnostics.converged,
            result.diagnostics.message);
    require(result.continuation.converged,
            result.continuation.message);
    require_near(
        result.continuation.reached_parameter, 1.0, 0.0,
        "continuation reaches target parameter");
    require_near(result.x[0], 10.0, 1.0e-8,
                 "continuation solves difficult target");
    require(result.continuation.accepted_stages > 1,
            "continuation uses staged warm starts");
    require(
        result.diagnostics.numeric_factorizations ==
            result.diagnostics.linear_solver_evaluations,
        "continuation aggregates numeric factorization work");
    if (result.diagnostics.linear_solver_backend == "umfpack") {
        require(
            result.diagnostics.symbolic_factorizations == 1,
            "fixed-pattern continuation reuses symbolic analysis");
    }
}

void test_continuation_falls_back_to_solvable_target() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x", "y"};
    problem.residual_names = {"y_target", "x_target"};
    problem.initial_guess = {1.0, 2.0};
    problem.variable_scales = {1.0, 1.0};
    problem.residual_scales = {1.0, 1.0};
    problem.lower_bounds = {0.0, 0.0};
    problem.upper_bounds = {
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    problem.residual = [](
                           const std::vector<double>& x,
                           std::vector<double>& residual) {
        residual[0] = x[1] - 4.0;
        residual[1] = x[0] - 3.0;
    };
    problem.jacobian = [](
                           const std::vector<double>&,
                           thermox::Matrix& jacobian) {
        jacobian[0][1] = 1.0;
        jacobian[1][0] = 1.0;
    };

    thermox::ContinuationOptions continuation;
    continuation.initial_step = 0.5;
    continuation.minimum_step = 0.5;
    const auto result = thermox::solve_continuation(
        problem, {}, continuation);

    require(result.diagnostics.converged,
            result.diagnostics.message);
    require(result.continuation.converged,
            result.continuation.message);
    require_near(result.x[0], 3.0, 1.0e-12,
                 "target fallback solves x");
    require_near(result.x[1], 4.0, 1.0e-12,
                 "target fallback solves y");
    require(
        result.continuation.rejected_stages == 1 &&
            result.continuation.accepted_stages == 1,
        "fallback diagnostics expose the rejected homotopy "
        "stage and accepted target solve");
    require(
        result.continuation.message.find(
            "direct target fallback converged") !=
            std::string::npos,
        "fallback convergence is explicit in diagnostics");
}

void test_equation_builder_exposes_component_continuation_path() {
    thermox::EquationSystemBuilder system;
    const auto x =
        system.add_variable("x", 1.0, 10.0);
    system.add_continuation_sparse_equation(
        "staged_target",
        {x},
        [x](const std::vector<double>& values,
            const std::vector<double>& anchor,
            double parameter,
            std::vector<thermox::EquationPartial>&
                jacobian) {
            jacobian.push_back({x, 1.0});
            return values.at(x) -
                   (anchor.at(x) +
                    (10.0 - anchor.at(x)) * parameter);
        },
        10.0);

    const auto problem = system.build();
    require(
        static_cast<bool>(
            problem.continuation_checked_residual),
        "builder exposes parameterized residual");
    require(
        static_cast<bool>(
            problem.continuation_sparse_jacobian_values),
        "builder exposes parameterized fixed-pattern Jacobian");

    std::vector<double> residual(1, 0.0);
    const auto status =
        problem.continuation_checked_residual(
            {1.0}, {1.0}, 0.5, residual);
    require(status.ok(), status.message);
    require_near(
        residual.at(0), -4.5, 0.0,
        "continuation equation receives stage parameter");
    problem.residual({1.0}, residual);
    require_near(
        residual.at(0), -9.0, 0.0,
        "ordinary residual remains exact target");

    thermox::ContinuationOptions continuation;
    continuation.initial_step = 0.2;
    const auto solved =
        thermox::solve_continuation(
            problem, {}, continuation);
    require(
        solved.continuation.converged,
        solved.continuation.message);
    require(
        solved.continuation.used_informed_path,
        "continuation reports component-informed path usage");
    require_near(
        solved.x.at(x), 10.0, 1.0e-10,
        "component-informed path reaches target");
}

void test_informed_residual_without_derivative_uses_finite_difference() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x"};
    problem.residual_names = {"target"};
    problem.initial_guess = {1.0};
    problem.variable_scales = {1.0};
    problem.residual_scales = {1.0};
    problem.residual = [](
                           const std::vector<double>& x,
                           std::vector<double>& residual) {
        residual[0] = x[0] - 2.0;
    };
    bool target_jacobian_called = false;
    problem.jacobian =
        [&target_jacobian_called](
            const std::vector<double>&,
            thermox::Matrix& jacobian) {
            target_jacobian_called = true;
            jacobian = {{1.0}};
        };
    problem.continuation_checked_residual = [](
        const std::vector<double>& x,
        const std::vector<double>& anchor,
        double parameter,
        std::vector<double>& residual) {
        residual[0] =
            x[0] -
            (anchor[0] +
             parameter * (2.0 - anchor[0]));
        return thermox::EvaluationStatus::success();
    };

    const auto solved =
        thermox::solve_continuation(problem);
    require(
        solved.continuation.converged,
        solved.continuation.message);
    require_near(
        solved.x[0], 2.0, 1.0e-9,
        "finite-difference informed path reaches target");
    require(
        !target_jacobian_called,
        "target Jacobian is not applied to an informed residual "
        "without matching derivatives");
}

void test_sparse_matrix_conversion_and_scaling() {
    const thermox::Matrix dense{{1.0, 0.0, 2.0}, {0.0, -3.0, 0.0}};
    auto sparse = thermox::sparse_from_dense(dense);
    require(sparse.rows() == 2, "sparse rows from dense");
    require(sparse.columns() == 3, "sparse columns from dense");
    require(sparse.nonzeros() == 3, "sparse nonzero count from dense");
    require_near(sparse.at(0, 0), 1.0, 0.0, "sparse at explicit value");
    require_near(sparse.at(0, 1), 0.0, 0.0, "sparse at implicit zero");
    require(sparse.to_dense() == dense, "sparse converts back to dense");

    sparse.scale_rows({1.0, 3.0});
    require_near(sparse.at(1, 1), -1.0, 0.0, "sparse row scaling");
    sparse.scale_columns({2.0, 4.0, 8.0});
    require_near(sparse.at(0, 0), 2.0, 0.0, "sparse column scaling first column");
    require_near(sparse.at(0, 2), 16.0, 0.0, "sparse column scaling third column");
    require_near(sparse.at(1, 1), -4.0, 0.0, "sparse column scaling second column");
    require(sparse.pattern().nonzeros() == sparse.nonzeros(), "sparse pattern preserves structure");

    auto from_triplets = thermox::sparse_from_triplets(
        2, 3, {{0, 2, 1.5}, {0, 2, 0.5}, {1, 0, 1.0e-15}}, 1.0e-12);
    require(from_triplets.nonzeros() == 1, "sparse triplets accumulate and drop tiny values");
    require_near(from_triplets.at(0, 2), 2.0, 0.0, "sparse triplet accumulation");
    require_near(from_triplets.at(1, 0), 0.0, 0.0, "sparse drop tolerance");
}

void test_sparse_matrix_validates_shape() {
    require_throws_invalid_argument(
        []() { (void)thermox::SparseMatrix(2, 2, {0, 1}, {0}, {1.0}); },
        "sparse matrix rejects bad row offset size");
    require_throws_invalid_argument(
        []() { (void)thermox::sparse_from_dense({{1.0}, {1.0, 2.0}}); },
        "sparse_from_dense rejects ragged matrices");
    require_throws_invalid_argument(
        []() { (void)thermox::sparse_from_triplets(1, 1, {{1, 0, 2.0}}); },
        "sparse_from_triplets rejects out-of-range rows");
}

void test_variable_registry() {
    thermox::VariableRegistry registry;
    registry.add_variable("x", 1.25, 2.0);
    registry.add_residual("r", 3.0);
    require(registry.variable_names().at(0) == "x", "variable registry name");
    require(registry.residual_names().at(0) == "r", "residual registry name");
    require_near(registry.initial_guess().at(0), 1.25, 0.0, "initial guess");
    require_near(registry.variables().at(0).scale, 2.0, 0.0, "variable scale");
    require_near(registry.residuals().at(0).scale, 3.0, 0.0, "residual scale");
    require_throws_invalid_argument([&]() { registry.add_variable("x", 0.0); },
                                    "variable registry rejects duplicate names");
    require_throws_invalid_argument([&]() { registry.add_residual("r"); },
                                    "residual registry rejects duplicate names");
}

void test_newton_solver() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x"};
    problem.residual_names = {"x_squared_minus_2"};
    problem.initial_guess = {1.0};
    problem.residual = [](const std::vector<double>& x, std::vector<double>& residual) {
        residual[0] = x[0] * x[0] - 2.0;
    };

    thermox::SolverOptions options;
    options.residual_tolerance = 1.0e-10;
    const auto result = thermox::solve_newton(problem, options);
    require(result.diagnostics.converged, result.diagnostics.message);
    require(result.diagnostics.function_evaluations > 0, "solver records function evaluations");
    require(result.diagnostics.jacobian_evaluations > 0, "solver records jacobian evaluations");
    require(result.diagnostics.linear_solver_evaluations > 0,
            "solver records linear solver evaluations");
    require_near(result.x[0], std::sqrt(2.0), 1.0e-7, "sqrt(2) solve");
}

void test_equation_builder_propagates_recoverable_evaluations() {
    thermox::EquationSystemBuilder builder;
    const auto x = builder.add_variable("x", 2.0, 1.0, 0.0, 4.0);
    builder.add_checked_equation(
        "domain_limited",
        [x](const std::vector<double>& values, double& residual) {
            if (values.at(x) < 1.0)
                return thermox::EvaluationStatus::recoverable("outside model domain");
            residual = values.at(x) - 1.5;
            return thermox::EvaluationStatus::success();
        });
    const auto problem = builder.build();
    require(static_cast<bool>(problem.checked_residual),
            "checked equation installs checked residual");
    std::vector<double> residual(1, 0.0);
    const auto failed = problem.checked_residual({0.5}, residual);
    require(failed.code == thermox::EvaluationStatusCode::recoverable_failure,
            "recoverable equation status is preserved");
    require_contains(failed.message, "domain_limited",
                     "checked equation failure includes equation name");
    const auto solved = thermox::solve_newton(problem);
    require(solved.diagnostics.converged, solved.diagnostics.message);
    require_near(solved.x.at(0), 1.5, 1e-8, "checked equation solves normally");
}

void test_checked_sparse_equation_preserves_status_and_derivative() {
    thermox::EquationSystemBuilder builder;
    const auto x = builder.add_variable(
        "x", 2.0, 1.0, 0.0, 4.0);
    builder.add_checked_sparse_equation(
        "checked_analytic",
        [x](const std::vector<double>& values,
            double& residual) {
            if (values.at(x) <= 0.0) {
                return thermox::EvaluationStatus::recoverable(
                    "outside positive domain");
            }
            residual = values.at(x) * values.at(x) - 4.0;
            return thermox::EvaluationStatus::success();
        },
        {x},
        [x](const std::vector<double>& values,
            std::vector<thermox::EquationPartial>& partials) {
            partials.push_back({x, 2.0 * values.at(x)});
            return values.at(x) * values.at(x) - 4.0;
        });
    const auto problem = builder.build();
    require(
        static_cast<bool>(problem.checked_residual),
        "checked sparse equation installs status-aware residual");
    require(
        problem.sparse_jacobian_pattern.has_value() &&
            static_cast<bool>(problem.sparse_jacobian_values),
        "checked sparse equation retains fixed analytic pattern");
    std::vector<double> residual(1, 0.0);
    require(
        problem.checked_residual({-1.0}, residual).code ==
            thermox::EvaluationStatusCode::recoverable_failure,
        "checked sparse equation preserves recoverable status");
    std::vector<double> values(1, 0.0);
    problem.sparse_jacobian_values({2.0}, values);
    require_near(
        values.at(0), 4.0, 0.0,
        "checked sparse equation exposes analytic derivative");
}

void test_newton_solver_uses_analytic_jacobian() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x", "y"};
    problem.residual_names = {"sum", "difference"};
    problem.initial_guess = {0.0, 0.0};
    problem.variable_scales = {10.0, 10.0};
    problem.residual = [](const std::vector<double>& x, std::vector<double>& residual) {
        residual[0] = x[0] + x[1] - 4.0;
        residual[1] = x[0] - x[1] - 2.0;
    };
    problem.jacobian = [](const std::vector<double>&, thermox::Matrix& jacobian) {
        jacobian[0][0] = 1.0;
        jacobian[0][1] = 1.0;
        jacobian[1][0] = 1.0;
        jacobian[1][1] = -1.0;
    };

    const auto result = thermox::solve_newton(problem);
    require(result.diagnostics.converged, result.diagnostics.message);
    require_near(result.x[0], 3.0, 1.0e-10, "analytic jacobian x");
    require_near(result.x[1], 1.0, 1.0e-10, "analytic jacobian y");
    require(result.diagnostics.jacobian_evaluations > 0, "analytic jacobian was evaluated");
    require(result.diagnostics.function_evaluations <= result.diagnostics.jacobian_evaluations + 2,
            "analytic jacobian avoids finite-difference residual evaluations");
}

void test_newton_solver_uses_sparse_jacobian_and_solver() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x", "y"};
    problem.residual_names = {"x_equation", "y_equation"};
    problem.initial_guess = {0.0, 0.0};
    problem.residual_scales = {10.0, 1.0};
    problem.residual = [](const std::vector<double>& x, std::vector<double>& residual) {
        residual[0] = 10.0 * (2.0 * x[0] - 4.0);
        residual[1] = 3.0 * x[1] - 9.0;
    };
    problem.sparse_jacobian = [](const std::vector<double>&,
                                 std::vector<thermox::SparseTriplet>& jacobian) {
        jacobian.push_back({0, 0, 20.0});
        jacobian.push_back({1, 1, 3.0});
    };

    bool saw_sparse_system = false;
    thermox::SolverOptions options;
    options.sparse_linear_solver = [&saw_sparse_system](thermox::SparseMatrix a,
                                                         std::vector<double> b) {
        saw_sparse_system = a.rows() == 2 && a.columns() == 2 && a.nonzeros() == 2 &&
                            std::abs(a.at(0, 0) - 2.0) < 1.0e-12 &&
                            std::abs(a.at(1, 1) - 3.0) < 1.0e-12 &&
                            std::abs(b[0] - 4.0) < 1.0e-12 &&
                            std::abs(b[1] - 9.0) < 1.0e-12;
        return thermox::solve_dense_linear_system(a.to_dense(), std::move(b));
    };

    const auto result = thermox::solve_newton(problem, options);
    require(result.diagnostics.converged, result.diagnostics.message);
    require(saw_sparse_system, "sparse linear solver received scaled sparse jacobian");
    require_near(result.x[0], 2.0, 1.0e-12, "sparse jacobian x solve");
    require_near(result.x[1], 3.0, 1.0e-12, "sparse jacobian y solve");
}

void test_newton_solver_converts_dense_jacobian_for_sparse_solver() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x"};
    problem.residual_names = {"linear"};
    problem.initial_guess = {0.0};
    problem.residual = [](const std::vector<double>& x, std::vector<double>& residual) {
        residual[0] = 2.0 * x[0] - 6.0;
    };
    problem.jacobian = [](const std::vector<double>&, thermox::Matrix& jacobian) {
        jacobian[0][0] = 2.0;
    };

    bool saw_sparse_conversion = false;
    thermox::SolverOptions options;
    options.sparse_linear_solver = [&saw_sparse_conversion](thermox::SparseMatrix a,
                                                            std::vector<double> b) {
        saw_sparse_conversion = a.rows() == 1 && a.columns() == 1 && a.nonzeros() == 1 &&
                                std::abs(a.at(0, 0) - 2.0) < 1.0e-12;
        return thermox::LinearSolveResult{true, {b[0] / a.at(0, 0)}, "sparse ok"};
    };

    const auto result = thermox::solve_newton(problem, options);
    require(result.diagnostics.converged, result.diagnostics.message);
    require(saw_sparse_conversion, "dense jacobian converted for sparse solver");
    require_near(result.x[0], 3.0, 1.0e-12, "converted sparse linear solve result");
}

void test_newton_solver_uses_default_sparse_linear_solver() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x"};
    problem.residual_names = {"linear"};
    problem.initial_guess = {0.0};
    problem.residual = [](const std::vector<double>& x, std::vector<double>& residual) {
        residual[0] = 4.0 * x[0] - 8.0;
    };
    problem.sparse_jacobian = [](const std::vector<double>&,
                                 std::vector<thermox::SparseTriplet>& jacobian) {
        jacobian.push_back({0, 0, 4.0});
    };

    const auto result = thermox::solve_newton(problem);
    require(result.diagnostics.converged, result.diagnostics.message);
    require_near(result.x[0], 2.0, 1.0e-12, "default sparse linear solver result");
}

void test_newton_solver_respects_bounds() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x"};
    problem.residual_names = {"target"};
    problem.initial_guess = {0.25};
    problem.lower_bounds = {0.0};
    problem.upper_bounds = {1.0};
    problem.residual = [](const std::vector<double>& x, std::vector<double>& residual) {
        residual[0] = x[0] - 0.75;
    };
    problem.jacobian = [](const std::vector<double>&, thermox::Matrix& jacobian) {
        jacobian[0][0] = 1.0;
    };

    const auto result = thermox::solve_newton(problem);
    require(result.diagnostics.converged, result.diagnostics.message);
    require_near(result.x[0], 0.75, 1.0e-12, "bounded solve stays in range");
}

void test_newton_solver_validates_options_and_problem() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x"};
    problem.residual_names = {"identity"};
    problem.initial_guess = {0.0};
    problem.residual = [](const std::vector<double>& x, std::vector<double>& residual) {
        residual[0] = x[0];
    };

    thermox::SolverOptions invalid_options;
    invalid_options.damping_reduction = 1.0;
    require_throws_invalid_argument([&]() { (void)thermox::solve_newton(problem, invalid_options); },
                                    "invalid damping_reduction should throw");

    thermox::NonlinearProblem invalid_problem = problem;
    invalid_problem.variable_scales = {0.0};
    require_throws_invalid_argument([&]() { (void)thermox::solve_newton(invalid_problem); },
                                    "invalid variable scale should throw");

    invalid_problem = problem;
    invalid_problem.residual_scales = {0.0};
    require_throws_invalid_argument([&]() { (void)thermox::solve_newton(invalid_problem); },
                                    "invalid residual scale should throw");

    invalid_problem = problem;
    invalid_problem.lower_bounds = {1.0};
    invalid_problem.upper_bounds = {2.0};
    require_throws_invalid_argument([&]() { (void)thermox::solve_newton(invalid_problem); },
                                    "initial guess outside bounds should throw");
}

void test_newton_solver_uses_custom_linear_solver() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x"};
    problem.residual_names = {"linear"};
    problem.initial_guess = {0.0};
    problem.residual = [](const std::vector<double>& x, std::vector<double>& residual) {
        residual[0] = 2.0 * x[0] - 4.0;
    };
    problem.jacobian = [](const std::vector<double>&, thermox::Matrix& jacobian) {
        jacobian[0][0] = 2.0;
    };

    int calls = 0;
    thermox::SolverOptions options;
    options.linear_solver = [&calls](thermox::Matrix a, std::vector<double> b) {
        ++calls;
        if (a.size() != 1 || a[0].size() != 1 || b.size() != 1) {
            return thermox::LinearSolveResult{false, {}, "unexpected linear system shape"};
        }
        return thermox::LinearSolveResult{true, {b[0] / a[0][0]}, "custom ok"};
    };

    const auto result = thermox::solve_newton(problem, options);
    require(result.diagnostics.converged, result.diagnostics.message);
    require_near(result.x[0], 2.0, 1.0e-12, "custom linear solver result");
    require(calls > 0, "custom linear solver was called");
    require(result.diagnostics.linear_solver_evaluations == calls,
            "linear solver diagnostics match custom calls");
}

void test_newton_solver_reports_linear_solver_failure() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x"};
    problem.residual_names = {"linear"};
    problem.initial_guess = {0.0};
    problem.residual = [](const std::vector<double>& x, std::vector<double>& residual) {
        residual[0] = x[0] - 1.0;
    };
    problem.jacobian = [](const std::vector<double>&, thermox::Matrix& jacobian) {
        jacobian[0][0] = 1.0;
    };

    thermox::SolverOptions options;
    options.linear_solver = [](thermox::Matrix, std::vector<double>) {
        return thermox::LinearSolveResult{false, {}, "backend unavailable"};
    };

    const auto result = thermox::solve_newton(problem, options);
    require(!result.diagnostics.converged, "linear solver failure should not converge");
    require(result.diagnostics.linear_solver_evaluations == 1,
            "linear solver failure increments diagnostics");
    require_contains(result.diagnostics.message, "linear solve failed: backend unavailable",
                     "linear solver failure message");
}

void test_newton_solver_rejects_invalid_linear_solver_step() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x"};
    problem.residual_names = {"linear"};
    problem.initial_guess = {0.0};
    problem.residual = [](const std::vector<double>& x, std::vector<double>& residual) {
        residual[0] = x[0] - 1.0;
    };
    problem.jacobian = [](const std::vector<double>&, thermox::Matrix& jacobian) {
        jacobian[0][0] = 1.0;
    };

    thermox::SolverOptions options;
    options.linear_solver = [](thermox::Matrix, std::vector<double>) {
        return thermox::LinearSolveResult{true, {1.0, 2.0}, "wrong size"};
    };

    const auto result = thermox::solve_newton(problem, options);
    require(!result.diagnostics.converged, "invalid linear solver step should not converge");
    require_contains(result.diagnostics.message, "linear solver returned step with wrong size",
                     "invalid linear solver step message");
}

void test_line_search_failure_names_dominant_residual() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x", "y"};
    problem.residual_names = {"minor_balance", "dominant_balance"};
    problem.initial_guess = {0.0, 0.0};
    problem.residual = [](
        const std::vector<double>&,
        std::vector<double>& residual) {
        residual[0] = 1.0;
        residual[1] = 3.0;
    };
    problem.jacobian = [](
        const std::vector<double>&,
        thermox::Matrix& jacobian) {
        jacobian[0][0] = 1.0;
        jacobian[1][1] = 1.0;
    };
    thermox::SolverOptions options;
    options.max_line_search_steps = 3;
    const auto result = thermox::solve_newton(problem, options);
    require(!result.diagnostics.converged,
            "constant residual must fail line search");
    require_contains(
        result.diagnostics.message, "dominant_balance",
        "line-search diagnostic names dominant scaled residual");
}

void test_newton_solver_uses_residual_scales_for_convergence() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x"};
    problem.residual_names = {"large_unit_residual"};
    problem.initial_guess = {0.0};
    problem.residual_scales = {1000.0};
    problem.residual = [](const std::vector<double>& x, std::vector<double>& residual) {
        residual[0] = x[0] - 100.0;
    };

    thermox::SolverOptions options;
    options.max_iterations = 0;
    options.residual_tolerance = 0.2;

    const auto result = thermox::solve_newton(problem, options);
    require(result.diagnostics.converged, result.diagnostics.message);
    require(result.diagnostics.iterations == 0, "scaled residual converges at initial guess");
    require_near(result.diagnostics.final_residual_norm, 0.1, 1.0e-12,
                 "diagnostics report scaled residual norm");
    require_near(result.x[0], 0.0, 0.0, "scaled convergence leaves accepted initial guess");
}

void test_newton_solver_scales_linear_system_rows() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x"};
    problem.residual_names = {"scaled_linear"};
    problem.initial_guess = {0.0};
    problem.residual_scales = {1.0e6};
    problem.residual = [](const std::vector<double>& x, std::vector<double>& residual) {
        residual[0] = 1.0e6 * (x[0] - 2.0);
    };
    problem.jacobian = [](const std::vector<double>&, thermox::Matrix& jacobian) {
        jacobian[0][0] = 1.0e6;
    };

    bool saw_scaled_system = false;
    thermox::SolverOptions options;
    options.linear_solver = [&saw_scaled_system](thermox::Matrix a, std::vector<double> b) {
        saw_scaled_system = a.size() == 1 && a[0].size() == 1 && b.size() == 1 &&
                            std::abs(a[0][0] - 1.0) < 1.0e-12 &&
                            std::abs(b[0] - 2.0) < 1.0e-12;
        return thermox::LinearSolveResult{true, {b[0] / a[0][0]}, "scaled ok"};
    };

    const auto result = thermox::solve_newton(problem, options);
    require(result.diagnostics.converged, result.diagnostics.message);
    require(saw_scaled_system, "linear solver received row-scaled jacobian and residual");
    require_near(result.x[0], 2.0, 1.0e-12, "scaled linear solve result");
}

void test_newton_solver_scales_columns_and_returns_physical_step() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"pressure"};
    problem.residual_names = {"pressure_target"};
    problem.initial_guess = {0.0};
    problem.variable_scales = {1.0e6};
    problem.residual_scales = {1.0e6};
    problem.residual = [](const std::vector<double>& x, std::vector<double>& residual) {
        residual[0] = x[0] - 2.0e6;
    };
    problem.jacobian = [](const std::vector<double>&, thermox::Matrix& jacobian) {
        jacobian[0][0] = 1.0;
    };

    bool saw_dimensionless_system = false;
    thermox::SolverOptions options;
    options.linear_solver =
        [&saw_dimensionless_system](thermox::Matrix a, std::vector<double> b) {
            saw_dimensionless_system =
                std::abs(a[0][0] - 1.0) < 1.0e-12 &&
                std::abs(b[0] - 2.0) < 1.0e-12;
            return thermox::LinearSolveResult{true, {b[0] / a[0][0]}, "ok"};
        };
    const auto result = thermox::solve_newton(problem, options);
    require(result.diagnostics.converged, result.diagnostics.message);
    require(saw_dimensionless_system, "Newton linear system uses row and column scaling");
    require_near(result.x[0], 2.0e6, 1.0e-6, "scaled Newton step returns physical state");
}

void test_newton_solver_recovers_from_invalid_trial_state() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"positive_x"};
    problem.residual_names = {"log_x"};
    problem.initial_guess = {10.0};
    problem.checked_residual = [](const std::vector<double>& x,
                                  std::vector<double>& residual) {
        if (x[0] <= 0.0) {
            return thermox::EvaluationStatus::recoverable("log domain");
        }
        residual[0] = std::log(x[0]);
        return thermox::EvaluationStatus::success();
    };
    problem.jacobian = [](const std::vector<double>& x, thermox::Matrix& jacobian) {
        jacobian[0][0] = 1.0 / x[0];
    };

    const auto result = thermox::solve_newton(problem);
    require(result.diagnostics.converged, result.diagnostics.message);
    require_near(result.x[0], 1.0, 1.0e-8, "line search recovers from invalid trial state");
}

void test_mixed_derivative_equation_system_stays_sparse() {
    thermox::EquationSystemBuilder system;
    const auto x = system.add_variable("x", 0.0);
    const auto y = system.add_variable("y", 0.0);
    system.add_linear_equation("x_target", {{x, 1.0}}, 2.0);
    system.add_equation("y_target", [y](const std::vector<double>& values) {
        return values[y] - 3.0;
    });

    const auto problem = system.build();
    require(static_cast<bool>(problem.partial_sparse_jacobian),
            "mixed derivative system exposes partial sparse assembly");
    require(problem.analytic_jacobian_rows == std::vector<bool>({true, false}),
            "mixed derivative system records analytic rows");
    const auto result = thermox::solve_newton(problem);
    require(result.diagnostics.converged, result.diagnostics.message);
    require_near(result.x[x], 2.0, 1.0e-8, "mixed derivative solve x");
    require_near(result.x[y], 3.0, 1.0e-8, "mixed derivative solve y");
}

void test_jacobian_verification_checks_only_provided_rows() {
    thermox::EquationSystemBuilder system;
    const auto x = system.add_variable("x", 2.0);
    const auto y = system.add_variable("y", 3.0);
    system.add_sparse_equation(
        "x_squared",
        [x](const std::vector<double>& values,
            std::vector<thermox::EquationPartial>& partials) {
            partials.push_back({x, 2.0 * values[x]});
            return values[x] * values[x] - 4.0;
        });
    system.add_equation(
        "y_target",
        [y](const std::vector<double>& values) {
            return values[y] - 3.0;
        });

    const auto report =
        thermox::verify_problem_jacobian(system.build());
    require(report.analytic_derivatives_available,
            "mixed problem exposes derivatives for verification");
    require(report.passed,
            "correct provided derivative passes verification");
    require(report.compared_rows == 1,
            "finite-difference-only rows are not attributed to provider");
    require(report.compared_entries == 2,
            "provided row is checked across every variable");
}

void test_jacobian_verification_reports_bad_derivative() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x"};
    problem.residual_names = {"x_squared"};
    problem.initial_guess = {2.0};
    problem.residual =
        [](const std::vector<double>& x,
           std::vector<double>& residual) {
            residual[0] = x[0] * x[0] - 4.0;
        };
    problem.jacobian =
        [](const std::vector<double>&,
           thermox::Matrix& jacobian) {
            jacobian[0][0] = 3.0;
        };

    const auto report =
        thermox::verify_problem_jacobian(problem);
    require(!report.passed && report.mismatch_count == 1,
            "incorrect derivative fails verification");
    require(report.mismatches.at(0).residual_name ==
                "x_squared" &&
            report.mismatches.at(0).variable_name == "x",
            "mismatch identifies its equation and variable");
}

void test_finite_difference_jacobian_uses_central_difference() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"x"};
    problem.residual_names = {"cubic"};
    problem.initial_guess = {1.0};
    problem.variable_scales = {1.0};
    problem.residual =
        [](const std::vector<double>& x,
           std::vector<double>& residual) {
            residual[0] = x[0] * x[0] * x[0];
        };
    problem.jacobian =
        [](const std::vector<double>& x,
           thermox::Matrix& jacobian) {
            jacobian[0][0] = 3.0 * x[0] * x[0];
        };

    thermox::JacobianVerificationOptions options;
    options.finite_difference_epsilon = 1.0e-3;
    options.absolute_tolerance = 2.0e-6;
    options.relative_tolerance = 0.0;
    const auto report =
        thermox::verify_problem_jacobian(problem, {}, options);
    require(
        report.passed,
        "interior finite differences use second-order central accuracy");
    require_near(
        report.maximum_absolute_error, 1.0e-6, 1.0e-9,
        "central cubic derivative truncation error");
}

void test_finite_difference_jacobian_recovers_one_sided() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"nonnegative_x"};
    problem.residual_names = {"square"};
    problem.initial_guess = {0.0};
    problem.variable_scales = {1.0};
    problem.lower_bounds = {-1.0};
    problem.upper_bounds = {1.0};
    problem.checked_residual =
        [](const std::vector<double>& x,
           std::vector<double>& residual) {
            if (x[0] < 0.0) {
                return thermox::EvaluationStatus::recoverable(
                    "negative branch is outside the physical domain");
            }
            residual[0] = x[0] * x[0];
            return thermox::EvaluationStatus::success();
        };
    problem.jacobian =
        [](const std::vector<double>& x,
           thermox::Matrix& jacobian) {
            jacobian[0][0] = 2.0 * x[0];
        };

    thermox::JacobianVerificationOptions options;
    options.finite_difference_epsilon = 1.0e-4;
    options.absolute_tolerance = 1.1e-4;
    options.relative_tolerance = 0.0;
    const auto report =
        thermox::verify_problem_jacobian(problem, {}, options);
    require(
        report.passed,
        "recoverable central perturbation failure falls back to the valid side");
}

void test_fixed_sparse_pattern_and_structure_analysis() {
    thermox::EquationSystemBuilder system;
    const auto x = system.add_variable("x", 0.0);
    const auto y = system.add_variable("y", 0.0);
    system.add_linear_equation("sum", {{x, 1.0}, {y, 1.0}}, 5.0);
    system.add_linear_equation("difference", {{x, 1.0}, {y, -1.0}}, 1.0);
    const auto problem = system.build();
    require(problem.sparse_jacobian_pattern.has_value(), "linear system has fixed CSR pattern");
    require(static_cast<bool>(problem.sparse_jacobian_values),
            "linear system updates fixed CSR values");
    const auto structure = thermox::analyze_problem_structure(problem);
    require(structure.valid_for_newton(), "fixed pattern is structurally nonsingular");
    require(structure.structurally_nonsingular, "structural matching covers all rows and columns");
}

void test_structural_analysis_localizes_singular_regions() {
    const auto structure = thermox::analyze_incidence_structure(
        {"left", "right"},
        {"left_target_a", "left_target_b"},
        {{0}, {0}});
    require(
        structure.square && !structure.structurally_nonsingular,
        "singular square incidence is rejected");
    require(
        structure.unmatched_variable_names ==
            std::vector<std::string>{"right"} &&
            structure.unmatched_residual_names.size() == 1,
        "maximum matching reports unmatched candidates");

    const auto under = std::find_if(
        structure.structural_regions.begin(),
        structure.structural_regions.end(),
        [](const auto& region) {
            return region.kind ==
                thermox::StructuralRegionKind::underdetermined;
        });
    require(
        under != structure.structural_regions.end() &&
            under->variable_names ==
                std::vector<std::string>{"right"} &&
            under->residual_names.empty(),
        "DM analysis localizes the unconstrained variable");

    const auto over = std::find_if(
        structure.structural_regions.begin(),
        structure.structural_regions.end(),
        [](const auto& region) {
            return region.kind ==
                thermox::StructuralRegionKind::overdetermined;
        });
    require(
        over != structure.structural_regions.end() &&
            over->variable_names ==
                std::vector<std::string>{"left"} &&
            over->residual_names.size() == 2,
        "DM analysis localizes dependent equations and their variable");
}

void test_fixed_bound_finite_difference_fails_cleanly() {
    thermox::NonlinearProblem problem;
    problem.variable_names = {"fixed"};
    problem.residual_names = {"unsatisfied"};
    problem.initial_guess = {1.0};
    problem.lower_bounds = {1.0};
    problem.upper_bounds = {1.0};
    problem.residual = [](const std::vector<double>& x, std::vector<double>& residual) {
        residual[0] = x[0] - 2.0;
    };
    const auto result = thermox::solve_newton(problem);
    require(!result.diagnostics.converged, "unsatisfied fixed state must not converge");
    require_contains(result.diagnostics.message, "linear solve failed",
                     "fixed-bound failure is returned through diagnostics");
}

void test_equation_system_builder() {
    thermox::EquationSystemBuilder system;
    const auto x = system.add_variable("x", 1.0, 10.0);
    const auto y = system.add_variable("y", 1.0, 10.0);

    system.add_equation("sum_to_five", [x, y](const std::vector<double>& values) {
        return values.at(x) + values.at(y) - 5.0;
    }, 5.0);
    system.add_equation("difference_one", [x, y](const std::vector<double>& values) {
        return values.at(x) - values.at(y) - 1.0;
    }, 2.0);

    const auto problem = system.build();
    require(problem.variable_names == std::vector<std::string>({"x", "y"}),
            "equation system variable names");
    require(problem.residual_names == std::vector<std::string>({"sum_to_five", "difference_one"}),
            "equation system residual names");
    require(problem.variable_scales == std::vector<double>({10.0, 10.0}),
            "equation system carries variable scales");
    require(problem.residual_scales == std::vector<double>({5.0, 2.0}),
            "equation system carries residual scales");
    std::vector<double> residual(2, 0.0);
    problem.residual(std::vector<double>({1.0, 1.0}), residual);
    require_near(residual.at(0), -3.0, 0.0, "equation residual callback returns raw first residual");
    require_near(residual.at(1), -1.0, 0.0, "equation residual callback returns raw second residual");

    const auto result = thermox::solve_newton(problem);
    require(result.diagnostics.converged, result.diagnostics.message);
    require_near(result.x.at(x), 3.0, 1.0e-7, "equation system x");
    require_near(result.x.at(y), 2.0, 1.0e-7, "equation system y");
}


void test_compiled_sparse_equation_system_builder() {
    thermox::EquationSystemBuilder system;
    const auto x = system.add_variable("x", 0.0, 10.0, -1.0, 10.0);
    const auto y = system.add_variable("y", 0.0, 10.0, -1.0, 10.0);

    system.add_linear_equation("sum_to_five", {{x, 1.0}, {y, 1.0}}, 5.0, 5.0);
    system.add_sparse_equation(
        "difference_one",
        [x, y](const std::vector<double>& values,
               std::vector<thermox::EquationPartial>& jacobian_row) {
            jacobian_row.push_back({x, 1.0});
            jacobian_row.push_back({y, -1.0});
            return values.at(x) - values.at(y) - 1.0;
        },
        2.0);

    const auto problem = system.build();
    require(problem.variable_names == std::vector<std::string>({"x", "y"}),
            "compiled system variable names");
    require(problem.residual_names == std::vector<std::string>({"sum_to_five", "difference_one"}),
            "compiled system residual names");
    require(problem.variable_scales == std::vector<double>({10.0, 10.0}),
            "compiled system variable scales");
    require(problem.residual_scales == std::vector<double>({5.0, 2.0}),
            "compiled system residual scales");
    require(problem.lower_bounds == std::vector<double>({-1.0, -1.0}),
            "compiled system lower bounds");
    require(problem.upper_bounds == std::vector<double>({10.0, 10.0}),
            "compiled system upper bounds");
    require(static_cast<bool>(problem.sparse_jacobian), "compiled system exposes sparse jacobian");

    std::vector<thermox::SparseTriplet> triplets;
    problem.sparse_jacobian({1.0, 1.0}, triplets);
    const auto jacobian = thermox::sparse_from_triplets(2, 2, triplets);
    require_near(jacobian.at(0, 0), 1.0, 0.0, "compiled sparse jacobian sum dx");
    require_near(jacobian.at(0, 1), 1.0, 0.0, "compiled sparse jacobian sum dy");
    require_near(jacobian.at(1, 0), 1.0, 0.0, "compiled sparse jacobian diff dx");
    require_near(jacobian.at(1, 1), -1.0, 0.0, "compiled sparse jacobian diff dy");

    bool saw_sparse_system = false;
    thermox::SolverOptions options;
    options.sparse_linear_solver = [&saw_sparse_system](thermox::SparseMatrix a,
                                                         std::vector<double> b) {
        saw_sparse_system = a.rows() == 2 && a.columns() == 2 && a.nonzeros() == 4 &&
                            std::abs(a.at(0, 0) - 2.0) < 1.0e-12 &&
                            std::abs(a.at(0, 1) - 2.0) < 1.0e-12 &&
                            std::abs(a.at(1, 0) - 5.0) < 1.0e-12 &&
                            std::abs(a.at(1, 1) + 5.0) < 1.0e-12 &&
                            std::abs(b[0] - 1.0) < 1.0e-12 &&
                            std::abs(b[1] - 0.5) < 1.0e-12;
        return thermox::solve_dense_linear_system(a.to_dense(), std::move(b));
    };

    const auto result = thermox::solve_newton(problem, options);
    require(result.diagnostics.converged, result.diagnostics.message);
    require(saw_sparse_system, "compiled sparse system reached sparse linear solver");
    require_near(result.x.at(x), 3.0, 1.0e-10, "compiled system x");
    require_near(result.x.at(y), 2.0, 1.0e-10, "compiled system y");
}

void test_compiled_sparse_equation_duplicates_accumulate() {
    thermox::EquationSystemBuilder system;
    const auto x = system.add_variable("x", 0.0, 1.0);

    system.add_linear_equation("duplicate_terms", {{x, 1.0}, {x, 2.0}}, 6.0);

    const auto problem = system.build();
    require(problem.sparse_jacobian_pattern.has_value(),
            "duplicate system exposes fixed sparse pattern");

    std::vector<double> residual(1, 0.0);
    problem.residual({0.0}, residual);
    require_near(residual.at(0), -6.0, 0.0, "duplicate linear residual");

    std::vector<double> values(problem.sparse_jacobian_pattern->nonzeros(), 0.0);
    problem.sparse_jacobian_values({0.0}, values);
    const auto jacobian =
        thermox::SparseMatrix(*problem.sparse_jacobian_pattern, std::move(values));
    require(jacobian.nonzeros() == 1, "duplicate sparse terms accumulate into one nonzero");
    require_near(jacobian.at(0, 0), 3.0, 0.0, "duplicate sparse derivative accumulation");

    const auto result = thermox::solve_newton(problem);
    require(result.diagnostics.converged, result.diagnostics.message);
    require_near(result.x.at(x), 2.0, 1.0e-12, "duplicate linear equation solve");
}

void test_linear_equation_relation_classification() {
    thermox::EquationSystemBuilder system;
    const auto x = system.add_variable("x", 0.0);
    const auto y = system.add_variable("y", 0.0);
    const auto z = system.add_variable("z", 0.0);

    require(
        system.classify_linear_equation(
            {{x, 1.0}, {y, -1.0}}, 0.0) ==
            thermox::LinearEquationRelation::independent,
        "first linear relation should be independent");
    system.add_linear_equation(
        "x_equals_y", {{x, 1.0}, {y, -1.0}}, 0.0);
    system.add_linear_equation(
        "y_equals_z", {{y, 1.0}, {z, -1.0}}, 0.0);

    require(
        system.classify_linear_equation(
            {{x, 2.0e12}, {z, -2.0e12}}, 0.0) ==
            thermox::LinearEquationRelation::redundant,
        "scaled loop closure should be redundant");
    require(
        system.classify_linear_equation(
            {{x, 1.0}, {z, -1.0}}, 1.0) ==
            thermox::LinearEquationRelation::inconsistent,
        "conflicting loop closure should be inconsistent");
    require(
        system.classify_linear_equation({{z, 1.0}}, 3.0) ==
            thermox::LinearEquationRelation::independent,
        "new absolute specification should remain independent");
}

void test_linear_initialization_propagates_from_explicit_anchor() {
    thermox::EquationSystemBuilder system;
    const auto source =
        system.add_variable("source", 5.0, 1.0);
    const auto connected =
        system.add_variable("connected", 0.0, 1.0);
    const auto transformed =
        system.add_variable("transformed", 0.0, 1.0);
    require_throws_invalid_argument(
        [&]() {
            system.mark_initialization_anchor(3);
        },
        "initialization anchors reject unknown variables");
    require_throws_invalid_argument(
        [&]() {
            system.add_initialization_relation(
                {{source, 1.0}, {source, -1.0}}, 0.0);
        },
        "initialization relations reject zero rows");
    system.mark_initialization_anchor(source);

    // Deliberately declare these in reverse propagation order.
    system.add_linear_equation(
        "transform",
        {{transformed, 1.0}, {connected, -2.0}},
        0.0);
    system.add_initialization_relation(
        {{transformed, 1.0}, {connected, -2.0}},
        0.0);
    system.add_linear_equation(
        "connection",
        {{connected, 1.0}, {source, -1.0}},
        0.0);
    system.add_initialization_relation(
        {{connected, 1.0}, {source, -1.0}},
        0.0);
    system.add_linear_equation(
        "target_boundary", {{source, 1.0}}, 10.0);

    const auto problem = system.build();
    require_near(
        problem.initial_guess.at(source), 5.0, 0.0,
        "explicit initialization anchor is preserved");
    require_near(
        problem.initial_guess.at(connected), 5.0, 0.0,
        "connection seed propagates from explicit anchor");
    require_near(
        problem.initial_guess.at(transformed), 10.0, 0.0,
        "linear component seed propagates over repeated sweeps");
}

}  // namespace

int main() {
    try {
        test_dense_linear_solver();
        test_sparse_linear_solver();
        test_reusable_sparse_factorization();
        test_newton_reuses_sparse_symbolic_factorization();
        test_continuation_recovers_difficult_initial_guess();
        test_continuation_falls_back_to_solvable_target();
        test_equation_builder_exposes_component_continuation_path();
        test_informed_residual_without_derivative_uses_finite_difference();
        test_sparse_matrix_conversion_and_scaling();
        test_sparse_matrix_validates_shape();
        test_variable_registry();
        test_newton_solver();
        test_equation_builder_propagates_recoverable_evaluations();
        test_checked_sparse_equation_preserves_status_and_derivative();
        test_newton_solver_uses_analytic_jacobian();
        test_newton_solver_uses_sparse_jacobian_and_solver();
        test_newton_solver_converts_dense_jacobian_for_sparse_solver();
        test_newton_solver_uses_default_sparse_linear_solver();
        test_newton_solver_respects_bounds();
        test_newton_solver_validates_options_and_problem();
        test_newton_solver_uses_custom_linear_solver();
        test_newton_solver_reports_linear_solver_failure();
        test_newton_solver_rejects_invalid_linear_solver_step();
        test_line_search_failure_names_dominant_residual();
        test_newton_solver_uses_residual_scales_for_convergence();
        test_newton_solver_scales_linear_system_rows();
        test_newton_solver_scales_columns_and_returns_physical_step();
        test_newton_solver_recovers_from_invalid_trial_state();
        test_mixed_derivative_equation_system_stays_sparse();
        test_jacobian_verification_checks_only_provided_rows();
        test_jacobian_verification_reports_bad_derivative();
        test_finite_difference_jacobian_uses_central_difference();
        test_finite_difference_jacobian_recovers_one_sided();
        test_fixed_sparse_pattern_and_structure_analysis();
        test_structural_analysis_localizes_singular_regions();
        test_fixed_bound_finite_difference_fails_cleanly();
        test_equation_system_builder();
        test_compiled_sparse_equation_system_builder();
        test_compiled_sparse_equation_duplicates_accumulate();
        test_linear_equation_relation_classification();
        test_linear_initialization_propagates_from_explicit_anchor();
    } catch (const std::exception& ex) {
        std::cerr << "test failure: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "thermox_core_tests passed\n";
    return 0;
}
