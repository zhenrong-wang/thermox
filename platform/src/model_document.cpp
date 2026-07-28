#include "thermox/platform/model_document.hpp"

#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace thermox::platform {

namespace {

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Object, Array };

    Type type{Type::Null};
    bool boolean{false};
    double number{0.0};
    std::string string;
    std::map<std::string, JsonValue> object;
    std::vector<JsonValue> array;
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    JsonValue parse_document() {
        skip_whitespace();
        JsonValue value = parse_value();
        skip_whitespace();
        if (!at_end()) {
            fail("unexpected trailing content");
        }
        return value;
    }

private:
    [[nodiscard]] bool at_end() const { return pos_ >= text_.size(); }

    [[nodiscard]] char peek() const {
        if (at_end()) {
            fail("unexpected end of input");
        }
        return text_[pos_];
    }

    char consume() {
        const char c = peek();
        ++pos_;
        return c;
    }

    void expect(char expected) {
        if (consume() != expected) {
            fail(std::string("expected '") + expected + "'");
        }
    }

    void skip_whitespace() {
        while (!at_end() && std::isspace(static_cast<unsigned char>(text_[pos_])) != 0) {
            ++pos_;
        }
    }

    [[noreturn]] void fail(const std::string& message) const {
        throw std::invalid_argument("invalid JSON at byte " + std::to_string(pos_) + ": " + message);
    }

    JsonValue parse_value() {
        skip_whitespace();
        const char c = peek();
        if (c == '{') {
            return parse_object();
        }
        if (c == '[') {
            return parse_array();
        }
        if (c == '"') {
            JsonValue value;
            value.type = JsonValue::Type::String;
            value.string = parse_string();
            return value;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c)) != 0) {
            return parse_number();
        }
        if (match_literal("true")) {
            JsonValue value;
            value.type = JsonValue::Type::Bool;
            value.boolean = true;
            return value;
        }
        if (match_literal("false")) {
            JsonValue value;
            value.type = JsonValue::Type::Bool;
            value.boolean = false;
            return value;
        }
        if (match_literal("null")) {
            JsonValue value;
            value.type = JsonValue::Type::Null;
            return value;
        }
        fail("expected JSON value");
    }

    bool match_literal(std::string_view literal) {
        if (text_.substr(pos_, literal.size()) == literal) {
            pos_ += literal.size();
            return true;
        }
        return false;
    }

    JsonValue parse_object() {
        JsonValue value;
        value.type = JsonValue::Type::Object;
        expect('{');
        skip_whitespace();
        if (!at_end() && peek() == '}') {
            consume();
            return value;
        }

        while (true) {
            skip_whitespace();
            if (peek() != '"') {
                fail("expected object key string");
            }
            const std::string key = parse_string();
            skip_whitespace();
            expect(':');
            JsonValue member = parse_value();
            const auto inserted = value.object.emplace(key, std::move(member));
            if (!inserted.second) {
                fail("duplicate object key: " + key);
            }
            skip_whitespace();
            const char separator = consume();
            if (separator == '}') {
                break;
            }
            if (separator != ',') {
                fail("expected ',' or '}' in object");
            }
        }
        return value;
    }

    JsonValue parse_array() {
        JsonValue value;
        value.type = JsonValue::Type::Array;
        expect('[');
        skip_whitespace();
        if (!at_end() && peek() == ']') {
            consume();
            return value;
        }

        while (true) {
            value.array.push_back(parse_value());
            skip_whitespace();
            const char separator = consume();
            if (separator == ']') {
                break;
            }
            if (separator != ',') {
                fail("expected ',' or ']' in array");
            }
        }
        return value;
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (true) {
            if (at_end()) {
                fail("unterminated string");
            }
            const char c = consume();
            if (c == '"') {
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20U) {
                fail("unescaped control character in string");
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }

            if (at_end()) {
                fail("unterminated escape sequence");
            }
            const char escaped = consume();
            switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(escaped);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u':
                    for (int i = 0; i < 4; ++i) {
                        if (at_end() || std::isxdigit(static_cast<unsigned char>(consume())) == 0) {
                            fail("invalid unicode escape");
                        }
                    }
                    out.push_back('?');
                    break;
                default:
                    fail("invalid string escape");
            }
        }
    }

    JsonValue parse_number() {
        const std::size_t start = pos_;
        if (!at_end() && peek() == '-') {
            consume();
        }

        if (at_end()) {
            fail("incomplete number");
        }
        if (peek() == '0') {
            consume();
            if (!at_end() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                fail("leading zero in number");
            }
        } else if (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            while (!at_end() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                consume();
            }
        } else {
            fail("expected digit in number");
        }

        if (!at_end() && peek() == '.') {
            consume();
            if (at_end() || std::isdigit(static_cast<unsigned char>(peek())) == 0) {
                fail("expected digit after decimal point");
            }
            while (!at_end() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                consume();
            }
        }

        if (!at_end() && (peek() == 'e' || peek() == 'E')) {
            consume();
            if (!at_end() && (peek() == '+' || peek() == '-')) {
                consume();
            }
            if (at_end() || std::isdigit(static_cast<unsigned char>(peek())) == 0) {
                fail("expected exponent digits");
            }
            while (!at_end() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                consume();
            }
        }

        JsonValue value;
        value.type = JsonValue::Type::Number;
        try {
            value.number = std::stod(std::string(text_.substr(start, pos_ - start)));
        } catch (const std::exception&) {
            fail("number is out of range");
        }
        if (!std::isfinite(value.number)) {
            fail("number is not finite");
        }
        return value;
    }

    std::string_view text_;
    std::size_t pos_{0};
};

