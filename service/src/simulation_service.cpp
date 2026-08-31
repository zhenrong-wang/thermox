#include "thermox/service/simulation_service.hpp"
#include "thermox/service/balance_uncertainty.hpp"
#include "thermox/service/thermal_feasibility.hpp"

#include "serialization_internal.hpp"
#include "runtime_internal.hpp"
#include "artifact_payload.hpp"

#include "thermox/nonlinear_solver.hpp"
#include "thermox/bounded_least_squares_optimizer.hpp"
#include "thermox/dense_cholesky.hpp"
#include "thermox/dense_linear_solver.hpp"
#include "thermox/least_squares_solver.hpp"
#include "thermox/continuation_solver.hpp"
#include "thermox/dae_linearization.hpp"
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
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
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
        definition.descriptor.default_mode = input.default_mode;
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
        for (const auto& mode : input.modes) {
            thermox::platform::ExpressionComponentModeDefinition
                mode_definition;
            mode_definition.name = mode.name;
            for (const auto& equation : mode.equations) {
                mode_definition.equations.push_back({
                    equation.name, equation.expression,
                    equation.residual_scale});
            }
            for (const auto& equation :
                 mode.transient_equations) {
                mode_definition.transient_equations.push_back({
                    equation.name, equation.expression,
                    equation.residual_scale});
            }
            definition.modes.push_back(
                std::move(mode_definition));
        }
        for (const auto& event : input.events) {
            platform::ExpressionComponentEventDefinition event_definition;
            event_definition.name = event.name;
            event_definition.expression = event.expression;
            event_definition.dimension = event.dimension;
            event_definition.direction = event.direction;
            event_definition.terminal = event.terminal;
            event_definition.priority = event.priority;
            event_definition.hysteresis_si = event.hysteresis_si;
            for (const auto& action : event.actions) {
                event_definition.actions.push_back({
                    action.type, action.target,
                    action.expression, action.mode});
            }
            definition.events.push_back(
                std::move(event_definition));
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
                platform::expression_component_artifact_type ||
            reference.artifact_type ==
                balance_uncertainty_artifact_type) {
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
        case physics::ThermochemistryCapability::lower_heating_value:
            return "lower_heating_value";
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
    switch (settings.globalization_policy) {
    case GlobalizationPolicy::line_search:
    case GlobalizationPolicy::trust_region:
        break;
    default:
        throw std::invalid_argument(
            "invalid globalization policy");
    }
    if (settings.max_iterations <= 0 ||
        settings.max_line_search_steps <= 0 ||
        settings.max_trust_region_steps <= 0 ||
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
        !std::isfinite(settings.trust_region_initial_radius) ||
        settings.trust_region_initial_radius <= 0.0 ||
        !std::isfinite(settings.trust_region_minimum_radius) ||
        settings.trust_region_minimum_radius <= 0.0 ||
        settings.trust_region_minimum_radius >
            settings.trust_region_initial_radius ||
        !std::isfinite(settings.trust_region_maximum_radius) ||
        settings.trust_region_maximum_radius <
            settings.trust_region_initial_radius ||
        !std::isfinite(
            settings.trust_region_acceptance_threshold) ||
        settings.trust_region_acceptance_threshold < 0.0 ||
        settings.trust_region_acceptance_threshold >= 1.0 ||
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
    options.globalization_policy =
        settings.globalization_policy ==
                GlobalizationPolicy::trust_region
            ? thermox::GlobalizationPolicy::trust_region
            : thermox::GlobalizationPolicy::line_search;
    options.trust_region_initial_radius =
        settings.trust_region_initial_radius;
    options.trust_region_minimum_radius =
        settings.trust_region_minimum_radius;
    options.trust_region_maximum_radius =
        settings.trust_region_maximum_radius;
    options.trust_region_acceptance_threshold =
        settings.trust_region_acceptance_threshold;
    options.max_trust_region_steps =
        settings.max_trust_region_steps;
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
    double previous_output_time =
        -std::numeric_limits<double>::infinity();
    for (const double output_time : settings.required_output_times) {
        if (!std::isfinite(output_time) ||
            output_time < settings.start_time ||
            output_time > settings.end_time ||
            output_time <= previous_output_time) {
            throw std::invalid_argument(
                "transient required_output_times must be finite, "
                "strictly increasing, and within the integration interval");
        }
        previous_output_time = output_time;
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
    options.required_output_times = settings.required_output_times;
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
            ? "thermox.newton-continuation/v13"
            : "thermox.newton/v12",
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
            {"globalization_policy",
             static_cast<double>(settings.globalization_policy)},
            {"trust_region_initial_radius",
             settings.trust_region_initial_radius},
            {"trust_region_minimum_radius",
             settings.trust_region_minimum_radius},
            {"trust_region_maximum_radius",
             settings.trust_region_maximum_radius},
            {"trust_region_acceptance_threshold",
             settings.trust_region_acceptance_threshold},
            {"max_trust_region_steps",
             static_cast<double>(
                 settings.max_trust_region_steps)},
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
    const TransientSolverSettings& settings);

SolverProvenance solver_provenance(
    const CalibrationSolverSettings& settings) {
    SolverProvenance provenance{
        "thermox.trust-region-least-squares/v2",
        {
            {"max_iterations",
             static_cast<double>(settings.max_iterations)},
            {"finite_difference_fraction",
             settings.finite_difference_fraction},
            {"initial_trust_region_radius",
             settings.initial_trust_region_radius},
            {"minimum_trust_region_radius",
             settings.minimum_trust_region_radius},
            {"maximum_trust_region_radius",
             settings.maximum_trust_region_radius},
            {"acceptance_ratio", settings.acceptance_ratio},
            {"gradient_tolerance", settings.gradient_tolerance},
            {"step_tolerance", settings.step_tolerance},
            {"objective_relative_tolerance",
             settings.objective_relative_tolerance},
            {"minimum_continuation_fraction",
             settings.minimum_continuation_fraction},
            {"continuation_growth",
             settings.continuation_growth},
        },
    };
    const auto simulation =
        solver_provenance(settings.steady_simulation_solver);
    for (const auto& setting : simulation.settings) {
        provenance.settings.push_back({
            "steady_simulation." + setting.name, setting.value});
    }
    const auto transient =
        solver_provenance(settings.transient_simulation_solver);
    for (const auto& setting : transient.settings) {
        provenance.settings.push_back({
            "transient_simulation." + setting.name,
            setting.value});
    }
    return provenance;
}

SolverProvenance solver_provenance(
    const ReconciliationSolverSettings& settings,
    ReconciliationMode mode,
    const ProfileLikelihoodSettings& profile,
    const JointConfidenceRegionSettings& joint_region) {
    SolverProvenance provenance{
        mode == ReconciliationMode::hard_equalities
            ? "thermox.reconciliation-newton/v1"
            : "thermox.reconciliation-cpqr-gauss-newton/v2",
        {
            {"max_iterations",
             static_cast<double>(settings.max_iterations)},
            {"finite_difference_fraction",
             settings.finite_difference_fraction},
            {"constraint_tolerance",
             settings.constraint_tolerance},
            {"step_tolerance", settings.step_tolerance},
            {"objective_relative_tolerance",
             settings.objective_relative_tolerance},
            {"minimum_line_search_fraction",
             settings.minimum_line_search_fraction},
            {"profile_likelihood.enabled",
             profile.enabled ? 1.0 : 0.0},
            {"profile_likelihood.objective_increase",
             profile.objective_increase},
            {"profile_likelihood.maximum_bracket_steps",
             static_cast<double>(profile.maximum_bracket_steps)},
            {"profile_likelihood.maximum_bisection_steps",
             static_cast<double>(profile.maximum_bisection_steps)},
            {"profile_likelihood.maximum_nuisance_iterations",
             static_cast<double>(profile.maximum_nuisance_iterations)},
            {"joint_confidence_region.enabled",
             joint_region.enabled ? 1.0 : 0.0},
            {"joint_confidence_region.objective_increase",
             joint_region.objective_increase},
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
        "thermox.dae-bdf/v13",
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
    provenance.settings.push_back({
        "required_output_time_count",
        static_cast<double>(settings.required_output_times.size())});
    for (std::size_t index = 0;
         index < settings.required_output_times.size(); ++index) {
        provenance.settings.push_back({
            "required_output_time." + std::to_string(index),
            settings.required_output_times[index]});
    }
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
        source.trust_region_trials,
        source.trust_region_rejections,
        source.final_trust_region_radius,
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
    std::optional<platform::GraphResult> steady_graph;
    std::map<double, platform::GraphResult> transient_graphs;
    CalibrationState state;
};

const platform::GraphResult& calibration_graph_at(
    const CalibrationCaseSolution& solution,
    const std::optional<platform::ScalarValue>& time) {
    if (!time.has_value()) {
        if (!solution.steady_graph.has_value()) {
            throw std::logic_error(
                "steady calibration result is missing");
        }
        return *solution.steady_graph;
    }
    const auto found = solution.transient_graphs.find(
        time->value_si);
    if (found == solution.transient_graphs.end()) {
        throw std::logic_error(
            "transient calibration result is missing at time " +
            std::to_string(time->value_si));
    }
    return found->second;
}

CalibrationCaseSolution solve_calibration_case(
    const platform::ModelDocument& document,
    const std::string& case_id,
    const SteadySolverSettings& steady_settings,
    const TransientSolverSettings& transient_settings,
    const std::vector<double>& observation_times,
    const platform::ComponentRegistry& components,
    const physics::PropertyPackageRegistry& properties,
    const platform::EngineeringArtifactRegistry& engineering_artifacts,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry,
    const CalibrationState* warm_start) {
    const auto* simulation_case =
        selected_case(document, case_id);
    const auto mode = validation_mode(simulation_case);
    CalibrationCaseSolution solution;
    if (mode == "steady") {
        if (!observation_times.empty()) {
            throw std::invalid_argument(
                "steady calibration case cannot have observation times: " +
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
            graph.problem, to_core(steady_settings));
        if (!solve.diagnostics.converged) {
            throw std::runtime_error(
                "calibration case '" + case_id +
                "' failed to solve: " +
                solve.diagnostics.message);
        }
        const platform::GraphResultEvaluator evaluator(
            document, graph, properties, thermochemistry);
        solution.steady_graph = evaluator.evaluate(solve.x);
        for (std::size_t index = 0;
             index < graph.problem.variable_names.size(); ++index) {
            solution.state.emplace(
                graph.problem.variable_names[index], solve.x[index]);
        }
        return solution;
    }

    if (observation_times.empty()) {
        throw std::invalid_argument(
            "transient calibration case requires observation times: " +
            case_id);
    }
    auto graph = platform::compile_transient_model_graph(
        document, components, properties, engineering_artifacts,
        thermochemistry, case_id);
    auto options = to_core(transient_settings);
    if (observation_times.back() <= options.start_time) {
        throw std::invalid_argument(
            "transient calibration case '" + case_id +
            "' latest observation time must be greater than the "
            "integration start time");
    }
    options.end_time = observation_times.back();
    options.required_output_times = observation_times;
    const auto solve = integrate_dae(graph.problem, options);
    if (!solve.diagnostics.success) {
        throw std::runtime_error(
            "calibration case '" + case_id +
            "' failed to integrate: " +
            solve.diagnostics.message);
    }
    const platform::GraphResultEvaluator evaluator(
        document, graph, properties, thermochemistry);
    std::size_t next_time = 0;
    for (const auto& sample : solve.trajectory) {
        if (next_time >= observation_times.size()) break;
        if (sample.time == observation_times[next_time]) {
            solution.transient_graphs.emplace(
                sample.time,
                evaluator.evaluate(
                    sample.state, sample.derivative));
            ++next_time;
        }
    }
    if (next_time != observation_times.size()) {
        throw std::runtime_error(
            "transient calibration case '" + case_id +
            "' did not produce every required observation time");
    }
    return solution;
}

struct ObjectiveEvaluation {
    double value{std::numeric_limits<double>::infinity()};
    std::vector<CalibrationObservationResidual> observations;
    std::map<std::string, CalibrationState> case_states;
};

thermox::Matrix measurement_correlation_matrix(
    const platform::CalibrationDefinition& calibration) {
    thermox::Matrix matrix(
        calibration.observations.size(),
        std::vector<double>(
            calibration.observations.size(), 0.0));
    std::map<std::string, std::size_t, std::less<>> indices;
    for (std::size_t index = 0;
         index < calibration.observations.size(); ++index) {
        matrix[index][index] = 1.0;
        indices.emplace(calibration.observations[index].id, index);
    }
    for (const auto& correlation :
         calibration.measurement_correlations) {
        const auto first = indices.at(
            correlation.first_observation_id);
        const auto second = indices.at(
            correlation.second_observation_id);
        matrix[first][second] = correlation.correlation;
        matrix[second][first] = correlation.correlation;
    }
    return matrix;
}

thermox::DenseCholeskyFactorization measurement_whitener(
    const platform::CalibrationDefinition& calibration) {
    thermox::DenseCholeskyFactorization factorization;
    if (!factorization.factorize(
            measurement_correlation_matrix(calibration))) {
        throw std::invalid_argument(
            "measurement correlation matrix cannot be whitened: " +
            factorization.message());
    }
    return factorization;
}

ObjectiveEvaluation evaluate_calibration_objective(
    const platform::ModelDocument& document,
    const platform::CalibrationDefinition& calibration,
    const SteadySolverSettings& steady_settings,
    const TransientSolverSettings& transient_settings,
    const platform::ComponentRegistry& components,
    const physics::PropertyPackageRegistry& properties,
    const platform::EngineeringArtifactRegistry& engineering_artifacts,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry,
    const std::map<std::string, CalibrationState>*
        warm_starts = nullptr) {
    ObjectiveEvaluation evaluation;
    evaluation.value = 0.0;
    std::vector<double> normalized_residuals;
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
            std::vector<double> observation_times;
            for (const auto& candidate : calibration.observations) {
                if (candidate.case_id == observation.case_id &&
                    candidate.time.has_value()) {
                    observation_times.push_back(
                        candidate.time->value_si);
                }
            }
            std::sort(
                observation_times.begin(), observation_times.end());
            observation_times.erase(
                std::unique(
                    observation_times.begin(), observation_times.end()),
                observation_times.end());
            case_results.emplace(
                observation.case_id,
                solve_calibration_case(
                    document, observation.case_id,
                    steady_settings,
                    transient_settings,
                    observation_times,
                    components, properties, engineering_artifacts,
                    thermochemistry, warm_start));
        }
        const auto& predicted = require_graph_value(
            calibration_graph_at(
                case_results.at(observation.case_id),
                observation.time),
            observation.target);
        const double residual =
            predicted.value_si - observation.measured.value_si;
        const double normalized =
            residual / observation.sigma.value_si;
        normalized_residuals.push_back(normalized);
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
            observation.time.has_value()
                ? std::optional<double>(observation.time->value_si)
                : std::nullopt,
        });
    }
    const auto whitener = measurement_whitener(calibration);
    const auto whitened = whitener.solve_lower(
        std::move(normalized_residuals));
    if (!whitened.success) {
        throw std::runtime_error(
            "could not whiten calibration residuals: " +
            whitened.message);
    }
    for (const double residual : whitened.x) {
        evaluation.value += residual * residual;
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

std::string to_string(CalculationIntent intent) {
    switch (intent) {
        case CalculationIntent::forward_prediction:
            return "forward_prediction";
        case CalculationIntent::parameter_calibration:
            return "parameter_calibration";
        case CalculationIntent::data_reconciliation:
            return "data_reconciliation";
    }
    throw std::invalid_argument("unknown calculation intent");
}

std::string to_string(ReconciliationMode mode) {
    switch (mode) {
        case ReconciliationMode::hard_equalities:
            return "hard_equalities";
        case ReconciliationMode::weighted_measurements:
            return "weighted_measurements";
    }
    throw std::invalid_argument("unknown reconciliation mode");
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

std::string to_string(GlobalizationPolicy policy) {
    switch (policy) {
    case GlobalizationPolicy::line_search:
        return "line_search";
    case GlobalizationPolicy::trust_region:
        return "trust_region";
    }
    return "unknown";
}

GlobalizationPolicy globalization_policy_from_string(
    std::string_view value) {
    if (value == "line_search") {
        return GlobalizationPolicy::line_search;
    }
    if (value == "trust_region") {
        return GlobalizationPolicy::trust_region;
    }
    throw std::invalid_argument(
        "unknown globalization policy: " + std::string(value));
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
        component.supported_modes = descriptor.supported_modes;
        component.default_mode = descriptor.default_mode;
        for (const auto& event : descriptor.events) {
            component.events.push_back({
                event.name, event.dimension, event.direction,
                event.terminal, event.priority,
                event.hysteresis_si});
        }
        for (const auto& port : descriptor.ports) {
            component.ports.push_back(
                {port.name, port.domain, port.direction,
                 port.maximum_connections,
                 port.medium_source_port});
        }
        for (const auto& group : descriptor.port_groups) {
            component.port_groups.push_back({
                group.name, group.port_name_prefix, group.domain,
                group.direction, group.minimum_count,
                group.maximum_count, group.maximum_connections});
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
        response.thermal_feasibility =
            audit_counterflow_thermal_feasibility(response.graph);
        append_counterflow_thermal_metrics(
            response.graph, response.thermal_feasibility);
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
        !std::isfinite(settings.finite_difference_fraction) ||
        settings.finite_difference_fraction <= 0.0 ||
        settings.finite_difference_fraction >= 1.0 ||
        !std::isfinite(settings.initial_trust_region_radius) ||
        settings.initial_trust_region_radius <= 0.0 ||
        !std::isfinite(settings.minimum_trust_region_radius) ||
        settings.minimum_trust_region_radius <= 0.0 ||
        settings.minimum_trust_region_radius >=
            settings.initial_trust_region_radius ||
        !std::isfinite(settings.maximum_trust_region_radius) ||
        settings.maximum_trust_region_radius <
            settings.initial_trust_region_radius ||
        !std::isfinite(settings.acceptance_ratio) ||
        settings.acceptance_ratio < 0.0 ||
        settings.acceptance_ratio >= 1.0 ||
        !std::isfinite(settings.gradient_tolerance) ||
        settings.gradient_tolerance <= 0.0 ||
        !std::isfinite(settings.step_tolerance) ||
        settings.step_tolerance <= 0.0 ||
        !std::isfinite(settings.objective_relative_tolerance) ||
        settings.objective_relative_tolerance <= 0.0 ||
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
        (void)to_core(settings.steady_simulation_solver);
        (void)to_core(settings.transient_simulation_solver);
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
        platform::validate_calibration_initial_state_contracts(
            document, runtime->impl_->components,
            runtime->impl_->properties, engineering_artifacts,
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
        response.diagnostics.measurement_correlation_count =
            calibration->measurement_correlations.size();
        response.diagnostics.measurement_covariance_applied =
            !calibration->measurement_correlations.empty();
        response.diagnostics.adjustable_parameter_count =
            calibration->parameters.size();
        response.diagnostics.measurement_count =
            calibration->observations.size();
        response.diagnostics.prior_count = static_cast<std::size_t>(
            std::count_if(
                calibration->parameters.begin(),
                calibration->parameters.end(),
                [](const auto& parameter) {
                    return parameter.prior_mean.has_value();
                }));
    } catch (const std::exception& ex) {
        response.error = make_error(
            "unknown_calibration", "request", ex.what());
        return response;
    }

    std::vector<double> values;
    std::vector<double> initial;
    std::vector<double> lower;
    std::vector<double> upper;
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
            settings.steady_simulation_solver,
            settings.transient_simulation_solver,
            runtime->impl_->components,
            runtime->impl_->properties,
            engineering_artifacts,
            runtime->impl_->thermochemistry,
            warm_starts);
    };
    const auto evaluate_counted =
        [&](const std::vector<double>& candidate,
            const std::map<std::string, CalibrationState>*
                warm_starts) {
        ++response.diagnostics.objective_evaluations;
        return evaluate_at(candidate, warm_starts);
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
                result = evaluate_counted(
                    candidate, &current_warm_starts);
                progress = next;
                current_warm_starts = result.case_states;
                fraction = std::min(
                    1.0 - progress,
                    fraction * settings.continuation_growth);
            } catch (const std::exception&) {
                fraction *= 0.5;
                if (fraction <
                    settings.minimum_continuation_fraction) {
                    throw;
                }
            }
        }
        return result;
    };
    thermox::DenseCholeskyFactorization calibration_whitener;
    try {
        calibration_whitener = measurement_whitener(*calibration);
    } catch (const std::exception& ex) {
        response.error = make_error(
            "invalid_measurement_covariance", "validation", ex.what());
        return response;
    }
    const auto residuals_from =
        [&](const ObjectiveEvaluation& evaluation,
            const std::vector<double>& candidate) {
        std::vector<double> residuals;
        residuals.reserve(
            evaluation.observations.size() +
            response.diagnostics.prior_count);
        for (const auto& observation : evaluation.observations) {
            residuals.push_back(observation.normalized_residual);
        }
        const auto whitened = calibration_whitener.solve_lower(
            std::move(residuals));
        if (!whitened.success) {
            throw std::runtime_error(
                "could not whiten calibration residuals: " +
                whitened.message);
        }
        residuals = whitened.x;
        for (std::size_t index = 0;
             index < calibration->parameters.size(); ++index) {
            const auto& parameter = calibration->parameters[index];
            if (!parameter.prior_mean.has_value()) continue;
            residuals.push_back(
                (candidate[index] - parameter.prior_mean->value_si) /
                parameter.prior_sigma->value_si);
        }
        return residuals;
    };

    std::map<std::vector<double>, ObjectiveEvaluation> evaluations;
    const auto residual_callback =
        [&](const std::vector<double>& candidate,
            const std::vector<double>* reference) {
        try {
            const auto existing = evaluations.find(candidate);
            if (existing != evaluations.end()) {
                return thermox::BoundedResidualEvaluation{
                    true,
                    residuals_from(existing->second, candidate),
                    "ok",
                };
            }
            ObjectiveEvaluation evaluation;
            if (reference != nullptr) {
                const auto origin = evaluations.find(*reference);
                if (origin != evaluations.end()) {
                    evaluation = continue_to(
                        *reference, candidate,
                        origin->second.case_states);
                } else {
                    evaluation = evaluate_counted(candidate, nullptr);
                }
            } else {
                evaluation = evaluate_counted(candidate, nullptr);
            }
            const auto residuals = residuals_from(evaluation, candidate);
            evaluations.insert_or_assign(candidate, std::move(evaluation));
            return thermox::BoundedResidualEvaluation{
                true, residuals, "ok"};
        } catch (const std::exception& ex) {
            return thermox::BoundedResidualEvaluation{
                false, {}, ex.what()};
        }
    };
    const auto optimized =
        thermox::solve_bounded_nonlinear_least_squares(
            residual_callback, values, lower, upper,
            {
                .max_iterations = settings.max_iterations,
                .finite_difference_fraction =
                    settings.finite_difference_fraction,
                .initial_trust_region_radius =
                    settings.initial_trust_region_radius,
                .minimum_trust_region_radius =
                    settings.minimum_trust_region_radius,
                .maximum_trust_region_radius =
                    settings.maximum_trust_region_radius,
                .acceptance_ratio = settings.acceptance_ratio,
                .gradient_tolerance = settings.gradient_tolerance,
                .step_tolerance = settings.step_tolerance,
                .objective_relative_tolerance =
                    settings.objective_relative_tolerance,
            });
    if (!optimized.success) {
        if (!optimized.x.empty()) apply_values(optimized.x);
        response.status = OperationStatus::solver_failed;
        response.error = make_error(
            "calibration_optimizer_failed", "calibration",
            optimized.message);
        return response;
    }
    values = optimized.x;
    const auto fitted = evaluations.find(values);
    if (fitted == evaluations.end()) {
        response.status = OperationStatus::solver_failed;
        response.error = make_error(
            "calibration_optimizer_state_missing", "calibration",
            "optimizer result has no corresponding physical evaluation");
        return response;
    }
    ObjectiveEvaluation best = fitted->second;
    response.diagnostics.converged =
        optimized.diagnostics.converged;
    response.diagnostics.iterations =
        optimized.diagnostics.iterations;
    response.diagnostics.sensitivity_evaluations =
        optimized.diagnostics.sensitivity_evaluations;
    response.diagnostics.accepted_steps =
        optimized.diagnostics.accepted_steps;
    response.diagnostics.rejected_steps =
        optimized.diagnostics.rejected_steps;
    response.diagnostics.final_projected_gradient_norm =
        optimized.diagnostics.final_projected_gradient_norm;
    response.diagnostics.final_trust_region_radius =
        optimized.diagnostics.final_trust_region_radius;
    response.diagnostics.optimizer_factorization_quality_available =
        optimized.diagnostics.factorization_quality.available;
    response.diagnostics.optimizer_reciprocal_pivot_ratio =
        optimized.diagnostics.factorization_quality
            .reciprocal_pivot_ratio;
    response.diagnostics.optimizer_factorization_quality_method =
        optimized.diagnostics.factorization_quality.method;
    response.diagnostics.message =
        optimized.diagnostics.message;
    response.diagnostics.initial_objective =
        optimized.initial_objective;
    apply_values(values);
    response.diagnostics.final_objective =
        optimized.final_objective;

    const auto numerical_rank = [](const thermox::Matrix& matrix) {
        if (matrix.empty() || matrix.front().empty()) {
            return std::size_t{0};
        }
        const std::size_t rows = matrix.size();
        const std::size_t columns = matrix.front().size();
        if (rows >= columns) {
            return thermox::solve_dense_least_squares(
                       matrix, std::vector<double>(rows, 0.0))
                .rank;
        }
        thermox::Matrix transpose(
            columns, std::vector<double>(rows, 0.0));
        for (std::size_t row = 0; row < rows; ++row) {
            for (std::size_t column = 0; column < columns; ++column) {
                transpose[column][row] = matrix[row][column];
            }
        }
        return thermox::solve_dense_least_squares(
                   std::move(transpose),
                   std::vector<double>(columns, 0.0))
            .rank;
    };
    try {
        thermox::Matrix data_sensitivity(
            best.observations.size(),
            std::vector<double>(values.size(), 0.0));
        for (std::size_t column = 0; column < values.size(); ++column) {
            const double range = upper[column] - lower[column];
            const double delta =
                settings.finite_difference_fraction * range;
            const bool can_subtract = values[column] - delta >= lower[column];
            const bool can_add = values[column] + delta <= upper[column];
            if (!can_subtract && !can_add) {
                throw std::runtime_error(
                    "could not perturb bounded calibration parameter '" +
                    calibration->parameters[column].id + "'");
            }

            ObjectiveEvaluation negative;
            ObjectiveEvaluation positive;
            if (can_subtract) {
                auto candidate = values;
                candidate[column] -= delta;
                ++response.diagnostics.objective_evaluations;
                ++response.diagnostics.sensitivity_evaluations;
                negative = evaluate_at(candidate, &best.case_states);
            }
            if (can_add) {
                auto candidate = values;
                candidate[column] += delta;
                ++response.diagnostics.objective_evaluations;
                ++response.diagnostics.sensitivity_evaluations;
                positive = evaluate_at(candidate, &best.case_states);
            }
            for (std::size_t row = 0;
                 row < data_sensitivity.size(); ++row) {
                if (can_subtract && can_add) {
                    data_sensitivity[row][column] =
                        (positive.observations[row].normalized_residual -
                         negative.observations[row].normalized_residual) /
                        (2.0 * delta);
                } else if (can_add) {
                    data_sensitivity[row][column] =
                        (positive.observations[row].normalized_residual -
                         best.observations[row].normalized_residual) /
                        delta;
                } else {
                    data_sensitivity[row][column] =
                        (best.observations[row].normalized_residual -
                         negative.observations[row].normalized_residual) /
                        delta;
                }
            }
        }
        apply_values(values);

        const auto whitener = measurement_whitener(*calibration);
        auto whitened_data = whitener.whiten_rows(data_sensitivity);
        if (whitened_data.empty()) {
            throw std::runtime_error(
                "could not whiten final calibration sensitivity");
        }
        response.diagnostics.data_sensitivity_rank =
            numerical_rank(whitened_data);
        response.diagnostics.locally_data_identifiable =
            response.diagnostics.data_sensitivity_rank == values.size();

        auto posterior_sensitivity = whitened_data;
        for (std::size_t index = 0;
             index < calibration->parameters.size(); ++index) {
            const auto& parameter = calibration->parameters[index];
            if (!parameter.prior_mean.has_value()) continue;
            std::vector<double> prior_row(values.size(), 0.0);
            prior_row[index] = 1.0 / parameter.prior_sigma->value_si;
            posterior_sensitivity.push_back(std::move(prior_row));
        }
        response.diagnostics.posterior_sensitivity_rank =
            numerical_rank(posterior_sensitivity);
        response.diagnostics.locally_posterior_identifiable =
            response.diagnostics.posterior_sensitivity_rank ==
            values.size();

        std::vector<bool> bound_active(values.size(), false);
        std::vector<std::size_t> free_indices;
        for (std::size_t index = 0; index < values.size(); ++index) {
            const double tolerance = 1.0e-10 *
                (upper[index] - lower[index]);
            bound_active[index] =
                values[index] - lower[index] <= tolerance ||
                upper[index] - values[index] <= tolerance;
            if (!bound_active[index]) free_indices.push_back(index);
        }
        response.diagnostics.active_bound_count =
            values.size() - free_indices.size();
        response.diagnostics.free_uncertainty_parameter_count =
            free_indices.size();

        std::vector<std::optional<double>> standard_uncertainties(
            values.size());
        thermox::Matrix free_covariance;
        if (free_indices.empty()) {
            response.diagnostics.uncertainty_message =
                "all fitted parameters are bound-active; two-sided local "
                "uncertainty is not estimated";
        } else if (!response.diagnostics.converged) {
            response.diagnostics.uncertainty_message =
                "calibration optimizer did not converge; local rank is "
                "reported but fitted-parameter covariance is withheld";
        } else {
            thermox::Matrix free_sensitivity(
                posterior_sensitivity.size(),
                std::vector<double>(free_indices.size(), 0.0));
            for (std::size_t row = 0;
                 row < posterior_sensitivity.size(); ++row) {
                for (std::size_t column = 0;
                     column < free_indices.size(); ++column) {
                    free_sensitivity[row][column] =
                        posterior_sensitivity[row][free_indices[column]];
                }
            }
            const auto factorization = thermox::solve_dense_least_squares(
                std::move(free_sensitivity),
                std::vector<double>(posterior_sensitivity.size(), 0.0));
            if (!factorization.success) {
                response.diagnostics.uncertainty_message =
                    "free-parameter posterior sensitivity is not full "
                    "rank: " + factorization.message;
            } else {
                response.diagnostics.uncertainty_available = true;
                response.diagnostics.uncertainty_message =
                    response.diagnostics.prior_count == 0U
                        ? "local linearized covariance from declared "
                          "measurement uncertainty"
                        : "local posterior covariance from declared "
                          "measurement uncertainty and parameter priors";
                response.diagnostics
                    .sensitivity_factorization_quality_available =
                    factorization.factorization_quality.available;
                response.diagnostics
                    .sensitivity_reciprocal_pivot_ratio =
                    factorization.factorization_quality
                        .reciprocal_pivot_ratio;
                response.diagnostics
                    .sensitivity_factorization_quality_method =
                    factorization.factorization_quality.method;
                free_covariance = factorization.covariance;
                for (std::size_t index = 0;
                     index < free_indices.size(); ++index) {
                    const double variance = free_covariance[index][index];
                    if (!std::isfinite(variance) || variance < 0.0) {
                        throw std::runtime_error(
                            "calibration covariance has an invalid "
                            "diagonal value");
                    }
                    standard_uncertainties[free_indices[index]] =
                        std::sqrt(variance);
                }
            }
        }

        for (std::size_t index = 0; index < values.size(); ++index) {
            response.parameter_uncertainties.push_back({
                calibration->parameters[index].id,
                calibration->parameters[index]
                    .lower_bound->dimension,
                standard_uncertainties[index],
                bound_active[index],
                bound_active[index]
                    ? "bound_active_one_sided_not_estimated"
                    : (response.diagnostics.uncertainty_available
                           ? (response.diagnostics.prior_count == 0U
                                  ? "local_measurement_linearized"
                                  : "local_posterior_linearized")
                           : (!response.diagnostics.converged
                                  ? "unavailable_fit_not_converged"
                                  : "unavailable_rank_deficient")),
            });
        }
        if (response.diagnostics.uncertainty_available) {
            for (std::size_t first = 0;
                 first < free_indices.size(); ++first) {
                for (std::size_t second = first + 1;
                     second < free_indices.size(); ++second) {
                    const double denominator =
                        *standard_uncertainties[free_indices[first]] *
                        *standard_uncertainties[free_indices[second]];
                    response.parameter_correlations.push_back({
                        calibration->parameters[free_indices[first]].id,
                        calibration->parameters[free_indices[second]].id,
                        denominator > 0.0
                            ? std::clamp(
                                  free_covariance[first][second] /
                                      denominator,
                                  -1.0, 1.0)
                            : 0.0,
                    });
                }
            }
        }
    } catch (const std::exception& ex) {
        apply_values(values);
        response.diagnostics.uncertainty_available = false;
        response.diagnostics.uncertainty_message =
            std::string("local sensitivity analysis failed: ") + ex.what();
    }

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

DataReconciliationResponse
SimulationService::run_data_reconciliation(
    const DataReconciliationRequest& request) const {
    DataReconciliationResponse response;
    response.reconciliation_id = request.reconciliation_id;
    response.mode = request.mode;
    if (!valid_schema(request.schema_version) ||
        request.model_json.empty() ||
        request.reconciliation_id.empty()) {
        response.error = make_error(
            "invalid_reconciliation_request", "request",
            "schema_version, model_json, and reconciliation_id "
            "are required");
        return response;
    }
    const auto& settings = request.solver;
    if (settings.max_iterations <= 0 ||
        !std::isfinite(settings.finite_difference_fraction) ||
        settings.finite_difference_fraction <= 0.0 ||
        settings.finite_difference_fraction >= 1.0 ||
        !std::isfinite(settings.constraint_tolerance) ||
        settings.constraint_tolerance <= 0.0 ||
        !std::isfinite(settings.step_tolerance) ||
        settings.step_tolerance <= 0.0 ||
        !std::isfinite(settings.objective_relative_tolerance) ||
        settings.objective_relative_tolerance <= 0.0 ||
        !std::isfinite(settings.minimum_line_search_fraction) ||
        settings.minimum_line_search_fraction <= 0.0 ||
        settings.minimum_line_search_fraction >= 1.0 ||
        (request.profile_likelihood.enabled &&
         (!std::isfinite(
              request.profile_likelihood.objective_increase) ||
          request.profile_likelihood.objective_increase <= 0.0 ||
          request.profile_likelihood.maximum_bracket_steps <= 0 ||
          request.profile_likelihood.maximum_bisection_steps <= 0 ||
          request.profile_likelihood.maximum_nuisance_iterations <= 0)) ||
        (request.joint_confidence_region.enabled &&
         (!std::isfinite(
              request.joint_confidence_region.objective_increase) ||
          request.joint_confidence_region.objective_increase <= 0.0))) {
        response.error = make_error(
            "invalid_reconciliation_settings", "request",
            "invalid data-reconciliation solver settings");
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
    SimulationArtifactBundle artifacts;
    platform::EngineeringArtifactRegistry engineering_artifacts;
    try {
        runtime = request_runtime(
            impl_->runtime, request.components);
        artifacts = resolve_artifacts(
            request.artifacts,
            impl_->artifact_resolver.get());
        engineering_artifacts = execution_engineering_artifacts(
            runtime->impl_->engineering_artifacts,
            artifacts);
    } catch (const std::exception& ex) {
        response.error = make_error(
            "invalid_reconciliation_runtime", "request", ex.what());
        return response;
    }

    platform::ModelDocument document;
    const platform::CalibrationDefinition* definition = nullptr;
    try {
        document = platform::parse_model_document_text(
            request.model_json, runtime->impl_->units);
        platform::validate_calibration_observation_contracts(
            document, runtime->impl_->components,
            runtime->impl_->thermochemistry);
        definition = &require_calibration(
            document, request.reconciliation_id);
        if (request.profile_likelihood.enabled &&
            request.mode != ReconciliationMode::weighted_measurements) {
            throw std::invalid_argument(
                "profile likelihood requires weighted-measurements "
                "reconciliation mode");
        }
        if (request.joint_confidence_region.enabled &&
            request.mode != ReconciliationMode::weighted_measurements) {
            throw std::invalid_argument(
                "joint confidence regions require weighted-measurements "
                "reconciliation mode");
        }
        if (request.mode == ReconciliationMode::hard_equalities &&
            definition->parameters.size() !=
                definition->observations.size()) {
            throw std::invalid_argument(
                "data reconciliation requires one hard equality per "
                "adjustable quantity");
        }
        if (request.mode ==
                ReconciliationMode::weighted_measurements &&
            definition->observations.size() <
                definition->parameters.size()) {
            throw std::invalid_argument(
                "weighted reconciliation requires at least as many "
                "measurements as adjustable quantities");
        }
        if (request.mode == ReconciliationMode::hard_equalities &&
            !definition->measurement_correlations.empty()) {
            throw std::invalid_argument(
                "hard-equality reconciliation does not use measurement "
                "correlations; select weighted-measurements mode");
        }
        for (const auto& parameter : definition->parameters) {
            if (parameter.prior_mean.has_value()) {
                throw std::invalid_argument(
                    "data reconciliation does not accept parameter "
                    "priors; hard equalities determine inferred values");
            }
        }
        if (request.profile_likelihood.enabled) {
            std::set<std::string> parameter_ids;
            for (const auto& parameter : definition->parameters) {
                parameter_ids.insert(parameter.id);
            }
            std::set<std::string> requested_ids;
            for (const auto& id :
                 request.profile_likelihood.parameter_ids) {
                if (id.empty() ||
                    !requested_ids.insert(id).second ||
                    !parameter_ids.contains(id)) {
                    throw std::invalid_argument(
                        "profile-likelihood parameter IDs must be "
                        "unique and reference declared adjustable "
                        "quantities: " + id);
                }
            }
        }
        if (request.joint_confidence_region.enabled) {
            std::set<std::string> parameter_ids;
            for (const auto& parameter : definition->parameters) {
                parameter_ids.insert(parameter.id);
            }
            std::set<std::string> requested_ids;
            for (const auto& id :
                 request.joint_confidence_region.parameter_ids) {
                if (id.empty() ||
                    !requested_ids.insert(id).second ||
                    !parameter_ids.contains(id)) {
                    throw std::invalid_argument(
                        "joint-confidence-region parameter IDs must be "
                        "unique and reference declared adjustable "
                        "quantities: " + id);
                }
            }
        }
        std::set<std::pair<std::string, std::string>> hard_targets;
        for (const auto& observation : definition->observations) {
            hard_targets.emplace(
                observation.case_id, observation.target);
        }
        std::set<std::string> held_out_ids;
        for (const auto& held_out_case : request.held_out_cases) {
            if (held_out_case.case_id.empty() ||
                held_out_case.observations.empty()) {
                throw std::invalid_argument(
                    "held-out cases require a case ID and observations");
            }
            for (const auto& observation :
                 held_out_case.observations) {
                if (observation.id.empty() ||
                    observation.target.empty() ||
                    observation.dimension.empty() ||
                    !held_out_ids.insert(observation.id).second ||
                    !std::isfinite(observation.measured_si) ||
                    !std::isfinite(observation.sigma_si) ||
                    observation.sigma_si <= 0.0) {
                    throw std::invalid_argument(
                        "held-out observations require unique IDs, "
                        "targets, dimensions, finite measurements, and "
                        "positive uncertainties");
                }
                if (hard_targets.contains({
                        held_out_case.case_id,
                        observation.target})) {
                    throw std::invalid_argument(
                        "held-out observation cannot also be a hard "
                        "reconciliation constraint");
                }
            }
        }
        response.metadata = execution_metadata(
            document, request.schema_version, "",
            "data_reconciliation",
            solver_provenance(
                settings, request.mode, request.profile_likelihood,
                request.joint_confidence_region),
            runtime->impl_->fingerprint,
            runtime->impl_->components,
            runtime->impl_->properties);
        response.metadata.artifacts = artifact_provenance(artifacts);
        response.diagnostics.adjustable_quantity_count =
            definition->parameters.size();
        response.diagnostics.measurement_count =
            definition->observations.size();
        response.diagnostics.measurement_correlation_count =
            definition->measurement_correlations.size();
        response.diagnostics.measurement_covariance_applied =
            !definition->measurement_correlations.empty();
        response.diagnostics.degrees_of_freedom =
            definition->observations.size() -
            definition->parameters.size();
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_model;
        response.error = make_error(
            "invalid_reconciliation_model", "validation", ex.what());
        return response;
    }

    std::vector<double> values;
    std::vector<double> initial;
    std::vector<double> lower;
    std::vector<double> upper;
    try {
        for (const auto& parameter : definition->parameters) {
            if (!parameter.lower_bound.has_value() ||
                !parameter.upper_bound.has_value() ||
                !std::isfinite(parameter.lower_bound->value_si) ||
                !std::isfinite(parameter.upper_bound->value_si)) {
                throw std::invalid_argument(
                    "reconciliation adjustable quantity '" +
                    parameter.id + "' requires finite bounds");
            }
            const double value =
                platform::require_calibration_parameter_target(
                    document, parameter.targets.front()).value_si;
            values.push_back(value);
            initial.push_back(value);
            lower.push_back(parameter.lower_bound->value_si);
            upper.push_back(parameter.upper_bound->value_si);
        }
    } catch (const std::exception& ex) {
        response.status = OperationStatus::invalid_model;
        response.error = make_error(
            "invalid_reconciliation_bounds", "validation", ex.what());
        return response;
    }

    const auto apply_values =
        [&](const std::vector<double>& candidate) {
        for (std::size_t index = 0;
             index < definition->parameters.size(); ++index) {
            for (const auto& target :
                 definition->parameters[index].targets) {
                platform::require_calibration_parameter_target(
                    document, target).value_si = candidate[index];
            }
        }
    };
    const auto evaluate =
        [&](const std::vector<double>& candidate,
            const std::map<std::string, CalibrationState>* warm_starts) {
        apply_values(candidate);
        auto result = evaluate_calibration_objective(
            document, *definition, settings.simulation_solver,
            TransientSolverSettings{},
            runtime->impl_->components,
            runtime->impl_->properties, engineering_artifacts,
            runtime->impl_->thermochemistry, warm_starts);
        ++response.diagnostics.model_evaluations;
        return result;
    };
    const auto maximum_constraint =
        [](const ObjectiveEvaluation& evaluation) {
        double maximum = 0.0;
        for (const auto& observation : evaluation.observations) {
            maximum = std::max(
                maximum,
                std::abs(observation.normalized_residual));
        }
        return maximum;
    };
    const auto squared_constraint_norm =
        [](const ObjectiveEvaluation& evaluation) {
        return evaluation.value;
    };

    thermox::DenseCholeskyFactorization correlation_whitener;
    if (request.mode == ReconciliationMode::weighted_measurements) {
        try {
            correlation_whitener = measurement_whitener(*definition);
        } catch (const std::exception& ex) {
            response.status = OperationStatus::invalid_model;
            response.error = make_error(
                "invalid_measurement_covariance", "validation",
                ex.what());
            return response;
        }
    }

    ObjectiveEvaluation best;
    try {
        best = evaluate(values, nullptr);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::solver_failed;
        response.error = make_error(
            "reconciliation_baseline_failed", "reconciliation",
            ex.what());
        return response;
    }
    response.diagnostics
        .initial_maximum_absolute_normalized_constraint =
        maximum_constraint(best);

    const auto sensitivity_at =
        [&](const std::vector<double>& center,
            const ObjectiveEvaluation& center_evaluation) {
        thermox::Matrix jacobian(
            center_evaluation.observations.size(),
            std::vector<double>(center.size(), 0.0));
        for (std::size_t column = 0;
             column < center.size(); ++column) {
            const double range = upper[column] - lower[column];
            double delta = settings.finite_difference_fraction * range;
            if (center[column] + delta > upper[column]) {
                delta = -delta;
            }
            if (center[column] + delta < lower[column] ||
                delta == 0.0) {
                throw std::runtime_error(
                    "could not perturb bounded adjustable quantity '" +
                    definition->parameters[column].id + "'");
            }
            auto perturbed_values = center;
            perturbed_values[column] += delta;
            const auto perturbed = evaluate(
                perturbed_values, &center_evaluation.case_states);
            for (std::size_t row = 0;
                 row < jacobian.size(); ++row) {
                jacobian[row][column] =
                    (perturbed.observations[row]
                         .normalized_residual -
                     center_evaluation.observations[row]
                         .normalized_residual) /
                    delta;
            }
        }
        apply_values(center);
        return jacobian;
    };
    const auto sensitivity_rank =
        [](thermox::Matrix matrix) {
        if (matrix.empty() || matrix.front().empty()) {
            return std::size_t{0};
        }
        double maximum = 0.0;
        for (const auto& row : matrix) {
            for (const double value : row) {
                maximum = std::max(maximum, std::abs(value));
            }
        }
        const double tolerance = std::max(
            1.0, maximum) *
            std::numeric_limits<double>::epsilon() *
            static_cast<double>(
                std::max(matrix.size(), matrix.front().size())) *
            100.0;
        std::size_t rank = 0;
        for (std::size_t column = 0;
             column < matrix.front().size() && rank < matrix.size();
             ++column) {
            std::size_t pivot = rank;
            for (std::size_t row = rank + 1;
                 row < matrix.size(); ++row) {
                if (std::abs(matrix[row][column]) >
                    std::abs(matrix[pivot][column])) {
                    pivot = row;
                }
            }
            if (std::abs(matrix[pivot][column]) <= tolerance) continue;
            std::swap(matrix[pivot], matrix[rank]);
            const double diagonal = matrix[rank][column];
            for (std::size_t row = rank + 1;
                 row < matrix.size(); ++row) {
                const double factor = matrix[row][column] / diagonal;
                for (std::size_t trailing = column;
                     trailing < matrix[row].size(); ++trailing) {
                    matrix[row][trailing] -=
                        factor * matrix[rank][trailing];
                }
            }
            ++rank;
        }
        return rank;
    };
    const auto record_factorization_quality =
        [&](const thermox::FactorizationQuality& quality) {
        if (!quality.available) return;
        const double ratio = quality.reciprocal_pivot_ratio;
        if (!response.diagnostics
                 .sensitivity_factorization_quality_available ||
            ratio < response.diagnostics
                .minimum_sensitivity_reciprocal_pivot_ratio) {
            response.diagnostics
                .minimum_sensitivity_reciprocal_pivot_ratio = ratio;
            response.diagnostics
                .sensitivity_factorization_quality_method =
                quality.method;
        }
        response.diagnostics
            .sensitivity_factorization_quality_available = true;
    };
    const auto at_lower_bound =
        [&](std::size_t index) {
        const double tolerance =
            1.0e-10 * (upper[index] - lower[index]);
        return values[index] - lower[index] <= tolerance;
    };
    const auto at_upper_bound =
        [&](std::size_t index) {
        const double tolerance =
            1.0e-10 * (upper[index] - lower[index]);
        return upper[index] - values[index] <= tolerance;
    };
    const auto update_active_bound_diagnostics =
        [&](const std::vector<double>* local_step = nullptr) {
        response.diagnostics.active_bounds.clear();
        for (std::size_t index = 0;
             index < values.size(); ++index) {
            const bool lower_active = at_lower_bound(index);
            const bool upper_active = at_upper_bound(index);
            if (!lower_active && !upper_active) continue;
            const bool lower_limits =
                local_step != nullptr && lower_active &&
                (*local_step)[index] < 0.0;
            const bool upper_limits =
                local_step != nullptr && upper_active &&
                (*local_step)[index] > 0.0;
            if (lower_active) {
                response.diagnostics.active_bounds.push_back({
                    definition->parameters[index].id,
                    "lower", values[index], lower[index],
                    lower_limits});
            } else {
                response.diagnostics.active_bounds.push_back({
                    definition->parameters[index].id,
                    "upper", values[index], upper[index],
                    upper_limits});
            }
        }
        response.diagnostics.active_bound_count =
            response.diagnostics.active_bounds.size();
    };

    for (int iteration = 0;
         iteration < settings.max_iterations; ++iteration) {
        const double current_maximum = maximum_constraint(best);
        if (request.mode == ReconciliationMode::hard_equalities &&
            current_maximum <= settings.constraint_tolerance) {
            response.diagnostics.converged = true;
            response.diagnostics.message =
                "hard reconciliation constraints satisfied";
            break;
        }
        thermox::Matrix jacobian;
        try {
            jacobian = sensitivity_at(values, best);
            if (request.mode ==
                    ReconciliationMode::weighted_measurements) {
                jacobian =
                    correlation_whitener.whiten_rows(jacobian);
                if (jacobian.empty()) {
                    throw std::runtime_error(
                        "could not whiten measurement sensitivity");
                }
            }
        } catch (const std::exception& ex) {
            response.error = make_error(
                "reconciliation_jacobian_failed", "reconciliation",
                ex.what());
            break;
        }
        std::vector<double> step;
        if (request.mode == ReconciliationMode::hard_equalities) {
            response.diagnostics.sensitivity_rank =
                sensitivity_rank(jacobian);
            response.diagnostics.locally_identifiable =
                response.diagnostics.sensitivity_rank == values.size();
            if (!response.diagnostics.locally_identifiable) {
                response.error = make_error(
                    "reconciliation_unidentifiable", "reconciliation",
                    "measurement sensitivity rank " +
                        std::to_string(
                            response.diagnostics.sensitivity_rank) +
                        " is below adjustable quantity count " +
                        std::to_string(values.size()));
                break;
            }
            std::vector<double> rhs(values.size());
            for (std::size_t row = 0; row < values.size(); ++row) {
                rhs[row] =
                    -best.observations[row].normalized_residual;
            }
            const auto linear = thermox::solve_dense_linear_system(
                std::move(jacobian), std::move(rhs));
            if (!linear.success) {
                response.error = make_error(
                    "reconciliation_unidentifiable", "reconciliation",
                    "measurement sensitivity system is singular: " +
                        linear.message);
                break;
            }
            record_factorization_quality(
                linear.factorization_quality);
            step = linear.x;
        } else {
            std::vector<double> rhs(jacobian.size(), 0.0);
            for (std::size_t row = 0; row < jacobian.size(); ++row) {
                rhs[row] =
                    -best.observations[row].normalized_residual;
            }
            const auto whitened_rhs =
                correlation_whitener.solve_lower(std::move(rhs));
            if (!whitened_rhs.success) {
                response.error = make_error(
                    "invalid_measurement_covariance", "reconciliation",
                    "could not whiten measurement residuals: " +
                        whitened_rhs.message);
                break;
            }
            const auto full_least_squares =
                thermox::solve_dense_least_squares(
                    jacobian, whitened_rhs.x);
            response.diagnostics.sensitivity_rank =
                full_least_squares.rank;
            response.diagnostics.locally_identifiable =
                full_least_squares.rank == values.size();
            record_factorization_quality(
                full_least_squares.factorization_quality);
            if (!full_least_squares.success) {
                response.error = make_error(
                    "reconciliation_unidentifiable", "reconciliation",
                    full_least_squares.message);
                break;
            }

            std::vector<std::size_t> free_indices;
            for (std::size_t column = 0;
                 column < values.size(); ++column) {
                double gradient = 0.0;
                double gradient_scale = 0.0;
                for (std::size_t row = 0;
                     row < jacobian.size(); ++row) {
                    const double contribution =
                        -jacobian[row][column] *
                        whitened_rhs.x[row];
                    gradient += contribution;
                    gradient_scale += std::abs(contribution);
                }
                const double tolerance =
                    1.0e-12 * std::max(1.0, gradient_scale);
                const bool outward =
                    (at_lower_bound(column) &&
                     gradient > tolerance) ||
                    (at_upper_bound(column) &&
                     gradient < -tolerance);
                if (!outward) free_indices.push_back(column);
            }
            step.assign(values.size(), 0.0);
            if (free_indices.size() == values.size()) {
                step = full_least_squares.x;
            } else if (!free_indices.empty()) {
                thermox::Matrix free_jacobian(
                    jacobian.size(),
                    std::vector<double>(free_indices.size(), 0.0));
                for (std::size_t row = 0;
                     row < jacobian.size(); ++row) {
                    for (std::size_t column = 0;
                         column < free_indices.size(); ++column) {
                        free_jacobian[row][column] =
                            jacobian[row][free_indices[column]];
                    }
                }
                const auto free_least_squares =
                    thermox::solve_dense_least_squares(
                        std::move(free_jacobian),
                        whitened_rhs.x);
                record_factorization_quality(
                    free_least_squares.factorization_quality);
                if (!free_least_squares.success) {
                    response.error = make_error(
                        "reconciliation_unidentifiable",
                        "reconciliation",
                        "free-parameter sensitivity is singular: " +
                            free_least_squares.message);
                    break;
                }
                for (std::size_t index = 0;
                     index < free_indices.size(); ++index) {
                    step[free_indices[index]] =
                        free_least_squares.x[index];
                }
            }
        }
        double maximum_scaled_step = 0.0;
        for (std::size_t index = 0;
             index < values.size(); ++index) {
            maximum_scaled_step = std::max(
                maximum_scaled_step,
                std::abs(step[index]) /
                    (upper[index] - lower[index]));
        }
        if (request.mode ==
                ReconciliationMode::weighted_measurements &&
            maximum_scaled_step <= settings.step_tolerance) {
            response.diagnostics.converged = true;
            response.diagnostics.message =
                "weighted reconciliation active-set step tolerance "
                "reached";
            break;
        }
        bool accepted = false;
        double fraction = 1.0;
        const double base_norm = squared_constraint_norm(best);
        while (fraction >=
               settings.minimum_line_search_fraction) {
            auto candidate = values;
            for (std::size_t index = 0;
                 index < candidate.size(); ++index) {
                candidate[index] = std::clamp(
                    values[index] +
                        fraction * step[index],
                    lower[index], upper[index]);
            }
            try {
                auto evaluation = evaluate(
                    candidate, &best.case_states);
                const double candidate_norm =
                    squared_constraint_norm(evaluation);
                if (candidate_norm < base_norm) {
                    values = std::move(candidate);
                    best = std::move(evaluation);
                    accepted = true;
                    if (request.mode ==
                            ReconciliationMode::weighted_measurements &&
                        (base_norm - candidate_norm) /
                                std::max(base_norm, 1.0) <=
                            settings.objective_relative_tolerance) {
                        response.diagnostics.converged = true;
                        response.diagnostics.message =
                            "weighted reconciliation objective "
                            "tolerance reached";
                    }
                    break;
                }
            } catch (const std::exception&) {
            }
            fraction *= 0.5;
        }
        response.diagnostics.iterations = iteration + 1;
        if (!accepted) {
            apply_values(values);
            update_active_bound_diagnostics(&step);
            response.diagnostics.locally_bound_limited =
                request.mode == ReconciliationMode::hard_equalities &&
                std::any_of(
                    response.diagnostics.active_bounds.begin(),
                    response.diagnostics.active_bounds.end(),
                    [](const auto& bound) {
                        return bound.limits_local_step;
                    });
            if (response.diagnostics.locally_bound_limited) {
                std::string parameter_list;
                for (const auto& bound :
                     response.diagnostics.active_bounds) {
                    if (!bound.limits_local_step) continue;
                    if (!parameter_list.empty()) {
                        parameter_list += ", ";
                    }
                    parameter_list += bound.parameter_id +
                        " (" + bound.side + ")";
                }
                response.error = make_error(
                    "reconciliation_locally_bound_limited",
                    "reconciliation",
                    "hard constraints remain unsatisfied and no "
                    "residual-reducing local Newton step was found "
                    "within declared bounds; limiting adjustable "
                    "quantities: " +
                        parameter_list);
            } else {
                response.error = make_error(
                    "reconciliation_line_search_failed",
                    "reconciliation",
                    "no bounded reconciliation step reduced the "
                    "measurement residual");
            }
            break;
        }
        if (response.diagnostics.converged) break;
    }
    if (request.mode == ReconciliationMode::hard_equalities &&
        !response.diagnostics.converged &&
        maximum_constraint(best) <= settings.constraint_tolerance) {
        response.diagnostics.converged = true;
        response.diagnostics.message =
            "hard reconciliation constraints satisfied";
    }
    if (!response.diagnostics.converged &&
        response.error.code.empty()) {
        response.error = make_error(
            "reconciliation_iteration_limit", "reconciliation",
            request.mode == ReconciliationMode::hard_equalities
                ? "hard constraints were not satisfied within the "
                  "iteration budget"
                : "weighted measurements did not converge within the "
                  "iteration budget");
    }
    apply_values(values);
    if (!response.diagnostics.locally_bound_limited) {
        update_active_bound_diagnostics();
    }
    response.diagnostics
        .final_maximum_absolute_normalized_constraint =
        maximum_constraint(best);
    response.diagnostics.weighted_sum_squares =
        squared_constraint_norm(best);
    if (response.diagnostics.degrees_of_freedom > 0) {
        response.diagnostics.reduced_chi_square_available = true;
        response.diagnostics.reduced_chi_square =
            response.diagnostics.weighted_sum_squares /
            static_cast<double>(
                response.diagnostics.degrees_of_freedom);
    }
    if (request.mode == ReconciliationMode::hard_equalities) {
        response.hard_constraints = best.observations;
    } else {
        response.weighted_measurements = best.observations;
    }
    for (std::size_t index = 0;
         index < definition->parameters.size(); ++index) {
        const auto& parameter = definition->parameters[index];
        response.inferred_parameters.push_back({
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
    response.reconciled_model_json =
        detail::serialize_model_document_json(document);
    if (!response.diagnostics.converged) {
        response.status = OperationStatus::solver_failed;
        return response;
    }

    if (request.mode == ReconciliationMode::weighted_measurements) {
        try {
        const auto final_jacobian = sensitivity_at(values, best);
        const auto whitened_final_jacobian =
            correlation_whitener.whiten_rows(final_jacobian);
        if (whitened_final_jacobian.empty()) {
            throw std::runtime_error(
                "could not whiten final measurement sensitivity");
        }
        const std::size_t count = values.size();
        std::vector<bool> bound_active(count, false);
        std::vector<std::size_t> free_indices;
        for (std::size_t index = 0; index < count; ++index) {
            bound_active[index] =
                at_lower_bound(index) || at_upper_bound(index);
            if (!bound_active[index]) free_indices.push_back(index);
        }
        response.diagnostics.active_bound_count =
            count - free_indices.size();
        response.diagnostics.free_uncertainty_parameter_count =
            free_indices.size();

        std::vector<std::optional<double>> standard_uncertainties(
            count);
        thermox::Matrix free_covariance;
        if (!free_indices.empty()) {
            thermox::Matrix free_jacobian(
                whitened_final_jacobian.size(),
                std::vector<double>(free_indices.size(), 0.0));
            for (std::size_t row = 0;
                 row < whitened_final_jacobian.size(); ++row) {
                for (std::size_t column = 0;
                     column < free_indices.size(); ++column) {
                    free_jacobian[row][column] =
                        whitened_final_jacobian[row]
                            [free_indices[column]];
                }
            }
            const auto free_factorization =
                thermox::solve_dense_least_squares(
                    std::move(free_jacobian),
                    std::vector<double>(
                        whitened_final_jacobian.size(), 0.0));
            if (!free_factorization.success) {
                throw std::runtime_error(
                    "final free-parameter sensitivity factorization "
                    "failed: " + free_factorization.message);
            }
            record_factorization_quality(
                free_factorization.factorization_quality);
            free_covariance = free_factorization.covariance;
            for (std::size_t free_index = 0;
                 free_index < free_indices.size(); ++free_index) {
                const double variance =
                    free_covariance[free_index][free_index];
                if (!std::isfinite(variance) || variance < 0.0) {
                    throw std::runtime_error(
                        "final free-parameter covariance is not positive "
                        "on its diagonal");
                }
                standard_uncertainties[free_indices[free_index]] =
                    std::sqrt(variance);
            }
        }
        for (std::size_t index = 0; index < count; ++index) {
            response.parameter_uncertainties.push_back({
                definition->parameters[index].id,
                definition->parameters[index]
                    .lower_bound->dimension,
                standard_uncertainties[index],
                bound_active[index],
                bound_active[index]
                    ? "bound_active_one_sided_not_estimated"
                    : "local_two_sided_linearized",
            });
        }
        for (std::size_t first = 0;
             first < free_indices.size(); ++first) {
            for (std::size_t second = first + 1;
                 second < free_indices.size(); ++second) {
                const double denominator =
                    *standard_uncertainties[free_indices[first]] *
                    *standard_uncertainties[free_indices[second]];
                const double correlation = denominator > 0.0
                    ? std::clamp(
                          free_covariance[first][second] / denominator,
                          -1.0, 1.0)
                    : 0.0;
                response.parameter_correlations.push_back({
                    definition->parameters[free_indices[first]].id,
                    definition->parameters[free_indices[second]].id,
                    correlation,
                });
            }
        }
        if (request.joint_confidence_region.enabled) {
            ReconciliationJointConfidenceRegion region;
            region.requested_objective_increase =
                request.joint_confidence_region.objective_increase;
            region.interpretation =
                "local_asymptotic_weighted_least_squares_ellipsoid; "
                "delta_parameter^T covariance^-1 delta_parameter <= "
                "requested_objective_increase; no coverage probability "
                "is inferred";

            std::vector<std::size_t> selected_indices;
            if (request.joint_confidence_region.parameter_ids.empty()) {
                selected_indices.resize(count);
                std::iota(
                    selected_indices.begin(), selected_indices.end(), 0U);
            } else {
                for (const auto& requested_id :
                     request.joint_confidence_region.parameter_ids) {
                    const auto found = std::find_if(
                        definition->parameters.begin(),
                        definition->parameters.end(),
                        [&](const auto& parameter) {
                            return parameter.id == requested_id;
                        });
                    selected_indices.push_back(
                        static_cast<std::size_t>(std::distance(
                            definition->parameters.begin(), found)));
                }
            }

            std::vector<std::optional<std::size_t>> free_positions(count);
            for (std::size_t position = 0;
                 position < free_indices.size(); ++position) {
                free_positions[free_indices[position]] = position;
            }
            bool selected_bound_active = false;
            for (const auto index : selected_indices) {
                const auto& parameter = definition->parameters[index];
                region.parameter_ids.push_back(parameter.id);
                region.dimensions.push_back(
                    parameter.lower_bound->dimension);
                region.center_si.push_back(values[index]);
                selected_bound_active =
                    selected_bound_active || bound_active[index];
            }
            if (selected_bound_active) {
                region.message =
                    "a selected parameter is bound-active; a two-sided "
                    "local covariance ellipsoid is not valid";
            } else {
                region.covariance_si.assign(
                    selected_indices.size(),
                    std::vector<double>(selected_indices.size(), 0.0));
                for (std::size_t row = 0;
                     row < selected_indices.size(); ++row) {
                    for (std::size_t column = 0;
                         column < selected_indices.size(); ++column) {
                        region.covariance_si[row][column] =
                            free_covariance[*free_positions[
                                selected_indices[row]]]
                                           [*free_positions[
                                               selected_indices[column]]];
                    }
                }
                region.succeeded = true;
                region.message =
                    "local joint covariance ellipsoid computed from the "
                    "whitened free-parameter sensitivity";
            }
            response.joint_confidence_region = std::move(region);
        }
        } catch (const std::exception& ex) {
        response.status = OperationStatus::solver_failed;
        response.error = make_error(
            "reconciliation_uncertainty_failed", "reconciliation",
            ex.what());
        return response;
        }
    }

    if (request.profile_likelihood.enabled) {
        struct ProfilePoint {
            std::vector<double> parameter_values;
            ObjectiveEvaluation evaluation;
            bool succeeded{false};
            std::string message;
        };
        const auto optimize_profile_point =
            [&](std::size_t fixed_index,
                double fixed_value,
                std::vector<double> candidate) {
            ProfilePoint point;
            candidate[fixed_index] = fixed_value;
            try {
                auto current = evaluate(candidate, &best.case_states);
                for (int iteration = 0;
                     iteration < request.profile_likelihood
                                      .maximum_nuisance_iterations;
                     ++iteration) {
                    auto jacobian = sensitivity_at(candidate, current);
                    jacobian = correlation_whitener.whiten_rows(jacobian);
                    if (jacobian.empty()) {
                        throw std::runtime_error(
                            "could not whiten profile sensitivity");
                    }
                    std::vector<double> rhs(jacobian.size(), 0.0);
                    for (std::size_t row = 0;
                         row < jacobian.size(); ++row) {
                        rhs[row] = -current.observations[row]
                                        .normalized_residual;
                    }
                    const auto whitened_rhs =
                        correlation_whitener.solve_lower(std::move(rhs));
                    if (!whitened_rhs.success) {
                        throw std::runtime_error(
                            "could not whiten profile residuals: " +
                            whitened_rhs.message);
                    }
                    std::vector<std::size_t> nuisance_indices;
                    for (std::size_t column = 0;
                         column < candidate.size(); ++column) {
                        if (column == fixed_index) continue;
                        double gradient = 0.0;
                        double gradient_scale = 0.0;
                        for (std::size_t row = 0;
                             row < jacobian.size(); ++row) {
                            const double contribution =
                                -jacobian[row][column] *
                                whitened_rhs.x[row];
                            gradient += contribution;
                            gradient_scale += std::abs(contribution);
                        }
                        const double bound_tolerance =
                            1.0e-10 *
                            (upper[column] - lower[column]);
                        const bool at_lower =
                            candidate[column] - lower[column] <=
                            bound_tolerance;
                        const bool at_upper =
                            upper[column] - candidate[column] <=
                            bound_tolerance;
                        const double gradient_tolerance =
                            1.0e-12 *
                            std::max(1.0, gradient_scale);
                        const bool outward =
                            (at_lower &&
                             gradient > gradient_tolerance) ||
                            (at_upper &&
                             gradient < -gradient_tolerance);
                        if (!outward) nuisance_indices.push_back(column);
                    }
                    if (nuisance_indices.empty()) break;

                    thermox::Matrix nuisance_jacobian(
                        jacobian.size(),
                        std::vector<double>(
                            nuisance_indices.size(), 0.0));
                    for (std::size_t row = 0;
                         row < jacobian.size(); ++row) {
                        for (std::size_t column = 0;
                             column < nuisance_indices.size(); ++column) {
                            nuisance_jacobian[row][column] =
                                jacobian[row][nuisance_indices[column]];
                        }
                    }
                    const auto least_squares =
                        thermox::solve_dense_least_squares(
                            std::move(nuisance_jacobian),
                            whitened_rhs.x);
                    if (!least_squares.success) {
                        throw std::runtime_error(
                            "profile nuisance sensitivity is singular: " +
                            least_squares.message);
                    }
                    double maximum_scaled_step = 0.0;
                    for (std::size_t index = 0;
                         index < nuisance_indices.size(); ++index) {
                        const auto parameter = nuisance_indices[index];
                        maximum_scaled_step = std::max(
                            maximum_scaled_step,
                            std::abs(least_squares.x[index]) /
                                (upper[parameter] - lower[parameter]));
                    }
                    if (maximum_scaled_step <= settings.step_tolerance) {
                        break;
                    }
                    bool accepted = false;
                    double fraction = 1.0;
                    while (fraction >=
                           settings.minimum_line_search_fraction) {
                        auto trial = candidate;
                        for (std::size_t index = 0;
                             index < nuisance_indices.size(); ++index) {
                            const auto parameter =
                                nuisance_indices[index];
                            trial[parameter] = std::clamp(
                                candidate[parameter] + fraction *
                                    least_squares.x[index],
                                lower[parameter], upper[parameter]);
                        }
                        trial[fixed_index] = fixed_value;
                        try {
                            auto trial_evaluation = evaluate(
                                trial, &current.case_states);
                            if (trial_evaluation.value < current.value) {
                                candidate = std::move(trial);
                                current = std::move(trial_evaluation);
                                accepted = true;
                                break;
                            }
                        } catch (const std::exception&) {
                        }
                        fraction *= 0.5;
                    }
                    if (!accepted) break;
                }
                point.parameter_values = std::move(candidate);
                point.evaluation = std::move(current);
                point.succeeded = true;
                point.message = "profile nuisance optimum evaluated";
            } catch (const std::exception& ex) {
                point.message = ex.what();
            }
            return point;
        };

        const double optimum_objective = best.value;
        const auto profile_side =
            [&](std::size_t parameter_index,
                int direction,
                int& evaluation_count,
                std::string& failure_message) {
            ProfileLikelihoodEndpoint endpoint;
            const double estimate = values[parameter_index];
            const double bound = direction < 0
                ? lower[parameter_index]
                : upper[parameter_index];
            const double span = std::abs(bound - estimate);
            if (span <= 1.0e-14 *
                    (upper[parameter_index] -
                     lower[parameter_index])) {
                endpoint.value_si = estimate;
                endpoint.bound_truncated = true;
                return endpoint;
            }

            ProfilePoint below{
                values, best, true, "profile optimum"};
            double below_coordinate = estimate;
            ProfilePoint above;
            double above_coordinate = estimate;
            bool bracketed = false;
            double fraction = std::ldexp(
                1.0,
                1 - request.profile_likelihood.maximum_bracket_steps);
            for (int step = 0;
                 step < request.profile_likelihood.maximum_bracket_steps;
                 ++step) {
                fraction = std::min(1.0, fraction);
                const double coordinate =
                    estimate + static_cast<double>(direction) *
                        span * fraction;
                const int before = response.diagnostics.model_evaluations;
                auto current = optimize_profile_point(
                    parameter_index, coordinate,
                    below.parameter_values);
                evaluation_count +=
                    response.diagnostics.model_evaluations - before;
                if (!current.succeeded) {
                    failure_message = current.message;
                    return endpoint;
                }
                const double increase =
                    current.evaluation.value - optimum_objective;
                if (increase <
                    -settings.objective_relative_tolerance *
                     std::max(optimum_objective, 1.0)) {
                    failure_message =
                        "profile found an objective below the reported "
                        "reconciliation optimum";
                    return endpoint;
                }
                if (increase >=
                    request.profile_likelihood.objective_increase) {
                    above = std::move(current);
                    above_coordinate = coordinate;
                    bracketed = true;
                    break;
                }
                below = std::move(current);
                below_coordinate = coordinate;
                if (fraction >= 1.0) break;
                fraction = std::min(1.0, fraction * 2.0);
            }
            if (!bracketed) {
                endpoint.value_si = bound;
                endpoint.objective_increase =
                    below.evaluation.value - optimum_objective;
                endpoint.bound_truncated = true;
                return endpoint;
            }

            for (int step = 0;
                 step < request.profile_likelihood.maximum_bisection_steps;
                 ++step) {
                const double coordinate =
                    0.5 * (below_coordinate + above_coordinate);
                const int before = response.diagnostics.model_evaluations;
                auto middle = optimize_profile_point(
                    parameter_index, coordinate,
                    below.parameter_values);
                evaluation_count +=
                    response.diagnostics.model_evaluations - before;
                if (!middle.succeeded) {
                    failure_message = middle.message;
                    return endpoint;
                }
                const double increase =
                    middle.evaluation.value - optimum_objective;
                if (increase >=
                    request.profile_likelihood.objective_increase) {
                    above = std::move(middle);
                    above_coordinate = coordinate;
                } else {
                    below = std::move(middle);
                    below_coordinate = coordinate;
                }
            }
            endpoint.value_si = above_coordinate;
            endpoint.objective_increase =
                above.evaluation.value - optimum_objective;
            endpoint.threshold_reached = true;
            endpoint.bound_truncated =
                std::abs(above_coordinate - bound) <=
                1.0e-12 *
                    (upper[parameter_index] - lower[parameter_index]);
            return endpoint;
        };

        std::set<std::string> selected(
            request.profile_likelihood.parameter_ids.begin(),
            request.profile_likelihood.parameter_ids.end());
        for (std::size_t parameter_index = 0;
             parameter_index < definition->parameters.size();
             ++parameter_index) {
            const auto& parameter =
                definition->parameters[parameter_index];
            if (!selected.empty() && !selected.contains(parameter.id)) {
                continue;
            }
            ReconciliationProfileInterval interval;
            interval.parameter_id = parameter.id;
            interval.dimension = parameter.lower_bound->dimension;
            interval.estimate_si = values[parameter_index];
            interval.requested_objective_increase =
                request.profile_likelihood.objective_increase;
            std::string lower_failure;
            std::string upper_failure;
            interval.lower = profile_side(
                parameter_index, -1,
                interval.model_evaluations, lower_failure);
            interval.upper = profile_side(
                parameter_index, 1,
                interval.model_evaluations, upper_failure);
            interval.succeeded =
                lower_failure.empty() && upper_failure.empty();
            interval.message = interval.succeeded
                ? "profile interval evaluated"
                : (!lower_failure.empty()
                       ? "lower profile failed: " + lower_failure
                       : "upper profile failed: " + upper_failure);
            response.profile_likelihood_intervals.push_back(
                std::move(interval));
        }
        apply_values(values);
    }

    for (const auto& held_out_case : request.held_out_cases) {
        StudyCaseResult result;
        result.case_id = held_out_case.case_id;
        result.mode = "steady";
        SteadySimulationRequest simulation;
        simulation.schema_version = request.schema_version;
        simulation.model_json = response.reconciled_model_json;
        simulation.case_id = held_out_case.case_id;
        simulation.solver = settings.simulation_solver;
        simulation.artifacts = request.artifacts;
        simulation.components = request.components;
        result.steady_simulation = run_steady(simulation);
        if (!result.steady_simulation->succeeded()) {
            response.status = result.steady_simulation->status;
            response.error = make_error(
                "reconciliation_held_out_solve_failed", "held_out",
                result.steady_simulation->error.message);
            return response;
        }
        try {
            for (const auto& observation :
                 held_out_case.observations) {
                const auto& predicted = require_graph_value(
                    result.steady_simulation->graph,
                    observation.target);
                if (predicted.dimension != observation.dimension) {
                    throw std::invalid_argument(
                        "held-out observation dimension does not match "
                        "the graph result");
                }
                const double residual =
                    predicted.value_si - observation.measured_si;
                const double normalized =
                    residual / observation.sigma_si;
                result.weighted_sum_squares += normalized * normalized;
                result.observations.push_back({
                    observation.id,
                    held_out_case.case_id,
                    observation.target,
                    observation.dimension,
                    observation.measured_si,
                    predicted.value_si,
                    observation.sigma_si,
                    residual,
                    normalized,
                    std::nullopt,
                });
            }
        } catch (const std::exception& ex) {
            response.status = OperationStatus::invalid_request;
            response.error = make_error(
                "invalid_reconciliation_held_out_observation",
                "held_out", ex.what());
            return response;
        }
        response.held_out_results.push_back(std::move(result));
    }
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
    platform::ModelDocument study_document;
    try {
        study_document = platform::parse_model_document_text(
            request.model_json, impl_->runtime->impl_->units);
        const auto& calibration = require_calibration(
            study_document, request.calibration_id);
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
            const auto mode = validation_mode(
                selected_case(study_document, prediction.case_id));
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
                if (mode == "transient") {
                    if (!observation.time_si.has_value() ||
                        !std::isfinite(*observation.time_si) ||
                        *observation.time_si < 0.0) {
                        throw std::invalid_argument(
                            "transient study observations require a "
                            "non-negative finite time_si");
                    }
                } else if (observation.time_si.has_value()) {
                    throw std::invalid_argument(
                        "steady study observations must not declare "
                        "time_si");
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
        result.mode = validation_mode(
            selected_case(study_document, prediction.case_id));
        if (result.mode == "steady") {
            SteadySimulationRequest simulation;
            simulation.schema_version = request.schema_version;
            simulation.model_json =
                response.calibration.fitted_model_json;
            simulation.case_id = prediction.case_id;
            simulation.solver = request.steady_prediction_solver;
            simulation.artifacts = request.artifacts;
            simulation.components = request.components;
            result.steady_simulation = run_steady(simulation);
        } else {
            TransientSimulationRequest simulation;
            simulation.schema_version = request.schema_version;
            simulation.model_json =
                response.calibration.fitted_model_json;
            simulation.case_id = prediction.case_id;
            simulation.solver = request.transient_prediction_solver;
            simulation.solver.required_output_times.clear();
            for (const auto& observation : prediction.observations) {
                simulation.solver.required_output_times.push_back(
                    *observation.time_si);
            }
            std::sort(
                simulation.solver.required_output_times.begin(),
                simulation.solver.required_output_times.end());
            simulation.solver.required_output_times.erase(
                std::unique(
                    simulation.solver.required_output_times.begin(),
                    simulation.solver.required_output_times.end()),
                simulation.solver.required_output_times.end());
            simulation.solver.end_time =
                simulation.solver.required_output_times.back();
            simulation.artifacts = request.artifacts;
            simulation.components = request.components;
            result.transient_simulation = run_transient(simulation);
        }
        const bool prediction_succeeded =
            result.mode == "steady"
                ? result.steady_simulation->succeeded()
                : result.transient_simulation->succeeded();
        if (!prediction_succeeded) {
            response.predictions.push_back(std::move(result));
            const auto& failed = response.predictions.back();
            response.status = failed.mode == "steady"
                ? failed.steady_simulation->status
                : failed.transient_simulation->status;
            const auto& failure = failed.mode == "steady"
                ? failed.steady_simulation->error
                : failed.transient_simulation->error;
            response.error = make_error(
                "study_prediction_failed",
                "prediction",
                "prediction case '" + prediction.case_id +
                    "' failed: " +
                    failure.message);
            return response;
        }
        try {
            for (const auto& observation :
                 prediction.observations) {
                const GraphResult* graph = nullptr;
                if (result.mode == "steady") {
                    graph = &result.steady_simulation->graph;
                } else {
                    const auto sample = std::find_if(
                        result.transient_simulation->trajectory.begin(),
                        result.transient_simulation->trajectory.end(),
                        [&](const auto& candidate) {
                            return candidate.time ==
                                *observation.time_si;
                        });
                    if (sample ==
                        result.transient_simulation->trajectory.end()) {
                        throw std::runtime_error(
                            "transient study result is missing exact "
                            "observation time " +
                            std::to_string(*observation.time_si));
                    }
                    graph = &sample->graph;
                }
                const auto& predicted = require_graph_value(
                    *graph,
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
                    observation.time_si,
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
            std::optional<GraphResult> graph_before_discontinuity;
            if (sample.state_before_discontinuity.empty() !=
                sample.derivative_before_discontinuity.empty()) {
                throw std::runtime_error(
                    "transient discontinuity evidence must contain both "
                    "state and derivative limits");
            }
            if (!sample.state_before_discontinuity.empty()) {
                graph_before_discontinuity = copy_graph_result(
                    evaluator.evaluate(
                        sample.state_before_discontinuity,
                        sample.derivative_before_discontinuity));
            }
            StateSample projected{
                sample.time,
                copy_graph_result(
                    evaluator.evaluate(
                        sample.state, sample.derivative)),
                std::move(graph_before_discontinuity),
            };
            const auto snapshot_feasibility =
                audit_counterflow_thermal_feasibility(
                    projected.graph);
            append_counterflow_thermal_metrics(
                projected.graph, snapshot_feasibility);
            response.trajectory.push_back(std::move(projected));
        }
        response.events.reserve(result.events.size());
        for (const auto& event : result.events) {
            EventValue projected{
                event.name,
                event.time,
                copy_graph_result(
                    evaluator.evaluate(event.state)),
                event.terminal,
                event.transitioned,
                event.priority,
            };
            const auto event_feasibility =
                audit_counterflow_thermal_feasibility(
                    projected.graph);
            append_counterflow_thermal_metrics(
                projected.graph, event_feasibility);
            response.events.push_back(std::move(projected));
        }
        response.thermal_feasibility =
            audit_counterflow_thermal_feasibility(
                response.trajectory);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::result_failed;
        response.error = make_error(
            "result_evaluation_failed", "result", ex.what());
        return response;
    }

    response.status = OperationStatus::succeeded;
    return response;
}

SmallSignalLinearizationResponse
SimulationService::run_small_signal_linearization(
    const SmallSignalLinearizationRequest& request) const {
    SmallSignalLinearizationResponse response;
    if (!valid_schema(request.schema_version)) {
        response.error = make_error(
            "unsupported_command_schema", "request",
            "unsupported command schema_version: " +
                request.schema_version);
        return response;
    }
    if (request.model_json.empty() || request.input_variables.empty()) {
        response.error = make_error(
            "invalid_request", "request",
            "model_json and at least one input variable are required");
        return response;
    }
    const auto valid_perturbation_ladder = [](
        const std::vector<double>& values) {
        if (values.empty()) return false;
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (!std::isfinite(values[index]) || values[index] <= 0.0 ||
                (index != 0U && values[index] <= values[index - 1U])) {
                return false;
            }
        }
        return true;
    };
    const auto& settings = request.settings;
    if (settings.verify_nonlinear_response &&
        (!valid_perturbation_ladder(
             settings.nonlinear_response_relative_perturbations) ||
         !std::isfinite(
             settings.nonlinear_response_absolute_normalized_tolerance) ||
         settings.nonlinear_response_absolute_normalized_tolerance < 0.0 ||
         !std::isfinite(settings.nonlinear_response_relative_tolerance) ||
         settings.nonlinear_response_relative_tolerance < 0.0)) {
        response.error = make_error(
            "invalid_nonlinear_response_settings", "request",
            "nonlinear response perturbations must be a nonempty, "
            "strictly increasing sequence of positive finite values "
            "and tolerances must be finite and non-negative");
        return response;
    }
    if (settings.verify_nonlinear_trajectory &&
        (!std::isfinite(settings.nonlinear_trajectory_duration) ||
         settings.nonlinear_trajectory_duration <= 0.0 ||
         !valid_perturbation_ladder(
             settings.nonlinear_trajectory_relative_perturbations) ||
         settings.nonlinear_trajectory_sample_count == 0U ||
         !std::isfinite(
             settings.nonlinear_trajectory_absolute_normalized_tolerance) ||
         settings.nonlinear_trajectory_absolute_normalized_tolerance < 0.0 ||
         !std::isfinite(settings.nonlinear_trajectory_relative_tolerance) ||
         settings.nonlinear_trajectory_relative_tolerance < 0.0)) {
        response.error = make_error(
            "invalid_nonlinear_trajectory_settings", "request",
            "nonlinear trajectory duration and sample count must be "
            "positive, perturbations must be a nonempty strictly "
            "increasing sequence of positive finite values, and "
            "tolerances must be finite and non-negative");
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

    platform::ModelDocument document;
    SimulationArtifactBundle artifacts;
    platform::EngineeringArtifactRegistry engineering_artifacts;
    SolverOptions nonlinear_options;
    try {
        nonlinear_options = to_core(
            request.settings.nonlinear_solver);
        document = platform::parse_model_document_text(
            request.model_json, runtime->impl_->units);
        artifacts = resolve_artifacts(
            request.artifacts, impl_->artifact_resolver.get());
        engineering_artifacts = execution_engineering_artifacts(
            runtime->impl_->engineering_artifacts, artifacts);
    } catch (const std::exception& ex) {
        response.error = make_error(
            "invalid_request", "request", ex.what());
        return response;
    }

    platform::CompiledTransientModelGraph graph;
    try {
        graph = platform::compile_transient_model_graph(
            document, runtime->impl_->components,
            runtime->impl_->properties, engineering_artifacts,
            runtime->impl_->thermochemistry, request.case_id);
        auto provenance = solver_provenance(
            request.settings.nonlinear_solver);
        provenance.contract_version =
            "thermox.index1-small-signal/v3";
        provenance.settings.push_back({
            "time", request.settings.time});
        provenance.settings.push_back({
            "relative_perturbation",
            request.settings.relative_perturbation});
        provenance.settings.push_back({
            "minimum_perturbation",
            request.settings.minimum_perturbation});
        provenance.settings.push_back({
            "verify_jacobian",
            request.settings.verify_jacobian ? 1.0 : 0.0});
        provenance.settings.push_back({
            "verify_nonlinear_response",
            request.settings.verify_nonlinear_response ? 1.0 : 0.0});
        for (std::size_t index = 0;
             index < request.settings
                         .nonlinear_response_relative_perturbations.size();
             ++index) {
            provenance.settings.push_back({
                "nonlinear_response_relative_perturbation[" +
                    std::to_string(index) + "]",
                request.settings
                    .nonlinear_response_relative_perturbations[index]});
        }
        provenance.settings.push_back({
            "verify_nonlinear_trajectory",
            request.settings.verify_nonlinear_trajectory ? 1.0 : 0.0});
        provenance.settings.push_back({
            "nonlinear_trajectory_duration",
            request.settings.nonlinear_trajectory_duration});
        for (std::size_t index = 0;
             index < request.settings
                         .nonlinear_trajectory_relative_perturbations.size();
             ++index) {
            provenance.settings.push_back({
                "nonlinear_trajectory_relative_perturbation[" +
                    std::to_string(index) + "]",
                request.settings
                    .nonlinear_trajectory_relative_perturbations[index]});
        }
        response.metadata = execution_metadata(
            document, request.schema_version,
            graph.case_id.value_or(""), "small_signal_linearization",
            std::move(provenance), runtime->impl_->fingerprint,
            runtime->impl_->components, runtime->impl_->properties);
        response.metadata.artifacts = artifact_provenance(artifacts);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::compilation_failed;
        response.error = make_error(
            "compilation_failed", "compilation", ex.what());
        return response;
    }

    const auto initialized = make_consistent_initial_conditions(
        graph.problem, request.settings.time, nonlinear_options);
    if (!initialized.diagnostics.converged) {
        response.status = OperationStatus::solver_failed;
        response.error = make_error(
            "initialization_failed", "initialization",
            initialized.diagnostics.message);
        return response;
    }

    if (request.settings.verify_jacobian) {
        try {
            response.jacobian_verification =
                verify_dae_problem_jacobian(
                    graph.problem, request.settings.time,
                    initialized.state, initialized.derivative,
                    request.settings.jacobian_verification);
        } catch (const std::exception& ex) {
            response.status = OperationStatus::solver_failed;
            response.error = make_error(
                "jacobian_verification_exception",
                "jacobian_verification", ex.what());
            return response;
        }
        if (!response.jacobian_verification.passed) {
            response.status = OperationStatus::solver_failed;
            response.error = make_error(
                "jacobian_verification_failed",
                "jacobian_verification",
                response.jacobian_verification.message);
            return response;
        }
    }

    std::vector<DaeLinearizationInput> inputs;
    std::vector<DaeLinearizationOutput> outputs;
    std::set<std::string> unique_inputs;
    std::set<std::string> unique_outputs;
    try {
        for (const auto& name : request.input_variables) {
            if (!unique_inputs.insert(name).second) {
                throw std::invalid_argument(
                    "duplicate linearization input variable: " + name);
            }
            const auto variable = std::find(
                graph.problem.variable_names.begin(),
                graph.problem.variable_names.end(), name);
            if (variable == graph.problem.variable_names.end()) {
                throw std::invalid_argument(
                    "unknown linearization input variable: " + name);
            }
            const auto residual_name = "fixed." +
                graph.case_id.value_or("") + "." + name;
            const auto residual = std::find(
                graph.problem.residual_names.begin(),
                graph.problem.residual_names.end(), residual_name);
            if (residual == graph.problem.residual_names.end()) {
                throw std::invalid_argument(
                    "linearization input must be fixed by the active "
                    "case: " + name);
            }
            inputs.push_back({
                static_cast<std::size_t>(std::distance(
                    graph.problem.variable_names.begin(), variable)),
                static_cast<std::size_t>(std::distance(
                    graph.problem.residual_names.begin(), residual)),
                name});
        }
        for (const auto& name : request.output_variables) {
            if (!unique_outputs.insert(name).second) {
                throw std::invalid_argument(
                    "duplicate linearization output variable: " + name);
            }
            const auto variable = std::find(
                graph.problem.variable_names.begin(),
                graph.problem.variable_names.end(), name);
            if (variable == graph.problem.variable_names.end()) {
                throw std::invalid_argument(
                    "unknown linearization output variable: " + name);
            }
            outputs.push_back({
                static_cast<std::size_t>(std::distance(
                    graph.problem.variable_names.begin(), variable)),
                name});
        }
    } catch (const std::exception& ex) {
        response.error = make_error(
            "invalid_linearization_variables", "request", ex.what());
        return response;
    }

    DaeLinearizationOptions options;
    options.relative_perturbation =
        request.settings.relative_perturbation;
    options.minimum_perturbation =
        request.settings.minimum_perturbation;
    DaeLinearizationResult result;
    try {
        result = linearize_index1_dae(
            graph.problem, request.settings.time,
            initialized.state, initialized.derivative,
            inputs, outputs, options);
    } catch (const std::exception& ex) {
        response.status = OperationStatus::solver_failed;
        response.error = make_error(
            "linearization_exception", "linearization", ex.what());
        return response;
    }
    response.diagnostics = {
        result.diagnostics.success,
        result.diagnostics.residual_evaluations,
        result.diagnostics.linear_right_hand_sides,
        result.diagnostics.maximum_operating_residual,
        result.diagnostics.message};
    if (!result.diagnostics.success) {
        response.status = OperationStatus::solver_failed;
        response.error = make_error(
            "linearization_failed", "linearization",
            result.diagnostics.message);
        return response;
    }
    const auto column_perturbation = [&](
        std::size_t variable, double relative_perturbation) {
        const double operating_magnitude = std::abs(
            result.operating_state[variable]);
        const double declared_scale =
            graph.problem.variable_scales[variable];
        const double reference =
            operating_magnitude > 1.0e-12 * declared_scale
                ? operating_magnitude
                : declared_scale;
        const double delta =
            relative_perturbation * reference;
        const double positive_room =
            graph.problem.upper_bounds[variable] -
            result.operating_state[variable];
        if (delta <= positive_room) {
            return delta;
        }
        const double negative_room =
            result.operating_state[variable] -
            graph.problem.lower_bounds[variable];
        if (delta <= negative_room) {
            return -delta;
        }
        throw std::invalid_argument(
            "relative perturbation exceeds both variable bounds for " +
            graph.problem.variable_names[variable]);
    };
    if (request.settings.verify_nonlinear_response) {
        const auto& settings = request.settings;
        DaeLinearizationResponseProbeOptions probe_options;
        probe_options.absolute_normalized_tolerance =
            settings.nonlinear_response_absolute_normalized_tolerance;
        probe_options.relative_tolerance =
            settings.nonlinear_response_relative_tolerance;
        probe_options.nonlinear_solver = nonlinear_options;
        try {
            for (const double perturbation :
                 settings.nonlinear_response_relative_perturbations) {
                const auto first_probe =
                    response.nonlinear_response_probes.size();
                for (std::size_t column = 0;
                     column < result.differential_state_indices.size();
                     ++column) {
                    std::vector<double> state_delta(
                        result.differential_state_indices.size(), 0.0);
                    std::vector<double> input_delta(inputs.size(), 0.0);
                    state_delta[column] = column_perturbation(
                        result.differential_state_indices[column],
                        perturbation);
                    response.nonlinear_response_probes.push_back(
                        validate_index1_dae_linearization_response(
                            graph.problem, request.settings.time,
                            result, inputs, state_delta, input_delta,
                            probe_options));
                }
                for (std::size_t column = 0;
                     column < inputs.size(); ++column) {
                    std::vector<double> state_delta(
                        result.differential_state_indices.size(), 0.0);
                    std::vector<double> input_delta(inputs.size(), 0.0);
                    input_delta[column] = column_perturbation(
                        inputs[column].variable, perturbation);
                    response.nonlinear_response_probes.push_back(
                        validate_index1_dae_linearization_response(
                            graph.problem, request.settings.time,
                            result, inputs, state_delta, input_delta,
                            probe_options));
                }
                SmallSignalLinearityEnvelopeLevel level;
                level.relative_perturbation = perturbation;
                level.probe_count =
                    response.nonlinear_response_probes.size() -
                    first_probe;
                level.passed = true;
                for (std::size_t index = first_probe;
                     index < response.nonlinear_response_probes.size();
                     ++index) {
                    const auto& probe =
                        response.nonlinear_response_probes[index];
                    level.passed = level.passed &&
                        probe.success && probe.passed;
                    level.maximum_normalized_absolute_error = std::max(
                        level.maximum_normalized_absolute_error,
                        probe.maximum_normalized_absolute_error);
                    level.maximum_relative_error = std::max(
                        level.maximum_relative_error,
                        probe.maximum_relative_error);
                }
                response.nonlinear_response_envelope.push_back(level);
            }
        } catch (const std::exception& ex) {
            response.status = OperationStatus::solver_failed;
            response.error = make_error(
                "nonlinear_response_verification_exception",
                "nonlinear_response_verification", ex.what());
            return response;
        }
        const auto failed = std::find_if(
            response.nonlinear_response_probes.begin(),
            response.nonlinear_response_probes.end(),
            [](const auto& probe) {
                return !probe.success || !probe.passed;
            });
        if (failed != response.nonlinear_response_probes.end()) {
            response.status = OperationStatus::solver_failed;
            response.error = make_error(
                "nonlinear_response_verification_failed",
                "nonlinear_response_verification",
                failed->message);
            return response;
        }
    }
    if (request.settings.verify_nonlinear_trajectory) {
        const auto& settings = request.settings;
        DaeLinearizationTrajectoryProbeOptions trajectory_options;
        trajectory_options.duration =
            settings.nonlinear_trajectory_duration;
        trajectory_options.sample_count =
            settings.nonlinear_trajectory_sample_count;
        trajectory_options.absolute_normalized_tolerance =
            settings.nonlinear_trajectory_absolute_normalized_tolerance;
        trajectory_options.relative_tolerance =
            settings.nonlinear_trajectory_relative_tolerance;
        trajectory_options.nonlinear_integration.nonlinear_options =
            nonlinear_options;
        try {
            for (const double perturbation :
                 settings.nonlinear_trajectory_relative_perturbations) {
                const auto first_probe =
                    response.nonlinear_trajectory_probes.size();
                for (std::size_t column = 0;
                     column < result.differential_state_indices.size();
                     ++column) {
                    std::vector<double> state_delta(
                        result.differential_state_indices.size(), 0.0);
                    std::vector<double> input_delta(inputs.size(), 0.0);
                    state_delta[column] = column_perturbation(
                        result.differential_state_indices[column],
                        perturbation);
                    response.nonlinear_trajectory_probes.push_back(
                        validate_index1_dae_linearization_trajectory(
                            graph.problem, request.settings.time,
                            result, inputs, state_delta, input_delta,
                            trajectory_options));
                }
                for (std::size_t column = 0;
                     column < inputs.size(); ++column) {
                    std::vector<double> state_delta(
                        result.differential_state_indices.size(), 0.0);
                    std::vector<double> input_delta(inputs.size(), 0.0);
                    input_delta[column] = column_perturbation(
                        inputs[column].variable, perturbation);
                    response.nonlinear_trajectory_probes.push_back(
                        validate_index1_dae_linearization_trajectory(
                            graph.problem, request.settings.time,
                            result, inputs, state_delta, input_delta,
                            trajectory_options));
                }
                SmallSignalLinearityEnvelopeLevel level;
                level.relative_perturbation = perturbation;
                level.probe_count =
                    response.nonlinear_trajectory_probes.size() -
                    first_probe;
                level.passed = true;
                for (std::size_t index = first_probe;
                     index < response.nonlinear_trajectory_probes.size();
                     ++index) {
                    const auto& probe =
                        response.nonlinear_trajectory_probes[index];
                    level.passed = level.passed &&
                        probe.success && probe.passed;
                    level.maximum_normalized_absolute_error = std::max(
                        level.maximum_normalized_absolute_error,
                        probe.maximum_normalized_absolute_error);
                    level.maximum_relative_error = std::max(
                        level.maximum_relative_error,
                        probe.maximum_relative_error);
                }
                response.nonlinear_trajectory_envelope.push_back(level);
            }
        } catch (const std::exception& ex) {
            response.status = OperationStatus::solver_failed;
            response.error = make_error(
                "nonlinear_trajectory_verification_exception",
                "nonlinear_trajectory_verification", ex.what());
            return response;
        }
        const auto failed = std::find_if(
            response.nonlinear_trajectory_probes.begin(),
            response.nonlinear_trajectory_probes.end(),
            [](const auto& probe) {
                return !probe.success || !probe.passed;
            });
        if (failed != response.nonlinear_trajectory_probes.end()) {
            response.status = OperationStatus::solver_failed;
            response.error = make_error(
                "nonlinear_trajectory_verification_failed",
                "nonlinear_trajectory_verification",
                failed->message);
            return response;
        }
    }
    response.state_names = std::move(
        result.differential_state_names);
    response.input_names = std::move(result.input_names);
    response.output_names = std::move(result.output_names);
    response.A = std::move(result.A);
    response.B = std::move(result.B);
    response.C = std::move(result.C);
    response.D = std::move(result.D);
    response.status = OperationStatus::succeeded;
    return response;
}

SmallSignalModelFamilyResponse
SimulationService::run_small_signal_model_family(
    const SmallSignalModelFamilyRequest& request) const {
    SmallSignalModelFamilyResponse response;
    if (!valid_schema(request.schema_version)) {
        response.error = make_error(
            "unsupported_command_schema", "request",
            "unsupported command schema_version: " +
                request.schema_version);
        return response;
    }
    if (request.model_json.empty() || request.input_variables.empty() ||
        request.coordinate_name.empty() ||
        request.coordinate_dimension.empty() ||
        request.operating_points.size() < 2U ||
        !std::isfinite(request.maximum_interpolation_gap_si) ||
        request.maximum_interpolation_gap_si < 0.0 ||
        !std::isfinite(request.validation_absolute_tolerance) ||
        request.validation_absolute_tolerance < 0.0 ||
        !std::isfinite(request.validation_relative_tolerance) ||
        request.validation_relative_tolerance < 0.0 ||
        (!request.validation_points.empty() &&
         request.validation_absolute_tolerance == 0.0 &&
         request.validation_relative_tolerance == 0.0)) {
        response.error = make_error(
            "invalid_request", "request",
            "model_json, coordinate identity, at least one input "
            "variable, at least two operating points, and a finite "
            "non-negative interpolation and validation tolerances are "
            "required; held-out validation additionally requires at "
            "least one positive error tolerance");
        return response;
    }
    auto declarations = request.operating_points;
    std::set<std::string> unique_cases;
    for (const auto& point : declarations) {
        if (point.case_id.empty() ||
            point.regime.empty() ||
            !std::isfinite(point.coordinate_si) ||
            !unique_cases.insert(point.case_id).second) {
            response.error = make_error(
                "invalid_operating_points", "operating_points",
                "model-family case IDs must be nonempty and unique, "
                "regime labels must be nonempty, and coordinates must "
                "be finite");
            return response;
        }
    }
    auto validation_declarations = request.validation_points;
    for (const auto& point : validation_declarations) {
        if (point.case_id.empty() || point.regime.empty() ||
            !std::isfinite(point.coordinate_si) ||
            !unique_cases.insert(point.case_id).second) {
            response.error = make_error(
                "invalid_validation_points", "validation_points",
                "held-out case IDs must be nonempty and distinct from "
                "training cases, regime labels must be nonempty, and "
                "coordinates must be finite");
            return response;
        }
    }
    std::sort(
        validation_declarations.begin(),
        validation_declarations.end(),
        [](const auto& left, const auto& right) {
            return left.coordinate_si < right.coordinate_si;
        });
    std::sort(
        declarations.begin(), declarations.end(),
        [](const auto& left, const auto& right) {
            return left.coordinate_si < right.coordinate_si;
        });
    if (std::adjacent_find(
            declarations.begin(), declarations.end(),
            [](const auto& left, const auto& right) {
                return left.coordinate_si == right.coordinate_si;
            }) != declarations.end()) {
        response.error = make_error(
            "invalid_operating_points", "operating_points",
            "model-family scheduling coordinates must be unique");
        return response;
    }
    for (const double coordinate : request.evaluation_coordinates_si) {
        if (!std::isfinite(coordinate)) {
            response.error = make_error(
                "invalid_evaluation_coordinate",
                "evaluation_coordinates_si",
                "model-family evaluation coordinates must be finite");
            return response;
        }
    }

    response.coordinate_name = request.coordinate_name;
    response.coordinate_dimension = request.coordinate_dimension;
    response.maximum_interpolation_gap_si =
        request.maximum_interpolation_gap_si;
    response.validation_absolute_tolerance =
        request.validation_absolute_tolerance;
    response.validation_relative_tolerance =
        request.validation_relative_tolerance;
    response.operating_points.reserve(declarations.size());
    for (const auto& declaration : declarations) {
        SmallSignalLinearizationRequest point_request;
        point_request.schema_version = request.schema_version;
        point_request.model_json = request.model_json;
        point_request.case_id = declaration.case_id;
        point_request.input_variables = request.input_variables;
        point_request.output_variables = request.output_variables;
        point_request.settings = request.settings;
        point_request.artifacts = request.artifacts;
        point_request.components = request.components;

        auto linearization =
            run_small_signal_linearization(point_request);
        response.operating_points.push_back({
            declaration.case_id, declaration.coordinate_si,
            declaration.regime, std::move(linearization)});
        const auto& point = response.operating_points.back();
        if (!point.linearization.succeeded()) {
            response.status = point.linearization.status;
            response.error = make_error(
                "operating_point_linearization_failed",
                declaration.case_id,
                "small-signal linearization failed for operating point '" +
                    declaration.case_id + "': " +
                    point.linearization.error.message);
            return response;
        }
        if (response.operating_points.size() == 1U) {
            response.state_names = point.linearization.state_names;
            response.input_names = point.linearization.input_names;
            response.output_names = point.linearization.output_names;
            continue;
        }
        if (point.linearization.state_names != response.state_names ||
            point.linearization.input_names != response.input_names ||
            point.linearization.output_names != response.output_names) {
            response.status = OperationStatus::compilation_failed;
            response.error = make_error(
                "inconsistent_model_family", declaration.case_id,
                "operating points in one model family must expose "
                "identical ordered state, input, and output identities");
            return response;
        }
    }

    const auto interpolate_matrix = [](
        const std::vector<std::vector<double>>& lower,
        const std::vector<std::vector<double>>& upper,
        double upper_weight) {
        auto result = lower;
        for (std::size_t row = 0; row < result.size(); ++row) {
            for (std::size_t column = 0;
                 column < result[row].size(); ++column) {
                result[row][column] += upper_weight *
                    (upper[row][column] - lower[row][column]);
            }
        }
        return result;
    };
    const auto evaluate_coordinate = [&](
        double coordinate,
        SmallSignalModelFamilyEvaluation& evaluation) -> ServiceError {
        const auto upper = std::lower_bound(
            response.operating_points.begin(),
            response.operating_points.end(), coordinate,
            [](const auto& point, double value) {
                return point.coordinate_si < value;
            });
        if (upper != response.operating_points.end() &&
            upper->coordinate_si == coordinate) {
            const auto& model = upper->linearization;
            evaluation = {
                coordinate, upper->case_id, upper->case_id,
                upper->regime, 0.0, true,
                model.A, model.B, model.C, model.D};
            return {};
        }
        if (upper == response.operating_points.begin() ||
            upper == response.operating_points.end()) {
            return make_error(
                "model_family_extrapolation_forbidden",
                request.coordinate_name,
                "model-family evaluation coordinate lies outside the "
                "qualified operating-point envelope");
        }
        const auto lower = std::prev(upper);
        if (lower->regime != upper->regime) {
            return make_error(
                "model_family_regime_boundary",
                request.coordinate_name,
                "model-family interpolation across different declared "
                "regimes is forbidden");
        }
        const double gap =
            upper->coordinate_si - lower->coordinate_si;
        if (request.maximum_interpolation_gap_si <= 0.0 ||
            gap > request.maximum_interpolation_gap_si) {
            return make_error(
                "model_family_interpolation_gap",
                request.coordinate_name,
                "bracketing operating points exceed the declared "
                "maximum interpolation gap");
        }
        const double upper_weight =
            (coordinate - lower->coordinate_si) / gap;
        evaluation = {
            coordinate, lower->case_id, upper->case_id,
            lower->regime, upper_weight, false,
            interpolate_matrix(
                lower->linearization.A, upper->linearization.A,
                upper_weight),
            interpolate_matrix(
                lower->linearization.B, upper->linearization.B,
                upper_weight),
            interpolate_matrix(
                lower->linearization.C, upper->linearization.C,
                upper_weight),
            interpolate_matrix(
                lower->linearization.D, upper->linearization.D,
                upper_weight)};
        return {};
    };

    response.evaluations.reserve(
        request.evaluation_coordinates_si.size());
    for (const double coordinate : request.evaluation_coordinates_si) {
        SmallSignalModelFamilyEvaluation evaluation;
        const auto error = evaluate_coordinate(coordinate, evaluation);
        if (!error.code.empty()) {
            response.error = error;
            return response;
        }
        response.evaluations.push_back(std::move(evaluation));
    }

    const auto validate_matrix = [
        absolute_tolerance = request.validation_absolute_tolerance,
        relative_tolerance = request.validation_relative_tolerance](
        const std::vector<std::vector<double>>& predicted,
        const std::vector<std::vector<double>>& reference) {
        SmallSignalModelFamilyMatrixValidation result;
        result.passed = predicted.size() == reference.size();
        if (!result.passed) {
            result.maximum_absolute_error =
                std::numeric_limits<double>::infinity();
            result.maximum_tolerance_ratio =
                std::numeric_limits<double>::infinity();
            return result;
        }
        for (std::size_t row = 0; row < reference.size(); ++row) {
            if (predicted[row].size() != reference[row].size()) {
                result.passed = false;
                result.maximum_absolute_error =
                    std::numeric_limits<double>::infinity();
                result.maximum_tolerance_ratio =
                    std::numeric_limits<double>::infinity();
                return result;
            }
            for (std::size_t column = 0;
                 column < reference[row].size();
                 ++column) {
                const double absolute_error = std::abs(
                    predicted[row][column] - reference[row][column]);
                const double tolerance = absolute_tolerance +
                    relative_tolerance *
                        std::abs(reference[row][column]);
                result.maximum_absolute_error = std::max(
                    result.maximum_absolute_error, absolute_error);
                const double ratio = tolerance > 0.0
                    ? absolute_error / tolerance
                    : (absolute_error == 0.0
                           ? 0.0
                           : std::numeric_limits<double>::infinity());
                result.maximum_tolerance_ratio = std::max(
                    result.maximum_tolerance_ratio, ratio);
                result.passed = result.passed &&
                    absolute_error <= tolerance;
            }
        }
        return result;
    };
    response.validations.reserve(validation_declarations.size());
    for (const auto& declaration : validation_declarations) {
        SmallSignalModelFamilyEvaluation prediction;
        const auto interpolation_error = evaluate_coordinate(
            declaration.coordinate_si, prediction);
        if (!interpolation_error.code.empty()) {
            response.error = make_error(
                interpolation_error.code, declaration.case_id,
                "held-out validation point cannot be predicted: " +
                    interpolation_error.message);
            return response;
        }
        if (prediction.exact) {
            response.error = make_error(
                "invalid_validation_points", declaration.case_id,
                "held-out validation coordinate must not coincide with "
                "a training operating point");
            return response;
        }
        if (prediction.regime != declaration.regime) {
            response.error = make_error(
                "model_family_regime_boundary", declaration.case_id,
                "held-out validation regime does not match the "
                "training interval regime");
            return response;
        }

        SmallSignalLinearizationRequest point_request;
        point_request.schema_version = request.schema_version;
        point_request.model_json = request.model_json;
        point_request.case_id = declaration.case_id;
        point_request.input_variables = request.input_variables;
        point_request.output_variables = request.output_variables;
        point_request.settings = request.settings;
        point_request.artifacts = request.artifacts;
        point_request.components = request.components;
        auto reference = run_small_signal_linearization(point_request);
        if (!reference.succeeded()) {
            response.status = reference.status;
            response.error = make_error(
                "validation_point_linearization_failed",
                declaration.case_id,
                "small-signal linearization failed for held-out point '" +
                    declaration.case_id + "': " +
                    reference.error.message);
            return response;
        }
        if (reference.state_names != response.state_names ||
            reference.input_names != response.input_names ||
            reference.output_names != response.output_names) {
            response.status = OperationStatus::compilation_failed;
            response.error = make_error(
                "inconsistent_validation_point", declaration.case_id,
                "held-out point must expose the same ordered state, "
                "input, and output identities as the model family");
            return response;
        }
        SmallSignalModelFamilyValidation validation;
        validation.reference = {
            declaration.case_id, declaration.coordinate_si,
            declaration.regime, std::move(reference)};
        validation.prediction = std::move(prediction);
        validation.A = validate_matrix(
            validation.prediction.A,
            validation.reference.linearization.A);
        validation.B = validate_matrix(
            validation.prediction.B,
            validation.reference.linearization.B);
        validation.C = validate_matrix(
            validation.prediction.C,
            validation.reference.linearization.C);
        validation.D = validate_matrix(
            validation.prediction.D,
            validation.reference.linearization.D);
        validation.passed = validation.A.passed &&
            validation.B.passed && validation.C.passed &&
            validation.D.passed;
        response.validations.push_back(std::move(validation));
        if (!response.validations.back().passed) {
            response.status = OperationStatus::result_failed;
            response.error = make_error(
                "model_family_validation_failed",
                declaration.case_id,
                "interpolated local model exceeds the declared "
                "held-out matrix tolerances");
            return response;
        }
    }
    response.status = OperationStatus::succeeded;
    return response;
}

}  // namespace thermox::service
