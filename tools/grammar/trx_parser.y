%{
#include "trx/parsing/ParserDriver.h"
#include "trx/parsing/ParserContext.h"
#include "trx/ast/Nodes.h"
#include "trx/ast/SourceLocation.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using FieldList = std::vector<trx::ast::RecordField>;
using StatementList = std::vector<trx::ast::Statement>;
using CaseList = std::vector<trx::ast::SwitchCase>;
using ExpressionList = std::vector<trx::ast::ExpressionPtr>;

struct SqlFragments {
    std::string sql;
    std::vector<trx::ast::VariableExpression> hostVariables;
};

struct FieldFormat {
    bool hasJson{false};
    bool explicitTag{false};
    std::string jsonName;
    bool omitEmpty{false};
};

using OptionalVariable = std::optional<trx::ast::VariableExpression>;

using TableColumnList = std::vector<trx::ast::TableColumn>;

struct TableColumnAttrs {
    bool isPrimaryKey{false};
    bool isNullable{true};
    std::optional<long> length{};
    std::optional<short> scale{};
    std::optional<std::string> defaultValue{};
};

inline FieldList *newFieldList() {
    return new FieldList();
}

inline FieldList *fieldListFrom(void *ptr) {
    return static_cast<FieldList *>(ptr);
}

inline TableColumnList *newTableColumnList() {
    return new TableColumnList();
}

inline TableColumnList *tableColumnListFrom(void *ptr) {
    return static_cast<TableColumnList *>(ptr);
}

inline TableColumnAttrs *newTableColumnAttrs() {
    return new TableColumnAttrs();
}

inline TableColumnAttrs *tableColumnAttrsFrom(void *ptr) {
    return static_cast<TableColumnAttrs *>(ptr);
}

inline StatementList *newStatementList() {
    return new StatementList();
}

inline StatementList *statementListFrom(void *ptr) {
    return static_cast<StatementList *>(ptr);
}

inline trx::ast::Statement *statementFrom(void *ptr) {
    return static_cast<trx::ast::Statement *>(ptr);
}

inline CaseList *newCaseList() {
    return new CaseList();
}

inline CaseList *caseListFrom(void *ptr) {
    return static_cast<CaseList *>(ptr);
}

inline trx::ast::SwitchCase *switchCaseFrom(void *ptr) {
    return static_cast<trx::ast::SwitchCase *>(ptr);
}

inline SqlFragments *newSqlFragments() {
    return new SqlFragments();
}

inline SqlFragments *sqlFragmentsFrom(void *ptr) {
    return static_cast<SqlFragments *>(ptr);
}

inline ExpressionList *newExpressionList() {
    return new ExpressionList();
}

inline ExpressionList *expressionListFrom(void *ptr) {
    return static_cast<ExpressionList *>(ptr);
}

inline FieldFormat *newFieldFormat() {
    return new FieldFormat();
}

inline FieldFormat *fieldFormatFrom(void *ptr) {
    return static_cast<FieldFormat *>(ptr);
}

inline OptionalVariable *newOptionalVariable() {
    return new OptionalVariable();
}

inline OptionalVariable *optionalVariableFrom(void *ptr) {
    return static_cast<OptionalVariable *>(ptr);
}

inline trx::ast::VariableExpression *variableFrom(void *ptr) {
    return static_cast<trx::ast::VariableExpression *>(ptr);
}

inline trx::ast::VariableExpression *variableFromPath(char *raw) {
    auto variable = new trx::ast::VariableExpression();
    if (!raw) {
        return variable;
    }

    std::string text(raw);
    std::free(raw);

    std::size_t start = 0;
    while (start <= text.size()) {
        const auto next = text.find('.', start);
        const auto length = (next == std::string::npos) ? std::string::npos : next - start;
        auto segment = text.substr(start, length);
        if (!segment.empty()) {
            variable->path.push_back(trx::ast::VariableSegment{.identifier = std::move(segment), .subscript = std::nullopt});
        }
        if (next == std::string::npos) {
            break;
        }
        start = next + 1;
    }

    return variable;
}

inline trx::ast::ExpressionPtr *expressionFrom(void *ptr) {
    return static_cast<trx::ast::ExpressionPtr *>(ptr);
}

inline trx::ast::ExpressionPtr *wrapExpression(trx::ast::ExpressionPtr value) {
    return new trx::ast::ExpressionPtr(std::move(value));
}

inline trx::ast::ExpressionPtr *binaryExpression(trx::ast::BinaryOperator op, void *lhsPtr, void *rhsPtr) {
    auto lhs = expressionFrom(lhsPtr);
    auto rhs = expressionFrom(rhsPtr);
    auto expr = wrapExpression(trx::ast::makeBinary(op, std::move(*lhs), std::move(*rhs)));
    delete lhs;
    delete rhs;
    return expr;
}

inline trx::ast::ExpressionPtr *unaryExpression(trx::ast::UnaryOperator op, void *operandPtr) {
    auto operand = expressionFrom(operandPtr);
    auto expr = wrapExpression(trx::ast::makeUnary(op, std::move(*operand)));
    delete operand;
    return expr;
}

inline long toLength(double value) {
    auto converted = static_cast<long>(value);
    if (converted < 0) {
        converted = 0;
    }
    return converted;
}

inline short toScale(double value) {
    auto converted = static_cast<short>(value);
    if (converted < 0) {
        converted = 0;
    }
    return converted;
}

inline short toDimension(double value) {
    auto converted = static_cast<long>(value);
    if (converted < 1) {
        converted = 1;
    }
    if (converted > std::numeric_limits<short>::max()) {
        converted = std::numeric_limits<short>::max();
    }
    return static_cast<short>(converted);
}

inline void trimSql(std::string &sql) {
    const auto first = sql.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        sql.clear();
        return;
    }
    const auto last = sql.find_last_not_of(" \t\r\n");
    sql = sql.substr(first, last - first + 1);
}