const JsonValue& require_object_root(const JsonValue& root) {
    if (root.type != JsonValue::Type::Object) {
        throw std::invalid_argument("model document must be a JSON object");
    }
    return root;
}

const JsonValue* find_member(const JsonValue& object, const std::string& key) {
    if (object.type != JsonValue::Type::Object) {
        return nullptr;
    }
    const auto it = object.object.find(key);
    if (it == object.object.end()) {
        return nullptr;
    }
    return &it->second;
}

const JsonValue& require_member(const JsonValue& object, const std::string& key) {
    const JsonValue* value = find_member(object, key);
    if (value == nullptr) {
        throw std::invalid_argument("missing required field: " + key);
    }
    return *value;
}

std::string require_string(const JsonValue& object, const std::string& key) {
    const JsonValue& value = require_member(object, key);
    if (value.type != JsonValue::Type::String || value.string.empty()) {
        throw std::invalid_argument("field '" + key + "' must be a non-empty string");
    }
    return value.string;
}

std::string require_string_value(
    const JsonValue& value,
    const std::string& field_name) {
    if (value.type != JsonValue::Type::String) {
        throw std::invalid_argument(
            "field '" + field_name + "' must be a string");
    }
    return value.string;
}

double require_number_value(const JsonValue& value, const std::string& field_name) {
    if (value.type != JsonValue::Type::Number) {
        throw std::invalid_argument("field '" + field_name + "' must be a finite number");
    }
    if (!std::isfinite(value.number)) {
        throw std::invalid_argument("field '" + field_name + "' must be finite");
    }
    return value.number;
}

std::string require_unit(const JsonValue& value, const std::string& field_name) {
    if (value.type != JsonValue::Type::Object) {
        throw std::invalid_argument("field '" + field_name + "' must be a number or {value, unit}");
    }
    const std::string unit_key{"unit"};
    const JsonValue& unit = require_member(value, unit_key);
    if (unit.type != JsonValue::Type::String || unit.string.empty()) {
        throw std::invalid_argument("field '" + field_name + ".unit' must be a non-empty string");
    }
    return unit.string;
}

double convert_pressure_to_pa(double value, const std::string& unit) {
    if (unit == "Pa") {
        return value;
    }
    if (unit == "kPa") {
        return value * 1000.0;
    }
    if (unit == "MPa") {
        return value * 1.0e6;
    }
    if (unit == "bar") {
        return value * 100000.0;
    }
    throw std::invalid_argument("unsupported pressure unit: " + unit);
}

double convert_temperature_to_k(double value, const std::string& unit) {
    if (unit == "K") {
        return value;
    }
    if (unit == "C" || unit == "degC") {
        return value + 273.15;
    }
    throw std::invalid_argument("unsupported temperature unit: " + unit);
}

double convert_mass_flow_to_kg_s(double value, const std::string& unit) {
    if (unit == "kg/s") {
        return value;
    }
    if (unit == "kg/h") {
        return value / 3600.0;
    }
    throw std::invalid_argument("unsupported mass-flow unit: " + unit);
}

double convert_cp_to_j_kg_k(double value, const std::string& unit) {
    if (unit == "J/kg/K" || unit == "J/(kg*K)") {
        return value;
    }
    if (unit == "kJ/kg/K" || unit == "kJ/(kg*K)") {
        return value * 1000.0;
    }
    throw std::invalid_argument("unsupported specific-heat unit: " + unit);
}

double convert_dimensionless(double value, const std::string& unit) {
    if (unit == "dimensionless" || unit == "1" || unit.empty()) {
        return value;
    }
    if (unit == "%") {
        return value / 100.0;
    }
    throw std::invalid_argument("unsupported dimensionless unit: " + unit);
}

