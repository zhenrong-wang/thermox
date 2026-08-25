#pragma once

#include "thermox/dae_equation_system.hpp"
#include "thermox/equation_system.hpp"
#include "thermox/nonlinear_solver.hpp"
#include "thermox/platform/model_document.hpp"
#include "thermox/platform/performance_map.hpp"
#include "thermox/physics/property_registry.hpp"
#include "thermox/physics/thermochemistry.hpp"

#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace thermox::platform {

struct ConnectorVariableDescriptor {
    std::string name;
    double initial_value{0.0};
    double scale{1.0};
    std::string dimension{"dimensionless"};
    bool expand_species{false};
};

struct ConnectorDomainDescriptor {
    std::string domain;
    std::string contract_version;
    std::string connection_kind;
    std::vector<ConnectorVariableDescriptor> variables;
};

struct RuntimeExtensionDescriptor {
    std::string package_id;
    std::string package_version;
};

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

struct EventModelDescriptor {
    std::string name;
    std::string dimension;
    std::string direction;
    bool terminal{false};
    int priority{0};
    double hysteresis_si{0.0};
};

// A catalog-visible family of ports whose cardinality is selected by each
// component instance through ComponentDefinition::port_counts. Generated
// ports are named <port_name_prefix>1 ... <port_name_prefix>N.
struct PortGroupModelDescriptor {
    std::string name;
    std::string port_name_prefix;
    std::string domain;
    std::string direction;
    std::size_t minimum_count{0};
    std::size_t maximum_count{0};
    std::size_t maximum_connections{1};
};

struct ComponentModelDescriptor {
    std::string kind;
    std::string version;
    // User-facing physical template identity is deliberately separate from
    // the executable model kind. Multiple calculation models may implement
    // one template (for example a map-based and an efficiency-based
    // compressor).
    std::string template_kind;
    std::string display_name;
    std::string category;
    std::string model_name;
    // "source" or "sink" marks a component as an explicit system
    // boundary. Empty means ordinary equipment; its unconnected ports
    // are still treated as system boundaries in steady result audits.
    std::string system_boundary_role;
    std::vector<PortModelDescriptor> ports;
    std::vector<PortGroupModelDescriptor> port_groups;
    std::vector<ParameterModelDescriptor> parameters;
    std::vector<ArtifactModelDescriptor> artifacts;
    std::vector<physics::PropertyCapability> required_property_capabilities;
    std::vector<physics::ThermochemistryCapability>
        required_thermochemistry_capabilities;
    bool supports_steady{true};
    bool supports_transient{false};
    std::vector<TransientVariableDescriptor> transient_variables;
    std::vector<InternalVariableDescriptor> internal_variables;
    std::vector<std::string> supported_modes;
    std::string default_mode;
    std::vector<EventModelDescriptor> events;
};

struct ComponentCompileContext {
    const ComponentDefinition& component;
    const CaseDefinition* active_case{nullptr};
    std::map<std::string, std::size_t> port_variables;
    std::map<std::string, std::size_t> internal_variables;
    std::map<std::string, std::vector<std::string>> port_species;
    std::map<std::string, std::shared_ptr<const physics::PropertyPackage>> port_properties;
    std::map<
        std::string,
        std::shared_ptr<const physics::ThermochemistryPackage>>
        port_thermochemistry;
    std::map<std::string, std::shared_ptr<const EngineeringArtifact>>
        artifacts;
    std::function<std::string()> active_mode;
    std::function<void(std::string)> set_active_mode;
};

class ComponentModel {
public:
    virtual ~ComponentModel() = default;
    virtual const ComponentModelDescriptor& descriptor() const = 0;
    // Expand instance-sized ports and their associated parameters. The base
    // descriptor remains the catalog contract; the returned descriptor is
    // the executable contract for one declared component instance.
    virtual ComponentModelDescriptor instance_descriptor(
        const ComponentDefinition& component) const {
        (void)component;
        return descriptor();
    }
    // Stable identity for implementation content not represented by the
    // public descriptor. The returned view must remain valid for the
    // lifetime of the model.
    virtual std::string_view implementation_fingerprint() const {
        return {};
    }
    // Descriptors expose the union of required capabilities for catalog
    // discovery. Models may narrow a capability to the ports where it is
    // actually evaluated.
    virtual bool requires_property_capability_on_port(
        physics::PropertyCapability capability,
        std::string_view port) const;
    virtual void add_equations(const ComponentCompileContext& context,
                               EquationSystemBuilder& system) const = 0;
    virtual void add_transient_equations(const ComponentCompileContext& context,
                                         DaeEquationSystemBuilder& system) const;
    virtual void add_transient_events(
        const ComponentCompileContext& context,
        std::vector<DaeEvent>& events) const;
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
    ComponentRegistry();

    void register_connector_domain(
        ConnectorDomainDescriptor descriptor);
    const ConnectorDomainDescriptor&
    require_connector_domain(
        const std::string& domain) const;
    std::vector<ConnectorDomainDescriptor>
    connector_domain_descriptors() const;
    void register_runtime_extension(
        RuntimeExtensionDescriptor descriptor);
    std::vector<RuntimeExtensionDescriptor>
    runtime_extension_descriptors() const;

    void register_model(std::shared_ptr<const ComponentModel> model);
    const ComponentModel& require_model(const std::string& kind) const;
    bool contains(const std::string& kind) const;
    std::vector<std::string> kinds() const;
    std::vector<ComponentModelDescriptor> descriptors() const;

private:
    std::map<std::string, ConnectorDomainDescriptor>
        connector_domains_;
    std::map<std::string, RuntimeExtensionDescriptor>
        runtime_extensions_;
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
    std::string direction;
    int system_boundary_sign{0};
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
    ProblemStructureReport structure;
    NonlinearProblem problem;
};

struct CompiledTransientModelGraph {
    std::string model_id;
    std::optional<std::string> case_id;
    std::vector<CompiledPortVariable> port_variables;
    std::vector<CompiledInternalVariable> internal_variables;
    std::vector<CompiledConnectionEquation> connection_equations;
    std::vector<std::string> fixed_value_equations;
    std::vector<std::string> scheduled_value_equations;
    ProblemStructureReport structure;
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
    const EngineeringArtifactRegistry& artifact_registry,
    const std::string& case_id = {});
CompiledModelGraph compile_model_graph(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::PropertyPackageRegistry& property_registry,
    const EngineeringArtifactRegistry& artifact_registry,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry_registry,
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
    const EngineeringArtifactRegistry& artifact_registry,
    const std::string& case_id = {});
CompiledTransientModelGraph compile_transient_model_graph(
    const ModelDocument& document,
    const ComponentRegistry& registry,
    const physics::PropertyPackageRegistry& property_registry,
    const EngineeringArtifactRegistry& artifact_registry,
    const physics::ThermochemistryPackageRegistry&
        thermochemistry_registry,
    const std::string& case_id = {});

}  // namespace thermox::platform
