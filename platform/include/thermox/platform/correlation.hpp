#pragma once

#include "thermox/platform/engineering_artifact.hpp"
#include "thermox/platform/safe_expression.hpp"

#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace thermox::platform {

inline constexpr char correlation_artifact_type[] =
    "thermox.correlation";
inline constexpr char correlation_artifact_schema_v1[] =
    "thermox.correlation/v1";

struct CorrelationVariable {
    std::string name;
    std::string dimension;
};

struct CorrelationEvaluation {
    double value{0.0};
    std::map<std::string, double> input_derivatives;
    std::string error;
    std::string selected_candidate;
    std::string selected_regime;
};

struct CorrelationApplicabilityRange {
    std::string input;
    std::optional<double> minimum;
    std::optional<double> maximum;
    bool minimum_inclusive{true};
    bool maximum_inclusive{true};
};

struct CorrelationApplicabilityAssessment {
    bool applicable{true};
    std::vector<std::string> violations;
};

struct CorrelationCandidate {
    std::string id;
    std::string regime;
    int priority{0};
    std::map<std::string, double> coefficients;
    std::string expression;
    std::vector<CorrelationApplicabilityRange> applicability;
};

struct CorrelationCoefficientDescriptor {
    std::string name;
    std::string dimension{"dimensionless"};
    std::optional<double> default_value;
    double lower_bound{-std::numeric_limits<double>::infinity()};
    double upper_bound{std::numeric_limits<double>::infinity()};
    bool lower_inclusive{true};
    bool upper_inclusive{true};
};

struct CorrelationTemplateDescriptor {
    std::string id;
    std::string version;
    std::string display_name;
    std::string category;
    std::string reference;
    std::vector<CorrelationVariable> inputs;
    CorrelationVariable output;
    std::vector<CorrelationCoefficientDescriptor> coefficients;
    std::string expression;
    std::string regime;
    std::vector<CorrelationApplicabilityRange> applicability;
};

struct CorrelationArtifactIdentity {
    std::string id;
    std::string revision;
    std::string checksum_sha256;
};

struct CorrelationTemplateCandidateBinding {
    std::string template_id;
    std::map<std::string, double> coefficients;
    std::string candidate_id;
    int priority{0};
};

class CorrelationTemplateRegistry {
public:
    void register_template(CorrelationTemplateDescriptor descriptor);
    [[nodiscard]] const CorrelationTemplateDescriptor& require_template(
        const std::string& id) const;
    [[nodiscard]] std::vector<CorrelationTemplateDescriptor>
        descriptors() const;

private:
    std::map<std::string, CorrelationTemplateDescriptor> templates_;
};

class CorrelationArtifact final : public EngineeringArtifact {
public:
    CorrelationArtifact(
        std::string artifact_id,
        std::string artifact_schema_version,
        std::string artifact_revision,
        std::string artifact_checksum_sha256,
        std::vector<CorrelationVariable> inputs,
        CorrelationVariable output,
        std::vector<CorrelationCandidate> candidates);

    [[nodiscard]] std::string_view artifact_type()
        const noexcept override {
        return correlation_artifact_type;
    }
    void validate() const override;

    [[nodiscard]] const std::vector<CorrelationVariable>& inputs()
        const noexcept;
    [[nodiscard]] const CorrelationVariable& output()
        const noexcept;
    [[nodiscard]] const std::vector<CorrelationCandidate>& candidates()
        const noexcept;
    [[nodiscard]] CorrelationApplicabilityAssessment assess_applicability(
        const std::map<std::string, double>& inputs) const;
    [[nodiscard]] CorrelationEvaluation evaluate(
        const std::map<std::string, double>& inputs) const;

private:
    std::vector<CorrelationVariable> inputs_;
    CorrelationVariable output_;
    std::vector<CorrelationCandidate> candidates_;
    std::vector<SafeExpression> compiled_candidates_;
};

[[nodiscard]] CorrelationTemplateRegistry
make_default_correlation_template_registry();

[[nodiscard]] CorrelationArtifact instantiate_correlation_template(
    const CorrelationTemplateDescriptor& descriptor,
    CorrelationArtifactIdentity identity,
    std::map<std::string, double> coefficients,
    std::string candidate_id = "default",
    int priority = 0);

[[nodiscard]] CorrelationArtifact instantiate_correlation_family(
    const CorrelationTemplateRegistry& registry,
    CorrelationArtifactIdentity identity,
    std::vector<CorrelationTemplateCandidateBinding> bindings);

}  // namespace thermox::platform
