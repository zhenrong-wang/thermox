#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include "job_codec.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace thermox::postgres::detail {

namespace {

using Tree = boost::property_tree::ptree;

std::string write(const Tree& tree) {
    std::ostringstream output;
    boost::property_tree::write_json(output, tree, false);
    return output.str();
}

Tree read(const std::string& payload) {
    try {
        std::istringstream input(payload);
        Tree tree;
        boost::property_tree::read_json(input, tree);
        return tree;
    } catch (const boost::property_tree::json_parser_error& error) {
        throw std::runtime_error(
            "invalid persisted job payload: " +
            std::string(error.what()));
    }
}

template <typename Item, typename Encoder>
Tree array(
    const std::vector<Item>& items,
    Encoder&& encoder) {
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
        result.push_back(
            std::forward<Decoder>(decoder)(entry.second));
    }
    return result;
}

Tree map_variable(const service::MapVariableInput& value) {
    Tree tree;
    tree.put("name", value.name);
    tree.put("dimension", value.dimension);
    return tree;
}

service::MapVariableInput decode_map_variable(
    const Tree& tree) {
    return {
        tree.get<std::string>("name"),
        tree.get<std::string>("dimension"),
    };
}

Tree map_payload(
    const service::PerformanceMapPayloadInput& value) {
    Tree tree;
    tree.add_child(
        "primary_variable", map_variable(value.primary_variable));
    tree.add_child(
        "family_variable", map_variable(value.family_variable));
    tree.add_child(
        "output_variables",
        array(
            value.output_variables,
            [](const auto& item) {
                return map_variable(item);
            }));
    if (!value.output_constraints.empty()) {
        tree.add_child(
            "output_constraints",
            array(
                value.output_constraints,
                [](const service::MapOutputConstraintInput& constraint) {
                    Tree encoded;
                    encoded.put("output", constraint.output);
                    if (constraint.minimum) {
                        encoded.put("minimum", *constraint.minimum);
                    }
                    if (constraint.maximum) {
                        encoded.put("maximum", *constraint.maximum);
                    }
                    encoded.put(
                        "minimum_inclusive",
                        constraint.minimum_inclusive);
                    encoded.put(
                        "maximum_inclusive",
                        constraint.maximum_inclusive);
                    return encoded;
                }));
    }
    tree.add_child(
        "curves",
        array(
            value.curves,
            [](const service::MapCurveInput& curve) {
                Tree encoded;
                encoded.put(
                    "family_coordinate",
                    curve.family_coordinate);
                encoded.add_child(
                    "samples",
                    array(
                        curve.samples,
                        [](const service::MapSampleInput& sample) {
                            Tree encoded_sample;
                            encoded_sample.put(
                                "coordinate",
                                sample.coordinate);
                            encoded_sample.add_child(
                                "outputs",
                                array(
                                    sample.outputs,
                                    [](double output) {
                                        Tree scalar;
                                        scalar.put_value(output);
                                        return scalar;
                                    }));
                            return encoded_sample;
                        }));
                return encoded;
            }));
    tree.put("primary_extrapolation", value.primary_extrapolation);
    tree.put("family_extrapolation", value.family_extrapolation);
    return tree;
}

service::PerformanceMapPayloadInput decode_map_payload(
    const Tree& tree) {
    service::PerformanceMapPayloadInput value;
    value.primary_variable = decode_map_variable(
        tree.get_child("primary_variable"));
    value.family_variable = decode_map_variable(
        tree.get_child("family_variable"));
    value.output_variables =
        decode_array<service::MapVariableInput>(
            tree.get_child("output_variables"),
            [](const Tree& item) {
                return decode_map_variable(item);
            });
    if (const auto constraints =
            tree.get_child_optional("output_constraints")) {
        value.output_constraints =
            decode_array<service::MapOutputConstraintInput>(
                *constraints,
                [](const Tree& encoded) {
                    service::MapOutputConstraintInput constraint;
                    constraint.output =
                        encoded.get<std::string>("output");
                    if (const auto minimum =
                            encoded.get_optional<double>("minimum")) {
                        constraint.minimum = *minimum;
                    }
                    if (const auto maximum =
                            encoded.get_optional<double>("maximum")) {
                        constraint.maximum = *maximum;
                    }
                    constraint.minimum_inclusive = encoded.get<bool>(
                        "minimum_inclusive", true);
                    constraint.maximum_inclusive = encoded.get<bool>(
                        "maximum_inclusive", true);
                    return constraint;
                });
    }
    value.curves = decode_array<service::MapCurveInput>(
        tree.get_child("curves"),
        [](const Tree& encoded_curve) {
            service::MapCurveInput curve;
            curve.family_coordinate =
                encoded_curve.get<double>("family_coordinate");
            curve.samples =
                decode_array<service::MapSampleInput>(
                    encoded_curve.get_child("samples"),
                    [](const Tree& encoded_sample) {
                        service::MapSampleInput sample;
                        sample.coordinate =
                            encoded_sample.get<double>(
                                "coordinate");
                        sample.outputs = decode_array<double>(
                            encoded_sample.get_child("outputs"),
                            [](const Tree& scalar) {
                                return scalar.get_value<double>();
                            });
                        return sample;
                    });
            return curve;
        });
    value.primary_extrapolation =
        tree.get<std::string>("primary_extrapolation");
    value.family_extrapolation =
        tree.get<std::string>("family_extrapolation");
    return value;
}

Tree operating_envelope(
    const std::vector<service::ArtifactCoordinateConstraintInput>& value) {
    return array(
        value,
        [](const service::ArtifactCoordinateConstraintInput& constraint) {
            Tree encoded;
            encoded.put("coordinate", constraint.coordinate);
            encoded.put("dimension", constraint.dimension);
            if (constraint.minimum) {
                encoded.put("minimum", *constraint.minimum);
            }
            if (constraint.maximum) {
                encoded.put("maximum", *constraint.maximum);
            }
            encoded.put(
                "minimum_inclusive", constraint.minimum_inclusive);
            encoded.put(
                "maximum_inclusive", constraint.maximum_inclusive);
            return encoded;
        });
}

std::vector<service::ArtifactCoordinateConstraintInput>
decode_operating_envelope(const Tree& tree) {
    return decode_array<service::ArtifactCoordinateConstraintInput>(
        tree, [](const Tree& encoded) {
            service::ArtifactCoordinateConstraintInput value;
            value.coordinate = encoded.get<std::string>("coordinate");
            value.dimension = encoded.get<std::string>("dimension");
            if (const auto minimum =
                    encoded.get_optional<double>("minimum")) {
                value.minimum = *minimum;
            }
            if (const auto maximum =
                    encoded.get_optional<double>("maximum")) {
                value.maximum = *maximum;
            }
            value.minimum_inclusive =
                encoded.get("minimum_inclusive", true);
            value.maximum_inclusive =
                encoded.get("maximum_inclusive", true);
            return value;
        });
}

Tree performance_map(
    const service::PerformanceMapArtifactInput& value) {
    Tree tree;
    tree.put("id", value.id);
    tree.put("schema_version", value.schema_version);
    tree.put("revision", value.revision);
    tree.put("checksum_sha256", value.checksum_sha256);
    if (value.map) {
        tree.add_child("map", map_payload(*value.map));
    }
    if (value.condition_variable) {
        tree.add_child(
            "condition_variable",
            map_variable(*value.condition_variable));
    }
    tree.add_child(
        "layers",
        array(
            value.layers,
            [](const service::ConditionedMapLayerInput& layer) {
                Tree encoded;
                encoded.put(
                    "condition_coordinate",
                    layer.condition_coordinate);
                encoded.add_child("map", map_payload(layer.map));
                return encoded;
            }));
    tree.put(
        "condition_extrapolation",
        value.condition_extrapolation);
    if (!value.operating_envelope.empty()) {
        tree.add_child(
            "operating_envelope",
            operating_envelope(value.operating_envelope));
    }
    return tree;
}

