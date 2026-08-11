#include "thermox/dense_linear_solver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace thermox {

bool DenseLinearFactorization::factorize(Matrix matrix) {
    valid_ = false;
    lu_.clear();
    pivot_rows_.clear();
    message_.clear();
    const std::size_t n = matrix.size();
    for (const auto& row : matrix) {
        if (row.size() != n) {
            message_ = "matrix must be square";
            return false;
        }
    }
    double matrix_norm = 0.0;
    for (const auto& row : matrix) {
        for (const double value : row) {
            if (!std::isfinite(value)) {
                message_ = "matrix contains a non-finite value";
                return false;
            }
            matrix_norm = std::max(matrix_norm, std::abs(value));
        }
    }
    if (n == 0) {
        lu_.clear();
        pivot_rows_.clear();
        valid_ = true;
        message_ = "ok";
        return true;
    }
    if (matrix_norm == 0.0) {
        message_ = "matrix is zero";
        return false;
    }
    const double pivot_tolerance =
        64.0 * std::numeric_limits<double>::epsilon() * static_cast<double>(n) * matrix_norm;

    pivot_rows_.assign(n, 0U);
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        double pivot_abs = std::abs(matrix[col][col]);
        for (std::size_t row = col + 1; row < n; ++row) {
            const double candidate = std::abs(matrix[row][col]);
            if (candidate > pivot_abs) {
                pivot = row;
                pivot_abs = candidate;
            }
        }

        if (pivot_abs <= pivot_tolerance) {
            std::ostringstream oss;
            oss << "singular matrix near column " << col;
            message_ = oss.str();
            return false;
        }

        pivot_rows_[col] = pivot;
        if (pivot != col) {
            std::swap(matrix[pivot], matrix[col]);
        }

        const double diag = matrix[col][col];
        for (std::size_t row = col + 1; row < n; ++row) {
            const double factor = matrix[row][col] / diag;
            matrix[row][col] = factor;
            for (std::size_t k = col + 1; k < n; ++k) {
                matrix[row][k] -= factor * matrix[col][k];
            }
        }
    }

    lu_ = std::move(matrix);
    valid_ = true;
    message_ = "ok";
    return true;
}

LinearSolveResult DenseLinearFactorization::solve(
    std::vector<double> rhs) const {
    if (!valid_) return {false, {}, message_};
    const std::size_t n = lu_.size();
    if (rhs.size() != n) {
        return {false, {}, "RHS size does not match factorization"};
    }
    for (const double value : rhs) {
        if (!std::isfinite(value)) {
            return {false, {}, "RHS contains a non-finite value"};
        }
    }
    for (std::size_t col = 0; col < n; ++col) {
        if (pivot_rows_[col] != col) {
            std::swap(rhs[pivot_rows_[col]], rhs[col]);
        }
    }
    for (std::size_t row = 0; row < n; ++row) {
        for (std::size_t column = 0; column < row; ++column) {
            rhs[row] -= lu_[row][column] * rhs[column];
        }
    }

    std::vector<double> x(n, 0.0);
    for (std::size_t i = n; i-- > 0;) {
        double sum = rhs[i];
        for (std::size_t j = i + 1; j < n; ++j) {
            sum -= lu_[i][j] * x[j];
        }
        x[i] = sum / lu_[i][i];
        if (!std::isfinite(x[i])) {
            return {false, {}, "linear solver produced a non-finite value"};
        }
    }

    return {true, x, "ok"};
}

std::vector<LinearSolveResult>
DenseLinearFactorization::solve_multiple(
    const Matrix& right_hand_sides) const {
    std::vector<LinearSolveResult> results;
    results.reserve(right_hand_sides.size());
    for (const auto& rhs : right_hand_sides) {
        results.push_back(solve(rhs));
    }
    return results;
}

LinearSolveResult solve_dense_linear_system(
    Matrix matrix, std::vector<double> rhs) {
    if (matrix.size() != rhs.size()) {
        return {false, {}, "matrix row count does not match RHS size"};
    }
    DenseLinearFactorization factorization;
    if (!factorization.factorize(std::move(matrix))) {
        return {false, {}, factorization.message()};
    }
    return factorization.solve(std::move(rhs));
}

}  // namespace thermox