double convert_power_to_w(double value, const std::string& unit) {
    if (unit == "W") {
        return value;
    }
    if (unit == "kW") {
        return value * 1000.0;
    }
    if (unit == "MW") {
        return value * 1.0e6;
    }
    throw std::invalid_argument("unsupported power unit: " + unit);
}

double convert_thermal_capacity_to_j_k(double value, const std::string& unit) {
    if (unit == "J/K") {
        return value;
    }
    if (unit == "kJ/K") {
        return value * 1000.0;
    }
    if (unit == "MJ/K") {
        return value * 1.0e6;
    }
    throw std::invalid_argument("unsupported thermal-capacity unit: " + unit);
}

double convert_specific_enthalpy_to_j_kg(double value, const std::string& unit) {
    if (unit == "J/kg") {
        return value;
    }
    if (unit == "kJ/kg") {
        return value * 1000.0;
    }
    throw std::invalid_argument("unsupported specific-enthalpy unit: " + unit);
}

ScalarValue make_scalar(double value_si, const std::string& unit, const std::string& dimension) {
    ScalarValue scalar;
    scalar.value_si = value_si;
    scalar.unit = unit;
    scalar.dimension = dimension;
    return scalar;
}

ScalarValue convert_scalar(double value, const std::string& unit, const std::string& field_name) {
    if (unit == "Pa" || unit == "kPa" || unit == "MPa" || unit == "bar") {
        return make_scalar(convert_pressure_to_pa(value, unit), "Pa", "pressure");
    }
    if (unit == "K" || unit == "C" || unit == "degC") {
        return make_scalar(convert_temperature_to_k(value, unit), "K", "temperature");
    }
    if (unit == "kg/s" || unit == "kg/h") {
        return make_scalar(convert_mass_flow_to_kg_s(value, unit), "kg/s", "mass_flow");
    }
    if (unit == "J/kg/K" || unit == "J/(kg*K)" || unit == "kJ/kg/K" || unit == "kJ/(kg*K)") {
        return make_scalar(convert_cp_to_j_kg_k(value, unit), "J/kg/K", "specific_heat");
    }
    if (unit == "J/kg" || unit == "kJ/kg") {
        return make_scalar(convert_specific_enthalpy_to_j_kg(value, unit), "J/kg", "specific_enthalpy");
    }
    if (unit == "W" || unit == "kW" || unit == "MW") {
        return make_scalar(convert_power_to_w(value, unit), "W", "power");
    }
    if (unit == "J/K" || unit == "kJ/K" || unit == "MJ/K") {
        return make_scalar(convert_thermal_capacity_to_j_k(value, unit),
                           "J/K", "thermal_capacity");
    }
    if (unit == "W/K" || unit == "kW/K" || unit == "MW/K") {
        const double multiplier =
            unit == "W/K" ? 1.0 :
            (unit == "kW/K" ? 1.0e3 : 1.0e6);
        return make_scalar(value * multiplier, "W/K",
                           "thermal_conductance");
    }
    if (unit == "m3" || unit == "m^3") {
        return make_scalar(value, "m3", "volume");
    }
    if (unit == "L") {
        return make_scalar(value * 1.0e-3, "m3", "volume");
    }
    if (unit == "kg") {
        return make_scalar(value, "kg", "mass");
    }
    if (unit == "J" || unit == "kJ" || unit == "MJ") {
        const double multiplier =
            unit == "J" ? 1.0 : (unit == "kJ" ? 1.0e3 : 1.0e6);
        return make_scalar(value * multiplier, "J", "energy");
    }
    if (unit == "dimensionless" || unit == "1" || unit == "%" || unit.empty()) {
        return make_scalar(convert_dimensionless(value, unit), "dimensionless", "dimensionless");
    }
    throw std::invalid_argument("unsupported unit for field '" + field_name + "': " + unit);
}

ScalarValue parse_scalar_value(const JsonValue& value, const std::string& field_name) {
    if (value.type == JsonValue::Type::Number) {
        return make_scalar(require_number_value(value, field_name), "dimensionless", "dimensionless");
    }
    if (value.type != JsonValue::Type::Object) {
        throw std::invalid_argument("field '" + field_name + "' must be a number or {value, unit}");
    }
    const double scalar = require_number_value(require_member(value, "value"), field_name + ".value");
    const std::string unit = require_unit(value, field_name);
    return convert_scalar(scalar, unit, field_name);
}

bool is_scalar_like(const JsonValue& value) {
    return value.type == JsonValue::Type::Number ||
           (value.type == JsonValue::Type::Object && find_member(value, "value") != nullptr &&
            find_member(value, "unit") != nullptr);
}