inline void trimWhitespace(std::string &text) {
    trimSql(text);
}

inline std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline void parseJsonFormatString(const std::string &text, FieldFormat &format) {
    bool firstPart = true;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto next = text.find(',', start);
        auto part = text.substr(start, next == std::string::npos ? std::string::npos : next - start);
        trimWhitespace(part);
        if (firstPart) {
            if (!part.empty() && part != "-") {
                format.jsonName = part;
            }
            firstPart = false;
        } else {
            if (part == "omitempty") {
                format.omitEmpty = true;
            }
        }
        if (next == std::string::npos) {
            break;
        }
        start = next + 1;
    }
    format.hasJson = true;
    format.explicitTag = true;
}

inline void applyFieldFormat(trx::ast::RecordField &field, const FieldFormat *format) {
    field.jsonName = toLowerCopy(field.name.name);
    field.jsonOmitEmpty = false;
    field.hasExplicitJsonName = false;

    if (format && format->hasJson) {
        field.hasExplicitJsonName = format->explicitTag;
        if (format->explicitTag && !format->jsonName.empty()) {
            field.jsonName = format->jsonName;
        }
        field.jsonOmitEmpty = format->omitEmpty;
    }
}

inline std::size_t skipSqlWhitespace(const std::string &text, std::size_t index) {
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index]))) {
        ++index;
    }
    return index;
}

inline std::pair<std::size_t, std::size_t> readSqlIdentifierBounds(const std::string &text, std::size_t index) {
    const auto start = index;
    while (index < text.size() && !std::isspace(static_cast<unsigned char>(text[index]))) {
        ++index;
    }
    return {start, index};
}

inline void classifySqlStatement(trx::ast::SqlStatement &statement) {
    if (statement.sql.empty()) {
        return;
    }

    std::string upper = statement.sql;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });

    const auto annotateIdentifier = [&](std::size_t start, std::size_t end) {
        statement.identifier = statement.sql.substr(start, end - start);
    };

    const auto matchesKeyword = [&](std::string_view keyword) -> bool {
        if (upper.size() < keyword.size()) {
            return false;
        }
        if (upper.compare(0, keyword.size(), keyword.data(), keyword.size()) != 0) {
            return false;
        }
        if (upper.size() == keyword.size()) {
            return true;
        }
        return std::isspace(static_cast<unsigned char>(upper[keyword.size()])) != 0;
    };

    if (matchesKeyword("DECLARE")) {
        auto pos = skipSqlWhitespace(upper, std::size_t(7));
        const auto [nameStart, nameEnd] = readSqlIdentifierBounds(upper, pos);
        if (nameStart == nameEnd) {
            return;
        }
        annotateIdentifier(nameStart, nameEnd);
        pos = skipSqlWhitespace(upper, nameEnd);
        if (pos + 6 <= upper.size() && upper.compare(pos, 6, "CURSOR") == 0 && (pos + 6 == upper.size() || std::isspace(static_cast<unsigned char>(upper[pos + 6])))) {
            statement.kind = trx::ast::SqlStatementKind::DeclareCursor;
        }
        return;
    }

    if (matchesKeyword("OPEN")) {
        auto pos = skipSqlWhitespace(upper, std::size_t(4));
        const auto [nameStart, nameEnd] = readSqlIdentifierBounds(upper, pos);
        if (nameStart == nameEnd) {
            return;
        }
        annotateIdentifier(nameStart, nameEnd);
        statement.kind = trx::ast::SqlStatementKind::OpenCursor;
        return;
    }

    if (matchesKeyword("CLOSE")) {
        auto pos = skipSqlWhitespace(upper, std::size_t(5));
        const auto [nameStart, nameEnd] = readSqlIdentifierBounds(upper, pos);
        if (nameStart == nameEnd) {
            return;
        }
        annotateIdentifier(nameStart, nameEnd);
        statement.kind = trx::ast::SqlStatementKind::CloseCursor;
        return;
    }

    if (matchesKeyword("FETCH")) {
        auto pos = skipSqlWhitespace(upper, std::size_t(5));
        auto [tokenStart, tokenEnd] = readSqlIdentifierBounds(upper, pos);
        if (tokenStart == tokenEnd) {
            return;
        }

        if (upper.compare(tokenStart, tokenEnd - tokenStart, "FROM") == 0) {
            pos = skipSqlWhitespace(upper, tokenEnd);
            std::tie(tokenStart, tokenEnd) = readSqlIdentifierBounds(upper, pos);
            if (tokenStart == tokenEnd) {
                return;
            }
        }

        annotateIdentifier(tokenStart, tokenEnd);
        statement.kind = trx::ast::SqlStatementKind::FetchCursor;
        return;
    }

    // Check for SELECT ... FOR UPDATE OF ... statements
    if (matchesKeyword("SELECT")) {
        // Look for "FOR UPDATE OF" pattern in the SQL
        std::string::size_type forUpdatePos = upper.find(" FOR UPDATE OF ");
        if (forUpdatePos != std::string::npos) {
            statement.kind = trx::ast::SqlStatementKind::SelectForUpdate;
        }
        return;
    }
}
} // namespace

struct YYLTYPE;
union YYSTYPE;

