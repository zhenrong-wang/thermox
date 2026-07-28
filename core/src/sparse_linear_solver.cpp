#include "thermox/sparse_linear_solver.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <sstream>
#include <utility>

#if defined(THERMOX_HAS_UMFPACK)
#include <umfpack.h>
#endif

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

[[maybe_unused]] static LinearSolveResult
solve_reference_sparse_linear_system(
    SparseMatrix a, std::vector<double> b) {
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

#if defined(THERMOX_HAS_UMFPACK)
LinearSolveResult solve_umfpack_sparse_linear_system(
    const SparseMatrix& a,
    const std::vector<double>& b) {
    const std::size_t n = b.size();
    if (a.rows() != n) {
        return {
            false, {},
            "sparse matrix row count does not match RHS size"};
    }
    if (a.columns() != n) {
        return {false, {}, "sparse matrix must be square"};
    }
    if (n == 0) return {true, {}, "ok (UMFPACK)"};
    if (n > static_cast<std::size_t>(
                std::numeric_limits<int>::max()) ||
        a.nonzeros() > static_cast<std::size_t>(
                           std::numeric_limits<int>::max())) {
        return {
            false, {},
            "sparse matrix exceeds UMFPACK integer index range"};
    }
    std::vector<int> column_offsets(n + 1, 0);
    for (const auto column : a.column_indices()) {
        ++column_offsets.at(column + 1);
    }
    for (std::size_t column = 0; column < n; ++column) {
        column_offsets[column + 1] += column_offsets[column];
    }
    std::vector<int> row_indices(a.nonzeros(), 0);
    std::vector<double> values(a.nonzeros(), 0.0);
    std::vector<int> next = column_offsets;
    for (std::size_t row = 0; row < n; ++row) {
        for (std::size_t offset = a.row_offsets()[row];
             offset < a.row_offsets()[row + 1]; ++offset) {
            const auto column = a.column_indices()[offset];
            const int destination = next[column]++;
            row_indices[static_cast<std::size_t>(destination)] =
                static_cast<int>(row);
            values[static_cast<std::size_t>(destination)] =
                a.values()[offset];
        }
    }

    void* symbolic = nullptr;
    int status = umfpack_di_symbolic(
        static_cast<int>(n), static_cast<int>(n),
        column_offsets.data(), row_indices.data(), values.data(),
        &symbolic, nullptr, nullptr);
    if (status != UMFPACK_OK) {
        return {
            false, {},
            status == UMFPACK_ERROR_out_of_memory
                ? "UMFPACK symbolic factorization ran out of memory"
                : "singular sparse matrix during UMFPACK symbolic factorization"};
    }
    void* numeric = nullptr;
    status = umfpack_di_numeric(
        column_offsets.data(), row_indices.data(), values.data(),
        symbolic, &numeric, nullptr, nullptr);
    umfpack_di_free_symbolic(&symbolic);
    if (status != UMFPACK_OK) {
        if (numeric != nullptr) {
            umfpack_di_free_numeric(&numeric);
        }
        return {
            false, {},
            status == UMFPACK_ERROR_out_of_memory
                ? "UMFPACK numeric factorization ran out of memory"
                : "singular sparse matrix during UMFPACK numeric factorization"};
    }
    std::vector<double> x(n, 0.0);
    status = umfpack_di_solve(
        UMFPACK_A, column_offsets.data(), row_indices.data(),
        values.data(), x.data(), b.data(), numeric, nullptr,
        nullptr);
    umfpack_di_free_numeric(&numeric);
    if (status != UMFPACK_OK) {
        return {
            false, {},
            "UMFPACK sparse back substitution failed"};
    }
    return {true, std::move(x), "ok (UMFPACK)"};
}
#endif

LinearSolveResult solve_sparse_linear_system(
    SparseMatrix a, std::vector<double> b) {
#if defined(THERMOX_HAS_UMFPACK)
    return solve_umfpack_sparse_linear_system(a, b);
#else
    return solve_reference_sparse_linear_system(
        std::move(a), std::move(b));
#endif
}

}  // namespace thermox
