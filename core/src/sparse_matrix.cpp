#include "thermox/sparse_matrix.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace thermox {

namespace {

void validate_csr_shape(std::size_t rows,
                        std::size_t columns,
                        const std::vector<std::size_t>& row_offsets,
                        const std::vector<std::size_t>& column_indices) {
    if (row_offsets.size() != rows + 1) {
        throw std::invalid_argument("sparse matrix row_offsets size must equal rows + 1");
    }
    if (!row_offsets.empty() && row_offsets.front() != 0) {
        throw std::invalid_argument("sparse matrix row_offsets must start at zero");
    }
    if (!row_offsets.empty() && row_offsets.back() != column_indices.size()) {
        throw std::invalid_argument("sparse matrix row_offsets must end at nonzero count");
    }
    for (std::size_t row = 0; row < rows; ++row) {
        if (row_offsets[row] > row_offsets[row + 1]) {
            throw std::invalid_argument("sparse matrix row_offsets must be monotonic");
        }
        std::size_t previous_column = 0;
        bool have_previous = false;
        for (std::size_t offset = row_offsets[row]; offset < row_offsets[row + 1]; ++offset) {
            if (column_indices[offset] >= columns) {
                throw std::invalid_argument("sparse matrix column index out of range");
            }
            if (have_previous && column_indices[offset] <= previous_column) {
                throw std::invalid_argument("sparse matrix column indices must be strictly increasing per row");
            }
            previous_column = column_indices[offset];
            have_previous = true;
        }
    }
}

bool keep_value(double value, double drop_tolerance) {
    return std::abs(value) > drop_tolerance;
}

}  // namespace

SparsePattern::SparsePattern(std::size_t rows,
                             std::size_t columns,
                             std::vector<std::size_t> row_offsets,
                             std::vector<std::size_t> column_indices)
    : rows_(rows),
      columns_(columns),
      row_offsets_(std::move(row_offsets)),
      column_indices_(std::move(column_indices)) {
    validate_csr_shape(rows_, columns_, row_offsets_, column_indices_);
}

SparseMatrix::SparseMatrix(std::size_t rows,
                           std::size_t columns,
                           std::vector<std::size_t> row_offsets,
                           std::vector<std::size_t> column_indices,
                           std::vector<double> values)
    : rows_(rows),
      columns_(columns),
      row_offsets_(std::move(row_offsets)),
      column_indices_(std::move(column_indices)),
      values_(std::move(values)) {
    validate_csr_shape(rows_, columns_, row_offsets_, column_indices_);
    if (column_indices_.size() != values_.size()) {
        throw std::invalid_argument("sparse matrix column_indices and values sizes differ");
    }
    for (const double value : values_) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("sparse matrix values must be finite");
        }
    }
}

SparseMatrix::SparseMatrix(SparsePattern pattern, std::vector<double> values)
    : SparseMatrix(pattern.rows(),
                   pattern.columns(),
                   pattern.row_offsets(),
                   pattern.column_indices(),
                   std::move(values)) {
}

SparsePattern SparseMatrix::pattern() const {
    return SparsePattern(rows_, columns_, row_offsets_, column_indices_);
}

double SparseMatrix::at(std::size_t row, std::size_t column) const {
    if (row >= rows_ || column >= columns_) {
        throw std::out_of_range("sparse matrix index out of range");
    }
    for (std::size_t offset = row_offsets_[row]; offset < row_offsets_[row + 1]; ++offset) {
        if (column_indices_[offset] == column) {
            return values_[offset];
        }
        if (column_indices_[offset] > column) {
            break;
        }
    }
    return 0.0;
}

Matrix SparseMatrix::to_dense() const {
    Matrix dense(rows_, std::vector<double>(columns_, 0.0));
    for (std::size_t row = 0; row < rows_; ++row) {
        for (std::size_t offset = row_offsets_[row]; offset < row_offsets_[row + 1]; ++offset) {
            dense[row][column_indices_[offset]] = values_[offset];
        }
    }
    return dense;
}

