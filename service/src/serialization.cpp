#include "thermox/service/serialization.hpp"

#include "serialization_internal.hpp"

#include <cmath>
#include <iomanip>
#include <map>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace thermox::service {

namespace {

void json_string(std::ostream& out, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for (const unsigned char character : value) {
        switch (character) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (character < 0x20U) {
                    out << "\\u00"
                        << hex[(character >> 4U) & 0x0fU]
                        << hex[character & 0x0fU];
                } else {
                    out << static_cast<char>(character);
                }
        }
    }
    out << '"';
}

void json_number(std::ostream& out, double value) {
    if (std::isfinite(value)) {
        out << std::setprecision(17) << value;
    } else {
        out << "null";
    }
}

void model_number(std::ostream& out, double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(
            "model serialization requires finite scalar values");
    }
    out << std::setprecision(17) << value;
}

void scalar_map(
    std::ostream& out,
    const std::map<std::string, platform::ScalarValue>& values,
    std::string_view indent) {
    out << "{";
    if (!values.empty()) out << "\n";
    std::size_t index = 0;
    for (const auto& [name, scalar] : values) {
        out << indent;
        json_string(out, name);
        out << ": ";
        if (scalar.dimension == "dimensionless") {
            model_number(out, scalar.value_si);
        } else {
            out << "{\"value\": ";
            model_number(out, scalar.value_si);
            out << ", \"unit\": ";
            json_string(out, scalar.unit);
            out << "}";
        }
        out << (++index == values.size() ? "\n" : ",\n");
    }
    if (!values.empty()) {
        out << std::string(indent.size() - 2, ' ');
    }
    out << "}";
}

void error_json(std::ostream& out, const ServiceError& error) {
    if (error.code.empty()) {
        out << "null";
        return;
    }
    out << "{\"schema_version\": ";
    json_string(out, error.schema_version);
    out << ", \"code\": ";
    json_string(out, error.code);
    out << ", \"stage\": ";
    json_string(out, error.stage);
    out << ", \"message\": ";
    json_string(out, error.message);
    out << "}";
}

void model_metadata_json(
    std::ostream& out,
    const ModelMetadata& metadata) {
    out << "{\"schema_version\": ";
    json_string(out, metadata.schema_version);
    out << ", \"id\": ";
    json_string(out, metadata.model_id);
    out << ", \"revision\": ";
    json_string(out, metadata.model_revision);
    out << ", \"case_id\": ";
    json_string(out, metadata.case_id);
    out << "}";
}

void execution_metadata_json(
    std::ostream& out,
    const ExecutionMetadata& metadata) {
    out << "{\n    \"result_schema_version\": ";
    json_string(out, metadata.result_schema_version);
    out << ",\n    \"command_schema_version\": ";
    json_string(out, metadata.command_schema_version);
    out << ",\n    \"operation\": ";
    json_string(out, metadata.operation);
    out << ",\n    \"solver_contract\": ";
    json_string(out, metadata.solver_contract);
    out << ",\n    \"model\": ";
    model_metadata_json(out, metadata.model);
    out << ",\n    \"components\": [";
    for (std::size_t i = 0; i < metadata.components.size(); ++i) {
        const auto& component = metadata.components[i];
        if (i != 0) out << ", ";
        out << "{\"id\": ";
        json_string(out, component.component_id);
        out << ", \"kind\": ";
        json_string(out, component.kind);
        out << ", \"implementation_version\": ";
        json_string(out, component.implementation_version);
        out << "}";
    }
    out << "],\n    \"media\": [";
    for (std::size_t i = 0; i < metadata.media.size(); ++i) {
        const auto& medium = metadata.media[i];
        if (i != 0) out << ", ";
        out << "{\"id\": ";
        json_string(out, medium.medium_id);
        out << ", \"backend\": ";
        json_string(out, medium.backend);
        out << ", \"substance\": ";
        json_string(out, medium.substance);
        out << ", \"package\": ";
        json_string(out, medium.package);
        out << "}";
    }
    out << "]\n  }";
}

void number_array(
    std::ostream& out,
    const std::vector<double>& values) {
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out << ", ";
        json_number(out, values[i]);
    }
    out << "]";
}

}  // namespace