int yylex(YYSTYPE *yylval, YYLTYPE *yylloc, void *yyscanner);
void yyerror(YYLTYPE *loc, trx::parsing::ParserDriver &driver, void *scanner, const char *message);
%}

%code provides {
    [[maybe_unused]] static trx::ast::SourceLocation makeLocation(const trx::parsing::ParserDriver &driver, const YYLTYPE &loc) {
        return trx::ast::SourceLocation{.file = driver.currentFile(), .line = static_cast<std::size_t>(loc.first_line), .column = static_cast<std::size_t>(loc.first_column)};
    }
}

%define api.pure full
%define parse.error verbose

%lex-param { void *scanner }
%parse-param { trx::parsing::ParserDriver &driver }
%parse-param { void *scanner }

%locations

%union {
    char *text;
    double number;
    void *ptr;
}

%token <text> IDENT STRING PATH SQL_TEXT SQL_VARIABLE
%token <number> NUMBER
%token INCLUDE CONSTANT FUNCTION PROCEDURE TABLE PRIMARY KEY NULL_K TYPE FROM VAR LIST
%token IF ELSE WHILE SWITCH CASE DEFAULT CALL TRY CATCH THROW RETURN
%token EXEC_SQL
%token ASSIGN
%token AND OR NOT TRUE FALSE
%token LE GE NEQ NEQ2 DOT
%token _CHAR _INTEGER _SMALLINT _DECIMAL _BOOLEAN _FILE _BLOB
%token DATE TIME JSON
%token SQLCODE TIMESTAMP WEEK WEEKDAY
%token LBRACE RBRACE LBRACKET RBRACKET LPAREN RPAREN COMMA SEMICOLON

%type <text> identifier
%type <text> input_type
%type <text> type_name
%type <text> include_target
%type <text> key
%type <ptr> fields field_def
%type <ptr> procedure_body block statement_list statement assignment_statement variable_declaration_statement throw_statement return_statement try_catch_statement if_statement else_clause while_statement switch_statement case_clauses case_clause default_clause sql_statement expression_statement arguments sql_chunks sql_chunk
%type <ptr> format_decl
%type <ptr> variable expression variable_reference
%type <ptr> logical_or_expression logical_and_expression equality_expression relational_expression additive_expression multiplicative_expression unary_expression primary_expression builtin literal object_properties array_elements
%type <number> dimension

%left OR
%left AND
%nonassoc '=' NEQ NEQ2
%nonassoc '<' '>' LE GE
%left '+' '-'
%left '*' '/'
%left DOT
%right NOT
%right UPLUS UMINUS

%%

translation_unit
    : definitions
    ;

definitions
    : /* empty */
    | definitions definition
    ;

definition
    : include_decl
    | constant_decl
    | type_decl
    | function_decl
    | procedure_decl
    | statement
      {
          auto stmt = statementFrom($1);
          if (stmt) {
              driver.context().addStatement(std::move(*stmt));
          }
          delete stmt;
      }
    ;

include_decl
        : INCLUDE include_target SEMICOLON
            {
                    auto name = std::string($2 ? $2 : "");
                    driver.context().addInclude(std::move(name), makeLocation(driver, @2));
                    std::free($2);
            }
        ;

include_target
        : identifier
            {
                    $$ = $1;
            }
        | PATH
            {
                    $$ = $1;
            }
        | STRING
            {
                    $$ = $1;
            }
        ;

constant_decl
    : CONSTANT identifier NUMBER SEMICOLON
      {
          auto name = std::string($2 ? $2 : "");
          driver.context().addConstant(name, $3, makeLocation(driver, @2));
          std::free($2);
      }
    | CONSTANT identifier STRING SEMICOLON
      {
          auto name = std::string($2 ? $2 : "");
          auto value = std::string($3 ? $3 : "");
          driver.context().addConstant(std::move(name), std::move(value), makeLocation(driver, @2));
          std::free($2);
          std::free($3);
      }
    ;

type_decl
    : TYPE identifier LBRACE fields RBRACE
      {
          trx::ast::RecordDecl record;
          record.name = {.name = $2 ? std::string($2) : std::string{}, .location = makeLocation(driver, @2)};

          auto list = fieldListFrom($4);
          if (list) {
              record.fields = std::move(*list);
              delete list;
          }

          driver.context().addRecord(std::move(record));
          std::free($2);
      }
    | TYPE identifier FROM TABLE identifier SEMICOLON
      {
          trx::ast::RecordDecl record;
          record.name = {.name = $2 ? std::string($2) : std::string{}, .location = makeLocation(driver, @2)};
          record.tableName = $5 ? std::string($5) : std::string{};

          driver.context().addRecord(std::move(record));
          std::free($2);
          std::free($5);
      }
    ;

fields
    : /* empty */
      {
          $$ = newFieldList();
      }
    | fields field_def SEMICOLON
      {
          auto list = fieldListFrom($1);
          auto single = fieldListFrom($2);
          list->insert(list->end(), single->begin(), single->end());
          delete single;
          $$ = list;
      }
    ;

