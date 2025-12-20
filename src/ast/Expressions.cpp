#include "trx/ast/Expressions.h"

#include <utility>

namespace trx::ast {

namespace {
ExpressionPtr make(Expression::Node node) {
    return std::make_shared<Expression>(Expression{.node = std::move(node)});
}
} // namespace

ExpressionPtr makeNumericLiteral(double value) {
    return make(LiteralExpression{.value = value});
}

ExpressionPtr makeStringLiteral(std::string value) {
    return make(LiteralExpression{.value = std::move(value)});
}

ExpressionPtr makeBooleanLiteral(bool value) {
    return make(LiteralExpression{.value = value});
}

ExpressionPtr makeVariable(VariableExpression value) {
    return make(std::move(value));
}

ExpressionPtr makeUnary(UnaryOperator op, ExpressionPtr operand) {
    return make(UnaryExpression{.op = op, .operand = std::move(operand)});
}

ExpressionPtr makeBinary(BinaryOperator op, ExpressionPtr lhs, ExpressionPtr rhs) {
    return make(BinaryExpression{.op = op, .lhs = std::move(lhs), .rhs = std::move(rhs)});
}

ExpressionPtr makeFunctionCall(std::string name, std::vector<ExpressionPtr> arguments) {
    return make(FunctionCallExpression{.functionName = std::move(name), .arguments = std::move(arguments)});
}

ExpressionPtr makeBuiltin(BuiltinValue builtin, std::vector<ExpressionPtr> arguments) {
    return make(BuiltinExpression{.value = builtin, .arguments = std::move(arguments)});
}

ExpressionPtr makeSqlFragment(std::vector<SqlFragmentElement> fragments) {
    return make(SqlFragmentExpression{.fragments = std::move(fragments)});
}

} // namespace trx::ast
