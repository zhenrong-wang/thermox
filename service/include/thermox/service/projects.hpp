#pragma once

#include "thermox/service/identity.hpp"
#include "thermox/service/result_projection.hpp"
#include "thermox/service/simulation_service.hpp"
#include "thermox/platform/unit_registry.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace thermox::service {

inline constexpr char project_schema_v1[] =
    "thermox.project/v1";
inline constexpr char model_revision_schema_v1[] =
    "thermox.model_revision/v1";
inline constexpr char case_revision_schema_v1[] =
    "thermox.case_revision/v1";
inline constexpr char artifact_revision_schema_v1[] =
    "thermox.artifact_revision/v1";
inline constexpr char study_revision_schema_v1[] =
    "thermox.study_revision/v1";
inline constexpr char calibration_revision_schema_v1[] =
    "thermox.calibration_revision/v1";
inline constexpr char run_configuration_revision_schema_v3[] =
    "thermox.run_configuration_revision/v3";
inline constexpr char project_model_validation_schema_v1[] =
    "thermox.project_model_validation/v1";
inline constexpr char project_component_catalog_schema_v1[] =
    "thermox.project_component_catalog/v1";

struct ProjectRecord {
    std::string schema_version{project_schema_v1};
    std::string project_id;
    std::string team_id;
    std::string name;
    std::string description;
    std::string created_by_user_id;
    std::chrono::system_clock::time_point created_at;
};

struct ModelRevisionRecord {
    std::string schema_version{model_revision_schema_v1};
    std::string model_revision_id;
    std::string project_id;
    std::string team_id;
    std::uint64_t revision_number{0};
    std::string parent_model_revision_id;
    std::string model_schema_version;
    std::string model_id;
    std::string model_revision_label;
    std::string canonical_model_json;
    std::string checksum;
    std::string created_by_user_id;
    std::chrono::system_clock::time_point created_at;
};

struct CaseRevisionRecord {
    std::string schema_version{case_revision_schema_v1};
    std::string case_revision_id;
    std::string model_revision_id;
    std::string project_id;
    std::string team_id;
    std::string case_id;
    std::uint64_t revision_number{0};
    std::string parent_case_revision_id;
    std::string mode;
    std::string canonical_case_json;
    std::string checksum;
    std::string created_by_user_id;
    std::chrono::system_clock::time_point created_at;
};

struct ArtifactContentManifest {
    std::string object_key;
    std::string media_type;
    std::uint64_t byte_size{0};
    std::string checksum;
};

struct ArtifactRevisionRecord {
    std::string schema_version{artifact_revision_schema_v1};
    std::string artifact_revision_id;
    std::string project_id;
    std::string team_id;
    std::string artifact_id;
    std::uint64_t revision_number{0};
    std::string parent_artifact_revision_id;
    std::string artifact_type;
    std::string artifact_schema_version;
    ArtifactContentManifest content;
    std::string created_by_user_id;
    std::chrono::system_clock::time_point created_at;
};

struct StudyRevisionRecord {
    std::string schema_version{study_revision_schema_v1};
    std::string study_revision_id;
    std::string study_id;
    std::string project_id;
    std::string team_id;
    std::uint64_t revision_number{0};
    std::string parent_study_revision_id;
    std::string model_revision_id;
    std::string case_revision_id;
    std::string intent;
    std::vector<std::string> artifact_revision_ids;
    std::vector<ResultProjection> result_projections;
    std::string checksum;
    std::string created_by_user_id;
    std::chrono::system_clock::time_point created_at;
};

struct CalibrationRevisionRecord {
    std::string schema_version{calibration_revision_schema_v1};
    std::string calibration_revision_id;
    std::string calibration_id;
    std::string project_id;
    std::string team_id;
    std::uint64_t revision_number{0};
    std::string parent_calibration_revision_id;
    std::string model_revision_id;
    std::vector<std::string> training_study_revision_ids;
    std::vector<std::string> validation_study_revision_ids;
    std::string definition_json;
    CalibrationSolverSettings solver;
    std::string checksum;
    std::string created_by_user_id;
    std::chrono::system_clock::time_point created_at;
};