service::PerformanceMapArtifactInput decode_performance_map(
    const Tree& tree) {
    service::PerformanceMapArtifactInput value;
    value.id = tree.get<std::string>("id");
    value.schema_version =
        tree.get<std::string>("schema_version");
    value.revision = tree.get<std::string>("revision");
    value.checksum_sha256 =
        tree.get<std::string>("checksum_sha256");
    if (const auto encoded = tree.get_child_optional("map")) {
        value.map = decode_map_payload(*encoded);
    }
    if (const auto encoded =
            tree.get_child_optional("condition_variable")) {
        value.condition_variable =
            decode_map_variable(*encoded);
    }
    value.layers =
        decode_array<service::ConditionedMapLayerInput>(
            tree.get_child("layers"),
            [](const Tree& encoded) {
                service::ConditionedMapLayerInput layer;
                layer.condition_coordinate =
                    encoded.get<double>(
                        "condition_coordinate");
                layer.map =
                    decode_map_payload(encoded.get_child("map"));
                return layer;
            });
    value.condition_extrapolation =
        tree.get<std::string>("condition_extrapolation");
    if (const auto constraints =
            tree.get_child_optional("operating_envelope")) {
        value.operating_envelope = decode_operating_envelope(*constraints);
    }
    return value;
}

Tree artifact_reference(
    const service::EngineeringArtifactReference& value) {
    Tree tree;
    tree.put("id", value.id);
    tree.put("artifact_type", value.artifact_type);
    tree.put("schema_version", value.schema_version);
    tree.put("revision", value.revision);
    tree.put("checksum_sha256", value.checksum_sha256);
    return tree;
}

service::EngineeringArtifactReference decode_artifact_reference(
    const Tree& tree) {
    return {
        tree.get<std::string>("id"),
        tree.get<std::string>("artifact_type"),
        tree.get<std::string>("schema_version"),
        tree.get<std::string>("revision"),
        tree.get<std::string>("checksum_sha256"),
    };
}

Tree correlation(
    const service::CorrelationArtifactInput& value) {
    Tree tree;
    tree.put("id", value.id);
    tree.put("schema_version", value.schema_version);
    tree.put("revision", value.revision);
    tree.put("checksum_sha256", value.checksum_sha256);
    tree.add_child(
        "inputs",
        array(
            value.inputs,
            [](const service::CorrelationVariableInput& input) {
                Tree encoded;
                encoded.put("name", input.name);
                encoded.put("dimension", input.dimension);
                return encoded;
            }));
    Tree output;
    output.put("name", value.output.name);
    output.put("dimension", value.output.dimension);
    tree.add_child("output", output);
    tree.add_child(
        "candidates",
        array(
            value.candidates,
            [](const service::CorrelationCandidateInput& candidate) {
                Tree encoded;
                encoded.put("id", candidate.id);
                encoded.put("regime", candidate.regime);
                encoded.put("priority", candidate.priority);
                Tree coefficients;
                for (const auto& [name, coefficient] :
                     candidate.coefficients) {
                    coefficients.put(name, coefficient);
                }
                encoded.add_child("coefficients", coefficients);
                encoded.put("expression", candidate.expression);
                encoded.add_child(
                    "applicability",
                    array(
                        candidate.applicability,
                        [](const auto& range) {
                            Tree item;
                            item.put("input", range.input);
                            if (range.minimum) {
                                item.put("minimum", *range.minimum);
                            }
                            if (range.maximum) {
                                item.put("maximum", *range.maximum);
                            }
                            item.put("minimum_inclusive",
                                     range.minimum_inclusive);
                            item.put("maximum_inclusive",
                                     range.maximum_inclusive);
                            return item;
                        }));
                return encoded;
            }));
    if (!value.operating_envelope.empty()) {
        tree.add_child(
            "operating_envelope",
            operating_envelope(value.operating_envelope));
    }
    return tree;
}

service::CorrelationArtifactInput decode_correlation(
    const Tree& tree) {
    service::CorrelationArtifactInput value;
    value.id = tree.get<std::string>("id");
    value.schema_version =
        tree.get<std::string>("schema_version");
    value.revision = tree.get<std::string>("revision");
    value.checksum_sha256 =
        tree.get<std::string>("checksum_sha256");
    value.inputs =
        decode_array<service::CorrelationVariableInput>(
            tree.get_child("inputs"),
            [](const Tree& encoded) {
                return service::CorrelationVariableInput{
                    encoded.get<std::string>("name"),
                    encoded.get<std::string>("dimension"),
                };
            });
    value.output = {
        tree.get<std::string>("output.name"),
        tree.get<std::string>("output.dimension"),
    };
    value.candidates = decode_array<service::CorrelationCandidateInput>(
            tree.get_child("candidates"),
            [](const Tree& encoded) {
                service::CorrelationCandidateInput candidate;
                candidate.id = encoded.get<std::string>("id");
                candidate.regime = encoded.get<std::string>("regime");
                candidate.priority = encoded.get("priority", 0);
                for (const auto& [name, coefficient] :
                     encoded.get_child("coefficients")) {
                    candidate.coefficients.emplace(
                        name, coefficient.get_value<double>());
                }
                candidate.expression =
                    encoded.get<std::string>("expression");
                candidate.applicability = decode_array<
                    service::CorrelationApplicabilityRangeInput>(
                    encoded.get_child("applicability"),
                    [](const Tree& range) {
                        const auto minimum =
                            range.get_optional<double>("minimum");
                        const auto maximum =
                            range.get_optional<double>("maximum");
                        return service::CorrelationApplicabilityRangeInput{
                            range.get<std::string>("input"),
                            minimum ? std::optional<double>{*minimum}
                                    : std::nullopt,
                            maximum ? std::optional<double>{*maximum}
                                    : std::nullopt,
                            range.get("minimum_inclusive", true),
                            range.get("maximum_inclusive", true),
                        };
                    });
                return candidate;
            });
    if (const auto envelope =
            tree.get_child_optional("operating_envelope")) {
        value.operating_envelope = decode_operating_envelope(*envelope);
    }
    return value;
}

Tree regime_map(const service::RegimeMapArtifactInput& value) {
    Tree tree;
    tree.put("id", value.id);
    tree.put("schema_version", value.schema_version);
    tree.put("revision", value.revision);
    tree.put("checksum_sha256", value.checksum_sha256);
    tree.add_child(
        "inputs",
        array(value.inputs, [](const auto& input) {
            Tree encoded;
            encoded.put("name", input.name);
            encoded.put("dimension", input.dimension);
            return encoded;
        }));
    tree.add_child(
        "regions",
        array(value.regions, [](const auto& region) {
            Tree encoded;
            encoded.put("id", region.id);
            encoded.put("regime", region.regime);
            encoded.put("priority", region.priority);
            encoded.add_child(
                "branches",
                array(region.branches, [](const auto& branch) {
                    Tree encoded_branch;
                    encoded_branch.put("id", branch.id);
                    encoded_branch.put("priority", branch.priority);
                    encoded_branch.add_child(
                        "criteria",
                        array(
                            branch.criteria,
                            [](const auto& criterion) {
                                Tree item;
                                item.put(
                                    "expression", criterion.expression);
                                item.put(
                                    "dimension", criterion.dimension);
                                if (criterion.minimum) {
                                    item.put(
                                        "minimum", *criterion.minimum);
                                }
                                if (criterion.maximum) {
                                    item.put(
                                        "maximum", *criterion.maximum);
                                }
                                item.put(
                                    "minimum_inclusive",
                                    criterion.minimum_inclusive);
                                item.put(
                                    "maximum_inclusive",
                                    criterion.maximum_inclusive);
                                return item;
                            }));
                    return encoded_branch;
                }));
            return encoded;
        }));
    if (!value.operating_envelope.empty()) {
        tree.add_child(
            "operating_envelope",
            operating_envelope(value.operating_envelope));
    }
    return tree;
}

