#include "thermox/service/serialization.hpp"
#include "thermox/service/simulation_service.hpp"
#include "thermox/service/performance_test.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void print_usage(std::ostream& out) {
    out << "Usage:\n"
        << "  thermox_cli solve --model <path> [--case <id>]"
           " [--continuation]"
           " [--structural-policy automatic|monolithic|blocks|tearing]"
           " [--format text|json]\n"
        << "  thermox_cli simulate --model <path> [--case <id>]"
           " --end-time <seconds>"
           " [--structural-policy automatic|monolithic|blocks|tearing]"
           " [--format text|json]\n"
        << "  thermox_cli performance-test --input <path>"
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
    std::string end_time_text;
    std::string format = "text";
    bool continuation = false;
    auto structural_policy = thermox::service::
        StructuralDecompositionPolicy::automatic;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--model" && i + 1 < argc) {
            model_path = argv[++i];
        } else if (arg == "--input" && i + 1 < argc) {
            input_path = argv[++i];
        } else if (arg == "--case" && i + 1 < argc) {
            case_id = argv[++i];
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
        command != "performance-test") {
        std::cerr << "Unknown command: " << command << "\n";
        print_usage(std::cerr);
        return 2;
    }
    if (command != "performance-test" && model_path.empty()) {
        std::cerr << "Missing required --model path\n";
        print_usage(std::cerr);
        return 2;
    }
    if (command == "performance-test" && input_path.empty()) {
        std::cerr << "Missing required --input path\n";
        print_usage(std::cerr);
        return 2;
    }
    if (format != "text" && format != "json") {
        std::cerr << "Unsupported format: " << format << "\n";
        return 2;
    }
    if (command == "solve" && !end_time_text.empty()) {
        std::cerr << "--end-time is only valid for simulate\n";
        return 2;
    }
    if (command == "simulate" && continuation) {
        std::cerr << "--continuation is only valid for solve\n";
        return 2;
    }
    if (command == "simulate" && end_time_text.empty()) {
        std::cerr << "Missing required --end-time for simulate\n";
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
        const std::string model_json = read_file(model_path);
        thermox::service::SimulationService service;
        if (command == "solve") {
            thermox::service::SteadySimulationRequest request;
            request.model_json = model_json;
            request.case_id = case_id;
            request.solver.continuation_enabled =
                continuation;
            request.solver.structural_decomposition_policy =
                structural_policy;
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

        thermox::service::TransientSimulationRequest request;
        request.model_json = model_json;
        request.case_id = case_id;
        request.solver.end_time =
            parse_positive_number(end_time_text, "--end-time");
        request.solver.nonlinear_solver
            .structural_decomposition_policy =
            structural_policy;
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
