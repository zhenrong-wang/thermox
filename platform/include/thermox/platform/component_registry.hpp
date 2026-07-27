#pragma once

#include "thermox/equation_system.hpp"
#include "thermox/platform/model_document.hpp"
#include "thermox/nonlinear_solver.hpp"
#include "thermox/physics/property_registry.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace thermox::platform {

struct PortModelDescriptor {
    std::string name;
    std::string domain;
    std::string direction;
};

struct ComponentModelDescriptor {
    std::string kind;
    std::string version;
    std::vector<PortModelDescriptor> ports;
    std::vector<physics::PropertyCapability> required_property_capabilities;
};

struct ComponentCompileContext {
    const ComponentDefinition& component;
    const CaseDefinition* active_case{nullptr};
    std::map<std::string, std::size_t> port_variables;
    std::map<std::string, std::shared_ptr<const physics::PropertyPackage>> port_properties;
};

class ComponentModel {
public:
    virtual ~ComponentModel() = default;
    virtual const ComponentModelDescriptor& descriptor() const = 0;
    virtual void add_equations(const ComponentCompileContext& context,
                               EquationSystemBuilder& system) const = 0;
};

class MetadataComponentModel final : public ComponentModel {
public:
    explicit MetadataComponentModel(ComponentModelDescriptor descriptor);

    const ComponentModelDescriptor& descriptor() const override { return descriptor_; }
    void add_equations(const ComponentCompileContext& context,
                       EquationSystemBuilder& system) const override;

private:
    ComponentModelDescriptor descriptor_;
};

class ComponentRegistry {
public:
    void register_model(std::shared_ptr<const ComponentModel> model);
    const ComponentModel& require_model(const std::string& kind) const;
    bool contains(const std::string& kind) const;
    std::vector<std::string> kinds() const;

private:
    std::map<std::string, std::shared_ptr<const ComponentModel>> models_;
};

struct CompiledPortVariable {
    std::string component_id;
    std::string port_name;
    std::string variable_name;
    std::string full_name;
    std::size_t index{0};
};

struct CompiledConnectionEquation {
    std::string connection_id;
    std::string variable_name;
    std::string residual_name;
    std::size_t residual_index{0};
};

struct CompiledModelGraph {
    std::string model_id;
    std::optional<std::string> case_id;
    std::vector<CompiledPortVariable> port_variables;
    std::vector<CompiledConnectionEquation> connection_equations;
    std::vector<std::string> fixed_value_equations;
    NonlinearProblem problem;
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

}  // namespace thermox::platform