field_def
        : identifier _CHAR LPAREN NUMBER RPAREN dimension format_decl
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = "CHAR";
          field.length = toLength($4);
                    field.dimension = toDimension($6);
                    auto format = fieldFormatFrom($7);
                    applyFieldFormat(field, format);
                    delete format;
          list->push_back(std::move(field));
          std::free($1);
          $$ = list;
      }
        | identifier DATE dimension format_decl
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = "DATE";
          field.length = 8;
                    field.dimension = toDimension($3);
                    auto format = fieldFormatFrom($4);
                    applyFieldFormat(field, format);
                    delete format;
          list->push_back(std::move(field));
          std::free($1);
          $$ = list;
      }
        | identifier TIME dimension format_decl
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = "TIME";
          field.length = 4;
                    field.dimension = toDimension($3);
                    auto format = fieldFormatFrom($4);
                    applyFieldFormat(field, format);
                    delete format;
          list->push_back(std::move(field));
          std::free($1);
          $$ = list;
      }
        | identifier _INTEGER dimension format_decl
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = "INTEGER";
          field.length = 4;
                    field.dimension = toDimension($3);
                    auto format = fieldFormatFrom($4);
                    applyFieldFormat(field, format);
                    delete format;
          list->push_back(std::move(field));
          std::free($1);
          $$ = list;
      }
        | identifier _SMALLINT dimension format_decl
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = "SMALLINT";
          field.length = 2;
                    field.dimension = toDimension($3);
                    auto format = fieldFormatFrom($4);
                    applyFieldFormat(field, format);
                    delete format;
          list->push_back(std::move(field));
          std::free($1);
          $$ = list;
      }
        | identifier _BOOLEAN dimension format_decl
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = "BOOLEAN";
          field.length = 1;
                    field.dimension = toDimension($3);
                    auto format = fieldFormatFrom($4);
                    applyFieldFormat(field, format);
                    delete format;
          list->push_back(std::move(field));
          std::free($1);
          $$ = list;
      }
        | identifier _DECIMAL LPAREN NUMBER COMMA NUMBER RPAREN dimension format_decl
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = "DECIMAL";
          field.length = toLength($4);
          field.dimension = toDimension($8);
          field.scale = static_cast<short>(toLength($6));
                    auto format = fieldFormatFrom($9);
                    applyFieldFormat(field, format);
                    delete format;
          list->push_back(std::move(field));
          std::free($1);
          $$ = list;
      }
        | identifier _BLOB dimension format_decl
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = "BLOB";
          field.length = 256;
                    field.dimension = toDimension($3);
                    auto format = fieldFormatFrom($4);
                    applyFieldFormat(field, format);
                    delete format;
          list->push_back(std::move(field));
          std::free($1);
          $$ = list;
      }
        | identifier _FILE dimension format_decl
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = "FILE";
          field.length = 256;
                    field.dimension = toDimension($3);
                    auto format = fieldFormatFrom($4);
                    applyFieldFormat(field, format);
                    delete format;
          list->push_back(std::move(field));
          std::free($1);
          $$ = list;
      }
        | identifier LIST LPAREN type_name RPAREN dimension format_decl
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = std::string("LIST(") + ($4 ? std::string($4) : std::string{}) + ")";
          field.length = 0;
                    field.dimension = toDimension($6);
                    auto format = fieldFormatFrom($7);
                    applyFieldFormat(field, format);
                    delete format;
          list->push_back(std::move(field));
          std::free($1);
          std::free($4);
          $$ = list;
      }
        | identifier identifier dimension format_decl
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = $2 ? std::string($2) : std::string{};
          field.length = 0;
                    field.dimension = toDimension($3);
                    auto format = fieldFormatFrom($4);
                    applyFieldFormat(field, format);
                    delete format;
          list->push_back(std::move(field));
          std::free($1);
          std::free($2);
          $$ = list;
      }
    ;

dimension
    : /* empty */
      {
          $$ = 1.0;
      }
    | '[' NUMBER ']'
      {
          $$ = $2;
      }
    ;

format_decl
    : /* empty */
      {
          $$ = nullptr;
      }
    | JSON ':' STRING
      {
          std::string kind = "json";
          std::string raw = $3 ? std::string($3) : std::string{};
          std::free($3);

          if (kind == "json") {
              auto format = newFieldFormat();
              parseJsonFormatString(raw, *format);
              $$ = format;
          } else {
              $$ = nullptr;
          }
      }
    ;

function_decl
    : FUNCTION identifier LPAREN input_type RPAREN ':' type_name procedure_body
        {
            trx::ast::ProcedureDecl procedure;
            procedure.name = {.name = $2 ? std::string($2) : std::string{}, .location = makeLocation(driver, @2)};

            if ($4) {
                procedure.input = driver.context().makeParameter(std::string($4), makeLocation(driver, @4));
                std::free($4);
            }

            if ($7) {
                procedure.output = driver.context().makeParameter(std::string($7), makeLocation(driver, @7));
                std::free($7);
            }

            auto body = statementListFrom($8);
            if (body) {
                procedure.body = std::move(*body);
                delete body;
            }

            driver.context().addProcedure(std::move(procedure));
            std::free($2);
        }
    ;

procedure_decl
    : PROCEDURE identifier LPAREN input_type RPAREN procedure_body
        {
            trx::ast::ProcedureDecl procedure;
            procedure.name = {.name = $2 ? std::string($2) : std::string{}, .location = makeLocation(driver, @2)};

            if ($4) {
                procedure.input = driver.context().makeParameter(std::string($4), makeLocation(driver, @4));
                std::free($4);
            }

            auto body = statementListFrom($6);
            if (body) {
                procedure.body = std::move(*body);
                delete body;
            }

            driver.context().addProcedure(std::move(procedure));
            std::free($2);
        }
    ;

procedure_body
        : block
            {
                    $$ = $1;
            }
        ;

block
        : LBRACE RBRACE
            {
                    $$ = newStatementList();
            }
        | LBRACE statement_list RBRACE
            {
                    $$ = $2;
            }
        ;