service::RegimeMapArtifactInput decode_regime_map(
    const Tree& tree) {
    service::RegimeMapArtifactInput value;
    value.id = tree.get<std::string>("id");
    value.schema_version = tree.get<std::string>("schema_version");
    value.revision = tree.get<std::string>("revision");
    value.checksum_sha256 =
        tree.get<std::string>("checksum_sha256");
    value.inputs = decode_array<service::RegimeMapVariableInput>(
        tree.get_child("inputs"), [](const Tree& input) {
            return service::RegimeMapVariableInput{
                input.get<std::string>("name"),
                input.get<std::string>("dimension"),
            };
        });
    value.regions = decode_array<service::RegimeMapRegionInput>(
        tree.get_child("regions"), [](const Tree& region) {
            service::RegimeMapRegionInput decoded;
            decoded.id = region.get<std::string>("id");
            decoded.regime = region.get<std::string>("regime");
            decoded.priority = region.get("priority", 0);
            decoded.branches =
                decode_array<service::RegimeMapBranchInput>(
                    region.get_child("branches"),
                    [](const Tree& branch) {
                        service::RegimeMapBranchInput decoded_branch;
                        decoded_branch.id =
                            branch.get<std::string>("id");
                        decoded_branch.priority =
                            branch.get("priority", 0);
                        decoded_branch.criteria = decode_array<
                            service::RegimeMapCriterionInput>(
                                branch.get_child("criteria"),
                                [](const Tree& criterion) {
                                    const auto minimum = criterion
                                        .get_optional<double>("minimum");
                                    const auto maximum = criterion
                                        .get_optional<double>("maximum");
                                    return service::RegimeMapCriterionInput{
                                        criterion.get<std::string>(
                                            "expression"),
                                        criterion.get<std::string>(
                                            "dimension",
                                            "dimensionless"),
                                        minimum
                                            ? std::optional<double>{
                                                  *minimum}
                                            : std::nullopt,
                                        maximum
                                            ? std::optional<double>{
                                                  *maximum}
                                            : std::nullopt,
                                        criterion.get(
                                            "minimum_inclusive", true),
                                        criterion.get(
                                            "maximum_inclusive", true),
                                    };
                                });
                        return decoded_branch;
                    });
            return decoded;
        });
    if (const auto envelope =
            tree.get_child_optional("operating_envelope")) {
        value.operating_envelope = decode_operating_envelope(*envelope);
    }
    return value;
}

Tree artifact_bundle(
    const service::SimulationArtifactBundle& value) {
    Tree tree;
    tree.add_child(
        "performance_maps",
        array(
            value.performance_maps,
            [](const auto& item) {
                return performance_map(item);
            }));
    tree.add_child(
        "correlations",
        array(
            value.correlations,
            [](const auto& item) {
                return correlation(item);
            }));
    tree.add_child(
        "regime_maps",
        array(
            value.regime_maps,
            [](const auto& item) {
                return regime_map(item);
            }));
    tree.add_child(
        "references",
        array(
            value.references,
            [](const auto& item) {
                return artifact_reference(item);
            }));
    return tree;
}

service::SimulationArtifactBundle decode_artifact_bundle(
    const Tree& tree) {
    service::SimulationArtifactBundle value;
    value.performance_maps =
        decode_array<service::PerformanceMapArtifactInput>(
            tree.get_child("performance_maps"),
            [](const Tree& item) {
                return decode_performance_map(item);
            });
    value.correlations =
        decode_array<service::CorrelationArtifactInput>(
            tree.get_child("correlations"),
            [](const Tree& item) {
                return decode_correlation(item);
            });
    value.regime_maps =
        decode_array<service::RegimeMapArtifactInput>(
            tree.get_child("regime_maps"),
            [](const Tree& item) {
                return decode_regime_map(item);
            });
    value.references =
        decode_array<service::EngineeringArtifactReference>(
            tree.get_child("references"),
            [](const Tree& item) {
                return decode_artifact_reference(item);
            });
    return value;
}

Tree component_bundle(
    const service::SimulationComponentBundle& value) {
    Tree tree;
    tree.add_child(
        "expression_components",
        array(
            value.expression_components,
            [](const service::ExpressionComponentInput& component) {
                Tree encoded;
                encoded.put(
                    "schema_version", component.schema_version);
                encoded.put("kind", component.kind);
                encoded.put("version", component.version);
                encoded.put(
                    "template_kind", component.template_kind);
                encoded.put(
                    "display_name", component.display_name);
                encoded.put("category", component.category);
                encoded.put("model_name", component.model_name);
                encoded.put(
                    "system_boundary_role",
                    component.system_boundary_role);
                encoded.put(
                    "supports_steady", component.supports_steady);
                encoded.put(
                    "supports_transient", component.supports_transient);
                encoded.add_child(
                    "ports",
                    array(
                        component.ports,
                        [](const auto& port) {
                            Tree value;
                            value.put("name", port.name);
                            value.put("domain", port.domain);
                            value.put(
                                "direction", port.direction);
                            value.put(
                                "maximum_connections",
                                port.maximum_connections);
                            return value;
                        }));
                encoded.add_child(
                    "parameters",
                    array(
                        component.parameters,
                        [](const auto& parameter) {
                            Tree value;
                            value.put("name", parameter.name);
                            value.put(
                                "dimension", parameter.dimension);
                            value.put(
                                "required", parameter.required);
                            value.put(
                                "has_default",
                                parameter.default_value_si
                                    .has_value());
                            if (parameter.default_value_si) {
                                value.put(
                                    "default_value_si",
                                    *parameter.default_value_si);
                            }
                            const bool has_lower =
                                std::isfinite(
                                    parameter.lower_bound);
                            const bool has_upper =
                                std::isfinite(
                                    parameter.upper_bound);
                            value.put(
                                "has_lower_bound", has_lower);
                            value.put(
                                "has_upper_bound", has_upper);
                            if (has_lower) {
                                value.put(
                                    "lower_bound",
                                    parameter.lower_bound);
                            }
                            if (has_upper) {
                                value.put(
                                    "upper_bound",
                                    parameter.upper_bound);
                            }
                            value.put(
                                "lower_inclusive",
                                parameter.lower_inclusive);
                            value.put(
                                "upper_inclusive",
                                parameter.upper_inclusive);
                            return value;
                        }));
                encoded.add_child(
                    "equations",
                    array(
                        component.equations,
                        [](const auto& equation) {
                            Tree value;
                            value.put("name", equation.name);
                            value.put(
                                "expression",
                                equation.expression);
                            value.put(
                                "residual_scale",
                                equation.residual_scale);
                            return value;
                        }));
                encoded.add_child(
                    "transient_variables",
                    array(component.transient_variables,
                          [](const auto& variable) {
                              Tree value;
                              value.put("port_name", variable.port_name);
                              value.put("variable_name", variable.variable_name);
                              value.put("kind", variable.kind);
                              value.put("derivative_scale",
                                        variable.derivative_scale);
                              return value;
                          }));
                encoded.add_child(
                    "internal_variables",
                    array(component.internal_variables,
                          [](const auto& variable) {
                              Tree value;
                              value.put("name", variable.name);
                              value.put("kind", variable.kind);
                              value.put("initial_value_si",
                                        variable.initial_value_si);
                              value.put("state_scale", variable.state_scale);
                              value.put("initial_derivative_si_s",
                                        variable.initial_derivative_si_s);
                              value.put("derivative_scale",
                                        variable.derivative_scale);
                              const bool has_lower =
                                  std::isfinite(variable.lower_bound);
                              const bool has_upper =
                                  std::isfinite(variable.upper_bound);
                              value.put("has_lower_bound", has_lower);
                              value.put("has_upper_bound", has_upper);
                              if (has_lower) value.put(
                                  "lower_bound", variable.lower_bound);
                              if (has_upper) value.put(
                                  "upper_bound", variable.upper_bound);
                              value.put("dimension", variable.dimension);
                              return value;
                          }));
                encoded.add_child(
                    "transient_equations",
                    array(component.transient_equations,
                          [](const auto& equation) {
                              Tree value;
                              value.put("name", equation.name);
                              value.put("expression", equation.expression);
                              value.put("residual_scale",
                                        equation.residual_scale);
                              return value;
                          }));
                return encoded;
            }));
    return tree;
}

