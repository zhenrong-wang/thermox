#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace thermox::platform {

class UnitRegistry;

struct ScalarValue {
    double value_si{0.0};
    std::string unit{"dimensionless"};
    std::string dimension{"dimensionless"};
};

struct MediumDefinition {
    std::string id;
    std::string backend;
    std::string substance;
    std::string package_version;
};

struct MaterialDefinition {
    std::string id;
    std::string backend;
    std::string mechanism;
    std::string phase;
    std::string package_version;
    std::vector<std::string> species;
};

struct ComponentDefinition {
    std::string id;
    std::string label;
    std::string kind;
    std::string version;
    std::map<std::string, std::string> medium_bindings;
    std::map<std::string, std::string> material_bindings;
    std::map<std::string, std::string> artifact_bindings;
    std::map<std::string, ScalarValue> parameters;
};

struct ConnectionDefinition {
    std::string id;
    std::string from;
    std::string to;
    std::string kind;
    std::string contract_version;
    std::map<std::string, ScalarValue> parameters;
};

// Assemblies are declaration-time hierarchy. They own no equations; before
// compilation they are deterministically expanded into ordinary components
// and connections. A public port resolves to one child component or nested
// assembly port, preserving the component registry as the physics boundary.
struct AssemblyPortDefinition {
    std::string name;
    std::string endpoint;
};

struct AssemblyParameterDefinition {
    std::string name;
    // child.parameter, where child may be a component or nested assembly.
    std::string target;
};

struct AssemblyDefinition {
    std::string id;
    std::string label;
    std::vector<ComponentDefinition> components;
    std::vector<ConnectionDefinition> connections;
    std::vector<AssemblyDefinition> assemblies;
    std::vector<AssemblyPortDefinition> ports;
    std::vector<AssemblyParameterDefinition> parameters;
};

struct InputSchedulePointDefinition {
    ScalarValue time;
    ScalarValue value;
};

struct InputScheduleDefinition {
    // The first/last values are held outside the declared knot interval.
    // Piecewise-linear interpolation is continuous at every knot.
    std::string interpolation;
    std::vector<InputSchedulePointDefinition> points;
};

struct StateEventDefinition {
    struct Action {
        std::string type;
        std::string target;
        ScalarValue value;
    };

    std::string id;
    std::string target;
    ScalarValue threshold;
    std::string direction;
    bool terminal{false};
    std::vector<Action> actions;
};

struct CaseDefinition {
    std::string id;
    std::string label;
    std::string mode;
    std::map<std::string, ScalarValue> parameter_overrides;
    std::map<std::string, ScalarValue> fixed_values;
    std::map<std::string, InputScheduleDefinition> input_schedules;
    std::map<std::string, ScalarValue> initial_guesses;
    std::vector<StateEventDefinition> state_events;
    std::map<std::string, ScalarValue> solver_options;
};

struct CalibrationParameterDefinition {
    std::string id;
    std::string label;
    std::string scope;
    std::vector<std::string> targets;
    std::vector<std::string> case_ids;
    std::optional<ScalarValue> lower_bound;
    std::optional<ScalarValue> upper_bound;
    std::optional<ScalarValue> prior_mean;
    std::optional<ScalarValue> prior_sigma;
};

struct CalibrationObservationDefinition {
    std::string id;
    std::string label;
    std::string case_id;
    std::string target;
    ScalarValue measured;
    ScalarValue sigma;
    // Required for transient cases and forbidden for steady cases.
    // Stored in SI seconds with dimension "time".
    std::optional<ScalarValue> time;
};

struct MeasurementCorrelationDefinition {
    std::string first_observation_id;
    std::string second_observation_id;
    double correlation{0.0};
};

struct CalibrationDefinition {
    std::string id;
    std::string label;
    std::vector<CalibrationParameterDefinition> parameters;
    std::vector<CalibrationObservationDefinition> observations;
    std::vector<MeasurementCorrelationDefinition>
        measurement_correlations;
};

struct ModelDocument {
    std::string schema_version;
    std::string model_id;
    std::string name;
    std::string revision;
    std::vector<MediumDefinition> media;
    std::vector<MaterialDefinition> materials;
    std::vector<ComponentDefinition> components;
    std::vector<ConnectionDefinition> connections;
    std::vector<AssemblyDefinition> assemblies;
    std::vector<CaseDefinition> cases;
    std::vector<CalibrationDefinition> calibrations;
};

// Returns the executable, hierarchy-free document used by steady and
// transient compilers. Flattened child IDs use '/' as a stable hierarchy
// separator (for example compressor/stage_01).
ModelDocument flatten_model_document(const ModelDocument& document);

ModelDocument load_model_document(const std::string& path);
ModelDocument parse_model_document_text(const std::string& text);
ModelDocument parse_model_document_text(
    const std::string& text,
    const UnitRegistry& units);
// Persistence-facing topology documents deliberately contain no operating
// cases or calibration campaigns. The returned ModelDocument therefore has
// empty cases/calibrations and can be composed into the existing compiler
// input without changing numerical or component contracts.
ModelDocument parse_topology_document_text(
    const std::string& text);
ModelDocument parse_topology_document_text(
    const std::string& text,
    const UnitRegistry& units);
CaseDefinition parse_case_document_text(
    const std::string& text);
CaseDefinition parse_case_document_text(
    const std::string& text,
    const UnitRegistry& units);
CalibrationDefinition parse_calibration_document_text(
    const std::string& text);
CalibrationDefinition parse_calibration_document_text(
    const std::string& text,
    const UnitRegistry& units);
ScalarValue parse_scalar_value_document_text(
    const std::string& text);
ScalarValue parse_scalar_value_document_text(
    const std::string& text,
    const UnitRegistry& units);
MediumDefinition parse_medium_definition_text(
    const std::string& text);
MaterialDefinition parse_material_definition_text(
    const std::string& text);
ComponentDefinition parse_component_definition_text(
    const std::string& text,
    const ModelDocument& context);
ComponentDefinition parse_component_definition_text(
    const std::string& text,
    const ModelDocument& context,
    const UnitRegistry& units);
AssemblyDefinition parse_assembly_definition_text(
    const std::string& text,
    const ModelDocument& context);
AssemblyDefinition parse_assembly_definition_text(
    const std::string& text,
    const ModelDocument& context,
    const UnitRegistry& units);
ConnectionDefinition parse_connection_definition_text(
    const std::string& text);
ConnectionDefinition parse_connection_definition_text(
    const std::string& text,
    const UnitRegistry& units);

}  // namespace thermox::platform
