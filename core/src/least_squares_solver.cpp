#include "thermox/least_squares_solver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>
#include <utility>

namespace thermox {

LeastSquaresResult solve_dense_least_squares(
    Matrix matrix,
    std::vector<double> rhs) {
    LeastSquaresResult result;
    const std::size_t rows = matrix.size();
    if (rows != rhs.size()) {
        result.message = "matrix row count does not match RHS size";
        return result;
    }
    if (rows == 0U) {
        result.success = true;
        result.message = "ok";
        return result;
    }
    const std::size_t columns = matrix.front().size();
    if (columns == 0U || rows < columns) {
        result.message =
            "least-squares matrix must have rows >= columns > 0";
        return result;
    }
    double matrix_scale = 0.0;
    for (const auto& row : matrix) {
        if (row.size() != columns) {
            result.message = "least-squares matrix must be rectangular";
            return result;
        }
        for (const double value : row) {
            if (!std::isfinite(value)) {
                result.message =
                    "least-squares matrix contains a non-finite value";
                return result;
            }
            matrix_scale = std::max(matrix_scale, std::abs(value));
        }
    }
    for (const double value : rhs) {
        if (!std::isfinite(value)) {
            result.message = "least-squares RHS contains a non-finite value";
            return result;
        }
    }
    if (matrix_scale == 0.0) {
        result.factorization_quality = {
            true, 0.0, 0.0, 0.0, 0U, columns,
            "reference-householder-cpqr-r-diagonal-ratio",
        };
        result.message = "least-squares matrix is zero";
        return result;
    }

    std::vector<std::size_t> permutation(columns);
    std::iota(permutation.begin(), permutation.end(), 0U);
    std::vector<double> column_norms(columns, 0.0);
    for (std::size_t column = 0; column < columns; ++column) {
        for (std::size_t row = 0; row < rows; ++row) {
            column_norms[column] = std::hypot(
                column_norms[column], matrix[row][column]);
        }
    }

    for (std::size_t diagonal = 0;
         diagonal < columns; ++diagonal) {
        const auto pivot_iterator = std::max_element(
            column_norms.begin() +
                static_cast<std::ptrdiff_t>(diagonal),
            column_norms.end());
        const std::size_t pivot = static_cast<std::size_t>(
            std::distance(column_norms.begin(), pivot_iterator));
        if (pivot != diagonal) {
            for (auto& row : matrix) {
                std::swap(row[diagonal], row[pivot]);
            }
            std::swap(column_norms[diagonal], column_norms[pivot]);
            std::swap(permutation[diagonal], permutation[pivot]);
        }

        double norm = 0.0;
        for (std::size_t row = diagonal; row < rows; ++row) {
            norm = std::hypot(norm, matrix[row][diagonal]);
        }
        if (norm > 0.0) {
            const double alpha = -std::copysign(
                norm, matrix[diagonal][diagonal]);
            std::vector<double> reflector(rows - diagonal, 0.0);
            for (std::size_t row = diagonal; row < rows; ++row) {
                reflector[row - diagonal] =
                    matrix[row][diagonal];
            }
            reflector.front() -= alpha;
            double reflector_norm_squared = 0.0;
            for (const double value : reflector) {
                reflector_norm_squared += value * value;
            }
            const double beta = 2.0 / reflector_norm_squared;
            for (std::size_t column = diagonal;
                 column < columns; ++column) {
                double projection = 0.0;
                for (std::size_t row = diagonal;
                     row < rows; ++row) {
                    projection += reflector[row - diagonal] *
                        matrix[row][column];
                }
                projection *= beta;
                for (std::size_t row = diagonal;
                     row < rows; ++row) {
                    matrix[row][column] -=
                        projection * reflector[row - diagonal];
                }
            }
            double rhs_projection = 0.0;
            for (std::size_t row = diagonal; row < rows; ++row) {
                rhs_projection += reflector[row - diagonal] * rhs[row];
            }
            rhs_projection *= beta;
            for (std::size_t row = diagonal; row < rows; ++row) {
                rhs[row] -=
                    rhs_projection * reflector[row - diagonal];
            }
            matrix[diagonal][diagonal] = alpha;
            for (std::size_t row = diagonal + 1;
                 row < rows; ++row) {
                matrix[row][diagonal] = 0.0;
            }
        }

        for (std::size_t column = diagonal + 1;
             column < columns; ++column) {
            column_norms[column] = 0.0;
            for (std::size_t row = diagonal + 1;
                 row < rows; ++row) {
                column_norms[column] = std::hypot(
                    column_norms[column], matrix[row][column]);
            }
        }
    }

    double maximum_diagonal = 0.0;
    for (std::size_t index = 0; index < columns; ++index) {
        maximum_diagonal = std::max(
            maximum_diagonal,
            std::abs(matrix[index][index]));
    }
    const double rank_tolerance =
        64.0 * std::numeric_limits<double>::epsilon() *
        static_cast<double>(std::max(rows, columns)) *
        maximum_diagonal;
    double minimum_accepted =
        std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < columns; ++index) {
        const double diagonal = std::abs(matrix[index][index]);
        if (diagonal > rank_tolerance) {
            ++result.rank;
            minimum_accepted = std::min(minimum_accepted, diagonal);
        }
    }
    result.factorization_quality = {
        true,
        result.rank == 0U ? 0.0 : minimum_accepted,
        maximum_diagonal,
        result.rank == columns && maximum_diagonal > 0.0
            ? minimum_accepted / maximum_diagonal
            : 0.0,
        result.rank,
        columns,
        "reference-householder-cpqr-r-diagonal-ratio",
    };
    if (result.rank != columns) {
        std::ostringstream message;
        message << "least-squares matrix rank " << result.rank
                << " is below column count " << columns;
        result.message = message.str();
        return result;
    }

    std::vector<double> pivoted_solution(columns, 0.0);
    for (std::size_t row = columns; row-- > 0;) {
        double value = rhs[row];
        for (std::size_t column = row + 1;
             column < columns; ++column) {
            value -= matrix[row][column] *
                pivoted_solution[column];
        }
        pivoted_solution[row] = value / matrix[row][row];
    }
    result.x.assign(columns, 0.0);
    for (std::size_t index = 0; index < columns; ++index) {
        result.x[permutation[index]] = pivoted_solution[index];
    }

    Matrix inverse_r(
        columns, std::vector<double>(columns, 0.0));
    for (std::size_t rhs_column = 0;
         rhs_column < columns; ++rhs_column) {
        for (std::size_t row = columns; row-- > 0;) {
            double value = row == rhs_column ? 1.0 : 0.0;
            for (std::size_t column = row + 1;
                 column < columns; ++column) {
                value -= matrix[row][column] *
                    inverse_r[column][rhs_column];
            }
            inverse_r[row][rhs_column] =
                value / matrix[row][row];
        }
    }
    result.covariance.assign(
        columns, std::vector<double>(columns, 0.0));
    for (std::size_t left = 0; left < columns; ++left) {
        for (std::size_t right = 0; right < columns; ++right) {
            double covariance = 0.0;
            for (std::size_t index = 0;
                 index < columns; ++index) {
                covariance += inverse_r[left][index] *
                    inverse_r[right][index];
            }
            result.covariance[permutation[left]][permutation[right]] =
                covariance;
        }
    }
    result.success = true;
    result.message = "ok";
    return result;
}

}  // namespace thermox
