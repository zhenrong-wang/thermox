#include "thermox/service/simulation_service.hpp"

#include "serialization_internal.hpp"
#include "runtime_internal.hpp"
#include "artifact_payload.hpp"

#include "thermox/nonlinear_solver.hpp"
#include "thermox/continuation_solver.hpp"
#include "thermox/solver_policy_benchmark.hpp"
#include "thermox/platform/calibration.hpp"
#include "thermox/platform/component_registry.hpp"
#include "thermox/platform/expression_component.hpp"
#include "thermox/platform/correlation.hpp"
#include "thermox/platform/model_document.hpp"
#include "thermox/platform/results.hpp"
#include "thermox/platform/regime_map.hpp"
#include "thermox/physics/property_registry.hpp"
#include "thermox/transient_solver.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace thermox::service {

namespace {

ServiceError make_error(
    std::string code,
    std::string stage,
    std::string message) {
    return {
        error_schema_v1,
        std::move(code),
        std::move(stage),
        std::move(message),
    };
}

bool valid_schema(const std::string& schema) {
    return schema == command_schema_v1;
}

void populate_structural_blocks(
    CompilationSummary& summary,
    const ProblemStructureReport& structure) {
    summary.structural_blocks.reserve(
        structure.structural_blocks.size());
    for (const auto& block : structure.structural_blocks) {
        summary.largest_structural_block_size = std::max(
            summary.largest_structural_block_size,
            block.variable_names.size());
        summary.structural_blocks.push_back({
            block.variable_names,
            block.residual_names,
            block.suggested_tear_variable_names,
            block.acyclic_after_suggested_tears,
            block.structural_nonzero_count,
            block.suggested_inner_variable_count,
            block.suggested_inner_nonzero_count,
            block.suggested_tear_coupling_nonzero_count,
            block.suggested_dense_schur_entry_count,
        });
    }
}

std::string sha256_hex(std::string_view value) {
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>
        context{EVP_MD_CTX_new(), EVP_MD_CTX_free};
    if (!context ||
        EVP_DigestInit_ex(
            context.get(), EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(
            context.get(), value.data(), value.size()) != 1) {
        throw std::runtime_error(
            "could not initialize artifact SHA-256 checksum");
    }
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int digest_size = 0;
    if (EVP_DigestFinal_ex(
            context.get(), digest, &digest_size) != 1) {
        throw std::runtime_error(
            "could not finalize artifact SHA-256 checksum");
    }
    std::ostringstream encoded;
    for (unsigned int index = 0; index < digest_size; ++index) {
        encoded << std::hex << std::setfill('0')
                << std::setw(2)
                << static_cast<unsigned int>(digest[index]);
    }
    return encoded.str();
}

CorrelationArtifactInput correlation_input(
    const platform::CorrelationArtifact& artifact) {
    CorrelationArtifactInput input;
    input.id = artifact.id;
    input.schema_version = artifact.schema_version;
    input.revision = artifact.revision;
    input.checksum_sha256 = artifact.checksum_sha256;
    for (const auto& variable : artifact.inputs()) {
        input.inputs.push_back({variable.name, variable.dimension});
    }
    input.output = {
        artifact.output().name, artifact.output().dimension};
    for (const auto& candidate : artifact.candidates()) {
        CorrelationCandidateInput value;
        value.id = candidate.id;
        value.regime = candidate.regime;
        value.priority = candidate.priority;
        value.coefficients = candidate.coefficients;
        value.expression = candidate.expression;
        value.flow_regimes = candidate.flow_regimes;
        value.fallback_for_unmapped_flow_regime =
            candidate.fallback_for_unmapped_flow_regime;
        for (const auto& range : candidate.applicability) {
            value.applicability.push_back({
                range.input, range.minimum, range.maximum,
                range.minimum_inclusive,
                range.maximum_inclusive});
        }
        input.candidates.push_back(std::move(value));
    }
    return input;
}

RegimeMapArtifactInput regime_map_input(
    const platform::RegimeMapArtifact& artifact) {
    RegimeMapArtifactInput input;
    input.id = artifact.id;
    input.schema_version = artifact.schema_version;
    input.revision = artifact.revision;
    input.checksum_sha256 = artifact.checksum_sha256;
    for (const auto& variable : artifact.inputs()) {
        input.inputs.push_back({variable.name, variable.dimension});
    }
    for (const auto& region : artifact.regions()) {
        RegimeMapRegionInput value;
        value.id = region.id;
        value.regime = region.regime;
        value.priority = region.priority;
        for (const auto& branch : region.branches) {
            RegimeMapBranchInput mapped_branch;
            mapped_branch.id = branch.id;
            mapped_branch.priority = branch.priority;
            for (const auto& criterion : branch.criteria) {
                mapped_branch.criteria.push_back({
                    criterion.expression, criterion.dimension,
                    criterion.minimum, criterion.maximum,
                    criterion.minimum_inclusive,
                    criterion.maximum_inclusive,
                });
            }
            value.branches.push_back(std::move(mapped_branch));
        }
        input.regions.push_back(std::move(value));
    }
    return input;
}

std::vector<platform::ExpressionComponentDefinition>
expression_component_definitions(
    const SimulationComponentBundle& bundle) {
    constexpr std::size_t maximum_components = 128;
    if (bundle.expression_components.size() >
        maximum_components) {
        throw std::invalid_argument(
            "simulation component bundle exceeds the "
            "128-definition limit");
    }
    std::vector<platform::ExpressionComponentDefinition>
        definitions;
    definitions.reserve(bundle.expression_components.size());
    for (const auto& input : bundle.expression_components) {
        platform::ExpressionComponentDefinition definition;
        definition.schema_version = input.schema_version;
        definition.descriptor.kind = input.kind;
        definition.descriptor.version = input.version;
        definition.descriptor.template_kind =
            input.template_kind;
        definition.descriptor.display_name = input.display_name;
        definition.descriptor.category = input.category;
        definition.descriptor.model_name = input.model_name;
        definition.descriptor.system_boundary_role =
            input.system_boundary_role;
        definition.descriptor.supports_steady = input.supports_steady;
        definition.descriptor.supports_transient = input.supports_transient;
        for (const auto& port : input.ports) {
            definition.descriptor.ports.push_back({
                port.name,
                port.domain,
                port.direction,
                port.maximum_connections,
            });
        }
        for (const auto& parameter : input.parameters) {
            definition.descriptor.parameters.push_back({
                parameter.name,
                parameter.dimension,
                parameter.required,
                parameter.default_value_si,
                parameter.lower_bound,
                parameter.upper_bound,
                parameter.lower_inclusive,
                parameter.upper_inclusive,
            });
        }
        for (const auto& equation : input.equations) {
            definition.equations.push_back({
                equation.name,
                equation.expression,
                equation.residual_scale,
            });
        }
        const auto dae_kind = [](const std::string& kind) {
            if (kind == "differential") {
                return DaeVariableKind::differential;
            }
            if (kind == "algebraic") {
                return DaeVariableKind::algebraic;
            }
            throw std::invalid_argument(
                "expression component DAE variable kind must be algebraic or differential: " +
                kind);
        };
        for (const auto& variable : input.transient_variables) {
            definition.descriptor.transient_variables.push_back({
                variable.port_name, variable.variable_name,
                dae_kind(variable.kind), variable.derivative_scale});
        }
        for (const auto& variable : input.internal_variables) {
            definition.descriptor.internal_variables.push_back({
                variable.name, dae_kind(variable.kind),
                variable.initial_value_si, variable.state_scale,
                variable.initial_derivative_si_s,
                variable.derivative_scale, variable.lower_bound,
                variable.upper_bound, variable.dimension});
        }
        for (const auto& equation : input.transient_equations) {
            definition.transient_equations.push_back({
                equation.name, equation.expression,
                equation.residual_scale});
        }
        definitions.push_back(std::move(definition));
    }
    return definitions;
}

std::shared_ptr<const SimulationRuntime> request_runtime(
    const std::shared_ptr<const SimulationRuntime>& base,
    const SimulationComponentBundle& components) {
    return detail::NativeRuntimeFactory::overlay(
        base, expression_component_definitions(components));
}

platform::EngineeringArtifactRegistry execution_engineering_artifacts(
    const platform::EngineeringArtifactRegistry& runtime_artifacts,
    const SimulationArtifactBundle& inputs) {
    auto artifacts = runtime_artifacts;
    for (const auto& input : inputs.performance_maps) {
        artifacts.register_artifact(
            detail::performance_map_artifact(input));
    }
    for (const auto& input : inputs.correlations) {
        artifacts.register_artifact(
            detail::correlation_artifact(input));
    }
    for (const auto& input : inputs.regime_maps) {
        artifacts.register_artifact(
            detail::regime_map_artifact(input));
    }
    return artifacts;
}

std::vector<std::string> map_quality_suggestions(
    const std::string& code) {
    if (code.find("linear_") != std::string::npos ||
        code.find("cross_layer_") != std::string::npos) {
        return {
            "Use reject extrapolation unless the extrapolated operating "
            "envelope has been reviewed and qualified.",
        };
    }
    return {
        "Review map coverage against the intended operating envelope.",
    };
}

void append_map_quality_diagnostic(
    std::vector<Diagnostic>& diagnostics,
    const std::string& artifact_id,
    const std::string& json_path,
    const platform::MapQualityAdvisory& advisory) {
    diagnostics.push_back({
        "performance_map_" + advisory.code,
        DiagnosticSeverity::warning,
        "physical",
        json_path,
        {}, {}, {},
        "performance map '" + artifact_id + "': " + advisory.message,
        map_quality_suggestions(advisory.code),
    });
}

void append_performance_map_quality(
    const SimulationArtifactBundle& inputs,
    const platform::EngineeringArtifactRegistry& artifacts,
    const platform::ModelDocument& document,
    ValidateModelResponse& response) {
    const auto executable = platform::flatten_model_document(document);
    std::map<std::string, std::string> selected_paths;
    for (std::size_t component_index = 0;
         component_index < executable.components.size();
         ++component_index) {
        const auto& component = executable.components[component_index];
        for (const auto& [role, artifact_id] :
             component.artifact_bindings) {
            const auto artifact =
                artifacts.require_artifact(artifact_id);
            if (artifact->artifact_type() !=
                platform::performance_map_artifact_type) {
                continue;
            }
            selected_paths.try_emplace(
                artifact_id,
                document.assemblies.empty()
                    ? "/model/components/" +
                          std::to_string(component_index) +
                          "/artifacts/" + role
                    : "/model/assemblies");
        }
    }
    std::vector<std::pair<
        std::shared_ptr<const platform::PerformanceMapArtifact>,
        std::string>> selected;
    std::set<std::string> emitted;
    for (std::size_t artifact_index = 0;
         artifact_index < inputs.performance_maps.size();
         ++artifact_index) {
        const auto& input = inputs.performance_maps[artifact_index];
        if (!selected_paths.contains(input.id)) continue;
        const auto artifact = artifacts.require_as<
            platform::PerformanceMapArtifact>(
                input.id, platform::performance_map_artifact_type);
        selected.push_back({
            artifact,
            "/artifacts/performance_maps/" +
                std::to_string(artifact_index),
        });
        emitted.insert(input.id);
    }
    for (const auto& [artifact_id, binding_path] : selected_paths) {
        if (emitted.contains(artifact_id)) continue;
        selected.push_back({
            artifacts.require_as<platform::PerformanceMapArtifact>(
                artifact_id, platform::performance_map_artifact_type),
            binding_path,
        });
    }
    for (const auto& [artifact, base_path] : selected) {
        auto summary =
            detail::performance_map_quality_summary(*artifact);
        if (artifact->map) {
            const auto& report = artifact->map->quality_report();
            for (const auto& advisory : report.advisories) {
                append_map_quality_diagnostic(
                    response.diagnostics, artifact->id,
                    base_path + "/map", advisory);
            }
        } else {
            const auto& report =
                artifact->conditioned_map->quality_report();
            for (std::size_t layer = 0;
                 layer < report.layers.size(); ++layer) {
                for (const auto& advisory :
                     report.layers[layer].advisories) {
                    append_map_quality_diagnostic(
                        response.diagnostics, artifact->id,
                        base_path + "/layers/" +
                            std::to_string(layer) + "/map",
                        advisory);
                }
            }
            for (const auto& advisory : report.advisories) {
                append_map_quality_diagnostic(
                    response.diagnostics, artifact->id,
                    base_path, advisory);
            }
        }
        response.performance_map_quality.push_back(std::move(summary));
    }
}

std::vector<ArtifactProvenance> artifact_provenance(
    const SimulationArtifactBundle& inputs) {
    std::vector<ArtifactProvenance> provenance;
    provenance.reserve(
        inputs.performance_maps.size() +
        inputs.correlations.size() +
        inputs.regime_maps.size() +
        inputs.references.size());
    for (const auto& artifact : inputs.performance_maps) {
        provenance.push_back({
            artifact.id,
            platform::performance_map_artifact_type,
            artifact.schema_version,
            artifact.revision,
            artifact.checksum_sha256,
        });
    }
    for (const auto& artifact : inputs.correlations) {
        provenance.push_back({
            artifact.id,
            platform::correlation_artifact_type,
            artifact.schema_version,
            artifact.revision,
            artifact.checksum_sha256,
        });
    }
    for (const auto& artifact : inputs.regime_maps) {
        provenance.push_back({
            artifact.id,
            platform::regime_map_artifact_type,
            artifact.schema_version,
            artifact.revision,
            artifact.checksum_sha256,
        });
    }
    for (const auto& reference : inputs.references) {
        provenance.push_back({
            reference.id,
            reference.artifact_type,
            reference.schema_version,
            reference.revision,
            reference.checksum_sha256,
        });
    }
    return provenance;
}

SimulationArtifactBundle resolve_artifacts(
    const SimulationArtifactBundle& inputs,
    const EngineeringArtifactResolver* resolver) {
    SimulationArtifactBundle resolved;
    resolved.performance_maps = inputs.performance_maps;
    resolved.correlations = inputs.correlations;
    resolved.regime_maps = inputs.regime_maps;
    for (const auto& reference : inputs.references) {
        if (reference.id.empty() ||
            reference.artifact_type.empty() ||
            reference.schema_version.empty() ||
            reference.revision.empty() ||
            reference.checksum_sha256.empty()) {
            throw std::invalid_argument(
                "engineering artifact references require id, type, "
                "schema version, revision, and checksum");
        }
        if (reference.artifact_type ==
            platform::expression_component_artifact_type) {
            resolved.references.push_back(reference);
            continue;
        }
        if (reference.artifact_type ==
            platform::correlation_artifact_type) {
            if (resolver == nullptr) {
                throw std::invalid_argument(
                    "no engineering artifact resolver configured for id: " +
                    reference.id);
            }
            auto artifact =
                resolver->resolve_correlation(reference.id);
            if (!artifact) {
                throw std::invalid_argument(
                    "engineering artifact not found: " +
                    reference.id);
            }
            if (artifact->id != reference.id ||
                artifact->schema_version !=
                    reference.schema_version ||
                artifact->revision != reference.revision ||
                artifact->checksum_sha256 !=
                    reference.checksum_sha256) {
                throw std::invalid_argument(
                    "resolved engineering artifact identity mismatch for id: " +
                    reference.id);
            }
            resolved.correlations.push_back(
                std::move(*artifact));
            continue;
        }
        if (reference.artifact_type !=
            platform::performance_map_artifact_type) {
            throw std::invalid_argument(
                "unsupported engineering artifact type: " +
                reference.artifact_type);
        }
        if (resolver == nullptr) {
            throw std::invalid_argument(
                "no engineering artifact resolver configured for id: " +
                reference.id);
        }
        auto artifact =
            resolver->resolve_performance_map(reference.id);
        if (!artifact) {
            throw std::invalid_argument(
                "engineering artifact not found: " + reference.id);
        }
        if (artifact->id != reference.id ||
            artifact->schema_version != reference.schema_version ||
            artifact->revision != reference.revision ||
            artifact->checksum_sha256 !=
                reference.checksum_sha256) {
            throw std::invalid_argument(
                "resolved engineering artifact identity mismatch for id: " +
                reference.id);
        }
        resolved.performance_maps.push_back(
            std::move(*artifact));
    }
    return resolved;
}

std::string_view capability_name(
    physics::PropertyCapability capability) {
    switch (capability) {
        case physics::PropertyCapability::state_pt:
            return "state_pt";
        case physics::PropertyCapability::state_ph:
            return "state_ph";
        case physics::PropertyCapability::state_ph_derivatives:
            return "state_ph_derivatives";
        case physics::PropertyCapability::state_ps:
            return "state_ps";
        case physics::PropertyCapability::saturation_p:
            return "saturation_p";
        case physics::PropertyCapability::transport:
            return "transport";
        case physics::PropertyCapability::surface_tension:
            return "surface_tension";
    }
    return "unknown";
}

std::string_view capability_name(
    physics::ThermochemistryCapability capability) {
    switch (capability) {
        case physics::ThermochemistryCapability::state_pt:
            return "state_pt";
        case physics::ThermochemistryCapability::state_ph:
            return "state_ph";
        case physics::ThermochemistryCapability::state_ps:
            return "state_ps";
        case physics::ThermochemistryCapability::equilibrium_hp:
            return "equilibrium_hp";
        case physics::ThermochemistryCapability::transport:
            return "transport";
    }
    return "unknown";
}

Diagnostic compilation_diagnostic(const std::string& message) {
    Diagnostic diagnostic;
    diagnostic.stage = "compilation";
    diagnostic.message = message;
    diagnostic.code = "model_compilation_failed";
    diagnostic.suggestions = {
        "Review the referenced component, connection, medium, and active case."};
    if (message.find("calibration observation") !=
        std::string::npos) {
        diagnostic.code = "invalid_calibration_observation";
        diagnostic.suggestions = {
            "Select an exposed graph result and use a measured value "
            "with the same physical dimension."};
    } else if (message.find("unknown case id") !=
               std::string::npos) {
        diagnostic.code = "unknown_case";
        diagnostic.suggestions = {
            "Select an operating case declared by the exact model revision."};
    } else if (message.find("no component model registered") !=
        std::string::npos) {
        diagnostic.code = "unknown_component_type";
        diagnostic.suggestions = {
            "Select a component kind from the active runtime catalog."};
    } else if (message.find("missing required parameter") !=
               std::string::npos) {
        diagnostic.code = "missing_required_parameter";
        diagnostic.suggestions = {
            "Provide every required parameter listed by the component type."};
    } else if (message.find("supplies unknown parameter") !=
               std::string::npos) {
        diagnostic.code = "unknown_component_parameter";
        diagnostic.suggestions = {
            "Remove the parameter or select it from the component type catalog."};
    } else if (message.find(
                   "requests property package version") !=
               std::string::npos) {
        diagnostic.code = "property_package_version_mismatch";
        diagnostic.suggestions = {
            "Use the property package version resolved by the active runtime catalog."};
    } else if (message.find(
                   "requests connector contract version") !=
               std::string::npos) {
        diagnostic.code = "connector_contract_version_mismatch";
        diagnostic.suggestions = {
            "Use the connector contract version resolved by the active runtime catalog."};
    } else if (message.find("requests version") !=
               std::string::npos) {
        diagnostic.code = "component_version_mismatch";
        diagnostic.suggestions = {
            "Use the component version resolved by the active runtime catalog."};
    } else if (message.find("declares unknown port") !=
                   std::string::npos ||
               message.find("binds a medium to unknown port") !=
                   std::string::npos ||
               message.find("unknown port during graph compilation") !=
                   std::string::npos) {
        diagnostic.code = "unknown_component_port";
        diagnostic.suggestions = {
            "Derive component ports from the active component type catalog."};
    } else if (message.find("missing medium binding") !=
               std::string::npos) {
        diagnostic.code = "missing_medium_binding";
        diagnostic.suggestions = {
            "Bind every fluid port to a medium declared by the model."};
    } else if (message.find("kind '") != std::string::npos &&
               message.find("incompatible with domain") !=
                   std::string::npos) {
        diagnostic.code = "incompatible_connection_kind";
        diagnostic.suggestions = {
            "Use the link contract declared for the connector domain."};
    } else if (message.find("maximum connection count") !=
               std::string::npos) {
        diagnostic.code = "port_cardinality_exceeded";
        diagnostic.suggestions = {
            "Insert an explicit mixer, splitter, junction, or distribution component."};
    } else if (message.find("connection '") !=
                   std::string::npos ||
               message.find("connection endpoint") !=
                   std::string::npos) {
        diagnostic.code = "invalid_topology_connection";
        diagnostic.suggestions = {
            "Correct the connection endpoints, directions, domains, or resource compatibility."};
    } else if (message.find("case '") != std::string::npos ||
               message.find("fixed value references") !=
                   std::string::npos ||
               message.find("initial guess references") !=
                   std::string::npos) {
        diagnostic.code = "invalid_study_definition";
        diagnostic.suggestions = {
            "Correct the selected case specifications and result-variable references."};
    } else if (message.find("under-specified") !=
               std::string::npos) {
        diagnostic.code = "under_specified_model";
        diagnostic.suggestions = {
            "Add independent case specifications or component equations."};
    } else if (message.find("over-specified") !=
               std::string::npos) {
        diagnostic.code = "over_specified_model";
        diagnostic.suggestions = {
            "Remove conflicting or redundant user specifications."};
    } else if (message.find("structurally singular") !=
               std::string::npos) {
        diagnostic.code = "structurally_singular_model";
        diagnostic.suggestions = {
            "Inspect the localized underdetermined and overdetermined "
            "regions; remove a dependent constraint and add an "
            "independent specification in the affected neighborhood."};
    } else if (message.find("property capability") !=
               std::string::npos) {
        diagnostic.code = "unsupported_property_capability";
        diagnostic.suggestions = {
            "Choose a property backend that supplies the required capability."};
    } else if (message.find("property package registered") !=
               std::string::npos) {
        diagnostic.code = "unknown_property_backend";
        diagnostic.suggestions = {
            "Select a property backend from the active runtime catalog."};
    } else if (message.find("does not support") !=
               std::string::npos ||
               message.find("must use dynamic") != std::string::npos) {
        diagnostic.code = "unsupported_simulation_mode";
        diagnostic.suggestions = {
            "Choose a case mode supported by every component in the graph."};
    } else if (message.find("component '") !=
               std::string::npos) {
        diagnostic.code = "invalid_component_definition";
        diagnostic.suggestions = {
            "Correct the component parameters, bindings, or registered model selection."};
    }
    return diagnostic;
}

std::string quoted_value_after(
    const std::string& message,
    const std::string_view prefix) {
    const auto begin = message.find(prefix);
    if (begin == std::string::npos) return {};
    const auto value_begin = begin + prefix.size();
    const auto end = message.find('\'', value_begin);
    if (end == std::string::npos) return {};
    return message.substr(value_begin, end - value_begin);
}

std::string readiness_layer_for(const std::string& code) {
    static const std::set<std::string> physical{
        "invalid_components",
        "invalid_artifacts",
        "unknown_component_type",
        "missing_required_parameter",
        "unknown_component_parameter",
        "property_package_version_mismatch",
        "component_version_mismatch",
        "missing_medium_binding",
        "unsupported_property_capability",
        "unknown_property_backend",
    };
    static const std::set<std::string> topology{
        "connector_contract_version_mismatch",
        "unknown_component_port",
        "incompatible_connection_kind",
        "port_cardinality_exceeded",
        "invalid_topology_connection",
    };
    static const std::set<std::string> study{
        "invalid_calibration_observation",
        "unknown_case",
        "unsupported_simulation_mode",
        "invalid_study_definition",
    };
    if (code == "invalid_component_definition") return "physical";
    if (physical.contains(code)) return "physical";
    if (topology.contains(code)) return "topology";
    if (study.contains(code)) return "study";
    if (code == "invalid_model_document" ||
        code == "missing_model_document") {
        return "draft";
    }
    return "compilation";
}

ReadinessLayer& readiness_layer(
    ReadinessSummary& readiness,
    const std::string& id) {
    const auto found = std::find_if(
        readiness.layers.begin(), readiness.layers.end(),
        [&](const ReadinessLayer& layer) {
            return layer.id == id;
        });
    if (found == readiness.layers.end()) {
        throw std::logic_error("unknown readiness layer: " + id);
    }
    return *found;
}

void set_layer_state(
    ReadinessSummary& readiness,
    const std::string& id,
    const ReadinessState state,
    const std::string& diagnostic_code = {}) {
    auto& layer = readiness_layer(readiness, id);
    layer.state = state;
    if (!diagnostic_code.empty() &&
        std::find(
            layer.diagnostic_codes.begin(),
            layer.diagnostic_codes.end(),
            diagnostic_code) == layer.diagnostic_codes.end()) {
        layer.diagnostic_codes.push_back(diagnostic_code);
    }
}

void initialize_entity_readiness(
    ReadinessSummary& readiness,
    const platform::ModelDocument& document) {
    const auto executable =
        platform::flatten_model_document(document);
    readiness.entities.clear();
    readiness.entities.push_back({
        "system", document.model_id,
        ReadinessState::not_evaluated, {}});
    for (const auto& component : executable.components) {
        readiness.entities.push_back({
            "component", component.id,
            ReadinessState::not_evaluated, {}});
    }
    for (const auto& connection : executable.connections) {
        readiness.entities.push_back({
            "connection", connection.id,
            ReadinessState::not_evaluated, {}});
    }
}

void set_all_entities_ready(ReadinessSummary& readiness) {
    for (auto& entity : readiness.entities) {
        entity.state = ReadinessState::ready;
    }
}

void attribute_diagnostic(
    Diagnostic& diagnostic,
    ReadinessSummary& readiness,
    const platform::ModelDocument& document) {
    const auto executable =
        platform::flatten_model_document(document);
    diagnostic.stage = readiness_layer_for(diagnostic.code);
    diagnostic.component_id =
        quoted_value_after(diagnostic.message, "component '");
    diagnostic.connection_id =
        quoted_value_after(diagnostic.message, "connection '");
    const auto endpoint =
        quoted_value_after(diagnostic.message, "port '");
    if (!endpoint.empty()) {
        const auto separator = endpoint.find('.');
        if (separator != std::string::npos) {
            diagnostic.component_id = endpoint.substr(0, separator);
            diagnostic.port_name = endpoint.substr(separator + 1);
        }
    }
    if (diagnostic.code == "unknown_component_type" &&
        diagnostic.component_id.empty()) {
        for (const auto& component : executable.components) {
            if (diagnostic.message.find(component.kind) !=
                std::string::npos) {
                diagnostic.component_id = component.id;
                break;
            }
        }
    }
    const auto component = std::find_if(
        executable.components.begin(), executable.components.end(),
        [&](const platform::ComponentDefinition& candidate) {
            return candidate.id == diagnostic.component_id;
        });
    if (component != executable.components.end()) {
        diagnostic.json_path = document.assemblies.empty()
            ? "/model/components/" + std::to_string(std::distance(
                  executable.components.begin(), component))
            : "/model/assemblies";
    }
    const auto connection = std::find_if(
        executable.connections.begin(), executable.connections.end(),
        [&](const platform::ConnectionDefinition& candidate) {
            return candidate.id == diagnostic.connection_id;
        });
    if (connection != executable.connections.end()) {
        diagnostic.json_path = document.assemblies.empty()
            ? "/model/connections/" + std::to_string(std::distance(
                  executable.connections.begin(), connection))
            : "/model/assemblies";
    }
    for (auto& entity : readiness.entities) {
        const bool matches =
            (entity.entity_type == "component" &&
             entity.entity_id == diagnostic.component_id) ||
            (entity.entity_type == "connection" &&
             entity.entity_id == diagnostic.connection_id) ||
            (entity.entity_type == "system" &&
             diagnostic.component_id.empty() &&
             diagnostic.connection_id.empty());
        if (!matches) continue;
        entity.state = ReadinessState::blocked;
        entity.diagnostic_codes.push_back(diagnostic.code);
    }
}

const platform::CaseDefinition* selected_case(
    const platform::ModelDocument& document,
    const std::string& case_id) {
    if (case_id.empty()) {
        return document.cases.empty() ? nullptr : &document.cases.front();
    }
    for (const auto& simulation_case : document.cases) {
        if (simulation_case.id == case_id) {
            return &simulation_case;
        }
    }
    return nullptr;
}

bool transient_case(const platform::CaseDefinition* simulation_case) {
    return simulation_case != nullptr &&
        (simulation_case->mode == "dynamic_initialization" ||
         simulation_case->mode == "dynamic_transient");
}

std::string validation_mode(
    const platform::CaseDefinition* simulation_case) {
    if (simulation_case == nullptr ||
        simulation_case->mode == "steady_state_design" ||
        simulation_case->mode == "steady_state_off_design") {
        return "steady";
    }
    if (transient_case(simulation_case)) {
        return "transient";
    }
    throw std::invalid_argument(
        "unsupported simulation case mode: " +
        simulation_case->mode);
}

void validate_settings(const SteadySolverSettings& settings) {
    switch (settings.structural_decomposition_policy) {
    case StructuralDecompositionPolicy::automatic:
    case StructuralDecompositionPolicy::monolithic:
    case StructuralDecompositionPolicy::blocks:
    case StructuralDecompositionPolicy::tearing:
        break;
    default:
        throw std::invalid_argument(
            "invalid structural decomposition policy");
    }
    if (settings.max_iterations <= 0 ||
        settings.max_line_search_steps <= 0 ||
        !std::isfinite(settings.residual_tolerance) ||
        settings.residual_tolerance <= 0.0 ||
        !std::isfinite(settings.step_tolerance) ||
        settings.step_tolerance <= 0.0 ||
        !std::isfinite(settings.linear_residual_tolerance) ||
        settings.linear_residual_tolerance <= 0.0 ||
        !std::isfinite(settings.finite_difference_epsilon) ||
        settings.finite_difference_epsilon <= 0.0 ||
        !std::isfinite(settings.min_damping) ||
        settings.min_damping <= 0.0 ||
        !std::isfinite(settings.damping_reduction) ||
        settings.damping_reduction <= 0.0 ||
        settings.damping_reduction >= 1.0 ||
        !std::isfinite(settings.sufficient_decrease) ||
        settings.sufficient_decrease <= 0.0 ||
        !std::isfinite(
            settings.continuation_initial_step) ||
        settings.continuation_initial_step <= 0.0 ||
        settings.continuation_initial_step > 1.0 ||
        !std::isfinite(
            settings.continuation_minimum_step) ||
        settings.continuation_minimum_step <= 0.0 ||
        settings.continuation_minimum_step >
            settings.continuation_initial_step ||
        !std::isfinite(
            settings.continuation_step_growth) ||
        settings.continuation_step_growth <= 1.0 ||
        !std::isfinite(
            settings.continuation_step_reduction) ||
        settings.continuation_step_reduction <= 0.0 ||
        settings.continuation_step_reduction >= 1.0 ||
        settings.continuation_maximum_stages <= 0) {
        throw std::invalid_argument("invalid steady solver settings");
    }
}

ContinuationOptions to_core_continuation(
    const SteadySolverSettings& settings) {
    validate_settings(settings);
    ContinuationOptions options;
    options.initial_step =
        settings.continuation_initial_step;
    options.minimum_step =
        settings.continuation_minimum_step;
    options.step_growth =
        settings.continuation_step_growth;
    options.step_reduction =
        settings.continuation_step_reduction;
    options.maximum_stages =
        settings.continuation_maximum_stages;
    return options;
}

thermox::StructuralDecompositionPolicy to_core(
    StructuralDecompositionPolicy policy) {
    switch (policy) {
    case StructuralDecompositionPolicy::automatic:
        return thermox::StructuralDecompositionPolicy::automatic;
    case StructuralDecompositionPolicy::monolithic:
        return thermox::StructuralDecompositionPolicy::monolithic;
    case StructuralDecompositionPolicy::blocks:
        return thermox::StructuralDecompositionPolicy::blocks;
    case StructuralDecompositionPolicy::tearing:
        return thermox::StructuralDecompositionPolicy::tearing;
    }
    throw std::invalid_argument(
        "unknown structural decomposition policy");
}

StructuralDecompositionPolicy to_service(
    thermox::StructuralDecompositionPolicy policy) {
    switch (policy) {
    case thermox::StructuralDecompositionPolicy::automatic:
        return StructuralDecompositionPolicy::automatic;
    case thermox::StructuralDecompositionPolicy::monolithic:
        return StructuralDecompositionPolicy::monolithic;
    case thermox::StructuralDecompositionPolicy::blocks:
        return StructuralDecompositionPolicy::blocks;
    case thermox::StructuralDecompositionPolicy::tearing:
        return StructuralDecompositionPolicy::tearing;
    }
    throw std::invalid_argument(
        "unknown core structural decomposition policy");
}

SolverOptions to_core(const SteadySolverSettings& settings) {
    validate_settings(settings);
    SolverOptions options;
    options.max_iterations = settings.max_iterations;
    options.residual_tolerance = settings.residual_tolerance;
    options.step_tolerance = settings.step_tolerance;
    options.linear_residual_tolerance =
        settings.linear_residual_tolerance;
    options.structural_decomposition_policy =
        to_core(settings.structural_decomposition_policy);
    options.finite_difference_epsilon =
        settings.finite_difference_epsilon;
    options.min_damping = settings.min_damping;
    options.damping_reduction = settings.damping_reduction;
    options.sufficient_decrease = settings.sufficient_decrease;
    options.max_line_search_steps = settings.max_line_search_steps;
    return options;
}

TimeIntegrationOptions to_core(
    const TransientSolverSettings& settings) {
    if (!std::isfinite(settings.start_time) ||
        !std::isfinite(settings.end_time) ||
        settings.end_time <= settings.start_time ||
        !std::isfinite(settings.initial_step) ||
        settings.initial_step <= 0.0 ||
        !std::isfinite(settings.min_step) ||
        settings.min_step <= 0.0 ||
        !std::isfinite(settings.max_step) ||
        settings.max_step < settings.min_step ||
        !std::isfinite(settings.absolute_tolerance) ||
        settings.absolute_tolerance <= 0.0 ||
        !std::isfinite(settings.relative_tolerance) ||
        settings.relative_tolerance <= 0.0 ||
        settings.max_steps <= 0 ||
        settings.max_consecutive_rejections <= 0 ||
        settings.maximum_order < 1 ||
        settings.maximum_order > 2) {
        throw std::invalid_argument("invalid transient solver settings");
    }
    TimeIntegrationOptions options;
    options.start_time = settings.start_time;
    options.end_time = settings.end_time;
    options.initial_step = settings.initial_step;
    options.min_step = settings.min_step;
    options.max_step = settings.max_step;
    options.absolute_tolerance = settings.absolute_tolerance;
    options.relative_tolerance = settings.relative_tolerance;
    options.max_steps = settings.max_steps;
    options.max_consecutive_rejections =
        settings.max_consecutive_rejections;
    options.maximum_order = settings.maximum_order;
    options.compute_consistent_initial_conditions =
        settings.compute_consistent_initial_conditions;
    options.nonlinear_options = to_core(settings.nonlinear_solver);
    return options;
}

ModelMetadata model_metadata(
    const platform::ModelDocument& document,
    std::string case_id = {}) {
    return {
        document.schema_version,
        document.model_id,
        document.revision,
        std::move(case_id),
    };
}

ExecutionMetadata execution_metadata(
    const platform::ModelDocument& document,
    const std::string& command_schema,
    const std::string& case_id,
    std::string operation,
    SolverProvenance solver,
    const std::string& catalog_fingerprint,
    const platform::ComponentRegistry& components,
    const physics::PropertyPackageRegistry& properties) {
    const auto executable =
        platform::flatten_model_document(document);
    ExecutionMetadata metadata;
    metadata.command_schema_version = command_schema;
    metadata.platform_version = THERMOX_PLATFORM_VERSION;
    metadata.operation = std::move(operation);
    metadata.solver = std::move(solver);
    metadata.catalog_fingerprint = catalog_fingerprint;
    metadata.model = model_metadata(document, case_id);
    for (const auto& component : executable.components) {
        metadata.components.push_back({
            component.id,
            component.kind,
            component.version,
            components.require_model(component.kind).descriptor().version,
        });
    }
    for (const auto& medium : document.media) {
        const auto package = properties.create(
            medium.backend, medium.substance);
        metadata.media.push_back({
            medium.id,
            medium.backend,
            medium.substance,
            std::string(package->name()),
            medium.package_version,
            std::string(package->version()),
        });
    }
    for (const auto& connector :
         components.connector_domain_descriptors()) {
        metadata.connector_domains.push_back(
            {connector.domain, connector.contract_version});
    }
    return metadata;
}

SolverProvenance solver_provenance(
    const SteadySolverSettings& settings) {
    return {
        settings.continuation_enabled
            ? "thermox.newton-continuation/v12"
            : "thermox.newton/v11",
        {
            {"max_iterations",
             static_cast<double>(settings.max_iterations)},
            {"residual_tolerance", settings.residual_tolerance},
            {"step_tolerance", settings.step_tolerance},
            {"linear_residual_tolerance",
             settings.linear_residual_tolerance},
            {"structural_decomposition_policy",
             static_cast<double>(
                 settings.structural_decomposition_policy)},
            {"finite_difference_epsilon",
             settings.finite_difference_epsilon},
            {"min_damping", settings.min_damping},
            {"damping_reduction", settings.damping_reduction},
            {"sufficient_decrease", settings.sufficient_decrease},
            {"max_line_search_steps",
             static_cast<double>(
                 settings.max_line_search_steps)},
            {"continuation_enabled",
             settings.continuation_enabled ? 1.0 : 0.0},
            {"continuation_initial_step",
             settings.continuation_initial_step},
            {"continuation_minimum_step",
             settings.continuation_minimum_step},
            {"continuation_step_growth",
             settings.continuation_step_growth},
            {"continuation_step_reduction",
             settings.continuation_step_reduction},
            {"continuation_maximum_stages",
             static_cast<double>(
                 settings.continuation_maximum_stages)},
        },
    };
}

SolverProvenance solver_provenance(
    const CalibrationSolverSettings& settings) {
    SolverProvenance provenance{
        "thermox.coordinate-search/v1",
        {
            {"max_iterations",
             static_cast<double>(settings.max_iterations)},
            {"initial_step_fraction",
             settings.initial_step_fraction},
            {"minimum_step_fraction",
             settings.minimum_step_fraction},
            {"step_reduction", settings.step_reduction},
            {"minimum_continuation_fraction",
             settings.minimum_continuation_fraction},
            {"continuation_growth",
             settings.continuation_growth},
        },
    };
    const auto simulation =
        solver_provenance(settings.simulation_solver);
    for (const auto& setting : simulation.settings) {
        provenance.settings.push_back({
            "simulation." + setting.name, setting.value});
    }
    return provenance;
}

SolverProvenance solver_provenance(
    const TransientSolverSettings& settings) {
    auto provenance = SolverProvenance{
        "thermox.dae-bdf/v12",
        {
            {"start_time", settings.start_time},
            {"end_time", settings.end_time},
            {"initial_step", settings.initial_step},
            {"min_step", settings.min_step},
            {"max_step", settings.max_step},
            {"absolute_tolerance", settings.absolute_tolerance},
            {"relative_tolerance", settings.relative_tolerance},
            {"max_steps", static_cast<double>(settings.max_steps)},
            {"max_consecutive_rejections",
             static_cast<double>(
                 settings.max_consecutive_rejections)},
            {"maximum_order",
             static_cast<double>(settings.maximum_order)},
            {"compute_consistent_initial_conditions",
             settings.compute_consistent_initial_conditions
                 ? 1.0
                 : 0.0},
        },
    };
    const auto nonlinear =
        solver_provenance(settings.nonlinear_solver);
    for (const auto& setting : nonlinear.settings) {
        provenance.settings.push_back({
            "nonlinear." + setting.name,
            setting.value,
        });
    }
    return provenance;
}

NonlinearDiagnostics copy_diagnostics(
    const SolverDiagnostics& source) {
    return {
        source.converged,
        source.iterations,
        source.final_residual_norm,
        source.final_maximum_absolute_normalized_residual,
        source.limiting_residual,
        source.final_step_norm,
        source.function_evaluations,
        source.jacobian_evaluations,
        source.linear_solver_evaluations,
        source.symbolic_factorizations,
        source.numeric_factorizations,
        source.factorization_quality_observations,
        source.last_reciprocal_pivot_ratio,
        source.minimum_reciprocal_pivot_ratio,
        source.minimum_absolute_pivot_at_minimum_ratio,
        source.maximum_absolute_pivot_at_minimum_ratio,
        source.accepted_pivot_count_at_minimum_ratio,
        source.factorization_size_at_minimum_ratio,
        source.factorization_quality_method,
        source.last_linear_backward_error,
        source.maximum_linear_backward_error,
        source.linear_refinement_attempts,
        source.linear_refinement_successes,
        source.structural_block_solves,
        source.largest_linear_system_size,
        source.structural_tearing_attempts,
        source.structural_tearing_successes,
        source.structural_tearing_fallbacks,
        source.largest_tearing_inner_system_size,
        source.largest_tearing_outer_system_size,
        source.largest_tearing_inner_nonzero_count,
        source.last_structural_tearing_fallback,
        source.failed_structural_block,
        source.linear_solver_backend,
        source.message,
    };
}

ContinuationRunDiagnostics copy_diagnostics(
    const thermox::ContinuationDiagnostics& source) {
    ContinuationRunDiagnostics result;
    result.enabled = true;
    result.converged = source.converged;
    result.used_informed_path = source.used_informed_path;
    result.reached_parameter = source.reached_parameter;
    result.accepted_stages = source.accepted_stages;
    result.rejected_stages = source.rejected_stages;
    result.message = source.message;
    result.stages.reserve(source.stages.size());
    for (const auto& stage : source.stages) {
        result.stages.push_back({
            stage.start_parameter,
            stage.target_parameter,
            stage.accepted,
            stage.nonlinear.iterations,
            stage.nonlinear.final_residual_norm,
            stage.nonlinear
                .final_maximum_absolute_normalized_residual,
            stage.nonlinear.limiting_residual,
            stage.nonlinear.maximum_linear_backward_error,
            stage.nonlinear.linear_refinement_attempts,
            stage.nonlinear.linear_refinement_successes,
            stage.nonlinear.factorization_quality_observations,
            stage.nonlinear.last_reciprocal_pivot_ratio,
            stage.nonlinear.minimum_reciprocal_pivot_ratio,
            stage.nonlinear.minimum_absolute_pivot_at_minimum_ratio,
            stage.nonlinear.maximum_absolute_pivot_at_minimum_ratio,
            stage.nonlinear.accepted_pivot_count_at_minimum_ratio,
            stage.nonlinear.factorization_size_at_minimum_ratio,
            stage.nonlinear.factorization_quality_method,
            stage.nonlinear.structural_block_solves,
            stage.nonlinear.largest_linear_system_size,
            stage.nonlinear.structural_tearing_attempts,
            stage.nonlinear.structural_tearing_successes,
            stage.nonlinear.structural_tearing_fallbacks,
            stage.nonlinear.largest_tearing_inner_system_size,
            stage.nonlinear.largest_tearing_outer_system_size,
            stage.nonlinear.largest_tearing_inner_nonzero_count,
            stage.nonlinear.last_structural_tearing_fallback,
            stage.nonlinear.failed_structural_block,
            stage.nonlinear.message,
        });
    }
    return result;
}

TimeIntegrationDiagnostics copy_diagnostics(
    const thermox::TimeIntegrationDiagnostics& source) {
    return {
        source.success,
        source.accepted_steps,
        source.rejected_steps,
        source.maximum_order_used,
        source.nonlinear_solves,
        source.nonlinear_iterations,
        source.symbolic_factorizations,
        source.numeric_factorizations,
        source.factorization_quality_observations,
        source.last_reciprocal_pivot_ratio,
        source.minimum_reciprocal_pivot_ratio,
        source.minimum_absolute_pivot_at_minimum_ratio,
        source.maximum_absolute_pivot_at_minimum_ratio,
        source.accepted_pivot_count_at_minimum_ratio,
        source.factorization_size_at_minimum_ratio,
        source.factorization_quality_method,
        source.maximum_linear_backward_error,
        source.linear_refinement_attempts,
        source.linear_refinement_successes,
        source.structural_block_solves,
        source.largest_linear_system_size,
        source.structural_tearing_attempts,
        source.structural_tearing_successes,
        source.structural_tearing_fallbacks,
        source.largest_tearing_inner_system_size,
        source.largest_tearing_outer_system_size,
        source.largest_tearing_inner_nonzero_count,
        source.last_structural_tearing_fallback,
        source.linear_solver_backend,
        source.final_time,
        source.last_step,
        source.last_error_norm,
        source.maximum_accepted_error_norm,
        source.maximum_error_ratio,
        source.limiting_error_variable,
        source.maximum_absolute_normalized_residual,
        source.limiting_nonlinear_residual,
        source.message,
    };
}

ResultValue copy_result_value(
    const platform::ResultValue& source) {
    return {
        source.name,
        source.dimension,
        source.value_si,
        source.has_derivative,
        source.derivative_si_s,
    };
}

std::vector<ResultValue> copy_result_values(
    const std::vector<platform::ResultValue>& source) {
    std::vector<ResultValue> values;
    values.reserve(source.size());
    for (const auto& value : source) {
        values.push_back(copy_result_value(value));
    }
    return values;
}

GraphResult copy_graph_result(
    const platform::GraphResult& source) {
    GraphResult graph;
    graph.system_balances =
        copy_result_values(source.system_balances);
    graph.kpis = copy_result_values(source.kpis);
    graph.components.reserve(source.components.size());
    for (const auto& source_component : source.components) {
        ComponentResult component;
        component.component_id =
            source_component.component_id;
        component.kind = source_component.kind;
        component.internal_values =
            copy_result_values(
                source_component.internal_values);
        component.metrics =
            copy_result_values(source_component.metrics);
        component.ports.reserve(source_component.ports.size());
        for (const auto& source_port :
             source_component.ports) {
            component.ports.push_back({
                source_port.port_name,
                source_port.domain,
                source_port.medium_id,
                source_port.phase,
                copy_result_values(
                    source_port.primary_values),
                copy_result_values(
                    source_port.derived_values),
            });
        }
        graph.components.push_back(std::move(component));
    }
    return graph;
}

const platform::CalibrationDefinition& require_calibration(
    const platform::ModelDocument& document,
    const std::string& id) {
    const auto calibration = std::find_if(
        document.calibrations.begin(),
        document.calibrations.end(),
        [&](const auto& candidate) {
            return candidate.id == id;
        });
    if (calibration == document.calibrations.end()) {
        throw std::invalid_argument(
            "unknown calibration id: " + id);
    }
    return *calibration;
}

const platform::ResultValue& require_graph_value(
    const platform::GraphResult& graph,
    const std::string& target) {
    const auto first = target.find('.');
    const auto second = target.find('.', first + 1);
    const auto component = std::find_if(
        graph.components.begin(), graph.components.end(),
        [&](const auto& candidate) {
            return candidate.component_id ==
                target.substr(0, first);
        });
    if (component == graph.components.end()) {
        throw std::runtime_error(
            "calibration result component missing: " + target);
    }
    const auto find_value = [&](const auto& values,
                                const std::string& name)
        -> const platform::ResultValue* {
        const auto value = std::find_if(
            values.begin(), values.end(),
            [&](const auto& candidate) {
                return candidate.name == name;
            });
        return value == values.end() ? nullptr : &*value;
    };
    if (second == std::string::npos) {
        if (const auto* value = find_value(
                component->internal_values,
                target.substr(first + 1))) {
            return *value;
        }
    } else {
        const auto port = std::find_if(
            component->ports.begin(), component->ports.end(),
            [&](const auto& candidate) {
                return candidate.port_name ==
                    target.substr(
                        first + 1, second - first - 1);
            });
        if (port != component->ports.end()) {
            const auto name = target.substr(second + 1);
            if (const auto* value =
                    find_value(port->primary_values, name)) {
                return *value;
            }
            if (const auto* value =
                    find_value(port->derived_values, name)) {
                return *value;
            }
        }
    }
    throw std::runtime_error(
        "calibration result value missing: " + target);
}

const ResultValue& require_graph_value(
    const GraphResult& graph,
    const std::string& target) {
    const auto first = target.find('.');
    const auto second = target.find('.', first + 1);
    if (first == std::string::npos) {
        throw std::invalid_argument(
            "study observation target must identify a component");
    }
    const auto component = std::find_if(
        graph.components.begin(), graph.components.end(),
        [&](const auto& candidate) {
            return candidate.component_id ==
                target.substr(0, first);
        });
    if (component == graph.components.end()) {
        throw std::invalid_argument(
            "study observation component is missing: " + target);
    }
    const auto find_value = [&](const auto& values,
                                const std::string& name)
        -> const ResultValue* {
        const auto value = std::find_if(
            values.begin(), values.end(),
            [&](const auto& candidate) {
                return candidate.name == name;
            });
        return value == values.end() ? nullptr : &*value;
    };
    if (second == std::string::npos) {
        if (const auto* value = find_value(
                component->internal_values,
                target.substr(first + 1))) {
            return *value;
        }
        if (const auto* value = find_value(
                component->metrics,
                target.substr(first + 1))) {
            return *value;
        }
    } else {
        const auto port = std::find_if(
            component->ports.begin(), component->ports.end(),
            [&](const auto& candidate) {
                return candidate.port_name ==
                    target.substr(
                        first + 1, second - first - 1);
            });
        if (port != component->ports.end()) {
            const auto name = target.substr(second + 1);
            if (const auto* value =
                    find_value(port->primary_values, name)) {
                return *value;
            }
            if (const auto* value =
                    find_value(port->derived_values, name)) {
                return *value;
            }
        }
    }
    throw std::invalid_argument(
        "study observation value is missing: " + target);
}

using CalibrationState =
    std::map<std::string, double, std::less<>>;

struct CalibrationCaseSolution {
    platform::GraphResult graph;
    CalibrationState state;
};

CalibrationCaseSolution solve_calibration_case(
    const platform::ModelDocument& document,
    const std::string& case_id,
    const SteadySolverSettings& settings,
    const platform::ComponentRegistry& components,
    const physics::PropertyPackageRegistry& properties,
    const platform::EngineeringArtifactRegistry& engineering_artifacts,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry,
    const CalibrationState* warm_start) {
    const auto* simulation_case =
        selected_case(document, case_id);
    if (validation_mode(simulation_case) != "steady") {
        throw std::invalid_argument(
            "calibration currently requires steady cases: " +
            case_id);
    }
    auto graph = platform::compile_model_graph(
        document, components, properties, engineering_artifacts,
        thermochemistry, case_id);
    if (warm_start != nullptr) {
        for (std::size_t index = 0;
             index < graph.problem.variable_names.size(); ++index) {
            const auto value = warm_start->find(
                graph.problem.variable_names[index]);
            if (value != warm_start->end()) {
                graph.problem.initial_guess[index] = value->second;
            }
        }
    }
    const auto solve = solve_newton(
        graph.problem, to_core(settings));
    if (!solve.diagnostics.converged) {
        throw std::runtime_error(
            "calibration case '" + case_id +
            "' failed to solve: " +
            solve.diagnostics.message);
    }
    const platform::GraphResultEvaluator evaluator(
        document, graph, properties, thermochemistry);
    CalibrationCaseSolution solution;
    solution.graph = evaluator.evaluate(solve.x);
    for (std::size_t index = 0;
         index < graph.problem.variable_names.size(); ++index) {
        solution.state.emplace(
            graph.problem.variable_names[index], solve.x[index]);
    }
    return solution;
}

struct ObjectiveEvaluation {
    double value{std::numeric_limits<double>::infinity()};
    std::vector<CalibrationObservationResidual> observations;
    std::map<std::string, CalibrationState> case_states;
};

ObjectiveEvaluation evaluate_calibration_objective(
    const platform::ModelDocument& document,
    const platform::CalibrationDefinition& calibration,
    const SteadySolverSettings& settings,
    const platform::ComponentRegistry& components,
    const physics::PropertyPackageRegistry& properties,
    const platform::EngineeringArtifactRegistry& engineering_artifacts,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry,
    const std::map<std::string, CalibrationState>*
        warm_starts = nullptr) {
    ObjectiveEvaluation evaluation;
    evaluation.value = 0.0;
    std::map<std::string, CalibrationCaseSolution> case_results;
    for (const auto& observation : calibration.observations) {
        if (!case_results.contains(observation.case_id)) {
            const CalibrationState* warm_start = nullptr;
            if (warm_starts != nullptr) {
                const auto found =
                    warm_starts->find(observation.case_id);
                if (found != warm_starts->end()) {
                    warm_start = &found->second;
                }
            }
            case_results.emplace(
                observation.case_id,
                solve_calibration_case(
                    document, observation.case_id, settings,
                    components, properties, engineering_artifacts,
                    thermochemistry, warm_start));
        }
        const auto& predicted = require_graph_value(
            case_results.at(observation.case_id).graph,
            observation.target);
        const double residual =
            predicted.value_si - observation.measured.value_si;
        const double normalized =
            residual / observation.sigma.value_si;
        evaluation.value += normalized * normalized;
        evaluation.observations.push_back({
            observation.id,
            observation.case_id,
            observation.target,
            observation.measured.dimension,
            observation.measured.value_si,
            predicted.value_si,
            observation.sigma.value_si,
            residual,
            normalized,
        });
    }
    for (auto& [case_id, solution] : case_results) {
        evaluation.case_states.emplace(
            case_id, std::move(solution.state));
    }
    for (const auto& parameter : calibration.parameters) {
        if (!parameter.prior_mean.has_value()) continue;
        const double value =
            platform::require_calibration_parameter_target(
                document, parameter.targets.front())
                .value_si;
        const double normalized =
            (value - parameter.prior_mean->value_si) /
            parameter.prior_sigma->value_si;
        evaluation.value += normalized * normalized;
    }
    return evaluation;
}

}  // namespace

struct SimulationService::Impl {
    Impl(
        std::shared_ptr<const SimulationRuntime> runtime_value,
        std::shared_ptr<const EngineeringArtifactResolver>
            resolver_value)
        : runtime(std::move(runtime_value)),
          artifact_resolver(std::move(resolver_value)) {}