service::SimulationComponentBundle decode_component_bundle(
    const Tree& tree) {
    service::SimulationComponentBundle value;
    value.expression_components =
        decode_array<service::ExpressionComponentInput>(
            tree.get_child("expression_components"),
            [](const Tree& encoded) {
                service::ExpressionComponentInput component;
                component.schema_version =
                    encoded.get<std::string>("schema_version");
                component.kind =
                    encoded.get<std::string>("kind");
                component.version =
                    encoded.get<std::string>("version");
                component.template_kind =
                    encoded.get<std::string>("template_kind");
                component.display_name =
                    encoded.get<std::string>("display_name");
                component.category =
                    encoded.get<std::string>("category");
                component.model_name =
                    encoded.get<std::string>("model_name");
                component.system_boundary_role =
                    encoded.get<std::string>(
                        "system_boundary_role");
                component.supports_steady =
                    encoded.get<bool>("supports_steady", true);
                component.supports_transient =
                    encoded.get<bool>("supports_transient", false);
                component.ports =
                    decode_array<
                        service::ExpressionComponentPortInput>(
                        encoded.get_child("ports"),
                        [](const Tree& port) {
                            return service::
                                ExpressionComponentPortInput{
                                    port.get<std::string>("name"),
                                    port.get<std::string>("domain"),
                                    port.get<std::string>(
                                        "direction"),
                                    port.get<std::size_t>(
                                        "maximum_connections"),
                                };
                        });
                component.parameters =
                    decode_array<
                        service::
                            ExpressionComponentParameterInput>(
                        encoded.get_child("parameters"),
                        [](const Tree& parameter) {
                            service::
                                ExpressionComponentParameterInput
                                    value;
                            value.name =
                                parameter.get<std::string>(
                                    "name");
                            value.dimension =
                                parameter.get<std::string>(
                                    "dimension");
                            value.required =
                                parameter.get<bool>("required");
                            if (parameter.get<bool>(
                                    "has_default")) {
                                value.default_value_si =
                                    parameter.get<double>(
                                        "default_value_si");
                            }
                            value.lower_bound =
                                parameter.get<bool>(
                                    "has_lower_bound")
                                ? parameter.get<double>(
                                      "lower_bound")
                                : -std::numeric_limits<
                                      double>::infinity();
                            value.upper_bound =
                                parameter.get<bool>(
                                    "has_upper_bound")
                                ? parameter.get<double>(
                                      "upper_bound")
                                : std::numeric_limits<
                                      double>::infinity();
                            value.lower_inclusive =
                                parameter.get<bool>(
                                    "lower_inclusive");
                            value.upper_inclusive =
                                parameter.get<bool>(
                                    "upper_inclusive");
                            return value;
                        });
                component.equations =
                    decode_array<
                        service::ExpressionComponentEquationInput>(
                        encoded.get_child("equations"),
                        [](const Tree& equation) {
                            return service::
                                ExpressionComponentEquationInput{
                                    equation.get<std::string>(
                                        "name"),
                                    equation.get<std::string>(
                                        "expression"),
                                    equation.get<double>(
                                        "residual_scale"),
                                };
                        });
                if (const auto variables = encoded.get_child_optional(
                        "transient_variables")) {
                    component.transient_variables = decode_array<
                        service::ExpressionComponentTransientVariableInput>(
                        *variables, [](const Tree& variable) {
                            service::ExpressionComponentTransientVariableInput value;
                            value.port_name = variable.get<std::string>("port_name");
                            value.variable_name = variable.get<std::string>("variable_name");
                            value.kind = variable.get<std::string>("kind");
                            value.derivative_scale =
                                variable.get<double>("derivative_scale");
                            return value;
                        });
                }
                if (const auto variables = encoded.get_child_optional(
                        "internal_variables")) {
                    component.internal_variables = decode_array<
                        service::ExpressionComponentInternalVariableInput>(
                        *variables, [](const Tree& variable) {
                            service::ExpressionComponentInternalVariableInput value;
                            value.name = variable.get<std::string>("name");
                            value.kind = variable.get<std::string>("kind");
                            value.initial_value_si =
                                variable.get<double>("initial_value_si");
                            value.state_scale =
                                variable.get<double>("state_scale");
                            value.initial_derivative_si_s = variable.get<double>(
                                "initial_derivative_si_s");
                            value.derivative_scale =
                                variable.get<double>("derivative_scale");
                            value.lower_bound = variable.get<bool>(
                                "has_lower_bound")
                                ? variable.get<double>("lower_bound")
                                : -std::numeric_limits<double>::infinity();
                            value.upper_bound = variable.get<bool>(
                                "has_upper_bound")
                                ? variable.get<double>("upper_bound")
                                : std::numeric_limits<double>::infinity();
                            value.dimension =
                                variable.get<std::string>("dimension");
                            return value;
                        });
                }
                if (const auto equations = encoded.get_child_optional(
                        "transient_equations")) {
                    component.transient_equations = decode_array<
                        service::ExpressionComponentEquationInput>(
                        *equations, [](const Tree& equation) {
                            return service::ExpressionComponentEquationInput{
                                equation.get<std::string>("name"),
                                equation.get<std::string>("expression"),
                                equation.get<double>("residual_scale")};
                        });
                }
                return component;
            });
    return value;
}

Tree steady_settings(
    const service::SteadySolverSettings& value) {
    Tree tree;
    tree.put("max_iterations", value.max_iterations);
    tree.put(
        "residual_tolerance", value.residual_tolerance);
    tree.put("step_tolerance", value.step_tolerance);
    tree.put(
        "linear_residual_tolerance",
        value.linear_residual_tolerance);
    tree.put(
        "structural_decomposition_policy",
        service::to_string(
            value.structural_decomposition_policy));
    tree.put(
        "finite_difference_epsilon",
        value.finite_difference_epsilon);
    tree.put("min_damping", value.min_damping);
    tree.put(
        "damping_reduction", value.damping_reduction);
    tree.put(
        "sufficient_decrease", value.sufficient_decrease);
    tree.put(
        "max_line_search_steps",
        value.max_line_search_steps);
    tree.put(
        "continuation_enabled",
        value.continuation_enabled);
    tree.put(
        "continuation_initial_step",
        value.continuation_initial_step);
    tree.put(
        "continuation_minimum_step",
        value.continuation_minimum_step);
    tree.put(
        "continuation_step_growth",
        value.continuation_step_growth);
    tree.put(
        "continuation_step_reduction",
        value.continuation_step_reduction);
    tree.put(
        "continuation_maximum_stages",
        value.continuation_maximum_stages);
    return tree;
}

service::SteadySolverSettings decode_steady_settings(
    const Tree& tree) {
    service::SteadySolverSettings value;
    value.max_iterations = tree.get<int>("max_iterations");
    value.residual_tolerance =
        tree.get<double>("residual_tolerance");
    value.step_tolerance =
        tree.get<double>("step_tolerance");
    value.linear_residual_tolerance =
        tree.get<double>("linear_residual_tolerance");
    value.structural_decomposition_policy =
        service::structural_decomposition_policy_from_string(
            tree.get<std::string>(
                "structural_decomposition_policy"));
    value.finite_difference_epsilon =
        tree.get<double>("finite_difference_epsilon");
    value.min_damping = tree.get<double>("min_damping");
    value.damping_reduction =
        tree.get<double>("damping_reduction");
    value.sufficient_decrease =
        tree.get<double>("sufficient_decrease");
    value.max_line_search_steps =
        tree.get<int>("max_line_search_steps");
    value.continuation_enabled = tree.get(
        "continuation_enabled",
        value.continuation_enabled);
    value.continuation_initial_step = tree.get(
        "continuation_initial_step",
        value.continuation_initial_step);
    value.continuation_minimum_step = tree.get(
        "continuation_minimum_step",
        value.continuation_minimum_step);
    value.continuation_step_growth = tree.get(
        "continuation_step_growth",
        value.continuation_step_growth);
    value.continuation_step_reduction = tree.get(
        "continuation_step_reduction",
        value.continuation_step_reduction);
    value.continuation_maximum_stages = tree.get(
        "continuation_maximum_stages",
        value.continuation_maximum_stages);
    return value;
}

Tree transient_settings(
    const service::TransientSolverSettings& value) {
    Tree tree;
    tree.put("start_time", value.start_time);
    tree.put("end_time", value.end_time);
    tree.put("initial_step", value.initial_step);
    tree.put("min_step", value.min_step);
    tree.put("max_step", value.max_step);
    tree.put(
        "absolute_tolerance", value.absolute_tolerance);
    tree.put(
        "relative_tolerance", value.relative_tolerance);
    tree.put("max_steps", value.max_steps);
    tree.put(
        "max_consecutive_rejections",
        value.max_consecutive_rejections);
    tree.put("maximum_order", value.maximum_order);
    tree.put(
        "compute_consistent_initial_conditions",
        value.compute_consistent_initial_conditions);
    tree.add_child(
        "nonlinear_solver",
        steady_settings(value.nonlinear_solver));
    return tree;
}

