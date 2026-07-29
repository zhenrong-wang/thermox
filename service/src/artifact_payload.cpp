#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include "artifact_payload.hpp"

#include "thermox/platform/performance_map.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

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
            "invalid performance-map payload: " +
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

}  // namespace thermox::service::detail
