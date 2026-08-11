#include "thermox/sparse_linear_solver.hpp"

#include "thermox/dense_linear_solver.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <tuple>
#include <utility>

#if defined(THERMOX_HAS_UMFPACK)
#include <umfpack.h>
#endif

namespace thermox {

namespace {

bool is_effectively_zero(double value, double tolerance) {
    return std::abs(value) <= tolerance;
}

void retain_worse_quality(
    FactorizationQuality& retained,
    const FactorizationQuality& candidate) {
    if (!candidate.available) return;
    if (!retained.available ||
        candidate.reciprocal_pivot_ratio <
            retained.reciprocal_pivot_ratio) {
        retained = candidate;
    }
}

struct SparsePatternKey {
    std::size_t rows{0};
    std::size_t columns{0};
    std::vector<std::size_t> row_offsets;
    std::vector<std::size_t> column_indices;

    [[nodiscard]] bool operator<(
        const SparsePatternKey& other) const {
        return std::tie(rows, columns, row_offsets, column_indices) <
               std::tie(other.rows, other.columns,
                        other.row_offsets, other.column_indices);
    }
};

struct SparseFactorizationResolverState {
    std::mutex mutex;
    std::map<SparsePatternKey, SparseFactorizationPtr> entries;
};

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

[[maybe_unused]] bool same_pattern(
    const SparsePattern& left,
    const SparsePattern& right) {
    return left.rows() == right.rows() &&
           left.columns() == right.columns() &&
           left.row_offsets() == right.row_offsets() &&
           left.column_indices() == right.column_indices();
}

}  // namespace

MultipleLinearSolveResult SparseFactorization::solve_multiple(
    const SparseMatrix& matrix,
    const Matrix& right_hand_sides) {
    MultipleLinearSolveResult result;
    result.x.reserve(right_hand_sides.size());
    for (const auto& rhs : right_hand_sides) {
        auto solved = solve(matrix, rhs);
        result.symbolic_factorizations +=
            solved.symbolic_factorizations;
        result.numeric_factorizations +=
            solved.numeric_factorizations;
        retain_worse_quality(
            result.factorization_quality,
            solved.factorization_quality);
        if (!solved.success) {
            result.message = solved.message;
            result.x.clear();
            return result;
        }
        result.x.push_back(std::move(solved.x));
    }
    result.success = true;
    result.message = "ok";
    return result;
}

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
        if (!std::isfinite(value)) {
            return {
                false, {},
                "sparse matrix contains a non-finite value"};
        }
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
        return {
            false, {}, "sparse matrix is zero", 0, 0,
            {true, 0.0, 0.0, 0.0, 0U, n,
             "reference-csr-u-diagonal-ratio"}};
    }
    const double pivot_tolerance =
        64.0 * std::numeric_limits<double>::epsilon() * static_cast<double>(n) * matrix_norm;
    const double drop_tolerance = pivot_tolerance;
    const double factor_tolerance =
        64.0 * std::numeric_limits<double>::epsilon() * static_cast<double>(n);

    auto rows = to_sparse_rows(a);
    double minimum_pivot = std::numeric_limits<double>::infinity();
    double maximum_pivot = 0.0;

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
            return {
                false, {}, oss.str(), 0, 0,
                {true,
                 col == 0U ? 0.0 : minimum_pivot,
                 maximum_pivot,
                 0.0,
                 col,
                 n,
                 "reference-csr-u-diagonal-ratio"}};
        }

        if (pivot != col) {
            std::swap(rows[pivot], rows[col]);
            std::swap(b[pivot], b[col]);
        }

        const double diag = rows[col].at(col);
        minimum_pivot = std::min(minimum_pivot, std::abs(diag));
        maximum_pivot = std::max(maximum_pivot, std::abs(diag));
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

    const FactorizationQuality factorization_quality{
        true,
        minimum_pivot,
        maximum_pivot,
        maximum_pivot > 0.0 ? minimum_pivot / maximum_pivot : 0.0,
        n,
        n,
        "reference-csr-u-diagonal-ratio",
    };

    std::vector<double> x(n, 0.0);
    for (std::size_t reverse = n; reverse-- > 0;) {
        const auto diag_entry = rows[reverse].find(reverse);
        if (diag_entry == rows[reverse].end() ||
            std::abs(diag_entry->second) <= pivot_tolerance) {
            std::ostringstream oss;
            oss << "singular sparse matrix during back substitution near row " << reverse;
            return {
                false, {}, oss.str(), 0, 0,
                factorization_quality};
        }

        double sum = b[reverse];
        for (const auto& [column, value] : rows[reverse]) {
            if (column > reverse) {
                sum -= value * x[column];
            }
        }
        x[reverse] = sum / diag_entry->second;
        if (!std::isfinite(x[reverse])) {
            return {
                false, {},
                "sparse linear solver produced a non-finite value",
                0, 0, factorization_quality};
        }
    }

    LinearSolveResult result{true, std::move(x), "ok"};
    result.factorization_quality = factorization_quality;
    return result;
}