statement_list
    : statement
        {
            auto list = newStatementList();
            auto stmt = statementFrom($1);
            list->push_back(std::move(*stmt));
            delete stmt;
            $$ = list;
        }
    | statement_list statement
        {
            auto list = statementListFrom($1);
            auto stmt = statementFrom($2);
            list->push_back(std::move(*stmt));
            delete stmt;
            $$ = list;
        }
    ;

statement
    : assignment_statement
        {
            $$ = $1;
        }
    | variable_declaration_statement
        {
            $$ = $1;
        }
    | throw_statement
        {
            $$ = $1;
        }
    | try_catch_statement
        {
            $$ = $1;
        }
    | if_statement
        {
                $$ = $1;
        }
    | while_statement
        {
                $$ = $1;
        }
    | switch_statement
        {
                $$ = $1;
        }
    | sql_statement
        {
                $$ = $1;
        }
    | return_statement
        {
            $$ = $1;
        }
    | expression_statement
        {
            $$ = $1;
        }
    ;

assignment_statement
    : variable ASSIGN expression SEMICOLON
      {
          auto stmt = new trx::ast::Statement();
          stmt->location = makeLocation(driver, @1);
          auto target = variableFrom($1);
          auto value = expressionFrom($3);
          stmt->node = trx::ast::AssignmentStatement{
              .target = std::move(*target),
              .value = std::move(*value)
          };
          delete target;
          delete value;
          $$ = stmt;
      }
    ;

variable_declaration_statement
    : VAR identifier SEMICOLON
      {
          auto stmt = new trx::ast::Statement();
          stmt->location = makeLocation(driver, @1);
          stmt->node = trx::ast::VariableDeclarationStatement{
              .name = {.name = $2 ? std::string($2) : std::string{}, .location = makeLocation(driver, @2)},
              .typeName = std::string{},
              .initializer = std::nullopt,
              .tableName = std::nullopt
          };
          std::free($2);
          $$ = stmt;
      }
    | VAR identifier type_name SEMICOLON
      {
          auto stmt = new trx::ast::Statement();
          stmt->location = makeLocation(driver, @1);
          stmt->node = trx::ast::VariableDeclarationStatement{
              .name = {.name = $2 ? std::string($2) : std::string{}, .location = makeLocation(driver, @2)},
              .typeName = $3 ? std::string($3) : std::string{},
              .initializer = std::nullopt,
              .tableName = std::nullopt
          };
          std::free($2);
          std::free($3);
          $$ = stmt;
      }
    | VAR identifier ASSIGN expression SEMICOLON
      {
          auto stmt = new trx::ast::Statement();
          stmt->location = makeLocation(driver, @1);
          auto value = expressionFrom($4);
          stmt->node = trx::ast::VariableDeclarationStatement{
              .name = {.name = $2 ? std::string($2) : std::string{}, .location = makeLocation(driver, @2)},
              .typeName = std::string{},
              .initializer = std::move(*value),
              .tableName = std::nullopt
          };
          delete value;
          std::free($2);
          $$ = stmt;
      }
    | VAR identifier type_name ASSIGN expression SEMICOLON
      {
          auto stmt = new trx::ast::Statement();
          stmt->location = makeLocation(driver, @1);
          auto value = expressionFrom($5);
          stmt->node = trx::ast::VariableDeclarationStatement{
              .name = {.name = $2 ? std::string($2) : std::string{}, .location = makeLocation(driver, @2)},
              .typeName = $3 ? std::string($3) : std::string{},
              .initializer = std::move(*value),
              .tableName = std::nullopt
          };
          delete value;
          std::free($2);
          std::free($3);
          $$ = stmt;
      }
    | VAR identifier FROM TABLE identifier SEMICOLON
      {
          auto stmt = new trx::ast::Statement();
          stmt->location = makeLocation(driver, @1);
          stmt->node = trx::ast::VariableDeclarationStatement{
              .name = {.name = $2 ? std::string($2) : std::string{}, .location = makeLocation(driver, @2)},
              .typeName = std::string{},
              .initializer = std::nullopt,
              .tableName = $5 ? std::string($5) : std::string{}
          };
          std::free($2);
          std::free($5);
          $$ = stmt;
      }

throw_statement
    : THROW expression SEMICOLON
      {
          auto stmt = new trx::ast::Statement();
          stmt->location = makeLocation(driver, @1);
          auto value = expressionFrom($2);
          stmt->node = trx::ast::ThrowStatement{
              .value = std::move(*value)
          };
          delete value;
          $$ = stmt;
      }
    ;

return_statement
    : RETURN expression SEMICOLON
      {
          auto stmt = new trx::ast::Statement();
          stmt->location = makeLocation(driver, @1);
          auto value = expressionFrom($2);
          stmt->node = trx::ast::ReturnStatement{
              .value = std::move(*value)
          };
          delete value;
          $$ = stmt;
      }
    ;

try_catch_statement
    : TRY block CATCH LPAREN variable RPAREN block
      {
          auto stmt = new trx::ast::Statement();
          stmt->location = makeLocation(driver, @1);
          auto tryBlock = statementListFrom($2);
          auto exceptionVar = variableFrom($5);
          auto catchBlock = statementListFrom($7);
          stmt->node = trx::ast::TryCatchStatement{
              .tryBlock = std::move(*tryBlock),
              .exceptionVar = std::move(*exceptionVar),
              .catchBlock = std::move(*catchBlock)
          };
          delete tryBlock;
          delete exceptionVar;
          delete catchBlock;
          $$ = stmt;
      }
    ;