std::map<std::string, ScalarValue> parse_scalar_map(const JsonValue& object,
                                                    const std::string& field_name,
                                                    bool skip_non_scalar = false) {
    if (object.type != JsonValue::Type::Object) {
        throw std::invalid_argument("field '" + field_name + "' must be an object");
    }
    std::map<std::string, ScalarValue> scalars;
    for (const auto& [key, value] : object.object) {
        if (!is_scalar_like(value)) {
            if (skip_non_scalar) {
                continue;
            }
            throw std::invalid_argument("field '" + field_name + "." + key + "' must be a scalar quantity");
        }
        scalars.emplace(key, parse_scalar_value(value, field_name + "." + key));
    }
    return scalars;
}

std::string optional_string(const JsonValue& object, const std::string& key) {
    const JsonValue* value = find_member(object, key);
    if (value == nullptr) {
        return {};
    }
    if (value->type != JsonValue::Type::String) {
        throw std::invalid_argument("field '" + key + "' must be a string");
    }
    return value->string;
}

const JsonValue& require_object_member(const JsonValue& object, const std::string& key) {
    const JsonValue& value = require_member(object, key);
    if (value.type != JsonValue::Type::Object) {
        throw std::invalid_argument("field '" + key + "' must be an object");
    }
    return value;
}

const JsonValue& require_array_member(const JsonValue& object, const std::string& key) {
    const JsonValue& value = require_member(object, key);
    if (value.type != JsonValue::Type::Array) {
        throw std::invalid_argument("field '" + key + "' must be an array");
    }
    return value;
}

const JsonValue* optional_array_member(
    const JsonValue& object,
    const std::string& key) {
    const JsonValue* value = find_member(object, key);
    if (value == nullptr) {
        return nullptr;
    }
    if (value->type != JsonValue::Type::Array) {
        throw std::invalid_argument(
            "field '" + key + "' must be an array");
    }
    return value;
}

const JsonValue* optional_object_member(const JsonValue& object, const std::string& key) {
    const JsonValue* value = find_member(object, key);
    if (value == nullptr) {
        return nullptr;
    }
    if (value->type != JsonValue::Type::Object) {
        throw std::invalid_argument("field '" + key + "' must be an object");
    }
    return value;
}

void require_unique_id(const std::string& id, std::set<std::string>& ids, const std::string& collection_name) {
    if (!ids.insert(id).second) {
        throw std::invalid_argument("duplicate " + collection_name + " id: " + id);
    }
}

std::string endpoint_component(const std::string& endpoint) {
    const std::size_t dot = endpoint.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= endpoint.size() ||
        endpoint.find('.', dot + 1) != std::string::npos) {
        throw std::invalid_argument("connection endpoint must use component.port: " + endpoint);
    }
    return endpoint.substr(0, dot);
}

std::string endpoint_port(const std::string& endpoint) {
    const std::size_t dot = endpoint.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= endpoint.size() ||
        endpoint.find('.', dot + 1) != std::string::npos) {
        throw std::invalid_argument("connection endpoint must use component.port: " + endpoint);
    }
    return endpoint.substr(dot + 1);
}

MediumDefinition parse_medium(const JsonValue& value) {
    if (value.type != JsonValue::Type::Object) {
        throw std::invalid_argument("media entries must be objects");
    }
    MediumDefinition medium;
    medium.id = require_string(value, "id");
    medium.backend = require_string(value, "backend");
    medium.substance = require_string(value, "substance");
    medium.package_version =
        optional_string(value, "package_version");
    return medium;
}

MaterialDefinition parse_material(const JsonValue& value) {
    if (value.type != JsonValue::Type::Object) {
        throw std::invalid_argument(
            "materials entries must be objects");
    }
    MaterialDefinition material;
    material.id = require_string(value, "id");
    material.backend = require_string(value, "backend");
    material.mechanism = require_string(value, "mechanism");
    material.phase = require_string(value, "phase");
    material.package_version =
        optional_string(value, "package_version");
    std::set<std::string> unique_species;
    for (const auto& entry :
         require_array_member(value, "species").array) {
        const auto name = require_string_value(
            entry, "material '" + material.id + "'.species");
        if (name.empty() ||
            !unique_species.insert(name).second) {
            throw std::invalid_argument(
                "material '" + material.id +
                "' species must be non-empty and unique");
        }
        material.species.push_back(name);
    }
    if (material.species.empty()) {
        throw std::invalid_argument(
            "material '" + material.id +
            "' must declare at least one species");
    }
    return material;
}

