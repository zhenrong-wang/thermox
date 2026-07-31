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
    std::ostringstream out;
    out << "fnv1a64:" << std::hex << std::setw(16)
        << std::setfill('0') << hash;
    return out.str();
}

class ExpressionComponentModel final : public ComponentModel {
public:
    ExpressionComponentModel(
        ComponentModelDescriptor descriptor,
        std::vector<std::pair<
            AlgebraicExpressionEquation,
            ParsedExpression>> equations,
        std::string fingerprint)
        : descriptor_(std::move(descriptor)),
          equations_(std::move(equations)),
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

        for (const auto& [definition, expression] :
             equations_) {
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

private:
    ComponentModelDescriptor descriptor_;
    std::vector<std::pair<
        AlgebraicExpressionEquation,
        ParsedExpression>> equations_;
    std::string fingerprint_;
};

}  // namespace

std::shared_ptr<const ComponentModel>
make_expression_component_model(
    const ComponentRegistry& registry,
    ExpressionComponentDefinition definition) {
    if (definition.schema_version !=
        expression_component_schema_v1) {
        throw std::invalid_argument(
            "unsupported expression component schema: " +
            definition.schema_version);
    }
    auto& descriptor = definition.descriptor;
    if (!descriptor.supports_steady ||
        descriptor.supports_transient ||
        !descriptor.transient_variables.empty() ||
        !descriptor.internal_variables.empty()) {
        throw std::invalid_argument(
            "expression component v1 supports steady algebraic "
            "components only");
    }
    if (!descriptor.artifacts.empty() ||
        !descriptor.required_property_capabilities.empty() ||
        !descriptor.required_thermochemistry_capabilities.empty()) {
        throw std::invalid_argument(
            "expression component v1 cannot access artifacts or "
            "property/thermochemistry callbacks");
    }
    if (definition.equations.empty()) {
        throw std::invalid_argument(
            "expression component requires at least one equation");
    }

    std::set<std::string> allowed_symbols;
    for (const auto& port : descriptor.ports) {
        const auto& domain =
            registry.require_connector_domain(port.domain);
        for (const auto& variable : domain.variables) {
            if (variable.expand_species) {
                throw std::invalid_argument(
                    "expression component v1 does not support "
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
        }
    }
    for (const auto& parameter : descriptor.parameters) {
        if (parameter.name.find('{') != std::string::npos ||
            parameter.name.find('}') != std::string::npos) {
            throw std::invalid_argument(
                "expression component v1 does not support "
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
        equations.emplace_back(
            std::move(equation), std::move(parsed));
    }

    return std::make_shared<ExpressionComponentModel>(
        std::move(descriptor), std::move(equations),
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