void SparseMatrix::scale_rows(const std::vector<double>& row_scales) {
    if (row_scales.size() != rows_) {
        throw std::invalid_argument("sparse row scale count must match row count");
    }
    for (std::size_t row = 0; row < rows_; ++row) {
        if (!std::isfinite(row_scales[row]) || row_scales[row] <= 0.0) {
            throw std::invalid_argument("sparse row scales must be positive and finite");
        }
        for (std::size_t offset = row_offsets_[row]; offset < row_offsets_[row + 1]; ++offset) {
            values_[offset] /= row_scales[row];
        }
    }
}

void SparseMatrix::scale_columns(const std::vector<double>& column_scales) {
    if (column_scales.size() != columns_) {
        throw std::invalid_argument("sparse column scale count must match column count");
    }
    for (const double scale : column_scales) {
        if (!std::isfinite(scale) || scale <= 0.0) {
            throw std::invalid_argument("sparse column scales must be positive and finite");
        }
    }
    for (std::size_t offset = 0; offset < values_.size(); ++offset) {
        values_[offset] *= column_scales[column_indices_[offset]];
    }
}

SparseMatrix sparse_from_dense(const Matrix& dense, double drop_tolerance) {
    if (!std::isfinite(drop_tolerance) || drop_tolerance < 0.0) {
        throw std::invalid_argument("drop_tolerance must be non-negative and finite");
    }
    const std::size_t rows = dense.size();
    const std::size_t columns = rows == 0 ? 0 : dense.front().size();
    std::vector<std::size_t> row_offsets;
    std::vector<std::size_t> column_indices;
    std::vector<double> values;
    row_offsets.reserve(rows + 1);
    row_offsets.push_back(0);

    for (const auto& row_values : dense) {
        if (row_values.size() != columns) {
            throw std::invalid_argument("dense matrix rows must have consistent column count");
        }
        for (std::size_t column = 0; column < columns; ++column) {
            const double value = row_values[column];
            if (!std::isfinite(value)) {
                throw std::invalid_argument("dense matrix values must be finite");
            }
            if (keep_value(value, drop_tolerance)) {
                column_indices.push_back(column);
                values.push_back(value);
            }
        }
        row_offsets.push_back(values.size());
    }

    return SparseMatrix(rows, columns, std::move(row_offsets), std::move(column_indices),
                        std::move(values));
}

SparseMatrix sparse_from_triplets(std::size_t rows,
                                  std::size_t columns,
                                  std::vector<SparseTriplet> triplets,
                                  double drop_tolerance) {
    if (!std::isfinite(drop_tolerance) || drop_tolerance < 0.0) {
        throw std::invalid_argument("drop_tolerance must be non-negative and finite");
    }

    std::map<std::pair<std::size_t, std::size_t>, double> accumulated;
    for (const auto& triplet : triplets) {
        if (triplet.row >= rows || triplet.column >= columns) {
            throw std::invalid_argument("sparse triplet index out of range");
        }
        if (!std::isfinite(triplet.value)) {
            throw std::invalid_argument("sparse triplet values must be finite");
        }
        accumulated[{triplet.row, triplet.column}] += triplet.value;
    }

    std::vector<std::size_t> row_offsets(rows + 1, 0);
    std::vector<std::size_t> column_indices;
    std::vector<double> values;

    std::size_t current_row = 0;
    for (const auto& entry : accumulated) {
        const auto [row, column] = entry.first;
        const double value = entry.second;
        while (current_row < row) {
            row_offsets[current_row + 1] = values.size();
            ++current_row;
        }
        if (keep_value(value, drop_tolerance)) {
            column_indices.push_back(column);
            values.push_back(value);
        }
    }
    while (current_row < rows) {
        row_offsets[current_row + 1] = values.size();
        ++current_row;
    }

    return SparseMatrix(rows, columns, std::move(row_offsets), std::move(column_indices),
                        std::move(values));
}

}  // namespace thermox