struct RunConfigurationRevisionRecord {
    std::string schema_version{
        run_configuration_revision_schema_v3};
    std::string run_configuration_revision_id;
    std::string run_configuration_id;
    std::string project_id;
    std::string team_id;
    std::uint64_t revision_number{0};
    std::string parent_run_configuration_revision_id;
    std::string study_revision_id;
    SteadySolverSettings steady_solver;
    TransientSolverSettings transient_solver;
    std::string checksum;
    std::string created_by_user_id;
    std::chrono::system_clock::time_point created_at;
};

class EngineeringArtifactContentStore {
public:
    virtual ~EngineeringArtifactContentStore() = default;

    virtual ArtifactContentManifest put_json(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& artifact_id,
        const std::string& artifact_schema_version,
        const std::string& canonical_json) = 0;
    virtual std::optional<std::string> get(
        const ArtifactContentManifest& manifest) const = 0;
};

class ProjectRequestError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

class ProjectStateError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ProjectRepository {
public:
    virtual ~ProjectRepository() = default;

    virtual ProjectRecord create_project(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& name,
        const std::string& description) = 0;
    virtual std::optional<ProjectRecord> get_project(
        const std::string& team_id,
        const std::string& project_id) const = 0;
    virtual std::vector<ProjectRecord> list_projects(
        const std::string& team_id) const = 0;

    virtual ModelRevisionRecord create_model_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& parent_model_revision_id,
        const std::string& model_schema_version,
        const std::string& model_id,
        const std::string& model_revision_label,
        const std::string& canonical_model_json,
        const std::string& checksum) = 0;
    virtual std::optional<ModelRevisionRecord>
    get_model_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& model_revision_id) const = 0;
    virtual std::vector<ModelRevisionRecord>
    list_model_revisions(
        const std::string& team_id,
        const std::string& project_id) const = 0;

    virtual CaseRevisionRecord create_case_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& model_revision_id,
        const std::string& parent_case_revision_id,
        const std::string& case_id,
        const std::string& mode,
        const std::string& canonical_case_json,
        const std::string& checksum) = 0;
    virtual std::optional<CaseRevisionRecord>
    get_case_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& model_revision_id,
        const std::string& case_revision_id) const = 0;
    virtual std::vector<CaseRevisionRecord>
    list_case_revisions(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& model_revision_id) const = 0;

    virtual ArtifactRevisionRecord create_artifact_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& artifact_id,
        const std::string& parent_artifact_revision_id,
        const std::string& artifact_type,
        const std::string& artifact_schema_version,
        const ArtifactContentManifest& content) = 0;
    virtual std::optional<ArtifactRevisionRecord>
    get_artifact_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& artifact_revision_id) const = 0;
    virtual std::vector<ArtifactRevisionRecord>
    list_artifact_revisions(
        const std::string& team_id,
        const std::string& project_id) const = 0;

    virtual StudyRevisionRecord create_study_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& study_id,
        const std::string& parent_study_revision_id,
        const std::string& model_revision_id,
        const std::string& case_revision_id,
        const std::string& intent,
        const std::vector<std::string>& artifact_revision_ids,
        const std::vector<ResultProjection>& result_projections,
        const std::string& checksum) = 0;
    virtual std::optional<StudyRevisionRecord> get_study_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& study_revision_id) const = 0;
    virtual std::vector<StudyRevisionRecord> list_study_revisions(
        const std::string& team_id,
        const std::string& project_id) const = 0;

    virtual CalibrationRevisionRecord create_calibration_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& calibration_id,
        const std::string& parent_calibration_revision_id,
        const std::string& model_revision_id,
        const std::vector<std::string>& training_study_revision_ids,
        const std::vector<std::string>& validation_study_revision_ids,
        const std::string& definition_json,
        const CalibrationSolverSettings& solver,
        const std::string& checksum) = 0;
    virtual std::optional<CalibrationRevisionRecord>
    get_calibration_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& calibration_revision_id) const = 0;
    virtual std::vector<CalibrationRevisionRecord>
    list_calibration_revisions(
        const std::string& team_id,
        const std::string& project_id) const = 0;

    virtual RunConfigurationRevisionRecord
    create_run_configuration_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& run_configuration_id,
        const std::string&
            parent_run_configuration_revision_id,
        const std::string& study_revision_id,
        const SteadySolverSettings& steady_solver,
        const TransientSolverSettings& transient_solver,
        const std::string& checksum) = 0;
    virtual std::optional<RunConfigurationRevisionRecord>
    get_run_configuration_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string&
            run_configuration_revision_id) const = 0;
    virtual std::vector<RunConfigurationRevisionRecord>
    list_run_configuration_revisions(
        const std::string& team_id,
        const std::string& project_id) const = 0;
};

