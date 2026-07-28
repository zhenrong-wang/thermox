#pragma once

#include "thermox/dae_equation_system.hpp"
#include "thermox/equation_system.hpp"
#include "thermox/nonlinear_solver.hpp"
#include "thermox/platform/model_document.hpp"
#include "thermox/platform/performance_map.hpp"
#include "thermox/physics/property_registry.hpp"

#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace thermox::platform {

struct PortModelDescriptor {
    std::string name;
    std::string domain;
    std::string direction;
    std::size_t maximum_connections{1};
};

struct ParameterModelDescriptor {
    std::string name;
    std::string dimension{"dimensionless"};
    bool required{true};
    std::optional<double> default_value;
    double lower_bound{-std::numeric_limits<double>::infinity()};
    double upper_bound{std::numeric_limits<double>::infinity()};
    bool lower_inclusive{true};
    bool upper_inclusive{true};
};

struct ArtifactModelDescriptor {
    std::string role;
    std::string artifact_type;
    bool required{true};
};

struct TransientVariableDescriptor {
    std::string port_name;
    std::string variable_name;
    DaeVariableKind kind{DaeVariableKind::algebraic};
    double derivative_scale{1.0};
};

struct InternalVariableDescriptor {
    std::string name;
    DaeVariableKind kind{DaeVariableKind::algebraic};
    double initial_value{0.0};
    double state_scale{1.0};
    double initial_derivative{0.0};
    double derivative_scale{1.0};
    double lower_bound{-std::numeric_limits<double>::infinity()};
    double upper_bound{std::numeric_limits<double>::infinity()};
    std::string dimension{"unspecified"};
};

struct ComponentModelDescriptor {
    std::string kind;
    std::string version;
    std::vector<PortModelDescriptor> ports;
    std::vector<ParameterModelDescriptor> parameters;
    std::vector<ArtifactModelDescriptor> artifacts;
    std::vector<physics::PropertyCapability> required_property_capabilities;
    bool supports_steady{true};
    bool supports_transient{false};
    std::vector<TransientVariableDescriptor> transient_variables;
    std::vector<InternalVariableDescriptor> internal_variables;
};

struct ComponentCompileContext {
    const ComponentDefinition& component;
    const CaseDefinition* active_case{nullptr};
    std::map<std::string, std::size_t> port_variables;
    std::map<std::string, std::size_t> internal_variables;
    std::map<std::string, std::shared_ptr<const physics::PropertyPackage>> port_properties;
    std::map<
        std::string,
        std::shared_ptr<const PerformanceMapArtifact>>
        performance_maps;
};

class ComponentModel {
public:
    virtual ~ComponentModel() = default;
    virtual const ComponentModelDescriptor& descriptor() const = 0;
    virtual void add_equations(const ComponentCompileContext& context,
                               EquationSystemBuilder& system) const = 0;
    virtual void add_transient_equations(const ComponentCompileContext& context,
                                         DaeEquationSystemBuilder& system) const;
};

class MetadataComponentModel final : public ComponentModel {
public:
    explicit MetadataComponentModel(ComponentModelDescriptor descriptor);

    const ComponentModelDescriptor& descriptor() const override { return descriptor_; }
    void add_equations(const ComponentCompileContext& context,
                       EquationSystemBuilder& system) const override;
    void add_transient_equations(const ComponentCompileContext& context,
                                 DaeEquationSystemBuilder& system) const override;

private:
    ComponentModelDescriptor descriptor_;
};

class ComponentRegistry {
public:
    void register_model(std::shared_ptr<const ComponentModel> model);
    const ComponentModel& require_model(const std::string& kind) const;
    bool contains(const std::string& kind) const;
    std::vector<std::string> kinds() const;
    std::vector<ComponentModelDescriptor> descriptors() const;

private:
    std::map<std::string, std::shared_ptr<const ComponentModel>> models_;
};

struct CompiledPortVariable {
    std::string component_id;
    std::string port_name;
    std::string variable_name;
    std::string full_name;
    std::string domain;
    std::string medium_id;
    std::string dimension;
    std::size_t index{0};
};

struct CompiledConnectionEquation {
    std::string connection_id;
    std::string variable_name;
    std::string residual_name;
    std::size_t residual_index{0};
};

struct CompiledInternalVariable {
    std::string component_id;
    std::string variable_name;
    std::string full_name;
    std::string dimension;
    std::size_t index{0};
};

struct CompiledModelGraph {
    std::string model_id;
    std::optional<std::string> case_id;
    std::vector<CompiledPortVariable> port_variables;
    std::vector<CompiledConnectionEquation> connection_equations;
    std::vector<std::string> reduced_connection_equations;
    std::vector<std::string> fixed_value_equations;
    NonlinearProblem problem;
};

struct CompiledTransientModelGraph {
    std::string model_id;
    std::optional<std::string> case_id;
    std::vector<CompiledPortVariable> port_variables;
    std::vector<CompiledInternalVariable> internal_variables;
    std::vector<CompiledConnectionEquation> connection_equations;
    std::vector<std::string> fixed_value_equations;
    DaeProblem problem;
};

ComponentRegistry make_default_component_registry();
CompiledModelGraph compile_model_graph(const ModelDocument& document,
                                       const ComponentRegistry& registry,
                                       const std::string& case_id = {});
CompiledModelGraph compile_model_graph(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::PropertyPackageRegistry& property_registry,
    const std::string& case_id = {});
CompiledModelGraph compile_model_graph(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::PropertyPackageRegistry& property_registry,
    const PerformanceMapRegistry& performance_map_registry,
    const std::string& case_id = {});
CompiledTransientModelGraph compile_transient_model_graph(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const std::string& case_id = {});
CompiledTransientModelGraph compile_transient_model_graph(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::PropertyPackageRegistry& property_registry,
    const std::string& case_id = {});
CompiledTransientModelGraph compile_transient_model_graph(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::PropertyPackageRegistry& property_registry,
    const PerformanceMapRegistry& performance_map_registry,
    const std::string& case_id = {});

}  // namespace thermox::platform
