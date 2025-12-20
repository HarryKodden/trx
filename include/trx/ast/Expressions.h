#pragma once

#include "trx/ast/SourceLocation.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace trx::ast {

struct Expression;
using ExpressionPtr = std::shared_ptr<Expression>;

struct LiteralExpression {
    std::variant<double, std::string, bool> value;
};

struct VariableSegment {
    std::string identifier;
    std::optional<ExpressionPtr> subscript; // nullptr when scalar access
};

struct VariableExpression {
    std::vector<VariableSegment> path;
};

enum class UnaryOperator {
    Positive,
    Negate,
    Not
};

struct UnaryExpression {
    UnaryOperator op;
    ExpressionPtr operand;
};

enum class BinaryOperator {
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    And,
    Or
};

struct BinaryExpression {
    BinaryOperator op;
    ExpressionPtr lhs;
    ExpressionPtr rhs;
};

struct FunctionCallExpression {
    std::string functionName;
    std::vector<ExpressionPtr> arguments;
};

enum class BuiltinValue {
    SqlCode,
    TrxDbms,
    TrxTime,
    TrxCode,
    Date,
    Stamp,
    Time,
    Week,
    WeekDay
};

struct BuiltinExpression {
    BuiltinValue value;
    std::vector<ExpressionPtr> arguments;
};

struct SqlFragmentElement {
    std::variant<std::string, VariableExpression> value;
};

struct SqlFragmentExpression {
    std::vector<SqlFragmentElement> fragments;
};

struct Expression {
    using Node = std::variant<LiteralExpression,
                              VariableExpression,
                              UnaryExpression,
                              BinaryExpression,
                              FunctionCallExpression,
                              BuiltinExpression,
                              SqlFragmentExpression>;
    Node node;
};

ExpressionPtr makeNumericLiteral(double value);
ExpressionPtr makeStringLiteral(std::string value);
ExpressionPtr makeBooleanLiteral(bool value);
ExpressionPtr makeVariable(VariableExpression value);
ExpressionPtr makeUnary(UnaryOperator op, ExpressionPtr operand);
ExpressionPtr makeBinary(BinaryOperator op, ExpressionPtr lhs, ExpressionPtr rhs);
ExpressionPtr makeFunctionCall(std::string name, std::vector<ExpressionPtr> arguments);
ExpressionPtr makeBuiltin(BuiltinValue builtin, std::vector<ExpressionPtr> arguments = {});
ExpressionPtr makeSqlFragment(std::vector<SqlFragmentElement> fragments);

} // namespace trx::ast