struct CreateProjectRequest {
    IdentityContext identity;
    std::string name;
    std::string description;
};

struct CreateModelRevisionRequest {
    IdentityContext identity;
    std::string project_id;
    std::string parent_model_revision_id;
    std::string model_json;
};

enum class GraphEntityType {
    medium,
    material,
    component,
    connection,
};

enum class GraphEditAction {
    upsert,
    remove,
};

struct GraphEditOperation {
    GraphEditAction action{GraphEditAction::upsert};
    GraphEntityType entity_type{GraphEntityType::component};
    std::string entity_id;
    std::string entity_json;
    bool cascade{false};
};

struct ApplyGraphEditsRequest {
    IdentityContext identity;
    std::string project_id;
    std::string base_model_revision_id;
    std::vector<GraphEditOperation> operations;
};

struct CreateCaseRevisionRequest {
    IdentityContext identity;
    std::string project_id;
    std::string model_revision_id;
    std::string parent_case_revision_id;
    std::string case_json;
};

enum class CaseEditField {
    label,
    mode,
    parameter_override,
    fixed_value,
    initial_guess,
    solver_option,
};

enum class CaseEditAction {
    upsert,
    remove,
};

struct CaseEditOperation {
    CaseEditAction action{CaseEditAction::upsert};
    CaseEditField field{CaseEditField::fixed_value};
    std::string key;
    std::string string_value;
    std::string scalar_json;
};

struct ApplyCaseEditsRequest {
    IdentityContext identity;
    std::string project_id;
    std::string model_revision_id;
    std::string base_case_revision_id;
    std::vector<CaseEditOperation> operations;
};

struct CreateArtifactRevisionRequest {
    IdentityContext identity;
    std::string project_id;
    std::string artifact_id;
    std::string parent_artifact_revision_id;
    std::string artifact_type;
    std::string artifact_schema_version;
    std::string artifact_json;
};

struct CreateRunConfigurationRevisionRequest {
    IdentityContext identity;
    std::string project_id;
    std::string run_configuration_id;
    std::string parent_run_configuration_revision_id;
    std::string study_revision_id;
    SteadySolverSettings steady_solver;
    TransientSolverSettings transient_solver;
};

struct CreateStudyRevisionRequest {
    IdentityContext identity;
    std::string project_id;
    std::string study_id;
    std::string parent_study_revision_id;
    std::string model_revision_id;
    std::string case_revision_id;
    std::string intent;
    std::vector<std::string> artifact_revision_ids;
    std::vector<ResultProjection> result_projections;
};

struct CreateCalibrationRevisionRequest {
    IdentityContext identity;
    std::string project_id;
    std::string calibration_id;
    std::string parent_calibration_revision_id;
    std::string model_revision_id;
    std::vector<std::string> training_study_revision_ids;
    std::vector<std::string> validation_study_revision_ids;
    std::string definition_json;
    CalibrationSolverSettings solver;
};

struct ResolvedEngineeringArtifacts {
    SimulationArtifactBundle snapshot;
    SimulationComponentBundle components;
    std::vector<ArtifactRevisionRecord> revisions;
};

struct ResolvedModelCase {
    std::string project_id;
    std::string model_revision_id;
    std::string model_checksum;
    std::string case_revision_id;
    std::string case_checksum;
    std::string case_id;
    std::string mode;
    std::string executable_model_json;
};

struct ResolvedRunConfiguration {
    RunConfigurationRevisionRecord configuration;
    StudyRevisionRecord study;
    ResolvedModelCase model_case;
    ResolvedEngineeringArtifacts artifacts;
};

struct ResolvedCalibration {
    CalibrationRevisionRecord calibration;
    ModelRevisionRecord model;
    std::vector<StudyRevisionRecord> studies;
    std::string executable_model_json;
    std::vector<StudyPredictionCase> validation_predictions;
    ResolvedEngineeringArtifacts artifacts;
};

struct ValidateProjectModelRequest {
    IdentityContext identity;
    std::string project_id;
    std::string model_revision_id;
    std::string case_revision_id;
    std::vector<std::string> artifact_revision_ids;
};

