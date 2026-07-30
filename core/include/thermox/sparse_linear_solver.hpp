#pragma once

#include "thermox/linear_solver.hpp"

#include <string_view>

namespace thermox {

class SparseFactorization {
public:
    virtual ~SparseFactorization() = default;

    [[nodiscard]] virtual std::string_view backend_name()
        const noexcept = 0;
    [[nodiscard]] virtual LinearSolveResult solve(
        const SparseMatrix& matrix,
        std::vector<double> rhs) = 0;
};

[[nodiscard]] SparseFactorizationPtr
make_default_sparse_factorization();

LinearSolveResult solve_sparse_linear_system(SparseMatrix a, std::vector<double> b);

}  // namespace thermox
