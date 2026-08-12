#pragma once

#include "thermox/linear_solver.hpp"

#include <string>
#include <vector>

namespace thermox {

// Reusable lower-triangular Cholesky factorization A = L L^T for finite,
// symmetric positive-definite dense matrices. Solving L y = r whitens a
// residual whose correlation or covariance matrix is A.
class DenseCholeskyFactorization {
public:
    bool factorize(Matrix matrix);

    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] std::size_t size() const { return lower_.size(); }
    [[nodiscard]] const std::string& message() const {
        return message_;
    }
    [[nodiscard]] const FactorizationQuality& quality() const {
        return quality_;
    }
    [[nodiscard]] LinearSolveResult solve_lower(
        std::vector<double> rhs) const;
    [[nodiscard]] Matrix whiten_rows(const Matrix& values) const;

private:
    Matrix lower_;
    bool valid_{false};
    std::string message_{"not factorized"};
    FactorizationQuality quality_;
};

}  // namespace thermox
