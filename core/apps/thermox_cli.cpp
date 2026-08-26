#include "thermox/service/serialization.hpp"
#include "thermox/service/simulation_service.hpp"
#include "thermox/service/performance_test.hpp"
#include "thermox/service/engineering_study.hpp"
#include "thermox/service/artifact_declaration.hpp"
#include "thermox/service/iso2314_equivalent_cooling.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void print_usage(std::ostream& out) {
    out << "Usage:\n"
        << "  thermox_cli solve --model <path> [--case <id>]"
           " [--performance-map <path>]..."
           " [--continuation]"
           " [--continuation-initial-step <value>]"
           " [--continuation-minimum-step <value>]"
           " [--residual-tolerance <value>]"
           " [--structural-policy automatic|monolithic|blocks|tearing]"
           " [--globalization line_search|trust_region]"
           " [--trust-region-initial-radius <value>]"
           " [--trust-region-minimum-radius <value>]"
           " [--format text|json]\n"
        << "  thermox_cli simulate --model <path> [--case <id>]"
           " [--performance-map <path>]..."
           " --end-time <seconds>"
           " [--structural-policy automatic|monolithic|blocks|tearing]"
           " [--globalization line_search|trust_region]"
           " [--trust-region-initial-radius <value>]"
           " [--trust-region-minimum-radius <value>]"
           " [--format text|json]\n"
        << "  thermox_cli linearize --model <path> [--case <id>]"
           " --input-variable <graph-variable>..."
           " [--output-variable <graph-variable>]..."
           " [--performance-map <path>]..."
           " [--relative-perturbation <value>]"
           " [--verify-jacobian]"
           " [--jacobian-fd-epsilon <value>]"
           " [--jacobian-absolute-tolerance <value>]"
           " [--jacobian-relative-tolerance <value>]"
           " [--verify-nonlinear-response]"
           " [--nonlinear-response-perturbation <value>]..."
           " [--nonlinear-response-absolute-normalized-tolerance <value>]"
           " [--nonlinear-response-relative-tolerance <value>]"
           " [--verify-nonlinear-trajectory]"
           " [--nonlinear-trajectory-duration <seconds>]"
           " [--nonlinear-trajectory-perturbation <value>]..."
           " [--nonlinear-trajectory-relative-tolerance <value>]"
           " [--residual-tolerance <value>]"
           " [--structural-policy automatic|monolithic|blocks|tearing]"
           " [--globalization line_search|trust_region]"
           " [--trust-region-initial-radius <value>]"
           " [--trust-region-minimum-radius <value>]"
           " [--format text|json]\n"
        << "  thermox_cli performance-test --input <path>"
           " [--format text|json]\n"
        << "  thermox_cli study --input <path>"
           " [--format text|json]\n"
        << "  thermox_cli iso2314-equivalent-cooling --input <path>"
           " [--format text|json]\n"
        << "  thermox_cli calibrate --model <path>"
           " --calibration <id>"
           " [--max-iterations <count>]"
           " [--format text|json]\n"
        << "  thermox_cli reconcile --model <path>"
           " --reconciliation <id>"
           " [--profile-likelihood]"
           " [--profile-objective-increase <value>]"
           " [--profile-parameter <id>]"
           " [--joint-region-objective-increase <value>]"
           " [--joint-region-parameter <id>]"
           " [--mode hard-equalities|weighted-measurements]"
           " [--max-iterations <count>]"
           " [--format text|json]\n";
}

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open model file: " + path);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

double parse_positive_number(
    const std::string& text,
    const std::string& option) {
    std::size_t parsed = 0;
    const double value = std::stod(text, &parsed);
    if (parsed != text.size() || !std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(
            option + " must be a positive finite number");
    }
    return value;
}

void print_graph_text(
    const thermox::service::GraphResult& graph) {
    for (const auto& component : graph.components) {
        for (const auto& port : component.ports) {
            const std::string prefix =
                component.component_id + "." + port.port_name;
            for (const auto& value : port.primary_values) {
                std::cout << prefix << "." << value.name
                          << " = " << value.value_si << "\n";
            }
            for (const auto& value : port.derived_values) {
                std::cout << prefix << "." << value.name
                          << " = " << value.value_si << "\n";
            }
        }
        for (const auto& value : component.internal_values) {
            std::cout << component.component_id << "."
                      << value.name << " = " << value.value_si
                      << "\n";
        }
    }
}

