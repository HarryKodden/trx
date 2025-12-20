%{
#include "trx/parsing/ParserDriver.h"
#include "trx/parsing/ParserContext.h"
#include "trx/ast/Nodes.h"
#include "trx/ast/SourceLocation.h"

#include <cstddef>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
using FieldList = std::vector<trx::ast::RecordField>;
using StatementList = std::vector<trx::ast::Statement>;

inline FieldList *newFieldList() {
    return new FieldList();
}

inline FieldList *fieldListFrom(void *ptr) {
    return static_cast<FieldList *>(ptr);
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

inline trx::ast::VariableExpression *variableFrom(void *ptr) {
    return static_cast<trx::ast::VariableExpression *>(ptr);
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
} // namespace

struct YYLTYPE;
union YYSTYPE;

int yylex(YYSTYPE *yylval, YYLTYPE *yylloc, void *yyscanner);
void yyerror(YYLTYPE *loc, trx::parsing::ParserDriver &driver, void *scanner, const char *message);
%}

%code provides {
    static trx::ast::SourceLocation makeLocation(const trx::parsing::ParserDriver &driver, const YYLTYPE &loc) {
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

%token <text> IDENT STRING PATH
%token <number> NUMBER
%token INCLUDE CONSTANT PROCEDURE NULL_K RECORD
%token ASSIGN
%token _CHAR _INTEGER _SMALLINT _DECIMAL _BOOLEAN _FILE _BLOB
%token DATE TIME
%token LBRACE RBRACE LPAREN RPAREN COMMA SEMICOLON

%type <text> identifier
%type <text> parameter
%type <text> include_target
%type <ptr> fields field_def
%type <ptr> procedure_body statement_list statement assignment_statement variable expression variable_reference
%type <number> dimension

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
    | record_decl
    | procedure_decl
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

record_decl
    : RECORD identifier LBRACE fields RBRACE
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
    : identifier _CHAR LPAREN NUMBER RPAREN dimension
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = "CHAR";
          field.length = toLength($4);
          field.dimension = toDimension($6);
          list->push_back(std::move(field));
          std::free($1);
          $$ = list;
      }
    | identifier DATE dimension
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = "DATE";
          field.length = 8;
          field.dimension = toDimension($3);
          list->push_back(std::move(field));
          std::free($1);
          $$ = list;
      }
    | identifier TIME dimension
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = "TIME";
          field.length = 4;
          field.dimension = toDimension($3);
          list->push_back(std::move(field));
          std::free($1);
          $$ = list;
      }
    | identifier _INTEGER dimension
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = "INTEGER";
          field.length = 4;
          field.dimension = toDimension($3);
          list->push_back(std::move(field));
          std::free($1);
          $$ = list;
      }
    | identifier _SMALLINT dimension
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = "SMALLINT";
          field.length = 2;
          field.dimension = toDimension($3);
          list->push_back(std::move(field));
          std::free($1);
          $$ = list;
      }
    | identifier _BOOLEAN dimension
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = "BOOLEAN";
          field.length = 1;
          field.dimension = toDimension($3);
          list->push_back(std::move(field));
          std::free($1);
          $$ = list;
      }
    | identifier _DECIMAL LPAREN NUMBER COMMA NUMBER RPAREN dimension
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = "DECIMAL";
          field.length = toLength($4);
          field.dimension = toDimension($8);
          field.scale = static_cast<short>(toLength($6));
          list->push_back(std::move(field));
          std::free($1);
          $$ = list;
      }
    | identifier _BLOB dimension
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = "BLOB";
          field.length = 256;
          field.dimension = toDimension($3);
          list->push_back(std::move(field));
          std::free($1);
          $$ = list;
      }
    | identifier _FILE dimension
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = "FILE";
          field.length = 256;
          field.dimension = toDimension($3);
          list->push_back(std::move(field));
          std::free($1);
          $$ = list;
      }
    | identifier identifier dimension
      {
          auto list = newFieldList();
          trx::ast::RecordField field;
          field.name = {.name = $1 ? std::string($1) : std::string{}, .location = makeLocation(driver, @1)};
          field.typeName = $2 ? std::string($2) : std::string{};
          field.length = 0;
          field.dimension = toDimension($3);
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

procedure_decl
    : PROCEDURE identifier LPAREN parameter COMMA parameter RPAREN procedure_body
      {
          trx::ast::ProcedureDecl procedure;
          procedure.name = {.name = $2 ? std::string($2) : std::string{}, .location = makeLocation(driver, @2)};

          if ($4) {
              procedure.input = driver.context().makeParameter(std::string($4), makeLocation(driver, @4));
              std::free($4);
          }

          if ($6) {
              procedure.output = driver.context().makeParameter(std::string($6), makeLocation(driver, @6));
              std::free($6);
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

procedure_body
    : LBRACE RBRACE
      {
          $$ = newStatementList();
      }
    | LBRACE statement_list RBRACE
      {
          $$ = statementListFrom($2);
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

expression
    : variable_reference
      {
          $$ = $1;
      }
    ;

variable_reference
    : variable
      {
          auto var = variableFrom($1);
          auto expr = new trx::ast::ExpressionPtr(trx::ast::makeVariable(std::move(*var)));
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
    ;

parameter
    : NULL_K
      {
          $$ = nullptr;
      }
    | identifier
      {
          $$ = $1;
      }
    ;

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