namespace detail {

std::string serialize_model_document_json(
    const platform::ModelDocument& document) {
    if (document.schema_version != "thermox.model/v1") {
        throw std::invalid_argument(
            "unsupported model schema_version for serialization: " +
            document.schema_version);
    }
    std::ostringstream out;
    out << "{\n  \"schema_version\": ";
    json_string(out, document.schema_version);
    out << ",\n  \"model\": {\n    \"id\": ";
    json_string(out, document.model_id);
    if (!document.name.empty()) {
        out << ",\n    \"name\": ";
        json_string(out, document.name);
    }
    if (!document.revision.empty()) {
        out << ",\n    \"revision\": ";
        json_string(out, document.revision);
    }
    out << ",\n    \"media\": [";
    if (!document.media.empty()) out << "\n";
    for (std::size_t i = 0; i < document.media.size(); ++i) {
        const auto& medium = document.media[i];
        out << "      {\"id\": ";
        json_string(out, medium.id);
        out << ", \"backend\": ";
        json_string(out, medium.backend);
        out << ", \"substance\": ";
        json_string(out, medium.substance);
        out << "}" << (i + 1 == document.media.size() ? "\n" : ",\n");
    }
    out << "    ],\n    \"components\": [";
    if (!document.components.empty()) out << "\n";
    for (std::size_t i = 0; i < document.components.size(); ++i) {
        const auto& component = document.components[i];
        out << "      {\n        \"id\": ";
        json_string(out, component.id);
        if (!component.label.empty()) {
            out << ",\n        \"label\": ";
            json_string(out, component.label);
        }
        out << ",\n        \"kind\": ";
        json_string(out, component.kind);
        if (!component.version.empty()) {
            out << ",\n        \"version\": ";
            json_string(out, component.version);
        }
        out << ",\n        \"ports\": {";
        if (!component.ports.empty()) out << "\n";
        std::size_t port_index = 0;
        for (const auto& [name, port] : component.ports) {
            out << "          ";
            json_string(out, name);
            out << ": {\"domain\": ";
            json_string(out, port.domain);
            if (!port.medium.empty()) {
                out << ", \"medium\": ";
                json_string(out, port.medium);
            }
            out << ", \"direction\": ";
            json_string(out, port.direction);
            out << "}"
                << (++port_index == component.ports.size()
                        ? "\n"
                        : ",\n");
        }
        out << "        }";
        if (!component.parameters.empty()) {
            out << ",\n        \"parameters\": ";
            scalar_map(out, component.parameters, "          ");
        }
        out << "\n      }"
            << (i + 1 == document.components.size() ? "\n" : ",\n");
    }
    out << "    ],\n    \"connections\": [";
    if (!document.connections.empty()) out << "\n";
    for (std::size_t i = 0; i < document.connections.size(); ++i) {
        const auto& connection = document.connections[i];
        out << "      {\"id\": ";
        json_string(out, connection.id);
        out << ", \"from\": ";
        json_string(out, connection.from);
        out << ", \"to\": ";
        json_string(out, connection.to);
        out << ", \"kind\": ";
        json_string(out, connection.kind);
        if (!connection.parameters.empty()) {
            out << ", \"parameters\": ";
            scalar_map(out, connection.parameters, "        ");
        }
        out << "}"
            << (i + 1 == document.connections.size() ? "\n" : ",\n");
    }
    out << "    ]\n  },\n  \"cases\": [";
    if (!document.cases.empty()) out << "\n";
    for (std::size_t i = 0; i < document.cases.size(); ++i) {
        const auto& simulation_case = document.cases[i];
        out << "    {\n      \"id\": ";
        json_string(out, simulation_case.id);
        if (!simulation_case.label.empty()) {
            out << ",\n      \"label\": ";
            json_string(out, simulation_case.label);
        }
        out << ",\n      \"mode\": ";
        json_string(out, simulation_case.mode);
        if (!simulation_case.fixed_values.empty()) {
            out << ",\n      \"fixed_values\": ";
            scalar_map(out, simulation_case.fixed_values, "        ");
        }
        if (!simulation_case.initial_guesses.empty()) {
            out << ",\n      \"initial_guesses\": ";
            scalar_map(out, simulation_case.initial_guesses, "        ");
        }
        if (!simulation_case.solver_options.empty()) {
            out << ",\n      \"solver_options\": ";
            scalar_map(out, simulation_case.solver_options, "        ");
        }
        out << "\n    }"
            << (i + 1 == document.cases.size() ? "\n" : ",\n");
    }
    out << "  ]\n}\n";
    return out.str();
}

}  // namespace detail

std::string serialize_validate_response_json(
    const ValidateModelResponse& response) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": ";
    json_string(out, result_schema_v1);
    out << ",\n  \"status\": ";
    json_string(out, to_string(response.status));
    out << ",\n  \"error\": ";
    error_json(out, response.error);
    out << ",\n  \"model\": ";
    model_metadata_json(out, response.model);
    out << ",\n  \"canonical_model_json\": ";
    json_string(out, response.canonical_model_json);
    out << "\n}\n";
    return out.str();
}

