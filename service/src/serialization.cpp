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

void scalar_value(
    std::ostream& out,
    const platform::ScalarValue& scalar) {
    if (scalar.dimension == "dimensionless") {
        model_number(out, scalar.value_si);
        return;
    }
    out << "{\"value\": ";
    model_number(out, scalar.value_si);
    out << ", \"unit\": ";
    json_string(out, scalar.unit);
    out << "}";
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
        scalar_value(out, scalar);
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
    out << ",\n    \"platform_version\": ";
    json_string(out, metadata.platform_version);
    out << ",\n    \"operation\": ";
    json_string(out, metadata.operation);
    out << ",\n    \"solver\": {\"contract_version\": ";
    json_string(out, metadata.solver.contract_version);
    out << ", \"settings\": {";
    for (std::size_t i = 0;
         i < metadata.solver.settings.size(); ++i) {
        if (i != 0) out << ", ";
        json_string(out, metadata.solver.settings[i].name);
        out << ": ";
        json_number(out, metadata.solver.settings[i].value);
    }
    out << "}}";
    out << ",\n    \"catalog_fingerprint\": ";
    json_string(out, metadata.catalog_fingerprint);
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
        out << ", \"requested_version\": ";
        json_string(out, component.requested_version);
        out << ", \"resolved_version\": ";
        json_string(out, component.resolved_version);
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
        out << ", \"requested_package_version\": ";
        json_string(out, medium.requested_package_version);
        out << ", \"resolved_package_version\": ";
        json_string(out, medium.resolved_package_version);
        out << "}";
    }
    out << "],\n    \"connector_domains\": [";
    for (std::size_t i = 0;
         i < metadata.connector_domains.size(); ++i) {
        if (i != 0) out << ", ";
        out << "{\"domain\": ";
        json_string(out, metadata.connector_domains[i].domain);
        out << ", \"contract_version\": ";
        json_string(
            out,
            metadata.connector_domains[i].contract_version);
        out << "}";
    }
    out << "]\n  }";
}

void diagnostic_json(
    std::ostream& out,
    const Diagnostic& diagnostic) {
    out << "{\"code\": ";
    json_string(out, diagnostic.code);
    out << ", \"severity\": ";
    json_string(out, to_string(diagnostic.severity));
    out << ", \"stage\": ";
    json_string(out, diagnostic.stage);
    out << ", \"json_path\": ";
    json_string(out, diagnostic.json_path);
    out << ", \"component_id\": ";
    json_string(out, diagnostic.component_id);
    out << ", \"port_name\": ";
    json_string(out, diagnostic.port_name);
    out << ", \"connection_id\": ";
    json_string(out, diagnostic.connection_id);
    out << ", \"message\": ";
    json_string(out, diagnostic.message);
    out << ", \"suggestions\": [";
    for (std::size_t i = 0;
         i < diagnostic.suggestions.size(); ++i) {
        if (i != 0) out << ", ";
        json_string(out, diagnostic.suggestions[i]);
    }
    out << "]}";
}

void result_value_json(
    std::ostream& out,
    const ResultValue& value) {
    out << "{\"name\": ";
    json_string(out, value.name);
    out << ", \"dimension\": ";
    json_string(out, value.dimension);
    out << ", \"value_si\": ";
    json_number(out, value.value_si);
    if (value.has_derivative) {
        out << ", \"derivative_si_s\": ";
        json_number(out, value.derivative_si_s);
    }
    out << "}";
}

void result_values_json(
    std::ostream& out,
    const std::vector<ResultValue>& values) {
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out << ", ";
        result_value_json(out, values[i]);
    }
    out << "]";
}

void graph_result_json(
    std::ostream& out,
    const GraphResult& graph) {
    out << "{\"components\": [";
    for (std::size_t i = 0; i < graph.components.size(); ++i) {
        if (i != 0) out << ", ";
        const auto& component = graph.components[i];
        out << "{\"component_id\": ";
        json_string(out, component.component_id);
        out << ", \"kind\": ";
        json_string(out, component.kind);
        out << ", \"ports\": [";
        for (std::size_t j = 0; j < component.ports.size(); ++j) {
            if (j != 0) out << ", ";
            const auto& port = component.ports[j];
            out << "{\"port_name\": ";
            json_string(out, port.port_name);
            out << ", \"domain\": ";
            json_string(out, port.domain);
            out << ", \"medium_id\": ";
            json_string(out, port.medium_id);
            out << ", \"phase\": ";
            json_string(out, port.phase);
            out << ", \"primary_values\": ";
            result_values_json(out, port.primary_values);
            out << ", \"derived_values\": ";
            result_values_json(out, port.derived_values);
            out << "}";
        }
        out << "], \"internal_values\": ";
        result_values_json(out, component.internal_values);
        out << ", \"metrics\": ";
        result_values_json(out, component.metrics);
        out << "}";
    }
    out << "], \"system_balances\": ";
    result_values_json(out, graph.system_balances);
    out << ", \"kpis\": ";
    result_values_json(out, graph.kpis);
    out << "}";
}

}  // namespace