    std::shared_ptr<const SimulationRuntime> runtime;
    std::shared_ptr<const EngineeringArtifactResolver>
        artifact_resolver;
};

std::string to_string(OperationStatus status) {
    switch (status) {
        case OperationStatus::succeeded: return "succeeded";
        case OperationStatus::invalid_request: return "invalid_request";
        case OperationStatus::invalid_model: return "invalid_model";
        case OperationStatus::compilation_failed:
            return "compilation_failed";
        case OperationStatus::solver_failed: return "solver_failed";
        case OperationStatus::result_failed: return "result_failed";
    }
    return "unknown";
}

std::string to_string(StructuralDecompositionPolicy policy) {
    switch (policy) {
    case StructuralDecompositionPolicy::automatic:
        return "automatic";
    case StructuralDecompositionPolicy::monolithic:
        return "monolithic";
    case StructuralDecompositionPolicy::blocks:
        return "blocks";
    case StructuralDecompositionPolicy::tearing:
        return "tearing";
    }
    return "unknown";
}

StructuralDecompositionPolicy
structural_decomposition_policy_from_string(
    std::string_view value) {
    if (value == "automatic") {
        return StructuralDecompositionPolicy::automatic;
    }
    if (value == "monolithic") {
        return StructuralDecompositionPolicy::monolithic;
    }
    if (value == "blocks") {
        return StructuralDecompositionPolicy::blocks;
    }
    if (value == "tearing") {
        return StructuralDecompositionPolicy::tearing;
    }
    throw std::invalid_argument(
        "unknown structural decomposition policy: " +
        std::string(value));
}

std::string to_string(DiagnosticSeverity severity) {
    switch (severity) {
        case DiagnosticSeverity::information:
            return "information";
        case DiagnosticSeverity::warning:
            return "warning";
        case DiagnosticSeverity::error:
            return "error";
    }
    return "unknown";
}

std::string to_string(ReadinessState state) {
    switch (state) {
        case ReadinessState::not_evaluated:
            return "not_evaluated";
        case ReadinessState::blocked:
            return "blocked";
        case ReadinessState::ready:
            return "ready";
    }
    return "unknown";
}

SimulationService::SimulationService()
    : SimulationService(make_default_simulation_runtime()) {}

SimulationService::SimulationService(
    std::shared_ptr<const SimulationRuntime> runtime)
    : SimulationService(std::move(runtime), nullptr) {}

SimulationService::SimulationService(
    std::shared_ptr<const SimulationRuntime> runtime,
    std::shared_ptr<const EngineeringArtifactResolver>
        artifact_resolver)
    : impl_(std::make_unique<Impl>(
          std::move(runtime), std::move(artifact_resolver))) {
    if (!impl_->runtime) {
        throw std::invalid_argument(
            "simulation service runtime must not be null");
    }
}

SimulationService::~SimulationService() = default;
SimulationService::SimulationService(SimulationService&&) noexcept =
    default;
SimulationService& SimulationService::operator=(
    SimulationService&&) noexcept = default;

ValidateModelResponse SimulationService::validate_model(
    const ValidateModelRequest& request) const {
    ValidateModelResponse response;
    if (!valid_schema(request.schema_version)) {
        response.error = make_error(
            "unsupported_command_schema",
            "request",
            "unsupported command schema_version: " +
                request.schema_version);
        return response;
    }
    if (request.model_json.empty()) {
        response.error = make_error(
            "missing_model", "request", "model_json must not be empty");
        set_layer_state(
            response.readiness, "draft", ReadinessState::blocked,
            "missing_model_document");
        response.diagnostics.push_back({
            "missing_model_document",
            DiagnosticSeverity::error,
            "draft",
            {}, {}, {}, {},
            "model_json must not be empty",
            {"Submit an authorable model document."},
        });
        return response;
    }
    std::shared_ptr<const SimulationRuntime> runtime;
    try {
        runtime = request_runtime(
            impl_->runtime, request.components);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_request;
        response.error = make_error(
            "invalid_components", "components", ex.what());
        set_layer_state(
            response.readiness, "physical", ReadinessState::blocked,
            "invalid_components");
        response.diagnostics.push_back({
            "invalid_components",
            DiagnosticSeverity::error,
            "physical",
            {}, {}, {}, {},
            ex.what(),
            {"Correct the request-scoped component definitions."},
        });
        return response;
    }
    SimulationArtifactBundle artifacts;
    platform::EngineeringArtifactRegistry engineering_artifacts;
    try {
        artifacts = resolve_artifacts(
            request.artifacts,
            impl_->artifact_resolver.get());
        engineering_artifacts = execution_engineering_artifacts(
            runtime->impl_->engineering_artifacts,
            artifacts);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_request;
        response.error = make_error(
            "invalid_artifacts", "artifacts", ex.what());
        set_layer_state(
            response.readiness, "physical", ReadinessState::blocked,
            "invalid_artifacts");
        response.diagnostics.push_back({
            "invalid_artifacts",
            DiagnosticSeverity::error,
            "physical",
            {}, {}, {}, {},
            ex.what(),
            {"Provide valid immutable engineering artifact payloads or references."},
        });
        return response;
    }
    std::optional<platform::ModelDocument> document;
    try {
        document = platform::parse_model_document_text(
            request.model_json, runtime->impl_->units);
        append_performance_map_quality(
            artifacts, engineering_artifacts, *document, response);
        initialize_entity_readiness(response.readiness, *document);
        set_layer_state(
            response.readiness, "draft", ReadinessState::ready);
        response.model = model_metadata(*document);
        response.canonical_model_json =
            detail::serialize_model_document_json(*document);
        platform::validate_calibration_observation_contracts(
            *document, runtime->impl_->components,
            runtime->impl_->thermochemistry);
        const auto* simulation_case =
            selected_case(*document, request.case_id);
        if (!request.case_id.empty() && simulation_case == nullptr) {
            throw std::invalid_argument(
                "unknown case id during graph compilation: " +
                request.case_id);
        }
        set_layer_state(
            response.readiness, "study", ReadinessState::ready);
        response.model.case_id =
            simulation_case == nullptr ? "" : simulation_case->id;
        response.compilation.catalog_fingerprint =
            runtime->impl_->fingerprint;
        const std::string mode = validation_mode(simulation_case);
        if (mode == "transient") {
            const auto graph =
                platform::compile_transient_model_graph(
                    *document,
                    runtime->impl_->components,
                    runtime->impl_->properties,
                    engineering_artifacts,
                    runtime->impl_->thermochemistry,
                    request.case_id);
            response.compilation.compiled = true;
            response.compilation.mode = "transient";
            response.compilation.variable_count =
                graph.problem.variable_names.size();
            response.compilation.equation_count =
                graph.problem.residual_names.size();
            populate_structural_blocks(
                response.compilation, graph.structure);
        } else {
            const auto graph = platform::compile_model_graph(
                *document,
                runtime->impl_->components,
                runtime->impl_->properties,
                engineering_artifacts,
                runtime->impl_->thermochemistry,
                request.case_id);
            response.compilation.compiled = true;
            response.compilation.mode = "steady";
            response.compilation.variable_count =
                graph.problem.variable_names.size();
            response.compilation.equation_count =
                graph.problem.residual_names.size();
            populate_structural_blocks(
                response.compilation, graph.structure);
            response.compilation.reduced_connection_equations =
                graph.reduced_connection_equations;
        }
        set_layer_state(
            response.readiness, "physical", ReadinessState::ready);
        set_layer_state(
            response.readiness, "topology", ReadinessState::ready);
        set_layer_state(
            response.readiness, "compilation", ReadinessState::ready);
        set_layer_state(
            response.readiness, "execution", ReadinessState::ready);
        set_all_entities_ready(response.readiness);
        response.readiness.calculatable = true;
        response.status = OperationStatus::succeeded;
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_model;
        const std::string message = ex.what();
        const bool compilation =
            !response.canonical_model_json.empty();
        if (compilation) {
            auto diagnostic = compilation_diagnostic(message);
            if (document.has_value()) {
                attribute_diagnostic(
                    diagnostic, response.readiness, *document);
            } else {
                diagnostic.stage =
                    readiness_layer_for(diagnostic.code);
            }
            set_layer_state(
                response.readiness,
                diagnostic.stage,
                ReadinessState::blocked,
                diagnostic.code);
            response.error = make_error(
                diagnostic.code, diagnostic.stage, message);
            response.diagnostics.push_back(diagnostic);
        } else {
            response.error = make_error(
                "invalid_model", "parsing", message);
            response.diagnostics.push_back({
                "invalid_model_document",
                DiagnosticSeverity::error,
                "parsing",
                {},
                {},
                {},
                {},
                message,
                {"Correct the model document and submit it again."},
            });
            set_layer_state(
                response.readiness, "draft", ReadinessState::blocked,
                "invalid_model_document");
        }
    }
    return response;
}

CatalogResponse SimulationService::get_catalog(
    const CatalogRequest& request) const {
    return get_catalog(SimulationComponentBundle{}, request);
}

CatalogResponse SimulationService::get_catalog(
    const SimulationComponentBundle& components,
    const CatalogRequest& request) const {
    CatalogResponse response;
    if (!valid_schema(request.schema_version)) {
        response.error = make_error(
            "unsupported_command_schema",
            "request",
            "unsupported command schema_version: " +
                request.schema_version);
        return response;
    }
    std::shared_ptr<const SimulationRuntime> runtime;
    try {
        runtime = request_runtime(impl_->runtime, components);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_request;
        response.error = make_error(
            "invalid_components", "components", ex.what());
        return response;
    }
    response.fingerprint = runtime->impl_->fingerprint;
    for (const auto& descriptor :
         runtime->impl_->correlation_templates.descriptors()) {
        CatalogCorrelationTemplateType value;
        value.id = descriptor.id;
        value.version = descriptor.version;
        value.display_name = descriptor.display_name;
        value.category = descriptor.category;
        value.reference = descriptor.reference;
        value.expression = descriptor.expression;
        value.regime = descriptor.regime;
        for (const auto& input : descriptor.inputs) {
            value.inputs.push_back({input.name, input.dimension});
        }
        value.output = {
            descriptor.output.name, descriptor.output.dimension};
        for (const auto& coefficient : descriptor.coefficients) {
            value.coefficients.push_back({
                coefficient.name, coefficient.dimension,
                coefficient.default_value.has_value(),
                coefficient.default_value.value_or(0.0),
                coefficient.lower_bound, coefficient.upper_bound,
                coefficient.lower_inclusive,
                coefficient.upper_inclusive});
        }
        for (const auto& range : descriptor.applicability) {
            value.applicability.push_back({
                range.input, range.minimum.has_value(),
                range.minimum.value_or(0.0),
                range.maximum.has_value(),
                range.maximum.value_or(0.0),
                range.minimum_inclusive, range.maximum_inclusive});
        }
        response.correlation_templates.push_back(std::move(value));
    }
    for (const auto& descriptor :
         runtime->impl_->correlation_templates.family_descriptors()) {
        CatalogCorrelationFamilyTemplateType value;
        value.id = descriptor.id;
        value.version = descriptor.version;
        value.display_name = descriptor.display_name;
        value.category = descriptor.category;
        value.reference = descriptor.reference;
        value.scope = descriptor.scope;
        for (const auto& binding : descriptor.bindings) {
            value.bindings.push_back({
                binding.template_id, binding.coefficients,
                binding.candidate_id, binding.priority,
                binding.flow_regimes,
                binding.fallback_for_unmapped_flow_regime});
        }
        response.correlation_family_templates.push_back(
            std::move(value));
    }
    for (const auto& descriptor :
         runtime->impl_->regime_map_templates.descriptors()) {
        CatalogRegimeMapTemplateType value;
        value.id = descriptor.id;
        value.version = descriptor.version;
        value.display_name = descriptor.display_name;
        value.category = descriptor.category;
        value.reference = descriptor.reference;
        value.scope = descriptor.scope;
        for (const auto& input : descriptor.inputs) {
            value.inputs.push_back({input.name, input.dimension});
        }
        for (const auto& region : descriptor.regions) {
            CatalogRegimeMapRegionType mapped_region;
            mapped_region.id = region.id;
            mapped_region.regime = region.regime;
            mapped_region.priority = region.priority;
            for (const auto& branch : region.branches) {
                CatalogRegimeMapBranchType mapped_branch;
                mapped_branch.id = branch.id;
                mapped_branch.priority = branch.priority;
                for (const auto& criterion : branch.criteria) {
                    mapped_branch.criteria.push_back({
                        criterion.expression, criterion.dimension,
                        criterion.minimum.has_value(),
                        criterion.minimum.value_or(0.0),
                        criterion.maximum.has_value(),
                        criterion.maximum.value_or(0.0),
                        criterion.minimum_inclusive,
                        criterion.maximum_inclusive,
                    });
                }
                mapped_region.branches.push_back(
                    std::move(mapped_branch));
            }
            value.regions.push_back(std::move(mapped_region));
        }
        response.regime_map_templates.push_back(std::move(value));
    }
    for (const auto& extension :
         runtime->impl_->components
             .runtime_extension_descriptors()) {
        response.native_extensions.push_back(
            {
                extension.package_id,
                extension.package_version,
            });
    }
    for (const auto& descriptor :
         runtime->impl_->units.descriptors()) {
        CatalogDimensionUnitType dimension;
        dimension.dimension = descriptor.dimension;
        dimension.canonical_unit = descriptor.canonical_unit;
        dimension.si_display = {
            descriptor.si_display.symbol,
            descriptor.si_display.scale_from_si,
            descriptor.si_display.offset_from_si,
        };
        dimension.engineering_display = {
            descriptor.engineering_display.symbol,
            descriptor.engineering_display.scale_from_si,
            descriptor.engineering_display.offset_from_si,
        };
        for (const auto& unit : descriptor.accepted_units) {
            dimension.accepted_units.push_back({
                unit.symbol,
                unit.aliases,
                unit.scale_to_si,
                unit.offset_to_si,
            });
        }
        response.unit_dimensions.push_back(
            std::move(dimension));
    }
    for (const auto& descriptor :
         runtime->impl_->components.descriptors()) {
        ComponentType component;
        component.kind = descriptor.kind;
        component.version = descriptor.version;
        component.template_kind = descriptor.template_kind.empty()
            ? descriptor.kind
            : descriptor.template_kind;
        component.display_name = descriptor.display_name.empty()
            ? descriptor.kind
            : descriptor.display_name;
        component.category = descriptor.category.empty()
            ? "Other"
            : descriptor.category;
        component.model_name = descriptor.model_name.empty()
            ? descriptor.kind
            : descriptor.model_name;
        component.system_boundary_role =
            descriptor.system_boundary_role;
        component.supports_steady = descriptor.supports_steady;
        component.supports_transient =
            descriptor.supports_transient;
        for (const auto& port : descriptor.ports) {
            component.ports.push_back(
                {port.name, port.domain, port.direction,
                 port.maximum_connections});
        }
        for (const auto& parameter : descriptor.parameters) {
            component.parameters.push_back({
                parameter.name,
                parameter.dimension,
                parameter.required,
                parameter.default_value.has_value(),
                parameter.default_value.value_or(0.0),
                parameter.lower_bound,
                parameter.upper_bound,
                parameter.lower_inclusive,
                parameter.upper_inclusive,
            });
        }
        for (const auto& artifact : descriptor.artifacts) {
            component.artifacts.push_back({
                artifact.role,
                artifact.artifact_type,
                artifact.required,
            });
        }
        for (const auto& variable :
             descriptor.internal_variables) {
            component.internal_variables.push_back({
                variable.name,
                variable.dimension,
                variable.kind == DaeVariableKind::differential
                    ? "differential"
                    : "algebraic",
            });
        }
        for (const auto capability :
             descriptor.required_property_capabilities) {
            component.required_property_capabilities.push_back(
                std::string(capability_name(capability)));
        }
        for (const auto capability :
             descriptor.required_thermochemistry_capabilities) {
            component.required_thermochemistry_capabilities
                .push_back(
                    std::string(capability_name(capability)));
        }
        response.components.push_back(std::move(component));
    }
    for (const auto& descriptor :
         runtime->impl_->properties.descriptors()) {
        PropertyBackendType backend;
        backend.backend = descriptor.backend;
        backend.implementation_name =
            descriptor.implementation_name;
        backend.implementation_version =
            descriptor.implementation_version;
        backend.supported_substances =
            descriptor.supported_substances;
        for (const auto capability : descriptor.capabilities) {
            backend.capabilities.push_back(
                std::string(capability_name(capability)));
        }
        response.property_backends.push_back(std::move(backend));
    }
    for (const auto& descriptor :
         runtime->impl_->thermochemistry.descriptors()) {
        ThermochemistryBackendType backend;
        backend.backend = descriptor.backend;
        backend.implementation_name =
            descriptor.implementation_name;
        backend.implementation_version =
            descriptor.implementation_version;
        for (const auto capability : descriptor.capabilities) {
            backend.capabilities.push_back(
                std::string(capability_name(capability)));
        }
        response.thermochemistry_backends.push_back(
            std::move(backend));
    }
    for (const auto& connector :
         runtime->impl_->components
             .connector_domain_descriptors()) {
        ConnectorDomainType domain;
        domain.domain = connector.domain;
        domain.contract_version =
            connector.contract_version;
        domain.connection_kind =
            connector.connection_kind;
        for (const auto& variable : connector.variables) {
            domain.variables.push_back(
                {
                    variable.name,
                    variable.dimension,
                    variable.initial_value,
                    variable.scale,
                    variable.expand_species,
                });
        }
        response.connector_domains.push_back(
            std::move(domain));
    }
    response.status = OperationStatus::succeeded;
    return response;
}

InstantiateCorrelationResponse
SimulationService::instantiate_correlation(
    const InstantiateCorrelationRequest& request) const {
    InstantiateCorrelationResponse response;
    response.catalog_fingerprint = impl_->runtime->impl_->fingerprint;
    if (!valid_schema(request.schema_version)) {
        response.error = make_error(
            "unsupported_command_schema", "request",
            "unsupported command schema_version: " +
                request.schema_version);
        return response;
    }
    if (request.artifact_id.empty() || request.revision.empty()) {
        response.error = make_error(
            "invalid_correlation_identity", "request",
            "correlation artifact_id and revision must not be empty");
        return response;
    }
    constexpr std::size_t maximum_bindings = 32;
    const bool uses_family_template =
        !request.family_template_id.empty();
    const bool has_bindings = !request.bindings.empty();
    if (uses_family_template == has_bindings ||
        request.bindings.size() > maximum_bindings) {
        response.error = make_error(
            "invalid_correlation_bindings", "request",
            "correlation instantiation requires exactly one of "
            "family_template_id or 1 to 32 template bindings");
        return response;
    }
    try {
        platform::CorrelationArtifact artifact = [&]() {
            const platform::CorrelationArtifactIdentity identity{
                request.artifact_id,
                request.revision,
                std::string(64, '0'),
            };
            if (uses_family_template) {
                return platform::instantiate_correlation_family_template(
                    impl_->runtime->impl_->correlation_templates,
                    impl_->runtime->impl_->correlation_templates
                        .require_family_template(
                            request.family_template_id),
                    identity);
            }
            std::vector<platform::CorrelationTemplateCandidateBinding>
                bindings;
            bindings.reserve(request.bindings.size());
            for (const auto& binding : request.bindings) {
                bindings.push_back({
                    binding.template_id,
                    binding.coefficients,
                    binding.candidate_id,
                    binding.priority,
                    binding.flow_regimes,
                    binding.fallback_for_unmapped_flow_regime,
                });
            }
            return platform::instantiate_correlation_family(
                impl_->runtime->impl_->correlation_templates,
                identity, std::move(bindings));
        }();
        response.artifact = correlation_input(artifact);
        response.canonical_payload_json =
            detail::correlation_payload_json(response.artifact);
        response.artifact.checksum_sha256 =
            sha256_hex(response.canonical_payload_json);
        response.status = OperationStatus::succeeded;
    } catch (const std::exception& ex) {
        response.error = make_error(
            "correlation_instantiation_failed",
            "correlation_template", ex.what());
    }
    return response;
}

InstantiateRegimeMapResponse SimulationService::instantiate_regime_map(
    const InstantiateRegimeMapRequest& request) const {
    InstantiateRegimeMapResponse response;
    response.catalog_fingerprint = impl_->runtime->impl_->fingerprint;
    if (!valid_schema(request.schema_version)) {
        response.error = make_error(
            "unsupported_command_schema", "request",
            "unsupported command schema_version: " +
                request.schema_version);
        return response;
    }
    if (request.artifact_id.empty() || request.revision.empty() ||
        request.template_id.empty()) {
        response.error = make_error(
            "invalid_regime_map_instantiation", "request",
            "regime-map artifact_id, revision, and template_id must "
            "not be empty");
        return response;
    }
    try {
        const auto& descriptor =
            impl_->runtime->impl_->regime_map_templates
                .require_template(request.template_id);
        auto artifact = platform::instantiate_regime_map_template(
            descriptor,
            {
                request.artifact_id,
                request.revision,
                std::string(64, '0'),
            });
        response.artifact = regime_map_input(artifact);
        response.canonical_payload_json =
            detail::regime_map_payload_json(response.artifact);
        response.artifact.checksum_sha256 =
            sha256_hex(response.canonical_payload_json);
        response.status = OperationStatus::succeeded;
    } catch (const std::exception& ex) {
        response.error = make_error(
            "regime_map_instantiation_failed",
            "regime_map_template", ex.what());
    }
    return response;
}

SteadySimulationResponse SimulationService::run_steady(
    const SteadySimulationRequest& request) const {
    SteadySimulationResponse response;
    if (!valid_schema(request.schema_version)) {
        response.error = make_error(
            "unsupported_command_schema",
            "request",
            "unsupported command schema_version: " +
                request.schema_version);
        return response;
    }
    if (request.model_json.empty()) {
        response.error = make_error(
            "missing_model", "request", "model_json must not be empty");
        return response;
    }
    std::shared_ptr<const SimulationRuntime> runtime;
    try {
        runtime = request_runtime(
            impl_->runtime, request.components);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_request;
        response.error = make_error(
            "invalid_components", "components", ex.what());
        return response;
    }

    platform::ModelDocument document;
    SolverOptions options;
    try {
        options = to_core(request.solver);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_request;
        response.error = make_error(
            "invalid_solver_settings", "request", ex.what());
        return response;
    }
    SimulationArtifactBundle artifacts;
    platform::EngineeringArtifactRegistry engineering_artifacts;
    try {
        artifacts = resolve_artifacts(
            request.artifacts,
            impl_->artifact_resolver.get());
        engineering_artifacts = execution_engineering_artifacts(
            runtime->impl_->engineering_artifacts,
            artifacts);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_request;
        response.error = make_error(
            "invalid_artifacts", "artifacts", ex.what());
        return response;
    }
    try {
        document =
            platform::parse_model_document_text(
                request.model_json, runtime->impl_->units);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_model;
        response.error = make_error(
            "invalid_model", "validation", ex.what());
        return response;
    }

    platform::CompiledModelGraph graph;
    try {
        graph = platform::compile_model_graph(
            document,
            runtime->impl_->components,
            runtime->impl_->properties,
            engineering_artifacts,
            runtime->impl_->thermochemistry,
            request.case_id);
        response.metadata = execution_metadata(
            document,
            request.schema_version,
            graph.case_id.value_or(""),
            "steady",
            solver_provenance(request.solver),
            runtime->impl_->fingerprint,
            runtime->impl_->components,
            runtime->impl_->properties);
        response.metadata.artifacts =
            artifact_provenance(artifacts);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::compilation_failed;
        response.error = make_error(
            "compilation_failed", "compilation", ex.what());
        return response;
    }

    NonlinearSolveResult result;
    try {
        if (request.solver.continuation_enabled) {
            auto continued = solve_continuation(
                graph.problem, options,
                to_core_continuation(request.solver));
            result.x = std::move(continued.x);
            result.diagnostics =
                std::move(continued.diagnostics);
            response.continuation = copy_diagnostics(
                continued.continuation);
        } else {
            result = solve_newton(graph.problem, options);
        }
    } catch (const std::exception& ex) {
        response.status = OperationStatus::solver_failed;
        response.error = make_error(
            "nonlinear_solver_exception", "solve", ex.what());
        return response;
    }
    response.diagnostics = copy_diagnostics(result.diagnostics);
    response.reduced_connection_equations =
        graph.reduced_connection_equations;

    if (!result.diagnostics.converged) {
        response.status = OperationStatus::solver_failed;
        const bool operating_envelope_violation =
            result.diagnostics.message.find(
                platform::operating_envelope_violation_code) !=
            std::string::npos;
        response.error = make_error(
            operating_envelope_violation
                ? "artifact_operating_envelope_violation"
                : "nonlinear_solver_failed",
            "solve",
            result.diagnostics.message);
        return response;
    }

    try {
        const platform::GraphResultEvaluator evaluator(
            document, graph,
            runtime->impl_->properties,
            runtime->impl_->thermochemistry);
        response.graph =
            copy_graph_result(evaluator.evaluate(result.x));
    } catch (const std::exception& ex) {
        response.status = OperationStatus::result_failed;
        response.error = make_error(
            "result_evaluation_failed", "result", ex.what());
        return response;
    }

    response.status = OperationStatus::succeeded;
    return response;
}

StructuralPolicyAuditResponse
SimulationService::run_structural_policy_audit(
    const StructuralPolicyAuditRequest& request) const {
    StructuralPolicyAuditResponse response;
    response.normalized_solution_tolerance =
        request.normalized_solution_tolerance;
    if (!valid_schema(request.schema_version)) {
        response.error = make_error(
            "unsupported_command_schema", "request",
            "unsupported command schema_version: " +
                request.schema_version);
        return response;
    }
    if (request.model_json.empty()) {
        response.error = make_error(
            "missing_model", "request",
            "model_json must not be empty");
        return response;
    }
    if (request.solver.continuation_enabled) {
        response.error = make_error(
            "invalid_solver_settings", "request",
            "structural policy audit compares direct Newton policies; "
            "continuation_enabled must be false");
        return response;
    }

    std::shared_ptr<const SimulationRuntime> runtime;
    try {
        runtime = request_runtime(
            impl_->runtime, request.components);
    } catch (const std::exception& ex) {
        response.error = make_error(
            "invalid_components", "components", ex.what());
        return response;
    }

    SolverOptions solver_options;
    try {
        solver_options = to_core(request.solver);
    } catch (const std::exception& ex) {
        response.error = make_error(
            "invalid_solver_settings", "request", ex.what());
        return response;
    }

    StructuralPolicyBenchmarkOptions benchmark_options;
    benchmark_options.normalized_solution_tolerance =
        request.normalized_solution_tolerance;
    benchmark_options.policies.clear();
    try {
        if (!std::isfinite(request.normalized_solution_tolerance) ||
            request.normalized_solution_tolerance <= 0.0) {
            throw std::invalid_argument(
                "normalized_solution_tolerance must be finite and "
                "positive");
        }
        for (const auto policy : request.policies) {
            benchmark_options.policies.push_back(to_core(policy));
        }
    } catch (const std::exception& ex) {
        response.error = make_error(
            "invalid_policy_audit", "request", ex.what());
        return response;
    }

    SimulationArtifactBundle artifacts;
    platform::EngineeringArtifactRegistry engineering_artifacts;
    try {
        artifacts = resolve_artifacts(
            request.artifacts,
            impl_->artifact_resolver.get());
        engineering_artifacts = execution_engineering_artifacts(
            runtime->impl_->engineering_artifacts,
            artifacts);
    } catch (const std::exception& ex) {
        response.error = make_error(
            "invalid_artifacts", "artifacts", ex.what());
        return response;
    }

    platform::ModelDocument document;
    try {
        document = platform::parse_model_document_text(
            request.model_json, runtime->impl_->units);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_model;
        response.error = make_error(
            "invalid_model", "validation", ex.what());
        return response;
    }

    platform::CompiledModelGraph graph;
    try {
        graph = platform::compile_model_graph(
            document,
            runtime->impl_->components,
            runtime->impl_->properties,
            engineering_artifacts,
            runtime->impl_->thermochemistry,
            request.case_id);
        auto provenance = solver_provenance(request.solver);
        provenance.contract_version =
            structural_policy_audit_schema_v1;
        provenance.settings.push_back({
            "normalized_solution_tolerance",
            request.normalized_solution_tolerance,
        });
        provenance.settings.push_back({
            "requested_policy_count",
            static_cast<double>(request.policies.size()),
        });
        response.metadata = execution_metadata(
            document,
            request.schema_version,
            graph.case_id.value_or(""),
            "structural_policy_audit",
            std::move(provenance),
            runtime->impl_->fingerprint,
            runtime->impl_->components,
            runtime->impl_->properties);
        response.metadata.artifacts =
            artifact_provenance(artifacts);
        response.compilation.compiled = true;
        response.compilation.mode = "steady";
        response.compilation.variable_count =
            graph.problem.variable_names.size();
        response.compilation.equation_count =
            graph.problem.residual_names.size();
        response.compilation.catalog_fingerprint =
            runtime->impl_->fingerprint;
        response.compilation.reduced_connection_equations =
            graph.reduced_connection_equations;
        populate_structural_blocks(
            response.compilation, graph.structure);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::compilation_failed;
        response.error = make_error(
            "compilation_failed", "compilation", ex.what());
        return response;
    }

    try {
        const auto benchmark = benchmark_structural_policies(
            graph.problem, solver_options, benchmark_options);
        response.monolithic_baseline_converged =
            benchmark.monolithic_baseline_converged;
        response.all_policies_executed =
            benchmark.all_policies_executed;
        response.all_policies_converged =
            benchmark.all_policies_converged;
        response.all_policies_equivalent_to_monolithic =
            benchmark.all_policies_equivalent_to_monolithic;
        response.message = benchmark.message;
        response.entries.reserve(benchmark.entries.size());
        for (const auto& entry : benchmark.entries) {
            response.entries.push_back({
                to_service(entry.policy),
                entry.executed,
                entry.executed &&
                    entry.solve.diagnostics.converged,
                entry.comparable_to_monolithic,
                entry.equivalent_to_monolithic,
                entry.maximum_normalized_solution_difference,
                copy_diagnostics(entry.solve.diagnostics),
                entry.message,
            });
        }
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_request;
        response.error = make_error(
            "invalid_policy_audit", "audit", ex.what());
        return response;
    }

    response.status = OperationStatus::succeeded;
    return response;
}

CalibrationResponse SimulationService::run_calibration(
    const CalibrationRequest& request) const {
    CalibrationResponse response;
    if (!valid_schema(request.schema_version) ||
        request.model_json.empty() ||
        request.calibration_id.empty()) {
        response.error = make_error(
            "invalid_calibration_request", "request",
            "schema_version, model_json, and calibration_id are required");
        return response;
    }
    const auto& settings = request.solver;
    if (settings.max_iterations <= 0 ||
        !std::isfinite(settings.initial_step_fraction) ||
        settings.initial_step_fraction <= 0.0 ||
        settings.initial_step_fraction > 1.0 ||
        !std::isfinite(settings.minimum_step_fraction) ||
        settings.minimum_step_fraction <= 0.0 ||
        settings.minimum_step_fraction >=
            settings.initial_step_fraction ||
        !std::isfinite(settings.step_reduction) ||
        settings.step_reduction <= 0.0 ||
        settings.step_reduction >= 1.0 ||
        !std::isfinite(
            settings.minimum_continuation_fraction) ||
        settings.minimum_continuation_fraction <= 0.0 ||
        settings.minimum_continuation_fraction > 1.0 ||
        !std::isfinite(settings.continuation_growth) ||
        settings.continuation_growth <= 1.0) {
        response.error = make_error(
            "invalid_calibration_settings", "request",
            "invalid bounded calibration solver settings");
        return response;
    }
    try {
        (void)to_core(settings.simulation_solver);
    } catch (const std::exception& ex) {
        response.error = make_error(
            "invalid_solver_settings", "request", ex.what());
        return response;
    }
    std::shared_ptr<const SimulationRuntime> runtime;
    try {
        runtime = request_runtime(
            impl_->runtime, request.components);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_request;
        response.error = make_error(
            "invalid_components", "components", ex.what());
        return response;
    }

    SimulationArtifactBundle artifacts;
    platform::EngineeringArtifactRegistry engineering_artifacts;
    try {
        artifacts = resolve_artifacts(
            request.artifacts,
            impl_->artifact_resolver.get());
        engineering_artifacts = execution_engineering_artifacts(
            runtime->impl_->engineering_artifacts,
            artifacts);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_request;
        response.error = make_error(
            "invalid_artifacts", "artifacts", ex.what());
        return response;
    }

    platform::ModelDocument document;
    try {
        document =
            platform::parse_model_document_text(
                request.model_json, runtime->impl_->units);
        platform::validate_calibration_observation_contracts(
            document, runtime->impl_->components,
            runtime->impl_->thermochemistry);
        response.calibration_id = request.calibration_id;
        response.metadata = execution_metadata(
            document, request.schema_version, "", "calibration",
            solver_provenance(settings),
            runtime->impl_->fingerprint,
            runtime->impl_->components,
            runtime->impl_->properties);
        response.metadata.artifacts =
            artifact_provenance(artifacts);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_model;
        response.error = make_error(
            "invalid_calibration_model", "validation", ex.what());
        return response;
    }

    const platform::CalibrationDefinition* calibration = nullptr;
    try {
        calibration =
            &require_calibration(document, request.calibration_id);
    } catch (const std::exception& ex) {
        response.error = make_error(
            "unknown_calibration", "request", ex.what());
        return response;
    }

    std::vector<double> values;
    std::vector<double> initial;
    std::vector<double> lower;
    std::vector<double> upper;
    std::vector<double> steps;
    try {
        for (const auto& parameter : calibration->parameters) {
            if (!parameter.lower_bound.has_value() ||
                !parameter.upper_bound.has_value() ||
                !std::isfinite(parameter.lower_bound->value_si) ||
                !std::isfinite(parameter.upper_bound->value_si)) {
                throw std::invalid_argument(
                    "bounded calibration parameter '" +
                    parameter.id +
                    "' requires finite lower and upper bounds");
            }
            const double value =
                platform::require_calibration_parameter_target(
                    document, parameter.targets.front())
                    .value_si;
            values.push_back(value);
            initial.push_back(value);
            lower.push_back(parameter.lower_bound->value_si);
            upper.push_back(parameter.upper_bound->value_si);
            steps.push_back(
                settings.initial_step_fraction *
                (upper.back() - lower.back()));
        }
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_model;
        response.error = make_error(
            "invalid_calibration_bounds", "validation", ex.what());
        return response;
    }

    const auto apply_values =
        [&](const std::vector<double>& candidate) {
        for (std::size_t index = 0;
             index < calibration->parameters.size(); ++index) {
            for (const auto& target :
                 calibration->parameters[index].targets) {
                platform::require_calibration_parameter_target(
                    document, target)
                    .value_si = candidate[index];
            }
        }
    };
    const auto evaluate_at =
        [&](const std::vector<double>& candidate,
            const std::map<std::string, CalibrationState>*
                warm_starts) {
        apply_values(candidate);
        return evaluate_calibration_objective(
            document, *calibration,
            settings.simulation_solver,
            runtime->impl_->components,
            runtime->impl_->properties,
            engineering_artifacts,
            runtime->impl_->thermochemistry,
            warm_starts);
    };
    const auto continue_to =
        [&](const std::vector<double>& from,
            const std::vector<double>& target,
            const std::map<std::string, CalibrationState>&
                warm_starts) {
        double progress = 0.0;
        double fraction = 1.0;
        auto current_warm_starts = warm_starts;
        ObjectiveEvaluation result;
        while (progress < 1.0) {
            const double next =
                std::min(1.0, progress + fraction);
            std::vector<double> candidate(from.size());
            for (std::size_t index = 0;
                 index < from.size(); ++index) {
                candidate[index] =
                    from[index] +
                    next * (target[index] - from[index]);
            }
            try {
                result = evaluate_at(
                    candidate, &current_warm_starts);
                ++response.diagnostics.objective_evaluations;
                progress = next;
                current_warm_starts = result.case_states;
                fraction = std::min(
                    1.0 - progress,
                    fraction * settings.continuation_growth);
            } catch (const std::exception&) {
                ++response.diagnostics.objective_evaluations;
                fraction *= 0.5;
                if (fraction <
                    settings.minimum_continuation_fraction) {
                    throw;
                }
            }
        }
        return result;
    };

    ObjectiveEvaluation best;
    try {
        best = evaluate_at(values, nullptr);
        response.diagnostics.initial_objective = best.value;
        response.diagnostics.objective_evaluations = 1;
    } catch (const std::exception& ex) {
        response.status = OperationStatus::solver_failed;
        response.error = make_error(
            "calibration_baseline_failed", "calibration", ex.what());
        return response;
    }

    for (int iteration = 0;
         iteration < settings.max_iterations; ++iteration) {
        bool improved = false;
        for (std::size_t index = 0; index < values.size();
             ++index) {
            const auto base_values = values;
            const auto base_evaluation = best;
            const double original = values[index];
            double selected = original;
            ObjectiveEvaluation selected_evaluation =
                base_evaluation;
            for (const double direction : {-1.0, 1.0}) {
                const double trial = std::clamp(
                    original + direction * steps[index],
                    lower[index], upper[index]);
                if (trial == original) continue;
                auto target_values = base_values;
                target_values[index] = trial;
                try {
                    auto evaluation = continue_to(
                        base_values, target_values,
                        base_evaluation.case_states);
                    if (evaluation.value <
                        selected_evaluation.value) {
                        selected = trial;
                        selected_evaluation =
                            std::move(evaluation);
                    }
                } catch (const std::exception&) {
                }
            }
            values[index] = selected;
            if (selected != original) {
                improved = true;
                best = std::move(selected_evaluation);
            }
            apply_values(values);
        }
        response.diagnostics.iterations = iteration + 1;
        if (!improved) {
            for (auto& step : steps) {
                step *= settings.step_reduction;
            }
        }
        bool small = true;
        for (std::size_t index = 0; index < steps.size();
             ++index) {
            small = small &&
                steps[index] <=
                    settings.minimum_step_fraction *
                        (upper[index] - lower[index]);
        }
        if (small) {
            response.diagnostics.converged = true;
            response.diagnostics.message =
                "bounded coordinate search step tolerance reached";
            break;
        }
    }
    if (!response.diagnostics.converged) {
        response.diagnostics.message =
            "bounded coordinate search reached iteration limit";
    }
    apply_values(values);
    response.diagnostics.final_objective = best.value;
    response.observations = std::move(best.observations);
    for (std::size_t index = 0;
         index < calibration->parameters.size(); ++index) {
        const auto& parameter = calibration->parameters[index];
        response.parameters.push_back({
            parameter.id,
            parameter.scope,
            parameter.lower_bound->dimension,
            initial[index],
            values[index],
            lower[index],
            upper[index],
            parameter.targets,
        });
    }
    response.fitted_model_json =
        detail::serialize_model_document_json(document);
    response.status = OperationStatus::succeeded;
    return response;
}

EngineeringStudyResponse
SimulationService::run_engineering_study(
    const EngineeringStudyRequest& request) const {
    EngineeringStudyResponse response;
    if (!valid_schema(request.schema_version) ||
        request.model_json.empty() ||
        request.calibration_id.empty() ||
        request.prediction_cases.empty()) {
        response.error = make_error(
            "invalid_engineering_study_request",
            "request",
            "schema_version, model_json, calibration_id, and "
            "prediction_cases are required");
        return response;
    }

    std::set<std::string> case_ids;
    std::set<std::string> observation_ids;
    try {
        const auto document =
            platform::parse_model_document_text(
                request.model_json, impl_->runtime->impl_->units);
        const auto& calibration = require_calibration(
            document, request.calibration_id);
        std::set<std::string> calibration_cases;
        for (const auto& observation :
             calibration.observations) {
            calibration_cases.insert(observation.case_id);
        }
        for (const auto& prediction :
             request.prediction_cases) {
            if (prediction.case_id.empty() ||
                !case_ids.insert(prediction.case_id).second) {
                throw std::invalid_argument(
                    "prediction case IDs must be nonempty and unique");
            }
            if (calibration_cases.contains(
                    prediction.case_id)) {
                throw std::invalid_argument(
                    "prediction case '" + prediction.case_id +
                    "' is also used by calibration observations");
            }
            if (prediction.observations.empty()) {
                throw std::invalid_argument(
                    "prediction case '" + prediction.case_id +
                    "' must declare observations");
            }
            for (const auto& observation :
                 prediction.observations) {
                if (observation.id.empty() ||
                    observation.target.empty() ||
                    observation.dimension.empty() ||
                    !observation_ids.insert(
                         observation.id).second ||
                    !std::isfinite(observation.measured_si) ||
                    !std::isfinite(observation.sigma_si) ||
                    observation.sigma_si <= 0.0) {
                    throw std::invalid_argument(
                        "study observations require unique IDs, "
                        "targets, dimensions, finite measurements, "
                        "and positive finite uncertainties");
                }
            }
        }
    } catch (const std::exception& ex) {
        response.error = make_error(
            "invalid_study_predictions", "request", ex.what());
        return response;
    }

    CalibrationRequest calibration;
    calibration.schema_version = request.schema_version;
    calibration.model_json = request.model_json;
    calibration.calibration_id = request.calibration_id;
    calibration.solver = request.calibration_solver;
    calibration.artifacts = request.artifacts;
    calibration.components = request.components;
    response.calibration = run_calibration(calibration);
    if (!response.calibration.succeeded()) {
        response.status = response.calibration.status;
        response.error = response.calibration.error;
        return response;
    }

    for (const auto& prediction : request.prediction_cases) {
        StudyCaseResult result;
        result.case_id = prediction.case_id;
        SteadySimulationRequest simulation;
        simulation.schema_version = request.schema_version;
        simulation.model_json =
            response.calibration.fitted_model_json;
        simulation.case_id = prediction.case_id;
        simulation.solver = request.prediction_solver;
        simulation.artifacts = request.artifacts;
        simulation.components = request.components;
        result.simulation = run_steady(simulation);
        if (!result.simulation.succeeded()) {
            response.predictions.push_back(std::move(result));
            response.status =
                response.predictions.back().simulation.status;
            response.error = make_error(
                "study_prediction_failed",
                "prediction",
                "prediction case '" + prediction.case_id +
                    "' failed: " +
                    response.predictions.back()
                        .simulation.error.message);
            return response;
        }
        try {
            for (const auto& observation :
                 prediction.observations) {
                const auto& predicted = require_graph_value(
                    result.simulation.graph,
                    observation.target);
                if (predicted.dimension !=
                    observation.dimension) {
                    throw std::invalid_argument(
                        "study observation '" + observation.id +
                        "' dimension '" + observation.dimension +
                        "' does not match result dimension '" +
                        predicted.dimension + "'");
                }
                const double residual =
                    predicted.value_si -
                    observation.measured_si;
                const double normalized =
                    residual / observation.sigma_si;
                result.weighted_sum_squares +=
                    normalized * normalized;
                response.diagnostics
                    .maximum_absolute_normalized_residual =
                    std::max(
                        response.diagnostics
                            .maximum_absolute_normalized_residual,
                        std::abs(normalized));
                result.observations.push_back({
                    observation.id,
                    prediction.case_id,
                    observation.target,
                    observation.dimension,
                    observation.measured_si,
                    predicted.value_si,
                    observation.sigma_si,
                    residual,
                    normalized,
                });
            }
        } catch (const std::exception& ex) {
            response.status = OperationStatus::invalid_request;
            response.error = make_error(
                "invalid_study_observation",
                "prediction",
                ex.what());
            return response;
        }
        response.diagnostics.weighted_sum_squares +=
            result.weighted_sum_squares;
        response.diagnostics.observation_count +=
            result.observations.size();
        response.predictions.push_back(std::move(result));
    }
    response.diagnostics.prediction_case_count =
        response.predictions.size();
    response.diagnostics.rms_normalized_residual =
        std::sqrt(
            response.diagnostics.weighted_sum_squares /
            static_cast<double>(
                response.diagnostics.observation_count));
    response.status = OperationStatus::succeeded;
    return response;
}

TransientSimulationResponse SimulationService::run_transient(
    const TransientSimulationRequest& request) const {
    TransientSimulationResponse response;
    if (!valid_schema(request.schema_version)) {
        response.error = make_error(
            "unsupported_command_schema",
            "request",
            "unsupported command schema_version: " +
                request.schema_version);
        return response;
    }
    if (request.model_json.empty()) {
        response.error = make_error(
            "missing_model", "request", "model_json must not be empty");
        return response;
    }
    std::shared_ptr<const SimulationRuntime> runtime;
    try {
        runtime = request_runtime(
            impl_->runtime, request.components);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_request;
        response.error = make_error(
            "invalid_components", "components", ex.what());
        return response;
    }

    platform::ModelDocument document;
    TimeIntegrationOptions options;
    try {
        options = to_core(request.solver);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_request;
        response.error = make_error(
            "invalid_solver_settings", "request", ex.what());
        return response;
    }
    SimulationArtifactBundle artifacts;
    platform::EngineeringArtifactRegistry engineering_artifacts;
    try {
        artifacts = resolve_artifacts(
            request.artifacts,
            impl_->artifact_resolver.get());
        engineering_artifacts = execution_engineering_artifacts(
            runtime->impl_->engineering_artifacts,
            artifacts);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_request;
        response.error = make_error(
            "invalid_artifacts", "artifacts", ex.what());
        return response;
    }
    try {
        document =
            platform::parse_model_document_text(
                request.model_json, runtime->impl_->units);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_model;
        response.error = make_error(
            "invalid_model", "validation", ex.what());
        return response;
    }

    platform::CompiledTransientModelGraph graph;
    try {
        graph = platform::compile_transient_model_graph(
            document,
            runtime->impl_->components,
            runtime->impl_->properties,
            engineering_artifacts,
            runtime->impl_->thermochemistry,
            request.case_id);
        response.metadata = execution_metadata(
            document,
            request.schema_version,
            graph.case_id.value_or(""),
            "transient",
            solver_provenance(request.solver),
            runtime->impl_->fingerprint,
            runtime->impl_->components,
            runtime->impl_->properties);
        response.metadata.artifacts =
            artifact_provenance(artifacts);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::compilation_failed;
        response.error = make_error(
            "compilation_failed", "compilation", ex.what());
        return response;
    }

    DaeSolveResult result;
    try {
        result = integrate_dae(graph.problem, options);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::solver_failed;
        response.error = make_error(
            "transient_solver_exception", "solve", ex.what());
        return response;
    }
    response.diagnostics = copy_diagnostics(result.diagnostics);
    if (!result.diagnostics.success) {
        response.status = OperationStatus::solver_failed;
        const bool operating_envelope_violation =
            result.diagnostics.message.find(
                platform::operating_envelope_violation_code) !=
            std::string::npos;
        response.error = make_error(
            operating_envelope_violation
                ? "artifact_operating_envelope_violation"
                : "transient_solver_failed",
            "solve",
            result.diagnostics.message);
        return response;
    }

    try {
        const platform::GraphResultEvaluator evaluator(
            document, graph,
            runtime->impl_->properties,
            runtime->impl_->thermochemistry);
        response.trajectory.reserve(result.trajectory.size());
        for (const auto& sample : result.trajectory) {
            response.trajectory.push_back({
                sample.time,
                copy_graph_result(
                    evaluator.evaluate(
                        sample.state, sample.derivative)),
            });
        }
        response.events.reserve(result.events.size());
        for (const auto& event : result.events) {
            response.events.push_back({
                event.name,
                event.time,
                copy_graph_result(
                    evaluator.evaluate(event.state)),
                event.terminal,
            });
        }
    } catch (const std::exception& ex) {
        response.status = OperationStatus::result_failed;
        response.error = make_error(
            "result_evaluation_failed", "result", ex.what());
        return response;
    }

    response.status = OperationStatus::succeeded;
    return response;
}

}  // namespace thermox::service