void print_steady_text(
    const thermox::service::SteadySimulationResponse& response) {
    std::cout << "model: " << response.metadata.model.model_id << "\n"
              << "status: "
              << thermox::service::to_string(response.status) << "\n"
              << "converged: "
              << (response.diagnostics.converged ? "yes" : "no") << "\n"
              << "iterations: " << response.diagnostics.iterations << "\n";
    std::cout << "structural_block_solves: "
              << response.diagnostics.structural_block_solves << "\n"
              << "largest_linear_system_size: "
              << response.diagnostics.largest_linear_system_size
              << "\nstructural_tearing_attempts: "
              << response.diagnostics.structural_tearing_attempts
              << "\nstructural_tearing_successes: "
              << response.diagnostics.structural_tearing_successes
              << "\nstructural_tearing_fallbacks: "
              << response.diagnostics.structural_tearing_fallbacks
              << "\nminimum_reciprocal_pivot_ratio: "
              << response.diagnostics.minimum_reciprocal_pivot_ratio
              << "\nfactorization_quality_method: "
              << response.diagnostics.factorization_quality_method
              << "\n";
    if (response.continuation.enabled) {
        std::cout << "continuation_parameter: "
                  << response.continuation.reached_parameter
                  << "\n"
                  << "continuation_stages: "
                  << response.continuation.accepted_stages
                  << "\n";
    }
    if (!response.error.code.empty()) {
        std::cout << "error: " << response.error.message << "\n";
    }
    print_graph_text(response.graph);
}

void print_transient_text(
    const thermox::service::TransientSimulationResponse& response) {
    std::cout << "model: " << response.metadata.model.model_id << "\n"
              << "status: "
              << thermox::service::to_string(response.status) << "\n"
              << "success: "
              << (response.diagnostics.success ? "yes" : "no") << "\n"
              << "final_time: " << response.diagnostics.final_time << "\n"
              << "accepted_steps: "
              << response.diagnostics.accepted_steps << "\n"
              << "structural_block_solves: "
              << response.diagnostics.structural_block_solves << "\n"
              << "largest_linear_system_size: "
              << response.diagnostics.largest_linear_system_size
              << "\nstructural_tearing_attempts: "
              << response.diagnostics.structural_tearing_attempts
              << "\nstructural_tearing_successes: "
              << response.diagnostics.structural_tearing_successes
              << "\nstructural_tearing_fallbacks: "
              << response.diagnostics.structural_tearing_fallbacks
              << "\nminimum_reciprocal_pivot_ratio: "
              << response.diagnostics.minimum_reciprocal_pivot_ratio
              << "\nfactorization_quality_method: "
              << response.diagnostics.factorization_quality_method
              << "\n";
    if (!response.error.code.empty()) {
        std::cout << "error: " << response.error.message << "\n";
    }
    if (response.trajectory.empty()) return;
    print_graph_text(response.trajectory.back().graph);
}