namespace detail {

std::string serialize_model_document_json(
    const platform::ModelDocument& document) {
    if (document.schema_version != "thermox.model/v2") {
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
        if (!medium.package_version.empty()) {
            out << ", \"package_version\": ";
            json_string(out, medium.package_version);
        }
        out << "}" << (i + 1 == document.media.size() ? "\n" : ",\n");
    }
    out << "    ],\n    \"materials\": [";
    if (!document.materials.empty()) out << "\n";
    for (std::size_t i = 0;
         i < document.materials.size(); ++i) {
        const auto& material = document.materials[i];
        out << "      {\"id\": ";
        json_string(out, material.id);
        out << ", \"backend\": ";
        json_string(out, material.backend);
        out << ", \"mechanism\": ";
        json_string(out, material.mechanism);
        out << ", \"phase\": ";
        json_string(out, material.phase);
        if (!material.package_version.empty()) {
            out << ", \"package_version\": ";
            json_string(out, material.package_version);
        }
        out << ", \"species\": [";
        for (std::size_t species = 0;
             species < material.species.size(); ++species) {
            if (species != 0) out << ", ";
            json_string(out, material.species[species]);
        }
        out << "]}"
            << (i + 1 == document.materials.size()
                    ? "\n"
                    : ",\n");
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
        if (!component.medium_bindings.empty()) {
            out << ",\n        \"media\": {";
            std::size_t binding_index = 0;
            for (const auto& [port_name, medium_id] :
                 component.medium_bindings) {
                if (binding_index++ != 0) out << ", ";
                json_string(out, port_name);
                out << ": ";
                json_string(out, medium_id);
            }
            out << "}";
        }
        if (!component.material_bindings.empty()) {
            out << ",\n        \"materials\": {";
            std::size_t binding_index = 0;
            for (const auto& [port_name, material_id] :
                 component.material_bindings) {
                if (binding_index++ != 0) out << ", ";
                json_string(out, port_name);
                out << ": ";
                json_string(out, material_id);
            }
            out << "}";
        }
        if (!component.artifact_bindings.empty()) {
            out << ",\n        \"artifacts\": {";
            std::size_t binding_index = 0;
            for (const auto& [role, artifact_id] :
                 component.artifact_bindings) {
                if (binding_index++ != 0) out << ", ";
                json_string(out, role);
                out << ": ";
                json_string(out, artifact_id);
            }
            out << "}";
        }
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
        if (!connection.contract_version.empty()) {
            out << ", \"contract_version\": ";
            json_string(out, connection.contract_version);
        }
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
    out << "  ]";
    if (!document.calibrations.empty()) {
        out << ",\n  \"calibrations\": [\n";
        for (std::size_t i = 0;
             i < document.calibrations.size(); ++i) {
            const auto& calibration =
                document.calibrations[i];
            out << "    {\n      \"id\": ";
            json_string(out, calibration.id);
            if (!calibration.label.empty()) {
                out << ",\n      \"label\": ";
                json_string(out, calibration.label);
            }
            out << ",\n      \"parameters\": [\n";
            for (std::size_t j = 0;
                 j < calibration.parameters.size(); ++j) {
                const auto& parameter =
                    calibration.parameters[j];
                out << "        {\"id\": ";
                json_string(out, parameter.id);
                if (!parameter.label.empty()) {
                    out << ", \"label\": ";
                    json_string(out, parameter.label);
                }
                out << ", \"scope\": ";
                json_string(out, parameter.scope);
                out << ", \"targets\": [";
                for (std::size_t target = 0;
                     target < parameter.targets.size();
                     ++target) {
                    if (target != 0) out << ", ";
                    json_string(
                        out, parameter.targets[target]);
                }
                out << "]";
                if (!parameter.case_ids.empty()) {
                    out << ", \"cases\": [";
                    for (std::size_t case_index = 0;
                         case_index <
                         parameter.case_ids.size();
                         ++case_index) {
                        if (case_index != 0) out << ", ";
                        json_string(
                            out,
                            parameter.case_ids[case_index]);
                    }
                    out << "]";
                }
                if (parameter.lower_bound.has_value() ||
                    parameter.upper_bound.has_value()) {
                    out << ", \"bounds\": {";
                    if (parameter.lower_bound.has_value()) {
                        out << "\"lower\": ";
                        scalar_value(
                            out, *parameter.lower_bound);
                    }
                    if (parameter.lower_bound.has_value() &&
                        parameter.upper_bound.has_value()) {
                        out << ", ";
                    }
                    if (parameter.upper_bound.has_value()) {
                        out << "\"upper\": ";
                        scalar_value(
                            out, *parameter.upper_bound);
                    }
                    out << "}";
                }
                if (parameter.prior_mean.has_value()) {
                    out << ", \"prior\": {\"mean\": ";
                    scalar_value(out, *parameter.prior_mean);
                    out << ", \"sigma\": ";
                    scalar_value(out, *parameter.prior_sigma);
                    out << "}";
                }
                out << "}"
                    << (j + 1 ==
                                calibration.parameters.size()
                            ? "\n"
                            : ",\n");
            }
            out << "      ],\n"
                   "      \"observations\": [\n";
            for (std::size_t j = 0;
                 j < calibration.observations.size(); ++j) {
                const auto& observation =
                    calibration.observations[j];
                out << "        {\"id\": ";
                json_string(out, observation.id);
                if (!observation.label.empty()) {
                    out << ", \"label\": ";
                    json_string(out, observation.label);
                }
                out << ", \"case\": ";
                json_string(out, observation.case_id);
                out << ", \"target\": ";
                json_string(out, observation.target);
                out << ", \"measured\": ";
                scalar_value(out, observation.measured);
                out << ", \"sigma\": ";
                scalar_value(out, observation.sigma);
                out << "}"
                    << (j + 1 ==
                                calibration.observations.size()
                            ? "\n"
                            : ",\n");
            }
            out << "      ]\n    }"
                << (i + 1 == document.calibrations.size()
                        ? "\n"
                        : ",\n");
        }
        out << "  ]";
    }
    out << "\n}\n";
    return out.str();
}

}  // namespace detail

std::string serialize_catalog_response_json(
    const CatalogResponse& response) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": ";
    json_string(out, response.schema_version);
    out << ",\n  \"status\": ";
    json_string(out, to_string(response.status));
    out << ",\n  \"error\": ";
    error_json(out, response.error);
    out << ",\n  \"fingerprint\": ";
    json_string(out, response.fingerprint);
    out << ",\n  \"components\": [";
    for (std::size_t i = 0; i < response.components.size(); ++i) {
        if (i != 0) out << ", ";
        const auto& component = response.components[i];
        out << "{\"kind\": ";
        json_string(out, component.kind);
        out << ", \"version\": ";
        json_string(out, component.version);
        out << ", \"supports_steady\": "
            << (component.supports_steady ? "true" : "false")
            << ", \"supports_transient\": "
            << (component.supports_transient ? "true" : "false")
            << ", \"ports\": [";
        for (std::size_t j = 0; j < component.ports.size(); ++j) {
            if (j != 0) out << ", ";
            const auto& port = component.ports[j];
            out << "{\"name\": ";
            json_string(out, port.name);
            out << ", \"domain\": ";
            json_string(out, port.domain);
            out << ", \"direction\": ";
            json_string(out, port.direction);
            out << ", \"maximum_connections\": "
                << port.maximum_connections;
            out << "}";
        }
        out << "], \"parameters\": [";
        for (std::size_t j = 0;
             j < component.parameters.size(); ++j) {
            if (j != 0) out << ", ";
            const auto& parameter = component.parameters[j];
            out << "{\"name\": ";
            json_string(out, parameter.name);
            out << ", \"dimension\": ";
            json_string(out, parameter.dimension);
            out << ", \"required\": "
                << (parameter.required ? "true" : "false")
                << ", \"default_value_si\": ";
            if (parameter.has_default) {
                json_number(out, parameter.default_value_si);
            } else {
                out << "null";
            }
            out << ", \"lower_bound\": ";
            json_number(out, parameter.lower_bound);
            out << ", \"upper_bound\": ";
            json_number(out, parameter.upper_bound);
            out << ", \"lower_inclusive\": "
                << (parameter.lower_inclusive ? "true" : "false")
                << ", \"upper_inclusive\": "
                << (parameter.upper_inclusive ? "true" : "false")
                << "}";
        }
        out << "], \"artifacts\": [";
        for (std::size_t j = 0;
             j < component.artifacts.size(); ++j) {
            if (j != 0) out << ", ";
            const auto& artifact = component.artifacts[j];
            out << "{\"role\": ";
            json_string(out, artifact.role);
            out << ", \"artifact_type\": ";
            json_string(out, artifact.artifact_type);
            out << ", \"required\": "
                << (artifact.required ? "true" : "false")
                << "}";
        }
        out << "], \"internal_variables\": [";
        for (std::size_t j = 0;
             j < component.internal_variables.size(); ++j) {
            if (j != 0) out << ", ";
            const auto& variable =
                component.internal_variables[j];
            out << "{\"name\": ";
            json_string(out, variable.name);
            out << ", \"dimension\": ";
            json_string(out, variable.dimension);
            out << ", \"kind\": ";
            json_string(out, variable.kind);
            out << "}";
        }
        out << "], \"required_property_capabilities\": [";
        for (std::size_t j = 0;
             j < component.required_property_capabilities.size();
             ++j) {
            if (j != 0) out << ", ";
            json_string(
                out,
                component.required_property_capabilities[j]);
        }
        out << "], \"required_thermochemistry_capabilities\": [";
        for (std::size_t j = 0;
             j < component
                     .required_thermochemistry_capabilities
                     .size();
             ++j) {
            if (j != 0) out << ", ";
            json_string(
                out,
                component
                    .required_thermochemistry_capabilities[j]);
        }
        out << "]}";
    }
    out << "],\n  \"property_backends\": [";
    for (std::size_t i = 0;
         i < response.property_backends.size(); ++i) {
        if (i != 0) out << ", ";
        out << "{\"backend\": ";
        const auto& backend = response.property_backends[i];
        json_string(out, backend.backend);
        out << ", \"implementation_name\": ";
        json_string(out, backend.implementation_name);
        out << ", \"implementation_version\": ";
        json_string(out, backend.implementation_version);
        out << ", \"supported_substances\": [";
        for (std::size_t j = 0;
             j < backend.supported_substances.size(); ++j) {
            if (j != 0) out << ", ";
            json_string(out, backend.supported_substances[j]);
        }
        out << "], \"capabilities\": [";
        for (std::size_t j = 0;
             j < backend.capabilities.size(); ++j) {
            if (j != 0) out << ", ";
            json_string(out, backend.capabilities[j]);
        }
        out << "]}";
    }
    out << "],\n  \"thermochemistry_backends\": [";
    for (std::size_t i = 0;
         i < response.thermochemistry_backends.size(); ++i) {
        if (i != 0) out << ", ";
        const auto& backend =
            response.thermochemistry_backends[i];
        out << "{\"backend\": ";
        json_string(out, backend.backend);
        out << ", \"implementation_name\": ";
        json_string(out, backend.implementation_name);
        out << ", \"implementation_version\": ";
        json_string(out, backend.implementation_version);
        out << ", \"capabilities\": [";
        for (std::size_t j = 0;
             j < backend.capabilities.size(); ++j) {
            if (j != 0) out << ", ";
            json_string(out, backend.capabilities[j]);
        }
        out << "]}";
    }
    out << "],\n  \"connector_domains\": [";
    for (std::size_t i = 0;
         i < response.connector_domains.size(); ++i) {
        if (i != 0) out << ", ";
        const auto& domain = response.connector_domains[i];
        out << "{\"domain\": ";
        json_string(out, domain.domain);
        out << ", \"contract_version\": ";
        json_string(out, domain.contract_version);
        out << ", \"variables\": [";
        for (std::size_t j = 0; j < domain.variables.size(); ++j) {
            if (j != 0) out << ", ";
            out << "{\"name\": ";
            json_string(out, domain.variables[j].name);
            out << ", \"dimension\": ";
            json_string(out, domain.variables[j].dimension);
            out << "}";
        }
        out << "]}";
    }
    out << "]\n}\n";
    return out.str();
}