class ReferenceSparseFactorization final
    : public SparseFactorization {
public:
    std::string_view backend_name() const noexcept override {
        return "reference-csr";
    }

    LinearSolveResult solve(
        const SparseMatrix& matrix,
        std::vector<double> rhs) override {
        auto result = solve_reference_sparse_linear_system(
            matrix, std::move(rhs));
        if (result.factorization_quality.available) {
            result.numeric_factorizations = 1;
        }
        return result;
    }

    MultipleLinearSolveResult solve_multiple(
        const SparseMatrix& matrix,
        const Matrix& right_hand_sides) override {
        DenseLinearFactorization factorization;
        MultipleLinearSolveResult result;
        if (!factorization.factorize(matrix.to_dense())) {
            result.message = factorization.message();
            result.numeric_factorizations = 1;
            result.factorization_quality = factorization.quality();
            return result;
        }
        const auto solved = factorization.solve_multiple(
            right_hand_sides);
        result.numeric_factorizations = 1;
        result.factorization_quality = factorization.quality();
        result.x.reserve(solved.size());
        for (const auto& solution : solved) {
            if (!solution.success) {
                result.message = solution.message;
                result.x.clear();
                return result;
            }
            result.x.push_back(solution.x);
        }
        result.success = true;
        result.message = "ok (reference CSR multi-RHS)";
        return result;
    }
};