service::TransientSolverSettings decode_transient_settings(
    const Tree& tree) {
    service::TransientSolverSettings value;
    value.start_time = tree.get<double>("start_time");
    value.end_time = tree.get<double>("end_time");
    value.initial_step = tree.get<double>("initial_step");
    value.min_step = tree.get<double>("min_step");
    value.max_step = tree.get<double>("max_step");
    value.absolute_tolerance =
        tree.get<double>("absolute_tolerance");
    value.relative_tolerance =
        tree.get<double>("relative_tolerance");
    value.max_steps = tree.get<int>("max_steps");
    value.max_consecutive_rejections =
        tree.get<int>("max_consecutive_rejections");
    value.maximum_order = tree.get<int>("maximum_order");
    value.compute_consistent_initial_conditions =
        tree.get<bool>(
            "compute_consistent_initial_conditions");
    value.nonlinear_solver = decode_steady_settings(
        tree.get_child("nonlinear_solver"));
    return value;
}

Tree calibration_settings(
    const service::CalibrationSolverSettings& value) {
    Tree tree;
    tree.put("max_iterations", value.max_iterations);
    tree.put("initial_step_fraction", value.initial_step_fraction);
    tree.put("minimum_step_fraction", value.minimum_step_fraction);
    tree.put("step_reduction", value.step_reduction);
    tree.put("minimum_continuation_fraction",
             value.minimum_continuation_fraction);
    tree.put("continuation_growth", value.continuation_growth);
    tree.add_child(
        "simulation_solver", steady_settings(value.simulation_solver));
    return tree;
}

service::CalibrationSolverSettings decode_calibration_settings(
    const Tree& tree) {
    service::CalibrationSolverSettings value;
    value.max_iterations = tree.get<int>("max_iterations");
    value.initial_step_fraction =
        tree.get<double>("initial_step_fraction");
    value.minimum_step_fraction =
        tree.get<double>("minimum_step_fraction");
    value.step_reduction = tree.get<double>("step_reduction");
    value.minimum_continuation_fraction =
        tree.get<double>("minimum_continuation_fraction");
    value.continuation_growth =
        tree.get<double>("continuation_growth");
    value.simulation_solver = decode_steady_settings(
        tree.get_child("simulation_solver"));
    return value;
}

Tree reconciliation_settings(
    const service::ReconciliationSolverSettings& value) {
    Tree tree;
    tree.put("max_iterations", value.max_iterations);
    tree.put(
        "finite_difference_fraction",
        value.finite_difference_fraction);
    tree.put("constraint_tolerance", value.constraint_tolerance);
    tree.put("step_tolerance", value.step_tolerance);
    tree.put(
        "objective_relative_tolerance",
        value.objective_relative_tolerance);
    tree.put(
        "minimum_line_search_fraction",
        value.minimum_line_search_fraction);
    tree.add_child(
        "simulation_solver", steady_settings(value.simulation_solver));
    return tree;
}

service::ReconciliationSolverSettings
decode_reconciliation_settings(const Tree& tree) {
    service::ReconciliationSolverSettings value;
    value.max_iterations = tree.get<int>("max_iterations");
    value.finite_difference_fraction =
        tree.get<double>("finite_difference_fraction");
    value.constraint_tolerance =
        tree.get<double>("constraint_tolerance");
    value.step_tolerance = tree.get<double>("step_tolerance");
    value.objective_relative_tolerance =
        tree.get<double>("objective_relative_tolerance");
    value.minimum_line_search_fraction =
        tree.get<double>("minimum_line_search_fraction");
    value.simulation_solver = decode_steady_settings(
        tree.get_child("simulation_solver"));
    return value;
}

Tree profile_likelihood_settings(
    const service::ProfileLikelihoodSettings& value) {
    Tree tree;
    tree.put("enabled", value.enabled);
    tree.put("objective_increase", value.objective_increase);
    tree.put("maximum_bracket_steps", value.maximum_bracket_steps);
    tree.put("maximum_bisection_steps", value.maximum_bisection_steps);
    tree.put(
        "maximum_nuisance_iterations",
        value.maximum_nuisance_iterations);
    tree.add_child(
        "parameter_ids",
        array(value.parameter_ids, [](const std::string& id) {
            Tree item;
            item.put_value(id);
            return item;
        }));
    return tree;
}

service::ProfileLikelihoodSettings
decode_profile_likelihood_settings(const Tree& tree) {
    service::ProfileLikelihoodSettings value;
    value.enabled = tree.get<bool>("enabled");
    value.objective_increase = tree.get<double>("objective_increase");
    value.maximum_bracket_steps =
        tree.get<int>("maximum_bracket_steps");
    value.maximum_bisection_steps =
        tree.get<int>("maximum_bisection_steps");
    value.maximum_nuisance_iterations =
        tree.get<int>("maximum_nuisance_iterations");
    value.parameter_ids = decode_array<std::string>(
        tree.get_child("parameter_ids"),
        [](const Tree& item) {
            return item.get_value<std::string>();
        });
    return value;
}

Tree joint_confidence_region_settings(
    const service::JointConfidenceRegionSettings& value) {
    Tree tree;
    tree.put("enabled", value.enabled);
    tree.put("objective_increase", value.objective_increase);
    tree.add_child(
        "parameter_ids",
        array(value.parameter_ids, [](const std::string& id) {
            Tree item;
            item.put_value(id);
            return item;
        }));
    return tree;
}

service::JointConfidenceRegionSettings
decode_joint_confidence_region_settings(const Tree& tree) {
    service::JointConfidenceRegionSettings value;
    value.enabled = tree.get<bool>("enabled");
    value.objective_increase = tree.get<double>("objective_increase");
    value.parameter_ids = decode_array<std::string>(
        tree.get_child("parameter_ids"),
        [](const Tree& item) {
            return item.get_value<std::string>();
        });
    return value;
}

service::SimulationJobMode decode_mode(
    const std::string& mode) {
    if (mode == "steady") {
        return service::SimulationJobMode::steady;
    }
    if (mode == "transient") {
        return service::SimulationJobMode::transient;
    }
    if (mode == "calibration") {
        return service::SimulationJobMode::calibration;
    }
    if (mode == "reconciliation") {
        return service::SimulationJobMode::reconciliation;
    }
    throw std::runtime_error(
        "invalid persisted simulation mode: " + mode);
}

Tree solver_provenance(
    const service::SolverProvenance& value) {
    Tree tree;
    tree.put("contract_version", value.contract_version);
    tree.add_child(
        "settings",
        array(
            value.settings,
            [](const service::SolverSetting& setting) {
                Tree encoded;
                encoded.put("name", setting.name);
                encoded.put("value", setting.value);
                return encoded;
            }));
    return tree;
}

service::SolverProvenance decode_solver_provenance(
    const Tree& tree) {
    service::SolverProvenance value;
    value.contract_version =
        tree.get<std::string>("contract_version");
    value.settings = decode_array<service::SolverSetting>(
        tree.get_child("settings"),
        [](const Tree& encoded) {
            return service::SolverSetting{
                encoded.get<std::string>("name"),
                encoded.get<double>("value"),
            };
        });
    return value;
}

Tree result_projection(
    const service::ResultProjection& value) {
    Tree tree;
    tree.put("id", value.id);
    tree.put("scope", service::to_string(value.scope));
    tree.put("component_id", value.component_id);
    tree.put("port_name", value.port_name);
    tree.put("value_name", value.value_name);
    tree.put("dimension", value.dimension);
    tree.put(
        "aggregation", service::to_string(value.aggregation));
    return tree;
}

service::ResultProjection decode_result_projection(
    const Tree& tree) {
    service::ResultProjection value;
    value.id = tree.get<std::string>("id");
    value.scope = service::result_value_scope_from_string(
        tree.get<std::string>("scope"));
    value.component_id =
        tree.get<std::string>("component_id", "");
    value.port_name =
        tree.get<std::string>("port_name", "");
    value.value_name =
        tree.get<std::string>("value_name");
    value.dimension =
        tree.get<std::string>("dimension");
    value.aggregation =
        service::result_aggregation_from_string(
            tree.get<std::string>("aggregation"));
    return value;
}