std::string serialize_validate_response_json(
    const ValidateModelResponse& response) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": ";
    json_string(out, result_schema_v3);
    out << ",\n  \"status\": ";
    json_string(out, to_string(response.status));
    out << ",\n  \"error\": ";
    error_json(out, response.error);
    out << ",\n  \"model\": ";
    model_metadata_json(out, response.model);
    out << ",\n  \"canonical_model_json\": ";
    json_string(out, response.canonical_model_json);
    out << ",\n  \"compilation\": {\"compiled\": "
        << (response.compilation.compiled ? "true" : "false")
        << ", \"mode\": ";
    json_string(out, response.compilation.mode);
    out << ", \"variable_count\": "
        << response.compilation.variable_count
        << ", \"equation_count\": "
        << response.compilation.equation_count
        << ", \"catalog_fingerprint\": ";
    json_string(out, response.compilation.catalog_fingerprint);
    out << ", \"reduced_connection_equations\": [";
    for (std::size_t i = 0;
         i < response.compilation.reduced_connection_equations.size();
         ++i) {
        if (i != 0) out << ", ";
        json_string(
            out,
            response.compilation.reduced_connection_equations[i]);
    }
    out << "]},\n  \"diagnostics\": [";
    for (std::size_t i = 0;
         i < response.diagnostics.size(); ++i) {
        if (i != 0) out << ", ";
        diagnostic_json(out, response.diagnostics[i]);
    }
    out << "]";
    out << "\n}\n";
    return out.str();
}

