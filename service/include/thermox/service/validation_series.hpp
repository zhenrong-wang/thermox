#pragma once

#include "thermox/service/result_projection.hpp"
#include "thermox/service/validation_evidence.hpp"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace thermox::service {

inline constexpr char validation_series_artifact_type[] =
    "thermox.validation_series";
inline constexpr char validation_series_schema_v1[] =
    "thermox.validation_series/v1";
inline constexpr char trajectory_validation_schema_v1[] =
    "thermox.trajectory_validation/v1";

struct ValidationSeriesSource {
    std::string reference;
    std::string checksum_sha256;
    ValidationEvidenceBasis evidence_basis{
        ValidationEvidenceBasis::independent_reference};
    std::string acquisition;
    std::string note;
    std::vector<std::string> limitations;
};

struct ValidationSeriesSample {
    double time_si{0.0};
    double value_si{0.0};
    std::optional<double> standard_uncertainty_si;
};

struct ValidationSeriesSignal {
    std::string id;
    std::string dimension;
    std::string canonical_unit;
    std::vector<ValidationSeriesSample> samples;
};

struct ValidationSeriesArtifact {
    std::string schema_version{validation_series_schema_v1};
    std::string id;
    ValidationSeriesSource source;
    std::vector<ValidationSeriesSignal> signals;
};

enum class TrajectoryComparison {
    absolute,
    projected_change,
};

std::string to_string(TrajectoryComparison comparison);
TrajectoryComparison trajectory_comparison_from_string(
    const std::string& value);

struct TrajectoryValidationBinding {
    std::string signal_id;
    ResultProjection projection;
    TrajectoryComparison comparison{TrajectoryComparison::absolute};
    double time_offset_si{0.0};
    double baseline_time_si{0.0};
    double absolute_tolerance_si{0.0};
    double relative_tolerance{0.0};
    double uncertainty_multiplier{0.0};
    double maximum_interpolation_gap_si{0.0};
};

struct StudyTrajectoryValidationBinding {
    std::string id;
    std::string artifact_revision_id;
    std::string signal_id;
    std::string projection_id;
    TrajectoryComparison comparison{TrajectoryComparison::absolute};
    double time_offset_si{0.0};
    double baseline_time_si{0.0};
    double absolute_tolerance_si{0.0};
    double relative_tolerance{0.0};
    double uncertainty_multiplier{0.0};
    double maximum_interpolation_gap_si{0.0};
};

struct TrajectoryValidationSummary {
    std::string schema_version{trajectory_validation_schema_v1};
    std::string artifact_id;
    std::size_t exact_alignment_count{0};
    std::size_t interpolated_alignment_count{0};
    double maximum_alignment_gap_si{0.0};
    ValidationEvidenceSummary evidence;
};

struct TrajectoryValidationPlan {
    std::string artifact_revision_id;
    ValidationSeriesArtifact artifact;
    std::vector<TrajectoryValidationBinding> bindings;
};

class ValidationSeriesError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

ValidationSeriesArtifact parse_validation_series_artifact_json(
    const std::string& text);

std::string serialize_validation_series_artifact_json(
    const ValidationSeriesArtifact& artifact);

std::string serialize_trajectory_validation_summary_json(
    const TrajectoryValidationSummary& summary);

TrajectoryValidationSummary evaluate_trajectory_validation(
    const ValidationSeriesArtifact& artifact,
    const std::vector<TrajectoryValidationBinding>& bindings,
    const std::vector<StateSample>& trajectory,
    const std::vector<EventValue>& events = {});

}  // namespace thermox::service
