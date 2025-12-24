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

struct ExpressionStatement {
    ExpressionPtr expression;
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

struct VariableDeclarationStatement {
    Identifier name;
    std::string typeName;
    std::optional<ExpressionPtr> initializer;
    std::optional<std::string> tableName; // If set, type will be inferred from database table schema
};

struct BatchStatement {
    std::string name;
    std::optional<VariableExpression> argument;
};

struct ThrowStatement {
    ExpressionPtr value;
};

enum class SqlStatementKind {
    ExecImmediate,
    DeclareCursor,
    OpenCursor,
    FetchCursor,
    CloseCursor,
    SelectForUpdate
};

struct SqlStatement {
    SqlStatementKind kind{SqlStatementKind::ExecImmediate};
    std::string identifier;      // cursor name when applicable
    std::string sql;             // textual SQL (for exec and declare)
    std::vector<VariableExpression> hostVariables; // fetch target list
};

struct TryCatchStatement;
struct BlockStatement;

struct Statement;
using StatementList = std::vector<Statement>;

struct SwitchCase {
    ExpressionPtr match;
    StatementList body;
};

struct IfStatement {
    ExpressionPtr condition;
    StatementList thenBranch;
    StatementList elseBranch;
};

struct WhileStatement {
    ExpressionPtr condition;
    StatementList body;
};

struct SwitchStatement {
    ExpressionPtr selector;
    std::vector<SwitchCase> cases;
    std::optional<StatementList> defaultBranch;
};

struct SortKey {
    double order{1.0};
    std::string fieldName;
};

struct SortStatement {
    VariableExpression array;
    std::vector<SortKey> keys;
};

struct TryCatchStatement {
    StatementList tryBlock;
    std::optional<VariableExpression> exceptionVar;
    StatementList catchBlock;
};

struct BlockStatement {
    StatementList statements;
};

struct ForStatement {
    VariableExpression loopVar;
    ExpressionPtr collection;
    StatementList body;
};

struct Statement {
    using Node = std::variant<TraceStatement,
                              ExpressionStatement,
                              ValidateStatement,
                              ReturnStatement,
                              SystemStatement,
                              AssignmentStatement,
                              VariableDeclarationStatement,
                              BatchStatement,
                              ThrowStatement,
                              TryCatchStatement,
                              SqlStatement,
                              IfStatement,
                              WhileStatement,
                              SwitchStatement,
                              SortStatement,
                              BlockStatement,
                              ForStatement>;
    Node node;
    SourceLocation location{};
};

} // namespace trx::ast