Tree acceptance_criterion(
    const service::EngineeringAcceptanceCriterion& value) {
    Tree tree;
    tree.put("id", value.id);
    tree.put("projection_id", value.projection_id);
    tree.put("dimension", value.dimension);
    if (value.lower_bound_si) {
        tree.put("lower_bound_si", *value.lower_bound_si);
    }
    if (value.upper_bound_si) {
        tree.put("upper_bound_si", *value.upper_bound_si);
    }
    tree.put("lower_inclusive", value.lower_inclusive);
    tree.put("upper_inclusive", value.upper_inclusive);
    return tree;
}

service::EngineeringAcceptanceCriterion
decode_acceptance_criterion(const Tree& tree) {
    service::EngineeringAcceptanceCriterion value;
    value.id = tree.get<std::string>("id");
    value.projection_id =
        tree.get<std::string>("projection_id");
    value.dimension = tree.get<std::string>("dimension");
    if (const auto lower =
            tree.get_optional<double>("lower_bound_si")) {
        value.lower_bound_si = *lower;
    }
    if (const auto upper =
            tree.get_optional<double>("upper_bound_si")) {
        value.upper_bound_si = *upper;
    }
    value.lower_inclusive = tree.get("lower_inclusive", true);
    value.upper_inclusive = tree.get("upper_inclusive", true);
    return value;
}

}  // namespace

std::string encode_request(
    const service::SimulationJobRequest& request) {
    Tree tree;
    tree.put("schema_version", request.schema_version);
    tree.put("identity.user_id", request.identity.user_id);
    tree.put("identity.team_id", request.identity.team_id);
    tree.put("identity.request_id", request.identity.request_id);
    tree.put("idempotency_key", request.idempotency_key);
    tree.put("mode", service::to_string(request.mode));
    tree.put("model_json", request.model_json);
    tree.put("case_id", request.case_id);
    tree.put("calibration_id", request.calibration_id);
    tree.put("reconciliation_id", request.reconciliation_id);
    if (request.source_revisions) {
        Tree source;
        source.put(
            "project_id",
            request.source_revisions->project_id);
        source.put(
            "run_configuration_revision_id",
            request.source_revisions
                ->run_configuration_revision_id);
        source.put(
            "run_configuration_checksum",
            request.source_revisions
                ->run_configuration_checksum);
        source.put(
            "study_revision_id",
            request.source_revisions->study_revision_id);
        source.put(
            "study_checksum",
            request.source_revisions->study_checksum);
        source.put(
            "calibration_revision_id",
            request.source_revisions->calibration_revision_id);
        source.put(
            "calibration_checksum",
            request.source_revisions->calibration_checksum);
        source.put(
            "reconciliation_revision_id",
            request.source_revisions->reconciliation_revision_id);
        source.put(
            "reconciliation_checksum",
            request.source_revisions->reconciliation_checksum);
        source.put(
            "model_revision_id",
            request.source_revisions->model_revision_id);
        source.put(
            "model_checksum",
            request.source_revisions->model_checksum);
        source.put(
            "case_revision_id",
            request.source_revisions->case_revision_id);
        source.put(
            "case_checksum",
            request.source_revisions->case_checksum);
        tree.add_child("source_revisions", source);
    }
    tree.add_child(
        "steady_solver", steady_settings(request.steady_solver));
    tree.add_child(
        "transient_solver",
        transient_settings(request.transient_solver));
    tree.add_child(
        "calibration_solver",
        calibration_settings(request.calibration_solver));
    tree.add_child(
        "calibration_predictions",
        array(
            request.calibration_predictions,
            [](const service::StudyPredictionCase& prediction) {
                Tree encoded;
                encoded.put("case_id", prediction.case_id);
                encoded.add_child(
                    "observations",
                    array(
                        prediction.observations,
                        [](const service::StudyObservation& observation) {
                            Tree item;
                            item.put("id", observation.id);
                            item.put("target", observation.target);
                            item.put("dimension", observation.dimension);
                            item.put("measured_si", observation.measured_si);
                            item.put("sigma_si", observation.sigma_si);
                            return item;
                        }));
                return encoded;
            }));
    tree.put(
        "reconciliation_mode",
        service::to_string(request.reconciliation_mode));
    tree.add_child(
        "reconciliation_solver",
        reconciliation_settings(request.reconciliation_solver));
    tree.add_child(
        "reconciliation_profile_likelihood",
        profile_likelihood_settings(
            request.reconciliation_profile_likelihood));
    tree.add_child(
        "reconciliation_joint_confidence_region",
        joint_confidence_region_settings(
            request.reconciliation_joint_confidence_region));
    tree.add_child(
        "reconciliation_held_out_cases",
        array(
            request.reconciliation_held_out_cases,
            [](const service::StudyPredictionCase& prediction) {
                Tree encoded;
                encoded.put("case_id", prediction.case_id);
                encoded.add_child(
                    "observations",
                    array(
                        prediction.observations,
                        [](const service::StudyObservation& observation) {
                            Tree item;
                            item.put("id", observation.id);
                            item.put("target", observation.target);
                            item.put("dimension", observation.dimension);
                            item.put("measured_si", observation.measured_si);
                            item.put("sigma_si", observation.sigma_si);
                            return item;
                        }));
                return encoded;
            }));
    tree.add_child("artifacts", artifact_bundle(request.artifacts));
    tree.add_child(
        "components", component_bundle(request.components));
    tree.add_child(
        "result_projections",
        array(
            request.result_projections,
            [](const service::ResultProjection& projection) {
                return result_projection(projection);
            }));
    tree.add_child(
        "acceptance_criteria",
        array(
            request.acceptance_criteria,
            [](const service::EngineeringAcceptanceCriterion& criterion) {
                return acceptance_criterion(criterion);
            }));
    return write(tree);
}