ComponentDefinition parse_component(
    const JsonValue& value,
    const std::set<std::string>& medium_ids,
    const std::set<std::string>& material_ids) {
    if (value.type != JsonValue::Type::Object) {
        throw std::invalid_argument("components entries must be objects");
    }
    ComponentDefinition component;
    component.id = require_string(value, "id");
    component.label = optional_string(value, "label");
    component.kind = require_string(value, "kind");
    component.version = optional_string(value, "version");

    if (const JsonValue* media =
            optional_object_member(value, "media")) {
        for (const auto& [port_name, medium_value] :
             media->object) {
            const std::string medium_id =
                require_string_value(
                    medium_value,
                    "component '" + component.id +
                        "'.media." + port_name);
            if (medium_ids.find(medium_id) == medium_ids.end()) {
                throw std::invalid_argument(
                    "unknown medium referenced by component '" +
                    component.id + "' port '" + port_name +
                    "': " + medium_id);
            }
            component.medium_bindings.emplace(
                port_name, medium_id);
        }
    }

    if (const JsonValue* materials =
            optional_object_member(value, "materials")) {
        for (const auto& [port_name, material_value] :
             materials->object) {
            const std::string material_id =
                require_string_value(
                    material_value,
                    "component '" + component.id +
                        "'.materials." + port_name);
            if (material_ids.find(material_id) ==
                material_ids.end()) {
                throw std::invalid_argument(
                    "unknown material referenced by component '" +
                    component.id + "' port '" + port_name +
                    "': " + material_id);
            }
            component.material_bindings.emplace(
                port_name, material_id);
        }
    }

    if (const JsonValue* artifacts =
            optional_object_member(value, "artifacts")) {
        for (const auto& [role, artifact_value] :
             artifacts->object) {
            if (role.empty()) {
                throw std::invalid_argument(
                    "component artifact role must not be empty");
            }
            const std::string artifact_id =
                require_string_value(
                    artifact_value,
                    "component '" + component.id +
                        "'.artifacts." + role);
            if (artifact_id.empty()) {
                throw std::invalid_argument(
                    "component '" + component.id +
                    "' artifact binding '" + role +
                    "' must not be empty");
            }
            component.artifact_bindings.emplace(
                role, artifact_id);
        }
    }

    if (const JsonValue* parameters = optional_object_member(value, "parameters")) {
        component.parameters = parse_scalar_map(*parameters, "component '" + component.id + "'.parameters");
    }
    return component;
}

ConnectionDefinition parse_connection(const JsonValue& value) {
    if (value.type != JsonValue::Type::Object) {
        throw std::invalid_argument("connections entries must be objects");
    }
    ConnectionDefinition connection;
    connection.id = require_string(value, "id");
    connection.from = require_string(value, "from");
    connection.to = require_string(value, "to");
    connection.kind = require_string(value, "kind");
    connection.contract_version =
        optional_string(value, "contract_version");
    if (const JsonValue* parameters = optional_object_member(value, "parameters")) {
        connection.parameters = parse_scalar_map(*parameters, "connection '" + connection.id + "'.parameters", true);
    }
    return connection;
}

CaseDefinition parse_case(const JsonValue& value) {
    if (value.type != JsonValue::Type::Object) {
        throw std::invalid_argument("cases entries must be objects");
    }
    CaseDefinition c;
    c.id = require_string(value, "id");
    c.label = optional_string(value, "label");
    c.mode = require_string(value, "mode");
    if (const JsonValue* fixed_values = optional_object_member(value, "fixed_values")) {
        c.fixed_values = parse_scalar_map(*fixed_values, "case '" + c.id + "'.fixed_values");
    }
    if (const JsonValue* initial_guesses = optional_object_member(value, "initial_guesses")) {
        c.initial_guesses = parse_scalar_map(*initial_guesses, "case '" + c.id + "'.initial_guesses");
    }
    if (const JsonValue* solver_options = optional_object_member(value, "solver_options")) {
        c.solver_options = parse_scalar_map(*solver_options, "case '" + c.id + "'.solver_options", true);
    }
    return c;
}

std::vector<std::string> parse_nonempty_string_array(
    const JsonValue& object,
    const std::string& key,
    const std::string& field_name,
    bool required) {
    const JsonValue* array = find_member(object, key);
    if (array == nullptr) {
        if (required) {
            throw std::invalid_argument(
                "missing required field: " + key);
        }
        return {};
    }
    if (array->type != JsonValue::Type::Array) {
        throw std::invalid_argument(
            "field '" + field_name + "' must be an array");
    }
    std::vector<std::string> result;
    std::set<std::string> unique;
    for (const auto& entry : array->array) {
        const auto text = require_string_value(entry, field_name);
        if (text.empty()) {
            throw std::invalid_argument(
                "field '" + field_name +
                "' must contain non-empty strings");
        }
        if (!unique.insert(text).second) {
            throw std::invalid_argument(
                "field '" + field_name +
                "' contains duplicate value: " + text);
        }
        result.push_back(text);
    }
    if (required && result.empty()) {
        throw std::invalid_argument(
            "field '" + field_name + "' must not be empty");
    }
    return result;
}

