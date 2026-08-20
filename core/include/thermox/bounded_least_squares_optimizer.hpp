#pragma once

#include "thermox/linear_solver.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace thermox {

struct BoundedLeastSquaresSettings {
    int max_iterations{20};
    double finite_difference_fraction{1.0e-4};
    double initial_trust_region_radius{0.25};
    double minimum_trust_region_radius{1.0e-6};
    double maximum_trust_region_radius{1.0};
    double acceptance_ratio{1.0e-4};
    double gradient_tolerance{1.0e-6};
    double step_tolerance{1.0e-6};
    double objective_relative_tolerance{1.0e-8};
};

struct BoundedResidualEvaluation {
    bool success{false};
    std::vector<double> residuals;
    std::string message;
};

// The optional reference point is the accepted point from which the
// optimizer is evaluating a perturbation or trial. Callers may use it to
// provide continuation or warm starts without exposing domain state here.
using BoundedResidualFunction = std::function<BoundedResidualEvaluation(
    const std::vector<double>& candidate,
    const std::vector<double>* reference)>;

struct BoundedLeastSquaresDiagnostics {
    bool converged{false};
    int iterations{0};
    int residual_evaluations{0};
    int sensitivity_evaluations{0};
    int accepted_steps{0};
    int rejected_steps{0};
    std::size_t sensitivity_rank{0};
    double final_projected_gradient_norm{0.0};
    double final_trust_region_radius{0.0};
    FactorizationQuality factorization_quality;
    std::string message;
};

struct BoundedLeastSquaresResult {
    bool success{false};
    std::vector<double> x;
    std::vector<double> residuals;
    double initial_objective{0.0};
    double final_objective{0.0};
    BoundedLeastSquaresDiagnostics diagnostics;
    std::string message;
};

// Minimizes the sum of squared residuals inside finite box bounds. Variables
// are scaled by their bound ranges. A rank-revealing Gauss-Newton step is
// globalized by a trust region; model evaluations remain sequential.
[[nodiscard]] BoundedLeastSquaresResult
solve_bounded_nonlinear_least_squares(
    const BoundedResidualFunction& evaluate,
    std::vector<double> initial,
    std::vector<double> lower_bounds,
    std::vector<double> upper_bounds,
    const BoundedLeastSquaresSettings& settings = {});

}  // namespace thermox
