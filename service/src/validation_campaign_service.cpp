#include "thermox/service/validation_campaign_service.hpp"

#include <stdexcept>
#include <utility>

namespace thermox::service {

ValidationCampaignReportService::ValidationCampaignReportService(
    std::shared_ptr<ProjectService> projects,
    std::shared_ptr<SimulationJobService> jobs)
    : projects_(std::move(projects)), jobs_(std::move(jobs)) {
    if (!projects_ || !jobs_) {
        throw std::invalid_argument(
            "validation campaign report service dependencies must not "
            "be null");
    }
}

std::optional<JobValidationReport>
ValidationCampaignReportService::report(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string& campaign_artifact_revision_id,
    const std::vector<std::string>& job_ids) const {
    if (project_id.empty() || campaign_artifact_revision_id.empty()) {
        throw ValidationCampaignError(
            "validation report requires Project and campaign revision "
            "identities");
    }
    const auto content = projects_->get_artifact_revision_content(
        identity, project_id, campaign_artifact_revision_id);
    if (!content) return std::nullopt;
    if (content->revision.artifact_type !=
            validation_campaign_artifact_type ||
        content->revision.artifact_schema_version !=
            validation_campaign_schema_v1) {
        throw ValidationCampaignError(
            "validation report revision is not a validation-campaign "
            "artifact");
    }
    const auto campaign = parse_validation_campaign_artifact_json(
        content->canonical_artifact_json);
    if (campaign.id != content->revision.artifact_id) {
        throw ValidationCampaignError(
            "validation campaign identity does not match its immutable "
            "revision metadata");
    }
    auto report = jobs_->validation_report(
        identity,
        job_ids,
        {
            content->revision.artifact_revision_id,
            content->revision.content.checksum,
            campaign,
        });
    if (report && report->project_id != project_id) {
        throw JobValidationReportError(
            "validation campaign jobs must belong to its exact Project");
    }
    return report;
}

}  // namespace thermox::service
