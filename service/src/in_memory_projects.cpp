#include "thermox/service/in_memory_projects.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace thermox::service {

namespace {

class InMemoryProjectRepository final
    : public ProjectRepository {
public:
    ProjectRecord create_project(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& name,
        const std::string& description) override {
        std::lock_guard lock(mutex_);
        ProjectRecord record;
        record.project_id = next_id("project", next_project_id_++);
        record.team_id = team_id;
        record.name = name;
        record.description = description;
        record.created_by_user_id = created_by_user_id;
        record.created_at = std::chrono::system_clock::now();
        projects_.emplace(record.project_id, record);
        return record;
    }

    std::optional<ProjectRecord> get_project(
        const std::string& team_id,
        const std::string& project_id) const override {
        std::lock_guard lock(mutex_);
        const auto found = projects_.find(project_id);
        if (found == projects_.end() ||
            found->second.team_id != team_id) {
            return std::nullopt;
        }
        return found->second;
    }

    std::vector<ProjectRecord> list_projects(
        const std::string& team_id) const override {
        std::lock_guard lock(mutex_);
        std::vector<ProjectRecord> records;
        for (const auto& [id, record] : projects_) {
            (void)id;
            if (record.team_id == team_id) {
                records.push_back(record);
            }
        }
        std::sort(
            records.begin(),
            records.end(),
            [](const auto& left, const auto& right) {
                return left.created_at < right.created_at;
            });
        return records;
    }

    ModelRevisionRecord create_model_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& parent_model_revision_id,
        const std::string& model_schema_version,
        const std::string& model_id,
        const std::string& model_revision_label,
        const std::string& canonical_model_json,
        const std::string& checksum) override {
        std::lock_guard lock(mutex_);
        const auto project = projects_.find(project_id);
        if (project == projects_.end() ||
            project->second.team_id != team_id) {
            throw ProjectStateError("project was not found");
        }
        if (!parent_model_revision_id.empty()) {
            const auto parent =
                model_revisions_.find(parent_model_revision_id);
            if (parent == model_revisions_.end() ||
                parent->second.team_id != team_id ||
                parent->second.project_id != project_id) {
                throw ProjectStateError(
                    "parent model revision was not found");
            }
        }

        ModelRevisionRecord record;
        record.model_revision_id =
            next_id("model-revision", next_model_revision_id_++);
        record.project_id = project_id;
        record.team_id = team_id;
        record.revision_number =
            ++project_revision_sequences_[project_id];
        record.parent_model_revision_id =
            parent_model_revision_id;
        record.model_schema_version = model_schema_version;
        record.model_id = model_id;
        record.model_revision_label = model_revision_label;
        record.canonical_model_json = canonical_model_json;
        record.checksum = checksum;
        record.created_by_user_id = created_by_user_id;
        record.created_at = std::chrono::system_clock::now();
        model_revisions_.emplace(
            record.model_revision_id, record);
        return record;
    }

