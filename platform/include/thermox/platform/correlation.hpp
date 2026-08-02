#pragma once

#include "thermox/platform/engineering_artifact.hpp"
#include "thermox/platform/safe_expression.hpp"

#include <map>
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
        std::map<std::string, double> coefficients,
        std::string expression);

    [[nodiscard]] std::string_view artifact_type()
        const noexcept override {
        return correlation_artifact_type;
    }
    void validate() const override;

    [[nodiscard]] const std::vector<CorrelationVariable>& inputs()
        const noexcept;
    [[nodiscard]] const CorrelationVariable& output()
        const noexcept;
    [[nodiscard]] const std::map<std::string, double>& coefficients()
        const noexcept;
    [[nodiscard]] const std::string& expression() const noexcept;
    [[nodiscard]] CorrelationEvaluation evaluate(
        const std::map<std::string, double>& inputs) const;

private:
    std::vector<CorrelationVariable> inputs_;
    CorrelationVariable output_;
    std::map<std::string, double> coefficients_;
    std::string expression_;
    SafeExpression compiled_;
};

}  // namespace thermox::platform
