#include "thermox/sparse_linear_solver.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <sstream>
#include <utility>

namespace thermox {

namespace {

bool is_effectively_zero(double value, double tolerance) {
    return std::abs(value) <= tolerance;
}

std::vector<std::map<std::size_t, double>> to_sparse_rows(const SparseMatrix& matrix) {
    std::vector<std::map<std::size_t, double>> rows(matrix.rows());
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
        for (std::size_t offset = matrix.row_offsets()[row];
             offset < matrix.row_offsets()[row + 1];
             ++offset) {
            rows[row].emplace(matrix.column_indices()[offset], matrix.values()[offset]);
        }
    }
    return rows;
}

}  // namespace

LinearSolveResult solve_sparse_linear_system(SparseMatrix a, std::vector<double> b) {
    const std::size_t n = b.size();
    if (a.rows() != n) {
        return {false, {}, "sparse matrix row count does not match RHS size"};
    }
    if (a.columns() != n) {
        return {false, {}, "sparse matrix must be square"};
    }
    double matrix_norm = 0.0;
    for (const double value : a.values()) {
        matrix_norm = std::max(matrix_norm, std::abs(value));
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
        return {false, {}, "sparse matrix is zero"};
    }
    const double pivot_tolerance =
        64.0 * std::numeric_limits<double>::epsilon() * static_cast<double>(n) * matrix_norm;
    const double drop_tolerance = pivot_tolerance;
    const double factor_tolerance =
        64.0 * std::numeric_limits<double>::epsilon() * static_cast<double>(n);

    auto rows = to_sparse_rows(a);

    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        double pivot_abs = 0.0;
        for (std::size_t row = col; row < n; ++row) {
            const auto entry = rows[row].find(col);
            const double candidate = entry == rows[row].end() ? 0.0 : std::abs(entry->second);
            if (candidate > pivot_abs) {
                pivot = row;
                pivot_abs = candidate;
            }
        }

        if (pivot_abs <= pivot_tolerance) {
            std::ostringstream oss;
            oss << "singular sparse matrix near column " << col;
            return {false, {}, oss.str()};
        }

        if (pivot != col) {
            std::swap(rows[pivot], rows[col]);
            std::swap(b[pivot], b[col]);
        }

        const double diag = rows[col].at(col);
        for (std::size_t row = col + 1; row < n; ++row) {
            const auto factor_entry = rows[row].find(col);
            if (factor_entry == rows[row].end()) {
                continue;
            }
            const double factor = factor_entry->second / diag;
            rows[row].erase(factor_entry);
            if (is_effectively_zero(factor, factor_tolerance)) {
                continue;
            }

            for (const auto& [column, value] : rows[col]) {
                if (column <= col) {
                    continue;
                }
                double& target = rows[row][column];
                target -= factor * value;
                if (is_effectively_zero(target, drop_tolerance)) {
                    rows[row].erase(column);
                }
            }
            b[row] -= factor * b[col];
        }
    }

    std::vector<double> x(n, 0.0);
    for (std::size_t reverse = n; reverse-- > 0;) {
        const auto diag_entry = rows[reverse].find(reverse);
        if (diag_entry == rows[reverse].end() ||
            std::abs(diag_entry->second) <= pivot_tolerance) {
            std::ostringstream oss;
            oss << "singular sparse matrix during back substitution near row " << reverse;
            return {false, {}, oss.str()};
        }

        double sum = b[reverse];
        for (const auto& [column, value] : rows[reverse]) {
            if (column > reverse) {
                sum -= value * x[column];
            }
        }
        x[reverse] = sum / diag_entry->second;
        if (!std::isfinite(x[reverse])) {
            return {false, {}, "sparse linear solver produced a non-finite value"};
        }
    }

    return {true, x, "ok"};
}

}  // namespace thermox
