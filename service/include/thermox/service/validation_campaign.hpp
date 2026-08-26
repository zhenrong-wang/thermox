#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace thermox::service {

inline constexpr char validation_campaign_artifact_type[] =
    "thermox.validation_campaign";
inline constexpr char validation_campaign_schema_v1[] =
    "thermox.validation_campaign/v1";

struct ValidationCampaignArtifact {
    std::string schema_version{validation_campaign_schema_v1};
    std::string id;
    std::string name;
    std::string objective;
    std::vector<std::string> study_revision_ids;
    std::vector<std::string> limitations;
};

struct ValidationCampaignReference {
    std::string artifact_revision_id;
    std::string artifact_checksum;
    ValidationCampaignArtifact artifact;
};

class ValidationCampaignError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

ValidationCampaignArtifact parse_validation_campaign_artifact_json(
    const std::string& text);

std::string serialize_validation_campaign_artifact_json(
    const ValidationCampaignArtifact& artifact);

}  // namespace thermox::service