const ScalarValue& calibration_target_value(
    const ModelDocument& document,
    const std::string& target) {
    constexpr std::string_view component_prefix{"components."};
    constexpr std::string_view connection_prefix{"connections."};
    constexpr std::string_view parameter_marker{".parameters."};
    const auto resolve = [&](std::string_view prefix,
                             const auto& owners,
                             const char* owner_name)
        -> const ScalarValue& {
        const std::string_view path{target};
        const auto marker = path.find(
            parameter_marker, prefix.size());
        if (!path.starts_with(prefix) ||
            marker == std::string_view::npos ||
            marker == prefix.size() ||
            marker + parameter_marker.size() >= path.size()) {
            throw std::invalid_argument(
                "calibration target must use components.<id>.parameters.<name> "
                "or connections.<id>.parameters.<name>: " + target);
        }
        const std::string owner_id{
            path.substr(prefix.size(), marker - prefix.size())};
        const std::string parameter_name{
            path.substr(marker + parameter_marker.size())};
        for (const auto& owner : owners) {
            if (owner.id != owner_id) {
                continue;
            }
            const auto parameter =
                owner.parameters.find(parameter_name);
            if (parameter == owner.parameters.end()) {
                throw std::invalid_argument(
                    "calibration target references unknown " +
                    std::string(owner_name) + " parameter: " +
                    target);
            }
            return parameter->second;
        }
        throw std::invalid_argument(
            "calibration target references unknown " +
            std::string(owner_name) + ": " + target);
    };

    if (std::string_view{target}.starts_with(component_prefix)) {
        return resolve(
            component_prefix, document.components, "component");
    }
    if (std::string_view{target}.starts_with(connection_prefix)) {
        return resolve(
            connection_prefix, document.connections, "connection");
    }
    throw std::invalid_argument(
        "calibration target must use components.<id>.parameters.<name> "
        "or connections.<id>.parameters.<name>: " + target);
}

CalibrationParameterDefinition parse_calibration_parameter(
    const JsonValue& value) {
    if (value.type != JsonValue::Type::Object) {
        throw std::invalid_argument(
            "calibration parameters entries must be objects");
    }
    CalibrationParameterDefinition parameter;
    parameter.id = require_string(value, "id");
    parameter.label = optional_string(value, "label");
    parameter.scope = require_string(value, "scope");
    if (parameter.scope != "component" &&
        parameter.scope != "system") {
        throw std::invalid_argument(
            "calibration parameter '" + parameter.id +
            "' scope must be 'component' or 'system'");
    }
    parameter.targets = parse_nonempty_string_array(
        value, "targets",
        "calibration parameter '" + parameter.id + "'.targets",
        true);
    parameter.case_ids = parse_nonempty_string_array(
        value, "cases",
        "calibration parameter '" + parameter.id + "'.cases",
        false);
    if (find_member(value, "cases") != nullptr &&
        parameter.case_ids.empty()) {
        throw std::invalid_argument(
            "calibration parameter '" + parameter.id +
            "'.cases must not be empty when supplied");
    }
    if (parameter.scope == "component" &&
        parameter.targets.size() != 1) {
        throw std::invalid_argument(
            "component-scoped calibration parameter '" +
            parameter.id + "' must have exactly one target");
    }
    if (parameter.scope == "component" &&
        !std::string_view{parameter.targets.front()}.starts_with(
            "components.")) {
        throw std::invalid_argument(
            "component-scoped calibration parameter '" +
            parameter.id + "' must target a component parameter");
    }
    if (const auto* bounds =
            optional_object_member(value, "bounds")) {
        if (const auto* lower = find_member(*bounds, "lower")) {
            parameter.lower_bound = parse_scalar_value(
                *lower, "calibration parameter '" +
                            parameter.id + "'.bounds.lower");
        }
        if (const auto* upper = find_member(*bounds, "upper")) {
            parameter.upper_bound = parse_scalar_value(
                *upper, "calibration parameter '" +
                            parameter.id + "'.bounds.upper");
        }
        if (!parameter.lower_bound.has_value() &&
            !parameter.upper_bound.has_value()) {
            throw std::invalid_argument(
                "calibration parameter '" + parameter.id +
                "'.bounds must declare lower or upper");
        }
    }
    if (const auto* prior =
            optional_object_member(value, "prior")) {
        parameter.prior_mean = parse_scalar_value(
            require_member(*prior, "mean"),
            "calibration parameter '" + parameter.id +
                "'.prior.mean");
        parameter.prior_sigma = parse_scalar_value(
            require_member(*prior, "sigma"),
            "calibration parameter '" + parameter.id +
                "'.prior.sigma");
    }
    return parameter;
}

