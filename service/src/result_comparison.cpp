#include "thermox/service/simulation_jobs.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <set>
#include <string>
#include <utility>

namespace thermox::service {

namespace {

std::vector<std::string> trajectory_policy_signature(
    const SimulationJobRequest& request) {
    std::vector<std::string> signature;
    for (const auto& validation : request.trajectory_validations) {
        for (const auto& binding : validation.bindings) {
            std::ostringstream row;
            row << std::setprecision(
                std::numeric_limits<double>::max_digits10);
            row << validation.artifact_revision_id << '|'
                << validation.artifact.id << '|'
                << binding.signal_id << '|'
                << binding.projection.id << '|'
                << to_string(binding.projection.scope) << '|'
                << binding.projection.component_id << '|'
                << binding.projection.port_name << '|'
                << binding.projection.value_name << '|'
                << binding.projection.dimension << '|'
                << to_string(binding.projection.aggregation) << '|'
                << to_string(binding.comparison) << '|'
                << binding.time_offset_si << '|'
                << binding.baseline_time_si << '|'
                << binding.absolute_tolerance_si << '|'
                << binding.relative_tolerance << '|'
                << binding.uncertainty_multiplier << '|'
                << binding.maximum_interpolation_gap_si;
            signature.push_back(row.str());
        }
    }
    std::sort(signature.begin(), signature.end());
    return signature;
}

}  // namespace

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
        case ComparedValueStatus::window_mismatch:
            return "window_mismatch";
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
    const auto window_evidence = [](const ProjectedResultValue& source)
        -> std::optional<ResultWindowEvidence> {
        if (!source.has_window) return std::nullopt;
        return ResultWindowEvidence{
            source.window_start_time,
            source.window_end_time,
            source.window_anchor_event_name,
            source.window_anchor_event_occurrence,
        };
    };
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
            value.candidate_window = window_evidence(candidate_value);
            value.candidate_value_si = candidate_value.value_si;
            ++comparison.candidate_only_count;
        } else if (candidate_found == candidate_values.end()) {
            const auto& baseline_value = *baseline_found->second;
            value.status = ComparedValueStatus::baseline_only;
            value.baseline_dimension = baseline_value.dimension;
            value.baseline_aggregation = baseline_value.aggregation;
            value.baseline_window = window_evidence(baseline_value);
            value.baseline_value_si = baseline_value.value_si;
            ++comparison.baseline_only_count;
        } else {
            const auto& baseline_value = *baseline_found->second;
            const auto& candidate_value = *candidate_found->second;
            value.baseline_dimension = baseline_value.dimension;
            value.candidate_dimension = candidate_value.dimension;
            value.baseline_aggregation = baseline_value.aggregation;
            value.candidate_aggregation = candidate_value.aggregation;
            value.baseline_window = window_evidence(baseline_value);
            value.candidate_window = window_evidence(candidate_value);
            value.baseline_value_si = baseline_value.value_si;
            value.candidate_value_si = candidate_value.value_si;
            if (baseline_value.dimension != candidate_value.dimension) {
                value.status = ComparedValueStatus::dimension_mismatch;
                ++comparison.incompatible_count;
            } else if (baseline_value.aggregation !=
                       candidate_value.aggregation) {
                value.status = ComparedValueStatus::aggregation_mismatch;
                ++comparison.incompatible_count;
            } else if (value.baseline_window != value.candidate_window) {
                value.status = ComparedValueStatus::window_mismatch;
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
    const auto baseline_validation =
        baseline->result_summary->trajectory_validation;
    const auto candidate_validation =
        candidate->result_summary->trajectory_validation;
    if (baseline_validation) {
        comparison.trajectory_validation.baseline_passed =
            baseline_validation->passed;
        comparison.trajectory_validation.baseline_sample_count =
            baseline_validation->passed_count +
            baseline_validation->failed_count;
    }
    if (candidate_validation) {
        comparison.trajectory_validation.candidate_passed =
            candidate_validation->passed;
        comparison.trajectory_validation.candidate_sample_count =
            candidate_validation->passed_count +
            candidate_validation->failed_count;
    }
    if (!baseline_validation && candidate_validation) {
        comparison.trajectory_validation.compatibility =
            "baseline_not_evaluated";
    } else if (baseline_validation && !candidate_validation) {
        comparison.trajectory_validation.compatibility =
            "candidate_not_evaluated";
    } else if (baseline_validation && candidate_validation) {
        const auto baseline_policy =
            trajectory_policy_signature(baseline->request);
        const auto candidate_policy =
            trajectory_policy_signature(candidate->request);
        if (baseline_policy.empty() || candidate_policy.empty() ||
            baseline_policy != candidate_policy ||
            baseline_validation->validation_count !=
                baseline->request.trajectory_validations.size() ||
            candidate_validation->validation_count !=
                candidate->request.trajectory_validations.size()) {
            comparison.trajectory_validation.compatibility =
                "evidence_policy_mismatch";
        } else {
            comparison.trajectory_validation.compatibility =
                "comparable";
            comparison.trajectory_validation.transition =
                std::string(
                    baseline_validation->passed
                        ? "matched"
                        : "not_matched") +
                "_to_" +
                (candidate_validation->passed
                     ? "matched"
                     : "not_matched");
        }
    }
    return comparison;
}

std::optional<JobValidationReport>
SimulationJobService::validation_report(
    const IdentityContext& identity,
    const std::vector<std::string>& job_ids) const {
    constexpr std::size_t maximum_report_jobs = 100U;
    if (job_ids.empty() || job_ids.size() > maximum_report_jobs) {
        throw JobValidationReportError(
            "validation reports require between 1 and 100 jobs");
    }
    std::set<std::string> unique_ids;
    JobValidationReport report;
    report.team_id = identity.team_id;
    report.job_count = job_ids.size();
    report.jobs.reserve(job_ids.size());
    for (const auto& job_id : job_ids) {
        if (job_id.empty() || !unique_ids.insert(job_id).second) {
            throw JobValidationReportError(
                "validation report job IDs must be non-empty and unique");
        }
        const auto job = get(identity, job_id);
        if (!job) return std::nullopt;
        if (!is_terminal(job->state)) {
            throw JobValidationReportError(
                "validation reports require terminal jobs");
        }
        if (!job->request.source_revisions ||
            job->request.source_revisions->project_id.empty() ||
            job->request.source_revisions->study_revision_id.empty() ||
            (job->request.mode != SimulationJobMode::steady &&
             job->request.mode != SimulationJobMode::transient)) {
            throw JobValidationReportError(
                "validation reports require revision-backed Study jobs");
        }
        const auto& source = *job->request.source_revisions;
        if (report.project_id.empty()) {
            report.project_id = source.project_id;
        } else if (report.project_id != source.project_id) {
            throw JobValidationReportError(
                "validation report jobs must belong to one Project");
        }

        JobValidationReportEntry entry;
        entry.job_id = job->job_id;
        entry.study_revision_id = source.study_revision_id;
        entry.mode = to_string(job->request.mode);
        entry.state = to_string(job->state);
        for (const auto& validation :
             job->request.trajectory_validations) {
            if (validation.artifact_revision_id.empty()) {
                throw JobValidationReportError(
                    "validation report evidence lacks exact revision "
                    "provenance");
            }
            entry.evidence_artifact_revision_ids.push_back(
                validation.artifact_revision_id);
        }
        std::sort(
            entry.evidence_artifact_revision_ids.begin(),
            entry.evidence_artifact_revision_ids.end());
        entry.evidence_artifact_revision_ids.erase(
            std::unique(
                entry.evidence_artifact_revision_ids.begin(),
                entry.evidence_artifact_revision_ids.end()),
            entry.evidence_artifact_revision_ids.end());
        const bool evidence_declared =
            !entry.evidence_artifact_revision_ids.empty();
        if (evidence_declared) ++report.evidence_declared_count;

        if (job->state == SimulationJobState::succeeded) {
            ++report.succeeded_count;
            if (!evidence_declared) {
                entry.validation_status = "not_declared";
            } else {
                if (!job->result_summary ||
                    !job->result_summary->trajectory_validation ||
                    job->result_summary->trajectory_validation
                            ->validation_count !=
                        job->request.trajectory_validations.size()) {
                    throw JobValidationReportError(
                        "successful evidence-bound job lacks a "
                        "consistent validation summary");
                }
                const auto& validation =
                    *job->result_summary->trajectory_validation;
                entry.validation_status =
                    validation.passed ? "matched" : "not_matched";
                entry.passed_count = validation.passed_count;
                entry.failed_count = validation.failed_count;
                entry.exact_alignment_count =
                    validation.exact_alignment_count;
                entry.interpolated_alignment_count =
                    validation.interpolated_alignment_count;
                ++report.evaluated_count;
                if (validation.passed) ++report.matched_count;
                else ++report.not_matched_count;
                report.passed_sample_count += validation.passed_count;
                report.failed_sample_count += validation.failed_count;
                report.exact_alignment_count +=
                    validation.exact_alignment_count;
                report.interpolated_alignment_count +=
                    validation.interpolated_alignment_count;
            }
        } else {
            ++report.unsuccessful_count;
            if (evidence_declared) {
                entry.validation_status =
                    "not_evaluated_execution_unsuccessful";
                ++report.unevaluated_count;
            } else {
                entry.validation_status = "not_declared";
            }
        }
        report.jobs.push_back(std::move(entry));
    }
    return report;
}

}  // namespace thermox::service
