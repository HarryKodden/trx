#pragma once

#include "trx/ast/SourceLocation.h"
#include "trx/ast/Statements.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace trx::ast {

struct IncludeDecl {
    Identifier file;
};

struct ConstantDecl {
    Identifier name;
    std::variant<double, std::string> value;
};

struct ParameterDecl {
    Identifier type;
};

struct ProcedureDecl {
    Identifier name;
    std::optional<ParameterDecl> input;
    std::optional<ParameterDecl> output;
    std::vector<Statement> body;
};

struct RecordField {
    Identifier name;
    std::string typeName;
    long length{0};
    short dimension{1};
    std::optional<short> scale{};
    std::string jsonName;
    bool jsonOmitEmpty{false};
    bool hasExplicitJsonName{false};
};

struct TableColumn {
    Identifier name;
    std::string typeName;
    bool isPrimaryKey{false};
    bool isNullable{true};
    std::optional<long> length{};
    std::optional<short> scale{};
    std::optional<std::string> defaultValue{};
};

struct RecordDecl {
    Identifier name;
    std::vector<RecordField> fields;
    std::optional<std::string> tableName; // If set, fields will be populated from database schema
};

struct TableDecl {
    Identifier name;
    std::vector<TableColumn> columns;
};

struct ExternalProcedureDecl {
    Identifier name;
    std::optional<Identifier> input;
    std::optional<Identifier> output;
};

using Declaration = std::variant<IncludeDecl,
                                 ConstantDecl,
                                 RecordDecl,
                                 TableDecl,
                                 ProcedureDecl,
                                 ExternalProcedureDecl,
                                 VariableDeclarationStatement,
                                 ExpressionStatement>;

struct Module {
    std::vector<Declaration> declarations;
    std::vector<Statement> statements;
};

} // namespace trx::ast