CalibrationObservationDefinition parse_calibration_observation(
    const JsonValue& value) {
    if (value.type != JsonValue::Type::Object) {
        throw std::invalid_argument(
            "calibration observations entries must be objects");
    }
    CalibrationObservationDefinition observation;
    observation.id = require_string(value, "id");
    observation.label = optional_string(value, "label");
    observation.case_id = require_string(value, "case");
    observation.target = require_string(value, "target");
    observation.measured = parse_scalar_value(
        require_member(value, "measured"),
        "calibration observation '" + observation.id +
            "'.measured");
    observation.sigma = parse_scalar_value(
        require_member(value, "sigma"),
        "calibration observation '" + observation.id +
            "'.sigma");
    if (observation.sigma.value_si <= 0.0) {
        throw std::invalid_argument(
            "calibration observation '" + observation.id +
            "' sigma must be positive");
    }
    if (observation.sigma.dimension !=
        observation.measured.dimension) {
        throw std::invalid_argument(
            "calibration observation '" + observation.id +
            "' measured value and sigma must have the same dimension");
    }
    return observation;
}

CalibrationDefinition parse_calibration(const JsonValue& value) {
    if (value.type != JsonValue::Type::Object) {
        throw std::invalid_argument(
            "calibrations entries must be objects");
    }
    CalibrationDefinition calibration;
    calibration.id = require_string(value, "id");
    calibration.label = optional_string(value, "label");
    std::set<std::string> parameter_ids;
    for (const auto& entry :
         require_array_member(value, "parameters").array) {
        auto parameter = parse_calibration_parameter(entry);
        require_unique_id(
            parameter.id, parameter_ids,
            "calibration parameter");
        calibration.parameters.push_back(std::move(parameter));
    }
    std::set<std::string> observation_ids;
    for (const auto& entry :
         require_array_member(value, "observations").array) {
        auto observation =
            parse_calibration_observation(entry);
        require_unique_id(
            observation.id, observation_ids,
            "calibration observation");
        calibration.observations.push_back(
            std::move(observation));
    }
    if (calibration.parameters.empty()) {
        throw std::invalid_argument(
            "calibration '" + calibration.id +
            "' must declare at least one parameter");
    }
    if (calibration.observations.empty()) {
        throw std::invalid_argument(
            "calibration '" + calibration.id +
            "' must declare at least one observation");
    }
    return calibration;
}

void validate_calibrations(ModelDocument& document) {
    std::set<std::string> case_ids;
    for (const auto& simulation_case : document.cases) {
        case_ids.insert(simulation_case.id);
    }
    for (auto& calibration : document.calibrations) {
        for (auto& parameter : calibration.parameters) {
            const ScalarValue* reference = nullptr;
            for (const auto& target : parameter.targets) {
                const auto& value =
                    calibration_target_value(document, target);
                if (reference != nullptr &&
                    reference->dimension != value.dimension) {
                    throw std::invalid_argument(
                        "calibration parameter '" + parameter.id +
                        "' targets must have the same dimension");
                }
                if (reference != nullptr &&
                    reference->value_si != value.value_si) {
                    throw std::invalid_argument(
                        "shared calibration parameter '" +
                        parameter.id +
                        "' targets must have the same initial value");
                }
                reference = &value;
            }
            for (const auto& case_id : parameter.case_ids) {
                if (!case_ids.contains(case_id)) {
                    throw std::invalid_argument(
                        "calibration parameter '" +
                        parameter.id +
                        "' references unknown case: " + case_id);
                }
            }
            const auto validate_quantity =
                [&](const std::optional<ScalarValue>& quantity,
                    const char* name) {
                    if (quantity.has_value() &&
                        quantity->dimension !=
                            reference->dimension) {
                        throw std::invalid_argument(
                            "calibration parameter '" +
                            parameter.id + "' " + name +
                            " dimension does not match its targets");
                    }
                };
            validate_quantity(parameter.lower_bound, "lower bound");
            validate_quantity(parameter.upper_bound, "upper bound");
            validate_quantity(parameter.prior_mean, "prior mean");
            validate_quantity(parameter.prior_sigma, "prior sigma");
            if (parameter.lower_bound.has_value() &&
                parameter.upper_bound.has_value() &&
                parameter.lower_bound->value_si >=
                    parameter.upper_bound->value_si) {
                throw std::invalid_argument(
                    "calibration parameter '" + parameter.id +
                    "' lower bound must be less than upper bound");
            }
            if (parameter.lower_bound.has_value() &&
                reference->value_si <
                    parameter.lower_bound->value_si) {
                throw std::invalid_argument(
                    "calibration parameter '" + parameter.id +
                    "' initial value is below its lower bound");
            }
            if (parameter.upper_bound.has_value() &&
                reference->value_si >
                    parameter.upper_bound->value_si) {
                throw std::invalid_argument(
                    "calibration parameter '" + parameter.id +
                    "' initial value is above its upper bound");
            }
            if (parameter.prior_sigma.has_value() &&
                parameter.prior_sigma->value_si <= 0.0) {
                throw std::invalid_argument(
                    "calibration parameter '" + parameter.id +
                    "' prior sigma must be positive");
            }
        }
        for (const auto& observation :
             calibration.observations) {
            if (!case_ids.contains(observation.case_id)) {
                throw std::invalid_argument(
                    "calibration observation '" +
                    observation.id +
                    "' references unknown case: " +
                    observation.case_id);
            }
        }
    }
}

