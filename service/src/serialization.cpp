#include "thermox/service/serialization.hpp"

#include "serialization_internal.hpp"

#include "thermox/platform/correlation.hpp"
#include "thermox/platform/performance_map.hpp"
#include "thermox/platform/regime_map.hpp"

#include <chrono>
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

void calibration_definition_json(
    std::ostream& out,
    const platform::CalibrationDefinition& calibration,
    std::string_view indent) {
    const std::string child_indent(indent.size() + 2U, ' ');
    const std::string item_indent(indent.size() + 4U, ' ');
    out << "{\n" << child_indent << "\"id\": ";
    json_string(out, calibration.id);
    if (!calibration.label.empty()) {
        out << ",\n" << child_indent << "\"label\": ";
        json_string(out, calibration.label);
    }
    out << ",\n" << child_indent << "\"parameters\": [\n";
    for (std::size_t index = 0; index < calibration.parameters.size();
         ++index) {
        const auto& parameter = calibration.parameters[index];
        out << item_indent << "{\"id\": ";
        json_string(out, parameter.id);
        if (!parameter.label.empty()) {
            out << ", \"label\": ";
            json_string(out, parameter.label);
        }
        out << ", \"scope\": ";
        json_string(out, parameter.scope);
        out << ", \"targets\": [";
        for (std::size_t target = 0; target < parameter.targets.size();
             ++target) {
            if (target != 0U) out << ", ";
            json_string(out, parameter.targets[target]);
        }
        out << "]";
        if (!parameter.case_ids.empty()) {
            out << ", \"cases\": [";
            for (std::size_t case_index = 0;
                 case_index < parameter.case_ids.size(); ++case_index) {
                if (case_index != 0U) out << ", ";
                json_string(out, parameter.case_ids[case_index]);
            }
            out << "]";
        }
        if (parameter.lower_bound || parameter.upper_bound) {
            out << ", \"bounds\": {";
            if (parameter.lower_bound) {
                out << "\"lower\": ";
                scalar_value(out, *parameter.lower_bound);
            }
            if (parameter.lower_bound && parameter.upper_bound) out << ", ";
            if (parameter.upper_bound) {
                out << "\"upper\": ";
                scalar_value(out, *parameter.upper_bound);
            }
            out << "}";
        }
        if (parameter.prior_mean) {
            out << ", \"prior\": {\"mean\": ";
            scalar_value(out, *parameter.prior_mean);
            out << ", \"sigma\": ";
            scalar_value(out, *parameter.prior_sigma);
            out << "}";
        }
        out << "}" << (index + 1U == calibration.parameters.size()
                              ? "\n"
                              : ",\n");
    }
    out << child_indent << "],\n" << child_indent
        << "\"observations\": [\n";
    for (std::size_t index = 0; index < calibration.observations.size();
         ++index) {
        const auto& observation = calibration.observations[index];
        out << item_indent << "{\"id\": ";
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
        out << "}" << (index + 1U == calibration.observations.size()
                              ? "\n"
                              : ",\n");
    }
    out << child_indent << "]\n" << indent << "}";
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

void revision_provenance_json(
    std::ostream& out,
    const RevisionProvenance& source) {
    out << "{\"project_id\": ";
    json_string(out, source.project_id);
    out << ", \"run_configuration_revision_id\": ";
    json_string(
        out, source.run_configuration_revision_id);
    out << ", \"run_configuration_checksum\": ";
    json_string(
        out, source.run_configuration_checksum);
    out << ", \"study_revision_id\": ";
    json_string(out, source.study_revision_id);
    out << ", \"study_checksum\": ";
    json_string(out, source.study_checksum);
    out << ", \"calibration_revision_id\": ";
    json_string(out, source.calibration_revision_id);
    out << ", \"calibration_checksum\": ";
    json_string(out, source.calibration_checksum);
    out << ", \"model_revision_id\": ";
    json_string(out, source.model_revision_id);
    out << ", \"model_checksum\": ";
    json_string(out, source.model_checksum);
    out << ", \"case_revision_id\": ";
    json_string(out, source.case_revision_id);
    out << ", \"case_checksum\": ";
    json_string(out, source.case_checksum);
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
    out << ",\n    \"source_revisions\": ";
    if (metadata.source_revisions) {
        revision_provenance_json(
            out, *metadata.source_revisions);
    } else {
        out << "null";
    }
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
    out << "],\n    \"artifacts\": [";
    for (std::size_t i = 0; i < metadata.artifacts.size(); ++i) {
        const auto& artifact = metadata.artifacts[i];
        if (i != 0) out << ", ";
        out << "{\"id\": ";
        json_string(out, artifact.id);
        out << ", \"artifact_type\": ";
        json_string(out, artifact.artifact_type);
        out << ", \"schema_version\": ";
        json_string(out, artifact.schema_version);
        out << ", \"revision\": ";
        json_string(out, artifact.revision);
        out << ", \"checksum_sha256\": ";
        json_string(out, artifact.checksum_sha256);
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

void readiness_json(
    std::ostream& out,
    const ReadinessSummary& readiness) {
    out << "{\"calculatable\": "
        << (readiness.calculatable ? "true" : "false")
        << ", \"layers\": [";
    for (std::size_t i = 0; i < readiness.layers.size(); ++i) {
        if (i != 0) out << ", ";
        const auto& layer = readiness.layers[i];
        out << "{\"id\": ";
        json_string(out, layer.id);
        out << ", \"state\": ";
        json_string(out, to_string(layer.state));
        out << ", \"diagnostic_codes\": [";
        for (std::size_t code = 0;
             code < layer.diagnostic_codes.size(); ++code) {
            if (code != 0) out << ", ";
            json_string(out, layer.diagnostic_codes[code]);
        }
        out << "]}";
    }
    out << "], \"entities\": [";
    for (std::size_t i = 0; i < readiness.entities.size(); ++i) {
        if (i != 0) out << ", ";
        const auto& entity = readiness.entities[i];
        out << "{\"entity_type\": ";
        json_string(out, entity.entity_type);
        out << ", \"entity_id\": ";
        json_string(out, entity.entity_id);
        out << ", \"state\": ";
        json_string(out, to_string(entity.state));
        out << ", \"diagnostic_codes\": [";
        for (std::size_t code = 0;
             code < entity.diagnostic_codes.size(); ++code) {
            if (code != 0) out << ", ";
            json_string(out, entity.diagnostic_codes[code]);
        }
        out << "]}";
    }
    out << "]}";
}

void map_output_quality_json(
    std::ostream& out,
    const PerformanceMapOutputQualitySummary& output) {
    out << "{\"name\": ";
    json_string(out, output.name);
    out << ", \"minimum\": ";
    json_number(out, output.minimum);
    out << ", \"maximum\": ";
    json_number(out, output.maximum);
    out << ", \"maximum_absolute_primary_slope\": ";
    json_number(out, output.maximum_absolute_primary_slope);
    out << ", \"maximum_absolute_primary_slope_jump\": ";
    json_number(out, output.maximum_absolute_primary_slope_jump);
    out << ", \"maximum_absolute_family_slope\": ";
    json_number(out, output.maximum_absolute_family_slope);
    out << ", \"declared_constraint\": ";
    if (output.constraint_minimum || output.constraint_maximum) {
        out << "{\"minimum\": ";
        if (output.constraint_minimum) {
            json_number(out, *output.constraint_minimum);
        } else {
            out << "null";
        }
        out << ", \"maximum\": ";
        if (output.constraint_maximum) {
            json_number(out, *output.constraint_maximum);
        } else {
            out << "null";
        }
        out << ", \"minimum_inclusive\": "
            << (output.constraint_minimum_inclusive
                    ? "true" : "false")
            << ", \"maximum_inclusive\": "
            << (output.constraint_maximum_inclusive
                    ? "true" : "false")
            << '}';
    } else {
        out << "null";
    }
    out << ", \"minimum_lower_margin\": ";
    if (output.minimum_lower_margin) {
        json_number(out, *output.minimum_lower_margin);
    } else {
        out << "null";
    }
    out << ", \"minimum_upper_margin\": ";
    if (output.minimum_upper_margin) {
        json_number(out, *output.minimum_upper_margin);
    } else {
        out << "null";
    }
    out << '}';
}

void advisory_codes_json(
    std::ostream& out,
    const std::vector<std::string>& codes) {
    out << '[';
    for (std::size_t i = 0; i < codes.size(); ++i) {
        if (i != 0) out << ", ";
        json_string(out, codes[i]);
    }
    out << ']';
}

void performance_map_quality_json(
    std::ostream& out,
    const PerformanceMapQualitySummary& quality) {
    out << "{\"schema_version\": ";
    json_string(out, quality.schema_version);
    out << ", \"artifact_id\": ";
    json_string(out, quality.artifact_id);
    out << ", \"conditioned\": "
        << (quality.conditioned ? "true" : "false");
    out << ", \"condition_domain\": ";
    if (quality.conditioned) {
        out << "{\"minimum\": ";
        json_number(out, quality.condition_minimum);
        out << ", \"maximum\": ";
        json_number(out, quality.condition_maximum);
        out << '}';
    } else {
        out << "null";
    }
    out << ", \"common_family_domain\": ";
    if (quality.conditioned &&
        quality.has_global_common_family_domain) {
        out << "{\"minimum\": ";
        json_number(out, quality.common_family_minimum);
        out << ", \"maximum\": ";
        json_number(out, quality.common_family_maximum);
        out << '}';
    } else {
        out << "null";
    }
    out << ", \"minimum_adjacent_family_overlap\": ";
    json_number(out, quality.minimum_adjacent_family_overlap);
    out << ", \"minimum_adjacent_primary_overlap\": ";
    json_number(out, quality.minimum_adjacent_primary_overlap);
    out << ", \"layers\": [";
    for (std::size_t i = 0; i < quality.layers.size(); ++i) {
        if (i != 0) out << ", ";
        const auto& layer = quality.layers[i];
        out << "{\"layer_index\": " << layer.layer_index
            << ", \"condition_coordinate\": ";
        if (layer.has_condition_coordinate) {
            json_number(out, layer.condition_coordinate);
        } else {
            out << "null";
        }
        out << ", \"curve_count\": " << layer.curve_count
            << ", \"sample_count\": " << layer.sample_count
            << ", \"family_domain\": {\"minimum\": ";
        json_number(out, layer.family_minimum);
        out << ", \"maximum\": ";
        json_number(out, layer.family_maximum);
        out << "}, \"common_primary_domain\": ";
        if (layer.has_global_common_primary_domain) {
            out << "{\"minimum\": ";
            json_number(out, layer.common_primary_minimum);
            out << ", \"maximum\": ";
            json_number(out, layer.common_primary_maximum);
            out << '}';
        } else {
            out << "null";
        }
        out << ", \"minimum_adjacent_primary_overlap\": ";
        json_number(out, layer.minimum_adjacent_primary_overlap);
        out << ", \"outputs\": [";
        for (std::size_t output = 0;
             output < layer.outputs.size(); ++output) {
            if (output != 0) out << ", ";
            map_output_quality_json(out, layer.outputs[output]);
        }
        out << "], \"advisory_codes\": ";
        advisory_codes_json(out, layer.advisory_codes);
        out << '}';
    }
    out << "], \"condition_outputs\": [";
    for (std::size_t i = 0; i < quality.outputs.size(); ++i) {
        if (i != 0) out << ", ";
        out << "{\"name\": ";
        json_string(out, quality.outputs[i].name);
        out << ", \"maximum_absolute_condition_slope\": ";
        json_number(
            out,
            quality.outputs[i].maximum_absolute_condition_slope);
        out << '}';
    }
    out << "], \"advisory_codes\": ";
    advisory_codes_json(out, quality.advisory_codes);
    out << '}';
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

namespace {

void assembly_component_json(
    std::ostream& out,
    const platform::ComponentDefinition& component,
    const std::string& indent) {
    out << indent << "{\n" << indent << "  \"id\": ";
    json_string(out, component.id);
    if (!component.label.empty()) {
        out << ",\n" << indent << "  \"label\": ";
        json_string(out, component.label);
    }
    out << ",\n" << indent << "  \"kind\": ";
    json_string(out, component.kind);
    if (!component.version.empty()) {
        out << ",\n" << indent << "  \"version\": ";
        json_string(out, component.version);
    }
    const auto bindings = [&](const char* key, const auto& values) {
        if (values.empty()) return;
        out << ",\n" << indent << "  \"" << key << "\": {";
        std::size_t index = 0;
        for (const auto& [name, value] : values) {
            if (index++ != 0U) out << ", ";
            json_string(out, name);
            out << ": ";
            json_string(out, value);
        }
        out << "}";
    };
    bindings("media", component.medium_bindings);
    bindings("materials", component.material_bindings);
    bindings("artifacts", component.artifact_bindings);
    if (!component.parameters.empty()) {
        out << ",\n" << indent << "  \"parameters\": ";
        scalar_map(out, component.parameters, indent + "    ");
    }
    out << "\n" << indent << "}";
}

void assembly_connection_json(
    std::ostream& out,
    const platform::ConnectionDefinition& connection,
    const std::string& indent) {
    out << indent << "{\"id\": ";
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
        scalar_map(out, connection.parameters, indent + "  ");
    }
    out << "}";
}

void assembly_json(
    std::ostream& out,
    const platform::AssemblyDefinition& assembly,
    const std::string& indent) {
    const auto child = indent + "  ";
    const auto item = child + "  ";
    out << indent << "{\n" << child << "\"id\": ";
    json_string(out, assembly.id);
    if (!assembly.label.empty()) {
        out << ",\n" << child << "\"label\": ";
        json_string(out, assembly.label);
    }
    out << ",\n" << child << "\"ports\": [";
    if (!assembly.ports.empty()) out << "\n";
    for (std::size_t index = 0; index < assembly.ports.size(); ++index) {
        out << item << "{\"name\": ";
        json_string(out, assembly.ports[index].name);
        out << ", \"endpoint\": ";
        json_string(out, assembly.ports[index].endpoint);
        out << "}" << (index + 1U == assembly.ports.size() ? "\n" : ",\n");
    }
    out << child << "],\n" << child << "\"parameters\": [";
    if (!assembly.parameters.empty()) out << "\n";
    for (std::size_t index = 0;
         index < assembly.parameters.size(); ++index) {
        out << item << "{\"name\": ";
        json_string(out, assembly.parameters[index].name);
        out << ", \"target\": ";
        json_string(out, assembly.parameters[index].target);
        out << "}"
            << (index + 1U == assembly.parameters.size()
                    ? "\n" : ",\n");
    }
    out << child << "],\n" << child << "\"components\": [";
    if (!assembly.components.empty()) out << "\n";
    for (std::size_t index = 0; index < assembly.components.size(); ++index) {
        assembly_component_json(out, assembly.components[index], item);
        out << (index + 1U == assembly.components.size() ? "\n" : ",\n");
    }
    out << child << "],\n" << child << "\"assemblies\": [";
    if (!assembly.assemblies.empty()) out << "\n";
    for (std::size_t index = 0; index < assembly.assemblies.size(); ++index) {
        assembly_json(out, assembly.assemblies[index], item);
        out << (index + 1U == assembly.assemblies.size() ? "\n" : ",\n");
    }
    out << child << "],\n" << child << "\"connections\": [";
    if (!assembly.connections.empty()) out << "\n";
    for (std::size_t index = 0; index < assembly.connections.size(); ++index) {
        assembly_connection_json(out, assembly.connections[index], item);
        out << (index + 1U == assembly.connections.size() ? "\n" : ",\n");
    }
    out << child << "]\n" << indent << "}";
}

}  // namespace

void serialize_topology(
    std::ostringstream& out,
    const platform::ModelDocument& document,
    const std::string& schema_version) {
    out << "{\n  \"schema_version\": ";
    json_string(out, schema_version);
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
    out << "    ],\n    \"assemblies\": [";
    if (!document.assemblies.empty()) out << "\n";
    for (std::size_t i = 0; i < document.assemblies.size(); ++i) {
        assembly_json(out, document.assemblies[i], "      ");
        out << (i + 1U == document.assemblies.size() ? "\n" : ",\n");
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
    out << "    ]\n  }";
}

std::string serialize_topology_document_json(
    const platform::ModelDocument& document) {
    std::ostringstream out;
    serialize_topology(
        out, document, "thermox.topology/v1");
    out << "\n}\n";
    return out.str();
}

std::string serialize_case_document_json(
    const platform::CaseDefinition& simulation_case) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": \"thermox.case/v1\","
           "\n  \"case\": {\n    \"id\": ";
    json_string(out, simulation_case.id);
    if (!simulation_case.label.empty()) {
        out << ",\n    \"label\": ";
        json_string(out, simulation_case.label);
    }
    out << ",\n    \"mode\": ";
    json_string(out, simulation_case.mode);
    if (!simulation_case.parameter_overrides.empty()) {
        out << ",\n    \"parameter_overrides\": ";
        scalar_map(
            out, simulation_case.parameter_overrides, "      ");
    }
    if (!simulation_case.fixed_values.empty()) {
        out << ",\n    \"fixed_values\": ";
        scalar_map(
            out, simulation_case.fixed_values, "      ");
    }
    if (!simulation_case.initial_guesses.empty()) {
        out << ",\n    \"initial_guesses\": ";
        scalar_map(
            out, simulation_case.initial_guesses, "      ");
    }
    if (!simulation_case.solver_options.empty()) {
        out << ",\n    \"solver_options\": ";
        scalar_map(
            out, simulation_case.solver_options, "      ");
    }
    out << "\n  }\n}\n";
    return out.str();
}

std::string serialize_calibration_document_json(
    const platform::CalibrationDefinition& calibration) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": \"thermox.calibration/v1\","
           "\n  \"calibration\": ";
    calibration_definition_json(out, calibration, "  ");
    out << "\n}\n";
    return out.str();
}

std::string serialize_model_document_json(
    const platform::ModelDocument& document) {
    if (document.schema_version != "thermox.model/v2") {
        throw std::invalid_argument(
            "unsupported model schema_version for serialization: " +
            document.schema_version);
    }
    std::ostringstream out;
    serialize_topology(out, document, document.schema_version);
    out << ",\n  \"cases\": [";
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
        if (!simulation_case.parameter_overrides.empty()) {
            out << ",\n      \"parameter_overrides\": ";
            scalar_map(
                out, simulation_case.parameter_overrides,
                "        ");
        }
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
    out << ",\n  \"native_extensions\": [";
    for (std::size_t i = 0;
         i < response.native_extensions.size(); ++i) {
        if (i != 0) out << ", ";
        const auto& extension = response.native_extensions[i];
        out << "{\"package_id\": ";
        json_string(out, extension.package_id);
        out << ", \"package_version\": ";
        json_string(out, extension.package_version);
        out << "}";
    }
    out << "],\n  \"unit_dimensions\": [";
    for (std::size_t i = 0;
         i < response.unit_dimensions.size(); ++i) {
        if (i != 0) out << ", ";
        const auto& dimension = response.unit_dimensions[i];
        const auto display_unit =
            [&](const CatalogDisplayUnitType& unit) {
                out << "{\"symbol\": ";
                json_string(out, unit.symbol);
                out << ", \"scale_from_si\": ";
                json_number(out, unit.scale_from_si);
                out << ", \"offset_from_si\": ";
                json_number(out, unit.offset_from_si);
                out << "}";
            };
        out << "{\"dimension\": ";
        json_string(out, dimension.dimension);
        out << ", \"canonical_unit\": ";
        json_string(out, dimension.canonical_unit);
        out << ", \"si_display\": ";
        display_unit(dimension.si_display);
        out << ", \"engineering_display\": ";
        display_unit(dimension.engineering_display);
        out << ", \"accepted_units\": [";
        for (std::size_t j = 0;
             j < dimension.accepted_units.size(); ++j) {
            if (j != 0) out << ", ";
            const auto& unit = dimension.accepted_units[j];
            out << "{\"symbol\": ";
            json_string(out, unit.symbol);
            out << ", \"aliases\": [";
            for (std::size_t k = 0;
                 k < unit.aliases.size(); ++k) {
                if (k != 0) out << ", ";
                json_string(out, unit.aliases[k]);
            }
            out << "], \"scale_to_si\": ";
            json_number(out, unit.scale_to_si);
            out << ", \"offset_to_si\": ";
            json_number(out, unit.offset_to_si);
            out << "}";
        }
        out << "]}";
    }
    out << "],\n  \"components\": [";
    for (std::size_t i = 0; i < response.components.size(); ++i) {
        if (i != 0) out << ", ";
        const auto& component = response.components[i];
        out << "{\"kind\": ";
        json_string(out, component.kind);
        out << ", \"version\": ";
        json_string(out, component.version);
        out << ", \"template_kind\": ";
        json_string(out, component.template_kind);
        out << ", \"display_name\": ";
        json_string(out, component.display_name);
        out << ", \"category\": ";
        json_string(out, component.category);
        out << ", \"model_name\": ";
        json_string(out, component.model_name);
        out << ", \"system_boundary_role\": ";
        json_string(out, component.system_boundary_role);
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
        out << ", \"connection_kind\": ";
        json_string(out, domain.connection_kind);
        out << ", \"variables\": [";
        for (std::size_t j = 0; j < domain.variables.size(); ++j) {
            if (j != 0) out << ", ";
            out << "{\"name\": ";
            json_string(out, domain.variables[j].name);
            out << ", \"dimension\": ";
            json_string(out, domain.variables[j].dimension);
            out << ", \"initial_value_si\": "
                << domain.variables[j].initial_value_si
                << ", \"scale_si\": "
                << domain.variables[j].scale_si
                << ", \"expand_species\": "
                << (domain.variables[j].expand_species
                        ? "true"
                        : "false")
                << "}";
        }
        out << "]}";
    }
    out << "],\n  \"correlation_templates\": [";
    for (std::size_t i = 0;
         i < response.correlation_templates.size(); ++i) {
        if (i != 0) out << ", ";
        const auto& descriptor = response.correlation_templates[i];
        out << "{\"id\": ";
        json_string(out, descriptor.id);
        out << ", \"version\": ";
        json_string(out, descriptor.version);
        out << ", \"display_name\": ";
        json_string(out, descriptor.display_name);
        out << ", \"category\": ";
        json_string(out, descriptor.category);
        out << ", \"reference\": ";
        json_string(out, descriptor.reference);
        out << ", \"inputs\": [";
        for (std::size_t j = 0; j < descriptor.inputs.size(); ++j) {
            if (j != 0) out << ", ";
            out << "{\"name\": ";
            json_string(out, descriptor.inputs[j].name);
            out << ", \"dimension\": ";
            json_string(out, descriptor.inputs[j].dimension);
            out << '}';
        }
        out << "], \"output\": {\"name\": ";
        json_string(out, descriptor.output.name);
        out << ", \"dimension\": ";
        json_string(out, descriptor.output.dimension);
        out << "}, \"coefficients\": [";
        for (std::size_t j = 0;
             j < descriptor.coefficients.size(); ++j) {
            if (j != 0) out << ", ";
            const auto& coefficient = descriptor.coefficients[j];
            out << "{\"name\": ";
            json_string(out, coefficient.name);
            out << ", \"dimension\": ";
            json_string(out, coefficient.dimension);
            out << ", \"has_default\": "
                << (coefficient.has_default ? "true" : "false");
            out << ", \"default_value_si\": ";
            json_number(out, coefficient.default_value_si);
            out << ", \"lower_bound\": ";
            json_number(out, coefficient.lower_bound);
            out << ", \"upper_bound\": ";
            json_number(out, coefficient.upper_bound);
            out << ", \"lower_inclusive\": "
                << (coefficient.lower_inclusive ? "true" : "false");
            out << ", \"upper_inclusive\": "
                << (coefficient.upper_inclusive ? "true" : "false")
                << '}';
        }
        out << "], \"expression\": ";
        json_string(out, descriptor.expression);
        out << ", \"regime\": ";
        json_string(out, descriptor.regime);
        out << ", \"applicability\": [";
        for (std::size_t j = 0;
             j < descriptor.applicability.size(); ++j) {
            if (j != 0) out << ", ";
            const auto& range = descriptor.applicability[j];
            out << "{\"input\": ";
            json_string(out, range.input);
            out << ", \"has_minimum\": "
                << (range.has_minimum ? "true" : "false");
            out << ", \"minimum_si\": ";
            json_number(out, range.minimum_si);
            out << ", \"has_maximum\": "
                << (range.has_maximum ? "true" : "false");
            out << ", \"maximum_si\": ";
            json_number(out, range.maximum_si);
            out << ", \"minimum_inclusive\": "
                << (range.minimum_inclusive ? "true" : "false");
            out << ", \"maximum_inclusive\": "
                << (range.maximum_inclusive ? "true" : "false")
                << '}';
        }
        out << "]}";
    }
    out << "],\n  \"correlation_family_templates\": [";
    for (std::size_t i = 0;
         i < response.correlation_family_templates.size(); ++i) {
        if (i != 0) out << ", ";
        const auto& descriptor =
            response.correlation_family_templates[i];
        out << "{\"id\": ";
        json_string(out, descriptor.id);
        out << ", \"version\": ";
        json_string(out, descriptor.version);
        out << ", \"display_name\": ";
        json_string(out, descriptor.display_name);
        out << ", \"category\": ";
        json_string(out, descriptor.category);
        out << ", \"reference\": ";
        json_string(out, descriptor.reference);
        out << ", \"scope\": ";
        json_string(out, descriptor.scope);
        out << ", \"bindings\": [";
        for (std::size_t j = 0; j < descriptor.bindings.size(); ++j) {
            if (j != 0) out << ", ";
            const auto& binding = descriptor.bindings[j];
            out << "{\"template_id\": ";
            json_string(out, binding.template_id);
            out << ", \"coefficients\": {";
            std::size_t coefficient_index = 0;
            for (const auto& [name, value] : binding.coefficients) {
                if (coefficient_index++ != 0) out << ", ";
                json_string(out, name);
                out << ": ";
                json_number(out, value);
            }
            out << "}, \"candidate_id\": ";
            json_string(out, binding.candidate_id);
            out << ", \"priority\": " << binding.priority;
            out << ", \"flow_regimes\": [";
            for (std::size_t k = 0;
                 k < binding.flow_regimes.size(); ++k) {
                if (k != 0) out << ", ";
                json_string(out, binding.flow_regimes[k]);
            }
            out << "], \"fallback_for_unmapped_flow_regime\": "
                << (binding.fallback_for_unmapped_flow_regime
                        ? "true"
                        : "false")
                << '}';
        }
        out << "]}";
    }
    out << "],\n  \"regime_map_templates\": [";
    for (std::size_t i = 0;
         i < response.regime_map_templates.size(); ++i) {
        if (i != 0) out << ", ";
        const auto& descriptor = response.regime_map_templates[i];
        out << "{\"id\": ";
        json_string(out, descriptor.id);
        out << ", \"version\": ";
        json_string(out, descriptor.version);
        out << ", \"display_name\": ";
        json_string(out, descriptor.display_name);
        out << ", \"category\": ";
        json_string(out, descriptor.category);
        out << ", \"reference\": ";
        json_string(out, descriptor.reference);
        out << ", \"scope\": ";
        json_string(out, descriptor.scope);
        out << ", \"inputs\": [";
        for (std::size_t j = 0; j < descriptor.inputs.size(); ++j) {
            if (j != 0) out << ", ";
            out << "{\"name\": ";
            json_string(out, descriptor.inputs[j].name);
            out << ", \"dimension\": ";
            json_string(out, descriptor.inputs[j].dimension);
            out << '}';
        }
        out << "], \"regions\": [";
        for (std::size_t j = 0; j < descriptor.regions.size(); ++j) {
            if (j != 0) out << ", ";
            const auto& region = descriptor.regions[j];
            out << "{\"id\": ";
            json_string(out, region.id);
            out << ", \"regime\": ";
            json_string(out, region.regime);
            out << ", \"priority\": " << region.priority;
            out << ", \"branches\": [";
            for (std::size_t k = 0; k < region.branches.size(); ++k) {
                if (k != 0) out << ", ";
                const auto& branch = region.branches[k];
                out << "{\"id\": ";
                json_string(out, branch.id);
                out << ", \"priority\": " << branch.priority;
                out << ", \"criteria\": [";
                for (std::size_t m = 0;
                     m < branch.criteria.size(); ++m) {
                    if (m != 0) out << ", ";
                    const auto& criterion = branch.criteria[m];
                    out << "{\"expression\": ";
                    json_string(out, criterion.expression);
                    out << ", \"dimension\": ";
                    json_string(out, criterion.dimension);
                    out << ", \"has_minimum\": "
                        << (criterion.has_minimum ? "true" : "false");
                    out << ", \"minimum_si\": ";
                    json_number(out, criterion.minimum_si);
                    out << ", \"has_maximum\": "
                        << (criterion.has_maximum ? "true" : "false");
                    out << ", \"maximum_si\": ";
                    json_number(out, criterion.maximum_si);
                    out << ", \"minimum_inclusive\": "
                        << (criterion.minimum_inclusive
                                ? "true" : "false");
                    out << ", \"maximum_inclusive\": "
                        << (criterion.maximum_inclusive
                                ? "true" : "false") << '}';
                }
                out << "]}";
            }
            out << "]}";
        }
        out << "]}";
    }
    out << "]\n}\n";
    return out.str();
}

std::string serialize_correlation_instantiation_response_json(
    const InstantiateCorrelationResponse& response) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": ";
    json_string(out, response.schema_version);
    out << ",\n  \"status\": ";
    json_string(out, to_string(response.status));
    out << ",\n  \"error\": ";
    error_json(out, response.error);
    out << ",\n  \"catalog_fingerprint\": ";
    json_string(out, response.catalog_fingerprint);
    out << ",\n  \"artifact\": ";
    if (!response.succeeded()) {
        out << "null";
    } else {
        out << "{\"id\": ";
        json_string(out, response.artifact.id);
        out << ", \"artifact_type\": ";
        json_string(out, platform::correlation_artifact_type);
        out << ", \"schema_version\": ";
        json_string(out, response.artifact.schema_version);
        out << ", \"revision\": ";
        json_string(out, response.artifact.revision);
        out << ", \"checksum_sha256\": ";
        json_string(out, response.artifact.checksum_sha256);
        out << ", \"payload\": "
            << response.canonical_payload_json << '}';
    }
    out << "\n}\n";
    return out.str();
}

std::string serialize_regime_map_instantiation_response_json(
    const InstantiateRegimeMapResponse& response) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": ";
    json_string(out, response.schema_version);
    out << ",\n  \"status\": ";
    json_string(out, to_string(response.status));
    out << ",\n  \"error\": ";
    error_json(out, response.error);
    out << ",\n  \"catalog_fingerprint\": ";
    json_string(out, response.catalog_fingerprint);
    out << ",\n  \"artifact\": ";
    if (!response.succeeded()) {
        out << "null";
    } else {
        out << "{\"id\": ";
        json_string(out, response.artifact.id);
        out << ", \"artifact_type\": ";
        json_string(out, platform::regime_map_artifact_type);
        out << ", \"schema_version\": ";
        json_string(out, response.artifact.schema_version);
        out << ", \"revision\": ";
        json_string(out, response.artifact.revision);
        out << ", \"checksum_sha256\": ";
        json_string(out, response.artifact.checksum_sha256);
        out << ", \"payload\": "
            << response.canonical_payload_json << '}';
    }
    out << "\n}\n";
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
        << ", \"largest_structural_block_size\": "
        << response.compilation.largest_structural_block_size
        << ", \"structural_blocks\": [";
    for (std::size_t index = 0;
         index < response.compilation.structural_blocks.size();
         ++index) {
        if (index != 0) out << ", ";
        const auto& block =
            response.compilation.structural_blocks[index];
        out << "{\"variable_names\": [";
        for (std::size_t name = 0;
             name < block.variable_names.size(); ++name) {
            if (name != 0) out << ", ";
            json_string(out, block.variable_names[name]);
        }
        out << "], \"equation_names\": [";
        for (std::size_t name = 0;
             name < block.equation_names.size(); ++name) {
            if (name != 0) out << ", ";
            json_string(out, block.equation_names[name]);
        }
        out << "], \"suggested_tear_variable_names\": [";
        for (std::size_t name = 0;
             name < block.suggested_tear_variable_names.size(); ++name) {
            if (name != 0) out << ", ";
            json_string(out, block.suggested_tear_variable_names[name]);
        }
        out << "], \"acyclic_after_suggested_tears\": "
            << (block.acyclic_after_suggested_tears ? "true" : "false")
            << ", \"structural_nonzero_count\": "
            << block.structural_nonzero_count
            << ", \"suggested_inner_variable_count\": "
            << block.suggested_inner_variable_count
            << ", \"suggested_inner_nonzero_count\": "
            << block.suggested_inner_nonzero_count
            << ", \"suggested_tear_coupling_nonzero_count\": "
            << block.suggested_tear_coupling_nonzero_count
            << ", \"suggested_dense_schur_entry_count\": "
            << block.suggested_dense_schur_entry_count
            << "}";
    }
    out << "]"
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
    out << "]},\n  \"readiness\": ";
    readiness_json(out, response.readiness);
    out << ",\n  \"performance_map_quality\": [";
    for (std::size_t i = 0;
         i < response.performance_map_quality.size(); ++i) {
        if (i != 0) out << ", ";
        performance_map_quality_json(
            out, response.performance_map_quality[i]);
    }
    out << ']';
    out << ",\n  \"diagnostics\": [";
    for (std::size_t i = 0;
         i < response.diagnostics.size(); ++i) {
        if (i != 0) out << ", ";
        diagnostic_json(out, response.diagnostics[i]);
    }
    out << "]";
    out << "\n}\n";
    return out.str();
}

