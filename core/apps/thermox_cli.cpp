#include "thermox/examples/brayton_cycle.hpp"
#include "thermox/examples/schema.hpp"

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
        const auto input = thermox::examples::load_brayton_cycle_model(model_path);
        thermox::SolverOptions options;
        options.residual_tolerance = 1.0e-10;
        const auto result = thermox::examples::solve_brayton_cycle(input, options);

        if (format == "json") {
            std::cout << thermox::examples::format_brayton_result_json(result);
        } else if (format == "text") {
            std::cout << thermox::examples::format_brayton_result_text(result);
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