struct ProjectModelValidationResponse {
    std::string schema_version{
        project_model_validation_schema_v1};
    std::string project_id;
    std::string model_revision_id;
    std::string model_checksum;
    std::string case_revision_id;
    std::string case_checksum;
    std::vector<ArtifactRevisionRecord> artifact_revisions;
    ValidateModelResponse validation;
};

struct ProjectComponentCatalogEntry {
    ArtifactRevisionRecord source;
    ComponentType component;
    ExpressionComponentInput definition;
    std::string catalog_fingerprint;
};

struct ProjectComponentCatalogResponse {
    std::string schema_version{
        project_component_catalog_schema_v1};
    std::string project_id;
    std::vector<ProjectComponentCatalogEntry> components;
};

class ProjectService {
public:
    explicit ProjectService(
        std::shared_ptr<ProjectRepository> repository);
    ProjectService(
        std::shared_ptr<ProjectRepository> repository,
        std::shared_ptr<EngineeringArtifactContentStore>
            artifact_content);
    ProjectService(
        std::shared_ptr<ProjectRepository> repository,
        std::shared_ptr<EngineeringArtifactContentStore>
            artifact_content,
        platform::UnitRegistry units);

    [[nodiscard]] ProjectRecord create_project(
        const CreateProjectRequest& request) const;
    [[nodiscard]] std::optional<ProjectRecord> get_project(
        const IdentityContext& identity,
        const std::string& project_id) const;
    [[nodiscard]] std::vector<ProjectRecord> list_projects(
        const IdentityContext& identity) const;
    [[nodiscard]] ModelRevisionRecord create_model_revision(
        const CreateModelRevisionRequest& request) const;
    [[nodiscard]] std::optional<ModelRevisionRecord>
    get_model_revision(
        const IdentityContext& identity,
        const std::string& project_id,
        const std::string& model_revision_id) const;
    [[nodiscard]] std::vector<ModelRevisionRecord>
    list_model_revisions(
        const IdentityContext& identity,
        const std::string& project_id) const;
    [[nodiscard]] ModelRevisionRecord apply_graph_edits(
        const ApplyGraphEditsRequest& request) const;
    [[nodiscard]] CaseRevisionRecord create_case_revision(
        const CreateCaseRevisionRequest& request) const;
    [[nodiscard]] CaseRevisionRecord apply_case_edits(
        const ApplyCaseEditsRequest& request) const;
    [[nodiscard]] std::optional<CaseRevisionRecord>
    get_case_revision(
        const IdentityContext& identity,
        const std::string& project_id,
        const std::string& model_revision_id,
        const std::string& case_revision_id) const;
    [[nodiscard]] std::vector<CaseRevisionRecord>
    list_case_revisions(
        const IdentityContext& identity,
        const std::string& project_id,
        const std::string& model_revision_id) const;
    [[nodiscard]] std::optional<ResolvedModelCase>
    resolve_model_case(
        const IdentityContext& identity,
        const std::string& project_id,
        const std::string& model_revision_id,
        const std::string& case_revision_id) const;
    [[nodiscard]] ArtifactRevisionRecord
    create_artifact_revision(
        const CreateArtifactRevisionRequest& request) const;
    [[nodiscard]] std::optional<ArtifactRevisionRecord>
    get_artifact_revision(
        const IdentityContext& identity,
        const std::string& project_id,
        const std::string& artifact_revision_id) const;
    [[nodiscard]] std::vector<ArtifactRevisionRecord>
    list_artifact_revisions(
        const IdentityContext& identity,
        const std::string& project_id) const;
    [[nodiscard]] std::optional<ResolvedEngineeringArtifacts>
    resolve_artifact_revisions(
        const IdentityContext& identity,
        const std::string& project_id,
        const std::vector<std::string>&
            artifact_revision_ids) const;
    [[nodiscard]] std::optional<
        std::vector<ResolvedEngineeringArtifacts>>
    resolve_component_revisions(
        const IdentityContext& identity,
        const std::string& project_id) const;
    [[nodiscard]] RunConfigurationRevisionRecord
    create_run_configuration_revision(
        const CreateRunConfigurationRevisionRequest&
            request) const;
    [[nodiscard]] StudyRevisionRecord create_study_revision(
        const CreateStudyRevisionRequest& request) const;
    [[nodiscard]] std::optional<StudyRevisionRecord>
    get_study_revision(
        const IdentityContext& identity,
        const std::string& project_id,
        const std::string& study_revision_id) const;
    [[nodiscard]] std::vector<StudyRevisionRecord>
    list_study_revisions(
        const IdentityContext& identity,
        const std::string& project_id) const;
    [[nodiscard]] CalibrationRevisionRecord
    create_calibration_revision(
        const CreateCalibrationRevisionRequest& request) const;
    [[nodiscard]] std::optional<CalibrationRevisionRecord>
    get_calibration_revision(
        const IdentityContext& identity,
        const std::string& project_id,
        const std::string& calibration_revision_id) const;
    [[nodiscard]] std::vector<CalibrationRevisionRecord>
    list_calibration_revisions(
        const IdentityContext& identity,
        const std::string& project_id) const;
    [[nodiscard]]
    std::optional<RunConfigurationRevisionRecord>
    get_run_configuration_revision(
        const IdentityContext& identity,
        const std::string& project_id,
        const std::string&
            run_configuration_revision_id) const;
    [[nodiscard]]
    std::vector<RunConfigurationRevisionRecord>
    list_run_configuration_revisions(
        const IdentityContext& identity,
        const std::string& project_id) const;
    [[nodiscard]] std::optional<ResolvedRunConfiguration>
    resolve_run_configuration(
        const IdentityContext& identity,
        const std::string& project_id,
        const std::string&
            run_configuration_revision_id) const;
    [[nodiscard]] std::optional<ResolvedCalibration>
    resolve_calibration(
        const IdentityContext& identity,
        const std::string& project_id,
        const std::string& calibration_revision_id) const;

private:
    std::shared_ptr<ProjectRepository> repository_;
    std::shared_ptr<EngineeringArtifactContentStore>
        artifact_content_;
    platform::UnitRegistry units_;
};

