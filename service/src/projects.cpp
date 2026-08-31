#include "thermox/service/projects.hpp"

#include "artifact_payload.hpp"
#include "serialization_internal.hpp"

#include "thermox/service/in_memory_projects.hpp"
#include "thermox/service/serialization.hpp"
#include "thermox/platform/expression_component.hpp"
#include "thermox/platform/correlation.hpp"
#include "thermox/platform/model_document.hpp"
#include "thermox/platform/performance_map.hpp"
#include "thermox/platform/regime_map.hpp"

#include <openssl/evp.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>
#include <utility>

namespace thermox::service {

namespace {

void require_identity(const IdentityContext& identity) {
    if (identity.user_id.empty() || identity.team_id.empty()) {
        throw ProjectRequestError(
            "user ID and Team ID must not be empty");
    }
}

std::string trim(std::string value) {
    const auto whitespace = [](unsigned char character) {
        return std::isspace(character) != 0;
    };
    std::size_t first = 0;
    while (first < value.size() &&
           whitespace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first &&
           whitespace(
               static_cast<unsigned char>(value[last - 1U]))) {
        --last;
    }
    return value.substr(first, last - first);
}

template <typename Entity>
typename std::vector<Entity>::iterator find_entity(
    std::vector<Entity>& entities,
    const std::string& id) {
    return std::find_if(
        entities.begin(),
        entities.end(),
        [&](const auto& entity) {
            return entity.id == id;
        });
}

template <typename Entity>
void upsert_entity(
    std::vector<Entity>& entities,
    Entity entity,
    const std::string& expected_id) {
    if (entity.id != expected_id) {
        throw ProjectRequestError(
            "graph entity document ID does not match operation "
            "entity_id");
    }
    const auto existing =
        find_entity(entities, expected_id);
    if (existing == entities.end()) {
        entities.push_back(std::move(entity));
    } else {
        *existing = std::move(entity);
    }
}

template <typename Entity>
void remove_entity(
    std::vector<Entity>& entities,
    const std::string& id,
    const std::string& type) {
    const auto existing = find_entity(entities, id);
    if (existing == entities.end()) {
        throw ProjectRequestError(
            type + " '" + id + "' does not exist");
    }
    entities.erase(existing);
}

std::string endpoint_component_id(const std::string& endpoint) {
    const auto separator = endpoint.find('.');
    return separator == std::string::npos
        ? endpoint
        : endpoint.substr(0, separator);
}

std::map<std::string, platform::ScalarValue>&
case_scalar_values(
    platform::CaseDefinition& simulation_case,
    CaseEditField field) {
    switch (field) {
        case CaseEditField::parameter_override:
            return simulation_case.parameter_overrides;
        case CaseEditField::fixed_value:
            return simulation_case.fixed_values;
        case CaseEditField::initial_guess:
            return simulation_case.initial_guesses;
        case CaseEditField::solver_option:
            return simulation_case.solver_options;
        case CaseEditField::label:
        case CaseEditField::mode:
            throw ProjectRequestError(
                "case metadata is not a scalar field");
    }
    throw ProjectRequestError(
        "unknown case edit field");
}

std::string checksum(std::string_view value) {
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>
        context{EVP_MD_CTX_new(), EVP_MD_CTX_free};
    if (!context ||
        EVP_DigestInit_ex(
            context.get(), EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(
            context.get(), value.data(), value.size()) != 1) {
        throw std::runtime_error(
            "could not initialize model SHA-256 checksum");
    }
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int digest_size = 0;
    if (EVP_DigestFinal_ex(
            context.get(), digest, &digest_size) != 1) {
        throw std::runtime_error(
            "could not finalize model SHA-256 checksum");
    }
    std::ostringstream encoded;
    encoded << "sha256:";
    for (unsigned int index = 0; index < digest_size; ++index) {
        encoded << std::hex << std::setfill('0')
                << std::setw(2)
                << static_cast<unsigned int>(digest[index]);
    }
    return encoded.str();
}

std::string canonical_topology_presentation(
    const std::string& payload,
    const platform::ModelDocument& model) {
    if (payload.empty() || payload.size() > 2U * 1024U * 1024U) {
        throw ProjectRequestError(
            "topology presentation must contain 1 byte to 2 MiB");
    }
    nlohmann::json value;
    try {
        value = nlohmann::json::parse(payload);
    } catch (const std::exception& error) {
        throw ProjectRequestError(
            std::string("invalid topology presentation JSON: ") +
            error.what());
    }
    if (!value.is_object()) {
        throw ProjectRequestError(
            "topology presentation must be a JSON object");
    }
    const std::set<std::string> root_fields = {
        "schema_version", "nodes", "viewport"};
    for (const auto& [key, unused] : value.items()) {
        (void)unused;
        if (!root_fields.contains(key)) {
            throw ProjectRequestError(
                "unknown topology presentation field: " + key);
        }
    }
    if (value.value("schema_version", std::string{}) !=
        topology_presentation_schema_v1) {
        throw ProjectRequestError(
            "unsupported topology presentation schema_version");
    }
    if (!value.contains("nodes") || !value["nodes"].is_array() ||
        value["nodes"].size() > 10000U) {
        throw ProjectRequestError(
            "topology presentation nodes must be an array of at most "
            "10000 entries");
    }
    std::set<std::string> entity_ids;
    for (const auto& component : model.components) {
        entity_ids.insert(component.id);
    }
    for (const auto& assembly : model.assemblies) {
        entity_ids.insert(assembly.id);
    }
    std::set<std::string> positioned;
    for (const auto& node : value["nodes"]) {
        if (!node.is_object() || node.size() != 3U ||
            !node.contains("entity_id") || !node["entity_id"].is_string() ||
            !node.contains("x") || !node["x"].is_number() ||
            !node.contains("y") || !node["y"].is_number()) {
            throw ProjectRequestError(
                "each topology presentation node requires entity_id, x, "
                "and y");
        }
        const auto entity_id = node["entity_id"].get<std::string>();
        const double x = node["x"].get<double>();
        const double y = node["y"].get<double>();
        if (!entity_ids.contains(entity_id)) {
            throw ProjectRequestError(
                "topology presentation references unknown entity: " +
                entity_id);
        }
        if (!positioned.insert(entity_id).second) {
            throw ProjectRequestError(
                "topology presentation repeats entity: " + entity_id);
        }
        if (!std::isfinite(x) || !std::isfinite(y) ||
            std::abs(x) > 1.0e7 || std::abs(y) > 1.0e7) {
            throw ProjectRequestError(
                "topology presentation node coordinates are out of range");
        }
    }
    if (!value.contains("viewport") || !value["viewport"].is_object()) {
        throw ProjectRequestError(
            "topology presentation requires a viewport object");
    }
    const auto& viewport = value["viewport"];
    if (viewport.size() != 3U ||
        !viewport.contains("x") || !viewport["x"].is_number() ||
        !viewport.contains("y") || !viewport["y"].is_number() ||
        !viewport.contains("zoom") || !viewport["zoom"].is_number()) {
        throw ProjectRequestError(
            "topology presentation viewport requires x, y, and zoom");
    }
    const double x = viewport["x"].get<double>();
    const double y = viewport["y"].get<double>();
    const double zoom = viewport["zoom"].get<double>();
    if (!std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(zoom) || std::abs(x) > 1.0e7 ||
        std::abs(y) > 1.0e7 || zoom < 0.05 || zoom > 10.0) {
        throw ProjectRequestError(
            "topology presentation viewport is out of range");
    }
    return value.dump();
}

std::string canonical_topology_draft(
    const std::string& schema_version,
    const std::string& artifact_id,
    const std::string& payload) {
    if (schema_version != topology_draft_schema_v1) {
        throw ProjectRequestError(
            "unsupported topology-draft schema version: " +
            schema_version);
    }
    if (payload.empty() || payload.size() > 2U * 1024U * 1024U) {
        throw ProjectRequestError(
            "topology draft must contain 1 byte to 2 MiB");
    }
    const auto value = nlohmann::json::parse(payload);
    if (!value.is_object()) {
        throw ProjectRequestError(
            "topology draft must be a JSON object");
    }
    const std::set<std::string> fields = {
        "schema_version", "id", "label", "document"};
    for (const auto& [key, unused] : value.items()) {
        (void)unused;
        if (!fields.contains(key)) {
            throw ProjectRequestError(
                "unknown topology-draft field: " + key);
        }
    }
    if (value.value("schema_version", std::string{}) !=
        topology_draft_schema_v1) {
        throw ProjectRequestError(
            "unsupported topology-draft schema_version");
    }
    if (!value.contains("id") || !value["id"].is_string() ||
        value["id"].get<std::string>().empty() ||
        value["id"].get<std::string>() != artifact_id) {
        throw ProjectRequestError(
            "topology-draft ID must be non-empty and match the "
            "project artifact ID");
    }
    if (value.contains("label") && !value["label"].is_string()) {
        throw ProjectRequestError(
            "topology-draft label must be a string");
    }
    if (!value.contains("document") || !value["document"].is_object()) {
        throw ProjectRequestError(
            "topology-draft document must be a JSON object");
    }
    return value.dump();
}

void append_topology_draft_issue(
    TopologyDraftPromotionReview& review,
    std::string code,
    std::string json_path,
    std::string message,
    std::vector<std::string> suggestions) {
    review.issues.push_back({
        std::move(code),
        DiagnosticSeverity::error,
        "draft",
        std::move(json_path),
        {}, {}, {},
        std::move(message),
        std::move(suggestions),
    });
}

bool non_empty_json_string(const nlohmann::json& value) {
    return value.is_string() &&
        !trim(value.get<std::string>()).empty();
}

const nlohmann::json* topology_array_field(
    const nlohmann::json& model,
    const std::string& field,
    bool required,
    TopologyDraftPromotionReview& review) {
    if (!model.contains(field)) {
        if (required) {
            append_topology_draft_issue(
                review,
                "topology_required_array_missing",
                "/model/" + field,
                "model." + field + " must be an array",
                {"Add an empty array while drafting, then populate it "
                 "before calculation when required."});
        }
        return nullptr;
    }
    if (!model[field].is_array()) {
        append_topology_draft_issue(
            review,
            "topology_array_type_invalid",
            "/model/" + field,
            "model." + field + " must be an array",
            {"Replace this value with a JSON array."});
        return nullptr;
    }
    return &model[field];
}

std::set<std::string> topology_entity_ids(
    const nlohmann::json* entities,
    const std::string& field,
    TopologyDraftPromotionReview& review) {
    std::set<std::string> ids;
    if (entities == nullptr) return ids;
    for (std::size_t index = 0; index < entities->size(); ++index) {
        const auto& entity = (*entities)[index];
        const auto path = "/model/" + field + "/" +
            std::to_string(index) + "/id";
        if (!entity.is_object() || !entity.contains("id") ||
            !non_empty_json_string(entity["id"])) {
            append_topology_draft_issue(
                review,
                "topology_entity_id_missing",
                path,
                "model." + field + "[" + std::to_string(index) +
                    "].id must be a non-empty string",
                {"Assign a stable ID unique within the topology."});
            continue;
        }
        const auto id = entity["id"].get<std::string>();
        if (!ids.insert(id).second) {
            append_topology_draft_issue(
                review,
                "topology_entity_id_duplicate",
                path,
                "model." + field + " repeats ID '" + id + "'",
                {"Rename one entity and update references to it."});
        }
    }
    return ids;
}

void preflight_topology_draft_document(
    const nlohmann::json& document,
    TopologyDraftPromotionReview& review) {
    if (!document.contains("schema_version") ||
        document["schema_version"] != "thermox.topology/v1") {
        append_topology_draft_issue(
            review,
            "topology_schema_invalid",
            "/schema_version",
            "schema_version must be thermox.topology/v1",
            {"Set schema_version to 'thermox.topology/v1'."});
    }
    if (!document.contains("model") ||
        !document["model"].is_object()) {
        append_topology_draft_issue(
            review,
            "topology_model_missing",
            "/model",
            "model must be a JSON object",
            {"Add the physical-system declaration under model."});
        return;
    }
    const auto& model = document["model"];
    for (const std::string field : {"id", "name", "revision"}) {
        if (!model.contains(field) ||
            !non_empty_json_string(model[field])) {
            append_topology_draft_issue(
                review,
                "topology_model_field_missing",
                "/model/" + field,
                "model." + field + " must be a non-empty string",
                {"Provide a stable model " + field + "."});
        }
    }
    if (model.contains("id") && non_empty_json_string(model["id"])) {
        review.model_id = model["id"].get<std::string>();
    }

    const auto* media = topology_array_field(
        model, "media", true, review);
    const auto* materials = topology_array_field(
        model, "materials", false, review);
    const auto* components = topology_array_field(
        model, "components", true, review);
    const auto* assemblies = topology_array_field(
        model, "assemblies", false, review);
    const auto* connections = topology_array_field(
        model, "connections", true, review);
    review.medium_count = media == nullptr ? 0U : media->size();
    review.material_count =
        materials == nullptr ? 0U : materials->size();
    review.component_count =
        components == nullptr ? 0U : components->size();
    review.assembly_count =
        assemblies == nullptr ? 0U : assemblies->size();
    review.connection_count =
        connections == nullptr ? 0U : connections->size();

    (void)topology_entity_ids(media, "media", review);
    (void)topology_entity_ids(materials, "materials", review);
    auto component_ids =
        topology_entity_ids(components, "components", review);
    const auto assembly_ids =
        topology_entity_ids(assemblies, "assemblies", review);
    (void)topology_entity_ids(connections, "connections", review);
    for (const auto& id : assembly_ids) {
        if (component_ids.contains(id)) {
            append_topology_draft_issue(
                review,
                "topology_entity_namespace_ambiguous",
                "/model/assemblies",
                "component and assembly share top-level ID '" + id + "'",
                {"Use distinct top-level component and assembly IDs."});
        }
        component_ids.insert(id);
    }
    if (components != nullptr) {
        for (std::size_t index = 0; index < components->size(); ++index) {
            const auto& component = (*components)[index];
            if (!component.is_object() || !component.contains("kind") ||
                !non_empty_json_string(component["kind"])) {
                append_topology_draft_issue(
                    review,
                    "topology_component_kind_missing",
                    "/model/components/" + std::to_string(index) +
                        "/kind",
                    "component kind must be a non-empty string",
                    {"Select a registered component kind."});
            }
        }
    }
    if (connections == nullptr) return;
    for (std::size_t index = 0; index < connections->size(); ++index) {
        const auto& connection = (*connections)[index];
        if (!connection.is_object()) continue;
        for (const std::string field : {"from", "to", "kind"}) {
            if (!connection.contains(field) ||
                !non_empty_json_string(connection[field])) {
                append_topology_draft_issue(
                    review,
                    "topology_connection_field_missing",
                    "/model/connections/" + std::to_string(index) +
                        "/" + field,
                    "connection " + field +
                        " must be a non-empty string",
                    {"Connect registered ports using entity.port syntax."});
            }
        }
        for (const std::string field : {"from", "to"}) {
            if (!connection.contains(field) ||
                !non_empty_json_string(connection[field])) continue;
            const auto endpoint = connection[field].get<std::string>();
            const auto separator = endpoint.find('.');
            const auto entity = separator == std::string::npos
                ? std::string{}
                : endpoint.substr(0, separator);
            if (entity.empty() || !component_ids.contains(entity)) {
                append_topology_draft_issue(
                    review,
                    "topology_connection_endpoint_unknown",
                    "/model/connections/" + std::to_string(index) +
                        "/" + field,
                    "connection endpoint '" + endpoint +
                        "' does not reference a top-level entity",
                    {"Use an existing component or assembly export ID."});
            }
        }
    }
}

std::string run_mode(const std::string& case_mode) {
    if (case_mode.find("transient") != std::string::npos ||
        case_mode.find("dynamic") != std::string::npos) {
        return "transient";
    }
    if (case_mode.find("steady") != std::string::npos ||
        case_mode == "design" ||
        case_mode == "off_design") {
        return "steady";
    }
    throw ProjectRequestError(
        "case mode is not executable: " + case_mode);
}

void validate_steady_solver(
    const SteadySolverSettings& value) {
    switch (value.globalization_policy) {
    case GlobalizationPolicy::line_search:
    case GlobalizationPolicy::trust_region:
        break;
    default:
        throw ProjectRequestError(
            "invalid steady solver globalization policy");
    }
    if (value.max_iterations <= 0 ||
        value.max_line_search_steps <= 0 ||
        value.max_trust_region_steps <= 0 ||
        !std::isfinite(value.residual_tolerance) ||
        value.residual_tolerance <= 0.0 ||
        !std::isfinite(value.step_tolerance) ||
        value.step_tolerance <= 0.0 ||
        !std::isfinite(value.linear_residual_tolerance) ||
        value.linear_residual_tolerance <= 0.0 ||
        !std::isfinite(value.finite_difference_epsilon) ||
        value.finite_difference_epsilon <= 0.0 ||
        !std::isfinite(value.min_damping) ||
        value.min_damping <= 0.0 ||
        !std::isfinite(value.damping_reduction) ||
        value.damping_reduction <= 0.0 ||
        value.damping_reduction >= 1.0 ||
        !std::isfinite(value.sufficient_decrease) ||
        value.sufficient_decrease <= 0.0 ||
        !std::isfinite(value.trust_region_initial_radius) ||
        value.trust_region_initial_radius <= 0.0 ||
        !std::isfinite(value.trust_region_minimum_radius) ||
        value.trust_region_minimum_radius <= 0.0 ||
        value.trust_region_minimum_radius >
            value.trust_region_initial_radius ||
        !std::isfinite(value.trust_region_maximum_radius) ||
        value.trust_region_maximum_radius <
            value.trust_region_initial_radius ||
        !std::isfinite(value.trust_region_acceptance_threshold) ||
        value.trust_region_acceptance_threshold < 0.0 ||
        value.trust_region_acceptance_threshold >= 1.0 ||
        !std::isfinite(value.continuation_initial_step) ||
        value.continuation_initial_step <= 0.0 ||
        value.continuation_initial_step > 1.0 ||
        !std::isfinite(value.continuation_minimum_step) ||
        value.continuation_minimum_step <= 0.0 ||
        value.continuation_minimum_step >
            value.continuation_initial_step ||
        !std::isfinite(value.continuation_step_growth) ||
        value.continuation_step_growth <= 1.0 ||
        !std::isfinite(value.continuation_step_reduction) ||
        value.continuation_step_reduction <= 0.0 ||
        value.continuation_step_reduction >= 1.0 ||
        value.continuation_maximum_stages <= 0) {
        throw ProjectRequestError(
            "invalid steady solver settings");
    }
}

void validate_transient_solver(
    const TransientSolverSettings& value) {
    validate_steady_solver(value.nonlinear_solver);
    if (!std::isfinite(value.start_time) ||
        !std::isfinite(value.end_time) ||
        value.end_time <= value.start_time ||
        !std::isfinite(value.initial_step) ||
        value.initial_step <= 0.0 ||
        !std::isfinite(value.min_step) ||
        value.min_step <= 0.0 ||
        !std::isfinite(value.max_step) ||
        value.max_step < value.min_step ||
        !std::isfinite(value.absolute_tolerance) ||
        value.absolute_tolerance <= 0.0 ||
        !std::isfinite(value.relative_tolerance) ||
        value.relative_tolerance <= 0.0 ||
        value.max_steps <= 0 ||
        value.max_consecutive_rejections <= 0 ||
        value.maximum_order < 1 || value.maximum_order > 2) {
        throw ProjectRequestError(
            "invalid transient solver settings");
    }
    double previous = -std::numeric_limits<double>::infinity();
    for (const double output_time : value.required_output_times) {
        if (!std::isfinite(output_time) ||
            output_time < value.start_time ||
            output_time > value.end_time || output_time <= previous) {
            throw ProjectRequestError(
                "invalid transient required output times");
        }
        previous = output_time;
    }
}

void append_steady(
    std::ostream& out,
    const SteadySolverSettings& value) {
    out << value.max_iterations << '|'
        << value.residual_tolerance << '|'
        << value.step_tolerance << '|'
        << value.linear_residual_tolerance << '|'
        << to_string(value.structural_decomposition_policy) << '|'
        << value.finite_difference_epsilon << '|'
        << value.min_damping << '|'
        << value.damping_reduction << '|'
        << value.sufficient_decrease << '|'
        << value.max_line_search_steps << '|'
        << to_string(value.globalization_policy) << '|'
        << value.trust_region_initial_radius << '|'
        << value.trust_region_minimum_radius << '|'
        << value.trust_region_maximum_radius << '|'
        << value.trust_region_acceptance_threshold << '|'
        << value.max_trust_region_steps << '|'
        << value.continuation_enabled << '|'
        << value.continuation_initial_step << '|'
        << value.continuation_minimum_step << '|'
        << value.continuation_step_growth << '|'
        << value.continuation_step_reduction << '|'
        << value.continuation_maximum_stages;
}

std::string run_configuration_identity(
    const CreateRunConfigurationRevisionRequest& request) {
    std::ostringstream out;
    out << std::setprecision(17)
        << request.run_configuration_id << '|'
        << request.study_revision_id << '|';
    append_steady(out, request.steady_solver);
    const auto& transient = request.transient_solver;
    out << '|' << transient.start_time
        << '|' << transient.end_time
        << '|' << transient.initial_step
        << '|' << transient.min_step
        << '|' << transient.max_step
        << '|' << transient.absolute_tolerance
        << '|' << transient.relative_tolerance
        << '|' << transient.max_steps
        << '|' << transient.max_consecutive_rejections
        << '|' << transient.maximum_order
        << '|' <<
            transient.compute_consistent_initial_conditions
        << '|' << transient.required_output_times.size() << '|';
    for (const double output_time :
         transient.required_output_times) {
        out << output_time << '|';
    }
    append_steady(out, transient.nonlinear_solver);
    return out.str();
}

std::string study_identity(
    const CreateStudyRevisionRequest& request,
    const std::vector<std::string>& artifacts,
    const std::vector<ArtifactQualificationRequirement>&
        qualification_requirements,
    const std::vector<ArtifactOperatingEnvelope>&
        operating_envelopes) {
    std::ostringstream out;
    out << request.study_id.size() << ':' << request.study_id << '|'
        << request.model_revision_id.size() << ':'
        << request.model_revision_id << '|'
        << request.case_revision_id.size() << ':'
        << request.case_revision_id << '|'
        << request.intent.size() << ':' << request.intent << '|';
    for (const auto& id : artifacts) {
        out << id.size() << ':' << id << '|';
    }
    out << qualification_requirements.size() << '|';
    for (const auto& requirement : qualification_requirements) {
        out << requirement.artifact_revision_id.size() << ':'
            << requirement.artifact_revision_id << '|'
            << requirement.review_id.size() << ':'
            << requirement.review_id << '|'
            << requirement.acceptable_dispositions.size() << '|';
        for (const auto disposition :
             requirement.acceptable_dispositions) {
            const auto value = to_string(disposition);
            out << value.size() << ':' << value << '|';
        }
    }
    out << operating_envelopes.size() << '|';
    for (const auto& envelope : operating_envelopes) {
        out << envelope.artifact_revision_id.size() << ':'
            << envelope.artifact_revision_id << '|'
            << envelope.coordinates.size() << '|';
        for (const auto& coordinate : envelope.coordinates) {
            out << coordinate.coordinate.size() << ':'
                << coordinate.coordinate << '|'
                << coordinate.dimension.size() << ':'
                << coordinate.dimension << '|'
                << coordinate.minimum.has_value() << '|';
            if (coordinate.minimum) out << *coordinate.minimum << '|';
            out << coordinate.maximum.has_value() << '|';
            if (coordinate.maximum) out << *coordinate.maximum << '|';
            out << coordinate.minimum_inclusive << '|'
                << coordinate.maximum_inclusive << '|';
        }
    }
    out << request.result_projections.size() << '|';
    for (const auto& projection : request.result_projections) {
        const auto append = [&](const std::string& value) {
            out << value.size() << ':' << value << '|';
        };
        append(projection.id);
        append(to_string(projection.scope));
        append(projection.component_id);
        append(projection.port_name);
        append(projection.value_name);
        append(projection.dimension);
        append(to_string(projection.aggregation));
        out << projection.window.has_value() << '|';
        if (projection.window) {
            append(to_string(projection.window->anchor));
            out << projection.window->start_time << '|'
                << projection.window->end_time << '|';
            append(projection.window->event_name);
            out << projection.window->event_occurrence << '|';
        }
    }
    out << request.acceptance_criteria.size() << '|';
    for (const auto& criterion : request.acceptance_criteria) {
        const auto append = [&](const std::string& value) {
            out << value.size() << ':' << value << '|';
        };
        append(criterion.id);
        append(criterion.projection_id);
        append(criterion.dimension);
        out << criterion.lower_bound_si.has_value() << '|';
        if (criterion.lower_bound_si) {
            out << *criterion.lower_bound_si << '|';
        }
        out << criterion.upper_bound_si.has_value() << '|';
        if (criterion.upper_bound_si) {
            out << *criterion.upper_bound_si << '|';
        }
        out << criterion.lower_inclusive << '|'
            << criterion.upper_inclusive << '|';
    }
    out << request.trajectory_validation_bindings.size() << '|';
    for (const auto& binding :
         request.trajectory_validation_bindings) {
        const auto append = [&](const std::string& value) {
            out << value.size() << ':' << value << '|';
        };
        append(binding.id);
        append(binding.artifact_revision_id);
        append(binding.signal_id);
        append(binding.projection_id);
        append(to_string(binding.comparison));
        out << binding.time_offset_si << '|'
            << binding.baseline_time_si << '|'
            << binding.absolute_tolerance_si << '|'
            << binding.relative_tolerance << '|'
            << binding.uncertainty_multiplier << '|'
            << binding.maximum_interpolation_gap_si << '|';
    }
    return out.str();
}

void validate_calibration_solver(
    const CalibrationSolverSettings& value) {
    validate_steady_solver(value.steady_simulation_solver);
    validate_transient_solver(value.transient_simulation_solver);
    if (value.max_iterations <= 0 ||
        !std::isfinite(value.finite_difference_fraction) ||
        value.finite_difference_fraction <= 0.0 ||
        value.finite_difference_fraction >= 1.0 ||
        !std::isfinite(value.initial_trust_region_radius) ||
        value.initial_trust_region_radius <= 0.0 ||
        !std::isfinite(value.minimum_trust_region_radius) ||
        value.minimum_trust_region_radius <= 0.0 ||
        value.minimum_trust_region_radius >=
            value.initial_trust_region_radius ||
        !std::isfinite(value.maximum_trust_region_radius) ||
        value.maximum_trust_region_radius <
            value.initial_trust_region_radius ||
        !std::isfinite(value.acceptance_ratio) ||
        value.acceptance_ratio < 0.0 || value.acceptance_ratio >= 1.0 ||
        !std::isfinite(value.gradient_tolerance) ||
        value.gradient_tolerance <= 0.0 ||
        !std::isfinite(value.step_tolerance) ||
        value.step_tolerance <= 0.0 ||
        !std::isfinite(value.objective_relative_tolerance) ||
        value.objective_relative_tolerance <= 0.0 ||
        !std::isfinite(value.minimum_continuation_fraction) ||
        value.minimum_continuation_fraction <= 0.0 ||
        value.minimum_continuation_fraction > 1.0 ||
        !std::isfinite(value.continuation_growth) ||
        value.continuation_growth <= 1.0) {
        throw ProjectRequestError("invalid calibration solver settings");
    }
}

std::string calibration_identity(
    const CreateCalibrationRevisionRequest& request,
    const std::vector<std::string>& training,
    const std::vector<std::string>& validation,
    const std::string& definition_json) {
    std::ostringstream out;
    const auto append = [&](const std::string& value) {
        out << value.size() << ':' << value << '|';
    };
    append(request.calibration_id);
    append(request.model_revision_id);
    for (const auto& id : training) append(id);
    out << "validation|";
    for (const auto& id : validation) append(id);
    append(definition_json);
    const auto& solver = request.solver;
    out << std::setprecision(17) << solver.max_iterations << '|'
        << solver.finite_difference_fraction << '|'
        << solver.initial_trust_region_radius << '|'
        << solver.minimum_trust_region_radius << '|'
        << solver.maximum_trust_region_radius << '|'
        << solver.acceptance_ratio << '|'
        << solver.gradient_tolerance << '|'
        << solver.step_tolerance << '|'
        << solver.objective_relative_tolerance << '|'
        << solver.minimum_continuation_fraction << '|'
        << solver.continuation_growth << '|';
    append_steady(out, solver.steady_simulation_solver);
    const auto& transient = solver.transient_simulation_solver;
    out << '|' << transient.start_time
        << '|' << transient.end_time
        << '|' << transient.initial_step
        << '|' << transient.min_step
        << '|' << transient.max_step
        << '|' << transient.absolute_tolerance
        << '|' << transient.relative_tolerance
        << '|' << transient.max_steps
        << '|' << transient.max_consecutive_rejections
        << '|' << transient.maximum_order
        << '|' << transient.compute_consistent_initial_conditions
        << '|' << transient.required_output_times.size() << '|';
    for (const double output_time :
         transient.required_output_times) {
        out << output_time << '|';
    }
    append_steady(out, transient.nonlinear_solver);
    return out.str();
}

void validate_reconciliation_solver(
    const ReconciliationSolverSettings& value,
    const ProfileLikelihoodSettings& profile,
    const JointConfidenceRegionSettings& joint_region) {
    validate_steady_solver(value.simulation_solver);
    if (value.max_iterations <= 0 ||
        !std::isfinite(value.finite_difference_fraction) ||
        value.finite_difference_fraction <= 0.0 ||
        value.finite_difference_fraction >= 1.0 ||
        !std::isfinite(value.constraint_tolerance) ||
        value.constraint_tolerance <= 0.0 ||
        !std::isfinite(value.step_tolerance) ||
        value.step_tolerance <= 0.0 ||
        !std::isfinite(value.objective_relative_tolerance) ||
        value.objective_relative_tolerance <= 0.0 ||
        !std::isfinite(value.minimum_line_search_fraction) ||
        value.minimum_line_search_fraction <= 0.0 ||
        value.minimum_line_search_fraction >= 1.0 ||
        (profile.enabled &&
         (!std::isfinite(profile.objective_increase) ||
          profile.objective_increase <= 0.0 ||
          profile.maximum_bracket_steps <= 0 ||
          profile.maximum_bisection_steps <= 0 ||
          profile.maximum_nuisance_iterations <= 0)) ||
        (joint_region.enabled &&
         (!std::isfinite(joint_region.objective_increase) ||
          joint_region.objective_increase <= 0.0))) {
        throw ProjectRequestError(
            "invalid reconciliation solver settings");
    }
}

std::string reconciliation_identity(
    const CreateReconciliationRevisionRequest& request,
    const std::vector<std::string>& constraints,
    const std::vector<std::string>& held_out,
    const std::string& definition_json) {
    std::ostringstream out;
    const auto append = [&](const std::string& value) {
        out << value.size() << ':' << value << '|';
    };
    append(request.reconciliation_id);
    append(request.model_revision_id);
    append(to_string(request.mode));
    for (const auto& id : constraints) append(id);
    out << "held_out|";
    for (const auto& id : held_out) append(id);
    append(definition_json);
    const auto& solver = request.solver;
    out << std::setprecision(17)
        << solver.max_iterations << '|'
        << solver.finite_difference_fraction << '|'
        << solver.constraint_tolerance << '|'
        << solver.step_tolerance << '|'
        << solver.objective_relative_tolerance << '|'
        << solver.minimum_line_search_fraction << '|';
    append_steady(out, solver.simulation_solver);
    const auto& profile = request.profile_likelihood;
    out << profile.enabled << '|'
        << profile.objective_increase << '|'
        << profile.maximum_bracket_steps << '|'
        << profile.maximum_bisection_steps << '|'
        << profile.maximum_nuisance_iterations << '|';
    for (const auto& id : profile.parameter_ids) append(id);
    out << "joint_region|" << request.joint_confidence_region.enabled
        << '|' << request.joint_confidence_region.objective_increase
        << '|';
    for (const auto& id :
         request.joint_confidence_region.parameter_ids) append(id);
    return out.str();
}

}  // namespace

std::string to_string(
    EngineeringReviewDisposition disposition) {
    switch (disposition) {
        case EngineeringReviewDisposition::approved:
            return "approved";
        case EngineeringReviewDisposition::approved_with_conditions:
            return "approved_with_conditions";
        case EngineeringReviewDisposition::rejected:
            return "rejected";
    }
    return "unknown";
}

ProjectService::ProjectService(
    std::shared_ptr<ProjectRepository> repository)
    : ProjectService(
          std::move(repository),
          make_in_memory_engineering_artifact_content_store()) {}

ProjectService::ProjectService(
    std::shared_ptr<ProjectRepository> repository,
    std::shared_ptr<EngineeringArtifactContentStore>
        artifact_content)
    : ProjectService(
          std::move(repository),
          std::move(artifact_content),
          platform::make_default_unit_registry()) {}

ProjectService::ProjectService(
    std::shared_ptr<ProjectRepository> repository,
    std::shared_ptr<EngineeringArtifactContentStore>
        artifact_content,
    platform::UnitRegistry units)
    : repository_(std::move(repository)),
      artifact_content_(std::move(artifact_content)),
      units_(std::move(units)) {
    if (!repository_) {
        throw std::invalid_argument(
            "project repository must not be null");
    }
    if (!artifact_content_) {
        throw std::invalid_argument(
            "engineering artifact content store must not be "
            "null");
    }
}

ProjectRecord ProjectService::create_project(
    const CreateProjectRequest& request) const {
    require_identity(request.identity);
    const auto name = trim(request.name);
    const auto description = trim(request.description);
    if (name.empty() || name.size() > 200U) {
        throw ProjectRequestError(
            "project name must contain 1 to 200 characters");
    }
    if (description.size() > 4000U) {
        throw ProjectRequestError(
            "project description exceeds 4000 characters");
    }
    return repository_->create_project(
        request.identity.team_id,
        request.identity.user_id,
        name,
        description);
}

std::optional<ProjectRecord> ProjectService::get_project(
    const IdentityContext& identity,
    const std::string& project_id) const {
    require_identity(identity);
    if (project_id.empty()) {
        throw ProjectRequestError(
            "project ID must not be empty");
    }
    return repository_->get_project(
        identity.team_id, project_id);
}

std::vector<ProjectRecord> ProjectService::list_projects(
    const IdentityContext& identity) const {
    require_identity(identity);
    return repository_->list_projects(identity.team_id);
}

ModelRevisionRecord ProjectService::create_model_revision(
    const CreateModelRevisionRequest& request) const {
    require_identity(request.identity);
    if (request.project_id.empty()) {
        throw ProjectRequestError(
            "project ID must not be empty");
    }
    if (request.model_json.empty()) {
        throw ProjectRequestError(
            "model document must not be empty");
    }

    platform::ModelDocument document;
    std::string canonical;
    try {
        document = platform::parse_topology_document_text(
            request.model_json, units_);
        canonical =
            detail::serialize_topology_document_json(document);
    } catch (const std::exception& error) {
        throw ProjectRequestError(
            std::string("invalid model document: ") +
            error.what());
    }
    return repository_->create_model_revision(
        request.identity.team_id,
        request.identity.user_id,
        request.project_id,
        request.parent_model_revision_id,
        document.schema_version,
        document.model_id,
        document.revision,
        "",
        "",
        canonical,
        checksum(canonical));
}

TopologyDraftPromotionReview ProjectService::review_topology_draft(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string& artifact_revision_id) const {
    require_identity(identity);
    if (project_id.empty() || artifact_revision_id.empty()) {
        throw ProjectRequestError(
            "project and topology draft revision IDs must not be empty");
    }
    const auto content = get_artifact_revision_content(
        identity, project_id, artifact_revision_id);
    if (!content) {
        throw ProjectStateError("topology draft revision was not found");
    }
    if (content->revision.artifact_type != topology_draft_artifact_type ||
        content->revision.artifact_schema_version !=
            topology_draft_schema_v1) {
        throw ProjectRequestError(
            "artifact revision is not a topology draft");
    }

    TopologyDraftPromotionReview review;
    review.project_id = project_id;
    review.artifact_revision_id = artifact_revision_id;
    review.artifact_checksum = content->revision.content.checksum;
    try {
        const auto wrapper =
            nlohmann::json::parse(content->canonical_artifact_json);
        const auto& draft_document = wrapper.at("document");
        preflight_topology_draft_document(draft_document, review);
        if (!review.issues.empty()) return review;
        const auto model_json = draft_document.dump();
        const auto document = platform::parse_topology_document_text(
            model_json, units_);
        (void)platform::flatten_model_document(document);
        review.promotable = true;
        review.model_id = document.model_id;
        review.medium_count = document.media.size();
        review.material_count = document.materials.size();
        review.component_count = document.components.size();
        review.assembly_count = document.assemblies.size();
        review.connection_count = document.connections.size();
    } catch (const std::exception& error) {
        append_topology_draft_issue(
            review,
            "topology_contract_invalid",
            {},
            error.what(),
            {"Correct the declaration at the reported model field and "
             "save a new immutable draft revision."});
    }
    return review;
}

ModelRevisionRecord ProjectService::promote_topology_draft(
    const PromoteTopologyDraftRequest& request) const {
    const auto review = review_topology_draft(
        request.identity,
        request.project_id,
        request.artifact_revision_id);
    if (!review.promotable) {
        throw ProjectRequestError(
            "topology draft is not promotable: " +
            (review.issues.empty()
                 ? std::string("unknown topology contract error")
                 : review.issues.front().message));
    }
    const auto content = get_artifact_revision_content(
        request.identity,
        request.project_id,
        request.artifact_revision_id);
    if (!content) {
        throw ProjectStateError("topology draft revision was not found");
    }
    platform::ModelDocument document;
    std::string canonical;
    try {
        const auto wrapper =
            nlohmann::json::parse(content->canonical_artifact_json);
        document = platform::parse_topology_document_text(
            wrapper.at("document").dump(), units_);
        (void)platform::flatten_model_document(document);
        canonical =
            detail::serialize_topology_document_json(document);
    } catch (const std::exception& error) {
        throw ProjectRequestError(
            std::string("topology draft promotion failed: ") +
            error.what());
    }
    return repository_->create_model_revision(
        request.identity.team_id,
        request.identity.user_id,
        request.project_id,
        request.parent_model_revision_id,
        document.schema_version,
        document.model_id,
        document.revision,
        request.artifact_revision_id,
        content->revision.content.checksum,
        canonical,
        checksum(canonical));
}

std::optional<ModelRevisionRecord>
ProjectService::get_model_revision(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string& model_revision_id) const {
    require_identity(identity);
    if (project_id.empty() || model_revision_id.empty()) {
        throw ProjectRequestError(
            "project and model revision IDs must not be empty");
    }
    return repository_->get_model_revision(
        identity.team_id,
        project_id,
        model_revision_id);
}

std::vector<ModelRevisionRecord>
ProjectService::list_model_revisions(
    const IdentityContext& identity,
    const std::string& project_id) const {
    require_identity(identity);
    if (project_id.empty()) {
        throw ProjectRequestError(
            "project ID must not be empty");
    }
    return repository_->list_model_revisions(
        identity.team_id, project_id);
}

TopologyPresentationRecord
ProjectService::put_topology_presentation(
    const PutTopologyPresentationRequest& request) const {
    require_identity(request.identity);
    if (request.project_id.empty() || request.model_revision_id.empty()) {
        throw ProjectRequestError(
            "project and model revision IDs must not be empty");
    }
    const auto revision = repository_->get_model_revision(
        request.identity.team_id,
        request.project_id,
        request.model_revision_id);
    if (!revision) {
        throw ProjectStateError("model revision was not found");
    }
    platform::ModelDocument model;
    try {
        model = platform::parse_topology_document_text(
            revision->canonical_model_json, units_);
    } catch (const std::exception& error) {
        throw ProjectStateError(
            std::string("persisted model revision is invalid: ") +
            error.what());
    }
    const auto canonical = canonical_topology_presentation(
        request.presentation_json, model);
    return repository_->upsert_topology_presentation(
        request.identity.team_id,
        request.identity.user_id,
        request.project_id,
        request.model_revision_id,
        canonical);
}

std::optional<TopologyPresentationRecord>
ProjectService::get_topology_presentation(
    const IdentityContext& identity,
    const std::string& project_id) const {
    require_identity(identity);
    if (project_id.empty()) {
        throw ProjectRequestError("project ID must not be empty");
    }
    if (!repository_->get_project(identity.team_id, project_id)) {
        return std::nullopt;
    }
    return repository_->get_topology_presentation(
        identity.team_id, identity.user_id, project_id);
}

ModelRevisionRecord ProjectService::apply_graph_edits(
    const ApplyGraphEditsRequest& request) const {
    require_identity(request.identity);
    if (request.project_id.empty() ||
        request.base_model_revision_id.empty()) {
        throw ProjectRequestError(
            "project and base model revision IDs must not be "
            "empty");
    }
    if (request.operations.empty() ||
        request.operations.size() > 256U) {
        throw ProjectRequestError(
            "graph edit batch must contain 1 to 256 operations");
    }
    const auto base = repository_->get_model_revision(
        request.identity.team_id,
        request.project_id,
        request.base_model_revision_id);
    if (!base) {
        throw ProjectStateError(
            "base model revision was not found");
    }

    platform::ModelDocument document;
    try {
        document = platform::parse_topology_document_text(
            base->canonical_model_json, units_);
        for (const auto& operation : request.operations) {
            if (operation.entity_id.empty()) {
                throw ProjectRequestError(
                    "graph edit entity_id must not be empty");
            }
            if (operation.action == GraphEditAction::upsert &&
                operation.entity_json.empty()) {
                throw ProjectRequestError(
                    "graph upsert requires an entity document");
            }
            if (operation.action == GraphEditAction::remove &&
                !operation.entity_json.empty()) {
                throw ProjectRequestError(
                    "graph removal must not contain an entity "
                    "document");
            }

            if (operation.action == GraphEditAction::upsert) {
                switch (operation.entity_type) {
                    case GraphEntityType::medium:
                        upsert_entity(
                            document.media,
                            platform::
                                parse_medium_definition_text(
                                    operation.entity_json),
                            operation.entity_id);
                        break;
                    case GraphEntityType::material:
                        upsert_entity(
                            document.materials,
                            platform::
                                parse_material_definition_text(
                                    operation.entity_json),
                            operation.entity_id);
                        break;
                    case GraphEntityType::component:
                        upsert_entity(
                            document.components,
                            platform::
                                parse_component_definition_text(
                                    operation.entity_json,
                                    document,
                                    units_),
                            operation.entity_id);
                        break;
                    case GraphEntityType::assembly:
                        upsert_entity(
                            document.assemblies,
                            platform::
                                parse_assembly_definition_text(
                                    operation.entity_json,
                                    document,
                                    units_),
                            operation.entity_id);
                        break;
                    case GraphEntityType::connection:
                        upsert_entity(
                            document.connections,
                            platform::
                                parse_connection_definition_text(
                                    operation.entity_json,
                                    units_),
                            operation.entity_id);
                        break;
                }
                continue;
            }

            switch (operation.entity_type) {
                case GraphEntityType::medium: {
                    const auto executable =
                        platform::flatten_model_document(document);
                    if (std::any_of(
                            executable.components.begin(),
                            executable.components.end(),
                            [&](const auto& component) {
                                return std::any_of(
                                    component.medium_bindings
                                        .begin(),
                                    component.medium_bindings
                                        .end(),
                                    [&](const auto& binding) {
                                        return binding.second ==
                                            operation.entity_id;
                                    });
                            })) {
                        throw ProjectRequestError(
                            "cannot remove a medium referenced by "
                            "a component");
                    }
                    remove_entity(
                        document.media,
                        operation.entity_id,
                        "medium");
                    break;
                }
                case GraphEntityType::material: {
                    const auto executable =
                        platform::flatten_model_document(document);
                    if (std::any_of(
                            executable.components.begin(),
                            executable.components.end(),
                            [&](const auto& component) {
                                return std::any_of(
                                    component.material_bindings
                                        .begin(),
                                    component.material_bindings
                                        .end(),
                                    [&](const auto& binding) {
                                        return binding.second ==
                                            operation.entity_id;
                                    });
                            })) {
                        throw ProjectRequestError(
                            "cannot remove a material referenced "
                            "by a component");
                    }
                    remove_entity(
                        document.materials,
                        operation.entity_id,
                        "material");
                    break;
                }
                case GraphEntityType::component: {
                    const auto attached = std::count_if(
                        document.connections.begin(),
                        document.connections.end(),
                        [&](const auto& connection) {
                            return endpoint_component_id(
                                       connection.from) ==
                                    operation.entity_id ||
                                endpoint_component_id(
                                    connection.to) ==
                                    operation.entity_id;
                        });
                    if (attached != 0 && !operation.cascade) {
                        throw ProjectRequestError(
                            "component removal requires cascade "
                            "when connections are attached");
                    }
                    if (operation.cascade) {
                        std::erase_if(
                            document.connections,
                            [&](const auto& connection) {
                                return endpoint_component_id(
                                           connection.from) ==
                                        operation.entity_id ||
                                    endpoint_component_id(
                                        connection.to) ==
                                        operation.entity_id;
                            });
                    }
                    remove_entity(
                        document.components,
                        operation.entity_id,
                        "component");
                    break;
                }
                case GraphEntityType::assembly: {
                    const auto attached = std::count_if(
                        document.connections.begin(),
                        document.connections.end(),
                        [&](const auto& connection) {
                            return endpoint_component_id(
                                       connection.from) ==
                                    operation.entity_id ||
                                endpoint_component_id(
                                    connection.to) ==
                                    operation.entity_id;
                        });
                    if (attached != 0 && !operation.cascade) {
                        throw ProjectRequestError(
                            "assembly removal requires cascade "
                            "when connections are attached");
                    }
                    if (operation.cascade) {
                        std::erase_if(
                            document.connections,
                            [&](const auto& connection) {
                                return endpoint_component_id(
                                           connection.from) ==
                                        operation.entity_id ||
                                    endpoint_component_id(
                                           connection.to) ==
                                        operation.entity_id;
                            });
                    }
                    remove_entity(
                        document.assemblies,
                        operation.entity_id,
                        "assembly");
                    break;
                }
                case GraphEntityType::connection:
                    remove_entity(
                        document.connections,
                        operation.entity_id,
                        "connection");
                    break;
            }
        }

        // Graph edits publish executable topology, not merely parseable
        // JSON. Expansion validates shared namespaces and every assembly
        // export without changing the persisted hierarchical document.
        (void)platform::flatten_model_document(document);
        const auto canonical =
            detail::serialize_topology_document_json(document);
        document = platform::parse_topology_document_text(
            canonical, units_);
        return repository_->create_model_revision(
            request.identity.team_id,
            request.identity.user_id,
            request.project_id,
            request.base_model_revision_id,
            document.schema_version,
            document.model_id,
            document.revision,
            "",
            "",
            canonical,
            checksum(canonical));
    } catch (const ProjectRequestError&) {
        throw;
    } catch (const std::exception& error) {
        throw ProjectRequestError(
            std::string("invalid graph edit batch: ") +
            error.what());
    }
}

CaseRevisionRecord ProjectService::create_case_revision(
    const CreateCaseRevisionRequest& request) const {
    require_identity(request.identity);
    if (request.project_id.empty() ||
        request.model_revision_id.empty()) {
        throw ProjectRequestError(
            "project and model revision IDs must not be empty");
    }
    if (request.case_json.empty()) {
        throw ProjectRequestError(
            "case document must not be empty");
    }

    platform::CaseDefinition simulation_case;
    std::string canonical;
    try {
        simulation_case =
            platform::parse_case_document_text(
                request.case_json, units_);
        canonical =
            detail::serialize_case_document_json(
                simulation_case);
    } catch (const std::exception& error) {
        throw ProjectRequestError(
            std::string("invalid case document: ") +
            error.what());
    }
    return repository_->create_case_revision(
        request.identity.team_id,
        request.identity.user_id,
        request.project_id,
        request.model_revision_id,
        request.parent_case_revision_id,
        simulation_case.id,
        simulation_case.mode,
        canonical,
        checksum(canonical));
}

CaseRevisionRecord ProjectService::apply_case_edits(
    const ApplyCaseEditsRequest& request) const {
    require_identity(request.identity);
    if (request.project_id.empty() ||
        request.model_revision_id.empty() ||
        request.base_case_revision_id.empty()) {
        throw ProjectRequestError(
            "project, model revision, and base case revision "
            "IDs must not be empty");
    }
    if (request.operations.empty() ||
        request.operations.size() > 256U) {
        throw ProjectRequestError(
            "case edit batch must contain 1 to 256 operations");
    }
    const auto base = repository_->get_case_revision(
        request.identity.team_id,
        request.project_id,
        request.model_revision_id,
        request.base_case_revision_id);
    if (!base) {
        throw ProjectStateError(
            "base case revision was not found");
    }

    try {
        auto simulation_case =
            platform::parse_case_document_text(
                base->canonical_case_json, units_);
        for (const auto& operation : request.operations) {
            const bool metadata =
                operation.field == CaseEditField::label ||
                operation.field == CaseEditField::mode;
            if (metadata && !operation.key.empty()) {
                throw ProjectRequestError(
                    "case metadata edits must not contain a "
                    "key");
            }
            if (!metadata && operation.key.empty()) {
                throw ProjectRequestError(
                    "case scalar edits require a non-empty key");
            }

            if (operation.action ==
                CaseEditAction::remove) {
                if (!operation.string_value.empty() ||
                    !operation.scalar_json.empty()) {
                    throw ProjectRequestError(
                        "case removal must not contain a value");
                }
                if (operation.field == CaseEditField::mode) {
                    throw ProjectRequestError(
                        "case mode cannot be removed");
                }
                if (operation.field == CaseEditField::label) {
                    simulation_case.label.clear();
                    continue;
                }
                auto& values = case_scalar_values(
                    simulation_case, operation.field);
                if (values.erase(operation.key) == 0U) {
                    throw ProjectRequestError(
                        "case scalar field '" + operation.key +
                        "' does not exist");
                }
                continue;
            }

            if (metadata) {
                if (!operation.scalar_json.empty() ||
                    operation.string_value.empty()) {
                    throw ProjectRequestError(
                        "case metadata upsert requires a "
                        "non-empty string value");
                }
                if (operation.field == CaseEditField::label) {
                    simulation_case.label =
                        operation.string_value;
                } else {
                    simulation_case.mode =
                        operation.string_value;
                }
                continue;
            }
            if (!operation.string_value.empty() ||
                operation.scalar_json.empty()) {
                throw ProjectRequestError(
                    "case scalar upsert requires a scalar "
                    "value document");
            }
            const auto scalar =
                platform::parse_scalar_value_document_text(
                    operation.scalar_json, units_);
            case_scalar_values(
                simulation_case,
                operation.field)[operation.key] = scalar;
        }

        const auto canonical =
            detail::serialize_case_document_json(
                simulation_case);
        simulation_case =
            platform::parse_case_document_text(
                canonical, units_);
        return repository_->create_case_revision(
            request.identity.team_id,
            request.identity.user_id,
            request.project_id,
            request.model_revision_id,
            request.base_case_revision_id,
            simulation_case.id,
            simulation_case.mode,
            canonical,
            checksum(canonical));
    } catch (const ProjectRequestError&) {
        throw;
    } catch (const std::exception& error) {
        throw ProjectRequestError(
            std::string("invalid case edit batch: ") +
            error.what());
    }
}

std::optional<CaseRevisionRecord>
ProjectService::get_case_revision(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string& model_revision_id,
    const std::string& case_revision_id) const {
    require_identity(identity);
    if (project_id.empty() || model_revision_id.empty() ||
        case_revision_id.empty()) {
        throw ProjectRequestError(
            "project, model revision, and case revision IDs "
            "must not be empty");
    }
    return repository_->get_case_revision(
        identity.team_id,
        project_id,
        model_revision_id,
        case_revision_id);
}

std::vector<CaseRevisionRecord>
ProjectService::list_case_revisions(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string& model_revision_id) const {
    require_identity(identity);
    if (project_id.empty() || model_revision_id.empty()) {
        throw ProjectRequestError(
            "project and model revision IDs must not be empty");
    }
    return repository_->list_case_revisions(
        identity.team_id, project_id, model_revision_id);
}

std::optional<ResolvedModelCase>
ProjectService::resolve_model_case(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string& model_revision_id,
    const std::string& case_revision_id) const {
    require_identity(identity);
    if (project_id.empty() || model_revision_id.empty() ||
        case_revision_id.empty()) {
        throw ProjectRequestError(
            "project, model revision, and case revision IDs "
            "must not be empty");
    }
    const auto model = repository_->get_model_revision(
        identity.team_id, project_id, model_revision_id);
    const auto simulation_case =
        repository_->get_case_revision(
            identity.team_id,
            project_id,
            model_revision_id,
            case_revision_id);
    if (!model || !simulation_case) {
        return std::nullopt;
    }
    try {
        auto document = platform::parse_topology_document_text(
            model->canonical_model_json, units_);
        document.schema_version = "thermox.model/v2";
        document.cases = {
            platform::parse_case_document_text(
                simulation_case->canonical_case_json,
                units_),
        };
        return ResolvedModelCase{
            project_id,
            model_revision_id,
            model->checksum,
            case_revision_id,
            simulation_case->checksum,
            simulation_case->case_id,
            simulation_case->mode,
            detail::serialize_model_document_json(document),
        };
    } catch (const std::exception& error) {
        throw ProjectStateError(
            std::string(
                "persisted model/case composition failed: ") +
            error.what());
    }
}

ArtifactRevisionRecord
ProjectService::create_artifact_revision(
    const CreateArtifactRevisionRequest& request) const {
    require_identity(request.identity);
    if (request.project_id.empty() ||
        request.artifact_id.empty()) {
        throw ProjectRequestError(
            "project and artifact IDs must not be empty");
    }
    if (request.artifact_type !=
            platform::performance_map_artifact_type &&
        request.artifact_type !=
            platform::correlation_artifact_type &&
        request.artifact_type !=
            platform::regime_map_artifact_type &&
        request.artifact_type !=
            platform::expression_component_artifact_type &&
        request.artifact_type !=
            assembly_template_artifact_type &&
        request.artifact_type != topology_draft_artifact_type &&
        request.artifact_type != validation_series_artifact_type &&
        request.artifact_type != validation_campaign_artifact_type &&
        request.artifact_type != balance_uncertainty_artifact_type) {
        throw ProjectRequestError(
            "unsupported engineering artifact type: " +
            request.artifact_type);
    }
    if (request.artifact_json.empty()) {
        throw ProjectRequestError(
            "artifact payload must not be empty");
    }
    if (!repository_->get_project(
            request.identity.team_id, request.project_id)) {
        throw ProjectStateError("project was not found");
    }
    std::string canonical;
    try {
        if (request.artifact_type ==
            platform::performance_map_artifact_type) {
            canonical =
                detail::canonicalize_performance_map_payload(
                    request.artifact_schema_version,
                    request.artifact_json);
        } else if (request.artifact_type ==
                   platform::correlation_artifact_type) {
            canonical = detail::canonicalize_correlation_payload(
                request.artifact_schema_version,
                request.artifact_json);
        } else if (request.artifact_type ==
                   platform::regime_map_artifact_type) {
            canonical = detail::canonicalize_regime_map_payload(
                request.artifact_schema_version,
                request.artifact_json);
        } else if (request.artifact_type ==
                   platform::expression_component_artifact_type) {
            canonical =
                detail::canonicalize_expression_component_payload(
                    request.artifact_schema_version,
                    request.artifact_json);
        } else if (request.artifact_type ==
                   validation_series_artifact_type) {
            if (request.artifact_schema_version !=
                validation_series_schema_v1) {
                throw ValidationSeriesError(
                    "unsupported validation-series schema version: " +
                    request.artifact_schema_version);
            }
            const auto artifact =
                parse_validation_series_artifact_json(
                    request.artifact_json);
            if (artifact.id != request.artifact_id) {
                throw ValidationSeriesError(
                    "validation-series payload ID must match the "
                    "project artifact ID");
            }
            canonical =
                serialize_validation_series_artifact_json(artifact);
        } else if (request.artifact_type ==
                   validation_campaign_artifact_type) {
            if (request.artifact_schema_version !=
                validation_campaign_schema_v1) {
                throw ValidationCampaignError(
                    "unsupported validation-campaign schema version: " +
                    request.artifact_schema_version);
            }
            const auto artifact =
                parse_validation_campaign_artifact_json(
                    request.artifact_json);
            if (artifact.id != request.artifact_id) {
                throw ValidationCampaignError(
                    "validation-campaign payload ID must match the "
                    "project artifact ID");
            }
            for (const auto& study_revision_id :
                 artifact.study_revision_ids) {
                if (!repository_->get_study_revision(
                        request.identity.team_id,
                        request.project_id,
                        study_revision_id)) {
                    throw ValidationCampaignError(
                        "validation-campaign Study revision was not "
                        "found in the Project: " + study_revision_id);
                }
            }
            canonical =
                serialize_validation_campaign_artifact_json(artifact);
        } else if (request.artifact_type ==
                   balance_uncertainty_artifact_type) {
            if (request.artifact_schema_version !=
                balance_uncertainty_schema_v1) {
                throw std::invalid_argument(
                    "unsupported balance-uncertainty schema version: " +
                    request.artifact_schema_version);
            }
            const auto artifact = parse_balance_uncertainty_model_json(
                request.artifact_json);
            if (artifact.id != request.artifact_id) {
                throw std::invalid_argument(
                    "balance-uncertainty payload ID must match the "
                    "project artifact ID");
            }
            canonical = serialize_balance_uncertainty_model_json(artifact);
        } else if (request.artifact_type ==
                   topology_draft_artifact_type) {
            canonical = canonical_topology_draft(
                request.artifact_schema_version,
                request.artifact_id,
                request.artifact_json);
        } else {
            canonical =
                detail::canonicalize_assembly_template_payload(
                    request.artifact_schema_version,
                    request.artifact_json);
        }
    } catch (const std::exception& error) {
        throw ProjectRequestError(
            std::string("invalid engineering artifact: ") +
            error.what());
    }
    if (request.artifact_type ==
        platform::expression_component_artifact_type) {
        const auto candidate =
            detail::expression_component_from_payload(
                request.artifact_schema_version, canonical);
        for (const auto& revision :
             repository_->list_artifact_revisions(
                 request.identity.team_id,
                 request.project_id)) {
            if (revision.artifact_type !=
                platform::expression_component_artifact_type) {
                continue;
            }
            const auto payload =
                artifact_content_->get(revision.content);
            if (!payload ||
                payload->size() != revision.content.byte_size ||
                checksum(*payload) != revision.content.checksum) {
                throw ProjectStateError(
                    "persisted component definition failed "
                    "integrity verification");
            }
            const auto existing =
                detail::expression_component_from_payload(
                    revision.artifact_schema_version,
                    *payload);
            if (revision.artifact_id == request.artifact_id &&
                existing.kind != candidate.kind) {
                throw ProjectRequestError(
                    "a component artifact cannot change kind "
                    "across revisions: " +
                    request.artifact_id);
            }
            if (existing.kind != candidate.kind) {
                continue;
            }
            if (revision.artifact_id != request.artifact_id) {
                throw ProjectRequestError(
                    "component kind is already owned by "
                    "another project artifact: " +
                    candidate.kind);
            }
            if (existing.version == candidate.version) {
                throw ProjectRequestError(
                    "component kind/version must be unique "
                    "across immutable revisions: " +
                    candidate.kind + " " +
                    candidate.version);
            }
        }
    }
    const auto expected_checksum = checksum(canonical);
    auto content = artifact_content_->put_json(
        request.identity.team_id,
        request.project_id,
        request.artifact_id,
        request.artifact_schema_version,
        canonical);
    if (content.object_key.empty() ||
        content.media_type != "application/json" ||
        content.byte_size != canonical.size()) {
        throw ProjectStateError(
            "artifact content store returned an invalid "
            "manifest");
    }
    if (!content.checksum.empty() &&
        content.checksum != expected_checksum) {
        throw ProjectStateError(
            "artifact content store checksum mismatch");
    }
    content.checksum = expected_checksum;
    return repository_->create_artifact_revision(
        request.identity.team_id,
        request.identity.user_id,
        request.project_id,
        request.artifact_id,
        request.parent_artifact_revision_id,
        request.artifact_type,
        request.artifact_schema_version,
        content);
}

std::optional<ArtifactRevisionRecord>
ProjectService::get_artifact_revision(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string& artifact_revision_id) const {
    require_identity(identity);
    if (project_id.empty() ||
        artifact_revision_id.empty()) {
        throw ProjectRequestError(
            "project and artifact revision IDs must not be "
            "empty");
    }
    return repository_->get_artifact_revision(
        identity.team_id,
        project_id,
        artifact_revision_id);
}

std::optional<ArtifactRevisionContent>
ProjectService::get_artifact_revision_content(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string& artifact_revision_id) const {
    const auto revision = get_artifact_revision(
        identity, project_id, artifact_revision_id);
    if (!revision) {
        return std::nullopt;
    }
    const auto payload = artifact_content_->get(revision->content);
    if (!payload) {
        throw ProjectStateError(
            "persisted engineering artifact content was not found");
    }
    if (payload->size() != revision->content.byte_size ||
        checksum(*payload) != revision->content.checksum) {
        throw ProjectStateError(
            "persisted engineering artifact content failed integrity "
            "verification");
    }
    return ArtifactRevisionContent{
        artifact_revision_content_schema_v1,
        *revision,
        *payload,
    };
}

std::vector<ArtifactRevisionRecord>
ProjectService::list_artifact_revisions(
    const IdentityContext& identity,
    const std::string& project_id) const {
    require_identity(identity);
    if (project_id.empty()) {
        throw ProjectRequestError(
            "project ID must not be empty");
    }
    return repository_->list_artifact_revisions(
        identity.team_id, project_id);
}

PerformanceMapQualityReviewRecord
ProjectService::create_performance_map_quality_review(
    const CreatePerformanceMapQualityReviewRequest& request) const {
    require_identity(request.identity);
    if (request.project_id.empty() ||
        request.artifact_revision_id.empty()) {
        throw ProjectRequestError(
            "project and artifact revision IDs must not be empty");
    }
    const auto reviewed_scope = trim(request.reviewed_scope);
    const auto rationale = trim(request.rationale);
    if (reviewed_scope.empty() || rationale.empty()) {
        throw ProjectRequestError(
            "performance-map quality reviews require a reviewed scope "
            "and rationale");
    }
    const auto content = get_artifact_revision_content(
        request.identity, request.project_id,
        request.artifact_revision_id);
    if (!content) {
        throw ProjectStateError(
            "reviewed artifact revision was not found");
    }
    if (content->revision.artifact_type !=
        platform::performance_map_artifact_type) {
        throw ProjectRequestError(
            "quality reviews may only target performance-map artifacts");
    }
    try {
        const auto input = detail::performance_map_from_payload(
            content->revision.artifact_id,
            content->revision.artifact_schema_version,
            content->revision.artifact_revision_id,
            content->revision.content.checksum.substr(7),
            content->canonical_artifact_json);
        const auto artifact = detail::performance_map_artifact(input);
        const auto quality =
            detail::performance_map_quality_summary(artifact);
        const auto snapshot =
            serialize_performance_map_quality_json(quality);
        return repository_->create_performance_map_quality_review(
            request.identity.team_id,
            request.identity.user_id,
            request.project_id,
            request.artifact_revision_id,
            content->revision.content.checksum,
            request.supersedes_review_id,
            request.disposition,
            reviewed_scope,
            rationale,
            quality.schema_version,
            snapshot,
            checksum(snapshot));
    } catch (const ProjectStateError&) {
        throw;
    } catch (const std::exception& error) {
        throw ProjectStateError(
            std::string("persisted performance map failed quality "
                        "assessment: ") + error.what());
    }
}

std::vector<PerformanceMapQualityReviewRecord>
ProjectService::list_performance_map_quality_reviews(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string& artifact_revision_id) const {
    require_identity(identity);
    if (project_id.empty() || artifact_revision_id.empty()) {
        throw ProjectRequestError(
            "project and artifact revision IDs must not be empty");
    }
    const auto artifact = repository_->get_artifact_revision(
        identity.team_id, project_id, artifact_revision_id);
    if (!artifact) return {};
    if (artifact->artifact_type !=
        platform::performance_map_artifact_type) {
        throw ProjectRequestError(
            "quality reviews may only target performance-map artifacts");
    }
    return repository_->list_performance_map_quality_reviews(
        identity.team_id, project_id, artifact_revision_id);
}

std::optional<ResolvedEngineeringArtifacts>
ProjectService::resolve_artifact_revisions(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::vector<std::string>&
        artifact_revision_ids) const {
    require_identity(identity);
    if (project_id.empty()) {
        throw ProjectRequestError(
            "project ID must not be empty");
    }
    ResolvedEngineeringArtifacts result;
    std::set<std::string> logical_ids;
    for (const auto& revision_id : artifact_revision_ids) {
        if (revision_id.empty()) {
            throw ProjectRequestError(
                "artifact revision IDs must not be empty");
        }
        const auto revision =
            repository_->get_artifact_revision(
                identity.team_id,
                project_id,
                revision_id);
        if (!revision) {
            return std::nullopt;
        }
        if (!logical_ids.insert(revision->artifact_id).second) {
            throw ProjectRequestError(
                "only one revision may be selected for "
                "artifact ID: " + revision->artifact_id);
        }
        const auto payload =
            artifact_content_->get(revision->content);
        if (!payload) {
            throw ProjectStateError(
                "persisted engineering artifact content was "
                "not found");
        }
        if (payload->size() != revision->content.byte_size ||
            checksum(*payload) != revision->content.checksum) {
            throw ProjectStateError(
                "persisted engineering artifact content "
                "failed integrity verification");
        }
        if (revision->artifact_type ==
            platform::performance_map_artifact_type) {
            result.snapshot.performance_maps.push_back(
                detail::performance_map_from_payload(
                    revision->artifact_id,
                    revision->artifact_schema_version,
                    revision->artifact_revision_id,
                    revision->content.checksum.substr(7),
                    *payload));
        } else if (
            revision->artifact_type ==
            platform::correlation_artifact_type) {
            result.snapshot.correlations.push_back(
                detail::correlation_from_payload(
                    revision->artifact_id,
                    revision->artifact_schema_version,
                    revision->artifact_revision_id,
                    revision->content.checksum.substr(7),
                    *payload));
        } else if (
            revision->artifact_type ==
            platform::regime_map_artifact_type) {
            result.snapshot.regime_maps.push_back(
                detail::regime_map_from_payload(
                    revision->artifact_id,
                    revision->artifact_schema_version,
                    revision->artifact_revision_id,
                    revision->content.checksum.substr(7),
                    *payload));
        } else if (
            revision->artifact_type ==
            platform::expression_component_artifact_type) {
            result.components.expression_components.push_back(
                detail::expression_component_from_payload(
                    revision->artifact_schema_version,
                    *payload));
            result.snapshot.references.push_back({
                revision->artifact_id,
                revision->artifact_type,
                revision->artifact_schema_version,
                revision->artifact_revision_id,
                revision->content.checksum.substr(7),
            });
        } else if (
            revision->artifact_type ==
            validation_series_artifact_type) {
            result.validation_series.push_back({
                *revision,
                parse_validation_series_artifact_json(*payload),
            });
        } else if (
            revision->artifact_type ==
            balance_uncertainty_artifact_type) {
            if (result.balance_uncertainty) {
                throw ProjectRequestError(
                    "a Study may bind at most one balance-uncertainty "
                    "artifact revision");
            }
            result.balance_uncertainty =
                ResolvedBalanceUncertaintyArtifact{
                    *revision,
                    parse_balance_uncertainty_model_json(*payload),
                };
            result.snapshot.references.push_back({
                revision->artifact_id,
                revision->artifact_type,
                revision->artifact_schema_version,
                revision->artifact_revision_id,
                revision->content.checksum.substr(7),
            });
        } else {
            throw ProjectStateError(
                "persisted engineering artifact type is not "
                "supported");
        }
        result.revisions.push_back(*revision);
    }
    return result;
}

std::optional<std::vector<ResolvedEngineeringArtifacts>>
ProjectService::resolve_component_revisions(
    const IdentityContext& identity,
    const std::string& project_id) const {
    require_identity(identity);
    if (project_id.empty()) {
        throw ProjectRequestError(
            "project ID must not be empty");
    }
    if (!repository_->get_project(
            identity.team_id, project_id)) {
        return std::nullopt;
    }
    std::vector<ArtifactRevisionRecord> revisions;
    for (const auto& revision :
         repository_->list_artifact_revisions(
             identity.team_id, project_id)) {
        if (revision.artifact_type !=
            platform::expression_component_artifact_type) {
            continue;
        }
        revisions.push_back(revision);
    }
    if (revisions.size() > 512U) {
        throw ProjectStateError(
            "project component catalog exceeds the "
            "512-revision limit");
    }
    std::sort(
        revisions.begin(),
        revisions.end(),
        [](const auto& left, const auto& right) {
            if (left.artifact_id != right.artifact_id) {
                return left.artifact_id < right.artifact_id;
            }
            return left.revision_number >
                right.revision_number;
        });
    std::vector<ResolvedEngineeringArtifacts> result;
    result.reserve(revisions.size());
    for (const auto& revision : revisions) {
        auto resolved = resolve_artifact_revisions(
            identity,
            project_id,
            {revision.artifact_revision_id});
        if (!resolved) {
            throw ProjectStateError(
                "component artifact revision disappeared "
                "during catalog resolution");
        }
        result.push_back(std::move(*resolved));
    }
    return result;
}

RunConfigurationRevisionRecord
ProjectService::create_run_configuration_revision(
    const CreateRunConfigurationRevisionRequest& request) const {
    require_identity(request.identity);
    if (request.project_id.empty() ||
        request.run_configuration_id.empty() ||
        request.study_revision_id.empty()) {
        throw ProjectRequestError(
            "project, run configuration, and study revision "
            "IDs must not be empty");
    }
    const auto study = get_study_revision(
        request.identity,
        request.project_id,
        request.study_revision_id);
    if (!study) {
        throw ProjectStateError(
            "study revision was not found");
    }
    validate_steady_solver(request.steady_solver);
    validate_transient_solver(request.transient_solver);
    return repository_->create_run_configuration_revision(
        request.identity.team_id,
        request.identity.user_id,
        request.project_id,
        request.run_configuration_id,
        request.parent_run_configuration_revision_id,
        request.study_revision_id,
        request.steady_solver,
        request.transient_solver,
        checksum(run_configuration_identity(request)));
}

StudyRevisionRecord ProjectService::create_study_revision(
    const CreateStudyRevisionRequest& request) const {
    require_identity(request.identity);
    if (request.project_id.empty() || request.study_id.empty() ||
        request.model_revision_id.empty() ||
        request.case_revision_id.empty() || request.intent.empty()) {
        throw ProjectRequestError(
            "project, study, model revision, case revision, and "
            "intent must not be empty");
    }
    const auto model_case = resolve_model_case(
        request.identity,
        request.project_id,
        request.model_revision_id,
        request.case_revision_id);
    if (!model_case) {
        throw ProjectStateError(
            "model/case revision pair was not found");
    }
    if (request.intent != model_case->mode) {
        throw ProjectRequestError(
            "study intent must match the bound case mode");
    }
    (void)run_mode(request.intent);

    auto artifact_ids = request.artifact_revision_ids;
    std::sort(artifact_ids.begin(), artifact_ids.end());
    if (std::adjacent_find(
            artifact_ids.begin(), artifact_ids.end()) !=
        artifact_ids.end()) {
        throw ProjectRequestError(
            "artifact revision IDs must be unique");
    }
    const auto resolved_artifacts = resolve_artifact_revisions(
            request.identity,
            request.project_id,
            artifact_ids);
    if (!resolved_artifacts) {
        throw ProjectStateError("artifact revision was not found");
    }
    auto operating_envelopes = request.artifact_operating_envelopes;
    for (auto& envelope : operating_envelopes) {
        if (envelope.artifact_revision_id.empty() ||
            envelope.coordinates.empty() ||
            !std::binary_search(
                artifact_ids.begin(), artifact_ids.end(),
                envelope.artifact_revision_id)) {
            throw ProjectRequestError(
                "artifact operating envelopes require a selected "
                "artifact and at least one coordinate");
        }
        std::map<std::string, std::string> declared_coordinates;
        const auto map = std::find_if(
            resolved_artifacts->snapshot.performance_maps.begin(),
            resolved_artifacts->snapshot.performance_maps.end(),
            [&](const auto& candidate) {
                return candidate.revision ==
                    envelope.artifact_revision_id;
            });
        if (map !=
            resolved_artifacts->snapshot.performance_maps.end()) {
            const auto& payload = map->map
                ? *map->map : map->layers.front().map;
            declared_coordinates.emplace(
                payload.primary_variable.name,
                payload.primary_variable.dimension);
            declared_coordinates.emplace(
                payload.family_variable.name,
                payload.family_variable.dimension);
            if (map->condition_variable) {
                declared_coordinates.emplace(
                    map->condition_variable->name,
                    map->condition_variable->dimension);
            }
        } else if (const auto correlation = std::find_if(
                       resolved_artifacts->snapshot.correlations.begin(),
                       resolved_artifacts->snapshot.correlations.end(),
                       [&](const auto& candidate) {
                           return candidate.revision ==
                               envelope.artifact_revision_id;
                       });
                   correlation !=
                       resolved_artifacts->snapshot.correlations.end()) {
            for (const auto& input : correlation->inputs) {
                declared_coordinates.emplace(
                    input.name, input.dimension);
            }
        } else if (const auto regime_map = std::find_if(
                       resolved_artifacts->snapshot.regime_maps.begin(),
                       resolved_artifacts->snapshot.regime_maps.end(),
                       [&](const auto& candidate) {
                           return candidate.revision ==
                               envelope.artifact_revision_id;
                       });
                   regime_map !=
                       resolved_artifacts->snapshot.regime_maps.end()) {
            for (const auto& input : regime_map->inputs) {
                declared_coordinates.emplace(
                    input.name, input.dimension);
            }
        } else {
            throw ProjectRequestError(
                "artifact operating envelopes require a performance "
                "map, correlation, or regime-map artifact");
        }
        const auto matches_variable = [&](const auto& coordinate) {
            const auto variable = declared_coordinates.find(
                coordinate.coordinate);
            return variable != declared_coordinates.end() &&
                variable->second == coordinate.dimension;
        };
        for (const auto& coordinate : envelope.coordinates) {
            if (!matches_variable(coordinate) ||
                (!coordinate.minimum && !coordinate.maximum) ||
                (coordinate.minimum &&
                 !std::isfinite(*coordinate.minimum)) ||
                (coordinate.maximum &&
                 !std::isfinite(*coordinate.maximum)) ||
                (coordinate.minimum && coordinate.maximum &&
                 (*coordinate.minimum > *coordinate.maximum ||
                  (*coordinate.minimum == *coordinate.maximum &&
                   (!coordinate.minimum_inclusive ||
                    !coordinate.maximum_inclusive))))) {
                throw ProjectRequestError(
                    "artifact operating-envelope coordinate or interval "
                    "is invalid");
            }
        }
        std::sort(
            envelope.coordinates.begin(), envelope.coordinates.end(),
            [](const auto& left, const auto& right) {
                return left.coordinate < right.coordinate;
            });
        if (std::adjacent_find(
                envelope.coordinates.begin(), envelope.coordinates.end(),
                [](const auto& left, const auto& right) {
                    return left.coordinate == right.coordinate;
                }) != envelope.coordinates.end()) {
            throw ProjectRequestError(
                "artifact operating-envelope coordinates must be unique");
        }
    }
    std::sort(
        operating_envelopes.begin(), operating_envelopes.end(),
        [](const auto& left, const auto& right) {
            return left.artifact_revision_id <
                right.artifact_revision_id;
        });
    if (std::adjacent_find(
            operating_envelopes.begin(), operating_envelopes.end(),
            [](const auto& left, const auto& right) {
                return left.artifact_revision_id ==
                    right.artifact_revision_id;
            }) != operating_envelopes.end()) {
        throw ProjectRequestError(
            "a Study may declare only one operating envelope per "
            "artifact revision");
    }
    auto qualification_requirements =
        request.artifact_qualification_requirements;
    for (auto& requirement : qualification_requirements) {
        if (requirement.artifact_revision_id.empty() ||
            requirement.review_id.empty() ||
            requirement.acceptable_dispositions.empty()) {
            throw ProjectRequestError(
                "artifact qualification requirements need an artifact "
                "revision, review, and acceptable disposition");
        }
        if (!std::binary_search(
                artifact_ids.begin(), artifact_ids.end(),
                requirement.artifact_revision_id)) {
            throw ProjectRequestError(
                "artifact qualification requirements may only target "
                "artifacts bound by the Study");
        }
        std::sort(
            requirement.acceptable_dispositions.begin(),
            requirement.acceptable_dispositions.end());
        if (std::find(
                requirement.acceptable_dispositions.begin(),
                requirement.acceptable_dispositions.end(),
                EngineeringReviewDisposition::rejected) !=
            requirement.acceptable_dispositions.end()) {
            throw ProjectRequestError(
                "a rejected engineering review cannot satisfy an "
                "artifact qualification requirement");
        }
        if (std::adjacent_find(
                requirement.acceptable_dispositions.begin(),
                requirement.acceptable_dispositions.end()) !=
            requirement.acceptable_dispositions.end()) {
            throw ProjectRequestError(
                "acceptable review dispositions must be unique");
        }
        const auto reviews =
            repository_->list_performance_map_quality_reviews(
                request.identity.team_id,
                request.project_id,
                requirement.artifact_revision_id);
        const auto review = std::find_if(
            reviews.begin(), reviews.end(),
            [&](const auto& candidate) {
                return candidate.review_id == requirement.review_id;
            });
        if (review == reviews.end()) {
            throw ProjectStateError(
                "artifact qualification review was not found");
        }
        if (std::find(
                requirement.acceptable_dispositions.begin(),
                requirement.acceptable_dispositions.end(),
                review->disposition) ==
            requirement.acceptable_dispositions.end()) {
            throw ProjectRequestError(
                "artifact qualification review disposition is not "
                "acceptable under the Study policy");
        }
    }
    std::sort(
        qualification_requirements.begin(),
        qualification_requirements.end(),
        [](const auto& left, const auto& right) {
            return std::tie(
                       left.artifact_revision_id, left.review_id) <
                std::tie(right.artifact_revision_id, right.review_id);
        });
    if (std::adjacent_find(
            qualification_requirements.begin(),
            qualification_requirements.end(),
            [](const auto& left, const auto& right) {
                return left.artifact_revision_id ==
                    right.artifact_revision_id;
            }) != qualification_requirements.end()) {
        throw ProjectRequestError(
            "a Study may bind only one qualification review per "
            "artifact revision");
    }
    try {
        validate_result_projections(request.result_projections);
        validate_engineering_acceptance_criteria(
            request.acceptance_criteria,
            request.result_projections);
    } catch (const ResultProjectionError& error) {
        throw ProjectRequestError(error.what());
    }
    auto trajectory_bindings =
        request.trajectory_validation_bindings;
    std::sort(
        trajectory_bindings.begin(), trajectory_bindings.end(),
        [](const auto& left, const auto& right) {
            return left.id < right.id;
        });
    std::set<std::pair<std::string, std::string>> bound_signals;
    for (const auto& binding : trajectory_bindings) {
        const auto projection = std::find_if(
            request.result_projections.begin(),
            request.result_projections.end(),
            [&](const auto& candidate) {
                return candidate.id == binding.projection_id;
            });
        const auto evidence = std::find_if(
            resolved_artifacts->validation_series.begin(),
            resolved_artifacts->validation_series.end(),
            [&](const auto& candidate) {
                return candidate.source.artifact_revision_id ==
                    binding.artifact_revision_id;
            });
        const auto signal = evidence ==
                resolved_artifacts->validation_series.end()
            ? static_cast<const ValidationSeriesSignal*>(nullptr)
            : [&]() -> const ValidationSeriesSignal* {
                const auto found = std::find_if(
                    evidence->artifact.signals.begin(),
                    evidence->artifact.signals.end(),
                    [&](const auto& candidate) {
                        return candidate.id == binding.signal_id;
                    });
                return found == evidence->artifact.signals.end()
                    ? nullptr : &*found;
            }();
        if (binding.id.empty() ||
            binding.artifact_revision_id.empty() ||
            binding.signal_id.empty() ||
            binding.projection_id.empty() ||
            projection == request.result_projections.end() ||
            signal == nullptr ||
            projection->dimension != signal->dimension ||
            projection->window.has_value() ||
            !std::isfinite(binding.time_offset_si) ||
            !std::isfinite(binding.baseline_time_si) ||
            !std::isfinite(binding.absolute_tolerance_si) ||
            !std::isfinite(binding.relative_tolerance) ||
            !std::isfinite(binding.uncertainty_multiplier) ||
            !std::isfinite(binding.maximum_interpolation_gap_si) ||
            binding.absolute_tolerance_si < 0.0 ||
            binding.relative_tolerance < 0.0 ||
            binding.uncertainty_multiplier < 0.0 ||
            binding.maximum_interpolation_gap_si < 0.0 ||
            !bound_signals.emplace(
                binding.artifact_revision_id,
                binding.signal_id).second) {
            throw ProjectRequestError(
                "trajectory-validation binding is missing, duplicated, "
                "dimensionally inconsistent, or has invalid policy");
        }
    }
    if (std::adjacent_find(
            trajectory_bindings.begin(), trajectory_bindings.end(),
            [](const auto& left, const auto& right) {
                return left.id == right.id;
            }) != trajectory_bindings.end()) {
        throw ProjectRequestError(
            "trajectory-validation binding IDs must be unique");
    }
    if (!trajectory_bindings.empty() &&
        run_mode(request.intent) != "transient") {
        throw ProjectRequestError(
            "trajectory validation requires a transient Study");
    }
    if (run_mode(request.intent) == "steady" &&
        std::any_of(
            request.result_projections.begin(),
            request.result_projections.end(),
            [](const auto& projection) {
                return projection.aggregation !=
                        ResultAggregation::final ||
                    projection.window.has_value();
            })) {
        throw ProjectRequestError(
            "steady studies only support unwindowed final result "
            "projections");
    }
    return repository_->create_study_revision(
        request.identity.team_id,
        request.identity.user_id,
        request.project_id,
        request.study_id,
        request.parent_study_revision_id,
        request.model_revision_id,
        request.case_revision_id,
        request.intent,
        artifact_ids,
        qualification_requirements,
        operating_envelopes,
        request.result_projections,
        request.acceptance_criteria,
        trajectory_bindings,
        checksum(study_identity(
            [&]() {
                auto canonical = request;
                canonical.trajectory_validation_bindings =
                    trajectory_bindings;
                return canonical;
            }(),
            artifact_ids, qualification_requirements,
            operating_envelopes)));
}

std::optional<StudyRevisionRecord>
ProjectService::get_study_revision(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string& study_revision_id) const {
    require_identity(identity);
    if (project_id.empty() || study_revision_id.empty()) {
        throw ProjectRequestError(
            "project and study revision IDs must not be empty");
    }
    return repository_->get_study_revision(
        identity.team_id, project_id, study_revision_id);
}

std::vector<StudyRevisionRecord>
ProjectService::list_study_revisions(
    const IdentityContext& identity,
    const std::string& project_id) const {
    require_identity(identity);
    if (project_id.empty()) {
        throw ProjectRequestError("project ID must not be empty");
    }
    return repository_->list_study_revisions(
        identity.team_id, project_id);
}

CalibrationRevisionRecord
ProjectService::create_calibration_revision(
    const CreateCalibrationRevisionRequest& request) const {
    require_identity(request.identity);
    if (request.project_id.empty() || request.calibration_id.empty() ||
        request.model_revision_id.empty() || request.definition_json.empty()) {
        throw ProjectRequestError(
            "project, calibration, model revision, and definition must not be empty");
    }
    if (request.training_study_revision_ids.empty()) {
        throw ProjectRequestError(
            "a calibration requires at least one training Study revision");
    }
    validate_calibration_solver(request.solver);

    auto training = request.training_study_revision_ids;
    auto validation = request.validation_study_revision_ids;
    std::sort(training.begin(), training.end());
    std::sort(validation.begin(), validation.end());
    if (std::adjacent_find(training.begin(), training.end()) != training.end() ||
        std::adjacent_find(validation.begin(), validation.end()) != validation.end()) {
        throw ProjectRequestError("calibration Study revision IDs must be unique");
    }
    std::vector<std::string> overlap;
    std::set_intersection(
        training.begin(), training.end(), validation.begin(), validation.end(),
        std::back_inserter(overlap));
    if (!overlap.empty()) {
        throw ProjectRequestError(
            "training and validation Study revisions must be disjoint");
    }

    const auto model = repository_->get_model_revision(
        request.identity.team_id, request.project_id,
        request.model_revision_id);
    if (!model) throw ProjectStateError("model revision was not found");

    std::vector<platform::CaseDefinition> cases;
    std::set<std::string> training_case_ids;
    std::set<std::string> validation_case_ids;
    std::optional<std::vector<std::string>> artifact_snapshot;
    const auto collect = [&](const std::vector<std::string>& ids,
                             bool is_training) {
        for (const auto& id : ids) {
            const auto study = repository_->get_study_revision(
                request.identity.team_id, request.project_id, id);
            if (!study || study->model_revision_id != request.model_revision_id) {
                throw ProjectStateError(
                    "calibration Study revision was not found for the bound model");
            }
            if (run_mode(study->intent) != "steady") {
                throw ProjectRequestError(
                    "calibration currently supports steady Studies only");
            }
            if (!artifact_snapshot) {
                artifact_snapshot = study->artifact_revision_ids;
            } else if (*artifact_snapshot !=
                       study->artifact_revision_ids) {
                throw ProjectRequestError(
                    "all calibration Studies must bind the same "
                    "engineering artifact revisions");
            }
            const auto simulation_case = repository_->get_case_revision(
                request.identity.team_id, request.project_id,
                request.model_revision_id, study->case_revision_id);
            if (!simulation_case) {
                throw ProjectStateError(
                    "calibration Study case revision was not found");
            }
            auto parsed = platform::parse_case_document_text(
                simulation_case->canonical_case_json, units_);
            if (is_training && !training_case_ids.insert(parsed.id).second) {
                throw ProjectRequestError(
                    "training Studies must bind distinct case IDs");
            }
            if (!is_training &&
                !validation_case_ids.insert(parsed.id).second) {
                throw ProjectRequestError(
                    "validation Studies must bind distinct case IDs");
            }
            if (std::none_of(cases.begin(), cases.end(), [&](const auto& item) {
                    return item.id == parsed.id;
                })) {
                cases.push_back(std::move(parsed));
            }
        }
    };
    collect(training, true);
    collect(validation, false);

    try {
        auto definition = platform::parse_calibration_document_text(
            request.definition_json, units_);
        if (definition.id != request.calibration_id) {
            throw ProjectRequestError(
                "calibration definition ID does not match calibration_id");
        }
        bool has_training_observation = false;
        std::set<std::string> observed_case_ids;
        for (const auto& observation : definition.observations) {
            observed_case_ids.insert(observation.case_id);
            if (training_case_ids.contains(observation.case_id)) {
                has_training_observation = true;
            } else if (!validation_case_ids.contains(observation.case_id)) {
                throw ProjectRequestError(
                    "calibration observations must reference a bound "
                    "training or validation Study case");
            }
        }
        if (!has_training_observation) {
            throw ProjectRequestError(
                "calibration requires at least one training observation");
        }
        const auto require_observed = [&](const auto& ids,
                                          const char* role) {
            for (const auto& case_id : ids) {
                if (!observed_case_ids.contains(case_id)) {
                    throw ProjectRequestError(
                        std::string(role) + " Study case '" + case_id +
                        "' has no calibration observation");
                }
            }
        };
        require_observed(training_case_ids, "training");
        require_observed(validation_case_ids, "validation");
        for (const auto& parameter : definition.parameters) {
            for (const auto& case_id : parameter.case_ids) {
                if (!training_case_ids.contains(case_id)) {
                    throw ProjectRequestError(
                        "case-scoped calibration parameters must reference training Study cases");
                }
            }
        }
        auto composed = platform::parse_topology_document_text(
            model->canonical_model_json, units_);
        composed.schema_version = "thermox.model/v2";
        composed.cases = std::move(cases);
        composed.calibrations = {definition};
        (void)platform::parse_model_document_text(
            detail::serialize_model_document_json(composed), units_);
        const auto canonical_definition =
            detail::serialize_calibration_document_json(definition);
        return repository_->create_calibration_revision(
            request.identity.team_id, request.identity.user_id,
            request.project_id, request.calibration_id,
            request.parent_calibration_revision_id,
            request.model_revision_id, training, validation,
            canonical_definition, request.solver,
            checksum(calibration_identity(
                request, training, validation, canonical_definition)));
    } catch (const ProjectRequestError&) {
        throw;
    } catch (const std::exception& error) {
        throw ProjectRequestError(
            std::string("invalid calibration revision: ") + error.what());
    }
}

std::optional<CalibrationRevisionRecord>
ProjectService::get_calibration_revision(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string& calibration_revision_id) const {
    require_identity(identity);
    if (project_id.empty() || calibration_revision_id.empty()) {
        throw ProjectRequestError(
            "project and calibration revision IDs must not be empty");
    }
    return repository_->get_calibration_revision(
        identity.team_id, project_id, calibration_revision_id);
}

std::vector<CalibrationRevisionRecord>
ProjectService::list_calibration_revisions(
    const IdentityContext& identity,
    const std::string& project_id) const {
    require_identity(identity);
    if (project_id.empty()) {
        throw ProjectRequestError("project ID must not be empty");
    }
    return repository_->list_calibration_revisions(
        identity.team_id, project_id);
}

ReconciliationRevisionRecord
ProjectService::create_reconciliation_revision(
    const CreateReconciliationRevisionRequest& request) const {
    require_identity(request.identity);
    if (request.project_id.empty() ||
        request.reconciliation_id.empty() ||
        request.model_revision_id.empty() ||
        request.definition_json.empty() ||
        request.constraint_study_revision_ids.empty()) {
        throw ProjectRequestError(
            "project, reconciliation, model revision, definition, and "
            "constraint Studies must not be empty");
    }
    validate_reconciliation_solver(
        request.solver, request.profile_likelihood,
        request.joint_confidence_region);
    if ((request.profile_likelihood.enabled ||
         request.joint_confidence_region.enabled) &&
        request.mode != ReconciliationMode::weighted_measurements) {
        throw ProjectRequestError(
            "profile likelihood and joint confidence regions require "
            "weighted-measurements reconciliation mode");
    }
    auto constraints = request.constraint_study_revision_ids;
    auto held_out = request.held_out_study_revision_ids;
    std::sort(constraints.begin(), constraints.end());
    std::sort(held_out.begin(), held_out.end());
    if (std::adjacent_find(constraints.begin(), constraints.end()) !=
            constraints.end() ||
        std::adjacent_find(held_out.begin(), held_out.end()) !=
            held_out.end()) {
        throw ProjectRequestError(
            "reconciliation Study revision IDs must be unique");
    }
    std::vector<std::string> overlap;
    std::set_intersection(
        constraints.begin(), constraints.end(),
        held_out.begin(), held_out.end(),
        std::back_inserter(overlap));
    if (!overlap.empty()) {
        throw ProjectRequestError(
            "constraint and held-out Studies must be disjoint");
    }
    const auto model = repository_->get_model_revision(
        request.identity.team_id, request.project_id,
        request.model_revision_id);
    if (!model) throw ProjectStateError("model revision was not found");

    std::vector<platform::CaseDefinition> cases;
    std::set<std::string> constraint_case_ids;
    std::set<std::string> held_out_case_ids;
    std::optional<std::vector<std::string>> artifact_snapshot;
    const auto collect = [&](const std::vector<std::string>& ids,
                             bool is_constraint) {
        for (const auto& id : ids) {
            const auto study = repository_->get_study_revision(
                request.identity.team_id, request.project_id, id);
            if (!study ||
                study->model_revision_id != request.model_revision_id) {
                throw ProjectStateError(
                    "reconciliation Study was not found for the model");
            }
            if (run_mode(study->intent) != "steady") {
                throw ProjectRequestError(
                    "reconciliation currently supports steady Studies only");
            }
            if (!artifact_snapshot) {
                artifact_snapshot = study->artifact_revision_ids;
            } else if (*artifact_snapshot !=
                       study->artifact_revision_ids) {
                throw ProjectRequestError(
                    "all reconciliation Studies must bind the same "
                    "engineering artifact revisions");
            }
            const auto simulation_case =
                repository_->get_case_revision(
                    request.identity.team_id, request.project_id,
                    request.model_revision_id, study->case_revision_id);
            if (!simulation_case) {
                throw ProjectStateError(
                    "reconciliation Study case was not found");
            }
            auto parsed = platform::parse_case_document_text(
                simulation_case->canonical_case_json, units_);
            auto& case_ids = is_constraint
                ? constraint_case_ids : held_out_case_ids;
            if (!case_ids.insert(parsed.id).second) {
                throw ProjectRequestError(
                    "reconciliation Studies must bind distinct case IDs");
            }
            if (std::none_of(cases.begin(), cases.end(),
                    [&](const auto& item) { return item.id == parsed.id; })) {
                cases.push_back(std::move(parsed));
            }
        }
    };
    collect(constraints, true);
    collect(held_out, false);

    try {
        auto definition = platform::parse_calibration_document_text(
            request.definition_json, units_);
        if (definition.id != request.reconciliation_id) {
            throw ProjectRequestError(
                "reconciliation definition ID does not match "
                "reconciliation_id");
        }
        const auto validate_selected_parameters =
            [&](const std::vector<std::string>& selected,
                const char* policy_name) {
                std::set<std::string> declared;
                for (const auto& parameter : definition.parameters) {
                    declared.insert(parameter.id);
                }
                std::set<std::string> unique;
                for (const auto& id : selected) {
                    if (id.empty() || !unique.insert(id).second ||
                        !declared.contains(id)) {
                        throw ProjectRequestError(
                            std::string(policy_name) +
                            " parameter IDs must be unique and reference "
                            "declared adjustable quantities: " + id);
                    }
                }
            };
        if (request.profile_likelihood.enabled) {
            validate_selected_parameters(
                request.profile_likelihood.parameter_ids,
                "profile-likelihood");
        }
        if (request.joint_confidence_region.enabled) {
            validate_selected_parameters(
                request.joint_confidence_region.parameter_ids,
                "joint-confidence-region");
        }
        std::set<std::string> observed_cases;
        std::size_t constraint_observations = 0;
        for (const auto& observation : definition.observations) {
            observed_cases.insert(observation.case_id);
            if (constraint_case_ids.contains(observation.case_id)) {
                ++constraint_observations;
            } else if (!held_out_case_ids.contains(observation.case_id)) {
                throw ProjectRequestError(
                    "reconciliation observations must reference a "
                    "bound constraint or held-out Study case");
            }
        }
        if (request.mode == ReconciliationMode::hard_equalities &&
            constraint_observations != definition.parameters.size()) {
            throw ProjectRequestError(
                "hard reconciliation requires one constraint "
                "observation per adjustable quantity");
        }
        if (request.mode == ReconciliationMode::weighted_measurements &&
            constraint_observations < definition.parameters.size()) {
            throw ProjectRequestError(
                "weighted reconciliation requires at least as many "
                "constraint observations as adjustable quantities");
        }
        for (const auto& case_id : constraint_case_ids) {
            if (!observed_cases.contains(case_id)) {
                throw ProjectRequestError(
                    "constraint Study case has no reconciliation "
                    "observation: " + case_id);
            }
        }
        for (const auto& case_id : held_out_case_ids) {
            if (!observed_cases.contains(case_id)) {
                throw ProjectRequestError(
                    "held-out Study case has no reconciliation "
                    "observation: " + case_id);
            }
        }
        auto composed = platform::parse_topology_document_text(
            model->canonical_model_json, units_);
        composed.schema_version = "thermox.model/v2";
        composed.cases = std::move(cases);
        composed.calibrations = {definition};
        (void)platform::parse_model_document_text(
            detail::serialize_model_document_json(composed), units_);
        const auto canonical_definition =
            detail::serialize_calibration_document_json(definition);
        return repository_->create_reconciliation_revision(
            request.identity.team_id, request.identity.user_id,
            request.project_id, request.reconciliation_id,
            request.parent_reconciliation_revision_id,
            request.model_revision_id, constraints, held_out,
            canonical_definition, request.mode, request.solver,
            request.profile_likelihood,
            request.joint_confidence_region,
            checksum(reconciliation_identity(
                request, constraints, held_out,
                canonical_definition)));
    } catch (const ProjectRequestError&) {
        throw;
    } catch (const std::exception& error) {
        throw ProjectRequestError(
            std::string("invalid reconciliation revision: ") +
            error.what());
    }
}

std::optional<ReconciliationRevisionRecord>
ProjectService::get_reconciliation_revision(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string& reconciliation_revision_id) const {
    require_identity(identity);
    if (project_id.empty() || reconciliation_revision_id.empty()) {
        throw ProjectRequestError(
            "project and reconciliation revision IDs must not be empty");
    }
    return repository_->get_reconciliation_revision(
        identity.team_id, project_id, reconciliation_revision_id);
}

std::vector<ReconciliationRevisionRecord>
ProjectService::list_reconciliation_revisions(
    const IdentityContext& identity,
    const std::string& project_id) const {
    require_identity(identity);
    if (project_id.empty()) {
        throw ProjectRequestError("project ID must not be empty");
    }
    return repository_->list_reconciliation_revisions(
        identity.team_id, project_id);
}

std::optional<RunConfigurationRevisionRecord>
ProjectService::get_run_configuration_revision(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string&
        run_configuration_revision_id) const {
    require_identity(identity);
    if (project_id.empty() ||
        run_configuration_revision_id.empty()) {
        throw ProjectRequestError(
            "project and run configuration revision IDs must "
            "not be empty");
    }
    return repository_->get_run_configuration_revision(
        identity.team_id,
        project_id,
        run_configuration_revision_id);
}

std::vector<RunConfigurationRevisionRecord>
ProjectService::list_run_configuration_revisions(
    const IdentityContext& identity,
    const std::string& project_id) const {
    require_identity(identity);
    if (project_id.empty()) {
        throw ProjectRequestError(
            "project ID must not be empty");
    }
    return repository_->list_run_configuration_revisions(
        identity.team_id, project_id);
}

std::optional<ResolvedRunConfiguration>
ProjectService::resolve_run_configuration(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string&
        run_configuration_revision_id) const {
    const auto configuration =
        get_run_configuration_revision(
            identity,
            project_id,
            run_configuration_revision_id);
    if (!configuration) {
        return std::nullopt;
    }
    const auto study = get_study_revision(
        identity,
        project_id,
        configuration->study_revision_id);
    if (!study) {
        throw ProjectStateError(
            "persisted run configuration study was not found");
    }
    const auto model_case = resolve_model_case(
        identity,
        project_id,
        study->model_revision_id,
        study->case_revision_id);
    auto artifacts = resolve_artifact_revisions(
        identity,
        project_id,
        study->artifact_revision_ids);
    if (!model_case || !artifacts) {
        throw ProjectStateError(
            "persisted run configuration dependencies were "
            "not found");
    }
    for (const auto& requirement :
         study->artifact_qualification_requirements) {
        const auto reviews =
            repository_->list_performance_map_quality_reviews(
                identity.team_id,
                project_id,
                requirement.artifact_revision_id);
        const auto review = std::find_if(
            reviews.begin(), reviews.end(),
            [&](const auto& candidate) {
                return candidate.review_id == requirement.review_id;
            });
        if (review == reviews.end() ||
            std::find(
                requirement.acceptable_dispositions.begin(),
                requirement.acceptable_dispositions.end(),
                review->disposition) ==
                requirement.acceptable_dispositions.end()) {
            throw ProjectStateError(
                "persisted Study artifact qualification evidence "
                "could not be verified");
        }
    }
    for (const auto& envelope : study->artifact_operating_envelopes) {
        const auto map = std::find_if(
            artifacts->snapshot.performance_maps.begin(),
            artifacts->snapshot.performance_maps.end(),
            [&](const auto& candidate) {
                return candidate.revision ==
                    envelope.artifact_revision_id;
            });
        if (map != artifacts->snapshot.performance_maps.end()) {
            map->operating_envelope = envelope.coordinates;
            continue;
        }
        const auto correlation = std::find_if(
            artifacts->snapshot.correlations.begin(),
            artifacts->snapshot.correlations.end(),
            [&](const auto& candidate) {
                return candidate.revision ==
                    envelope.artifact_revision_id;
            });
        if (correlation != artifacts->snapshot.correlations.end()) {
            correlation->operating_envelope = envelope.coordinates;
            continue;
        }
        const auto regime_map = std::find_if(
            artifacts->snapshot.regime_maps.begin(),
            artifacts->snapshot.regime_maps.end(),
            [&](const auto& candidate) {
                return candidate.revision ==
                    envelope.artifact_revision_id;
            });
        if (regime_map != artifacts->snapshot.regime_maps.end()) {
            regime_map->operating_envelope = envelope.coordinates;
            continue;
        }
        throw ProjectStateError(
            "persisted Study operating-envelope artifact was not found");
    }
    return ResolvedRunConfiguration{
        *configuration,
        *study,
        *model_case,
        *artifacts,
    };
}

std::optional<ResolvedCalibration>
ProjectService::resolve_calibration(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string& calibration_revision_id) const {
    const auto calibration = get_calibration_revision(
        identity, project_id, calibration_revision_id);
    if (!calibration) return std::nullopt;
    const auto model = repository_->get_model_revision(
        identity.team_id, project_id,
        calibration->model_revision_id);
    if (!model) {
        throw ProjectStateError(
            "persisted calibration model revision was not found");
    }

    std::vector<StudyRevisionRecord> studies;
    std::vector<platform::CaseDefinition> cases;
    std::set<std::string> training_case_ids;
    std::set<std::string> validation_case_ids;
    std::optional<std::vector<std::string>> artifact_ids;
    const auto collect = [&](const std::vector<std::string>& ids,
                             bool training) {
        for (const auto& id : ids) {
            const auto study = repository_->get_study_revision(
                identity.team_id, project_id, id);
            if (!study ||
                study->model_revision_id != model->model_revision_id) {
                throw ProjectStateError(
                    "persisted calibration Study revision was not found");
            }
            if (!artifact_ids) {
                artifact_ids = study->artifact_revision_ids;
            } else if (*artifact_ids != study->artifact_revision_ids) {
                throw ProjectStateError(
                    "persisted calibration Studies have incompatible "
                    "artifact revisions");
            }
            const auto simulation_case = repository_->get_case_revision(
                identity.team_id, project_id,
                model->model_revision_id, study->case_revision_id);
            if (!simulation_case) {
                throw ProjectStateError(
                    "persisted calibration case revision was not found");
            }
            auto parsed = platform::parse_case_document_text(
                simulation_case->canonical_case_json, units_);
            (training ? training_case_ids : validation_case_ids)
                .insert(parsed.id);
            if (std::none_of(cases.begin(), cases.end(),
                    [&](const auto& item) { return item.id == parsed.id; })) {
                cases.push_back(std::move(parsed));
            }
            studies.push_back(*study);
        }
    };
    collect(calibration->training_study_revision_ids, true);
    collect(calibration->validation_study_revision_ids, false);
    const auto artifacts = resolve_artifact_revisions(
        identity, project_id, artifact_ids.value_or(
            std::vector<std::string>{}));
    if (!artifacts) {
        throw ProjectStateError(
            "persisted calibration artifacts were not found");
    }
    try {
        auto document = platform::parse_topology_document_text(
            model->canonical_model_json, units_);
        document.schema_version = "thermox.model/v2";
        document.cases = std::move(cases);
        auto definition = platform::parse_calibration_document_text(
            calibration->definition_json, units_);
        std::vector<StudyPredictionCase> predictions;
        auto training_definition = definition;
        training_definition.observations.clear();
        for (const auto& observation : definition.observations) {
            if (training_case_ids.contains(observation.case_id)) {
                training_definition.observations.push_back(observation);
                continue;
            }
            if (!validation_case_ids.contains(observation.case_id)) {
                throw ProjectStateError(
                    "persisted calibration observation is not bound "
                    "to a calibration Study");
            }
            auto prediction = std::find_if(
                predictions.begin(), predictions.end(),
                [&](const auto& item) {
                    return item.case_id == observation.case_id;
                });
            if (prediction == predictions.end()) {
                predictions.push_back({observation.case_id, {}});
                prediction = std::prev(predictions.end());
            }
            prediction->observations.push_back({
                observation.id,
                observation.target,
                observation.measured.dimension,
                observation.measured.value_si,
                observation.sigma.value_si,
                observation.time.has_value()
                    ? std::optional<double>(
                          observation.time->value_si)
                    : std::nullopt,
            });
        }
        document.calibrations = {std::move(training_definition)};
        const auto executable =
            detail::serialize_model_document_json(document);
        (void)platform::parse_model_document_text(executable, units_);
        return ResolvedCalibration{
            *calibration, *model, std::move(studies), executable,
            std::move(predictions),
            *artifacts,
        };
    } catch (const std::exception& error) {
        throw ProjectStateError(
            std::string("persisted calibration composition failed: ") +
            error.what());
    }
}

std::optional<ResolvedReconciliation>
ProjectService::resolve_reconciliation(
    const IdentityContext& identity,
    const std::string& project_id,
    const std::string& reconciliation_revision_id) const {
    const auto reconciliation = get_reconciliation_revision(
        identity, project_id, reconciliation_revision_id);
    if (!reconciliation) return std::nullopt;
    const auto model = repository_->get_model_revision(
        identity.team_id, project_id,
        reconciliation->model_revision_id);
    if (!model) {
        throw ProjectStateError(
            "persisted reconciliation model revision was not found");
    }
    std::vector<StudyRevisionRecord> studies;
    std::vector<platform::CaseDefinition> cases;
    std::set<std::string> constraint_case_ids;
    std::set<std::string> held_out_case_ids;
    std::optional<std::vector<std::string>> artifact_ids;
    const auto collect = [&](const std::vector<std::string>& ids,
                             bool constraint) {
        for (const auto& id : ids) {
            const auto study = repository_->get_study_revision(
                identity.team_id, project_id, id);
            if (!study ||
                study->model_revision_id != model->model_revision_id) {
                throw ProjectStateError(
                    "persisted reconciliation Study was not found");
            }
            if (!artifact_ids) {
                artifact_ids = study->artifact_revision_ids;
            } else if (*artifact_ids != study->artifact_revision_ids) {
                throw ProjectStateError(
                    "persisted reconciliation Studies have "
                    "incompatible artifact revisions");
            }
            const auto simulation_case =
                repository_->get_case_revision(
                    identity.team_id, project_id,
                    model->model_revision_id, study->case_revision_id);
            if (!simulation_case) {
                throw ProjectStateError(
                    "persisted reconciliation case was not found");
            }
            auto parsed = platform::parse_case_document_text(
                simulation_case->canonical_case_json, units_);
            (constraint ? constraint_case_ids : held_out_case_ids)
                .insert(parsed.id);
            if (std::none_of(cases.begin(), cases.end(),
                    [&](const auto& item) { return item.id == parsed.id; })) {
                cases.push_back(std::move(parsed));
            }
            studies.push_back(*study);
        }
    };
    collect(reconciliation->constraint_study_revision_ids, true);
    collect(reconciliation->held_out_study_revision_ids, false);
    const auto artifacts = resolve_artifact_revisions(
        identity, project_id,
        artifact_ids.value_or(std::vector<std::string>{}));
    if (!artifacts) {
        throw ProjectStateError(
            "persisted reconciliation artifacts were not found");
    }
    try {
        auto document = platform::parse_topology_document_text(
            model->canonical_model_json, units_);
        document.schema_version = "thermox.model/v2";
        document.cases = std::move(cases);
        auto definition = platform::parse_calibration_document_text(
            reconciliation->definition_json, units_);
        std::vector<StudyPredictionCase> held_out;
        auto constraint_definition = definition;
        constraint_definition.observations.clear();
        for (const auto& observation : definition.observations) {
            if (constraint_case_ids.contains(observation.case_id)) {
                constraint_definition.observations.push_back(observation);
                continue;
            }
            if (!held_out_case_ids.contains(observation.case_id)) {
                throw ProjectStateError(
                    "persisted reconciliation observation is not bound "
                    "to a reconciliation Study");
            }
            auto prediction = std::find_if(
                held_out.begin(), held_out.end(),
                [&](const auto& item) {
                    return item.case_id == observation.case_id;
                });
            if (prediction == held_out.end()) {
                held_out.push_back({observation.case_id, {}});
                prediction = std::prev(held_out.end());
            }
            prediction->observations.push_back({
                observation.id,
                observation.target,
                observation.measured.dimension,
                observation.measured.value_si,
                observation.sigma.value_si,
                std::nullopt,
            });
        }
        document.calibrations = {std::move(constraint_definition)};
        const auto executable =
            detail::serialize_model_document_json(document);
        (void)platform::parse_model_document_text(executable, units_);
        return ResolvedReconciliation{
            *reconciliation, *model, std::move(studies), executable,
            std::move(held_out), *artifacts,
        };
    } catch (const std::exception& error) {
        throw ProjectStateError(
            std::string("persisted reconciliation composition failed: ") +
            error.what());
    }
}

ProjectModelValidationService::ProjectModelValidationService(
    std::shared_ptr<ProjectService> projects,
    std::shared_ptr<const SimulationRuntime> runtime)
    : projects_(std::move(projects)),
      simulation_(std::move(runtime)) {
    if (!projects_) {
        throw std::invalid_argument(
            "project validation service requires a project "
            "service");
    }
}

ProjectModelValidationResponse
ProjectModelValidationService::validate(
    const ValidateProjectModelRequest& request) const {
    if (request.project_id.empty() ||
        request.model_revision_id.empty() ||
        request.case_revision_id.empty()) {
        throw ProjectRequestError(
            "project, model revision, and case revision IDs "
            "must not be empty");
    }
    auto artifact_ids = request.artifact_revision_ids;
    std::sort(artifact_ids.begin(), artifact_ids.end());
    if (std::adjacent_find(
            artifact_ids.begin(), artifact_ids.end()) !=
        artifact_ids.end()) {
        throw ProjectRequestError(
            "artifact revision IDs must be unique");
    }
    const auto model_case = projects_->resolve_model_case(
        request.identity,
        request.project_id,
        request.model_revision_id,
        request.case_revision_id);
    if (!model_case) {
        throw ProjectStateError(
            "model/case revision pair was not found");
    }
    const auto artifacts = projects_->resolve_artifact_revisions(
        request.identity,
        request.project_id,
        artifact_ids);
    if (!artifacts) {
        throw ProjectStateError(
            "artifact revision was not found");
    }

    ValidateModelRequest validation_request;
    validation_request.model_json =
        model_case->executable_model_json;
    validation_request.case_id = model_case->case_id;
    validation_request.artifacts = artifacts->snapshot;
    validation_request.components = artifacts->components;
    return {
        project_model_validation_schema_v1,
        model_case->project_id,
        model_case->model_revision_id,
        model_case->model_checksum,
        model_case->case_revision_id,
        model_case->case_checksum,
        artifacts->revisions,
        simulation_.validate_model(validation_request),
    };
}

ProjectComponentCatalogService::
    ProjectComponentCatalogService(
        std::shared_ptr<ProjectService> projects,
        std::shared_ptr<const SimulationRuntime> runtime)
    : projects_(std::move(projects)),
      simulation_(std::move(runtime)) {
    if (!projects_) {
        throw std::invalid_argument(
            "project component catalog requires a project "
            "service");
    }
}

ProjectComponentCatalogResponse
ProjectComponentCatalogService::get(
    const IdentityContext& identity,
    const std::string& project_id) const {
    const auto resolved =
        projects_->resolve_component_revisions(
            identity, project_id);
    if (!resolved) {
        throw ProjectStateError("project was not found");
    }
    ProjectComponentCatalogResponse response;
    response.project_id = project_id;
    for (const auto& revision : *resolved) {
        if (revision.components.expression_components.size() !=
                1U ||
            revision.revisions.size() != 1U) {
            throw ProjectStateError(
                "component catalog revision did not resolve "
                "to exactly one definition");
        }
        const auto catalog =
            simulation_.get_catalog(revision.components);
        if (!catalog.succeeded()) {
            throw ProjectStateError(
                "project component catalog composition failed: " +
                catalog.error.message);
        }
        const auto& definition =
            revision.components.expression_components.front();
        const auto found = std::find_if(
            catalog.components.begin(),
            catalog.components.end(),
            [&](const auto& component) {
                return component.kind == definition.kind;
            });
        if (found == catalog.components.end()) {
            throw ProjectStateError(
                "resolved component definition is missing from "
                "the composed runtime catalog");
        }
        response.components.push_back({
            revision.revisions.front(),
            *found,
            definition,
            catalog.fingerprint,
        });
    }
    return response;
}

}  // namespace thermox::service