void print_small_signal_text(
    const thermox::service::SmallSignalLinearizationResponse& response) {
    std::cout << "model: " << response.metadata.model.model_id << "\n"
              << "status: "
              << thermox::service::to_string(response.status) << "\n"
              << "success: "
              << (response.diagnostics.success ? "yes" : "no") << "\n"
              << "residual_evaluations: "
              << response.diagnostics.residual_evaluations << "\n"
              << "linear_right_hand_sides: "
              << response.diagnostics.linear_right_hand_sides << "\n";
    if (response.jacobian_verification
            .analytic_derivatives_available) {
        std::cout << "jacobian_verification: "
                  << (response.jacobian_verification.passed
                          ? "passed" : "failed")
                  << "\nstate_jacobian_mismatches: "
                  << response.jacobian_verification
                         .state_jacobian.mismatch_count
                  << "\nderivative_jacobian_mismatches: "
                  << response.jacobian_verification
                         .derivative_jacobian.mismatch_count
                  << "\n";
    }
    if (!response.nonlinear_response_probes.empty()) {
        double maximum_normalized_absolute_error = 0.0;
        for (const auto& probe :
             response.nonlinear_response_probes) {
            maximum_normalized_absolute_error = std::max(
                maximum_normalized_absolute_error,
                probe.maximum_normalized_absolute_error);
        }
        std::cout << "nonlinear_response_probes: "
                  << response.nonlinear_response_probes.size()
                  << "\nnonlinear_response_envelope_levels: "
                  << response.nonlinear_response_envelope.size()
                  << "\nnonlinear_response_maximum_normalized_absolute_error: "
                  << maximum_normalized_absolute_error << "\n";
    }
    if (!response.nonlinear_trajectory_probes.empty()) {
        double maximum_normalized_absolute_error = 0.0;
        for (const auto& probe :
             response.nonlinear_trajectory_probes) {
            maximum_normalized_absolute_error = std::max(
                maximum_normalized_absolute_error,
                probe.maximum_normalized_absolute_error);
        }
        std::cout << "nonlinear_trajectory_probes: "
                  << response.nonlinear_trajectory_probes.size()
                  << "\nnonlinear_trajectory_envelope_levels: "
                  << response.nonlinear_trajectory_envelope.size()
                  << "\nnonlinear_trajectory_maximum_normalized_absolute_error: "
                  << maximum_normalized_absolute_error << "\n";
    }
    if (!response.error.code.empty()) {
        std::cout << "error: " << response.error.message << "\n";
        return;
    }
    const auto print_matrix = [](
        const std::string& name,
        const auto& matrix) {
        std::cout << name << ":\n";
        for (const auto& row : matrix) {
            for (std::size_t column = 0; column < row.size(); ++column) {
                if (column != 0U) std::cout << ' ';
                std::cout << row[column];
            }
            std::cout << '\n';
        }
    };
    std::cout << "states:";
    for (const auto& name : response.state_names) std::cout << ' ' << name;
    std::cout << "\ninputs:";
    for (const auto& name : response.input_names) std::cout << ' ' << name;
    std::cout << "\noutputs:";
    for (const auto& name : response.output_names) std::cout << ' ' << name;
    std::cout << '\n';
    print_matrix("A", response.A);
    print_matrix("B", response.B);
    print_matrix("C", response.C);
    print_matrix("D", response.D);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 1) {
        print_usage(std::cerr);
        return 2;
    }

    const std::string command = argv[1];
    std::string model_path;
    std::string input_path;
    std::string case_id;
    std::string calibration_id;
    std::string reconciliation_id;
    std::string max_iterations_text;
    std::string residual_tolerance_text;
    std::string continuation_initial_step_text;
    std::string continuation_minimum_step_text;
    std::string trust_region_initial_radius_text;
    std::string trust_region_minimum_radius_text;
    std::string reconciliation_mode = "hard-equalities";
    std::string end_time_text;
    std::string relative_perturbation_text;
    std::string jacobian_fd_epsilon_text;
    std::string jacobian_absolute_tolerance_text;
    std::string jacobian_relative_tolerance_text;
    std::vector<std::string> nonlinear_response_perturbation_texts;
    std::string nonlinear_response_absolute_tolerance_text;
    std::string nonlinear_response_relative_tolerance_text;
    std::string nonlinear_trajectory_duration_text;
    std::vector<std::string> nonlinear_trajectory_perturbation_texts;
    std::string nonlinear_trajectory_relative_tolerance_text;
    std::string format = "text";
    std::vector<std::string> performance_map_paths;
    std::vector<std::string> input_variables;
    std::vector<std::string> output_variables;
    bool continuation = false;
    bool verify_jacobian = false;
    bool verify_nonlinear_response = false;
    bool verify_nonlinear_trajectory = false;
    bool profile_likelihood = false;
    std::string profile_objective_increase_text;
    std::vector<std::string> profile_parameter_ids;
    std::string joint_region_objective_increase_text;
    std::vector<std::string> joint_region_parameter_ids;
    auto structural_policy = thermox::service::
        StructuralDecompositionPolicy::automatic;
    auto globalization_policy = thermox::service::
        GlobalizationPolicy::line_search;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--model" && i + 1 < argc) {
            model_path = argv[++i];
        } else if (arg == "--input" && i + 1 < argc) {
            input_path = argv[++i];
        } else if (arg == "--case" && i + 1 < argc) {
            case_id = argv[++i];
        } else if (arg == "--performance-map" && i + 1 < argc) {
            performance_map_paths.emplace_back(argv[++i]);
        } else if (arg == "--input-variable" && i + 1 < argc) {
            input_variables.emplace_back(argv[++i]);
        } else if (arg == "--output-variable" && i + 1 < argc) {
            output_variables.emplace_back(argv[++i]);
        } else if (arg == "--relative-perturbation" && i + 1 < argc) {
            relative_perturbation_text = argv[++i];
        } else if (arg == "--verify-jacobian") {
            verify_jacobian = true;
        } else if (arg == "--jacobian-fd-epsilon" && i + 1 < argc) {
            jacobian_fd_epsilon_text = argv[++i];
        } else if (arg == "--jacobian-absolute-tolerance" &&
                   i + 1 < argc) {
            jacobian_absolute_tolerance_text = argv[++i];
        } else if (arg == "--jacobian-relative-tolerance" &&
                   i + 1 < argc) {
            jacobian_relative_tolerance_text = argv[++i];
        } else if (arg == "--verify-nonlinear-response") {
            verify_nonlinear_response = true;
        } else if (arg == "--nonlinear-response-perturbation" &&
                   i + 1 < argc) {
            nonlinear_response_perturbation_texts.emplace_back(argv[++i]);
        } else if (
            arg == "--nonlinear-response-absolute-normalized-tolerance" &&
            i + 1 < argc) {
            nonlinear_response_absolute_tolerance_text = argv[++i];
        } else if (arg == "--nonlinear-response-relative-tolerance" &&
                   i + 1 < argc) {
            nonlinear_response_relative_tolerance_text = argv[++i];
        } else if (arg == "--verify-nonlinear-trajectory") {
            verify_nonlinear_trajectory = true;
        } else if (arg == "--nonlinear-trajectory-duration" &&
                   i + 1 < argc) {
            nonlinear_trajectory_duration_text = argv[++i];
        } else if (arg == "--nonlinear-trajectory-perturbation" &&
                   i + 1 < argc) {
            nonlinear_trajectory_perturbation_texts.emplace_back(argv[++i]);
        } else if (arg == "--nonlinear-trajectory-relative-tolerance" &&
                   i + 1 < argc) {
            nonlinear_trajectory_relative_tolerance_text = argv[++i];
        } else if (arg == "--calibration" && i + 1 < argc) {
            calibration_id = argv[++i];
        } else if (arg == "--reconciliation" && i + 1 < argc) {
            reconciliation_id = argv[++i];
        } else if (arg == "--max-iterations" && i + 1 < argc) {
            max_iterations_text = argv[++i];
        } else if (arg == "--residual-tolerance" && i + 1 < argc) {
            residual_tolerance_text = argv[++i];
        } else if (arg == "--continuation-initial-step" &&
                   i + 1 < argc) {
            continuation_initial_step_text = argv[++i];
        } else if (arg == "--continuation-minimum-step" &&
                   i + 1 < argc) {
            continuation_minimum_step_text = argv[++i];
        } else if (arg == "--trust-region-initial-radius" &&
                   i + 1 < argc) {
            trust_region_initial_radius_text = argv[++i];
        } else if (arg == "--trust-region-minimum-radius" &&
                   i + 1 < argc) {
            trust_region_minimum_radius_text = argv[++i];
        } else if (arg == "--mode" && i + 1 < argc) {
            reconciliation_mode = argv[++i];
        } else if (arg == "--profile-likelihood") {
            profile_likelihood = true;
        } else if (arg == "--profile-objective-increase" &&
                   i + 1 < argc) {
            profile_objective_increase_text = argv[++i];
        } else if (arg == "--profile-parameter" && i + 1 < argc) {
            profile_parameter_ids.emplace_back(argv[++i]);
        } else if (arg == "--joint-region-objective-increase" &&
                   i + 1 < argc) {
            joint_region_objective_increase_text = argv[++i];
        } else if (arg == "--joint-region-parameter" &&
                   i + 1 < argc) {
            joint_region_parameter_ids.emplace_back(argv[++i]);
        } else if (arg == "--end-time" && i + 1 < argc) {
            end_time_text = argv[++i];
        } else if (arg == "--format" && i + 1 < argc) {
            format = argv[++i];
        } else if (arg == "--continuation") {
            continuation = true;
        } else if (arg == "--structural-policy" &&
                   i + 1 < argc) {
            structural_policy = thermox::service::
                structural_decomposition_policy_from_string(
                    argv[++i]);
        } else if (arg == "--globalization" &&
                   i + 1 < argc) {
            globalization_policy = thermox::service::
                globalization_policy_from_string(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            print_usage(std::cout);
            return 0;
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << "\n";
            print_usage(std::cerr);
            return 2;
        }
    }

    if (command != "solve" && command != "simulate" &&
        command != "linearize" &&
        command != "performance-test" && command != "study" &&
        command != "iso2314-equivalent-cooling" &&
        command != "calibrate" &&
        command != "reconcile") {
        std::cerr << "Unknown command: " << command << "\n";
        print_usage(std::cerr);
        return 2;
    }
    if (command != "performance-test" && command != "study" &&
        command != "iso2314-equivalent-cooling" &&
        model_path.empty()) {
        std::cerr << "Missing required --model path\n";
        print_usage(std::cerr);
        return 2;
    }
    if (command == "calibrate" && calibration_id.empty()) {
        std::cerr << "Missing required --calibration id\n";
        print_usage(std::cerr);
        return 2;
    }
    if (command == "reconcile" && reconciliation_id.empty()) {
        std::cerr << "Missing required --reconciliation id\n";
        print_usage(std::cerr);
        return 2;
    }
    if ((command == "performance-test" || command == "study" ||
         command == "iso2314-equivalent-cooling") &&
        input_path.empty()) {
        std::cerr << "Missing required --input path\n";
        print_usage(std::cerr);
        return 2;
    }
    if (format != "text" && format != "json") {
        std::cerr << "Unsupported format: " << format << "\n";
        return 2;
    }
    if (command != "simulate" && !end_time_text.empty()) {
        std::cerr << "--end-time is only valid for simulate\n";
        return 2;
    }
    if (command != "solve" && continuation) {
        std::cerr << "--continuation is only valid for solve\n";
        return 2;
    }
    if (command != "solve" && command != "simulate" &&
        command != "linearize" &&
        !performance_map_paths.empty()) {
        std::cerr << "--performance-map is only valid for solve or "
                     "simulate or linearize\n";
        return 2;
    }
    if (command != "solve" && command != "linearize" &&
        !residual_tolerance_text.empty()) {
        std::cerr << "--residual-tolerance is only valid for solve or "
                     "linearize\n";
        return 2;
    }
    if (command != "solve" &&
        (!continuation_initial_step_text.empty() ||
         !continuation_minimum_step_text.empty())) {
        std::cerr << "continuation step options are only valid for solve\n";
        return 2;
    }
    if (command == "simulate" && end_time_text.empty()) {
        std::cerr << "Missing required --end-time for simulate\n";
        return 2;
    }
    if (command == "linearize" && input_variables.empty()) {
        std::cerr << "Missing required --input-variable for linearize\n";
        return 2;
    }
    if (command != "linearize" && !input_variables.empty()) {
        std::cerr << "--input-variable is only valid for linearize\n";
        return 2;
    }
    if (command != "linearize" && !output_variables.empty()) {
        std::cerr << "--output-variable is only valid for linearize\n";
        return 2;
    }
    if (command != "linearize" && !relative_perturbation_text.empty()) {
        std::cerr << "--relative-perturbation is only valid for linearize\n";
        return 2;
    }
    if (command != "linearize" && verify_jacobian) {
        std::cerr << "--verify-jacobian is only valid for linearize\n";
        return 2;
    }
    if (command != "linearize" &&
        (!jacobian_fd_epsilon_text.empty() ||
         !jacobian_absolute_tolerance_text.empty() ||
         !jacobian_relative_tolerance_text.empty())) {
        std::cerr << "Jacobian verification tolerances are only valid "
                     "for linearize\n";
        return 2;
    }
    if (command == "linearize" && !verify_jacobian &&
        (!jacobian_fd_epsilon_text.empty() ||
         !jacobian_absolute_tolerance_text.empty() ||
         !jacobian_relative_tolerance_text.empty())) {
        std::cerr << "Jacobian verification tolerances require "
                     "--verify-jacobian\n";
        return 2;
    }
    if (command != "linearize" && verify_nonlinear_response) {
        std::cerr << "--verify-nonlinear-response is only valid for "
                     "linearize\n";
        return 2;
    }
    if (command != "linearize" &&
        (!nonlinear_response_perturbation_texts.empty() ||
         !nonlinear_response_absolute_tolerance_text.empty() ||
         !nonlinear_response_relative_tolerance_text.empty())) {
        std::cerr << "Nonlinear response options are only valid for "
                     "linearize\n";
        return 2;
    }
    if (command == "linearize" && !verify_nonlinear_response &&
        (!nonlinear_response_perturbation_texts.empty() ||
         !nonlinear_response_absolute_tolerance_text.empty() ||
         !nonlinear_response_relative_tolerance_text.empty())) {
        std::cerr << "Nonlinear response options require "
                     "--verify-nonlinear-response\n";
        return 2;
    }
    if (command != "linearize" && verify_nonlinear_trajectory) {
        std::cerr << "--verify-nonlinear-trajectory is only valid for "
                     "linearize\n";
        return 2;
    }
    if (command != "linearize" &&
        (!nonlinear_trajectory_duration_text.empty() ||
         !nonlinear_trajectory_perturbation_texts.empty() ||
         !nonlinear_trajectory_relative_tolerance_text.empty())) {
        std::cerr << "Nonlinear trajectory options are only valid for "
                     "linearize\n";
        return 2;
    }
    if (command == "linearize" && !verify_nonlinear_trajectory &&
        (!nonlinear_trajectory_duration_text.empty() ||
         !nonlinear_trajectory_perturbation_texts.empty() ||
         !nonlinear_trajectory_relative_tolerance_text.empty())) {
        std::cerr << "Nonlinear trajectory options require "
                     "--verify-nonlinear-trajectory\n";
        return 2;
    }

    try {
        if (command == "performance-test") {
            const auto response = thermox::service::
                evaluate_gas_turbine_performance_test_json(
                    read_file(input_path));
            if (format == "json") {
                std::cout << thermox::service::
                    serialize_gas_turbine_performance_test_result_json(
                        response);
            } else {
                std::cout
                    << "campaign: " << response.id << "\n"
                    << "equipment: " << response.equipment_id << "\n"
                    << "standard: " << response.standard_reference << "\n"
                    << "iso_conformity_demonstrated: "
                    << (response.iso_conformity_demonstrated ? "yes" : "no")
                    << "\n"
                    << "average_corrected_net_generator_power_w: "
                    << response.average_corrected_net_generator_power_w
                    << "\n"
                    << "average_corrected_heat_rate_j_per_kwh: "
                    << response.average_corrected_heat_rate_j_per_kwh
                    << "\n";
            }
            return 0;
        }
        if (command == "study") {
            const auto response = thermox::service::
                evaluate_engineering_study_json(read_file(input_path));
            if (format == "json") {
                std::cout << thermox::service::
                    serialize_engineering_study_response_json(response);
            } else {
                std::cout << "status: "
                          << thermox::service::to_string(response.status)
                          << "\nprediction_cases: "
                          << response.diagnostics.prediction_case_count
                          << "\nrms_normalized_residual: "
                          << response.diagnostics.rms_normalized_residual
                          << "\nmaximum_absolute_normalized_residual: "
                          << response.diagnostics
                                 .maximum_absolute_normalized_residual
                          << "\n";
                for (const auto& prediction : response.predictions) {
                    for (const auto& observation :
                         prediction.observations) {
                        std::cout << prediction.case_id << "."
                                  << observation.id << " predicted="
                                  << observation.predicted_si
                                  << " measured="
                                  << observation.measured_si
                                  << " residual="
                                  << observation.residual_si << "\n";
                    }
                }
            }
            return response.succeeded() ? 0 : 1;
        }
        if (command == "iso2314-equivalent-cooling") {
            const auto response = thermox::service::
                evaluate_iso2314_equivalent_cooling_json(
                    read_file(input_path));
            if (format == "json") {
                std::cout << thermox::service::
                    serialize_iso2314_equivalent_cooling_response_json(
                        response);
            } else {
                std::cout << "id: " << response.id
                          << "\ndetermination: "
                          << thermox::physics::to_string(
                                 response.calculation.determination)
                          << "\nequivalent_compressor_mass_flow_kg_s: "
                          << response.calculation
                                 .equivalent_compressor_mass_flow_kg_s
                          << "\nequivalent_extraction_mass_flow_kg_s: "
                          << response.calculation
                                 .equivalent_extraction_mass_flow_kg_s
                          << "\nrelative_equivalent_flow_difference_md: "
                          << response.calculation
                                 .relative_equivalent_flow_difference_md
                          << "\nequivalent_extraction_energy_w: "
                          << response.calculation
                                 .equivalent_extraction_energy_w
                          << "\n";
            }
            return 0;
        }
        const std::string model_json = read_file(model_path);
        thermox::service::SimulationService service;
        if (command == "reconcile") {
            thermox::service::DataReconciliationRequest request;
            request.model_json = model_json;
            request.reconciliation_id = reconciliation_id;
            if (reconciliation_mode == "hard-equalities") {
                request.mode = thermox::service::
                    ReconciliationMode::hard_equalities;
            } else if (reconciliation_mode ==
                       "weighted-measurements") {
                request.mode = thermox::service::
                    ReconciliationMode::weighted_measurements;
            } else {
                throw std::invalid_argument(
                    "--mode must be hard-equalities or "
                    "weighted-measurements");
            }
            if (!max_iterations_text.empty()) {
                const double count = parse_positive_number(
                    max_iterations_text, "--max-iterations");
                if (std::floor(count) != count || count > 1000.0) {
                    throw std::invalid_argument(
                        "--max-iterations must be an integer no greater "
                        "than 1000");
                }
                request.solver.max_iterations =
                    static_cast<int>(count);
            }
            request.profile_likelihood.enabled = profile_likelihood;
            request.profile_likelihood.parameter_ids =
                profile_parameter_ids;
            if (!profile_parameter_ids.empty()) {
                request.profile_likelihood.enabled = true;
            }
            if (!profile_objective_increase_text.empty()) {
                request.profile_likelihood.objective_increase =
                    parse_positive_number(
                        profile_objective_increase_text,
                        "--profile-objective-increase");
                request.profile_likelihood.enabled = true;
            }
            request.joint_confidence_region.parameter_ids =
                joint_region_parameter_ids;
            if (!joint_region_objective_increase_text.empty()) {
                request.joint_confidence_region.objective_increase =
                    parse_positive_number(
                        joint_region_objective_increase_text,
                        "--joint-region-objective-increase");
                request.joint_confidence_region.enabled = true;
            } else if (!joint_region_parameter_ids.empty()) {
                throw std::invalid_argument(
                    "--joint-region-parameter requires an explicit "
                    "--joint-region-objective-increase");
            }
            const auto response =
                service.run_data_reconciliation(request);
            if (format == "json") {
                std::cout << thermox::service::
                    serialize_data_reconciliation_response_json(response);
            } else {
                std::cout << "reconciliation: "
                          << response.reconciliation_id << "\n"
                          << "status: "
                          << thermox::service::to_string(response.status)
                          << "\nconverged: "
                          << (response.diagnostics.converged
                                  ? "yes" : "no")
                          << "\n";
                for (const auto& parameter :
                     response.inferred_parameters) {
                    std::cout << parameter.id << " = "
                              << parameter.fitted_value_si << "\n";
                }
            }
            return response.succeeded() ? 0 : 1;
        }
        if (command == "calibrate") {
            thermox::service::CalibrationRequest request;
            request.model_json = model_json;
            request.calibration_id = calibration_id;
            if (!max_iterations_text.empty()) {
                const double count = parse_positive_number(
                    max_iterations_text, "--max-iterations");
                if (std::floor(count) != count || count > 1000.0) {
                    throw std::invalid_argument(
                        "--max-iterations must be an integer no greater "
                        "than 1000");
                }
                request.solver.max_iterations =
                    static_cast<int>(count);
            }
            const auto response = service.run_calibration(request);
            if (format == "json") {
                std::cout << thermox::service::
                    serialize_calibration_response_json(response);
            } else {
                std::cout << "calibration: "
                          << response.calibration_id << "\n"
                          << "status: "
                          << thermox::service::to_string(response.status)
                          << "\nobjective: "
                          << response.diagnostics.final_objective << "\n";
                for (const auto& parameter : response.parameters) {
                    std::cout << parameter.id << " = "
                              << parameter.fitted_value_si << "\n";
                }
            }
            return response.succeeded() ? 0 : 1;
        }
        if (command == "solve") {
            thermox::service::SteadySimulationRequest request;
            request.model_json = model_json;
            request.case_id = case_id;
            for (const auto& path : performance_map_paths) {
                request.artifacts.performance_maps.push_back(
                    thermox::service::
                        parse_performance_map_artifact_declaration_json(
                            read_file(path)));
            }
            request.solver.continuation_enabled =
                continuation;
            if (!continuation_initial_step_text.empty()) {
                request.solver.continuation_initial_step =
                    parse_positive_number(
                        continuation_initial_step_text,
                        "--continuation-initial-step");
            }
            if (!continuation_minimum_step_text.empty()) {
                request.solver.continuation_minimum_step =
                    parse_positive_number(
                        continuation_minimum_step_text,
                        "--continuation-minimum-step");
            }
            if (!residual_tolerance_text.empty()) {
                request.solver.residual_tolerance =
                    parse_positive_number(
                        residual_tolerance_text,
                        "--residual-tolerance");
            }
            request.solver.structural_decomposition_policy =
                structural_policy;
            request.solver.globalization_policy =
                globalization_policy;
            if (!trust_region_initial_radius_text.empty()) {
                request.solver.trust_region_initial_radius =
                    parse_positive_number(
                        trust_region_initial_radius_text,
                        "--trust-region-initial-radius");
                request.solver.trust_region_maximum_radius =
                    std::max(
                        request.solver.trust_region_maximum_radius,
                        request.solver.trust_region_initial_radius);
            }
            if (!trust_region_minimum_radius_text.empty()) {
                request.solver.trust_region_minimum_radius =
                    parse_positive_number(
                        trust_region_minimum_radius_text,
                        "--trust-region-minimum-radius");
            }
            const auto response = service.run_steady(request);
            if (format == "json") {
                std::cout <<
                    thermox::service::serialize_steady_response_json(
                        response);
            } else {
                print_steady_text(response);
            }
            return response.succeeded() ? 0 : 1;
        }

        if (command == "linearize") {
            thermox::service::SmallSignalLinearizationRequest request;
            request.model_json = model_json;
            request.case_id = case_id;
            request.input_variables = input_variables;
            request.output_variables = output_variables;
            request.settings.verify_jacobian = verify_jacobian;
            request.settings.verify_nonlinear_response =
                verify_nonlinear_response;
            request.settings.verify_nonlinear_trajectory =
                verify_nonlinear_trajectory;
            if (!jacobian_fd_epsilon_text.empty()) {
                request.settings.jacobian_verification
                    .finite_difference_epsilon = parse_positive_number(
                        jacobian_fd_epsilon_text,
                        "--jacobian-fd-epsilon");
            }
            if (!jacobian_absolute_tolerance_text.empty()) {
                request.settings.jacobian_verification
                    .absolute_tolerance = parse_positive_number(
                        jacobian_absolute_tolerance_text,
                        "--jacobian-absolute-tolerance");
            }
            if (!jacobian_relative_tolerance_text.empty()) {
                request.settings.jacobian_verification
                    .relative_tolerance = parse_positive_number(
                        jacobian_relative_tolerance_text,
                        "--jacobian-relative-tolerance");
            }
            if (!nonlinear_response_perturbation_texts.empty()) {
                request.settings
                    .nonlinear_response_relative_perturbations.clear();
                for (const auto& value :
                     nonlinear_response_perturbation_texts) {
                    request.settings
                        .nonlinear_response_relative_perturbations
                        .push_back(parse_positive_number(
                            value,
                            "--nonlinear-response-perturbation"));
                }
            }
            if (!nonlinear_response_absolute_tolerance_text.empty()) {
                request.settings
                    .nonlinear_response_absolute_normalized_tolerance =
                    parse_positive_number(
                        nonlinear_response_absolute_tolerance_text,
                        "--nonlinear-response-absolute-normalized-tolerance");
            }
            if (!nonlinear_response_relative_tolerance_text.empty()) {
                request.settings
                    .nonlinear_response_relative_tolerance =
                    parse_positive_number(
                        nonlinear_response_relative_tolerance_text,
                        "--nonlinear-response-relative-tolerance");
            }
            if (!nonlinear_trajectory_duration_text.empty()) {
                request.settings.nonlinear_trajectory_duration =
                    parse_positive_number(
                        nonlinear_trajectory_duration_text,
                        "--nonlinear-trajectory-duration");
            }
            if (!nonlinear_trajectory_perturbation_texts.empty()) {
                request.settings
                    .nonlinear_trajectory_relative_perturbations.clear();
                for (const auto& value :
                     nonlinear_trajectory_perturbation_texts) {
                    request.settings
                        .nonlinear_trajectory_relative_perturbations
                        .push_back(parse_positive_number(
                            value,
                            "--nonlinear-trajectory-perturbation"));
                }
            }
            if (!nonlinear_trajectory_relative_tolerance_text.empty()) {
                request.settings
                    .nonlinear_trajectory_relative_tolerance =
                    parse_positive_number(
                        nonlinear_trajectory_relative_tolerance_text,
                        "--nonlinear-trajectory-relative-tolerance");
            }
            for (const auto& path : performance_map_paths) {
                request.artifacts.performance_maps.push_back(
                    thermox::service::
                        parse_performance_map_artifact_declaration_json(
                            read_file(path)));
            }
            if (!relative_perturbation_text.empty()) {
                request.settings.relative_perturbation =
                    parse_positive_number(
                        relative_perturbation_text,
                        "--relative-perturbation");
            }
            if (!residual_tolerance_text.empty()) {
                request.settings.nonlinear_solver.residual_tolerance =
                    parse_positive_number(
                        residual_tolerance_text,
                        "--residual-tolerance");
            }
            request.settings.nonlinear_solver
                .structural_decomposition_policy = structural_policy;
            request.settings.nonlinear_solver.globalization_policy =
                globalization_policy;
            if (!trust_region_initial_radius_text.empty()) {
                request.settings.nonlinear_solver
                    .trust_region_initial_radius = parse_positive_number(
                        trust_region_initial_radius_text,
                        "--trust-region-initial-radius");
                request.settings.nonlinear_solver
                    .trust_region_maximum_radius = std::max(
                        request.settings.nonlinear_solver
                            .trust_region_maximum_radius,
                        request.settings.nonlinear_solver
                            .trust_region_initial_radius);
            }
            if (!trust_region_minimum_radius_text.empty()) {
                request.settings.nonlinear_solver
                    .trust_region_minimum_radius = parse_positive_number(
                        trust_region_minimum_radius_text,
                        "--trust-region-minimum-radius");
            }
            const auto response =
                service.run_small_signal_linearization(request);
            if (format == "json") {
                std::cout << thermox::service::
                    serialize_small_signal_linearization_response_json(
                        response);
            } else {
                print_small_signal_text(response);
            }
            return response.succeeded() ? 0 : 1;
        }

        thermox::service::TransientSimulationRequest request;
        request.model_json = model_json;
        request.case_id = case_id;
        for (const auto& path : performance_map_paths) {
            request.artifacts.performance_maps.push_back(
                thermox::service::
                    parse_performance_map_artifact_declaration_json(
                        read_file(path)));
        }
        request.solver.end_time =
            parse_positive_number(end_time_text, "--end-time");
        request.solver.nonlinear_solver
            .structural_decomposition_policy =
            structural_policy;
        request.solver.nonlinear_solver.globalization_policy =
            globalization_policy;
        if (!trust_region_initial_radius_text.empty()) {
            request.solver.nonlinear_solver
                .trust_region_initial_radius =
                parse_positive_number(
                    trust_region_initial_radius_text,
                    "--trust-region-initial-radius");
            request.solver.nonlinear_solver
                .trust_region_maximum_radius = std::max(
                    request.solver.nonlinear_solver
                        .trust_region_maximum_radius,
                    request.solver.nonlinear_solver
                        .trust_region_initial_radius);
        }
        if (!trust_region_minimum_radius_text.empty()) {
            request.solver.nonlinear_solver
                .trust_region_minimum_radius =
                parse_positive_number(
                    trust_region_minimum_radius_text,
                    "--trust-region-minimum-radius");
        }
        const auto response = service.run_transient(request);
        if (format == "json") {
            std::cout <<
                thermox::service::serialize_transient_response_json(
                    response);
        } else {
            print_transient_text(response);
        }
        return response.succeeded() ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "thermox_cli error: " << ex.what() << "\n";
        return 1;
    }
}