std::string serialize_steady_response_json(
    const SteadySimulationResponse& response) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": ";
    json_string(out, result_schema_v1);
    out << ",\n  \"status\": ";
    json_string(out, to_string(response.status));
    out << ",\n  \"error\": ";
    error_json(out, response.error);
    out << ",\n  \"metadata\": ";
    execution_metadata_json(out, response.metadata);
    out << ",\n  \"diagnostics\": {\"converged\": "
        << (response.diagnostics.converged ? "true" : "false")
        << ", \"iterations\": " << response.diagnostics.iterations
        << ", \"final_residual_norm\": ";
    json_number(out, response.diagnostics.final_residual_norm);
    out << ", \"final_step_norm\": ";
    json_number(out, response.diagnostics.final_step_norm);
    out << ", \"function_evaluations\": "
        << response.diagnostics.function_evaluations
        << ", \"jacobian_evaluations\": "
        << response.diagnostics.jacobian_evaluations
        << ", \"linear_solver_evaluations\": "
        << response.diagnostics.linear_solver_evaluations
        << ", \"message\": ";
    json_string(out, response.diagnostics.message);
    out << "},\n  \"variables\": {";
    for (std::size_t i = 0; i < response.variables.size(); ++i) {
        if (i != 0) out << ", ";
        json_string(out, response.variables[i].name);
        out << ": ";
        json_number(out, response.variables[i].value_si);
    }
    out << "},\n  \"fluid_ports\": [";
    for (std::size_t i = 0; i < response.fluid_ports.size(); ++i) {
        const auto& port = response.fluid_ports[i];
        if (i != 0) out << ", ";
        out << "{\"component_id\": ";
        json_string(out, port.component_id);
        out << ", \"port_name\": ";
        json_string(out, port.port_name);
        out << ", \"medium_id\": ";
        json_string(out, port.medium_id);
        out << ", \"mass_flow_kg_s\": ";
        json_number(out, port.mass_flow_kg_s);
        out << ", \"pressure_pa\": ";
        json_number(out, port.pressure_pa);
        out << ", \"temperature_k\": ";
        json_number(out, port.temperature_k);
        out << ", \"density_kg_m3\": ";
        json_number(out, port.density_kg_m3);
        out << ", \"enthalpy_j_kg\": ";
        json_number(out, port.enthalpy_j_kg);
        out << ", \"entropy_j_kg_k\": ";
        json_number(out, port.entropy_j_kg_k);
        out << ", \"vapor_quality\": ";
        json_number(out, port.vapor_quality);
        out << "}";
    }
    out << "],\n  \"reduced_connection_equations\": [";
    for (std::size_t i = 0;
         i < response.reduced_connection_equations.size();
         ++i) {
        if (i != 0) out << ", ";
        json_string(out, response.reduced_connection_equations[i]);
    }
    out << "]\n}\n";
    return out.str();
}

std::string serialize_transient_response_json(
    const TransientSimulationResponse& response) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": ";
    json_string(out, result_schema_v1);
    out << ",\n  \"status\": ";
    json_string(out, to_string(response.status));
    out << ",\n  \"error\": ";
    error_json(out, response.error);
    out << ",\n  \"metadata\": ";
    execution_metadata_json(out, response.metadata);
    out << ",\n  \"diagnostics\": {\"success\": "
        << (response.diagnostics.success ? "true" : "false")
        << ", \"accepted_steps\": "
        << response.diagnostics.accepted_steps
        << ", \"rejected_steps\": "
        << response.diagnostics.rejected_steps
        << ", \"nonlinear_solves\": "
        << response.diagnostics.nonlinear_solves
        << ", \"nonlinear_iterations\": "
        << response.diagnostics.nonlinear_iterations
        << ", \"final_time\": ";
    json_number(out, response.diagnostics.final_time);
    out << ", \"last_step\": ";
    json_number(out, response.diagnostics.last_step);
    out << ", \"message\": ";
    json_string(out, response.diagnostics.message);
    out << "},\n  \"variable_names\": [";
    for (std::size_t i = 0; i < response.variable_names.size(); ++i) {
        if (i != 0) out << ", ";
        json_string(out, response.variable_names[i]);
    }
    out << "],\n  \"trajectory\": [";
    for (std::size_t i = 0; i < response.trajectory.size(); ++i) {
        if (i != 0) out << ", ";
        const auto& sample = response.trajectory[i];
        out << "{\"time\": ";
        json_number(out, sample.time);
        out << ", \"state\": ";
        number_array(out, sample.state);
        out << ", \"derivative\": ";
        number_array(out, sample.derivative);
        out << "}";
    }
    out << "],\n  \"events\": [";
    for (std::size_t i = 0; i < response.events.size(); ++i) {
        if (i != 0) out << ", ";
        const auto& event = response.events[i];
        out << "{\"name\": ";
        json_string(out, event.name);
        out << ", \"time\": ";
        json_number(out, event.time);
        out << ", \"state\": ";
        number_array(out, event.state);
        out << ", \"terminal\": "
            << (event.terminal ? "true" : "false") << "}";
    }
    out << "]\n}\n";
    return out.str();
}

}  // namespace thermox::service
