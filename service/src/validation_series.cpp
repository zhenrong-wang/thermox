#include "thermox/service/validation_series.hpp"

#include "thermox/platform/unit_registry.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <utility>

namespace thermox::service {
namespace {

using Json = nlohmann::json;

bool valid_sha256(const std::string& value) {
    return value.size() == 64U &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return (c >= '0' && c <= '9') ||
                (c >= 'a' && c <= 'f');
        });
}

void validate_source(const ValidationSeriesSource& source) {
    if (source.reference.empty() ||
        !valid_sha256(source.checksum_sha256)) {
        throw ValidationSeriesError(
            "validation-series source requires a reference and a "
            "lowercase SHA-256 checksum");
    }
    static const std::set<std::string> acquisitions{
        "measured", "computational", "derived", "digitized"};
    if (!acquisitions.contains(source.acquisition)) {
        throw ValidationSeriesError(
            "validation-series acquisition must be measured, "
            "computational, derived, or digitized");
    }
    std::set<std::string> limitations;
    for (const auto& limitation : source.limitations) {
        if (limitation.empty() ||
            !limitations.insert(limitation).second) {
            throw ValidationSeriesError(
                "validation-series limitations must be non-empty and "
                "unique");
        }
    }
    (void)to_string(source.evidence_basis);
}

void validate_artifact(const ValidationSeriesArtifact& artifact) {
    if (artifact.schema_version != validation_series_schema_v1 ||
        artifact.id.empty() || artifact.signals.empty() ||
        artifact.signals.size() > 128U) {
        throw ValidationSeriesError(
            "validation-series schema, identity, or signal count is "
            "invalid");
    }
    validate_source(artifact.source);
    std::set<std::string> ids;
    std::size_t sample_count = 0U;
    for (const auto& signal : artifact.signals) {
        if (signal.id.empty() || signal.dimension.empty() ||
            signal.canonical_unit.empty() ||
            signal.samples.empty() ||
            !ids.insert(signal.id).second) {
            throw ValidationSeriesError(
                "validation-series signals require unique identities, "
                "dimensions, units, and samples");
        }
        sample_count += signal.samples.size();
        for (std::size_t index = 0U;
             index < signal.samples.size(); ++index) {
            const auto& sample = signal.samples[index];
            if (!std::isfinite(sample.time_si) ||
                !std::isfinite(sample.value_si) ||
                sample.time_si < 0.0 ||
                (index > 0U &&
                 sample.time_si <= signal.samples[index - 1U].time_si) ||
                (sample.standard_uncertainty_si &&
                 (!std::isfinite(*sample.standard_uncertainty_si) ||
                  *sample.standard_uncertainty_si < 0.0))) {
                throw ValidationSeriesError(
                    "validation-series samples require finite, strictly "
                    "increasing non-negative times, finite values, and "
                    "non-negative uncertainties");
            }
        }
    }
    if (sample_count > 100000U) {
        throw ValidationSeriesError(
            "validation-series may contain at most 100000 samples");
    }
}

double uncertainty_to_si(
    const thermox::platform::UnitRegistry& units,
    double value,
    const std::string& unit) {
    const double zero = units.convert(0.0, unit).value_si;
    return std::abs(units.convert(value, unit).value_si - zero);
}

double alignment_gap(
    const std::vector<StateSample>& trajectory,
    double time) {
    const auto found = std::lower_bound(
        trajectory.begin(), trajectory.end(), time,
        [](const StateSample& sample, double candidate) {
            return sample.time < candidate;
        });
    if (found != trajectory.end() && found->time == time) return 0.0;
    if (found == trajectory.begin() || found == trajectory.end()) {
        throw ValidationSeriesError(
            "validation-series comparison time lies outside the "
            "simulation trajectory");
    }
    return found->time - std::prev(found)->time;
}

}  // namespace

std::string to_string(TrajectoryComparison comparison) {
    switch (comparison) {
    case TrajectoryComparison::absolute:
        return "absolute";
    case TrajectoryComparison::projected_change:
        return "projected_change";
    }
    throw ValidationSeriesError(
        "unknown trajectory comparison mode");
}

TrajectoryComparison trajectory_comparison_from_string(
    const std::string& value) {
    if (value == "absolute") return TrajectoryComparison::absolute;
    if (value == "projected_change") {
        return TrajectoryComparison::projected_change;
    }
    throw ValidationSeriesError(
        "unsupported trajectory comparison mode: " + value);
}