std::string serialize_performance_map_quality_json(
    const PerformanceMapQualitySummary& quality) {
    std::ostringstream out;
    performance_map_quality_json(out, quality);
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
    out << ", \"final_maximum_absolute_normalized_residual\": ";
    json_number(
        out,
        response.diagnostics
            .final_maximum_absolute_normalized_residual);
    out << ", \"limiting_residual\": ";
    json_string(out, response.diagnostics.limiting_residual);
    out << ", \"final_step_norm\": ";
    json_number(out, response.diagnostics.final_step_norm);
    out << ", \"function_evaluations\": "
        << response.diagnostics.function_evaluations
        << ", \"jacobian_evaluations\": "
        << response.diagnostics.jacobian_evaluations
        << ", \"linear_solver_evaluations\": "
        << response.diagnostics.linear_solver_evaluations
        << ", \"symbolic_factorizations\": "
        << response.diagnostics.symbolic_factorizations
        << ", \"numeric_factorizations\": "
        << response.diagnostics.numeric_factorizations;
    out << ", \"last_linear_backward_error\": ";
    json_number(
        out, response.diagnostics.last_linear_backward_error);
    out << ", \"maximum_linear_backward_error\": ";
    json_number(
        out, response.diagnostics.maximum_linear_backward_error);
    out << ", \"structural_block_solves\": "
        << response.diagnostics.structural_block_solves
        << ", \"largest_linear_system_size\": "
        << response.diagnostics.largest_linear_system_size
        << ", \"structural_tearing_attempts\": "
        << response.diagnostics.structural_tearing_attempts
        << ", \"structural_tearing_successes\": "
        << response.diagnostics.structural_tearing_successes
        << ", \"structural_tearing_fallbacks\": "
        << response.diagnostics.structural_tearing_fallbacks
        << ", \"largest_tearing_inner_system_size\": "
        << response.diagnostics.largest_tearing_inner_system_size
        << ", \"largest_tearing_outer_system_size\": "
        << response.diagnostics.largest_tearing_outer_system_size
        << ", \"largest_tearing_inner_nonzero_count\": "
        << response.diagnostics.largest_tearing_inner_nonzero_count
        << ", \"last_structural_tearing_fallback\": ";
    json_string(
        out, response.diagnostics.last_structural_tearing_fallback);
    out
        << ", \"failed_structural_block\": ";
    json_string(
        out, response.diagnostics.failed_structural_block);
    out << ", \"linear_solver_backend\": ";
    json_string(
        out, response.diagnostics.linear_solver_backend);
    out
        << ", \"message\": ";
    json_string(out, response.diagnostics.message);
    out << "},\n  \"continuation\": {\"enabled\": "
        << (response.continuation.enabled ? "true" : "false")
        << ", \"converged\": "
        << (response.continuation.converged ? "true" : "false")
        << ", \"used_informed_path\": "
        << (response.continuation.used_informed_path
                ? "true"
                : "false")
        << ", \"reached_parameter\": ";
    json_number(out, response.continuation.reached_parameter);
    out << ", \"accepted_stages\": "
        << response.continuation.accepted_stages
        << ", \"rejected_stages\": "
        << response.continuation.rejected_stages
        << ", \"message\": ";
    json_string(out, response.continuation.message);
    out << ", \"stages\": [";
    for (std::size_t index = 0;
         index < response.continuation.stages.size(); ++index) {
        if (index != 0) out << ", ";
        const auto& stage =
            response.continuation.stages[index];
        out << "{\"start_parameter\": ";
        json_number(out, stage.start_parameter);
        out << ", \"target_parameter\": ";
        json_number(out, stage.target_parameter);
        out << ", \"accepted\": "
            << (stage.accepted ? "true" : "false")
            << ", \"nonlinear_iterations\": "
            << stage.nonlinear_iterations
            << ", \"final_residual_norm\": ";
        json_number(out, stage.final_residual_norm);
        out << ", \"final_maximum_absolute_normalized_residual\": ";
        json_number(
            out,
            stage.final_maximum_absolute_normalized_residual);
        out << ", \"limiting_residual\": ";
        json_string(out, stage.limiting_residual);
        out << ", \"maximum_linear_backward_error\": ";
        json_number(
            out, stage.maximum_linear_backward_error);
        out << ", \"structural_block_solves\": "
            << stage.structural_block_solves
            << ", \"largest_linear_system_size\": "
            << stage.largest_linear_system_size
            << ", \"structural_tearing_attempts\": "
            << stage.structural_tearing_attempts
            << ", \"structural_tearing_successes\": "
            << stage.structural_tearing_successes
            << ", \"structural_tearing_fallbacks\": "
            << stage.structural_tearing_fallbacks
            << ", \"largest_tearing_inner_system_size\": "
            << stage.largest_tearing_inner_system_size
            << ", \"largest_tearing_outer_system_size\": "
            << stage.largest_tearing_outer_system_size
            << ", \"largest_tearing_inner_nonzero_count\": "
            << stage.largest_tearing_inner_nonzero_count
            << ", \"last_structural_tearing_fallback\": ";
        json_string(out, stage.last_structural_tearing_fallback);
        out
            << ", \"failed_structural_block\": ";
        json_string(out, stage.failed_structural_block);
        out << ", \"message\": ";
        json_string(out, stage.message);
        out << '}';
    }
    out << "]},\n  \"graph\": ";
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

std::string serialize_calibration_response_json(
    const CalibrationResponse& response) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": ";
    json_string(out, result_schema_v3);
    out << ",\n  \"status\": ";
    json_string(out, to_string(response.status));
    out << ",\n  \"error\": ";
    error_json(out, response.error);
    out << ",\n  \"metadata\": ";
    execution_metadata_json(out, response.metadata);
    out << ",\n  \"calibration_id\": ";
    json_string(out, response.calibration_id);
    out << ",\n  \"diagnostics\": {\"converged\": "
        << (response.diagnostics.converged ? "true" : "false")
        << ", \"iterations\": "
        << response.diagnostics.iterations
        << ", \"objective_evaluations\": "
        << response.diagnostics.objective_evaluations
        << ", \"initial_objective\": ";
    json_number(out, response.diagnostics.initial_objective);
    out << ", \"final_objective\": ";
    json_number(out, response.diagnostics.final_objective);
    out << ", \"message\": ";
    json_string(out, response.diagnostics.message);
    out << "},\n  \"parameters\": [";
    for (std::size_t i = 0; i < response.parameters.size(); ++i) {
        if (i != 0) out << ", ";
        const auto& parameter = response.parameters[i];
        out << "{\"id\": ";
        json_string(out, parameter.id);
        out << ", \"scope\": ";
        json_string(out, parameter.scope);
        out << ", \"dimension\": ";
        json_string(out, parameter.dimension);
        out << ", \"initial_value_si\": ";
        json_number(out, parameter.initial_value_si);
        out << ", \"fitted_value_si\": ";
        json_number(out, parameter.fitted_value_si);
        out << ", \"lower_bound_si\": ";
        json_number(out, parameter.lower_bound_si);
        out << ", \"upper_bound_si\": ";
        json_number(out, parameter.upper_bound_si);
        out << ", \"targets\": [";
        for (std::size_t target = 0;
             target < parameter.targets.size(); ++target) {
            if (target != 0) out << ", ";
            json_string(out, parameter.targets[target]);
        }
        out << "]}";
    }
    out << "],\n  \"observations\": [";
    for (std::size_t i = 0;
         i < response.observations.size(); ++i) {
        if (i != 0) out << ", ";
        const auto& observation = response.observations[i];
        out << "{\"id\": ";
        json_string(out, observation.id);
        out << ", \"case_id\": ";
        json_string(out, observation.case_id);
        out << ", \"target\": ";
        json_string(out, observation.target);
        out << ", \"dimension\": ";
        json_string(out, observation.dimension);
        out << ", \"measured_si\": ";
        json_number(out, observation.measured_si);
        out << ", \"predicted_si\": ";
        json_number(out, observation.predicted_si);
        out << ", \"sigma_si\": ";
        json_number(out, observation.sigma_si);
        out << ", \"residual_si\": ";
        json_number(out, observation.residual_si);
        out << ", \"normalized_residual\": ";
        json_number(out, observation.normalized_residual);
        out << "}";
    }
    out << "],\n  \"fitted_model_json\": ";
    json_string(out, response.fitted_model_json);
    out << "\n}\n";
    return out.str();
}