    std::optional<ModelRevisionRecord> get_model_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& model_revision_id) const override {
        std::lock_guard lock(mutex_);
        const auto found =
            model_revisions_.find(model_revision_id);
        if (found == model_revisions_.end() ||
            found->second.team_id != team_id ||
            found->second.project_id != project_id) {
            return std::nullopt;
        }
        return found->second;
    }

    std::vector<ModelRevisionRecord> list_model_revisions(
        const std::string& team_id,
        const std::string& project_id) const override {
        std::lock_guard lock(mutex_);
        const auto project = projects_.find(project_id);
        if (project == projects_.end() ||
            project->second.team_id != team_id) {
            return {};
        }
        std::vector<ModelRevisionRecord> records;
        for (const auto& [id, record] : model_revisions_) {
            (void)id;
            if (record.team_id == team_id &&
                record.project_id == project_id) {
                records.push_back(record);
            }
        }
        std::sort(
            records.begin(),
            records.end(),
            [](const auto& left, const auto& right) {
                return left.revision_number <
                    right.revision_number;
            });
        return records;
    }

    CaseRevisionRecord create_case_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& model_revision_id,
        const std::string& parent_case_revision_id,
        const std::string& case_id,
        const std::string& mode,
        const std::string& canonical_case_json,
        const std::string& checksum) override {
        std::lock_guard lock(mutex_);
        const auto model =
            model_revisions_.find(model_revision_id);
        if (model == model_revisions_.end() ||
            model->second.team_id != team_id ||
            model->second.project_id != project_id) {
            throw ProjectStateError(
                "model revision was not found");
        }
        if (!parent_case_revision_id.empty()) {
            const auto parent =
                case_revisions_.find(parent_case_revision_id);
            if (parent == case_revisions_.end() ||
                parent->second.team_id != team_id ||
                parent->second.project_id != project_id ||
                parent->second.model_revision_id !=
                    model_revision_id ||
                parent->second.case_id != case_id) {
                throw ProjectStateError(
                    "parent case revision was not found");
            }
        }

        const std::string sequence_key =
            model_revision_id + '\0' + case_id;
        CaseRevisionRecord record;
        record.case_revision_id =
            next_id("case-revision", next_case_revision_id_++);
        record.model_revision_id = model_revision_id;
        record.project_id = project_id;
        record.team_id = team_id;
        record.case_id = case_id;
        record.revision_number =
            ++case_revision_sequences_[sequence_key];
        record.parent_case_revision_id =
            parent_case_revision_id;
        record.mode = mode;
        record.canonical_case_json = canonical_case_json;
        record.checksum = checksum;
        record.created_by_user_id = created_by_user_id;
        record.created_at = std::chrono::system_clock::now();
        case_revisions_.emplace(record.case_revision_id, record);
        return record;
    }

    std::optional<CaseRevisionRecord> get_case_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& model_revision_id,
        const std::string& case_revision_id) const override {
        std::lock_guard lock(mutex_);
        const auto found =
            case_revisions_.find(case_revision_id);
        if (found == case_revisions_.end() ||
            found->second.team_id != team_id ||
            found->second.project_id != project_id ||
            found->second.model_revision_id !=
                model_revision_id) {
            return std::nullopt;
        }
        return found->second;
    }

    std::vector<CaseRevisionRecord> list_case_revisions(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& model_revision_id) const override {
        std::lock_guard lock(mutex_);
        const auto model =
            model_revisions_.find(model_revision_id);
        if (model == model_revisions_.end() ||
            model->second.team_id != team_id ||
            model->second.project_id != project_id) {
            return {};
        }
        std::vector<CaseRevisionRecord> records;
        for (const auto& [id, record] : case_revisions_) {
            (void)id;
            if (record.team_id == team_id &&
                record.project_id == project_id &&
                record.model_revision_id ==
                    model_revision_id) {
                records.push_back(record);
            }
        }
        std::sort(
            records.begin(),
            records.end(),
            [](const auto& left, const auto& right) {
                if (left.case_id != right.case_id) {
                    return left.case_id < right.case_id;
                }
                return left.revision_number <
                    right.revision_number;
            });
        return records;
    }

    ArtifactRevisionRecord create_artifact_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& artifact_id,
        const std::string& parent_artifact_revision_id,
        const std::string& artifact_type,
        const std::string& artifact_schema_version,
        const ArtifactContentManifest& content) override {
        std::lock_guard lock(mutex_);
        const auto project = projects_.find(project_id);
        if (project == projects_.end() ||
            project->second.team_id != team_id) {
            throw ProjectStateError("project was not found");
        }
        if (!parent_artifact_revision_id.empty()) {
            const auto parent = artifact_revisions_.find(
                parent_artifact_revision_id);
            if (parent == artifact_revisions_.end() ||
                parent->second.team_id != team_id ||
                parent->second.project_id != project_id ||
                parent->second.artifact_id != artifact_id ||
                parent->second.artifact_type != artifact_type) {
                throw ProjectStateError(
                    "parent artifact revision was not found");
            }
        }
        const auto sequence_key =
            project_id + '\0' + artifact_id;
        ArtifactRevisionRecord record;
        record.artifact_revision_id = next_id(
            "artifact-revision",
            next_artifact_revision_id_++);
        record.project_id = project_id;
        record.team_id = team_id;
        record.artifact_id = artifact_id;
        record.revision_number =
            ++artifact_revision_sequences_[sequence_key];
        record.parent_artifact_revision_id =
            parent_artifact_revision_id;
        record.artifact_type = artifact_type;
        record.artifact_schema_version =
            artifact_schema_version;
        record.content = content;
        record.created_by_user_id = created_by_user_id;
        record.created_at = std::chrono::system_clock::now();
        artifact_revisions_.emplace(
            record.artifact_revision_id, record);
        return record;
    }

    std::optional<ArtifactRevisionRecord>
    get_artifact_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& artifact_revision_id) const override {
        std::lock_guard lock(mutex_);
        const auto found =
            artifact_revisions_.find(artifact_revision_id);
        if (found == artifact_revisions_.end() ||
            found->second.team_id != team_id ||
            found->second.project_id != project_id) {
            return std::nullopt;
        }
        return found->second;
    }

    std::vector<ArtifactRevisionRecord>
    list_artifact_revisions(
        const std::string& team_id,
        const std::string& project_id) const override {
        std::lock_guard lock(mutex_);
        const auto project = projects_.find(project_id);
        if (project == projects_.end() ||
            project->second.team_id != team_id) {
            return {};
        }
        std::vector<ArtifactRevisionRecord> records;
        for (const auto& [id, record] : artifact_revisions_) {
            (void)id;
            if (record.team_id == team_id &&
                record.project_id == project_id) {
                records.push_back(record);
            }
        }
        std::sort(
            records.begin(),
            records.end(),
            [](const auto& left, const auto& right) {
                if (left.artifact_id != right.artifact_id) {
                    return left.artifact_id <
                        right.artifact_id;
                }
                return left.revision_number <
                    right.revision_number;
            });
        return records;
    }

    PerformanceMapQualityReviewRecord
    create_performance_map_quality_review(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& artifact_revision_id,
        const std::string& artifact_checksum,
        const std::string& supersedes_review_id,
        EngineeringReviewDisposition disposition,
        const std::string& reviewed_scope,
        const std::string& rationale,
        const std::string& quality_schema_version,
        const std::string& quality_snapshot_json,
        const std::string& quality_snapshot_checksum) override {
        std::lock_guard lock(mutex_);
        const auto artifact =
            artifact_revisions_.find(artifact_revision_id);
        if (artifact == artifact_revisions_.end() ||
            artifact->second.team_id != team_id ||
            artifact->second.project_id != project_id ||
            artifact->second.content.checksum != artifact_checksum) {
            throw ProjectStateError(
                "reviewed artifact revision was not found");
        }
        if (!supersedes_review_id.empty()) {
            const auto superseded =
                quality_reviews_.find(supersedes_review_id);
            if (superseded == quality_reviews_.end() ||
                superseded->second.team_id != team_id ||
                superseded->second.project_id != project_id ||
                superseded->second.artifact_revision_id !=
                    artifact_revision_id) {
                throw ProjectStateError(
                    "superseded quality review was not found");
            }
        }
        PerformanceMapQualityReviewRecord record;
        record.review_id = next_id(
            "map-quality-review", next_quality_review_id_++);
        record.project_id = project_id;
        record.team_id = team_id;
        record.artifact_revision_id = artifact_revision_id;
        record.artifact_checksum = artifact_checksum;
        record.supersedes_review_id = supersedes_review_id;
        record.disposition = disposition;
        record.reviewed_scope = reviewed_scope;
        record.rationale = rationale;
        record.quality_schema_version = quality_schema_version;
        record.quality_snapshot_json = quality_snapshot_json;
        record.quality_snapshot_checksum =
            quality_snapshot_checksum;
        record.created_by_user_id = created_by_user_id;
        record.created_at = std::chrono::system_clock::now();
        quality_reviews_.emplace(record.review_id, record);
        return record;
    }

    std::vector<PerformanceMapQualityReviewRecord>
    list_performance_map_quality_reviews(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& artifact_revision_id) const override {
        std::lock_guard lock(mutex_);
        std::vector<PerformanceMapQualityReviewRecord> records;
        for (const auto& [id, record] : quality_reviews_) {
            (void)id;
            if (record.team_id == team_id &&
                record.project_id == project_id &&
                record.artifact_revision_id == artifact_revision_id) {
                records.push_back(record);
            }
        }
        std::sort(
            records.begin(), records.end(),
            [](const auto& left, const auto& right) {
                return left.created_at < right.created_at;
            });
        return records;
    }

    StudyRevisionRecord create_study_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& study_id,
        const std::string& parent_study_revision_id,
        const std::string& model_revision_id,
        const std::string& case_revision_id,
        const std::string& intent,
        const std::vector<std::string>& artifact_revision_ids,
        const std::vector<ArtifactQualificationRequirement>&
            artifact_qualification_requirements,
        const std::vector<ArtifactOperatingEnvelope>&
            artifact_operating_envelopes,
        const std::vector<ResultProjection>& result_projections,
        const std::vector<EngineeringAcceptanceCriterion>&
            acceptance_criteria,
        const std::vector<StudyTrajectoryValidationBinding>&
            trajectory_validation_bindings,
        const std::string& checksum) override {
        std::lock_guard lock(mutex_);
        const auto simulation_case = case_revisions_.find(case_revision_id);
        if (simulation_case == case_revisions_.end() ||
            simulation_case->second.team_id != team_id ||
            simulation_case->second.project_id != project_id ||
            simulation_case->second.model_revision_id != model_revision_id) {
            throw ProjectStateError("model/case revision pair was not found");
        }
        if (!parent_study_revision_id.empty()) {
            const auto parent = study_revisions_.find(parent_study_revision_id);
            if (parent == study_revisions_.end() ||
                parent->second.team_id != team_id ||
                parent->second.project_id != project_id ||
                parent->second.study_id != study_id) {
                throw ProjectStateError("parent study revision was not found");
            }
        }
        for (const auto& revision_id : artifact_revision_ids) {
            const auto artifact = artifact_revisions_.find(revision_id);
            if (artifact == artifact_revisions_.end() ||
                artifact->second.team_id != team_id ||
                artifact->second.project_id != project_id) {
                throw ProjectStateError("artifact revision was not found");
            }
        }
        const auto sequence_key = project_id + '\0' + study_id;
        StudyRevisionRecord record;
        record.study_revision_id = next_id(
            "study-revision", next_study_revision_id_++);
        record.study_id = study_id;
        record.project_id = project_id;
        record.team_id = team_id;
        record.revision_number = ++study_revision_sequences_[sequence_key];
        record.parent_study_revision_id = parent_study_revision_id;
        record.model_revision_id = model_revision_id;
        record.case_revision_id = case_revision_id;
        record.intent = intent;
        record.artifact_revision_ids = artifact_revision_ids;
        record.artifact_qualification_requirements =
            artifact_qualification_requirements;
        record.artifact_operating_envelopes =
            artifact_operating_envelopes;
        record.result_projections = result_projections;
        record.acceptance_criteria = acceptance_criteria;
        record.trajectory_validation_bindings =
            trajectory_validation_bindings;
        record.checksum = checksum;
        record.created_by_user_id = created_by_user_id;
        record.created_at = std::chrono::system_clock::now();
        study_revisions_.emplace(record.study_revision_id, record);
        return record;
    }

    std::optional<StudyRevisionRecord> get_study_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& study_revision_id) const override {
        std::lock_guard lock(mutex_);
        const auto found = study_revisions_.find(study_revision_id);
        if (found == study_revisions_.end() ||
            found->second.team_id != team_id ||
            found->second.project_id != project_id) {
            return std::nullopt;
        }
        return found->second;
    }

    std::vector<StudyRevisionRecord> list_study_revisions(
        const std::string& team_id,
        const std::string& project_id) const override {
        std::lock_guard lock(mutex_);
        std::vector<StudyRevisionRecord> records;
        for (const auto& [id, record] : study_revisions_) {
            (void)id;
            if (record.team_id == team_id && record.project_id == project_id) {
                records.push_back(record);
            }
        }
        std::sort(
            records.begin(),
            records.end(),
            [](const auto& left, const auto& right) {
                if (left.study_id != right.study_id) {
                    return left.study_id < right.study_id;
                }
                return left.revision_number <
                    right.revision_number;
            });
        return records;
    }

    CalibrationRevisionRecord create_calibration_revision(
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
        const std::string& checksum) override {
        std::lock_guard lock(mutex_);
        const auto validate_studies = [&](const auto& ids) {
            for (const auto& id : ids) {
                const auto study = study_revisions_.find(id);
                if (study == study_revisions_.end() ||
                    study->second.team_id != team_id ||
                    study->second.project_id != project_id ||
                    study->second.model_revision_id != model_revision_id) {
                    throw ProjectStateError(
                        "calibration Study revision was not found");
                }
            }
        };
        validate_studies(training_study_revision_ids);
        validate_studies(validation_study_revision_ids);
        if (!parent_calibration_revision_id.empty()) {
            const auto parent = calibration_revisions_.find(
                parent_calibration_revision_id);
            if (parent == calibration_revisions_.end() ||
                parent->second.team_id != team_id ||
                parent->second.project_id != project_id ||
                parent->second.calibration_id != calibration_id) {
                throw ProjectStateError(
                    "parent calibration revision was not found");
            }
        }
        const auto key = project_id + '\0' + calibration_id;
        CalibrationRevisionRecord record;
        record.calibration_revision_id = next_id(
            "calibration-revision", next_calibration_revision_id_++);
        record.calibration_id = calibration_id;
        record.project_id = project_id;
        record.team_id = team_id;
        record.revision_number = ++calibration_revision_sequences_[key];
        record.parent_calibration_revision_id =
            parent_calibration_revision_id;
        record.model_revision_id = model_revision_id;
        record.training_study_revision_ids =
            training_study_revision_ids;
        record.validation_study_revision_ids =
            validation_study_revision_ids;
        record.definition_json = definition_json;
        record.solver = solver;
        record.checksum = checksum;
        record.created_by_user_id = created_by_user_id;
        record.created_at = std::chrono::system_clock::now();
        calibration_revisions_.emplace(
            record.calibration_revision_id, record);
        return record;
    }

    std::optional<CalibrationRevisionRecord>
    get_calibration_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& calibration_revision_id) const override {
        std::lock_guard lock(mutex_);
        const auto found = calibration_revisions_.find(
            calibration_revision_id);
        if (found == calibration_revisions_.end() ||
            found->second.team_id != team_id ||
            found->second.project_id != project_id) {
            return std::nullopt;
        }
        return found->second;
    }

    std::vector<CalibrationRevisionRecord>
    list_calibration_revisions(
        const std::string& team_id,
        const std::string& project_id) const override {
        std::lock_guard lock(mutex_);
        std::vector<CalibrationRevisionRecord> records;
        for (const auto& [id, record] : calibration_revisions_) {
            (void)id;
            if (record.team_id == team_id &&
                record.project_id == project_id) {
                records.push_back(record);
            }
        }
        std::sort(
            records.begin(), records.end(),
            [](const auto& left, const auto& right) {
                if (left.calibration_id != right.calibration_id) {
                    return left.calibration_id < right.calibration_id;
                }
                return left.revision_number < right.revision_number;
            });
        return records;
    }

    ReconciliationRevisionRecord create_reconciliation_revision(
        const std::string& team_id,
        const std::string& created_by_user_id,
        const std::string& project_id,
        const std::string& reconciliation_id,
        const std::string& parent_reconciliation_revision_id,
        const std::string& model_revision_id,
        const std::vector<std::string>& constraint_study_revision_ids,
        const std::vector<std::string>& held_out_study_revision_ids,
        const std::string& definition_json,
        ReconciliationMode mode,
        const ReconciliationSolverSettings& solver,
        const ProfileLikelihoodSettings& profile_likelihood,
        const JointConfidenceRegionSettings& joint_confidence_region,
        const std::string& checksum) override {
        std::lock_guard lock(mutex_);
        const auto validate_studies = [&](const auto& ids) {
            for (const auto& id : ids) {
                const auto study = study_revisions_.find(id);
                if (study == study_revisions_.end() ||
                    study->second.team_id != team_id ||
                    study->second.project_id != project_id ||
                    study->second.model_revision_id != model_revision_id) {
                    throw ProjectStateError(
                        "reconciliation Study revision was not found");
                }
            }
        };
        validate_studies(constraint_study_revision_ids);
        validate_studies(held_out_study_revision_ids);
        if (!parent_reconciliation_revision_id.empty()) {
            const auto parent = reconciliation_revisions_.find(
                parent_reconciliation_revision_id);
            if (parent == reconciliation_revisions_.end() ||
                parent->second.team_id != team_id ||
                parent->second.project_id != project_id ||
                parent->second.reconciliation_id != reconciliation_id) {
                throw ProjectStateError(
                    "parent reconciliation revision was not found");
            }
        }
        const auto key = project_id + '\0' + reconciliation_id;
        ReconciliationRevisionRecord record;
        record.reconciliation_revision_id = next_id(
            "reconciliation-revision",
            next_reconciliation_revision_id_++);
        record.reconciliation_id = reconciliation_id;
        record.project_id = project_id;
        record.team_id = team_id;
        record.revision_number =
            ++reconciliation_revision_sequences_[key];
        record.parent_reconciliation_revision_id =
            parent_reconciliation_revision_id;
        record.model_revision_id = model_revision_id;
        record.constraint_study_revision_ids =
            constraint_study_revision_ids;
        record.held_out_study_revision_ids =
            held_out_study_revision_ids;
        record.definition_json = definition_json;
        record.mode = mode;
        record.solver = solver;
        record.profile_likelihood = profile_likelihood;
        record.joint_confidence_region = joint_confidence_region;
        record.checksum = checksum;
        record.created_by_user_id = created_by_user_id;
        record.created_at = std::chrono::system_clock::now();
        reconciliation_revisions_.emplace(
            record.reconciliation_revision_id, record);
        return record;
    }

    std::optional<ReconciliationRevisionRecord>
    get_reconciliation_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& reconciliation_revision_id) const override {
        std::lock_guard lock(mutex_);
        const auto found = reconciliation_revisions_.find(
            reconciliation_revision_id);
        if (found == reconciliation_revisions_.end() ||
            found->second.team_id != team_id ||
            found->second.project_id != project_id) {
            return std::nullopt;
        }
        return found->second;
    }

    std::vector<ReconciliationRevisionRecord>
    list_reconciliation_revisions(
        const std::string& team_id,
        const std::string& project_id) const override {
        std::lock_guard lock(mutex_);
        std::vector<ReconciliationRevisionRecord> records;
        for (const auto& [id, record] : reconciliation_revisions_) {
            (void)id;
            if (record.team_id == team_id &&
                record.project_id == project_id) {
                records.push_back(record);
            }
        }
        std::sort(records.begin(), records.end(),
            [](const auto& left, const auto& right) {
                if (left.reconciliation_id != right.reconciliation_id) {
                    return left.reconciliation_id < right.reconciliation_id;
                }
                return left.revision_number < right.revision_number;
            });
        return records;
    }

    RunConfigurationRevisionRecord
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
        const std::string& checksum) override {
        std::lock_guard lock(mutex_);
        const auto study = study_revisions_.find(study_revision_id);
        if (study == study_revisions_.end() ||
            study->second.team_id != team_id ||
            study->second.project_id != project_id) {
            throw ProjectStateError(
                "study revision was not found");
        }
        if (!parent_run_configuration_revision_id.empty()) {
            const auto parent =
                run_configuration_revisions_.find(
                    parent_run_configuration_revision_id);
            if (parent ==
                    run_configuration_revisions_.end() ||
                parent->second.team_id != team_id ||
                parent->second.project_id != project_id ||
                parent->second.run_configuration_id !=
                    run_configuration_id) {
                throw ProjectStateError(
                    "parent run configuration revision was "
                    "not found");
            }
        }
        const auto sequence_key =
            project_id + '\0' + run_configuration_id;
        RunConfigurationRevisionRecord record;
        record.run_configuration_revision_id = next_id(
            "run-configuration-revision",
            next_run_configuration_revision_id_++);
        record.run_configuration_id = run_configuration_id;
        record.project_id = project_id;
        record.team_id = team_id;
        record.revision_number =
            ++run_configuration_revision_sequences_[
                sequence_key];
        record.parent_run_configuration_revision_id =
            parent_run_configuration_revision_id;
        record.study_revision_id = study_revision_id;
        record.steady_solver = steady_solver;
        record.transient_solver = transient_solver;
        record.checksum = checksum;
        record.created_by_user_id = created_by_user_id;
        record.created_at = std::chrono::system_clock::now();
        run_configuration_revisions_.emplace(
            record.run_configuration_revision_id, record);
        return record;
    }

    std::optional<RunConfigurationRevisionRecord>
    get_run_configuration_revision(
        const std::string& team_id,
        const std::string& project_id,
        const std::string&
            run_configuration_revision_id) const override {
        std::lock_guard lock(mutex_);
        const auto found = run_configuration_revisions_.find(
            run_configuration_revision_id);
        if (found == run_configuration_revisions_.end() ||
            found->second.team_id != team_id ||
            found->second.project_id != project_id) {
            return std::nullopt;
        }
        return found->second;
    }

    std::vector<RunConfigurationRevisionRecord>
    list_run_configuration_revisions(
        const std::string& team_id,
        const std::string& project_id) const override {
        std::lock_guard lock(mutex_);
        std::vector<RunConfigurationRevisionRecord> records;
        for (const auto& [id, record] :
             run_configuration_revisions_) {
            (void)id;
            if (record.team_id == team_id &&
                record.project_id == project_id) {
                records.push_back(record);
            }
        }
        std::sort(
            records.begin(),
            records.end(),
            [](const auto& left, const auto& right) {
                if (left.run_configuration_id !=
                    right.run_configuration_id) {
                    return left.run_configuration_id <
                        right.run_configuration_id;
                }
                return left.revision_number <
                    right.revision_number;
            });
        return records;
    }

