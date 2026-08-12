#include "thermox/dense_cholesky.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <utility>

namespace thermox {

bool DenseCholeskyFactorization::factorize(Matrix matrix) {
    valid_ = false;
    lower_.clear();
    message_.clear();
    quality_ = {};
    const std::size_t count = matrix.size();
    double scale = 0.0;
    for (const auto& row : matrix) {
        if (row.size() != count) {
            message_ = "Cholesky matrix must be square";
            return false;
        }
        for (const double value : row) {
            if (!std::isfinite(value)) {
                message_ = "Cholesky matrix contains a non-finite value";
                return false;
            }
            scale = std::max(scale, std::abs(value));
        }
    }
    if (count == 0U) {
        valid_ = true;
        message_ = "ok";
        return true;
    }
    const double symmetry_tolerance =
        64.0 * std::numeric_limits<double>::epsilon() *
        static_cast<double>(count) * std::max(scale, 1.0);
    for (std::size_t row = 0; row < count; ++row) {
        for (std::size_t column = 0; column < row; ++column) {
            if (std::abs(matrix[row][column] -
                         matrix[column][row]) > symmetry_tolerance) {
                message_ = "Cholesky matrix must be symmetric";
                return false;
            }
        }
    }

    lower_.assign(count, std::vector<double>(count, 0.0));
    double minimum_diagonal =
        std::numeric_limits<double>::infinity();
    double maximum_diagonal = 0.0;
    const double positive_tolerance =
        64.0 * std::numeric_limits<double>::epsilon() *
        static_cast<double>(count) * std::max(scale, 1.0);
    for (std::size_t row = 0; row < count; ++row) {
        for (std::size_t column = 0; column <= row; ++column) {
            double value = matrix[row][column];
            for (std::size_t inner = 0; inner < column; ++inner) {
                value -= lower_[row][inner] * lower_[column][inner];
            }
            if (row == column) {
                if (!(value > positive_tolerance)) {
                    quality_ = {
                        true,
                        row == 0U ? 0.0 : minimum_diagonal,
                        maximum_diagonal,
                        0.0,
                        row,
                        count,
                        "reference-dense-cholesky-diagonal-ratio",
                    };
                    std::ostringstream message;
                    message << "matrix is not positive definite at row "
                            << row;
                    message_ = message.str();
                    lower_.clear();
                    return false;
                }
                lower_[row][column] = std::sqrt(value);
                minimum_diagonal = std::min(
                    minimum_diagonal, lower_[row][column]);
                maximum_diagonal = std::max(
                    maximum_diagonal, lower_[row][column]);
            } else {
                lower_[row][column] =
                    value / lower_[column][column];
            }
        }
    }
    quality_ = {
        true,
        minimum_diagonal,
        maximum_diagonal,
        minimum_diagonal / maximum_diagonal,
        count,
        count,
        "reference-dense-cholesky-diagonal-ratio",
    };
    valid_ = true;
    message_ = "ok";
    return true;
}

LinearSolveResult DenseCholeskyFactorization::solve_lower(
    std::vector<double> rhs) const {
    if (!valid_) return {false, {}, message_};
    if (rhs.size() != lower_.size()) {
        return {false, {}, "RHS size does not match Cholesky factor"};
    }
    for (std::size_t row = 0; row < lower_.size(); ++row) {
        if (!std::isfinite(rhs[row])) {
            return {false, {}, "RHS contains a non-finite value"};
        }
        for (std::size_t column = 0; column < row; ++column) {
            rhs[row] -= lower_[row][column] * rhs[column];
        }
        rhs[row] /= lower_[row][row];
    }
    LinearSolveResult result{true, std::move(rhs), "ok"};
    result.factorization_quality = quality_;
    return result;
}

Matrix DenseCholeskyFactorization::whiten_rows(
    const Matrix& values) const {
    if (!valid_) return {};
    if (values.size() != lower_.size()) return {};
    if (values.empty()) return {};
    const std::size_t columns = values.front().size();
    for (const auto& row : values) {
        if (row.size() != columns) return {};
    }
    Matrix whitened(
        values.size(), std::vector<double>(columns, 0.0));
    for (std::size_t column = 0; column < columns; ++column) {
        std::vector<double> rhs(values.size(), 0.0);
        for (std::size_t row = 0; row < values.size(); ++row) {
            rhs[row] = values[row][column];
        }
        const auto solution = solve_lower(std::move(rhs));
        if (!solution.success) return {};
        for (std::size_t row = 0; row < values.size(); ++row) {
            whitened[row][column] = solution.x[row];
        }
    }
    return whitened;
}

}  // namespace thermox