std::string serialize_engineering_study_response_json(
    const EngineeringStudyResponse& response) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": ";
    json_string(out, result_schema_v3);
    out << ",\n  \"status\": ";
    json_string(out, to_string(response.status));
    out << ",\n  \"error\": ";
    error_json(out, response.error);
    out << ",\n  \"diagnostics\": {"
        << "\"prediction_case_count\": "
        << response.diagnostics.prediction_case_count
        << ", \"observation_count\": "
        << response.diagnostics.observation_count
        << ", \"weighted_sum_squares\": ";
    json_number(
        out, response.diagnostics.weighted_sum_squares);
    out << ", \"rms_normalized_residual\": ";
    json_number(
        out,
        response.diagnostics.rms_normalized_residual);
    out << ", \"maximum_absolute_normalized_residual\": ";
    json_number(
        out,
        response.diagnostics
            .maximum_absolute_normalized_residual);
    out << "},\n  \"calibration\": "
        << serialize_calibration_response_json(
               response.calibration);
    out << ",  \"predictions\": [";
    for (std::size_t i = 0;
         i < response.predictions.size(); ++i) {
        if (i != 0) out << ", ";
        const auto& prediction = response.predictions[i];
        out << "{\"case_id\": ";
        json_string(out, prediction.case_id);
        out << ", \"weighted_sum_squares\": ";
        json_number(out, prediction.weighted_sum_squares);
        out << ", \"observations\": [";
        for (std::size_t observation_index = 0;
             observation_index <
             prediction.observations.size();
             ++observation_index) {
            if (observation_index != 0) out << ", ";
            const auto& observation =
                prediction.observations[observation_index];
            out << "{\"id\": ";
            json_string(out, observation.id);
            out << ", \"case_id\": ";
            json_string(out, observation.case_id);
            out << ", \"target\": ";
            json_string(out, observation.target);
            out << ", \"dimension\": ";
            json_string(out, observation.dimension);
            out << ", \"measured_si\": ";
            json_number(out, observation.measured_si);
            out << ", \"predicted_si\": ";
            json_number(out, observation.predicted_si);
            out << ", \"sigma_si\": ";
            json_number(out, observation.sigma_si);
            out << ", \"residual_si\": ";
            json_number(out, observation.residual_si);
            out << ", \"normalized_residual\": ";
            json_number(
                out, observation.normalized_residual);
            out << "}";
        }
        out << "], \"simulation\": "
            << serialize_steady_response_json(
                   prediction.simulation)
            << "}";
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
        << ", \"maximum_order_used\": "
        << response.diagnostics.maximum_order_used
        << ", \"nonlinear_solves\": "
        << response.diagnostics.nonlinear_solves
        << ", \"nonlinear_iterations\": "
        << response.diagnostics.nonlinear_iterations
        << ", \"symbolic_factorizations\": "
        << response.diagnostics.symbolic_factorizations
        << ", \"numeric_factorizations\": "
        << response.diagnostics.numeric_factorizations;
    out << ", \"maximum_linear_backward_error\": ";
    json_number(
        out,
        response.diagnostics.maximum_linear_backward_error);
    out << ", \"structural_block_solves\": "
        << response.diagnostics.structural_block_solves
        << ", \"largest_linear_system_size\": "
        << response.diagnostics.largest_linear_system_size
        << ", \"structural_tearing_attempts\": "
        << response.diagnostics.structural_tearing_attempts
        << ", \"structural_tearing_successes\": "
        << response.diagnostics.structural_tearing_successes
        << ", \"structural_tearing_fallbacks\": "
        << response.diagnostics.structural_tearing_fallbacks
        << ", \"largest_tearing_inner_system_size\": "
        << response.diagnostics.largest_tearing_inner_system_size
        << ", \"largest_tearing_outer_system_size\": "
        << response.diagnostics.largest_tearing_outer_system_size
        << ", \"largest_tearing_inner_nonzero_count\": "
        << response.diagnostics.largest_tearing_inner_nonzero_count
        << ", \"last_structural_tearing_fallback\": ";
    json_string(
        out, response.diagnostics.last_structural_tearing_fallback);
    out << ", \"linear_solver_backend\": ";
    json_string(
        out, response.diagnostics.linear_solver_backend);
    out
        << ", \"final_time\": ";
    json_number(out, response.diagnostics.final_time);
    out << ", \"last_step\": ";
    json_number(out, response.diagnostics.last_step);
    out << ", \"last_error_norm\": ";
    json_number(out, response.diagnostics.last_error_norm);
    out << ", \"maximum_accepted_error_norm\": ";
    json_number(
        out, response.diagnostics.maximum_accepted_error_norm);
    out << ", \"maximum_error_ratio\": ";
    json_number(out, response.diagnostics.maximum_error_ratio);
    out << ", \"limiting_error_variable\": ";
    json_string(out, response.diagnostics.limiting_error_variable);
    out << ", \"maximum_absolute_normalized_residual\": ";
    json_number(
        out,
        response.diagnostics
            .maximum_absolute_normalized_residual);
    out << ", \"limiting_nonlinear_residual\": ";
    json_string(
        out, response.diagnostics.limiting_nonlinear_residual);
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

std::string serialize_result_summary_json(
    const ResultSummary& summary) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": ";
    json_string(out, summary.schema_version);
    out << ",\n  \"mode\": ";
    json_string(out, summary.mode);
    out << ",\n  \"values\": [";
    for (std::size_t index = 0;
         index < summary.values.size();
         ++index) {
        if (index != 0U) {
            out << ", ";
        }
        const auto& value = summary.values[index];
        out << "{\"id\": ";
        json_string(out, value.id);
        out << ", \"dimension\": ";
        json_string(out, value.dimension);
        out << ", \"value_si\": ";
        json_number(out, value.value_si);
        out << ", \"aggregation\": ";
        json_string(out, to_string(value.aggregation));
        out << ", \"sample_time\": ";
        if (value.has_sample_time) {
            json_number(out, value.sample_time);
        } else {
            out << "null";
        }
        out << '}';
    }
    out << "],\n  \"engineering_acceptance\": ";
    if (!summary.engineering_acceptance) {
        out << "null";
    } else {
        const auto& acceptance = *summary.engineering_acceptance;
        out << "{\"passed\": "
            << (acceptance.passed ? "true" : "false")
            << ", \"passed_count\": "
            << acceptance.passed_count
            << ", \"failed_count\": "
            << acceptance.failed_count
            << ", \"criteria\": [";
        for (std::size_t index = 0;
             index < acceptance.criteria.size(); ++index) {
            if (index != 0U) out << ", ";
            const auto& criterion = acceptance.criteria[index];
            out << "{\"criterion_id\": ";
            json_string(out, criterion.criterion_id);
            out << ", \"projection_id\": ";
            json_string(out, criterion.projection_id);
            out << ", \"dimension\": ";
            json_string(out, criterion.dimension);
            out << ", \"actual_value_si\": ";
            json_number(out, criterion.actual_value_si);
            out << ", \"lower_bound_si\": ";
            if (criterion.lower_bound_si) {
                json_number(out, *criterion.lower_bound_si);
            } else {
                out << "null";
            }
            out << ", \"upper_bound_si\": ";
            if (criterion.upper_bound_si) {
                json_number(out, *criterion.upper_bound_si);
            } else {
                out << "null";
            }
            out << ", \"lower_inclusive\": "
                << (criterion.lower_inclusive ? "true" : "false")
                << ", \"upper_inclusive\": "
                << (criterion.upper_inclusive ? "true" : "false");
            out << ", \"lower_margin_si\": ";
            if (criterion.lower_margin_si) {
                json_number(out, *criterion.lower_margin_si);
            } else {
                out << "null";
            }
            out << ", \"upper_margin_si\": ";
            if (criterion.upper_margin_si) {
                json_number(out, *criterion.upper_margin_si);
            } else {
                out << "null";
            }
            out << ", \"limiting_margin_si\": ";
            json_number(out, criterion.limiting_margin_si);
            out << ", \"limiting_bound\": ";
            json_string(out, criterion.limiting_bound);
            out
                << ", \"passed\": "
                << (criterion.passed ? "true" : "false")
                << '}';
        }
        out << "]}";
    }
    out << "\n}\n";
    return out.str();
}

