#pragma once

#include "trx/ast/SourceLocation.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace trx::ast {

struct Identifier {
    std::string name;
    SourceLocation location{};
};

struct Expression;
using ExpressionPtr = std::shared_ptr<Expression>;

struct LiteralExpression {
    std::variant<double, std::string, bool> value;
};

struct ObjectLiteralExpression {
    std::unordered_map<std::string, ExpressionPtr> properties;
};

struct ArrayLiteralExpression {
    std::vector<ExpressionPtr> elements;
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

struct MethodCallExpression {
    ExpressionPtr object;
    std::string methodName;
    std::vector<ExpressionPtr> arguments;
};

enum class BuiltinValue {
    SqlCode,
    Date,
    Time,
    Week,
    WeekDay,
    TimeStamp
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
                              ObjectLiteralExpression,
                              ArrayLiteralExpression,
                              VariableExpression,
                              UnaryExpression,
                              BinaryExpression,
                              FunctionCallExpression,
                              MethodCallExpression,
                              BuiltinExpression,
                              SqlFragmentExpression>;
    Node node;
};

ExpressionPtr makeNumericLiteral(double value);
ExpressionPtr makeStringLiteral(std::string value);
ExpressionPtr makeBooleanLiteral(bool value);
ExpressionPtr makeObjectLiteral(std::unordered_map<std::string, ExpressionPtr> properties);
ExpressionPtr makeArrayLiteral(std::vector<ExpressionPtr> elements);
ExpressionPtr makeVariable(VariableExpression value);
ExpressionPtr makeUnary(UnaryOperator op, ExpressionPtr operand);
ExpressionPtr makeBinary(BinaryOperator op, ExpressionPtr lhs, ExpressionPtr rhs);
ExpressionPtr makeFunctionCall(std::string name, std::vector<ExpressionPtr> arguments);
ExpressionPtr makeMethodCall(ExpressionPtr object, std::string methodName, std::vector<ExpressionPtr> arguments);
ExpressionPtr makeBuiltin(BuiltinValue builtin, std::vector<ExpressionPtr> arguments = {});
ExpressionPtr makeSqlFragment(std::vector<SqlFragmentElement> fragments);

} // namespace trx::ast