service::SimulationJobRequest decode_request(
    const std::string& payload) {
    const Tree tree = read(payload);
    service::SimulationJobRequest request;
    request.schema_version =
        tree.get<std::string>("schema_version");
    request.identity.user_id =
        tree.get<std::string>("identity.user_id");
    request.identity.team_id =
        tree.get<std::string>("identity.team_id");
    request.identity.request_id =
        tree.get<std::string>("identity.request_id");
    request.idempotency_key =
        tree.get<std::string>("idempotency_key");
    request.mode =
        decode_mode(tree.get<std::string>("mode"));
    request.model_json = tree.get<std::string>("model_json");
    request.case_id = tree.get<std::string>("case_id");
    request.calibration_id =
        tree.get<std::string>("calibration_id");
    request.reconciliation_id =
        tree.get<std::string>("reconciliation_id");
    if (const auto source =
            tree.get_child_optional("source_revisions")) {
        request.source_revisions =
            service::RevisionProvenance{
                source->get<std::string>("project_id"),
                source->get<std::string>(
                    "model_revision_id"),
                source->get<std::string>("model_checksum"),
                source->get<std::string>(
                    "case_revision_id"),
                source->get<std::string>("case_checksum"),
                source->get<std::string>(
                    "run_configuration_revision_id", ""),
                source->get<std::string>(
                    "run_configuration_checksum", ""),
                source->get<std::string>(
                    "study_revision_id", ""),
                source->get<std::string>("study_checksum", ""),
                source->get<std::string>(
                    "calibration_revision_id", ""),
                source->get<std::string>(
                    "calibration_checksum", ""),
                source->get<std::string>(
                    "reconciliation_revision_id", ""),
                source->get<std::string>(
                    "reconciliation_checksum", ""),
            };
    }
    request.steady_solver = decode_steady_settings(
        tree.get_child("steady_solver"));
    request.transient_solver = decode_transient_settings(
        tree.get_child("transient_solver"));
    request.calibration_solver = decode_calibration_settings(
        tree.get_child("calibration_solver"));
    request.calibration_predictions =
        decode_array<service::StudyPredictionCase>(
            tree.get_child("calibration_predictions"),
            [](const Tree& encoded) {
                service::StudyPredictionCase prediction;
                prediction.case_id =
                    encoded.get<std::string>("case_id");
                prediction.observations =
                    decode_array<service::StudyObservation>(
                        encoded.get_child("observations"),
                        [](const Tree& item) {
                            return service::StudyObservation{
                                item.get<std::string>("id"),
                                item.get<std::string>("target"),
                                item.get<std::string>("dimension"),
                                item.get<double>("measured_si"),
                                item.get<double>("sigma_si"),
                            };
                        });
                return prediction;
            });
    const auto reconciliation_mode =
        tree.get<std::string>("reconciliation_mode");
    if (reconciliation_mode == "hard_equalities") {
        request.reconciliation_mode =
            service::ReconciliationMode::hard_equalities;
    } else if (reconciliation_mode == "weighted_measurements") {
        request.reconciliation_mode =
            service::ReconciliationMode::weighted_measurements;
    } else {
        throw std::runtime_error(
            "invalid persisted reconciliation mode: " +
            reconciliation_mode);
    }
    request.reconciliation_solver =
        decode_reconciliation_settings(
            tree.get_child("reconciliation_solver"));
    request.reconciliation_profile_likelihood =
        decode_profile_likelihood_settings(
            tree.get_child("reconciliation_profile_likelihood"));
    if (const auto region = tree.get_child_optional(
            "reconciliation_joint_confidence_region")) {
        request.reconciliation_joint_confidence_region =
            decode_joint_confidence_region_settings(*region);
    }
    request.reconciliation_held_out_cases =
        decode_array<service::StudyPredictionCase>(
            tree.get_child("reconciliation_held_out_cases"),
            [](const Tree& encoded) {
                service::StudyPredictionCase prediction;
                prediction.case_id =
                    encoded.get<std::string>("case_id");
                prediction.observations =
                    decode_array<service::StudyObservation>(
                        encoded.get_child("observations"),
                        [](const Tree& item) {
                            return service::StudyObservation{
                                item.get<std::string>("id"),
                                item.get<std::string>("target"),
                                item.get<std::string>("dimension"),
                                item.get<double>("measured_si"),
                                item.get<double>("sigma_si"),
                            };
                        });
                return prediction;
            });
    request.artifacts =
        decode_artifact_bundle(tree.get_child("artifacts"));
    request.components =
        decode_component_bundle(tree.get_child("components"));
    request.result_projections =
        decode_array<service::ResultProjection>(
            tree.get_child("result_projections"),
            [](const Tree& encoded) {
                return decode_result_projection(encoded);
            });
    service::validate_result_projections(
        request.result_projections);
    if (const auto encoded =
            tree.get_child_optional("acceptance_criteria")) {
        request.acceptance_criteria =
            decode_array<service::EngineeringAcceptanceCriterion>(
                *encoded,
                [](const Tree& item) {
                    return decode_acceptance_criterion(item);
                });
    }
    service::validate_engineering_acceptance_criteria(
        request.acceptance_criteria,
        request.result_projections);
    return request;
}

std::string encode_execution(
    const service::ExecutionMetadata& execution) {
    Tree tree;
    tree.put(
        "result_schema_version",
        execution.result_schema_version);
    tree.put(
        "command_schema_version",
        execution.command_schema_version);
    tree.put("platform_version", execution.platform_version);
    tree.put("operation", execution.operation);
    tree.add_child("solver", solver_provenance(execution.solver));
    tree.put(
        "catalog_fingerprint",
        execution.catalog_fingerprint);
    tree.put(
        "model.schema_version",
        execution.model.schema_version);
    tree.put("model.model_id", execution.model.model_id);
    tree.put(
        "model.model_revision",
        execution.model.model_revision);
    tree.put("model.case_id", execution.model.case_id);
    if (execution.source_revisions) {
        Tree source;
        source.put(
            "project_id",
            execution.source_revisions->project_id);
        source.put(
            "run_configuration_revision_id",
            execution.source_revisions
                ->run_configuration_revision_id);
        source.put(
            "run_configuration_checksum",
            execution.source_revisions
                ->run_configuration_checksum);
        source.put(
            "study_revision_id",
            execution.source_revisions->study_revision_id);
        source.put(
            "study_checksum",
            execution.source_revisions->study_checksum);
        source.put(
            "calibration_revision_id",
            execution.source_revisions->calibration_revision_id);
        source.put(
            "calibration_checksum",
            execution.source_revisions->calibration_checksum);
        source.put(
            "reconciliation_revision_id",
            execution.source_revisions->reconciliation_revision_id);
        source.put(
            "reconciliation_checksum",
            execution.source_revisions->reconciliation_checksum);
        source.put(
            "model_revision_id",
            execution.source_revisions->model_revision_id);
        source.put(
            "model_checksum",
            execution.source_revisions->model_checksum);
        source.put(
            "case_revision_id",
            execution.source_revisions->case_revision_id);
        source.put(
            "case_checksum",
            execution.source_revisions->case_checksum);
        tree.add_child("source_revisions", source);
    }
    tree.add_child(
        "components",
        array(
            execution.components,
            [](const service::ComponentProvenance& value) {
                Tree encoded;
                encoded.put(
                    "component_id", value.component_id);
                encoded.put("kind", value.kind);
                encoded.put(
                    "requested_version",
                    value.requested_version);
                encoded.put(
                    "resolved_version",
                    value.resolved_version);
                return encoded;
            }));
    tree.add_child(
        "media",
        array(
            execution.media,
            [](const service::MediumProvenance& value) {
                Tree encoded;
                encoded.put("medium_id", value.medium_id);
                encoded.put("backend", value.backend);
                encoded.put("substance", value.substance);
                encoded.put("package", value.package);
                encoded.put(
                    "requested_package_version",
                    value.requested_package_version);
                encoded.put(
                    "resolved_package_version",
                    value.resolved_package_version);
                return encoded;
            }));
    tree.add_child(
        "artifacts",
        array(
            execution.artifacts,
            [](const service::ArtifactProvenance& value) {
                Tree encoded;
                encoded.put("id", value.id);
                encoded.put(
                    "artifact_type", value.artifact_type);
                encoded.put(
                    "schema_version", value.schema_version);
                encoded.put("revision", value.revision);
                encoded.put(
                    "checksum_sha256",
                    value.checksum_sha256);
                return encoded;
            }));
    tree.add_child(
        "connector_domains",
        array(
            execution.connector_domains,
            [](const service::ConnectorProvenance& value) {
                Tree encoded;
                encoded.put("domain", value.domain);
                encoded.put(
                    "contract_version",
                    value.contract_version);
                return encoded;
            }));
    return write(tree);
}

service::ExecutionMetadata decode_execution(
    const std::string& payload) {
    const Tree tree = read(payload);
    service::ExecutionMetadata value;
    value.result_schema_version =
        tree.get<std::string>("result_schema_version");
    value.command_schema_version =
        tree.get<std::string>("command_schema_version");
    value.platform_version =
        tree.get<std::string>("platform_version");
    value.operation = tree.get<std::string>("operation");
    value.solver =
        decode_solver_provenance(tree.get_child("solver"));
    value.catalog_fingerprint =
        tree.get<std::string>("catalog_fingerprint");
    value.model = {
        tree.get<std::string>("model.schema_version"),
        tree.get<std::string>("model.model_id"),
        tree.get<std::string>("model.model_revision"),
        tree.get<std::string>("model.case_id"),
    };
    if (const auto source =
            tree.get_child_optional("source_revisions")) {
        value.source_revisions =
            service::RevisionProvenance{
                source->get<std::string>("project_id"),
                source->get<std::string>(
                    "model_revision_id"),
                source->get<std::string>("model_checksum"),
                source->get<std::string>(
                    "case_revision_id"),
                source->get<std::string>("case_checksum"),
                source->get<std::string>(
                    "run_configuration_revision_id", ""),
                source->get<std::string>(
                    "run_configuration_checksum", ""),
                source->get<std::string>(
                    "study_revision_id", ""),
                source->get<std::string>("study_checksum", ""),
                source->get<std::string>(
                    "calibration_revision_id", ""),
                source->get<std::string>(
                    "calibration_checksum", ""),
                source->get<std::string>(
                    "reconciliation_revision_id", ""),
                source->get<std::string>(
                    "reconciliation_checksum", ""),
            };
    }
    value.components =
        decode_array<service::ComponentProvenance>(
            tree.get_child("components"),
            [](const Tree& encoded) {
                return service::ComponentProvenance{
                    encoded.get<std::string>("component_id"),
                    encoded.get<std::string>("kind"),
                    encoded.get<std::string>(
                        "requested_version"),
                    encoded.get<std::string>(
                        "resolved_version"),
                };
            });
    value.media = decode_array<service::MediumProvenance>(
        tree.get_child("media"),
        [](const Tree& encoded) {
            return service::MediumProvenance{
                encoded.get<std::string>("medium_id"),
                encoded.get<std::string>("backend"),
                encoded.get<std::string>("substance"),
                encoded.get<std::string>("package"),
                encoded.get<std::string>(
                    "requested_package_version"),
                encoded.get<std::string>(
                    "resolved_package_version"),
            };
        });
    value.artifacts =
        decode_array<service::ArtifactProvenance>(
            tree.get_child("artifacts"),
            [](const Tree& encoded) {
                return service::ArtifactProvenance{
                    encoded.get<std::string>("id"),
                    encoded.get<std::string>("artifact_type"),
                    encoded.get<std::string>(
                        "schema_version"),
                    encoded.get<std::string>("revision"),
                    encoded.get<std::string>(
                        "checksum_sha256"),
                };
            });
    value.connector_domains =
        decode_array<service::ConnectorProvenance>(
            tree.get_child("connector_domains"),
            [](const Tree& encoded) {
                return service::ConnectorProvenance{
                    encoded.get<std::string>("domain"),
                    encoded.get<std::string>(
                        "contract_version"),
                };
            });
    return value;
}