std::string serialize_job_record_json(
    const SimulationJobRecord& record) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": ";
    json_string(out, record.schema_version);
    out << ",\n  \"job_id\": ";
    json_string(out, record.job_id);
    out << ",\n  \"owner\": {\"team_id\": ";
    json_string(out, record.team_id);
    out << ", \"submitted_by_user_id\": ";
    json_string(out, record.submitted_by_user_id);
    out << "}";
    out << ",\n  \"revision\": " << record.revision;
    out << ",\n  \"created_at_unix_ms\": "
        << std::chrono::duration_cast<std::chrono::milliseconds>(
               record.created_at.time_since_epoch())
               .count();
    out << ",\n  \"state\": ";
    json_string(out, to_string(record.state));
    out << ",\n  \"request\": {\"schema_version\": ";
    json_string(out, record.request.schema_version);
    out << ", \"mode\": ";
    json_string(out, to_string(record.request.mode));
    out << ", \"case_id\": ";
    json_string(out, record.request.case_id);
    out << ", \"calibration_id\": ";
    json_string(out, record.request.calibration_id);
    out << ", \"source_revisions\": ";
    if (record.request.source_revisions) {
        revision_provenance_json(
            out, *record.request.source_revisions);
    } else {
        out << "null";
    }
    out << ", \"engineering_artifacts\": [";
    for (std::size_t index = 0;
         index <
         record.request.artifacts.performance_maps.size();
         ++index) {
        if (index != 0U) {
            out << ", ";
        }
        const auto& artifact =
            record.request.artifacts.performance_maps[index];
        out << "{\"id\": ";
        json_string(out, artifact.id);
        out << ", \"artifact_type\": ";
        json_string(
            out,
            platform::performance_map_artifact_type);
        out << ", \"schema_version\": ";
        json_string(out, artifact.schema_version);
        out << ", \"revision\": ";
        json_string(out, artifact.revision);
        out << ", \"checksum_sha256\": ";
        json_string(out, artifact.checksum_sha256);
        out << '}';
    }
    for (std::size_t index = 0;
         index < record.request.artifacts.correlations.size();
         ++index) {
        if (!record.request.artifacts.performance_maps.empty() ||
            index != 0U) {
            out << ", ";
        }
        const auto& artifact =
            record.request.artifacts.correlations[index];
        out << "{\"id\": ";
        json_string(out, artifact.id);
        out << ", \"artifact_type\": ";
        json_string(out, platform::correlation_artifact_type);
        out << ", \"schema_version\": ";
        json_string(out, artifact.schema_version);
        out << ", \"revision\": ";
        json_string(out, artifact.revision);
        out << ", \"checksum_sha256\": ";
        json_string(out, artifact.checksum_sha256);
        out << '}';
    }
    for (std::size_t index = 0;
         index < record.request.artifacts.regime_maps.size();
         ++index) {
        if (!record.request.artifacts.performance_maps.empty() ||
            !record.request.artifacts.correlations.empty() ||
            index != 0U) {
            out << ", ";
        }
        const auto& artifact =
            record.request.artifacts.regime_maps[index];
        out << "{\"id\": ";
        json_string(out, artifact.id);
        out << ", \"artifact_type\": ";
        json_string(out, platform::regime_map_artifact_type);
        out << ", \"schema_version\": ";
        json_string(out, artifact.schema_version);
        out << ", \"revision\": ";
        json_string(out, artifact.revision);
        out << ", \"checksum_sha256\": ";
        json_string(out, artifact.checksum_sha256);
        out << '}';
    }
    for (std::size_t index = 0;
         index < record.request.artifacts.references.size();
         ++index) {
        if (!record.request.artifacts.performance_maps.empty() ||
            !record.request.artifacts.correlations.empty() ||
            !record.request.artifacts.regime_maps.empty() ||
            index != 0U) {
            out << ", ";
        }
        const auto& reference =
            record.request.artifacts.references[index];
        out << "{\"id\": ";
        json_string(out, reference.id);
        out << ", \"artifact_type\": ";
        json_string(out, reference.artifact_type);
        out << ", \"schema_version\": ";
        json_string(out, reference.schema_version);
        out << ", \"revision\": ";
        json_string(out, reference.revision);
        out << ", \"checksum_sha256\": ";
        json_string(out, reference.checksum_sha256);
        out << '}';
    }
    out << ']';
    out << ", \"result_projections\": [";
    for (std::size_t index = 0;
         index < record.request.result_projections.size();
         ++index) {
        if (index != 0U) {
            out << ", ";
        }
        const auto& projection =
            record.request.result_projections[index];
        out << "{\"id\": ";
        json_string(out, projection.id);
        out << ", \"scope\": ";
        json_string(out, to_string(projection.scope));
        out << ", \"component_id\": ";
        json_string(out, projection.component_id);
        out << ", \"port_name\": ";
        json_string(out, projection.port_name);
        out << ", \"value_name\": ";
        json_string(out, projection.value_name);
        out << ", \"dimension\": ";
        json_string(out, projection.dimension);
        out << ", \"aggregation\": ";
        json_string(out, to_string(projection.aggregation));
        out << '}';
    }
    out << ']';
    out << ", \"acceptance_criteria\": [";
    for (std::size_t index = 0;
         index < record.request.acceptance_criteria.size();
         ++index) {
        if (index != 0U) out << ", ";
        const auto& criterion =
            record.request.acceptance_criteria[index];
        out << "{\"id\": ";
        json_string(out, criterion.id);
        out << ", \"projection_id\": ";
        json_string(out, criterion.projection_id);
        out << ", \"dimension\": ";
        json_string(out, criterion.dimension);
        out << ", \"lower_bound_si\": ";
        if (criterion.lower_bound_si) {
            json_number(out, *criterion.lower_bound_si);
        } else {
            out << "null";
        }
        out << ", \"upper_bound_si\": ";
        if (criterion.upper_bound_si) {
            json_number(out, *criterion.upper_bound_si);
        } else {
            out << "null";
        }
        out << ", \"lower_inclusive\": "
            << (criterion.lower_inclusive ? "true" : "false")
            << ", \"upper_inclusive\": "
            << (criterion.upper_inclusive ? "true" : "false")
            << '}';
    }
    out << ']';
    out << ", \"validation_prediction_count\": "
        << record.request.calibration_predictions.size();
    out << ", \"fingerprint\": ";
    json_string(out, record.request_fingerprint);
    out << "}";
    out << ",\n  \"worker_id\": ";
    if (record.worker_id.empty()) {
        out << "null";
    } else {
        json_string(out, record.worker_id);
    }
    out << ",\n  \"attempt\": " << record.attempt;
    out << ",\n  \"lease_expires_at_unix_ms\": ";
    if (record.lease_expires_at) {
        out << std::chrono::duration_cast<
                   std::chrono::milliseconds>(
                   record.lease_expires_at->time_since_epoch())
                   .count();
    } else {
        out << "null";
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
    out << ",\n  \"result_summary\": ";
    if (record.result_summary) {
        auto summary =
            serialize_result_summary_json(*record.result_summary);
        if (!summary.empty() && summary.back() == '\n') {
            summary.pop_back();
        }
        out << summary;
    } else {
        out << "null";
    }
    out << "\n}\n";
    return out.str();
}