class ProjectModelValidationService {
public:
    ProjectModelValidationService(
        std::shared_ptr<ProjectService> projects,
        std::shared_ptr<const SimulationRuntime> runtime);

    [[nodiscard]] ProjectModelValidationResponse validate(
        const ValidateProjectModelRequest& request) const;

private:
    std::shared_ptr<ProjectService> projects_;
    SimulationService simulation_;
};

class ProjectComponentCatalogService {
public:
    ProjectComponentCatalogService(
        std::shared_ptr<ProjectService> projects,
        std::shared_ptr<const SimulationRuntime> runtime);

    [[nodiscard]] ProjectComponentCatalogResponse get(
        const IdentityContext& identity,
        const std::string& project_id) const;

private:
    std::shared_ptr<ProjectService> projects_;
    SimulationService simulation_;
};

std::string serialize_project_json(
    const ProjectRecord& project);
std::string serialize_projects_json(
    const std::vector<ProjectRecord>& projects);
std::string serialize_model_revision_json(
    const ModelRevisionRecord& revision,
    bool include_model = true);
std::string serialize_model_revisions_json(
    const std::vector<ModelRevisionRecord>& revisions);
std::string serialize_case_revision_json(
    const CaseRevisionRecord& revision,
    bool include_case = true);
std::string serialize_case_revisions_json(
    const std::vector<CaseRevisionRecord>& revisions);
std::string serialize_artifact_revision_json(
    const ArtifactRevisionRecord& revision);
std::string serialize_artifact_revisions_json(
    const std::vector<ArtifactRevisionRecord>& revisions);
std::string serialize_study_revision_json(
    const StudyRevisionRecord& revision);
std::string serialize_study_revisions_json(
    const std::vector<StudyRevisionRecord>& revisions);
std::string serialize_calibration_revision_json(
    const CalibrationRevisionRecord& revision);
std::string serialize_calibration_revisions_json(
    const std::vector<CalibrationRevisionRecord>& revisions);
std::string serialize_run_configuration_revision_json(
    const RunConfigurationRevisionRecord& revision);
std::string serialize_run_configuration_revisions_json(
    const std::vector<RunConfigurationRevisionRecord>& revisions);
std::string serialize_project_model_validation_json(
    const ProjectModelValidationResponse& response);
std::string serialize_project_component_catalog_json(
    const ProjectComponentCatalogResponse& response);

}  // namespace thermox::service
