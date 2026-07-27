#include "thermox/dense_linear_solver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace thermox {

LinearSolveResult solve_dense_linear_system(Matrix a, std::vector<double> b) {
    const std::size_t n = b.size();
    if (a.size() != n) {
        return {false, {}, "matrix row count does not match RHS size"};
    }
    for (const auto& row : a) {
        if (row.size() != n) {
            return {false, {}, "matrix must be square"};
        }
    }
    double matrix_norm = 0.0;
    for (const auto& row : a) {
        for (const double value : row) {
            if (!std::isfinite(value)) {
                return {false, {}, "matrix contains a non-finite value"};
            }
            matrix_norm = std::max(matrix_norm, std::abs(value));
        }
    }
    for (const double value : b) {
        if (!std::isfinite(value)) {
            return {false, {}, "RHS contains a non-finite value"};
        }
    }
    if (n == 0) {
        return {true, {}, "ok"};
    }
    if (matrix_norm == 0.0) {
        return {false, {}, "matrix is zero"};
    }
    const double pivot_tolerance =
        64.0 * std::numeric_limits<double>::epsilon() * static_cast<double>(n) * matrix_norm;

    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        double pivot_abs = std::abs(a[col][col]);
        for (std::size_t row = col + 1; row < n; ++row) {
            const double candidate = std::abs(a[row][col]);
            if (candidate > pivot_abs) {
                pivot = row;
                pivot_abs = candidate;
            }
        }

        if (pivot_abs <= pivot_tolerance) {
            std::ostringstream oss;
            oss << "singular matrix near column " << col;
            return {false, {}, oss.str()};
        }

        if (pivot != col) {
            std::swap(a[pivot], a[col]);
            std::swap(b[pivot], b[col]);
        }

        const double diag = a[col][col];
        for (std::size_t row = col + 1; row < n; ++row) {
            const double factor = a[row][col] / diag;
            a[row][col] = 0.0;
            for (std::size_t k = col + 1; k < n; ++k) {
                a[row][k] -= factor * a[col][k];
            }
            b[row] -= factor * b[col];
        }
    }

    std::vector<double> x(n, 0.0);
    for (std::size_t i = n; i-- > 0;) {
        double sum = b[i];
        for (std::size_t j = i + 1; j < n; ++j) {
            sum -= a[i][j] * x[j];
        }
        x[i] = sum / a[i][i];
        if (!std::isfinite(x[i])) {
            return {false, {}, "linear solver produced a non-finite value"};
        }
    }

    return {true, x, "ok"};
}

}  // namespace thermox