std::string encode_error(const service::ServiceError& error) {
    Tree tree;
    tree.put("schema_version", error.schema_version);
    tree.put("code", error.code);
    tree.put("stage", error.stage);
    tree.put("message", error.message);
    return write(tree);
}

service::ServiceError decode_error(
    const std::string& payload) {
    const Tree tree = read(payload);
    return {
        tree.get<std::string>("schema_version"),
        tree.get<std::string>("code"),
        tree.get<std::string>("stage"),
        tree.get<std::string>("message"),
    };
}

std::string encode_result_artifact(
    const service::ResultArtifactManifest& artifact) {
    Tree tree;
    tree.put("artifact_id", artifact.artifact_id);
    tree.put("media_type", artifact.media_type);
    tree.put("schema_version", artifact.schema_version);
    tree.put("byte_size", artifact.byte_size);
    tree.put("checksum", artifact.checksum);
    return write(tree);
}

service::ResultArtifactManifest decode_result_artifact(
    const std::string& payload) {
    const Tree tree = read(payload);
    return {
        tree.get<std::string>("artifact_id"),
        tree.get<std::string>("media_type"),
        tree.get<std::string>("schema_version"),
        tree.get<std::uint64_t>("byte_size"),
        tree.get<std::string>("checksum"),
    };
}

std::string encode_result_summary(
    const service::ResultSummary& summary) {
    Tree tree;
    tree.put("schema_version", summary.schema_version);
    tree.put("mode", summary.mode);
    tree.add_child(
        "values",
        array(
            summary.values,
            [](const service::ProjectedResultValue& value) {
                Tree encoded;
                encoded.put("id", value.id);
                encoded.put("dimension", value.dimension);
                encoded.put("value_si", value.value_si);
                encoded.put(
                    "aggregation",
                    service::to_string(value.aggregation));
                encoded.put(
                    "has_sample_time",
                    value.has_sample_time);
                encoded.put(
                    "sample_time", value.sample_time);
                return encoded;
            }));
    if (summary.engineering_acceptance) {
        const auto& acceptance = *summary.engineering_acceptance;
        Tree encoded;
        encoded.put("passed", acceptance.passed);
        encoded.put("passed_count", acceptance.passed_count);
        encoded.put("failed_count", acceptance.failed_count);
        encoded.add_child(
            "criteria",
            array(
                acceptance.criteria,
                [](const service::EngineeringAcceptanceResult& result) {
                    Tree item;
                    item.put("criterion_id", result.criterion_id);
                    item.put("projection_id", result.projection_id);
                    item.put("dimension", result.dimension);
                    item.put("actual_value_si", result.actual_value_si);
                    if (result.lower_bound_si) {
                        item.put("lower_bound_si", *result.lower_bound_si);
                    }
                    if (result.upper_bound_si) {
                        item.put("upper_bound_si", *result.upper_bound_si);
                    }
                    item.put("lower_inclusive", result.lower_inclusive);
                    item.put("upper_inclusive", result.upper_inclusive);
                    if (result.lower_margin_si) {
                        item.put(
                            "lower_margin_si", *result.lower_margin_si);
                    }
                    if (result.upper_margin_si) {
                        item.put(
                            "upper_margin_si", *result.upper_margin_si);
                    }
                    item.put(
                        "limiting_margin_si", result.limiting_margin_si);
                    item.put("limiting_bound", result.limiting_bound);
                    item.put("passed", result.passed);
                    return item;
                }));
        tree.add_child("engineering_acceptance", encoded);
    }
    return write(tree);
}

service::ResultSummary decode_result_summary(
    const std::string& payload) {
    const Tree tree = read(payload);
    service::ResultSummary summary;
    summary.schema_version =
        tree.get<std::string>("schema_version");
    summary.mode = tree.get<std::string>("mode");
    if (summary.schema_version !=
            service::result_summary_schema_v2 ||
        (summary.mode != "steady" &&
         summary.mode != "transient")) {
        throw std::runtime_error(
            "persisted result summary has an invalid contract");
    }
    summary.values =
        decode_array<service::ProjectedResultValue>(
            tree.get_child("values"),
            [](const Tree& encoded) {
                service::ProjectedResultValue value;
                value.id = encoded.get<std::string>("id");
                value.dimension =
                    encoded.get<std::string>("dimension");
                value.value_si =
                    encoded.get<double>("value_si");
                value.aggregation =
                    service::result_aggregation_from_string(
                        encoded.get<std::string>(
                            "aggregation"));
                value.has_sample_time =
                    encoded.get<bool>("has_sample_time");
                value.sample_time =
                    encoded.get<double>("sample_time");
                return value;
            });
    if (const auto encoded =
            tree.get_child_optional("engineering_acceptance")) {
        service::EngineeringAcceptanceSummary acceptance;
        acceptance.passed = encoded->get<bool>("passed");
        acceptance.passed_count =
            encoded->get<std::size_t>("passed_count");
        acceptance.failed_count =
            encoded->get<std::size_t>("failed_count");
        acceptance.criteria =
            decode_array<service::EngineeringAcceptanceResult>(
                encoded->get_child("criteria"),
                [](const Tree& item) {
                    service::EngineeringAcceptanceResult result;
                    result.criterion_id =
                        item.get<std::string>("criterion_id");
                    result.projection_id =
                        item.get<std::string>("projection_id");
                    result.dimension =
                        item.get<std::string>("dimension");
                    result.actual_value_si =
                        item.get<double>("actual_value_si");
                    if (const auto lower = item.get_optional<double>(
                            "lower_bound_si")) {
                        result.lower_bound_si = *lower;
                    }
                    if (const auto upper = item.get_optional<double>(
                            "upper_bound_si")) {
                        result.upper_bound_si = *upper;
                    }
                    result.lower_inclusive =
                        item.get("lower_inclusive", true);
                    result.upper_inclusive =
                        item.get("upper_inclusive", true);
                    if (const auto margin = item.get_optional<double>(
                            "lower_margin_si")) {
                        result.lower_margin_si = *margin;
                    }
                    if (const auto margin = item.get_optional<double>(
                            "upper_margin_si")) {
                        result.upper_margin_si = *margin;
                    }
                    result.limiting_margin_si =
                        item.get<double>("limiting_margin_si");
                    result.limiting_bound =
                        item.get<std::string>("limiting_bound");
                    result.passed = item.get<bool>("passed");
                    return result;
                });
        summary.engineering_acceptance = std::move(acceptance);
        service::validate_engineering_acceptance_summary(
            *summary.engineering_acceptance);
    }
    return summary;
}

}  // namespace thermox::postgres::detail
