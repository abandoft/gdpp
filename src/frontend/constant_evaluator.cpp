#include "gdpp/frontend/constant_evaluator.hpp"

#include "gdpp/frontend/literal.hpp"

#include <limits>

namespace gdpp {

std::optional<std::int64_t>
evaluate_integer_constant(const ast::Expression& expression,
                          const std::unordered_map<std::string, std::int64_t>& previous) {
    if (const auto* literal = expression.get_if<ast::LiteralExpression>();
        literal && literal->kind == ast::LiteralKind::integer) {
        const auto parsed = parse_integer_literal(literal->text);
        if (!parsed)
            return std::nullopt;
        const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (parsed->magnitude <= maximum)
            return static_cast<std::int64_t>(parsed->magnitude);
        if (parsed->base == 10 && parsed->magnitude == maximum + 1U)
            return std::numeric_limits<std::int64_t>::min();
        return std::nullopt;
    }
    if (const auto* identifier = expression.get_if<ast::IdentifierExpression>()) {
        const auto found = previous.find(identifier->name);
        return found == previous.end() ? std::nullopt : std::optional<std::int64_t>{found->second};
    }
    if (const auto* unary = expression.get_if<ast::UnaryExpression>()) {
        const auto operand = evaluate_integer_constant(*unary->operand, previous);
        if (!operand)
            return std::nullopt;
        if (unary->operation == "+")
            return operand;
        if (unary->operation == "~")
            return ~*operand;
        if (unary->operation == "-") {
            // Godot reserves the decimal 2^63 magnitude so the source spelling of INT64_MIN is
            // representable. Keep that sentinel stable instead of invoking signed overflow.
            if (*operand == std::numeric_limits<std::int64_t>::min())
                return *operand;
            return -*operand;
        }
        return std::nullopt;
    }
    const auto* binary = expression.get_if<ast::BinaryExpression>();
    if (!binary)
        return std::nullopt;
    const auto left = evaluate_integer_constant(*binary->left, previous);
    const auto right = evaluate_integer_constant(*binary->right, previous);
    if (!left || !right)
        return std::nullopt;
    const auto minimum = std::numeric_limits<std::int64_t>::min();
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const auto& operation = binary->operation;
    if (operation == "+") {
        if ((*right > 0 && *left > maximum - *right) || (*right < 0 && *left < minimum - *right))
            return std::nullopt;
        return *left + *right;
    }
    if (operation == "-") {
        if ((*right < 0 && *left > maximum + *right) || (*right > 0 && *left < minimum + *right))
            return std::nullopt;
        return *left - *right;
    }
    if (operation == "*") {
        if (*left == 0 || *right == 0)
            return 0;
        if ((*left == -1 && *right == minimum) || (*right == -1 && *left == minimum))
            return std::nullopt;
        if ((*left > 0 && *right > 0 && *left > maximum / *right) ||
            (*left > 0 && *right < 0 && *right < minimum / *left) ||
            (*left < 0 && *right > 0 && *left < minimum / *right) ||
            (*left < 0 && *right < 0 && *left < maximum / *right))
            return std::nullopt;
        return *left * *right;
    }
    if (operation == "/" || operation == "%") {
        if (*right == 0 || (*left == minimum && *right == -1))
            return std::nullopt;
        return operation == "/" ? *left / *right : *left % *right;
    }
    if (operation == "<<" || operation == ">>") {
        if (*right < 0 || *right >= 63 || *left < 0)
            return std::nullopt;
        if (operation == "<<" && *left > (maximum >> *right))
            return std::nullopt;
        return operation == "<<" ? *left << *right : *left >> *right;
    }
    if (operation == "&")
        return *left & *right;
    if (operation == "|")
        return *left | *right;
    if (operation == "^")
        return *left ^ *right;
    return std::nullopt;
}

} // namespace gdpp
