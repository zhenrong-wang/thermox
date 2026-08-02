#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>

namespace thermox::platform {

struct SafeExpressionEvaluation {
    double value{0.0};
    std::map<std::string, double> derivatives;
    std::string error;
};

// Immutable, bounded algebraic expression with analytic first derivatives.
// The grammar deliberately excludes assignment, I/O, loops, and callbacks.
class SafeExpression {
public:
    static SafeExpression parse(const std::string& expression);

    [[nodiscard]] const std::set<std::string>& symbols() const;
    [[nodiscard]] SafeExpressionEvaluation evaluate(
        const std::map<std::string, double>& values) const;

private:
    struct Impl;
    explicit SafeExpression(std::shared_ptr<const Impl> impl);
    std::shared_ptr<const Impl> impl_;
};

}  // namespace thermox::platform