ValidationSeriesArtifact parse_validation_series_artifact_json(
    const std::string& text) {
    try {
        const auto root = Json::parse(text);
        const auto units = thermox::platform::make_default_unit_registry();
        ValidationSeriesArtifact artifact;
        artifact.schema_version = root.at("schema_version").get<std::string>();
        artifact.id = root.at("id").get<std::string>();
        const auto& source = root.at("source");
        artifact.source.reference =
            source.at("reference").get<std::string>();
        artifact.source.checksum_sha256 =
            source.at("checksum_sha256").get<std::string>();
        artifact.source.evidence_basis =
            validation_evidence_basis_from_string(
                source.at("evidence_basis").get<std::string>());
        artifact.source.acquisition =
            source.at("acquisition").get<std::string>();
        artifact.source.note = source.value("note", std::string{});
        artifact.source.limitations = source.value(
            "limitations", std::vector<std::string>{});
        const std::string time_unit =
            root.at("time_unit").get<std::string>();
        const auto time_conversion = units.convert(0.0, time_unit);
        if (time_conversion.dimension != "time") {
            throw ValidationSeriesError(
                "validation-series time_unit must have time dimension");
        }
        for (const auto& encoded_signal : root.at("signals")) {
            ValidationSeriesSignal signal;
            signal.id = encoded_signal.at("id").get<std::string>();
            signal.dimension =
                encoded_signal.at("dimension").get<std::string>();
            const std::string unit =
                encoded_signal.at("unit").get<std::string>();
            const auto unit_conversion = units.convert(0.0, unit);
            if (unit_conversion.dimension != signal.dimension) {
                throw ValidationSeriesError(
                    "validation-series signal unit dimension does not "
                    "match its declared dimension");
            }
            signal.canonical_unit = unit_conversion.unit;
            for (const auto& encoded_sample :
                 encoded_signal.at("samples")) {
                ValidationSeriesSample sample;
                sample.time_si = units.convert(
                    encoded_sample.at("time").get<double>(),
                    time_unit).value_si;
                sample.value_si = units.convert(
                    encoded_sample.at("value").get<double>(),
                    unit).value_si;
                if (encoded_sample.contains("standard_uncertainty")) {
                    sample.standard_uncertainty_si = uncertainty_to_si(
                        units,
                        encoded_sample.at("standard_uncertainty")
                            .get<double>(),
                        unit);
                }
                signal.samples.push_back(std::move(sample));
            }
            artifact.signals.push_back(std::move(signal));
        }
        validate_artifact(artifact);
        return artifact;
    } catch (const ValidationSeriesError&) {
        throw;
    } catch (const std::exception& error) {
        throw ValidationSeriesError(
            "invalid validation-series artifact: " +
            std::string(error.what()));
    }
}

std::string serialize_validation_series_artifact_json(
    const ValidationSeriesArtifact& artifact) {
    validate_artifact(artifact);
    Json root;
    root["schema_version"] = artifact.schema_version;
    root["id"] = artifact.id;
    root["source"] = {
        {"reference", artifact.source.reference},
        {"checksum_sha256", artifact.source.checksum_sha256},
        {"evidence_basis", to_string(artifact.source.evidence_basis)},
        {"acquisition", artifact.source.acquisition},
        {"note", artifact.source.note},
        {"limitations", artifact.source.limitations},
    };
    root["time_unit"] = "s";
    root["signals"] = Json::array();
    for (const auto& signal : artifact.signals) {
        Json encoded{
            {"id", signal.id},
            {"dimension", signal.dimension},
            {"unit", signal.canonical_unit},
            {"samples", Json::array()},
        };
        for (const auto& sample : signal.samples) {
            Json item{{"time", sample.time_si}, {"value", sample.value_si}};
            if (sample.standard_uncertainty_si) {
                item["standard_uncertainty"] =
                    *sample.standard_uncertainty_si;
            }
            encoded["samples"].push_back(std::move(item));
        }
        root["signals"].push_back(std::move(encoded));
    }
    return root.dump(2) + "\n";
}

