#pragma once

#include <map>
#include <string>
#include <vector>

namespace thermox::platform {

struct ScalarValue {
    double value_si{0.0};
    std::string unit{"dimensionless"};
    std::string dimension{"dimensionless"};
};

struct MediumDefinition {
    std::string id;
    std::string backend;
    std::string substance;
};

struct PortDefinition {
    std::string domain;
    std::string medium;
    std::string direction;
};

struct ComponentDefinition {
    std::string id;
    std::string label;
    std::string kind;
    std::string version;
    std::map<std::string, PortDefinition> ports;
    std::map<std::string, ScalarValue> parameters;
};

struct ConnectionDefinition {
    std::string id;
    std::string from;
    std::string to;
    std::string kind;
    std::map<std::string, ScalarValue> parameters;
};

struct CaseDefinition {
    std::string id;
    std::string label;
    std::string mode;
    std::map<std::string, ScalarValue> fixed_values;
    std::map<std::string, ScalarValue> initial_guesses;
    std::map<std::string, ScalarValue> solver_options;
};

struct ModelDocument {
    std::string schema_version;
    std::string model_id;
    std::string name;
    std::string revision;
    std::vector<MediumDefinition> media;
    std::vector<ComponentDefinition> components;
    std::vector<ConnectionDefinition> connections;
    std::vector<CaseDefinition> cases;
};

ModelDocument load_model_document(const std::string& path);
ModelDocument parse_model_document_text(const std::string& text);

}  // namespace thermox::platform
