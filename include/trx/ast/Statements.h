#pragma once

#include "trx/ast/Expressions.h"
#include "trx/ast/SourceLocation.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace trx::ast {

struct ValidationOutcome {
    double code{0.0};
    std::string message;
};

struct TraceStatement {
    ExpressionPtr value;
};

struct ValidateStatement {
    VariableExpression variable;
    ExpressionPtr rule;
    ValidationOutcome failure;
    ValidationOutcome success;
    ValidationOutcome finalOutcome;
};

struct ReturnStatement {
    ExpressionPtr value;
};

struct SystemStatement {
    ExpressionPtr command;
};

struct AssignmentStatement {
    VariableExpression target;
    ExpressionPtr value;
};

struct BatchStatement {
    std::string name;
    std::optional<VariableExpression> argument;
};

struct CallStatement {
    std::string name;
    std::optional<VariableExpression> input;
    std::optional<VariableExpression> output;
    bool sync{false};
};

enum class SqlStatementKind {
    ExecImmediate,
    DeclareCursor,
    OpenCursor,
    FetchCursor,
    CloseCursor
};

struct SqlStatement {
    SqlStatementKind kind{SqlStatementKind::ExecImmediate};
    std::string identifier;      // cursor name when applicable
    std::string sql;             // textual SQL (for exec and declare)
    std::vector<VariableExpression> hostVariables; // fetch target list
};

struct IfStatement;
struct WhileStatement;
struct SortStatement;
struct BlockStatement;

struct Statement;
using StatementList = std::vector<Statement>;

struct IfStatement {
    ExpressionPtr condition;
    StatementList thenBranch;
    StatementList elseBranch;
};

struct WhileStatement {
    ExpressionPtr condition;
    StatementList body;
};

struct SortKey {
    double order{1.0};
    std::string fieldName;
};

struct SortStatement {
    VariableExpression array;
    std::vector<SortKey> keys;
};

struct BlockStatement {
    StatementList statements;
};

struct Statement {
    using Node = std::variant<TraceStatement,
                              ValidateStatement,
                              ReturnStatement,
                              SystemStatement,
                              AssignmentStatement,
                              BatchStatement,
                              CallStatement,
                              SqlStatement,
                              IfStatement,
                              WhileStatement,
                              SortStatement,
                              BlockStatement>;
    Node node;
    SourceLocation location{};
};

} // namespace trx::ast
