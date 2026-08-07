#pragma once

#include "thermox/platform/engineering_artifact.hpp"
#include "thermox/platform/safe_expression.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace thermox::platform {

inline constexpr char regime_map_artifact_type[] =
    "thermox.regime_map";
inline constexpr char regime_map_artifact_schema_v1[] =
    "thermox.regime_map/v1";

struct RegimeMapVariable {
    std::string name;
    std::string dimension;
};

// A criterion is satisfied when its scalar expression lies inside the
// declared interval. Expressions use the bounded SafeExpression grammar.
struct RegimeMapCriterion {
    std::string expression;
    std::string dimension{"dimensionless"};
    std::optional<double> minimum;
    std::optional<double> maximum;
    bool minimum_inclusive{true};
    bool maximum_inclusive{true};
};

struct RegimeMapRegion {
    std::string id;
    std::string regime;
    int priority{0};
    std::vector<RegimeMapCriterion> criteria;
};

// A packaged, cited map definition. Templates remain separate from immutable
// project artifacts: users instantiate a template into an artifact with their
// own identity before binding it to a model.
struct RegimeMapTemplateDescriptor {
    std::string id;
    std::string version;
    std::string display_name;
    std::string category;
    std::string reference;
    std::string scope;
    std::vector<RegimeMapVariable> inputs;
    std::vector<RegimeMapRegion> regions;
};

struct RegimeMapArtifactIdentity {
    std::string id;
    std::string revision;
    std::string checksum_sha256;
};

class RegimeMapTemplateRegistry {
public:
    void register_template(RegimeMapTemplateDescriptor descriptor);
    [[nodiscard]] const RegimeMapTemplateDescriptor& require_template(
        const std::string& id) const;
    [[nodiscard]] std::vector<RegimeMapTemplateDescriptor>
        descriptors() const;

private:
    std::map<std::string, RegimeMapTemplateDescriptor> templates_;
};

struct RegimeMapEvaluation {
    std::string selected_region;
    std::string selected_regime;
    std::string error;

    [[nodiscard]] bool succeeded() const noexcept {
        return error.empty();
    }
};

class RegimeMapArtifact final : public EngineeringArtifact {
public:
    RegimeMapArtifact(
        std::string artifact_id,
        std::string artifact_schema_version,
        std::string artifact_revision,
        std::string artifact_checksum_sha256,
        std::vector<RegimeMapVariable> inputs,
        std::vector<RegimeMapRegion> regions);

    [[nodiscard]] std::string_view artifact_type()
        const noexcept override {
        return regime_map_artifact_type;
    }
    void validate() const override;

    [[nodiscard]] const std::vector<RegimeMapVariable>& inputs()
        const noexcept;
    [[nodiscard]] const std::vector<RegimeMapRegion>& regions()
        const noexcept;
    [[nodiscard]] RegimeMapEvaluation classify(
        const std::map<std::string, double>& inputs) const;

private:
    std::vector<RegimeMapVariable> inputs_;
    std::vector<RegimeMapRegion> regions_;
    std::vector<std::vector<SafeExpression>> compiled_criteria_;
};

[[nodiscard]] RegimeMapTemplateRegistry
make_default_regime_map_template_registry();

[[nodiscard]] RegimeMapArtifact instantiate_regime_map_template(
    const RegimeMapTemplateDescriptor& descriptor,
    RegimeMapArtifactIdentity identity);

}  // namespace thermox::platform
