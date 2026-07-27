#pragma once

#include "thermox/sparse_matrix.hpp"

#include <functional>
#include <string>
#include <vector>

namespace thermox {

struct LinearSolveResult {
    bool success{false};
    std::vector<double> x;
    std::string message;
};

using LinearSolverFunction = std::function<LinearSolveResult(Matrix a, std::vector<double> b)>;
using SparseLinearSolverFunction =
    std::function<LinearSolveResult(SparseMatrix a, std::vector<double> b)>;

}  // namespace thermox
