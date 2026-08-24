#include "thermox/platform/expression_component.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace thermox::platform {

namespace {

constexpr std::size_t maximum_expression_length = 4096;
constexpr std::size_t maximum_expression_nodes = 512;
constexpr std::size_t maximum_expression_depth = 64;

enum class NodeKind {
    literal,
    symbol,
    negate,
    add,
    subtract,
    multiply,
    divide,
    power,
    absolute,
    square_root,
    exponential,
    logarithm,
};

struct ExpressionNode {
    NodeKind kind{NodeKind::literal};
    double literal{0.0};
    std::string symbol;
    std::shared_ptr<const ExpressionNode> left;
    std::shared_ptr<const ExpressionNode> right;
};

struct ParsedExpression {
    std::shared_ptr<const ExpressionNode> root;
    std::set<std::string> symbols;
};

using DimensionSignature = std::map<std::string, double>;

struct DimensionInference {
    DimensionSignature signature;
    std::optional<double> constant;
};

DimensionSignature physical_dimension(const std::string& dimension) {
    using Signature = DimensionSignature;
    static const std::map<std::string, Signature> known{
        {"dimensionless", {}},
        {"pressure", {{"mass", 1.0}, {"length", -1.0}, {"time", -2.0}}},
        {"temperature", {{"temperature", 1.0}}},
        {"temperature_difference", {{"temperature", 1.0}}},
        {"angle", {}},
        {"length", {{"length", 1.0}}},
        {"mass_flow", {{"mass", 1.0}, {"time", -1.0}}},
        {"specific_heat", {{"length", 2.0}, {"time", -2.0},
                           {"temperature", -1.0}}},
        {"specific_heat_capacity", {{"length", 2.0}, {"time", -2.0},
                                    {"temperature", -1.0}}},
        {"specific_entropy", {{"length", 2.0}, {"time", -2.0},
                              {"temperature", -1.0}}},
        {"specific_enthalpy", {{"length", 2.0}, {"time", -2.0}}},
        {"specific_internal_energy", {{"length", 2.0}, {"time", -2.0}}},
        {"power", {{"mass", 1.0}, {"length", 2.0}, {"time", -3.0}}},
        {"electrical_power", {{"mass", 1.0}, {"length", 2.0},
                              {"time", -3.0}}},
        {"thermal_capacity", {{"mass", 1.0}, {"length", 2.0},
                              {"time", -2.0}, {"temperature", -1.0}}},
        {"thermal_conductance", {{"mass", 1.0}, {"length", 2.0},
                                 {"time", -3.0}, {"temperature", -1.0}}},
        {"volume", {{"length", 3.0}}},
        {"mass", {{"mass", 1.0}}},
        {"molar_mass", {{"mass", 1.0}, {"amount", -1.0}}},
        {"energy", {{"mass", 1.0}, {"length", 2.0}, {"time", -2.0}}},
        {"time", {{"time", 1.0}}},
        {"moment_of_inertia", {{"mass", 1.0}, {"length", 2.0}}},
        {"angular_speed", {{"time", -1.0}}},
        {"frequency", {{"time", -1.0}}},
        {"density", {{"mass", 1.0}, {"length", -3.0}}},
        {"area", {{"length", 2.0}}},
        {"mass_flux", {{"mass", 1.0}, {"length", -2.0},
                       {"time", -1.0}}},
        {"pressure_gradient", {{"mass", 1.0}, {"length", -2.0},
                               {"time", -2.0}}},
        {"dynamic_viscosity", {{"mass", 1.0}, {"length", -1.0},
                               {"time", -1.0}}},
        {"thermal_conductivity", {{"mass", 1.0}, {"length", 1.0},
                                  {"time", -3.0}, {"temperature", -1.0}}},
        {"surface_tension", {{"mass", 1.0}, {"time", -2.0}}},
        {"speed", {{"length", 1.0}, {"time", -1.0}}},
        {"acceleration", {{"length", 1.0}, {"time", -2.0}}},
    };
    const auto found = known.find(dimension);
    if (found != known.end()) return found->second;
    return {{"custom:" + dimension, 1.0}};
}

void add_dimension(
    DimensionSignature& target,
    const DimensionSignature& source,
    double scale = 1.0) {
    constexpr double tolerance = 1.0e-12;
    for (const auto& [base, exponent] : source) {
        auto& value = target[base];
        value += scale * exponent;
        if (std::abs(value) <= tolerance) target.erase(base);
    }
}

bool same_dimension(
    const DimensionSignature& left,
    const DimensionSignature& right) {
    constexpr double tolerance = 1.0e-12;
    if (left.size() != right.size()) return false;
    for (const auto& [base, exponent] : left) {
        const auto found = right.find(base);
        if (found == right.end() ||
            std::abs(exponent - found->second) > tolerance) {
            return false;
        }
    }
    return true;
}

bool constant_zero(const DimensionInference& value) {
    return value.constant.has_value() && *value.constant == 0.0;
}

DimensionInference infer_dimension(
    const ExpressionNode& node,
    const std::map<std::string, DimensionSignature>& symbols) {
    if (node.kind == NodeKind::literal) {
        return {{}, node.literal};
    }
    if (node.kind == NodeKind::symbol) {
        const auto found = symbols.find(node.symbol);
        if (found == symbols.end()) {
            throw std::invalid_argument(
                "unbound dimension for symbol '" + node.symbol + "'");
        }
        return {found->second, std::nullopt};
    }

    auto left = infer_dimension(*node.left, symbols);
    if (node.kind == NodeKind::negate) {
        if (left.constant) left.constant = -*left.constant;
        return left;
    }
    if (node.kind == NodeKind::absolute) {
        if (left.constant) left.constant = std::abs(*left.constant);
        return left;
    }
    if (node.kind == NodeKind::square_root) {
        for (auto& [_, exponent] : left.signature) exponent *= 0.5;
        if (left.constant && *left.constant >= 0.0) {
            left.constant = std::sqrt(*left.constant);
        } else {
            left.constant.reset();
        }
        return left;
    }
    if (node.kind == NodeKind::exponential ||
        node.kind == NodeKind::logarithm) {
        if (!left.signature.empty()) {
            throw std::invalid_argument(
                "exp and log require a dimensionless argument");
        }
        if (left.constant) {
            const double value = node.kind == NodeKind::exponential
                ? std::exp(*left.constant)
                : (*left.constant > 0.0
                       ? std::log(*left.constant)
                       : std::numeric_limits<double>::quiet_NaN());
            left.constant = std::isfinite(value)
                ? std::optional<double>{value}
                : std::nullopt;
        }
        return left;
    }

    auto right = infer_dimension(*node.right, symbols);
    if (node.kind == NodeKind::add ||
        node.kind == NodeKind::subtract) {
        if (!same_dimension(left.signature, right.signature)) {
            if (constant_zero(left)) return right;
            if (constant_zero(right)) return left;
            throw std::invalid_argument(
                "addition and subtraction require compatible dimensions");
        }
        if (left.constant && right.constant) {
            left.constant = node.kind == NodeKind::add
                ? *left.constant + *right.constant
                : *left.constant - *right.constant;
        } else {
            left.constant.reset();
        }
        return left;
    }
    if (node.kind == NodeKind::multiply ||
        node.kind == NodeKind::divide) {
        add_dimension(
            left.signature, right.signature,
            node.kind == NodeKind::multiply ? 1.0 : -1.0);
        if (left.constant && right.constant &&
            (node.kind == NodeKind::multiply || *right.constant != 0.0)) {
            left.constant = node.kind == NodeKind::multiply
                ? *left.constant * *right.constant
                : *left.constant / *right.constant;
        } else {
            left.constant.reset();
        }
        return left;
    }
    if (node.kind == NodeKind::power) {
        if (!right.signature.empty()) {
            throw std::invalid_argument(
                "pow exponent must be dimensionless");
        }
        if (!left.signature.empty()) {
            if (!right.constant || !std::isfinite(*right.constant)) {
                throw std::invalid_argument(
                    "pow of a dimensioned value requires a constant exponent");
            }
            for (auto& [_, exponent] : left.signature) {
                exponent *= *right.constant;
            }
        }
        if (left.constant && right.constant) {
            const double value = std::pow(*left.constant, *right.constant);
            left.constant = std::isfinite(value)
                ? std::optional<double>{value}
                : std::nullopt;
        } else {
            left.constant.reset();
        }
        return left;
    }
    throw std::logic_error("unsupported expression node for dimension inference");
}

void validate_dimensions(
    const ParsedExpression& expression,
    const std::map<std::string, DimensionSignature>& symbols,
    const std::string& equation_name) {
    try {
        (void)infer_dimension(*expression.root, symbols);
    } catch (const std::invalid_argument& error) {
        throw std::invalid_argument(
            "expression equation '" + equation_name +
            "' is dimensionally invalid: " + error.what());
    }
}

class ExpressionParser {
public:
    explicit ExpressionParser(std::string_view text)
        : text_(text) {
        if (text.empty()) {
            fail("expression must not be empty");
        }
        if (text.size() > maximum_expression_length) {
            fail("expression exceeds the 4096-character limit");
        }
    }