void require_endpoint_component(
    const ModelDocument& document,
    const std::string& endpoint) {
    const std::string component_id = endpoint_component(endpoint);
    (void)endpoint_port(endpoint);
    for (const ComponentDefinition& component : document.components) {
        if (component.id == component_id) {
            return;
        }
    }
    throw std::invalid_argument("unknown component in connection endpoint: " + endpoint);
}

void validate_connections(const ModelDocument& document) {
    std::set<std::string> connection_ids;
    for (const ConnectionDefinition& connection : document.connections) {
        require_unique_id(connection.id, connection_ids, "connection");
        require_endpoint_component(document, connection.from);
        require_endpoint_component(document, connection.to);
        if (connection.from == connection.to) {
            throw std::invalid_argument(
                "connection '" + connection.id +
                "' cannot connect a port to itself");
        }
    }
}

ModelDocument parse_model_document_root(const JsonValue& root) {
    const JsonValue& object = require_object_root(root);
    ModelDocument document;
    document.schema_version = require_string(object, "schema_version");
    if (document.schema_version != "thermox.model/v2") {
        throw std::invalid_argument("unsupported schema_version: " + document.schema_version);
    }
    const std::string model_key{"model"};
    const JsonValue& model = require_object_member(object, model_key);
    document.model_id = require_string(model, "id");
    document.name = optional_string(model, "name");
    document.revision = optional_string(model, "revision");

    std::set<std::string> medium_ids;
    std::set<std::string> all_binding_ids;
    for (const JsonValue& medium_value : require_array_member(model, "media").array) {
        MediumDefinition medium = parse_medium(medium_value);
        require_unique_id(medium.id, medium_ids, "medium");
        require_unique_id(
            medium.id, all_binding_ids, "medium or material");
        document.media.push_back(std::move(medium));
    }
    std::set<std::string> material_ids;
    if (const auto* materials =
            optional_array_member(model, "materials")) {
        for (const JsonValue& material_value :
             materials->array) {
            MaterialDefinition material =
                parse_material(material_value);
            require_unique_id(
                material.id, material_ids, "material");
            require_unique_id(
                material.id, all_binding_ids,
                "medium or material");
            document.materials.push_back(std::move(material));
        }
    }

    std::set<std::string> component_ids;
    for (const JsonValue& component_value : require_array_member(model, "components").array) {
        ComponentDefinition component = parse_component(
            component_value, medium_ids, material_ids);
        require_unique_id(component.id, component_ids, "component");
        document.components.push_back(std::move(component));
    }

    for (const JsonValue& connection_value : require_array_member(model, "connections").array) {
        document.connections.push_back(parse_connection(connection_value));
    }

    std::set<std::string> case_ids;
    for (const JsonValue& case_value : require_array_member(object, "cases").array) {
        CaseDefinition c = parse_case(case_value);
        require_unique_id(c.id, case_ids, "case");
        document.cases.push_back(std::move(c));
    }

    std::set<std::string> calibration_ids;
    if (const auto* calibrations =
            optional_array_member(object, "calibrations")) {
        for (const auto& calibration_value :
             calibrations->array) {
            auto calibration =
                parse_calibration(calibration_value);
            require_unique_id(
                calibration.id, calibration_ids, "calibration");
            document.calibrations.push_back(
                std::move(calibration));
        }
    }

    validate_connections(document);
    validate_calibrations(document);
    return document;
}

}  // namespace

std::string read_text_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("failed to open file: " + path);
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

ModelDocument parse_model_document_text(const std::string& text) {
    const JsonValue root_value = JsonParser{text}.parse_document();
    return parse_model_document_root(root_value);
}

ModelDocument load_model_document(const std::string& path) {
    return parse_model_document_text(read_text_file(path));
}

}  // namespace thermox::platform