if_statement
        : IF expression block else_clause
            {
                    auto stmt = new trx::ast::Statement();
                    stmt->location = makeLocation(driver, @1);
                    auto condition = expressionFrom($2);
                    auto thenBranch = statementListFrom($3);
                    auto elseBranch = statementListFrom($4);
                    trx::ast::IfStatement node;
                    node.condition = std::move(*condition);
                    node.thenBranch = std::move(*thenBranch);
                    node.elseBranch = std::move(*elseBranch);
                    delete condition;
                    delete thenBranch;
                    delete elseBranch;
                    stmt->node = std::move(node);
                    $$ = stmt;
            }
        ;

else_clause
        : ELSE block
            {
                    $$ = $2;
            }
        | /* empty */
            {
                    $$ = newStatementList();
            }
        ;

while_statement
        : WHILE expression block
            {
                    auto stmt = new trx::ast::Statement();
                    stmt->location = makeLocation(driver, @1);
                    auto condition = expressionFrom($2);
                    auto body = statementListFrom($3);
                    trx::ast::WhileStatement node;
                    node.condition = std::move(*condition);
                    node.body = std::move(*body);
                    delete condition;
                    delete body;
                    stmt->node = std::move(node);
                    $$ = stmt;
            }
        ;

switch_statement
        : SWITCH expression LBRACE case_clauses default_clause RBRACE
            {
                    auto stmt = new trx::ast::Statement();
                    stmt->location = makeLocation(driver, @1);
                    auto selector = expressionFrom($2);
                    auto clauses = caseListFrom($4);
                    auto defaultRaw = $5;
                    trx::ast::SwitchStatement node;
                    node.selector = std::move(*selector);
                    if (clauses) {
                            node.cases = std::move(*clauses);
                            delete clauses;
                    }
                    if (defaultRaw) {
                            auto defaultBlock = statementListFrom(defaultRaw);
                            node.defaultBranch = std::move(*defaultBlock);
                            delete defaultBlock;
                    } else {
                            node.defaultBranch = std::nullopt;
                    }
                    delete selector;
                    stmt->node = std::move(node);
                    $$ = stmt;
            }
        ;

case_clauses
        : /* empty */
            {
                    $$ = newCaseList();
            }
        | case_clauses case_clause
            {
                    auto list = caseListFrom($1);
                    auto clause = switchCaseFrom($2);
                    list->push_back(std::move(*clause));
                    delete clause;
                    $$ = list;
            }
        ;

case_clause
        : CASE expression block
            {
                    auto clause = new trx::ast::SwitchCase();
                    auto value = expressionFrom($2);
                    auto body = statementListFrom($3);
                    clause->match = std::move(*value);
                    clause->body = std::move(*body);
                    delete value;
                    delete body;
                    $$ = clause;
            }
        ;

default_clause
        : DEFAULT block
            {
                    $$ = $2;
            }
        | /* empty */
            {
                    $$ = nullptr;
            }
        ;

sql_statement
    : EXEC_SQL sql_chunks SEMICOLON
      {
          auto stmt = new trx::ast::Statement();
          stmt->location = makeLocation(driver, @1);
          auto fragments = sqlFragmentsFrom($2);
          trx::ast::SqlStatement node;
          node.kind = trx::ast::SqlStatementKind::ExecImmediate;
          if (fragments) {
              node.sql = std::move(fragments->sql);
              trimSql(node.sql);
              node.hostVariables = std::move(fragments->hostVariables);
              delete fragments;
              classifySqlStatement(node);
          }
          stmt->node = std::move(node);
          $$ = stmt;
      }
    ;

expression_statement
    : expression SEMICOLON
      {
          auto stmt = new trx::ast::Statement();
          stmt->location = makeLocation(driver, @1);
          // For expression statements, we evaluate the expression but discard the result
          // This allows function calls like debug(), info(), error() to be used as statements
          auto expr = expressionFrom($1);
          trx::ast::ExpressionStatement node;
          node.expression = std::move(*expr);
          stmt->node = std::move(node);
          delete expr;
          $$ = stmt;
      }
    ;

arguments
    : /* empty */
      {
          $$ = newExpressionList();
      }
    | expression
      {
          auto list = newExpressionList();
          auto expr = expressionFrom($1);
          list->push_back(std::move(*expr));
          delete expr;
          $$ = list;
      }
    | arguments COMMA expression
      {
          auto list = expressionListFrom($1);
          auto expr = expressionFrom($3);
          list->push_back(std::move(*expr));
          delete expr;
          $$ = list;
      }
    ;

sql_chunks
    : sql_chunk
      {
          $$ = $1;
      }
    | sql_chunks sql_chunk
      {
          auto accum = sqlFragmentsFrom($1);
          auto fragment = sqlFragmentsFrom($2);
          if (fragment) {
              accum->sql += fragment->sql;
              for (auto &var : fragment->hostVariables) {
                  accum->hostVariables.push_back(std::move(var));
              }
              delete fragment;
          }
          $$ = accum;
      }
    ;

sql_chunk
    : SQL_TEXT
      {
          auto fragment = newSqlFragments();
          if ($1) {
              fragment->sql.append($1);
              std::free($1);
          }
          $$ = fragment;
      }
    | SQL_VARIABLE
      {
          auto fragment = newSqlFragments();
          auto variable = variableFromPath($1);
          fragment->sql.push_back('?');
          if (variable) {
              fragment->hostVariables.push_back(std::move(*variable));
              delete variable;
          }
          $$ = fragment;
      }
    ;

expression
        : logical_or_expression
            {
                    $$ = $1;
            }
        ;