TrajectoryValidationSummary evaluate_trajectory_validation(
    const ValidationSeriesArtifact& artifact,
    const std::vector<TrajectoryValidationBinding>& bindings,
    const std::vector<StateSample>& trajectory,
    const std::vector<EventValue>& events) {
    validate_artifact(artifact);
    if (bindings.empty() || trajectory.empty()) {
        throw ValidationSeriesError(
            "trajectory validation requires bindings and a trajectory");
    }
    std::set<std::string> bound_signals;
    std::vector<ResultProjection> projections;
    std::vector<ValidationEvidenceCriterion> criteria;
    TrajectoryValidationSummary summary;
    summary.artifact_id = artifact.id;
    for (const auto& binding : bindings) {
        const auto signal = std::find_if(
            artifact.signals.begin(), artifact.signals.end(),
            [&](const auto& candidate) {
                return candidate.id == binding.signal_id;
            });
        if (signal == artifact.signals.end() ||
            !bound_signals.insert(binding.signal_id).second ||
            binding.projection.dimension != signal->dimension ||
            binding.projection.window ||
            !std::isfinite(binding.time_offset_si) ||
            !std::isfinite(binding.baseline_time_si) ||
            !std::isfinite(binding.absolute_tolerance_si) ||
            !std::isfinite(binding.relative_tolerance) ||
            !std::isfinite(binding.uncertainty_multiplier) ||
            !std::isfinite(binding.maximum_interpolation_gap_si) ||
            binding.absolute_tolerance_si < 0.0 ||
            binding.relative_tolerance < 0.0 ||
            binding.uncertainty_multiplier < 0.0 ||
            binding.maximum_interpolation_gap_si < 0.0) {
            throw ValidationSeriesError(
                "trajectory validation binding is missing, duplicated, "
                "dimensionally inconsistent, or has invalid policy");
        }
        for (std::size_t index = 0U;
             index < signal->samples.size(); ++index) {
            const auto& sample = signal->samples[index];
            const double simulation_time =
                binding.time_offset_si + sample.time_si;
            if (binding.comparison ==
                    TrajectoryComparison::projected_change &&
                simulation_time < binding.baseline_time_si) {
                throw ValidationSeriesError(
                    "projected-change sample precedes its baseline");
            }
            double gap = alignment_gap(trajectory, simulation_time);
            if (binding.comparison ==
                TrajectoryComparison::projected_change) {
                gap = std::max(
                    gap,
                    alignment_gap(
                        trajectory, binding.baseline_time_si));
            }
            if (gap > binding.maximum_interpolation_gap_si) {
                throw ValidationSeriesError(
                    "trajectory validation interpolation gap exceeds "
                    "the binding policy");
            }
            summary.exact_alignment_count += gap == 0.0 ? 1U : 0U;
            summary.interpolated_alignment_count += gap == 0.0 ? 0U : 1U;
            summary.maximum_alignment_gap_si = std::max(
                summary.maximum_alignment_gap_si, gap);
            const std::string id = binding.signal_id + "[" +
                std::to_string(index) + "]";
            auto projection = binding.projection;
            projection.id = id;
            projection.aggregation = binding.comparison ==
                    TrajectoryComparison::absolute
                ? ResultAggregation::final
                : ResultAggregation::change;
            projection.window = ResultWindow{
                ResultWindowAnchor::simulation,
                binding.comparison == TrajectoryComparison::absolute
                    ? simulation_time : binding.baseline_time_si,
                simulation_time,
                {},
                0U,
            };
            projections.push_back(std::move(projection));
            criteria.push_back({
                id + ".reference",
                id,
                ValidationEvidenceLayer::system,
                artifact.source.evidence_basis,
                signal->dimension,
                sample.value_si,
                binding.absolute_tolerance_si +
                    binding.uncertainty_multiplier *
                        sample.standard_uncertainty_si.value_or(0.0),
                binding.relative_tolerance,
                artifact.source.reference + " sha256:" +
                    artifact.source.checksum_sha256,
                artifact.source.note,
            });
        }
    }
    if (projections.size() > 256U) {
        throw ValidationSeriesError(
            "trajectory validation may compare at most 256 samples per "
            "evaluation");
    }
    try {
        const auto projected = project_transient_result(
            trajectory, events, projections);
        summary.evidence = evaluate_validation_evidence(
            validation_observations_from_result_summary(projected),
            criteria,
            artifact.source.limitations);
    } catch (const std::exception& error) {
        throw ValidationSeriesError(
            "trajectory validation evaluation failed: " +
            std::string(error.what()));
    }
    return summary;
}

}  // namespace thermox::service
