#pragma once

#include <cstddef>
#include <vector>

namespace thermox {

using Matrix = std::vector<std::vector<double>>;

struct SparseTriplet {
    std::size_t row{0};
    std::size_t column{0};
    double value{0.0};
};

class SparsePattern {
public:
    SparsePattern() = default;
    SparsePattern(std::size_t rows,
                  std::size_t columns,
                  std::vector<std::size_t> row_offsets,
                  std::vector<std::size_t> column_indices);

    [[nodiscard]] std::size_t rows() const { return rows_; }
    [[nodiscard]] std::size_t columns() const { return columns_; }
    [[nodiscard]] std::size_t nonzeros() const { return column_indices_.size(); }
    [[nodiscard]] bool empty() const { return rows_ == 0 && columns_ == 0; }
    [[nodiscard]] const std::vector<std::size_t>& row_offsets() const { return row_offsets_; }
    [[nodiscard]] const std::vector<std::size_t>& column_indices() const { return column_indices_; }

private:
    std::size_t rows_{0};
    std::size_t columns_{0};
    std::vector<std::size_t> row_offsets_;
    std::vector<std::size_t> column_indices_;
};

class SparseMatrix {
public:
    SparseMatrix() = default;
    SparseMatrix(std::size_t rows,
                 std::size_t columns,
                 std::vector<std::size_t> row_offsets,
                 std::vector<std::size_t> column_indices,
                 std::vector<double> values);
    SparseMatrix(SparsePattern pattern, std::vector<double> values);

    [[nodiscard]] std::size_t rows() const { return rows_; }
    [[nodiscard]] std::size_t columns() const { return columns_; }
    [[nodiscard]] std::size_t nonzeros() const { return values_.size(); }
    [[nodiscard]] bool empty() const { return rows_ == 0 && columns_ == 0; }

    [[nodiscard]] const std::vector<std::size_t>& row_offsets() const { return row_offsets_; }
    [[nodiscard]] const std::vector<std::size_t>& column_indices() const { return column_indices_; }
    [[nodiscard]] const std::vector<double>& values() const { return values_; }
    [[nodiscard]] SparsePattern pattern() const;

    [[nodiscard]] double at(std::size_t row, std::size_t column) const;
    [[nodiscard]] Matrix to_dense() const;

    void scale_rows(const std::vector<double>& row_scales);
    void scale_columns(const std::vector<double>& column_scales);

private:
    std::size_t rows_{0};
    std::size_t columns_{0};
    std::vector<std::size_t> row_offsets_;
    std::vector<std::size_t> column_indices_;
    std::vector<double> values_;
};

[[nodiscard]] SparseMatrix sparse_from_dense(const Matrix& dense, double drop_tolerance = 0.0);
[[nodiscard]] SparseMatrix sparse_from_triplets(std::size_t rows,
                                                std::size_t columns,
                                                std::vector<SparseTriplet> triplets,
                                                double drop_tolerance = 0.0);

}  // namespace thermox