private:
    static std::string next_id(
        const std::string& prefix,
        std::uint64_t value) {
        std::ostringstream id;
        id << prefix << '-' << std::setfill('0')
           << std::setw(8) << value;
        return id.str();
    }

    mutable std::mutex mutex_;
    std::uint64_t next_project_id_{1};
    std::uint64_t next_model_revision_id_{1};
    std::uint64_t next_case_revision_id_{1};
    std::uint64_t next_artifact_revision_id_{1};
    std::uint64_t next_quality_review_id_{1};
    std::uint64_t next_study_revision_id_{1};
    std::uint64_t next_calibration_revision_id_{1};
    std::uint64_t next_reconciliation_revision_id_{1};
    std::uint64_t next_run_configuration_revision_id_{1};
    std::unordered_map<std::string, ProjectRecord> projects_;
    std::unordered_map<std::string, ModelRevisionRecord>
        model_revisions_;
    std::unordered_map<std::string, CaseRevisionRecord>
        case_revisions_;
    std::unordered_map<std::string, ArtifactRevisionRecord>
        artifact_revisions_;
    std::unordered_map<std::string, PerformanceMapQualityReviewRecord>
        quality_reviews_;
    std::unordered_map<std::string, StudyRevisionRecord> study_revisions_;
    std::unordered_map<std::string, CalibrationRevisionRecord>
        calibration_revisions_;
    std::unordered_map<std::string, ReconciliationRevisionRecord>
        reconciliation_revisions_;
    std::unordered_map<
        std::string,
        RunConfigurationRevisionRecord>
        run_configuration_revisions_;
    std::unordered_map<std::string, std::uint64_t>
        project_revision_sequences_;
    std::unordered_map<std::string, std::uint64_t>
        case_revision_sequences_;
    std::unordered_map<std::string, std::uint64_t>
        artifact_revision_sequences_;
    std::unordered_map<std::string, std::uint64_t>
        study_revision_sequences_;
    std::unordered_map<std::string, std::uint64_t>
        calibration_revision_sequences_;
    std::unordered_map<std::string, std::uint64_t>
        reconciliation_revision_sequences_;
    std::unordered_map<std::string, std::uint64_t>
        run_configuration_revision_sequences_;
};

