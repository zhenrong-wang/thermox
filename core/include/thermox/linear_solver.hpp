#pragma once

#include "thermox/sparse_matrix.hpp"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace thermox {

struct FactorizationQuality {
    bool available{false};
    double minimum_absolute_pivot{0.0};
    double maximum_absolute_pivot{0.0};
    double reciprocal_pivot_ratio{0.0};
    std::size_t accepted_pivot_count{0};
    std::size_t factorization_size{0};
    // Backend-specific provenance. A pivot ratio is not a matrix condition
    // number and must not be presented as one.
    std::string method;
};

struct LinearSolveResult {
    bool success{false};
    std::vector<double> x;
    std::string message;
    int symbolic_factorizations{0};
    int numeric_factorizations{0};
    FactorizationQuality factorization_quality;

    LinearSolveResult() = default;
    LinearSolveResult(
        bool success_value,
        std::vector<double> solution,
        std::string message_value,
        int symbolic_count = 0,
        int numeric_count = 0,
        FactorizationQuality quality = {})
        : success(success_value),
          x(std::move(solution)),
          message(std::move(message_value)),
          symbolic_factorizations(symbolic_count),
          numeric_factorizations(numeric_count),
          factorization_quality(std::move(quality)) {}
};

struct MultipleLinearSolveResult {
    bool success{false};
    // One solution vector per supplied right-hand side.
    Matrix x;
    std::string message;
    int symbolic_factorizations{0};
    int numeric_factorizations{0};
    FactorizationQuality factorization_quality;

    MultipleLinearSolveResult() = default;
    MultipleLinearSolveResult(
        bool success_value,
        Matrix solutions,
        std::string message_value,
        int symbolic_count = 0,
        int numeric_count = 0,
        FactorizationQuality quality = {})
        : success(success_value),
          x(std::move(solutions)),
          message(std::move(message_value)),
          symbolic_factorizations(symbolic_count),
          numeric_factorizations(numeric_count),
          factorization_quality(std::move(quality)) {}
};

class SparseFactorization;

using LinearSolverFunction = std::function<LinearSolveResult(Matrix a, std::vector<double> b)>;
using SparseLinearSolverFunction =
    std::function<LinearSolveResult(SparseMatrix a, std::vector<double> b)>;
using SparseFactorizationPtr = std::shared_ptr<SparseFactorization>;
using SparseFactorizationResolver =
    std::function<SparseFactorizationPtr(const SparsePattern& pattern)>;

}  // namespace thermox
