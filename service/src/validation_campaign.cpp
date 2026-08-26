#include "thermox/service/validation_campaign.hpp"

#include <nlohmann/json.hpp>

#include <set>

namespace thermox::service {
namespace {

using Json = nlohmann::json;

void validate_campaign(const ValidationCampaignArtifact& artifact) {
    if (artifact.schema_version != validation_campaign_schema_v1 ||
        artifact.id.empty() || artifact.name.empty() ||
        artifact.objective.empty() ||
        artifact.study_revision_ids.empty() ||
        artifact.study_revision_ids.size() > 100U ||
        artifact.limitations.size() > 64U) {
        throw ValidationCampaignError(
            "validation campaign requires its v1 schema, identity, name, "
            "objective, 1 to 100 Studies, and at most 64 limitations");
    }
    std::set<std::string> studies;
    for (const auto& study_revision_id : artifact.study_revision_ids) {
        if (study_revision_id.empty() ||
            !studies.insert(study_revision_id).second) {
            throw ValidationCampaignError(
                "validation campaign Study revision IDs must be non-empty "
                "and unique");
        }
    }
    std::set<std::string> limitations;
    for (const auto& limitation : artifact.limitations) {
        if (limitation.empty() ||
            !limitations.insert(limitation).second) {
            throw ValidationCampaignError(
                "validation campaign limitations must be non-empty and "
                "unique");
        }
    }
}

}  // namespace

ValidationCampaignArtifact parse_validation_campaign_artifact_json(
    const std::string& text) {
    try {
        const auto root = Json::parse(text);
        ValidationCampaignArtifact artifact;
        artifact.schema_version =
            root.at("schema_version").get<std::string>();
        artifact.id = root.at("id").get<std::string>();
        artifact.name = root.at("name").get<std::string>();
        artifact.objective = root.at("objective").get<std::string>();
        artifact.study_revision_ids =
            root.at("study_revision_ids")
                .get<std::vector<std::string>>();
        artifact.limitations = root.value(
            "limitations", std::vector<std::string>{});
        validate_campaign(artifact);
        return artifact;
    } catch (const ValidationCampaignError&) {
        throw;
    } catch (const std::exception& error) {
        throw ValidationCampaignError(
            "invalid validation-campaign artifact: " +
            std::string(error.what()));
    }
}

std::string serialize_validation_campaign_artifact_json(
    const ValidationCampaignArtifact& artifact) {
    validate_campaign(artifact);
    const Json root{
        {"schema_version", artifact.schema_version},
        {"id", artifact.id},
        {"name", artifact.name},
        {"objective", artifact.objective},
        {"study_revision_ids", artifact.study_revision_ids},
        {"limitations", artifact.limitations},
    };
    return root.dump(2) + "\n";
}

}  // namespace thermox::service
