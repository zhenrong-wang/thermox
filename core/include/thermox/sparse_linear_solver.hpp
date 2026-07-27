#pragma once

#include "thermox/linear_solver.hpp"

namespace thermox {

LinearSolveResult solve_sparse_linear_system(SparseMatrix a, std::vector<double> b);

}  // namespace thermox
