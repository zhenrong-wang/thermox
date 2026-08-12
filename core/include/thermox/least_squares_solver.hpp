#pragma once

#include "thermox/linear_solver.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace thermox {

struct LeastSquaresResult {
    bool success{false};
    std::vector<double> x;
    // Unscaled local covariance kernel (A^T A)^-1. The caller owns any
    // statistical variance scaling required by its measurement model.
    Matrix covariance;
    std::size_t rank{0};
    std::string message;
    FactorizationQuality factorization_quality;
};

// Solves min ||A x - b||_2 for a finite rectangular system with rows >=
// columns using column-pivoted Householder QR. Rank and covariance are
// obtained from the rectangular factorization; A^T A is never formed.
[[nodiscard]] LeastSquaresResult solve_dense_least_squares(
    Matrix a,
    std::vector<double> b);

}  // namespace thermox
