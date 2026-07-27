#pragma once

#include "thermox/linear_solver.hpp"

namespace thermox {

LinearSolveResult solve_dense_linear_system(Matrix a, std::vector<double> b);

}  // namespace thermox
