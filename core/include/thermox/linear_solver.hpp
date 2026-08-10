#pragma once

#include "thermox/sparse_matrix.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace thermox {

struct LinearSolveResult {
    bool success{false};
    std::vector<double> x;
    std::string message;
    int symbolic_factorizations{0};
    int numeric_factorizations{0};
};

class SparseFactorization;

using LinearSolverFunction = std::function<LinearSolveResult(Matrix a, std::vector<double> b)>;
using SparseLinearSolverFunction =
    std::function<LinearSolveResult(SparseMatrix a, std::vector<double> b)>;
using SparseFactorizationPtr = std::shared_ptr<SparseFactorization>;
using SparseFactorizationResolver =
    std::function<SparseFactorizationPtr(const SparsePattern& pattern)>;

}  // namespace thermox