logical_or_expression
        : logical_and_expression
            {
                    $$ = $1;
            }
        | logical_or_expression OR logical_and_expression
            {
                    $$ = binaryExpression(trx::ast::BinaryOperator::Or, $1, $3);
            }
        ;

logical_and_expression
        : equality_expression
            {
                    $$ = $1;
            }
        | logical_and_expression AND equality_expression
            {
                    $$ = binaryExpression(trx::ast::BinaryOperator::And, $1, $3);
            }
        ;

equality_expression
        : relational_expression
            {
                    $$ = $1;
            }
        | equality_expression '=' relational_expression
            {
                    $$ = binaryExpression(trx::ast::BinaryOperator::Equal, $1, $3);
            }
        | equality_expression NEQ relational_expression
            {
                    $$ = binaryExpression(trx::ast::BinaryOperator::NotEqual, $1, $3);
            }
        | equality_expression NEQ2 relational_expression
            {
                    $$ = binaryExpression(trx::ast::BinaryOperator::NotEqual, $1, $3);
            }
        ;

relational_expression
        : additive_expression
            {
                    $$ = $1;
            }
        | relational_expression '<' additive_expression
            {
                    $$ = binaryExpression(trx::ast::BinaryOperator::Less, $1, $3);
            }
        | relational_expression '>' additive_expression
            {
                    $$ = binaryExpression(trx::ast::BinaryOperator::Greater, $1, $3);
            }
        | relational_expression LE additive_expression
            {
                    $$ = binaryExpression(trx::ast::BinaryOperator::LessEqual, $1, $3);
            }
        | relational_expression GE additive_expression
            {
                    $$ = binaryExpression(trx::ast::BinaryOperator::GreaterEqual, $1, $3);
            }
        ;

additive_expression
        : multiplicative_expression
            {
                    $$ = $1;
            }
        | additive_expression '+' multiplicative_expression
            {
                    $$ = binaryExpression(trx::ast::BinaryOperator::Add, $1, $3);
            }
        | additive_expression '-' multiplicative_expression
            {
                    $$ = binaryExpression(trx::ast::BinaryOperator::Subtract, $1, $3);
            }
        ;

multiplicative_expression
        : unary_expression
            {
                    $$ = $1;
            }
        | multiplicative_expression '*' unary_expression
            {
                    $$ = binaryExpression(trx::ast::BinaryOperator::Multiply, $1, $3);
            }
        | multiplicative_expression '/' unary_expression
            {
                    $$ = binaryExpression(trx::ast::BinaryOperator::Divide, $1, $3);
            }
        ;

unary_expression
        : primary_expression
            {
                    $$ = $1;
            }
        | '-' unary_expression %prec UMINUS
            {
                    $$ = unaryExpression(trx::ast::UnaryOperator::Negate, $2);
            }
        | '+' unary_expression %prec UPLUS
            {
                    $$ = unaryExpression(trx::ast::UnaryOperator::Positive, $2);
            }
        | NOT unary_expression
            {
                    $$ = unaryExpression(trx::ast::UnaryOperator::Not, $2);
            }
        ;

primary_expression
        : literal
            {
                    $$ = $1;
            }
        | builtin
            {
                    $$ = $1;
            }
        | variable_reference
            {
                    $$ = $1;
            }
        | identifier LPAREN arguments RPAREN
            {
                    auto args = expressionListFrom($3);
                    auto expr = wrapExpression(trx::ast::makeFunctionCall($1 ? std::string($1) : std::string{}, std::move(*args)));
                    delete args;
                    std::free($1);
                    $$ = expr;
            }
        | variable DOT identifier LPAREN arguments RPAREN
            {
                    auto args = expressionListFrom($5);
                    auto object = expressionFrom(wrapExpression(trx::ast::makeVariable(std::move(*variableFrom($1)))));
                    auto expr = wrapExpression(trx::ast::makeMethodCall(std::move(*object), $3 ? std::string($3) : std::string{}, std::move(*args)));
                    delete args;
                    delete object;
                    std::free($3);
                    $$ = expr;
            }
        | LPAREN expression RPAREN
            {
                    $$ = expressionFrom($2);
            }
        ;

literal
        : NUMBER
            {
                    $$ = wrapExpression(trx::ast::makeNumericLiteral($1));
            }
        | STRING
            {
                    auto expr = wrapExpression(trx::ast::makeStringLiteral($1 ? std::string($1) : std::string{}));
                    std::free($1);
                    $$ = expr;
            }
        | TRUE
            {
                    $$ = wrapExpression(trx::ast::makeBooleanLiteral(true));
            }
        | FALSE
            {
                    $$ = wrapExpression(trx::ast::makeBooleanLiteral(false));
            }
        | LBRACE object_properties RBRACE
            {
                    $$ = $2;
            }
        | LBRACKET array_elements RBRACKET
            {
                    $$ = $2;
            }
        ;

builtin
        : SQLCODE
            {
                    $$ = wrapExpression(trx::ast::makeBuiltin(trx::ast::BuiltinValue::SqlCode));
            }
        | DATE
            {
                    $$ = wrapExpression(trx::ast::makeBuiltin(trx::ast::BuiltinValue::Date));
            }
        | TIME
            {
                    $$ = wrapExpression(trx::ast::makeBuiltin(trx::ast::BuiltinValue::Time));
            }
        | WEEK
            {
                    $$ = wrapExpression(trx::ast::makeBuiltin(trx::ast::BuiltinValue::Week));
            }
        | WEEKDAY
            {
                    $$ = wrapExpression(trx::ast::makeBuiltin(trx::ast::BuiltinValue::WeekDay));
            }
        | TIMESTAMP
            {
                    $$ = wrapExpression(trx::ast::makeBuiltin(trx::ast::BuiltinValue::TimeStamp));
            }
        ;

