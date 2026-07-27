#include "thermox/nonlinear_solver.hpp"
#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/model_document.hpp"
#include "thermox/platform/results.hpp"
#include "thermox/transient_solver.hpp"

#include <cmath>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage(std::ostream& out) {
    out << "Usage:\n"
        << "  thermox_cli solve --model <path> [--case <id>] [--format text|json]\n"
        << "  thermox_cli simulate --model <path> [--case <id>] --end-time <seconds>"
           " [--format text|json]\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 1) {
        print_usage(std::cerr);
        return 2;
    }

    std::string command;
    std::string model_path;
    std::string case_id;
    std::string end_time_text;
    std::string format = "text";

    command = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--model" && i + 1 < argc) {
            model_path = argv[++i];
        } else if (arg == "--case" && i + 1 < argc) {
            case_id = argv[++i];
        } else if (arg == "--end-time" && i + 1 < argc) {
            end_time_text = argv[++i];
        } else if (arg == "--format" && i + 1 < argc) {
            format = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            print_usage(std::cout);
            return 0;
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << "\n";
            print_usage(std::cerr);
            return 2;
        }
    }

    if (command != "solve" && command != "simulate") {
        std::cerr << "Unknown command: " << command << "\n";
        print_usage(std::cerr);
        return 2;
    }
    if (model_path.empty()) {
        std::cerr << "Missing required --model path\n";
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
    if (command == "simulate" && end_time_text.empty()) {
        std::cerr << "Missing required --end-time for simulate\n";
        return 2;
    }

    try {
        const auto document = thermox::platform::load_model_document(model_path);
        const auto components = thermox::platform::make_default_component_registry();
        if (command == "solve") {
            const auto graph = thermox::platform::compile_model_graph(
                document, components, case_id);
            thermox::SolverOptions options;
            options.residual_tolerance = 1.0e-10;
            const auto result = thermox::solve_newton(graph.problem, options);
            const auto fluid_ports =
                result.diagnostics.converged
                    ? thermox::platform::evaluate_fluid_port_results(
                          document, graph, result.x)
                    : std::vector<thermox::platform::FluidPortResult>{};

            if (format == "json") {
                std::cout << "{\n"
                          << "  \"model_id\": \"" << graph.model_id << "\",\n"
                          << "  \"converged\": "
                          << (result.diagnostics.converged ? "true" : "false") << ",\n"
                          << "  \"iterations\": " << result.diagnostics.iterations << ",\n"
                          << "  \"variables\": {\n";
                for (std::size_t i = 0; i < graph.problem.variable_names.size(); ++i) {
                    std::cout << "    \"" << graph.problem.variable_names[i] << "\": "
                              << result.x[i]
                              << (i + 1 == graph.problem.variable_names.size() ? "\n" : ",\n");
                }
                std::cout << "  },\n"
                          << "  \"fluid_ports\": {\n";
                for (std::size_t i = 0; i < fluid_ports.size(); ++i) {
                    const auto& port = fluid_ports[i];
                    std::cout
                        << "    \"" << port.component_id << "."
                        << port.port_name << "\": {"
                        << "\"T\": " << port.state.temperature_k
                        << ", \"rho\": " << port.state.density_kg_m3
                        << ", \"s\": " << port.state.entropy_j_kg_k
                        << ", \"quality\": " << port.state.vapor_quality
                        << "}"
                        << (i + 1 == fluid_ports.size() ? "\n" : ",\n");
                }
                std::cout << "  }\n}\n";
            } else {
                std::cout << "model: " << graph.model_id << "\n"
                          << "converged: "
                          << (result.diagnostics.converged ? "yes" : "no") << "\n"
                          << "iterations: " << result.diagnostics.iterations << "\n";
                for (std::size_t i = 0; i < graph.problem.variable_names.size(); ++i) {
                    std::cout << graph.problem.variable_names[i] << " = "
                              << result.x[i] << "\n";
                }
                for (const auto& port : fluid_ports) {
                    const std::string prefix =
                        port.component_id + "." + port.port_name;
                    std::cout << prefix << ".T = "
                              << port.state.temperature_k << "\n"
                              << prefix << ".rho = "
                              << port.state.density_kg_m3 << "\n"
                              << prefix << ".s = "
                              << port.state.entropy_j_kg_k << "\n"
                              << prefix << ".quality = "
                              << port.state.vapor_quality << "\n";
                }
            }
            return result.diagnostics.converged ? 0 : 1;
        }

        std::size_t parsed = 0;
        const double end_time = std::stod(end_time_text, &parsed);
        if (parsed != end_time_text.size() || !std::isfinite(end_time) ||
            end_time <= 0.0) {
            throw std::invalid_argument(
                "--end-time must be a positive finite number");
        }
        const auto graph =
            thermox::platform::compile_transient_model_graph(
                document, components, case_id);
        thermox::TimeIntegrationOptions options;
        options.end_time = end_time;
        const auto result = thermox::integrate_dae(graph.problem, options);
        if (result.trajectory.empty()) {
            std::cerr << "transient solve failed: "
                      << result.diagnostics.message << "\n";
            return 1;
        }
        const auto& final = result.trajectory.back();
        if (format == "json") {
            std::cout << "{\n"
                      << "  \"model_id\": \"" << graph.model_id << "\",\n"
                      << "  \"success\": "
                      << (result.diagnostics.success ? "true" : "false") << ",\n"
                      << "  \"final_time\": " << final.time << ",\n"
                      << "  \"accepted_steps\": "
                      << result.diagnostics.accepted_steps << ",\n"
                      << "  \"variables\": {\n";
            for (std::size_t i = 0;
                 i < graph.problem.variable_names.size(); ++i) {
                std::cout << "    \"" << graph.problem.variable_names[i]
                          << "\": " << final.state[i]
                          << (i + 1 == graph.problem.variable_names.size()
                                  ? "\n"
                                  : ",\n");
            }
            std::cout << "  }\n}\n";
        } else {
            std::cout << "model: " << graph.model_id << "\n"
                      << "success: "
                      << (result.diagnostics.success ? "yes" : "no") << "\n"
                      << "final_time: " << final.time << "\n"
                      << "accepted_steps: "
                      << result.diagnostics.accepted_steps << "\n";
            for (std::size_t i = 0;
                 i < graph.problem.variable_names.size(); ++i) {
                std::cout << graph.problem.variable_names[i] << " = "
                          << final.state[i] << "\n";
            }
        }
        return result.diagnostics.success ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "thermox_cli error: " << ex.what() << "\n";
        return 1;
    }
}
