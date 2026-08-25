#include "thermox/service/artifact_declaration.hpp"

#include "artifact_payload.hpp"

#include <nlohmann/json.hpp>

#include <exception>
#include <set>
#include <string>

namespace thermox::service {

PerformanceMapArtifactInput
parse_performance_map_artifact_declaration_json(
    const std::string& text) {
    try {
        const auto declaration = nlohmann::json::parse(text);
        if (!declaration.is_object()) {
            throw ArtifactDeclarationError(
                "performance-map artifact declaration must be an object");
        }
        const std::set<std::string> allowed = {
            "id", "schema_version", "revision", "checksum_sha256",
            "payload", "import_audit"};
        for (const auto& [key, unused] : declaration.items()) {
            (void)unused;
            if (!allowed.contains(key)) {
                throw ArtifactDeclarationError(
                    "unknown performance-map artifact declaration field: " +
                    key);
            }
        }
        for (const char* required : {
                 "id", "schema_version", "revision",
                 "checksum_sha256", "payload"}) {
            if (!declaration.contains(required)) {
                throw ArtifactDeclarationError(
                    "performance-map artifact declaration is missing field: " +
                    std::string(required));
            }
        }
        return detail::performance_map_from_payload(
            declaration.at("id").get<std::string>(),
            declaration.at("schema_version").get<std::string>(),
            declaration.at("revision").get<std::string>(),
            declaration.at("checksum_sha256").get<std::string>(),
            declaration.at("payload").dump());
    } catch (const ArtifactDeclarationError&) {
        throw;
    } catch (const std::exception& error) {
        throw ArtifactDeclarationError(
            std::string("invalid performance-map artifact declaration: ") +
            error.what());
    }
}

}  // namespace thermox::service