std::string serialize_job_comparison_json(
    const SimulationJobComparison& comparison) {
    const auto optional_number = [](std::ostream& out,
                                    const std::optional<double>& value) {
        if (value) json_number(out, *value);
        else out << "null";
    };
    const auto optional_aggregation = [](
        std::ostream& out,
        const std::optional<ResultAggregation>& value) {
        if (value) json_string(out, to_string(*value));
        else out << "null";
    };
    const auto optional_bool = [](std::ostream& out,
                                  const std::optional<bool>& value) {
        if (value) out << (*value ? "true" : "false");
        else out << "null";
    };
    std::ostringstream out;
    out << "{\n  \"schema_version\": ";
    json_string(out, comparison.schema_version);
    out << ",\n  \"team_id\": ";
    json_string(out, comparison.team_id);
    out << ",\n  \"project_id\": ";
    json_string(out, comparison.project_id);
    out << ",\n  \"baseline_job_id\": ";
    json_string(out, comparison.baseline_job_id);
    out << ",\n  \"candidate_job_id\": ";
    json_string(out, comparison.candidate_job_id);
    out << ",\n  \"baseline_study_revision_id\": ";
    json_string(out, comparison.baseline_study_revision_id);
    out << ",\n  \"candidate_study_revision_id\": ";
    json_string(out, comparison.candidate_study_revision_id);
    out << ",\n  \"mode\": ";
    json_string(out, comparison.mode);
    out << ",\n  \"coverage\": {\"matched_count\": "
        << comparison.matched_count
        << ", \"incompatible_count\": "
        << comparison.incompatible_count
        << ", \"baseline_only_count\": "
        << comparison.baseline_only_count
        << ", \"candidate_only_count\": "
        << comparison.candidate_only_count << "}";
    out << ",\n  \"engineering_acceptance\": {"
        << "\"baseline_passed\": ";
    optional_bool(
        out, comparison.engineering_acceptance.baseline_passed);
    out << ", \"candidate_passed\": ";
    optional_bool(
        out, comparison.engineering_acceptance.candidate_passed);
    out << ", \"transition\": ";
    json_string(
        out, comparison.engineering_acceptance.transition);
    out << "},\n  \"values\": [";
    for (std::size_t index = 0;
         index < comparison.values.size(); ++index) {
        if (index != 0U) out << ", ";
        const auto& value = comparison.values[index];
        out << "{\"id\": ";
        json_string(out, value.id);
        out << ", \"status\": ";
        json_string(out, to_string(value.status));
        out << ", \"baseline_dimension\": ";
        json_string(out, value.baseline_dimension);
        out << ", \"candidate_dimension\": ";
        json_string(out, value.candidate_dimension);
        out << ", \"baseline_aggregation\": ";
        optional_aggregation(out, value.baseline_aggregation);
        out << ", \"candidate_aggregation\": ";
        optional_aggregation(out, value.candidate_aggregation);
        out << ", \"baseline_value_si\": ";
        optional_number(out, value.baseline_value_si);
        out << ", \"candidate_value_si\": ";
        optional_number(out, value.candidate_value_si);
        out << ", \"absolute_delta_si\": ";
        optional_number(out, value.absolute_delta_si);
        out << ", \"relative_delta\": ";
        optional_number(out, value.relative_delta);
        out << '}';
    }
    out << "]\n}\n";
    return out.str();
}

std::string serialize_job_page_json(
    const SimulationJobPage& page,
    const std::string& next_cursor) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": "
        << "\"thermox.job_list/v1\",\n  \"jobs\": [";
    for (std::size_t index = 0; index < page.jobs.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        auto job = serialize_job_record_json(page.jobs[index]);
        if (!job.empty() && job.back() == '\n') {
            job.pop_back();
        }
        out << job;
    }
    out << "],\n  \"next_cursor\": ";
    if (next_cursor.empty()) {
        out << "null";
    } else {
        json_string(out, next_cursor);
    }
    out << "\n}\n";
    return out.str();
}

}  // namespace thermox::service
