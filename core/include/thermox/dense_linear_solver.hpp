#pragma once

#include "thermox/linear_solver.hpp"

#include <string>
#include <vector>

namespace thermox {

// Reusable partial-pivoting LU factorization for multiple right-hand sides
// sharing one dense matrix.
class DenseLinearFactorization {
public:
    bool factorize(Matrix matrix);

    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] std::size_t size() const { return lu_.size(); }
    [[nodiscard]] const std::string& message() const {
        return message_;
    }
    [[nodiscard]] const FactorizationQuality& quality() const {
        return quality_;
    }
    [[nodiscard]] LinearSolveResult solve(
        std::vector<double> rhs) const;
    [[nodiscard]] std::vector<LinearSolveResult> solve_multiple(
        const Matrix& right_hand_sides) const;

private:
    Matrix lu_;
    std::vector<std::size_t> pivot_rows_;
    bool valid_{false};
    std::string message_{"not factorized"};
    FactorizationQuality quality_;
};

LinearSolveResult solve_dense_linear_system(Matrix a, std::vector<double> b);

}  // namespace thermox
