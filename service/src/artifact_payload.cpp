#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include "artifact_payload.hpp"
#include "serialization_internal.hpp"

#include "thermox/service/projects.hpp"
#include "thermox/platform/correlation.hpp"
#include "thermox/platform/expression_component.hpp"
#include "thermox/platform/model_document.hpp"
#include "thermox/platform/performance_map.hpp"
#include "thermox/platform/regime_map.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace thermox::service::detail {
namespace {

using Tree = boost::property_tree::ptree;

platform::MapExtrapolationPolicy extrapolation(
    const std::string& value) {
    if (value == "reject") {
        return platform::MapExtrapolationPolicy::reject;
    }
    if (value == "clamp") {
        return platform::MapExtrapolationPolicy::clamp;
    }
    if (value == "linear") {
        return platform::MapExtrapolationPolicy::linear;
    }
    throw std::invalid_argument(
        "unknown performance-map extrapolation policy: " +
        value);
}

std::shared_ptr<const platform::PerformanceMap> validate_map(
    const PerformanceMapPayloadInput& input) {
    std::vector<platform::MapVariable> outputs;
    for (const auto& value : input.output_variables) {
        outputs.push_back({value.name, value.dimension});
    }
    std::vector<platform::MapCurve> curves;
    for (const auto& value : input.curves) {
        platform::MapCurve curve;
        curve.family_coordinate = value.family_coordinate;
        for (const auto& sample : value.samples) {
            curve.samples.push_back(
                {sample.coordinate, sample.outputs});
        }
        curves.push_back(std::move(curve));
    }
    return std::make_shared<const platform::PerformanceMap>(
        platform::MapVariable{
            input.primary_variable.name,
            input.primary_variable.dimension},
        platform::MapVariable{
            input.family_variable.name,
            input.family_variable.dimension},
        std::move(outputs),
        std::move(curves),
        extrapolation(input.primary_extrapolation),
        extrapolation(input.family_extrapolation));
}

void validate(const PerformanceMapArtifactInput& artifact) {
    if (artifact.map) {
        (void)validate_map(*artifact.map);
        return;
    }
    std::vector<platform::ConditionedMapLayer> layers;
    for (const auto& layer : artifact.layers) {
        layers.push_back({
            layer.condition_coordinate,
            validate_map(layer.map),
        });
    }
    (void)platform::ConditionedPerformanceMap(
        {
            artifact.condition_variable->name,
            artifact.condition_variable->dimension,
        },
        std::move(layers),
        extrapolation(artifact.condition_extrapolation));
}

template <typename Item, typename Encoder>
Tree array(const std::vector<Item>& items, Encoder&& encoder) {
    Tree result;
    for (const auto& item : items) {
        result.push_back(
            {"", std::forward<Encoder>(encoder)(item)});
    }
    return result;
}

template <typename Item, typename Decoder>
std::vector<Item> decode_array(
    const Tree& tree,
    Decoder&& decoder) {
    std::vector<Item> result;
    result.reserve(tree.size());
    for (const auto& entry : tree) {
        if (!entry.first.empty()) {
            throw std::invalid_argument(
                "performance-map array must not contain "
                "named members");
        }
        result.push_back(
            std::forward<Decoder>(decoder)(entry.second));
    }
    return result;
}

Tree read(const std::string& payload) {
    try {
        std::istringstream input(payload);
        Tree tree;
        boost::property_tree::read_json(input, tree);
        return tree;
    } catch (const std::exception& error) {
        throw std::invalid_argument(
            "invalid engineering-artifact payload: " +
            std::string(error.what()));
    }
}

std::string write(const Tree& tree) {
    std::ostringstream output;
    boost::property_tree::write_json(output, tree, false);
    return output.str();
}

MapVariableInput decode_variable(const Tree& tree) {
    return {
        tree.get<std::string>("name"),
        tree.get<std::string>("dimension"),
    };
}

Tree encode_variable(const MapVariableInput& variable) {
    Tree tree;
    tree.put("name", variable.name);
    tree.put("dimension", variable.dimension);
    return tree;
}

PerformanceMapPayloadInput decode_map(const Tree& tree) {
    PerformanceMapPayloadInput map;
    map.primary_variable =
        decode_variable(tree.get_child("primary_variable"));
    map.family_variable =
        decode_variable(tree.get_child("family_variable"));
    map.output_variables = decode_array<MapVariableInput>(
        tree.get_child("output_variables"),
        [](const Tree& item) {
            return decode_variable(item);
        });
    map.curves = decode_array<MapCurveInput>(
        tree.get_child("curves"),
        [](const Tree& encoded) {
            MapCurveInput curve;
            curve.family_coordinate =
                encoded.get<double>("family_coordinate");
            curve.samples = decode_array<MapSampleInput>(
                encoded.get_child("samples"),
                [](const Tree& sample_tree) {
                    MapSampleInput sample;
                    sample.coordinate =
                        sample_tree.get<double>("coordinate");
                    sample.outputs = decode_array<double>(
                        sample_tree.get_child("outputs"),
                        [](const Tree& scalar) {
                            return scalar.get_value<double>();
                        });
                    return sample;
                });
            return curve;
        });
    map.primary_extrapolation =
        tree.get<std::string>(
            "primary_extrapolation", "reject");
    map.family_extrapolation =
        tree.get<std::string>(
            "family_extrapolation", "reject");
    return map;
}

Tree encode_map(const PerformanceMapPayloadInput& map) {
    Tree tree;
    tree.add_child(
        "primary_variable",
        encode_variable(map.primary_variable));
    tree.add_child(
        "family_variable",
        encode_variable(map.family_variable));
    tree.add_child(
        "output_variables",
        array(
            map.output_variables,
            [](const auto& item) {
                return encode_variable(item);
            }));
    tree.add_child(
        "curves",
        array(
            map.curves,
            [](const MapCurveInput& curve) {
                Tree encoded;
                encoded.put(
                    "family_coordinate",
                    curve.family_coordinate);
                encoded.add_child(
                    "samples",
                    array(
                        curve.samples,
                        [](const MapSampleInput& sample) {
                            Tree encoded_sample;
                            encoded_sample.put(
                                "coordinate",
                                sample.coordinate);
                            encoded_sample.add_child(
                                "outputs",
                                array(
                                    sample.outputs,
                                    [](double value) {
                                        Tree scalar;
                                        scalar.put_value(value);
                                        return scalar;
                                    }));
                            return encoded_sample;
                        }));
                return encoded;
            }));
    tree.put(
        "primary_extrapolation",
        map.primary_extrapolation);
    tree.put(
        "family_extrapolation",
        map.family_extrapolation);
    return tree;
}

PerformanceMapArtifactInput decode(
    const std::string& artifact_id,
    const std::string& schema_version,
    const std::string& revision,
    const std::string& checksum,
    const Tree& tree) {
    PerformanceMapArtifactInput artifact;
    artifact.id = artifact_id;
    artifact.schema_version = schema_version;
    artifact.revision = revision;
    artifact.checksum_sha256 = checksum;
    if (schema_version ==
        platform::performance_map_artifact_schema_v1) {
        artifact.map = decode_map(tree);
    } else if (
        schema_version ==
        platform::performance_map_artifact_schema_v2) {
        artifact.condition_variable =
            decode_variable(
                tree.get_child("condition_variable"));
        artifact.layers =
            decode_array<ConditionedMapLayerInput>(
                tree.get_child("layers"),
                [](const Tree& encoded) {
                    return ConditionedMapLayerInput{
                        encoded.get<double>(
                            "condition_coordinate"),
                        decode_map(
                            encoded.get_child("map")),
                    };
                });
        artifact.condition_extrapolation =
            tree.get<std::string>(
                "condition_extrapolation", "reject");
    } else {
        throw std::invalid_argument(
            "unsupported performance-map schema: " +
            schema_version);
    }
    return artifact;
}

Tree encode(const PerformanceMapArtifactInput& artifact) {
    if (artifact.map) {
        return encode_map(*artifact.map);
    }
    Tree tree;
    tree.add_child(
        "condition_variable",
        encode_variable(*artifact.condition_variable));
    tree.add_child(
        "layers",
        array(
            artifact.layers,
            [](const ConditionedMapLayerInput& layer) {
                Tree encoded;
                encoded.put(
                    "condition_coordinate",
                    layer.condition_coordinate);
                encoded.add_child("map", encode_map(layer.map));
                return encoded;
            }));
    tree.put(
        "condition_extrapolation",
        artifact.condition_extrapolation);
    return tree;
}

CorrelationArtifactInput decode_correlation(
    const std::string& artifact_id,
    const std::string& schema_version,
    const std::string& revision,
    const std::string& checksum,
    const Tree& tree) {
    if (schema_version != platform::correlation_artifact_schema_v2) {
        throw std::invalid_argument(
            "unsupported correlation schema: " +
            schema_version);
    }
    CorrelationArtifactInput artifact;
    artifact.id = artifact_id;
    artifact.schema_version = schema_version;
    artifact.revision = revision;
    artifact.checksum_sha256 = checksum;
    artifact.inputs = decode_array<CorrelationVariableInput>(
        tree.get_child("inputs"),
        [](const Tree& encoded) {
            return CorrelationVariableInput{
                encoded.get<std::string>("name"),
                encoded.get<std::string>("dimension"),
            };
        });
    artifact.output = {
        tree.get<std::string>("output.name"),
        tree.get<std::string>("output.dimension"),
    };
    artifact.candidates = decode_array<CorrelationCandidateInput>(
        tree.get_child("candidates"),
        [](const Tree& encoded) {
                CorrelationCandidateInput candidate;
                candidate.id = encoded.get<std::string>("id");
                candidate.regime =
                    encoded.get<std::string>("regime");
                candidate.priority = encoded.get("priority", 0);
                for (const auto& [name, value] :
                     encoded.get_child("coefficients")) {
                    candidate.coefficients.emplace(
                        name, value.get_value<double>());
                }
                candidate.expression =
                    encoded.get<std::string>("expression");
                candidate.flow_regimes = decode_array<std::string>(
                    encoded.get_child("flow_regimes"),
                    [](const Tree& value) {
                        return value.get_value<std::string>();
                    });
                candidate.fallback_for_unmapped_flow_regime =
                    encoded.get<bool>(
                        "fallback_for_unmapped_flow_regime");
                if (const auto ranges =
                        encoded.get_child_optional("applicability")) {
                    candidate.applicability = decode_array<
                        CorrelationApplicabilityRangeInput>(
                        *ranges,
                        [](const Tree& range) {
                            const auto minimum =
                                range.get_optional<double>("minimum");
                            const auto maximum =
                                range.get_optional<double>("maximum");
                            return CorrelationApplicabilityRangeInput{
                                range.get<std::string>("input"),
                                minimum ? std::optional<double>{*minimum}
                                        : std::nullopt,
                                maximum ? std::optional<double>{*maximum}
                                        : std::nullopt,
                                range.get("minimum_inclusive", true),
                                range.get("maximum_inclusive", true),
                            };
                        });
                }
            return candidate;
        });
    return artifact;
}

Tree encode_correlation(
    const CorrelationArtifactInput& artifact) {
    Tree tree;
    tree.add_child(
        "inputs",
        array(
            artifact.inputs,
            [](const CorrelationVariableInput& input) {
                Tree encoded;
                encoded.put("name", input.name);
                encoded.put("dimension", input.dimension);
                return encoded;
            }));
    Tree output;
    output.put("name", artifact.output.name);
    output.put("dimension", artifact.output.dimension);
    tree.add_child("output", output);
    tree.add_child(
        "candidates",
        array(
                artifact.candidates,
                [](const CorrelationCandidateInput& candidate) {
                    Tree encoded;
                    encoded.put("id", candidate.id);
                    encoded.put("regime", candidate.regime);
                    encoded.put("priority", candidate.priority);
                    Tree coefficients;
                    for (const auto& [name, value] :
                         candidate.coefficients) {
                        coefficients.put(name, value);
                    }
                    encoded.add_child("coefficients", coefficients);
                    encoded.put("expression", candidate.expression);
                    encoded.add_child(
                        "flow_regimes",
                        array(
                            candidate.flow_regimes,
                            [](const std::string& regime) {
                                Tree value;
                                value.put_value(regime);
                                return value;
                            }));
                    encoded.put(
                        "fallback_for_unmapped_flow_regime",
                        candidate.fallback_for_unmapped_flow_regime);
                    encoded.add_child(
                        "applicability",
                        array(
                            candidate.applicability,
                            [](const auto& range) {
                                Tree value;
                                value.put("input", range.input);
                                if (range.minimum) {
                                    value.put("minimum", *range.minimum);
                                }
                                if (range.maximum) {
                                    value.put("maximum", *range.maximum);
                                }
                                value.put("minimum_inclusive",
                                          range.minimum_inclusive);
                                value.put("maximum_inclusive",
                                          range.maximum_inclusive);
                                return value;
                            }));
                    return encoded;
            }));
    return tree;
}

platform::CorrelationArtifact correlation(
    const CorrelationArtifactInput& input) {
    std::vector<platform::CorrelationVariable> variables;
    variables.reserve(input.inputs.size());
    for (const auto& variable : input.inputs) {
        variables.push_back({variable.name, variable.dimension});
    }
    std::vector<platform::CorrelationCandidate> candidates;
    candidates.reserve(input.candidates.size());
    for (const auto& candidate : input.candidates) {
        std::vector<platform::CorrelationApplicabilityRange> ranges;
        for (const auto& range : candidate.applicability) {
            ranges.push_back({
                range.input, range.minimum, range.maximum,
                range.minimum_inclusive, range.maximum_inclusive});
        }
        candidates.push_back({
            candidate.id, candidate.regime, candidate.priority,
            candidate.coefficients, candidate.expression,
            std::move(ranges), candidate.flow_regimes,
            candidate.fallback_for_unmapped_flow_regime});
    }
    return {
        input.id, input.schema_version, input.revision,
        input.checksum_sha256, std::move(variables),
        {input.output.name, input.output.dimension},
        std::move(candidates),
    };
}

RegimeMapArtifactInput decode_regime_map(
    const std::string& artifact_id,
    const std::string& schema_version,
    const std::string& revision,
    const std::string& checksum,
    const Tree& tree) {
    if (schema_version != platform::regime_map_artifact_schema_v2) {
        throw std::invalid_argument(
            "unsupported regime-map schema: " + schema_version);
    }
    RegimeMapArtifactInput artifact;
    artifact.id = artifact_id;
    artifact.schema_version = schema_version;
    artifact.revision = revision;
    artifact.checksum_sha256 = checksum;
    artifact.inputs = decode_array<RegimeMapVariableInput>(
        tree.get_child("inputs"),
        [](const Tree& encoded) {
            return RegimeMapVariableInput{
                encoded.get<std::string>("name"),
                encoded.get<std::string>("dimension"),
            };
        });
    artifact.regions = decode_array<RegimeMapRegionInput>(
        tree.get_child("regions"),
        [](const Tree& encoded) {
            RegimeMapRegionInput region;
            region.id = encoded.get<std::string>("id");
            region.regime = encoded.get<std::string>("regime");
            region.priority = encoded.get("priority", 0);
            region.branches = decode_array<RegimeMapBranchInput>(
                encoded.get_child("branches"),
                [](const Tree& branch) {
                    RegimeMapBranchInput value;
                    value.id = branch.get<std::string>("id");
                    value.priority = branch.get("priority", 0);
                    value.criteria =
                        decode_array<RegimeMapCriterionInput>(
                            branch.get_child("criteria"),
                            [](const Tree& criterion) {
                                const auto minimum = criterion
                                    .get_optional<double>("minimum");
                                const auto maximum = criterion
                                    .get_optional<double>("maximum");
                                return RegimeMapCriterionInput{
                                    criterion.get<std::string>(
                                        "expression"),
                                    criterion.get<std::string>(
                                        "dimension", "dimensionless"),
                                    minimum
                                        ? std::optional<double>{*minimum}
                                        : std::nullopt,
                                    maximum
                                        ? std::optional<double>{*maximum}
                                        : std::nullopt,
                                    criterion.get(
                                        "minimum_inclusive", true),
                                    criterion.get(
                                        "maximum_inclusive", true),
                                };
                            });
                    return value;
                });
            return region;
        });
    return artifact;
}

Tree encode_regime_map(const RegimeMapArtifactInput& artifact) {
    Tree tree;
    tree.add_child(
        "inputs",
        array(
            artifact.inputs,
            [](const RegimeMapVariableInput& input) {
                Tree encoded;
                encoded.put("name", input.name);
                encoded.put("dimension", input.dimension);
                return encoded;
            }));
    tree.add_child(
        "regions",
        array(
            artifact.regions,
            [](const RegimeMapRegionInput& region) {
                Tree encoded;
                encoded.put("id", region.id);
                encoded.put("regime", region.regime);
                encoded.put("priority", region.priority);
                encoded.add_child(
                    "branches",
                    array(
                        region.branches,
                        [](const RegimeMapBranchInput& branch) {
                            Tree value;
                            value.put("id", branch.id);
                            value.put("priority", branch.priority);
                            value.add_child(
                                "criteria",
                                array(
                                    branch.criteria,
                                    [](const RegimeMapCriterionInput&
                                           criterion) {
                                        Tree encoded_criterion;
                                        encoded_criterion.put(
                                            "expression",
                                            criterion.expression);
                                        encoded_criterion.put(
                                            "dimension",
                                            criterion.dimension);
                                        if (criterion.minimum) {
                                            encoded_criterion.put(
                                                "minimum",
                                                *criterion.minimum);
                                        }
                                        if (criterion.maximum) {
                                            encoded_criterion.put(
                                                "maximum",
                                                *criterion.maximum);
                                        }
                                        encoded_criterion.put(
                                            "minimum_inclusive",
                                            criterion.minimum_inclusive);
                                        encoded_criterion.put(
                                            "maximum_inclusive",
                                            criterion.maximum_inclusive);
                                        return encoded_criterion;
                                    }));
                            return value;
                        }));
                return encoded;
            }));
    return tree;
}

platform::RegimeMapArtifact regime_map(
    const RegimeMapArtifactInput& input) {
    std::vector<platform::RegimeMapVariable> variables;
    for (const auto& variable : input.inputs) {
        variables.push_back({variable.name, variable.dimension});
    }
    std::vector<platform::RegimeMapRegion> regions;
    for (const auto& region : input.regions) {
        std::vector<platform::RegimeMapBranch> branches;
        for (const auto& branch : region.branches) {
            std::vector<platform::RegimeMapCriterion> criteria;
            for (const auto& criterion : branch.criteria) {
                criteria.push_back({
                    criterion.expression, criterion.dimension,
                    criterion.minimum, criterion.maximum,
                    criterion.minimum_inclusive,
                    criterion.maximum_inclusive,
                });
            }
            branches.push_back({
                branch.id, branch.priority, std::move(criteria),
            });
        }
        regions.push_back({
            region.id, region.regime, region.priority,
            std::move(branches),
        });
    }
    return {
        input.id, input.schema_version, input.revision,
        input.checksum_sha256, std::move(variables),
        std::move(regions),
    };
}

ExpressionComponentInput decode_expression_component(
    const std::string& schema_version,
    const Tree& tree) {
    if (schema_version != platform::expression_component_schema_v2 &&
        schema_version != platform::expression_component_schema_v3) {
        throw std::invalid_argument(
            "unsupported expression-component schema: " +
            schema_version);
    }
    ExpressionComponentInput component;
    component.schema_version = schema_version;
    component.kind = tree.get<std::string>("kind");
    component.version = tree.get<std::string>("version");
    component.template_kind =
        tree.get<std::string>("template_kind");
    component.display_name =
        tree.get<std::string>("display_name");
    component.category = tree.get<std::string>("category");
    component.model_name =
        tree.get<std::string>("model_name");
    component.system_boundary_role =
        tree.get<std::string>("system_boundary_role", "");
    component.supports_steady =
        tree.get<bool>("supports_steady", true);
    component.supports_transient =
        tree.get<bool>("supports_transient", false);
    component.ports =
        decode_array<ExpressionComponentPortInput>(
            tree.get_child("ports"),
            [](const Tree& encoded) {
                return ExpressionComponentPortInput{
                    encoded.get<std::string>("name"),
                    encoded.get<std::string>("domain"),
                    encoded.get<std::string>("direction"),
                    encoded.get<std::size_t>(
                        "maximum_connections", 1),
                };
            });
    component.parameters =
        decode_array<ExpressionComponentParameterInput>(
            tree.get_child("parameters"),
            [](const Tree& encoded) {
                ExpressionComponentParameterInput parameter;
                parameter.name =
                    encoded.get<std::string>("name");
                parameter.dimension =
                    encoded.get<std::string>(
                        "dimension", "dimensionless");
                parameter.required =
                    encoded.get<bool>("required", true);
                if (const auto value =
                        encoded.get_optional<double>(
                            "default_value_si")) {
                    parameter.default_value_si = *value;
                }
                parameter.lower_bound =
                    encoded.get<double>(
                        "lower_bound",
                        -std::numeric_limits<double>::infinity());
                parameter.upper_bound =
                    encoded.get<double>(
                        "upper_bound",
                        std::numeric_limits<double>::infinity());
                parameter.lower_inclusive =
                    encoded.get<bool>(
                        "lower_inclusive", true);
                parameter.upper_inclusive =
                    encoded.get<bool>(
                        "upper_inclusive", true);
                return parameter;
            });
    component.equations =
        decode_array<ExpressionComponentEquationInput>(
            tree.get_child("equations"),
            [](const Tree& encoded) {
                return ExpressionComponentEquationInput{
                    encoded.get<std::string>("name"),
                    encoded.get<std::string>("expression"),
                    encoded.get<double>(
                        "residual_scale", 1.0),
                };
            });
    if (const auto variables =
            tree.get_child_optional("transient_variables")) {
        component.transient_variables =
            decode_array<ExpressionComponentTransientVariableInput>(
                *variables, [](const Tree& encoded) {
                    ExpressionComponentTransientVariableInput value;
                    value.port_name =
                        encoded.get<std::string>("port_name");
                    value.variable_name =
                        encoded.get<std::string>("variable_name");
                    value.kind =
                        encoded.get<std::string>("kind", "algebraic");
                    value.derivative_scale =
                        encoded.get<double>("derivative_scale", 1.0);
                    return value;
                });
    }
    if (const auto variables =
            tree.get_child_optional("internal_variables")) {
        component.internal_variables =
            decode_array<ExpressionComponentInternalVariableInput>(
                *variables, [](const Tree& encoded) {
                    ExpressionComponentInternalVariableInput value;
                    value.name = encoded.get<std::string>("name");
                    value.kind =
                        encoded.get<std::string>("kind", "algebraic");
                    value.initial_value_si =
                        encoded.get<double>("initial_value_si", 0.0);
                    value.state_scale =
                        encoded.get<double>("state_scale", 1.0);
                    value.initial_derivative_si_s = encoded.get<double>(
                        "initial_derivative_si_s", 0.0);
                    value.derivative_scale =
                        encoded.get<double>("derivative_scale", 1.0);
                    value.lower_bound = encoded.get<double>(
                        "lower_bound",
                        -std::numeric_limits<double>::infinity());
                    value.upper_bound = encoded.get<double>(
                        "upper_bound",
                        std::numeric_limits<double>::infinity());
                    value.dimension =
                        encoded.get<std::string>("dimension", "unspecified");
                    return value;
                });
    }
    if (const auto equations =
            tree.get_child_optional("transient_equations")) {
        component.transient_equations =
            decode_array<ExpressionComponentEquationInput>(
                *equations, [](const Tree& encoded) {
                    return ExpressionComponentEquationInput{
                        encoded.get<std::string>("name"),
                        encoded.get<std::string>("expression"),
                        encoded.get<double>("residual_scale", 1.0)};
                });
    }
    return component;
}

Tree encode_expression_component(
    const ExpressionComponentInput& component) {
    Tree tree;
    tree.put("kind", component.kind);
    tree.put("version", component.version);
    tree.put("template_kind", component.template_kind);
    tree.put("display_name", component.display_name);
    tree.put("category", component.category);
    tree.put("model_name", component.model_name);
    tree.put(
        "system_boundary_role",
        component.system_boundary_role);
    tree.put("supports_steady", component.supports_steady);
    tree.put("supports_transient", component.supports_transient);
    tree.add_child(
        "ports",
        array(
            component.ports,
            [](const auto& port) {
                Tree encoded;
                encoded.put("name", port.name);
                encoded.put("domain", port.domain);
                encoded.put("direction", port.direction);
                encoded.put(
                    "maximum_connections",
                    port.maximum_connections);
                return encoded;
            }));
    tree.add_child(
        "parameters",
        array(
            component.parameters,
            [](const auto& parameter) {
                Tree encoded;
                encoded.put("name", parameter.name);
                encoded.put(
                    "dimension", parameter.dimension);
                encoded.put("required", parameter.required);
                if (parameter.default_value_si) {
                    encoded.put(
                        "default_value_si",
                        *parameter.default_value_si);
                }
                if (std::isfinite(parameter.lower_bound)) {
                    encoded.put(
                        "lower_bound",
                        parameter.lower_bound);
                }
                if (std::isfinite(parameter.upper_bound)) {
                    encoded.put(
                        "upper_bound",
                        parameter.upper_bound);
                }
                encoded.put(
                    "lower_inclusive",
                    parameter.lower_inclusive);
                encoded.put(
                    "upper_inclusive",
                    parameter.upper_inclusive);
                return encoded;
            }));
    tree.add_child(
        "equations",
        array(
            component.equations,
            [](const auto& equation) {
                Tree encoded;
                encoded.put("name", equation.name);
                encoded.put(
                    "expression", equation.expression);
                encoded.put(
                    "residual_scale",
                    equation.residual_scale);
                return encoded;
            }));
    tree.add_child(
        "transient_variables",
        array(component.transient_variables, [](const auto& variable) {
            Tree encoded;
            encoded.put("port_name", variable.port_name);
            encoded.put("variable_name", variable.variable_name);
            encoded.put("kind", variable.kind);
            encoded.put("derivative_scale", variable.derivative_scale);
            return encoded;
        }));
    tree.add_child(
        "internal_variables",
        array(component.internal_variables, [](const auto& variable) {
            Tree encoded;
            encoded.put("name", variable.name);
            encoded.put("kind", variable.kind);
            encoded.put("initial_value_si", variable.initial_value_si);
            encoded.put("state_scale", variable.state_scale);
            encoded.put("initial_derivative_si_s",
                        variable.initial_derivative_si_s);
            encoded.put("derivative_scale", variable.derivative_scale);
            if (std::isfinite(variable.lower_bound)) {
                encoded.put("lower_bound", variable.lower_bound);
            }
            if (std::isfinite(variable.upper_bound)) {
                encoded.put("upper_bound", variable.upper_bound);
            }
            encoded.put("dimension", variable.dimension);
            return encoded;
        }));
    tree.add_child(
        "transient_equations",
        array(component.transient_equations, [](const auto& equation) {
            Tree encoded;
            encoded.put("name", equation.name);
            encoded.put("expression", equation.expression);
            encoded.put("residual_scale", equation.residual_scale);
            return encoded;
        }));
    return tree;
}

platform::ExpressionComponentDefinition definition(
    const ExpressionComponentInput& input) {
    platform::ExpressionComponentDefinition value;
    value.schema_version = input.schema_version;
    value.descriptor.kind = input.kind;
    value.descriptor.version = input.version;
    value.descriptor.template_kind = input.template_kind;
    value.descriptor.display_name = input.display_name;
    value.descriptor.category = input.category;
    value.descriptor.model_name = input.model_name;
    value.descriptor.system_boundary_role =
        input.system_boundary_role;
    value.descriptor.supports_steady = input.supports_steady;
    value.descriptor.supports_transient = input.supports_transient;
    for (const auto& port : input.ports) {
        value.descriptor.ports.push_back({
            port.name,
            port.domain,
            port.direction,
            port.maximum_connections,
        });
    }
    for (const auto& parameter : input.parameters) {
        value.descriptor.parameters.push_back({
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
        value.equations.push_back({
            equation.name,
            equation.expression,
            equation.residual_scale,
        });
    }
    const auto dae_kind = [](const std::string& kind) {
        if (kind == "differential") return DaeVariableKind::differential;
        if (kind == "algebraic") return DaeVariableKind::algebraic;
        throw std::invalid_argument(
            "expression component DAE variable kind must be algebraic or differential: " +
            kind);
    };
    for (const auto& variable : input.transient_variables) {
        value.descriptor.transient_variables.push_back({
            variable.port_name, variable.variable_name,
            dae_kind(variable.kind), variable.derivative_scale});
    }
    for (const auto& variable : input.internal_variables) {
        value.descriptor.internal_variables.push_back({
            variable.name, dae_kind(variable.kind),
            variable.initial_value_si, variable.state_scale,
            variable.initial_derivative_si_s, variable.derivative_scale,
            variable.lower_bound, variable.upper_bound,
            variable.dimension});
    }
    for (const auto& equation : input.transient_equations) {
        value.transient_equations.push_back({
            equation.name, equation.expression,
            equation.residual_scale});
    }
    return value;
}

}  // namespace

std::string canonicalize_performance_map_payload(
    const std::string& schema_version,
    const std::string& payload_json) {
    const auto artifact = decode(
        "validation",
        schema_version,
        "validation",
        std::string(64, '0'),
        read(payload_json));
    validate(artifact);
    return write(encode(artifact));
}

PerformanceMapArtifactInput performance_map_from_payload(
    const std::string& artifact_id,
    const std::string& schema_version,
    const std::string& revision,
    const std::string& checksum,
    const std::string& payload_json) {
    return decode(
        artifact_id,
        schema_version,
        revision,
        checksum,
        read(payload_json));
}

std::string canonicalize_correlation_payload(
    const std::string& schema_version,
    const std::string& payload_json) {
    const auto artifact = decode_correlation(
        "validation", schema_version, "validation",
        std::string(64, '0'), read(payload_json));
    correlation(artifact).validate();
    return write(encode_correlation(artifact));
}

CorrelationArtifactInput correlation_from_payload(
    const std::string& artifact_id,
    const std::string& schema_version,
    const std::string& revision,
    const std::string& checksum,
    const std::string& payload_json) {
    return decode_correlation(
        artifact_id, schema_version, revision, checksum,
        read(payload_json));
}

std::string correlation_payload_json(
    const CorrelationArtifactInput& artifact) {
    correlation(artifact).validate();
    return write(encode_correlation(artifact));
}

std::string canonicalize_regime_map_payload(
    const std::string& schema_version,
    const std::string& payload_json) {
    const auto artifact = decode_regime_map(
        "validation", schema_version, "validation",
        std::string(64, '0'), read(payload_json));
    regime_map(artifact).validate();
    return write(encode_regime_map(artifact));
}

RegimeMapArtifactInput regime_map_from_payload(
    const std::string& artifact_id,
    const std::string& schema_version,
    const std::string& revision,
    const std::string& checksum,
    const std::string& payload_json) {
    return decode_regime_map(
        artifact_id, schema_version, revision, checksum,
        read(payload_json));
}

std::string regime_map_payload_json(
    const RegimeMapArtifactInput& artifact) {
    regime_map(artifact).validate();
    return write(encode_regime_map(artifact));
}

std::string canonicalize_expression_component_payload(
    const std::string& schema_version,
    const std::string& payload_json) {
    const auto component =
        decode_expression_component(
            schema_version, read(payload_json));
    auto registry =
        platform::make_default_component_registry();
    platform::register_expression_component(
        registry, definition(component));
    return write(encode_expression_component(component));
}

ExpressionComponentInput expression_component_from_payload(
    const std::string& schema_version,
    const std::string& payload_json) {
    return decode_expression_component(
        schema_version, read(payload_json));
}

std::string canonicalize_assembly_template_payload(
    const std::string& schema_version,
    const std::string& payload_json) {
    if (schema_version != assembly_template_schema_v1) {
        throw std::invalid_argument(
            "unsupported assembly-template schema: " +
            schema_version);
    }
    auto document =
        platform::parse_topology_document_text(payload_json);
    if (!document.components.empty() ||
        !document.connections.empty() ||
        document.assemblies.size() != 1U) {
        throw std::invalid_argument(
            "assembly template must contain exactly one "
            "top-level assembly and no top-level components "
            "or connections");
    }
    (void)platform::flatten_model_document(document);
    return serialize_topology_document_json(document);
}

}  // namespace thermox::service::detail
