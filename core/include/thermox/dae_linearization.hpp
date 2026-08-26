#pragma once

#include "thermox/transient_solver.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace thermox {

// Declares one algebraic variable as an exogenous small-signal input. The
// residual that normally fixes that variable is removed from the local
// response problem, preserving a square index-1 system.
struct DaeLinearizationInput {
    std::size_t variable{0};
    std::size_t fixed_residual{0};
    std::string name;
};

// Selects one native DAE variable as a small-signal output. Differential
// states and released inputs produce direct identity rows; algebraic outputs
// are recovered from the same tangent elimination used to form A and B.
struct DaeLinearizationOutput {
    std::size_t variable{0};
    std::string name;
};

struct DaeLinearizationOptions {
    double relative_perturbation{3.0e-4};
    double minimum_perturbation{1.0e-10};
};

struct DaeLinearizationDiagnostics {
    bool success{false};
    int residual_evaluations{0};
    int linear_right_hand_sides{0};
    double maximum_operating_residual{0.0};
    std::string message;
};

struct DaeLinearizationResult {
    // Native differential-state coordinates and declared input coordinates.
    // A = d(ydot_differential)/d(y_differential)
    // B = d(ydot_differential)/d(input)
    Matrix A;
    Matrix B;
    // C = d(output)/d(y_differential)
    // D = d(output)/d(input)
    Matrix C;
    Matrix D;
    std::vector<std::size_t> differential_state_indices;
    std::vector<std::string> differential_state_names;
    std::vector<std::size_t> input_indices;
    std::vector<std::string> input_names;
    std::vector<std::size_t> output_indices;
    std::vector<std::string> output_names;
    std::vector<double> operating_state;
    std::vector<double> operating_derivative;
    DaeLinearizationDiagnostics diagnostics;
};

struct DaeLinearizationResponseProbeOptions {
    double absolute_normalized_tolerance{1.0e-5};
    double relative_tolerance{1.0e-2};
    SolverOptions nonlinear_solver;
};

struct DaeLinearizationResponseProbeState {
    std::string state_name;
    double predicted_rate_change{0.0};
    double nonlinear_rate_change{0.0};
    double absolute_error{0.0};
    double normalized_absolute_error{0.0};
    double relative_error{0.0};
};

struct DaeLinearizationResponseProbeOutput {
    std::string output_name;
    double predicted_change{0.0};
    double nonlinear_change{0.0};
    double absolute_error{0.0};
    double normalized_absolute_error{0.0};
    double relative_error{0.0};
};

struct DaeLinearizationResponseProbeResult {
    bool success{false};
    bool passed{false};
    std::vector<double> state_perturbations;
    std::vector<double> input_perturbations;
    std::vector<DaeLinearizationResponseProbeState> states;
    std::vector<DaeLinearizationResponseProbeOutput> outputs;
    double maximum_normalized_absolute_error{0.0};
    double maximum_relative_error{0.0};
    SolverDiagnostics nonlinear_diagnostics;
    std::string message;
};

struct DaeLinearizationTrajectoryProbeOptions {
    double duration{1.0e-2};
    std::size_t sample_count{2};
    std::size_t linear_substeps_per_sample{10};
    double absolute_normalized_tolerance{1.0e-5};
    double relative_tolerance{1.0e-2};
    TimeIntegrationOptions nonlinear_integration;
};

struct DaeLinearizationTrajectoryProbeState {
    std::string state_name;
    double linear_change{0.0};
    double nonlinear_change{0.0};
    double absolute_error{0.0};
    double normalized_absolute_error{0.0};
    double relative_error{0.0};
};

struct DaeLinearizationTrajectoryProbeOutput {
    std::string output_name;
    double linear_change{0.0};
    double nonlinear_change{0.0};
    double absolute_error{0.0};
    double normalized_absolute_error{0.0};
    double relative_error{0.0};
};

struct DaeLinearizationTrajectoryProbeSample {
    double time{0.0};
    std::vector<DaeLinearizationTrajectoryProbeState> states;
    std::vector<DaeLinearizationTrajectoryProbeOutput> outputs;
};

struct DaeLinearizationTrajectoryProbeResult {
    bool success{false};
    bool passed{false};
    std::vector<double> state_perturbations;
    std::vector<double> input_perturbations;
    std::vector<DaeLinearizationTrajectoryProbeSample> samples;
    double maximum_normalized_absolute_error{0.0};
    double maximum_relative_error{0.0};
    TimeIntegrationDiagnostics nonlinear_diagnostics;
    std::string message;
};

// Linearize an initialized index-1 DAE by assembling the local DAE tangent
// system and eliminating algebraic variables and differential rates through
// one factorization. Input fixed-value residuals must be supplied explicitly.
DaeLinearizationResult linearize_index1_dae(
    const DaeProblem& problem,
    double time,
    const std::vector<double>& operating_state,
    const std::vector<double>& operating_derivative,
    const std::vector<DaeLinearizationInput>& inputs,
    const std::vector<DaeLinearizationOutput>& outputs,
    const DaeLinearizationOptions& options = {});

// Recompute a consistent nonlinear DAE response at a finite perturbation and
// compare its differential-state rate change with A*dx + B*du.
DaeLinearizationResponseProbeResult
validate_index1_dae_linearization_response(
    const DaeProblem& problem,
    double time,
    const DaeLinearizationResult& linearization,
    const std::vector<DaeLinearizationInput>& inputs,
    const std::vector<double>& state_perturbations,
    const std::vector<double>& input_perturbations,
    const DaeLinearizationResponseProbeOptions& options = {});

// Compare a finite-duration nonlinear DAE trajectory with the integrated
// local A/B model for fixed input and initial differential-state changes.
DaeLinearizationTrajectoryProbeResult
validate_index1_dae_linearization_trajectory(
    const DaeProblem& problem,
    double time,
    const DaeLinearizationResult& linearization,
    const std::vector<DaeLinearizationInput>& inputs,
    const std::vector<double>& state_perturbations,
    const std::vector<double>& input_perturbations,
    const DaeLinearizationTrajectoryProbeOptions& options = {});

}  // namespace thermox
