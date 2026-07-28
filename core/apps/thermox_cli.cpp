#include "thermox/service/serialization.hpp"
#include "thermox/service/simulation_service.hpp"

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
           " [--format text|json]\n"
        << "  thermox_cli simulate --model <path> [--case <id>]"
           " --end-time <seconds> [--format text|json]\n";
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

void print_steady_text(
    const thermox::service::SteadySimulationResponse& response) {
    std::cout << "model: " << response.metadata.model.model_id << "\n"
              << "status: "
              << thermox::service::to_string(response.status) << "\n"
              << "converged: "
              << (response.diagnostics.converged ? "yes" : "no") << "\n"
              << "iterations: " << response.diagnostics.iterations << "\n";
    if (!response.error.code.empty()) {
        std::cout << "error: " << response.error.message << "\n";
    }
    for (const auto& variable : response.variables) {
        std::cout << variable.name << " = " << variable.value_si << "\n";
    }
    for (const auto& port : response.fluid_ports) {
        const std::string prefix =
            port.component_id + "." + port.port_name;
        std::cout << prefix << ".T = " << port.temperature_k << "\n"
                  << prefix << ".rho = " << port.density_kg_m3 << "\n"
                  << prefix << ".s = " << port.entropy_j_kg_k << "\n"
                  << prefix << ".quality = " << port.vapor_quality << "\n";
    }
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
              << response.diagnostics.accepted_steps << "\n";
    if (!response.error.code.empty()) {
        std::cout << "error: " << response.error.message << "\n";
    }
    if (response.trajectory.empty()) return;
    const auto& final = response.trajectory.back().state;
    for (std::size_t i = 0;
         i < response.variable_names.size() && i < final.size();
         ++i) {
        std::cout << response.variable_names[i] << " = "
                  << final[i] << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 1) {
        print_usage(std::cerr);
        return 2;
    }

    const std::string command = argv[1];
    std::string model_path;
    std::string case_id;
    std::string end_time_text;
    std::string format = "text";

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
        const std::string model_json = read_file(model_path);
        thermox::service::SimulationService service;
        if (command == "solve") {
            thermox::service::SteadySimulationRequest request;
            request.model_json = model_json;
            request.case_id = case_id;
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