#if defined(THERMOX_HAS_UMFPACK)
class UmfpackSparseFactorization final
    : public SparseFactorization {
public:
    ~UmfpackSparseFactorization() override {
        if (symbolic_ != nullptr) {
            umfpack_di_free_symbolic(&symbolic_);
        }
    }

    std::string_view backend_name() const noexcept override {
        return "umfpack";
    }

    LinearSolveResult solve(
        const SparseMatrix& matrix,
        std::vector<double> rhs) override {
        auto multiple = solve_multiple(matrix, {std::move(rhs)});
        LinearSolveResult result;
        result.success = multiple.success;
        result.message = std::move(multiple.message);
        result.symbolic_factorizations =
            multiple.symbolic_factorizations;
        result.numeric_factorizations =
            multiple.numeric_factorizations;
        result.factorization_quality =
            multiple.factorization_quality;
        if (multiple.success && !multiple.x.empty()) {
            result.x = std::move(multiple.x.front());
        }
        return result;
    }

    MultipleLinearSolveResult solve_multiple(
        const SparseMatrix& matrix,
        const Matrix& right_hand_sides) override {
        std::scoped_lock lock(mutex_);
        const std::size_t n = matrix.rows();
        if (matrix.columns() != n) {
            return {
                false, {},
                "sparse matrix must be square"};
        }
        for (const auto& rhs : right_hand_sides) {
            if (rhs.size() != n) {
                return {
                    false, {},
                    "sparse matrix row count does not match RHS size"};
            }
            for (const double value : rhs) {
                if (!std::isfinite(value)) {
                    return {
                        false, {},
                        "RHS contains a non-finite value"};
                }
            }
        }
        for (const double value : matrix.values()) {
            if (!std::isfinite(value)) {
                return {
                    false, {},
                    "sparse matrix contains a non-finite value"};
            }
        }
        if (n == 0) {
            return {
                true, Matrix(right_hand_sides.size()),
                "ok (UMFPACK)"};
        }
        if (n > static_cast<std::size_t>(
                    std::numeric_limits<int>::max()) ||
            matrix.nonzeros() >
                static_cast<std::size_t>(
                    std::numeric_limits<int>::max())) {
            return {
                false, {},
                "sparse matrix exceeds UMFPACK integer index range"};
        }

        MultipleLinearSolveResult result;
        const auto pattern = matrix.pattern();
        if (symbolic_ == nullptr ||
            !same_pattern(pattern_, pattern)) {
            clear_symbolic();
            prepare_pattern(matrix);
            copy_values(matrix);
            const int status = umfpack_di_symbolic(
                static_cast<int>(n), static_cast<int>(n),
                column_offsets_.data(), row_indices_.data(),
                values_.data(), &symbolic_, nullptr, nullptr);
            ++result.symbolic_factorizations;
            if (status != UMFPACK_OK) {
                clear_symbolic();
                result.message =
                    status == UMFPACK_ERROR_out_of_memory
                    ? "UMFPACK symbolic factorization ran out of memory"
                    : "singular sparse matrix during UMFPACK "
                      "symbolic factorization";
                return result;
            }
            pattern_ = pattern;
        } else {
            copy_values(matrix);
        }

        void* numeric = nullptr;
        std::array<double, UMFPACK_INFO> numeric_info{};
        const int numeric_status = umfpack_di_numeric(
            column_offsets_.data(), row_indices_.data(),
            values_.data(), symbolic_, &numeric, nullptr,
            numeric_info.data());
        ++result.numeric_factorizations;
        if (numeric_status == UMFPACK_OK ||
            numeric_status == UMFPACK_WARNING_singular_matrix) {
            const double minimum_pivot =
                numeric_info[UMFPACK_UMIN];
            const double maximum_pivot =
                numeric_info[UMFPACK_UMAX];
            const double pivot_ratio =
                numeric_info[UMFPACK_RCOND];
            const double accepted_pivots =
                numeric_info[UMFPACK_UDIAG_NZ];
            if (std::isfinite(minimum_pivot) &&
                std::isfinite(maximum_pivot) &&
                std::isfinite(pivot_ratio) &&
                std::isfinite(accepted_pivots) &&
                minimum_pivot >= 0.0 && maximum_pivot >= 0.0 &&
                pivot_ratio >= 0.0 && accepted_pivots >= 0.0 &&
                accepted_pivots <= static_cast<double>(n)) {
                result.factorization_quality = {
                    true,
                    minimum_pivot,
                    maximum_pivot,
                    pivot_ratio,
                    static_cast<std::size_t>(accepted_pivots),
                    n,
                    "umfpack-u-diagonal-ratio",
                };
            }
        }
        if (numeric_status != UMFPACK_OK) {
            if (numeric != nullptr) {
                umfpack_di_free_numeric(&numeric);
            }
            result.message =
                numeric_status == UMFPACK_ERROR_out_of_memory
                ? "UMFPACK numeric factorization ran out of memory"
                : "singular sparse matrix during UMFPACK "
                  "numeric factorization";
            return result;
        }

        result.x.assign(
            right_hand_sides.size(),
            std::vector<double>(n, 0.0));
        int solve_status = UMFPACK_OK;
        for (std::size_t index = 0;
             index < right_hand_sides.size(); ++index) {
            solve_status = umfpack_di_solve(
                UMFPACK_A, column_offsets_.data(),
                row_indices_.data(), values_.data(),
                result.x[index].data(),
                right_hand_sides[index].data(),
                numeric, nullptr, nullptr);
            if (solve_status != UMFPACK_OK) break;
        }
        umfpack_di_free_numeric(&numeric);
        if (solve_status != UMFPACK_OK) {
            result.x.clear();
            result.message =
                "UMFPACK sparse multi-RHS back substitution failed";
            return result;
        }
        result.success = true;
        result.message = "ok (UMFPACK)";
        return result;
    }

private:
    void clear_symbolic() {
        if (symbolic_ != nullptr) {
            umfpack_di_free_symbolic(&symbolic_);
        }
        pattern_ = {};
        column_offsets_.clear();
        row_indices_.clear();
        csr_to_csc_.clear();
        values_.clear();
    }

    void prepare_pattern(const SparseMatrix& matrix) {
        const std::size_t n = matrix.rows();
        column_offsets_.assign(n + 1, 0);
        for (const auto column : matrix.column_indices()) {
            ++column_offsets_.at(column + 1);
        }
        for (std::size_t column = 0; column < n; ++column) {
            column_offsets_[column + 1] +=
                column_offsets_[column];
        }
        row_indices_.assign(matrix.nonzeros(), 0);
        csr_to_csc_.assign(matrix.nonzeros(), 0);
        values_.assign(matrix.nonzeros(), 0.0);
        std::vector<int> next = column_offsets_;
        for (std::size_t row = 0; row < n; ++row) {
            for (std::size_t offset =
                     matrix.row_offsets()[row];
                 offset < matrix.row_offsets()[row + 1];
                 ++offset) {
                const auto column =
                    matrix.column_indices()[offset];
                const int destination = next[column]++;
                row_indices_.at(
                    static_cast<std::size_t>(destination)) =
                    static_cast<int>(row);
                csr_to_csc_.at(offset) = destination;
            }
        }
    }

    void copy_values(const SparseMatrix& matrix) {
        for (std::size_t offset = 0;
             offset < matrix.nonzeros(); ++offset) {
            values_.at(static_cast<std::size_t>(
                csr_to_csc_.at(offset))) =
                matrix.values().at(offset);
        }
    }

    std::mutex mutex_;
    SparsePattern pattern_;
    std::vector<int> column_offsets_;
    std::vector<int> row_indices_;
    std::vector<int> csr_to_csc_;
    std::vector<double> values_;
    void* symbolic_{nullptr};
};
#endif

SparseFactorizationPtr make_default_sparse_factorization() {
#if defined(THERMOX_HAS_UMFPACK)
    return std::make_shared<UmfpackSparseFactorization>();
#else
    return std::make_shared<ReferenceSparseFactorization>();
#endif
}

SparseFactorizationResolver
make_default_sparse_factorization_resolver() {
    auto state =
        std::make_shared<SparseFactorizationResolverState>();
    return [state](const SparsePattern& pattern) {
        SparsePatternKey key{
            pattern.rows(), pattern.columns(),
            pattern.row_offsets(), pattern.column_indices()};
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto found = state->entries.find(key);
        if (found != state->entries.end()) {
            return found->second;
        }
        auto factorization = make_default_sparse_factorization();
        state->entries.emplace(std::move(key), factorization);
        return factorization;
    };
}

LinearSolveResult solve_sparse_linear_system(
    SparseMatrix a, std::vector<double> b) {
    return make_default_sparse_factorization()->solve(
        a, std::move(b));
}

}  // namespace thermox