    ParsedExpression parse() {
        auto root = parse_sum(0);
        skip_whitespace();
        if (!at_end()) {
            fail("unexpected trailing token");
        }
        return {std::move(root), std::move(symbols_)};
    }

private:
    [[nodiscard]] bool at_end() const {
        return position_ >= text_.size();
    }

    void skip_whitespace() {
        while (!at_end() &&
               std::isspace(
                   static_cast<unsigned char>(
                       text_[position_]))) {
            ++position_;
        }
    }

    bool consume(char expected) {
        skip_whitespace();
        if (!at_end() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    [[noreturn]] void fail(const std::string& message) const {
        throw std::invalid_argument(
            "invalid safe expression at offset " +
            std::to_string(position_) + ": " + message);
    }

    std::shared_ptr<const ExpressionNode> node(
        NodeKind kind,
        std::shared_ptr<const ExpressionNode> left = {},
        std::shared_ptr<const ExpressionNode> right = {},
        double literal_value = 0.0,
        std::string symbol = {}) {
        if (++node_count_ > maximum_expression_nodes) {
            fail("expression exceeds the 512-node limit");
        }
        auto out = std::make_shared<ExpressionNode>();
        out->kind = kind;
        out->left = std::move(left);
        out->right = std::move(right);
        out->literal = literal_value;
        out->symbol = std::move(symbol);
        return out;
    }

    std::shared_ptr<const ExpressionNode> parse_sum(
        std::size_t depth) {
        auto left = parse_product(depth);
        while (true) {
            if (consume('+')) {
                left = node(
                    NodeKind::add, std::move(left),
                    parse_product(depth));
            } else if (consume('-')) {
                left = node(
                    NodeKind::subtract, std::move(left),
                    parse_product(depth));
            } else {
                return left;
            }
        }
    }

    std::shared_ptr<const ExpressionNode> parse_product(
        std::size_t depth) {
        auto left = parse_unary(depth);
        while (true) {
            if (consume('*')) {
                left = node(
                    NodeKind::multiply, std::move(left),
                    parse_unary(depth));
            } else if (consume('/')) {
                left = node(
                    NodeKind::divide, std::move(left),
                    parse_unary(depth));
            } else {
                return left;
            }
        }
    }

    std::shared_ptr<const ExpressionNode> parse_unary(
        std::size_t depth) {
        if (depth >= maximum_expression_depth) {
            fail("expression nesting exceeds the 64-level limit");
        }
        if (consume('+')) return parse_unary(depth + 1);
        if (consume('-')) {
            return node(
                NodeKind::negate, parse_unary(depth + 1));
        }
        return parse_primary(depth);
    }

    std::shared_ptr<const ExpressionNode> parse_primary(
        std::size_t depth) {
        if (depth >= maximum_expression_depth) {
            fail("expression nesting exceeds the 64-level limit");
        }
        skip_whitespace();
        if (at_end()) fail("expected a value");
        if (consume('(')) {
            auto expression = parse_sum(depth + 1);
            if (!consume(')')) fail("expected ')'");
            return expression;
        }
        const unsigned char current =
            static_cast<unsigned char>(text_[position_]);
        if (std::isdigit(current) || text_[position_] == '.') {
            return parse_number();
        }
        if (std::isalpha(current) || text_[position_] == '_') {
            const auto identifier = parse_identifier();
            if (!consume('(')) {
                if (identifier == "pi") {
                    return literal(
                        3.141592653589793238462643383279502884);
                }
                if (identifier == "e") {
                    return literal(
                        2.718281828459045235360287471352662498);
                }
                symbols_.insert(identifier);
                return node(
                    NodeKind::symbol, {}, {}, 0.0,
                    identifier);
            }
            auto first = parse_sum(depth + 1);
            std::shared_ptr<const ExpressionNode> second;
            if (consume(',')) {
                second = parse_sum(depth + 1);
            }
            if (!consume(')')) fail("expected ')' after function");
            if (identifier == "pow") {
                if (!second) fail("pow requires two arguments");
                return node(
                    NodeKind::power, std::move(first),
                    std::move(second));
            }
            if (second) {
                fail(identifier + " accepts one argument");
            }
            if (identifier == "abs") {
                return node(
                    NodeKind::absolute, std::move(first));
            }
            if (identifier == "sqrt") {
                return node(
                    NodeKind::square_root, std::move(first));
            }
            if (identifier == "exp") {
                return node(
                    NodeKind::exponential, std::move(first));
            }
            if (identifier == "log") {
                return node(
                    NodeKind::logarithm, std::move(first));
            }
            fail("unknown function '" + identifier + "'");
        }
        fail("expected a number, symbol, function, or '('");
    }

    std::shared_ptr<const ExpressionNode> literal(double value) {
        return node(
            NodeKind::literal, {}, {}, value);
    }

    std::shared_ptr<const ExpressionNode> parse_number() {
        const char* begin = text_.data() + position_;
        const char* end = text_.data() + text_.size();
        double value = 0.0;
        const auto parsed = std::from_chars(
            begin, end, value, std::chars_format::general);
        if (parsed.ptr == begin ||
            parsed.ec != std::errc{} ||
            !std::isfinite(value)) {
            fail("number must be finite");
        }
        position_ +=
            static_cast<std::size_t>(parsed.ptr - begin);
        return literal(value);
    }

    std::string parse_identifier() {
        skip_whitespace();
        const std::size_t begin = position_;
        while (!at_end()) {
            const unsigned char value =
                static_cast<unsigned char>(text_[position_]);
            if (!std::isalnum(value) && text_[position_] != '_' &&
                text_[position_] != '.') {
                break;
            }
            ++position_;
        }
        const std::string identifier{text_.substr(
            begin, position_ - begin)};
        if (identifier.empty() ||
            identifier.front() == '.' ||
            identifier.back() == '.' ||
            identifier.find("..") != std::string::npos) {
            fail("invalid symbol");
        }
        return identifier;
    }

    std::string_view text_;
    std::size_t position_{0};
    std::size_t node_count_{0};
    std::set<std::string> symbols_;
};

struct Evaluation {
    double value{0.0};
    std::map<std::size_t, double> derivatives;
    std::string error;
};

Evaluation failure(std::string message) {
    Evaluation out;
    out.error = std::move(message);
    return out;
}

void add_scaled(
    std::map<std::size_t, double>& target,
    const std::map<std::size_t, double>& source,
    double scale) {
    for (const auto& [variable, derivative] : source) {
        target[variable] += scale * derivative;
    }
}

Evaluation evaluate(
    const ExpressionNode& node,
    const std::vector<double>& values,
    const std::map<std::string, std::size_t>& variables,
    const std::map<std::string, double>& parameters) {
    if (node.kind == NodeKind::literal) {
        return {node.literal, {}, {}};
    }
    if (node.kind == NodeKind::symbol) {
        const auto variable = variables.find(node.symbol);
        if (variable != variables.end()) {
            return {
                values.at(variable->second),
                {{variable->second, 1.0}},
                {}};
        }
        const auto parameter = parameters.find(node.symbol);
        if (parameter != parameters.end()) {
            return {parameter->second, {}, {}};
        }
        return failure("unbound symbol '" + node.symbol + "'");
    }

    auto left = evaluate(
        *node.left, values, variables, parameters);
    if (!left.error.empty()) return left;
    if (node.kind == NodeKind::negate) {
        left.value = -left.value;
        for (auto& [unused, derivative] : left.derivatives) {
            (void)unused;
            derivative = -derivative;
        }
        return left;
    }
    if (node.kind == NodeKind::absolute) {
        const double sign =
            left.value > 0.0 ? 1.0 :
            left.value < 0.0 ? -1.0 : 0.0;
        left.value = std::abs(left.value);
        for (auto& [unused, derivative] : left.derivatives) {
            (void)unused;
            derivative *= sign;
        }
        return left;
    }
    if (node.kind == NodeKind::square_root) {
        if (left.value <= 0.0) {
            return failure("sqrt argument must be positive");
        }
        const double result = std::sqrt(left.value);
        for (auto& [unused, derivative] : left.derivatives) {
            (void)unused;
            derivative /= 2.0 * result;
        }
        left.value = result;
        return left;
    }
    if (node.kind == NodeKind::exponential) {
        const double result = std::exp(left.value);
        if (!std::isfinite(result)) {
            return failure("exp result is not finite");
        }
        for (auto& [unused, derivative] : left.derivatives) {
            (void)unused;
            derivative *= result;
        }
        left.value = result;
        return left;
    }
    if (node.kind == NodeKind::logarithm) {
        if (left.value <= 0.0) {
            return failure("log argument must be positive");
        }
        const double divisor = left.value;
        left.value = std::log(left.value);
        for (auto& [unused, derivative] : left.derivatives) {
            (void)unused;
            derivative /= divisor;
        }
        return left;
    }

    auto right = evaluate(
        *node.right, values, variables, parameters);
    if (!right.error.empty()) return right;
    Evaluation out;
    if (node.kind == NodeKind::add ||
        node.kind == NodeKind::subtract) {
        out.value = node.kind == NodeKind::add
            ? left.value + right.value
            : left.value - right.value;
        out.derivatives = std::move(left.derivatives);
        add_scaled(
            out.derivatives, right.derivatives,
            node.kind == NodeKind::add ? 1.0 : -1.0);
    } else if (node.kind == NodeKind::multiply) {
        out.value = left.value * right.value;
        add_scaled(
            out.derivatives, left.derivatives, right.value);
        add_scaled(
            out.derivatives, right.derivatives, left.value);
    } else if (node.kind == NodeKind::divide) {
        if (right.value == 0.0) {
            return failure("division by zero");
        }
        out.value = left.value / right.value;
        add_scaled(
            out.derivatives, left.derivatives,
            1.0 / right.value);
        add_scaled(
            out.derivatives, right.derivatives,
            -left.value / (right.value * right.value));
    } else if (node.kind == NodeKind::power) {
        out.value = std::pow(left.value, right.value);
        if (!std::isfinite(out.value)) {
            return failure("pow result is not finite");
        }
        if (left.value > 0.0) {
            add_scaled(
                out.derivatives, left.derivatives,
                out.value * right.value / left.value);
            add_scaled(
                out.derivatives, right.derivatives,
                out.value * std::log(left.value));
        } else if (right.derivatives.empty() &&
                   std::floor(right.value) == right.value) {
            add_scaled(
                out.derivatives, left.derivatives,
                right.value *
                    std::pow(left.value, right.value - 1.0));
        } else {
            return failure(
                "pow requires a positive base when its exponent "
                "is variable or non-integral");
        }
    }
    if (!std::isfinite(out.value)) {
        return failure("expression result is not finite");
    }
    for (const auto& [unused, derivative] : out.derivatives) {
        (void)unused;
        if (!std::isfinite(derivative)) {
            return failure(
                "expression derivative is not finite");
        }
    }
    return out;
}

bool valid_name(std::string_view value) {
    if (value.empty()) return false;
    for (const unsigned char character : value) {
        if (!std::isalnum(character) && character != '_' &&
            character != '-') {
            return false;
        }
    }
    return true;
}

bool valid_symbol(std::string_view value) {
    bool segment_start = true;
    for (const unsigned char character : value) {
        if (character == '.') {
            if (segment_start) return false;
            segment_start = true;
            continue;
        }
        if (segment_start) {
            if (!std::isalpha(character) && character != '_') {
                return false;
            }
            segment_start = false;
        } else if (!std::isalnum(character) && character != '_') {
            return false;
        }
    }
    return !value.empty() && !segment_start;
}

std::string definition_fingerprint(
    const ExpressionComponentDefinition& definition) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto hash_text =
        [&](std::string_view text) {
            constexpr std::uint64_t prime = 1099511628211ULL;
            for (const unsigned char value : text) {
                hash ^= value;
                hash *= prime;
            }
            hash ^= 0xffU;
            hash *= prime;
        };
    hash_text(definition.schema_version);
    hash_text(definition.descriptor.kind);
    hash_text(definition.descriptor.version);
    for (const auto& equation : definition.equations) {
        hash_text(equation.name);
        hash_text(equation.expression);
        std::ostringstream scale;
        scale << std::setprecision(17)
              << equation.residual_scale;
        hash_text(scale.str());
    }
    for (const auto& variable : definition.descriptor.transient_variables) {
        hash_text(variable.port_name);
        hash_text(variable.variable_name);
        hash_text(variable.kind == DaeVariableKind::differential
                      ? "differential" : "algebraic");
        std::ostringstream scale;
        scale << std::setprecision(17) << variable.derivative_scale;
        hash_text(scale.str());
    }
    for (const auto& variable : definition.descriptor.internal_variables) {
        hash_text(variable.name);
        hash_text(variable.kind == DaeVariableKind::differential
                      ? "differential" : "algebraic");
        std::ostringstream values;
        values << std::setprecision(17) << variable.initial_value
               << ':' << variable.state_scale << ':'
               << variable.initial_derivative << ':'
               << variable.derivative_scale << ':'
               << variable.lower_bound << ':' << variable.upper_bound;
        hash_text(values.str());
        hash_text(variable.dimension);
    }
    for (const auto& equation : definition.transient_equations) {
        hash_text(equation.name);
        hash_text(equation.expression);
        std::ostringstream scale;
        scale << std::setprecision(17) << equation.residual_scale;
        hash_text(scale.str());
    }
    hash_text(definition.descriptor.default_mode);
    for (const auto& mode : definition.modes) {
        hash_text(mode.name);
        for (const auto& equation : mode.equations) {
            hash_text(equation.name);
            hash_text(equation.expression);
            std::ostringstream scale;
            scale << std::setprecision(17)
                  << equation.residual_scale;
            hash_text(scale.str());
        }
        for (const auto& equation : mode.transient_equations) {
            hash_text(equation.name);
            hash_text(equation.expression);
            std::ostringstream scale;
            scale << std::setprecision(17)
                  << equation.residual_scale;
            hash_text(scale.str());
        }
    }
    std::ostringstream out;
    out << "fnv1a64:" << std::hex << std::setw(16)
        << std::setfill('0') << hash;
    return out.str();
}

class ExpressionComponentModel final : public ComponentModel {
public:
    using ParsedEquationSet = std::vector<std::pair<
        AlgebraicExpressionEquation, ParsedExpression>>;
    using ParsedModeEquationSets =
        std::map<std::string, ParsedEquationSet>;