std::string serialize_steady_response_json(
    const SteadySimulationResponse& response) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": ";
    json_string(out, result_schema_v3);
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
    out << "},\n  \"graph\": ";
    graph_result_json(out, response.graph);
    out << ",\n  \"reduced_connection_equations\": [";
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
    json_string(out, result_schema_v3);
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
    out << "},\n  \"trajectory\": [";
    for (std::size_t i = 0; i < response.trajectory.size(); ++i) {
        if (i != 0) out << ", ";
        const auto& sample = response.trajectory[i];
        out << "{\"time\": ";
        json_number(out, sample.time);
        out << ", \"graph\": ";
        graph_result_json(out, sample.graph);
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
        out << ", \"graph\": ";
        graph_result_json(out, event.graph);
        out << ", \"terminal\": "
            << (event.terminal ? "true" : "false") << "}";
    }
    out << "]\n}\n";
    return out.str();
}

std::string serialize_job_record_json(
    const SimulationJobRecord& record) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": ";
    json_string(out, record.schema_version);
    out << ",\n  \"job_id\": ";
    json_string(out, record.job_id);
    out << ",\n  \"revision\": " << record.revision;
    out << ",\n  \"state\": ";
    json_string(out, to_string(record.state));
    out << ",\n  \"request\": {\"schema_version\": ";
    json_string(out, record.request.schema_version);
    out << ", \"mode\": ";
    json_string(out, to_string(record.request.mode));
    out << ", \"case_id\": ";
    json_string(out, record.request.case_id);
    out << ", \"fingerprint\": ";
    json_string(out, record.request_fingerprint);
    out << "}";
    out << ",\n  \"worker_id\": ";
    if (record.worker_id.empty()) {
        out << "null";
    } else {
        json_string(out, record.worker_id);
    }
    out << ",\n  \"execution\": ";
    if (record.execution) {
        execution_metadata_json(out, *record.execution);
    } else {
        out << "null";
    }
    out << ",\n  \"error\": ";
    if (record.error) {
        error_json(out, *record.error);
    } else {
        out << "null";
    }
    out << ",\n  \"result_artifact\": ";
    if (record.result_artifact) {
        const auto& artifact = *record.result_artifact;
        out << "{\"artifact_id\": ";
        json_string(out, artifact.artifact_id);
        out << ", \"media_type\": ";
        json_string(out, artifact.media_type);
        out << ", \"schema_version\": ";
        json_string(out, artifact.schema_version);
        out << ", \"byte_size\": " << artifact.byte_size;
        out << ", \"checksum\": ";
        json_string(out, artifact.checksum);
        out << "}";
    } else {
        out << "null";
    }
    out << "\n}\n";
    return out.str();
}

}  // namespace thermox::service