object_properties
        : /* empty */
            {
                    $$ = wrapExpression(trx::ast::makeObjectLiteral({}));
            }
        | key ':' expression
            {
                    auto expr = expressionFrom($3);
                    std::unordered_map<std::string, trx::ast::ExpressionPtr> props;
                    props.emplace($1, std::move(*expr));
                    delete expr;
                    $$ = wrapExpression(trx::ast::makeObjectLiteral(std::move(props)));
                    std::free($1);
            }
        | object_properties COMMA key ':' expression
            {
                    // This is complex to modify existing object, so for now we'll create new ones
                    // In a real implementation, we'd need to modify the existing object
                    auto prevExpr = expressionFrom($1);
                    auto &prevObj = std::get<trx::ast::ObjectLiteralExpression>(prevExpr->get()->node);
                    auto valueExpr = expressionFrom($5);
                    prevObj.properties.emplace($3, std::move(*valueExpr));
                    delete valueExpr;
                    $$ = $1;
                    std::free($3);
            }
        ;

key
        : identifier
            {
                    $$ = $1;
            }
        | STRING
            {
                    // Remove quotes from the string
                    std::string str = $1 ? std::string($1) : std::string{};
                    if (!str.empty() && str.front() == '"' && str.back() == '"') {
                        str = str.substr(1, str.size() - 2);
                    }
                    $$ = strdup(str.c_str());
                    std::free($1);
            }
        ;

array_elements
        : /* empty */
            {
                    $$ = wrapExpression(trx::ast::makeArrayLiteral({}));
            }
        | expression
            {
                    auto expr = expressionFrom($1);
                    std::vector<trx::ast::ExpressionPtr> elements;
                    elements.push_back(std::move(*expr));
                    delete expr;
                    $$ = wrapExpression(trx::ast::makeArrayLiteral(std::move(elements)));
            }
        | array_elements COMMA expression
            {
                    auto prevExpr = expressionFrom($1);
                    auto &prevArr = std::get<trx::ast::ArrayLiteralExpression>(prevExpr->get()->node);
                    auto valueExpr = expressionFrom($3);
                    prevArr.elements.push_back(std::move(*valueExpr));
                    delete valueExpr;
                    $$ = $1;
            }
        ;

variable_reference
        : variable
            {
                    auto var = variableFrom($1);
                    auto expr = wrapExpression(trx::ast::makeVariable(std::move(*var)));
                    delete var;
                    $$ = expr;
            }
        ;

variable
        : identifier
            {
                    auto var = new trx::ast::VariableExpression();
                    trx::ast::VariableSegment segment{.identifier = $1 ? std::string($1) : std::string{}, .subscript = std::nullopt};
                    std::free($1);
                    var->path.push_back(std::move(segment));
                    $$ = var;
            }
        | PATH
            {
                    $$ = variableFromPath($1);
            }
        | variable '.' identifier
            {
                    auto var = variableFrom($1);
                    trx::ast::VariableSegment segment{.identifier = $3 ? std::string($3) : std::string{}, .subscript = std::nullopt};
                    std::free($3);
                    var->path.push_back(std::move(segment));
                    $$ = var;
            }
        | variable LBRACKET expression RBRACKET
            {
                    auto var = variableFrom($1);
                    // Modify the last segment to add subscript
                    if (!var->path.empty()) {
                        auto expr = expressionFrom($3);
                        var->path.back().subscript = std::move(*expr);
                        delete expr;
                    }
                    $$ = var;
            }
        ;

input_type
    : /* empty */
      {
          $$ = nullptr;
      }
    | type_name
      {
          $$ = $1;
      }

type_name
    : identifier
      {
          $$ = $1;
      }
    | LIST LPAREN type_name RPAREN
      {
          std::string listType = "LIST(";
          listType += $3;
          listType += ")";
          $$ = strdup(listType.c_str());
      }
    | _CHAR LPAREN NUMBER RPAREN
      {
          std::string type = "_CHAR(";
          type += std::to_string($3);
          type += ")";
          $$ = strdup(type.c_str());
      }
    | _CHAR
      {
          $$ = strdup("_CHAR");
      }
    | _INTEGER
      {
          $$ = strdup("_INTEGER");
      }
    | _SMALLINT
      {
          $$ = strdup("_SMALLINT");
      }
    | _DECIMAL
      {
          $$ = strdup("_DECIMAL");
      }
    | _BOOLEAN
      {
          $$ = strdup("_BOOLEAN");
      }
    | _FILE
      {
          $$ = strdup("_FILE");
      }
    | _BLOB
      {
          $$ = strdup("_BLOB");
      }
    | DATE
      {
          $$ = strdup("DATE");
      }
    | TIME
      {
          $$ = strdup("TIME");
      }
    | JSON
      {
          $$ = strdup("JSON");
      }

identifier
    : IDENT
      {
          $$ = $1;
      }
    ;

%%

void yyerror(YYLTYPE *loc, trx::parsing::ParserDriver &driver, void * /*scanner*/, const char *message) {
    driver.context().diagnosticEngine().report(
        trx::diagnostics::Diagnostic::Level::Error,
        message,
        trx::ast::SourceLocation{.file = driver.currentFile(), .line = static_cast<std::size_t>(loc->first_line), .column = static_cast<std::size_t>(loc->first_column)});
}