    ExpressionComponentModel(
        ComponentModelDescriptor descriptor,
        ParsedEquationSet equations,
        ParsedEquationSet transient_equations,
        ParsedModeEquationSets mode_equations,
        ParsedModeEquationSets mode_transient_equations,
        std::string fingerprint)
        : descriptor_(std::move(descriptor)),
          equations_(std::move(equations)),
          transient_equations_(std::move(transient_equations)),
          mode_equations_(std::move(mode_equations)),
          mode_transient_equations_(
              std::move(mode_transient_equations)),
          fingerprint_(std::move(fingerprint)) {}

    const ComponentModelDescriptor& descriptor() const override {
        return descriptor_;
    }

    std::string_view implementation_fingerprint()
        const override {
        return fingerprint_;
    }

    void add_equations(
        const ComponentCompileContext& context,
        EquationSystemBuilder& system) const override {
        std::map<std::string, std::size_t> variables;
        for (const auto& [name, index] :
             context.port_variables) {
            variables.emplace(name, index);
        }
        std::map<std::string, double> parameters;
        for (const auto& descriptor :
             descriptor_.parameters) {
            const auto supplied =
                context.component.parameters.find(
                    descriptor.name);
            if (supplied !=
                context.component.parameters.end()) {
                parameters.emplace(
                    "parameter." + descriptor.name,
                    supplied->second.value_si);
            } else if (descriptor.default_value.has_value()) {
                parameters.emplace(
                    "parameter." + descriptor.name,
                    *descriptor.default_value);
            }
        }

        const ParsedEquationSet* selected_equations = &equations_;
        if (!mode_equations_.empty()) {
            if (!context.active_mode) {
                throw std::logic_error(
                    "mode-aware expression component has no active mode");
            }
            selected_equations = &mode_equations_.at(
                context.active_mode());
        }
        for (const auto& [definition, expression] :
             *selected_equations) {
            std::vector<std::size_t> sparsity;
            for (const auto& symbol : expression.symbols) {
                const auto variable = variables.find(symbol);
                if (variable != variables.end()) {
                    sparsity.push_back(variable->second);
                }
            }
            std::sort(sparsity.begin(), sparsity.end());
            sparsity.erase(
                std::unique(
                    sparsity.begin(), sparsity.end()),
                sparsity.end());
            const auto root = expression.root;
            const auto bound_variables = variables;
            const auto bound_parameters = parameters;
            const std::string equation_name =
                "component." + context.component.id +
                ".expression." + definition.name;
            system.add_checked_sparse_equation(
                equation_name,
                [root, bound_variables, bound_parameters](
                    const std::vector<double>& values,
                    double& residual) {
                    const auto result = evaluate(
                        *root, values, bound_variables,
                        bound_parameters);
                    if (!result.error.empty()) {
                        return EvaluationStatus::recoverable(
                            result.error);
                    }
                    residual = result.value;
                    return EvaluationStatus::success();
                },
                std::move(sparsity),
                [root, bound_variables, bound_parameters](
                    const std::vector<double>& values,
                    std::vector<EquationPartial>& partials) {
                    const auto result = evaluate(
                        *root, values, bound_variables,
                        bound_parameters);
                    if (!result.error.empty()) {
                        throw std::runtime_error(
                            "safe expression evaluation failed: " +
                            result.error);
                    }
                    for (const auto& [variable, derivative] :
                         result.derivatives) {
                        partials.push_back(
                            {variable, derivative});
                    }
                    return result.value;
                },
                definition.residual_scale);
        }
    }