class InMemoryEngineeringArtifactContentStore final
    : public EngineeringArtifactContentStore {
public:
    ArtifactContentManifest put_json(
        const std::string& team_id,
        const std::string& project_id,
        const std::string& artifact_id,
        const std::string& artifact_schema_version,
        const std::string& canonical_json) override {
        const auto key =
            team_id + "/" + project_id + "/" + artifact_id +
            "/" + artifact_schema_version + "/" +
            std::to_string(next_id_++);
        content_[key] = canonical_json;
        return {
            key,
            "application/json",
            static_cast<std::uint64_t>(
                canonical_json.size()),
            {},
        };
    }

    std::optional<std::string> get(
        const ArtifactContentManifest& manifest) const override {
        const auto found = content_.find(manifest.object_key);
        if (found == content_.end()) {
            return std::nullopt;
        }
        return found->second;
    }

private:
    std::uint64_t next_id_{1};
    std::unordered_map<std::string, std::string> content_;
};

}  // namespace

std::shared_ptr<ProjectRepository>
make_in_memory_project_repository() {
    return std::make_shared<InMemoryProjectRepository>();
}

std::shared_ptr<EngineeringArtifactContentStore>
make_in_memory_engineering_artifact_content_store() {
    return std::make_shared<
        InMemoryEngineeringArtifactContentStore>();
}

}  // namespace thermox::service
