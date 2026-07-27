#include "thermox/nonlinear_solver.hpp"
#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/model_document.hpp"

#include <exception>
#include <iostream>
#include <string>

namespace {

void print_usage(std::ostream& out) {
    out << "Usage:\n"
        << "  thermox_cli solve --model <path> [--format text|json]\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 1) {
        print_usage(std::cerr);
        return 2;
    }

    std::string command;
    std::string model_path;
    std::string format = "text";

    command = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--model" && i + 1 < argc) {
            model_path = argv[++i];
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

    if (command != "solve") {
        std::cerr << "Unknown command: " << command << "\n";
        print_usage(std::cerr);
        return 2;
    }
    if (model_path.empty()) {
        std::cerr << "Missing required --model path\n";
        print_usage(std::cerr);
        return 2;
    }

    try {
        const auto document = thermox::platform::load_model_document(model_path);
        const auto components = thermox::platform::make_default_component_registry();
        const auto graph = thermox::platform::compile_model_graph(document, components);
        thermox::SolverOptions options;
        options.residual_tolerance = 1.0e-10;
        const auto result = thermox::solve_newton(graph.problem, options);

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
            std::cout << "  }\n}\n";
        } else if (format == "text") {
            std::cout << "model: " << graph.model_id << "\n"
                      << "converged: "
                      << (result.diagnostics.converged ? "yes" : "no") << "\n"
                      << "iterations: " << result.diagnostics.iterations << "\n";
            for (std::size_t i = 0; i < graph.problem.variable_names.size(); ++i)
                std::cout << graph.problem.variable_names[i] << " = " << result.x[i] << "\n";
        } else {
            std::cerr << "Unsupported format: " << format << "\n";
            return 2;
        }

        return result.diagnostics.converged ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "thermox_cli error: " << ex.what() << "\n";
        return 1;
    }
}