    void add_transient_equations(
        const ComponentCompileContext& context,
        DaeEquationSystemBuilder& system) const override {
        if (!mode_transient_equations_.empty() &&
            !context.active_mode) {
            throw std::logic_error(
                "mode-aware expression component has no active mode");
        }
        struct Binding {
            std::string symbol;
            std::size_t variable{0};
            bool derivative{false};
        };

        std::vector<Binding> bindings;
        std::map<std::string, std::size_t> expression_variables;
        const auto bind = [&](std::string symbol, std::size_t variable,
                              bool derivative) {
            const auto local = bindings.size();
            expression_variables.emplace(symbol, local);
            bindings.push_back({std::move(symbol), variable, derivative});
        };
        for (const auto& [name, index] : context.port_variables) {
            bind(name, index, false);
            bind("derivative." + name, index, true);
        }
        for (const auto& [name, index] : context.internal_variables) {
            bind("internal." + name, index, false);
            bind("derivative.internal." + name, index, true);
        }

        std::map<std::string, double> parameters;
        for (const auto& descriptor : descriptor_.parameters) {
            const auto supplied = context.component.parameters.find(
                descriptor.name);
            if (supplied != context.component.parameters.end()) {
                parameters.emplace("parameter." + descriptor.name,
                                   supplied->second.value_si);
            } else if (descriptor.default_value.has_value()) {
                parameters.emplace("parameter." + descriptor.name,
                                   *descriptor.default_value);
            }
        }

        const ParsedEquationSet* structural_equations =
            &transient_equations_;
        if (!mode_transient_equations_.empty()) {
            structural_equations =
                &mode_transient_equations_.at(
                    descriptor_.default_mode);
        }
        for (std::size_t equation_index = 0;
             equation_index < structural_equations->size();
             ++equation_index) {
            const auto& [definition, expression] =
                structural_equations->at(equation_index);
            std::vector<std::size_t> sparsity;
            for (const auto& symbol : expression.symbols) {
                const auto found = expression_variables.find(symbol);
                if (found != expression_variables.end()) {
                    sparsity.push_back(bindings.at(found->second).variable);
                }
            }
            std::sort(sparsity.begin(), sparsity.end());
            sparsity.erase(std::unique(sparsity.begin(), sparsity.end()),
                           sparsity.end());
            const auto root = expression.root;
            std::map<std::string,
                     std::shared_ptr<const ExpressionNode>> mode_roots;
            for (const auto& [mode, equations] :
                 mode_transient_equations_) {
                mode_roots.emplace(
                    mode, equations.at(equation_index).second.root);
            }
            const auto active_mode = context.active_mode;
            const auto bound_variables = expression_variables;
            const auto bound_bindings = bindings;
            const auto bound_parameters = parameters;
            const std::string equation_name =
                "component." + context.component.id +
                ".expression_transient." + definition.name;
            system.add_sparse_equation(
                equation_name, std::move(sparsity),
                [root, mode_roots, active_mode,
                 bound_variables, bound_bindings, bound_parameters](
                    double time, const std::vector<double>& state,
                    const std::vector<double>& derivative, double& residual,
                    std::vector<DaeEquationPartial>& partials) {
                    std::vector<double> local_values(bound_bindings.size());
                    for (std::size_t i = 0; i < bound_bindings.size(); ++i) {
                        const auto& binding = bound_bindings[i];
                        local_values[i] = binding.derivative
                            ? derivative.at(binding.variable)
                            : state.at(binding.variable);
                    }
                    auto dynamic_parameters = bound_parameters;
                    dynamic_parameters.emplace("time", time);
                    const auto selected_root = mode_roots.empty()
                        ? root
                        : mode_roots.at(active_mode());
                    const auto result = evaluate(
                        *selected_root, local_values, bound_variables,
                        dynamic_parameters);
                    if (!result.error.empty()) {
                        return EvaluationStatus::recoverable(result.error);
                    }
                    residual = result.value;
                    std::map<std::size_t, DaeEquationPartial> combined;
                    for (const auto& [local, value] : result.derivatives) {
                        const auto& binding = bound_bindings.at(local);
                        auto& partial = combined[binding.variable];
                        partial.variable = binding.variable;
                        if (binding.derivative) {
                            partial.state_rate_derivative += value;
                        } else {
                            partial.state_derivative += value;
                        }
                    }
                    for (const auto& [_, partial] : combined) {
                        partials.push_back(partial);
                    }
                    return EvaluationStatus::success();
                },
                definition.residual_scale);
        }
    }

private:
    ComponentModelDescriptor descriptor_;
    ParsedEquationSet equations_;
    ParsedEquationSet transient_equations_;
    ParsedModeEquationSets mode_equations_;
    ParsedModeEquationSets mode_transient_equations_;
    std::string fingerprint_;
};

}  // namespace

struct SafeExpression::Impl {
    ParsedExpression parsed;
};

SafeExpression::SafeExpression(
    std::shared_ptr<const Impl> impl)
    : impl_(std::move(impl)) {}

SafeExpression SafeExpression::parse(
    const std::string& expression) {
    return SafeExpression(std::make_shared<const Impl>(
        Impl{ExpressionParser(expression).parse()}));
}

const std::set<std::string>& SafeExpression::symbols()
    const {
    return impl_->parsed.symbols;
}

SafeExpressionEvaluation SafeExpression::evaluate(
    const std::map<std::string, double>& values) const {
    std::map<std::string, std::size_t> variables;
    std::vector<std::string> names;
    std::vector<double> ordered_values;
    names.reserve(impl_->parsed.symbols.size());
    ordered_values.reserve(impl_->parsed.symbols.size());
    for (const auto& symbol : impl_->parsed.symbols) {
        const auto found = values.find(symbol);
        if (found == values.end()) {
            return {
                0.0, {}, "unbound symbol '" + symbol + "'"};
        }
        variables.emplace(symbol, names.size());
        names.push_back(symbol);
        ordered_values.push_back(found->second);
    }
    if (values.size() != names.size()) {
        return {0.0, {}, "safe expression received unknown symbols"};
    }
    const auto result = thermox::platform::evaluate(
        *impl_->parsed.root, ordered_values, variables, {});
    SafeExpressionEvaluation output;
    output.value = result.value;
    output.error = result.error;
    for (const auto& [index, derivative] : result.derivatives) {
        output.derivatives.emplace(names.at(index), derivative);
    }
    return output;
}

std::shared_ptr<const ComponentModel>
make_expression_component_model(
    const ComponentRegistry& registry,
    ExpressionComponentDefinition definition) {
    const bool version_4 = definition.schema_version ==
        expression_component_schema_v4;
    if (!version_4) {
        throw std::invalid_argument(
            "unsupported expression component schema: " +
            definition.schema_version);
    }
    auto& descriptor = definition.descriptor;
    if (!definition.modes.empty()) {
        if (!definition.equations.empty() ||
            !definition.transient_equations.empty()) {
            throw std::invalid_argument(
                "mode-aware expression components cannot combine "
                "top-level and mode-specific equations");
        }
        if (!descriptor.supported_modes.empty()) {
            throw std::invalid_argument(
                "expression component modes are derived from mode "
                "equation declarations");
        }
        std::set<std::string> mode_names;
        for (const auto& mode : definition.modes) {
            if (!valid_name(mode.name) ||
                !mode_names.insert(mode.name).second) {
                throw std::invalid_argument(
                    "expression component mode names must be unique "
                    "and contain only letters, digits, '_' or '-': " +
                    mode.name);
            }
            descriptor.supported_modes.push_back(mode.name);
        }
        if (descriptor.default_mode.empty() ||
            !mode_names.contains(descriptor.default_mode)) {
            throw std::invalid_argument(
                "mode-aware expression component default mode must "
                "name one declared mode");
        }
    } else if (!descriptor.supported_modes.empty() ||
               !descriptor.default_mode.empty()) {
        throw std::invalid_argument(
            "expression component mode metadata requires mode "
            "equation declarations");
    }
    if (descriptor.template_kind.empty() ||
        descriptor.display_name.empty() ||
        descriptor.category.empty() ||
        descriptor.model_name.empty()) {
        throw std::invalid_argument(
            "expression component v4 requires physical template "
            "kind, display name, category, and calculation model name");
    }
    if (!descriptor.artifacts.empty() ||
        !descriptor.required_property_capabilities.empty() ||
        !descriptor.required_thermochemistry_capabilities.empty()) {
        throw std::invalid_argument(
            "expression component v4 cannot access artifacts or "
            "property/thermochemistry callbacks");
    }
    const bool mode_aware = !definition.modes.empty();
    const bool missing_transient_equations = mode_aware
        ? std::any_of(
              definition.modes.begin(), definition.modes.end(),
              [](const auto& mode) {
                  return mode.transient_equations.empty();
              })
        : definition.transient_equations.empty();
    const bool missing_steady_equations = mode_aware
        ? std::any_of(
              definition.modes.begin(), definition.modes.end(),
              [](const auto& mode) {
                  return mode.equations.empty();
              })
        : definition.equations.empty();
    if (descriptor.supports_transient &&
        missing_transient_equations) {
        throw std::invalid_argument(
            "transient expression components require at least one "
            "transient equation in every equation set");
    }
    if (!descriptor.supports_transient &&
        (!descriptor.transient_variables.empty() ||
         !descriptor.internal_variables.empty() ||
         !definition.transient_equations.empty() ||
         std::any_of(
             definition.modes.begin(), definition.modes.end(),
             [](const auto& mode) {
                 return !mode.transient_equations.empty();
             }))) {
        throw std::invalid_argument(
            "expression component transient declarations require "
            "supports_transient");
    }
    if (descriptor.supports_steady && missing_steady_equations) {
        throw std::invalid_argument(
            "steady expression components require at least one "
            "equation in every equation set");
    }
    if (!descriptor.supports_steady &&
        (!definition.equations.empty() ||
         std::any_of(
             definition.modes.begin(), definition.modes.end(),
             [](const auto& mode) {
                 return !mode.equations.empty();
             }))) {
        throw std::invalid_argument(
            "expression component steady equations require "
            "supports_steady");
    }
    if (!descriptor.supports_steady && !descriptor.supports_transient) {
        throw std::invalid_argument(
            "expression component must support steady or transient execution");
    }

    std::set<std::string> allowed_symbols;
    std::set<std::string> transient_symbols;
    std::map<std::string, DimensionSignature> allowed_dimensions;
    std::map<std::string, DimensionSignature> transient_dimensions;
    std::set<std::string> differential_ports;
    for (const auto& variable : descriptor.transient_variables) {
        if (variable.kind == DaeVariableKind::differential) {
            differential_ports.insert(
                variable.port_name + "." + variable.variable_name);
        }
    }
    for (const auto& port : descriptor.ports) {
        const auto& domain =
            registry.require_connector_domain(port.domain);
        for (const auto& variable : domain.variables) {
            if (variable.expand_species) {
                throw std::invalid_argument(
                    "expression component v4 does not support "
                    "species-expanded connector variables: " +
                    port.name + "." + variable.name);
            }
            const auto symbol =
                port.name + "." + variable.name;
            if (!valid_symbol(symbol)) {
                throw std::invalid_argument(
                    "expression component port variable is not a "
                    "valid DSL symbol: " + symbol);
            }
            allowed_symbols.insert(symbol);
            transient_symbols.insert(symbol);
            const auto dimension =
                physical_dimension(variable.dimension);
            allowed_dimensions.emplace(symbol, dimension);
            transient_dimensions.emplace(symbol, dimension);
            if (differential_ports.contains(symbol)) {
                transient_symbols.insert("derivative." + symbol);
                auto derivative_dimension = dimension;
                add_dimension(
                    derivative_dimension,
                    physical_dimension("time"), -1.0);
                transient_dimensions.emplace(
                    "derivative." + symbol,
                    std::move(derivative_dimension));
            }
        }
    }
    if (version_4) {
        std::set<std::string> declared_transient_variables;
        for (const auto& variable : descriptor.transient_variables) {
            const std::string symbol =
                variable.port_name + "." + variable.variable_name;
            if (!allowed_symbols.contains(symbol)) {
                throw std::invalid_argument(
                    "expression component transient variable references unknown port variable: " +
                    symbol);
            }
            if (!declared_transient_variables.insert(symbol).second) {
                throw std::invalid_argument(
                    "expression component transient variable is duplicated: " + symbol);
            }
            if (!std::isfinite(variable.derivative_scale) ||
                variable.derivative_scale <= 0.0) {
                throw std::invalid_argument(
                    "expression component transient derivative scale must be positive and finite: " +
                    symbol);
            }
        }
        for (const auto& variable : descriptor.internal_variables) {
            if (!std::isfinite(variable.initial_value) ||
                !std::isfinite(variable.initial_derivative) ||
                !std::isfinite(variable.state_scale) ||
                variable.state_scale <= 0.0 ||
                !std::isfinite(variable.derivative_scale) ||
                variable.derivative_scale <= 0.0 ||
                std::isnan(variable.lower_bound) ||
                std::isnan(variable.upper_bound) ||
                variable.lower_bound > variable.upper_bound) {
                throw std::invalid_argument(
                    "expression component internal variable has invalid initial value, scale, or bounds: " +
                    variable.name);
            }
        }
    }
    for (const auto& parameter : descriptor.parameters) {
        if (parameter.name.find('{') != std::string::npos ||
            parameter.name.find('}') != std::string::npos) {
            throw std::invalid_argument(
                "expression component v4 does not support "
                "parameter templates: " + parameter.name);
        }
        const auto symbol =
            "parameter." + parameter.name;
        if (!valid_symbol(symbol)) {
            throw std::invalid_argument(
                "expression component parameter is not a valid "
                "DSL symbol: " + symbol);
        }
        allowed_symbols.insert(symbol);
        transient_symbols.insert(symbol);
        const auto dimension =
            physical_dimension(parameter.dimension);
        allowed_dimensions.emplace(symbol, dimension);
        transient_dimensions.emplace(symbol, dimension);
    }

    if (version_4) {
        transient_symbols.insert("time");
        transient_dimensions.emplace(
            "time", physical_dimension("time"));
        std::set<std::string> internal_names;
        for (const auto& variable : descriptor.internal_variables) {
            if (!valid_symbol(variable.name) ||
                !internal_names.insert(variable.name).second) {
                throw std::invalid_argument(
                    "expression component internal variable names must be unique valid symbols: " +
                    variable.name);
            }
            transient_symbols.insert("internal." + variable.name);
            const auto dimension =
                physical_dimension(variable.dimension);
            transient_dimensions.emplace(
                "internal." + variable.name, dimension);
            if (variable.kind == DaeVariableKind::differential) {
                transient_symbols.insert(
                    "derivative.internal." + variable.name);
                auto derivative_dimension = dimension;
                add_dimension(
                    derivative_dimension,
                    physical_dimension("time"), -1.0);
                transient_dimensions.emplace(
                    "derivative.internal." + variable.name,
                    std::move(derivative_dimension));
            }
        }
    }

    std::set<std::string> equation_names;
    std::vector<std::pair<
        AlgebraicExpressionEquation,
        ParsedExpression>> equations;
    equations.reserve(definition.equations.size());
    const auto fingerprint =
        definition_fingerprint(definition);
    for (auto& equation : definition.equations) {
        if (!valid_name(equation.name) ||
            !equation_names.insert(equation.name).second) {
            throw std::invalid_argument(
                "expression equation names must be unique and "
                "contain only letters, digits, '_' or '-': " +
                equation.name);
        }
        if (!std::isfinite(equation.residual_scale) ||
            equation.residual_scale <= 0.0) {
            throw std::invalid_argument(
                "expression equation residual scale must be "
                "positive and finite: " + equation.name);
        }
        auto parsed =
            ExpressionParser{equation.expression}.parse();
        for (const auto& symbol : parsed.symbols) {
            if (allowed_symbols.find(symbol) ==
                allowed_symbols.end()) {
                throw std::invalid_argument(
                    "expression equation '" + equation.name +
                    "' references unknown symbol: " + symbol);
            }
        }
        const bool has_variable =
            std::any_of(
                parsed.symbols.begin(),
                parsed.symbols.end(),
                [&](const auto& symbol) {
                    return symbol.rfind(
                               "parameter.", 0) != 0;
                });
        if (!has_variable) {
            throw std::invalid_argument(
                "expression equation '" + equation.name +
                "' must reference at least one port variable");
        }
        validate_dimensions(
            parsed, allowed_dimensions, equation.name);
        equations.emplace_back(
            std::move(equation), std::move(parsed));
    }

    std::vector<std::pair<
        AlgebraicExpressionEquation,
        ParsedExpression>> transient_equations;
    transient_equations.reserve(definition.transient_equations.size());
    for (auto& equation : definition.transient_equations) {
        if (!valid_name(equation.name) ||
            !equation_names.insert("transient:" + equation.name).second) {
            throw std::invalid_argument(
                "transient expression equation names must be unique and contain only letters, digits, '_' or '-': " +
                equation.name);
        }
        if (!std::isfinite(equation.residual_scale) ||
            equation.residual_scale <= 0.0) {
            throw std::invalid_argument(
                "transient expression equation residual scale must be positive and finite: " +
                equation.name);
        }
        auto parsed = ExpressionParser{equation.expression}.parse();
        for (const auto& symbol : parsed.symbols) {
            if (transient_symbols.find(symbol) == transient_symbols.end()) {
                throw std::invalid_argument(
                    "transient expression equation '" + equation.name +
                    "' references unknown symbol: " + symbol);
            }
        }
        const bool has_variable = std::any_of(
            parsed.symbols.begin(), parsed.symbols.end(),
            [&](const auto& symbol) {
                return symbol != "time" &&
                    symbol.rfind("parameter.", 0) != 0;
            });
        if (!has_variable) {
            throw std::invalid_argument(
                "transient expression equation '" + equation.name +
                "' must reference at least one state or state rate");
        }
        validate_dimensions(
            parsed, transient_dimensions, equation.name);
        transient_equations.emplace_back(
            std::move(equation), std::move(parsed));
    }

    ExpressionComponentModel::ParsedModeEquationSets mode_equations;
    ExpressionComponentModel::ParsedModeEquationSets
        mode_transient_equations;
    const auto parse_mode_equation_set = [&](
        std::vector<AlgebraicExpressionEquation>& declarations,
        const std::set<std::string>& symbols,
        const std::map<std::string, DimensionSignature>& dimensions,
        bool transient,
        const std::string& mode_name) {
        ExpressionComponentModel::ParsedEquationSet parsed_equations;
        parsed_equations.reserve(declarations.size());
        std::set<std::string> names;
        for (auto& equation : declarations) {
            if (!valid_name(equation.name) ||
                !names.insert(equation.name).second) {
                throw std::invalid_argument(
                    "expression component mode '" + mode_name +
                    "' has duplicate or invalid " +
                    (transient ? "transient " : "") +
                    "equation name: " + equation.name);
            }
            if (!std::isfinite(equation.residual_scale) ||
                equation.residual_scale <= 0.0) {
                throw std::invalid_argument(
                    "expression component mode '" + mode_name +
                    "' equation residual scale must be positive and "
                    "finite: " + equation.name);
            }
            auto parsed =
                ExpressionParser{equation.expression}.parse();
            for (const auto& symbol : parsed.symbols) {
                if (!symbols.contains(symbol)) {
                    throw std::invalid_argument(
                        "expression component mode '" + mode_name +
                        "' equation '" + equation.name +
                        "' references unknown symbol: " + symbol);
                }
            }
            const bool has_variable = std::any_of(
                parsed.symbols.begin(), parsed.symbols.end(),
                [&](const auto& symbol) {
                    return (!transient || symbol != "time") &&
                        symbol.rfind("parameter.", 0) != 0;
                });
            if (!has_variable) {
                throw std::invalid_argument(
                    "expression component mode '" + mode_name +
                    "' equation '" + equation.name +
                    "' must reference at least one graph variable");
            }
            validate_dimensions(
                parsed, dimensions,
                mode_name + "." + equation.name);
            parsed_equations.emplace_back(
                std::move(equation), std::move(parsed));
        }
        return parsed_equations;
    };
    const auto validate_fixed_structure = [](
        const ExpressionComponentModel::ParsedEquationSet& reference,
        const ExpressionComponentModel::ParsedEquationSet& candidate,
        const std::string& mode_name,
        const std::string& equation_kind) {
        if (candidate.size() != reference.size()) {
            throw std::invalid_argument(
                "expression component mode '" + mode_name +
                "' must declare the same number of " + equation_kind +
                " equations as the default mode");
        }
        for (std::size_t index = 0;
             index < reference.size(); ++index) {
            const auto& [reference_definition, reference_expression] =
                reference[index];
            const auto& [candidate_definition, candidate_expression] =
                candidate[index];
            if (candidate_definition.name != reference_definition.name ||
                candidate_definition.residual_scale !=
                    reference_definition.residual_scale) {
                throw std::invalid_argument(
                    "expression component mode '" + mode_name +
                    "' must preserve " + equation_kind +
                    " equation names, order, and residual scales");
            }
            if (candidate_expression.symbols !=
                reference_expression.symbols) {
                throw std::invalid_argument(
                    "expression component mode '" + mode_name +
                    "' must preserve variable incidence for " +
                    equation_kind + " equation '" +
                    reference_definition.name + "'");
            }
        }
    };

    for (auto& mode : definition.modes) {
        auto steady = parse_mode_equation_set(
            mode.equations, allowed_symbols, allowed_dimensions,
            false, mode.name);
        auto transient = parse_mode_equation_set(
            mode.transient_equations, transient_symbols,
            transient_dimensions, true, mode.name);
        mode_equations.emplace(mode.name, std::move(steady));
        mode_transient_equations.emplace(
            mode.name, std::move(transient));
    }
    if (!definition.modes.empty()) {
        const auto& reference_steady =
            mode_equations.at(descriptor.default_mode);
        const auto& reference_transient =
            mode_transient_equations.at(descriptor.default_mode);
        for (const auto& mode : descriptor.supported_modes) {
            validate_fixed_structure(
                reference_steady, mode_equations.at(mode), mode,
                "steady");
            validate_fixed_structure(
                reference_transient,
                mode_transient_equations.at(mode), mode,
                "transient");
        }
    }

    return std::make_shared<ExpressionComponentModel>(
        std::move(descriptor), std::move(equations),
        std::move(transient_equations),
        std::move(mode_equations),
        std::move(mode_transient_equations),
        fingerprint);
}

void register_expression_component(
    ComponentRegistry& registry,
    ExpressionComponentDefinition definition) {
    registry.register_model(
        make_expression_component_model(
            registry, std::move(definition)));
}

}  // namespace thermox::platform
