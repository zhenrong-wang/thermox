#include "thermox/service/simulation_jobs.hpp"

#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace thermox::service {

std::string to_string(ComparedValueStatus status) {
    switch (status) {
        case ComparedValueStatus::matched:
            return "matched";
        case ComparedValueStatus::baseline_only:
            return "baseline_only";
        case ComparedValueStatus::candidate_only:
            return "candidate_only";
        case ComparedValueStatus::dimension_mismatch:
            return "dimension_mismatch";
        case ComparedValueStatus::aggregation_mismatch:
            return "aggregation_mismatch";
    }
    return "unknown";
}

std::optional<SimulationJobComparison> SimulationJobService::compare(
    const IdentityContext& identity,
    const std::string& baseline_job_id,
    const std::string& candidate_job_id) const {
    if (baseline_job_id.empty() || candidate_job_id.empty()) {
        throw JobComparisonError(
            "comparison job IDs must not be empty");
    }
    if (baseline_job_id == candidate_job_id) {
        throw JobComparisonError(
            "baseline and candidate jobs must be different");
    }
    const auto baseline = get(identity, baseline_job_id);
    const auto candidate = get(identity, candidate_job_id);
    if (!baseline || !candidate) return std::nullopt;
    if (baseline->state != SimulationJobState::succeeded ||
        candidate->state != SimulationJobState::succeeded ||
        !baseline->result_summary || !candidate->result_summary) {
        throw JobComparisonError(
            "comparison requires two successful jobs with result summaries");
    }
    if (!baseline->request.source_revisions ||
        !candidate->request.source_revisions) {
        throw JobComparisonError(
            "Study comparison requires revision-backed jobs");
    }
    const auto& baseline_source = *baseline->request.source_revisions;
    const auto& candidate_source = *candidate->request.source_revisions;
    if (baseline_source.project_id.empty() ||
        baseline_source.project_id != candidate_source.project_id) {
        throw JobComparisonError(
            "comparison jobs must belong to the same Project");
    }
    if (baseline_source.study_revision_id.empty() ||
        candidate_source.study_revision_id.empty()) {
        throw JobComparisonError(
            "comparison jobs must identify immutable Study revisions");
    }
    if (baseline->result_summary->mode !=
        candidate->result_summary->mode) {
        throw JobComparisonError(
            "comparison jobs must have the same simulation mode");
    }

    SimulationJobComparison comparison;
    comparison.team_id = identity.team_id;
    comparison.project_id = baseline_source.project_id;
    comparison.baseline_job_id = baseline->job_id;
    comparison.candidate_job_id = candidate->job_id;
    comparison.baseline_study_revision_id =
        baseline_source.study_revision_id;
    comparison.candidate_study_revision_id =
        candidate_source.study_revision_id;
    comparison.mode = baseline->result_summary->mode;

    std::map<std::string, const ProjectedResultValue*> baseline_values;
    std::map<std::string, const ProjectedResultValue*> candidate_values;
    for (const auto& value : baseline->result_summary->values) {
        baseline_values.emplace(value.id, &value);
    }
    for (const auto& value : candidate->result_summary->values) {
        candidate_values.emplace(value.id, &value);
    }
    std::set<std::string> value_ids;
    for (const auto& [id, unused] : baseline_values) {
        (void)unused;
        value_ids.emplace(id);
    }
    for (const auto& [id, unused] : candidate_values) {
        (void)unused;
        value_ids.emplace(id);
    }
    comparison.values.reserve(value_ids.size());
    for (const auto& id : value_ids) {
        const auto baseline_found = baseline_values.find(id);
        const auto candidate_found = candidate_values.find(id);
        ComparedResultValue value;
        value.id = id;
        if (baseline_found == baseline_values.end()) {
            const auto& candidate_value = *candidate_found->second;
            value.status = ComparedValueStatus::candidate_only;
            value.candidate_dimension = candidate_value.dimension;
            value.candidate_aggregation = candidate_value.aggregation;
            value.candidate_value_si = candidate_value.value_si;
            ++comparison.candidate_only_count;
        } else if (candidate_found == candidate_values.end()) {
            const auto& baseline_value = *baseline_found->second;
            value.status = ComparedValueStatus::baseline_only;
            value.baseline_dimension = baseline_value.dimension;
            value.baseline_aggregation = baseline_value.aggregation;
            value.baseline_value_si = baseline_value.value_si;
            ++comparison.baseline_only_count;
        } else {
            const auto& baseline_value = *baseline_found->second;
            const auto& candidate_value = *candidate_found->second;
            value.baseline_dimension = baseline_value.dimension;
            value.candidate_dimension = candidate_value.dimension;
            value.baseline_aggregation = baseline_value.aggregation;
            value.candidate_aggregation = candidate_value.aggregation;
            value.baseline_value_si = baseline_value.value_si;
            value.candidate_value_si = candidate_value.value_si;
            if (baseline_value.dimension != candidate_value.dimension) {
                value.status = ComparedValueStatus::dimension_mismatch;
                ++comparison.incompatible_count;
            } else if (baseline_value.aggregation !=
                       candidate_value.aggregation) {
                value.status = ComparedValueStatus::aggregation_mismatch;
                ++comparison.incompatible_count;
            } else {
                value.status = ComparedValueStatus::matched;
                value.absolute_delta_si =
                    candidate_value.value_si - baseline_value.value_si;
                if (baseline_value.value_si != 0.0) {
                    value.relative_delta = *value.absolute_delta_si /
                        std::abs(baseline_value.value_si);
                }
                ++comparison.matched_count;
            }
        }
        comparison.values.push_back(std::move(value));
    }

    const auto baseline_acceptance =
        baseline->result_summary->engineering_acceptance;
    const auto candidate_acceptance =
        candidate->result_summary->engineering_acceptance;
    if (baseline_acceptance) {
        comparison.engineering_acceptance.baseline_passed =
            baseline_acceptance->passed;
    }
    if (candidate_acceptance) {
        comparison.engineering_acceptance.candidate_passed =
            candidate_acceptance->passed;
    }
    if (baseline_acceptance && candidate_acceptance) {
        comparison.engineering_acceptance.transition =
            std::string(
                baseline_acceptance->passed
                    ? "accepted"
                    : "not_accepted") +
            "_to_" +
            (candidate_acceptance->passed
                 ? "accepted"
                 : "not_accepted");
    }
    return comparison;
}

}  // namespace thermox::service
