#pragma once

#include "thermox/service/projects.hpp"
#include "thermox/service/simulation_jobs.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace thermox::service {

class ValidationCampaignReportService {
public:
    ValidationCampaignReportService(
        std::shared_ptr<ProjectService> projects,
        std::shared_ptr<SimulationJobService> jobs);

    [[nodiscard]] std::optional<JobValidationReport> report(
        const IdentityContext& identity,
        const std::string& project_id,
        const std::string& campaign_artifact_revision_id,
        const std::vector<std::string>& job_ids) const;

private:
    std::shared_ptr<ProjectService> projects_;
    std::shared_ptr<SimulationJobService> jobs_;
};

}  // namespace thermox::service
