#include "trx/ast/Nodes.h"
#include "trx/ast/Statements.h"
#include "trx/parsing/ParserDriver.h"
#include "trx/runtime/Interpreter.h"

#include <cstddef>
#include <iostream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <variant>

namespace {

void reportDiagnostics(const trx::parsing::ParserDriver &driver) {
    std::cerr << "Parsing failed:\n";
    for (const auto &diagnostic : driver.diagnostics().messages()) {
        std::cerr << "  - " << diagnostic.message << "\n";
    }
}

bool expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

const trx::ast::ProcedureDecl *findProcedure(const trx::ast::Module &module, std::string_view name) {
    for (const auto &declaration : module.declarations) {
        if (const auto *procedure = std::get_if<trx::ast::ProcedureDecl>(&declaration)) {
            if (procedure->name.name == name) {
                return procedure;
            }
        }
    }
    return nullptr;
}

const trx::ast::RecordDecl *findRecord(const trx::ast::Module &module, std::string_view name) {
    for (const auto &declaration : module.declarations) {
        if (const auto *record = std::get_if<trx::ast::RecordDecl>(&declaration)) {
            if (record->name.name == name) {
                return record;
            }
        }
    }
    return nullptr;
}

bool expectVariablePath(const trx::ast::VariableExpression &variable, std::initializer_list<std::string_view> segments) {
    if (!expect(variable.path.size() == segments.size(), "variable path length mismatch")) {
        return false;
    }

    std::size_t index = 0;
    for (const auto expected : segments) {
        if (!expect(variable.path[index].identifier == expected, "variable path segment mismatch")) {
            return false;
        }
        ++index;
    }

    return true;
}

bool runCopyProcedureTest() {
    constexpr const char *source = R"TRX(
        RECORD ADDRESS {
            STREET CHAR(64);
            ZIP INTEGER;
        }

        RECORD CUSTOMER {
            NAME CHAR(64);
            HOME ADDRESS;
        }

        PROCEDURE copy_customer(CUSTOMER, CUSTOMER) {
            output := input;
        }
    )TRX";

    trx::parsing::ParserDriver driver;
    if (!driver.parseString(source, "copy_customer.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    trx::runtime::Interpreter interpreter(driver.context().module());

    trx::runtime::JsonValue::Object home;
    home.emplace("STREET", trx::runtime::JsonValue("Main Street"));
    home.emplace("ZIP", trx::runtime::JsonValue(12345));

    trx::runtime::JsonValue::Object customer;
    customer.emplace("NAME", trx::runtime::JsonValue("Alice"));
    customer.emplace("HOME", trx::runtime::JsonValue(std::move(home)));

    trx::runtime::JsonValue input{std::move(customer)};

    const auto output = interpreter.execute("copy_customer", input);

    if (output != input) {
        std::cerr << "Output JSON did not match input JSON\n";
        return false;
    }

    return true;
}

bool validateNumericExpression(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(!procedure.body.empty(), "numeric_case missing body")) {
        return false;
    }

    const auto *assignment = std::get_if<trx::ast::AssignmentStatement>(&procedure.body.front().node);
    if (!expect(assignment != nullptr, "numeric_case first statement is not assignment")) {
        return false;
    }

    const auto *addExpr = std::get_if<trx::ast::BinaryExpression>(&assignment->value->node);
    if (!expect(addExpr != nullptr, "numeric_case expression is not binary add") ||
        !expect(addExpr->op == trx::ast::BinaryOperator::Add, "numeric_case root operator is not Add")) {
        return false;
    }

    const auto *multiplyExpr = std::get_if<trx::ast::BinaryExpression>(&addExpr->lhs->node);
    if (!expect(multiplyExpr != nullptr, "numeric_case left operand is not binary multiply") ||
        !expect(multiplyExpr->op == trx::ast::BinaryOperator::Multiply, "numeric_case left operand is not Multiply")) {
        return false;
    }

    const auto *leftVar = std::get_if<trx::ast::VariableExpression>(&multiplyExpr->lhs->node);
    if (!expect(leftVar != nullptr, "numeric_case lhs is not variable") ||
        !expect(leftVar->path.size() == 2, "numeric_case variable path not two segments") ||
        !expect(leftVar->path[0].identifier == "input", "numeric_case variable missing input segment") ||
        !expect(leftVar->path[1].identifier == "VALUE", "numeric_case variable missing VALUE segment")) {
        return false;
    }

    const auto *twoLiteral = std::get_if<trx::ast::LiteralExpression>(&multiplyExpr->rhs->node);
    if (!expect(twoLiteral != nullptr, "numeric_case multiplier is not literal")) {
        return false;
    }
    const auto *twoValue = std::get_if<double>(&twoLiteral->value);
    if (!expect(twoValue != nullptr && *twoValue == 2.0, "numeric_case multiplier literal not 2")) {
        return false;
    }

    const auto *addLiteral = std::get_if<trx::ast::LiteralExpression>(&addExpr->rhs->node);
    if (!expect(addLiteral != nullptr, "numeric_case addend is not literal")) {
        return false;
    }
    const auto *addValue = std::get_if<double>(&addLiteral->value);
    if (!expect(addValue != nullptr && *addValue == 5.0, "numeric_case add literal not 5")) {
        return false;
    }

    return true;
}

bool validateBooleanExpression(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(!procedure.body.empty(), "boolean_case missing body")) {
        return false;
    }

    const auto *assignment = std::get_if<trx::ast::AssignmentStatement>(&procedure.body.front().node);
    if (!expect(assignment != nullptr, "boolean_case first statement is not assignment")) {
        return false;
    }

    const auto *andExpr = std::get_if<trx::ast::BinaryExpression>(&assignment->value->node);
    if (!expect(andExpr != nullptr, "boolean_case expression is not binary") ||
        !expect(andExpr->op == trx::ast::BinaryOperator::And, "boolean_case root operator is not And")) {
        return false;
    }

    const auto *greaterExpr = std::get_if<trx::ast::BinaryExpression>(&andExpr->lhs->node);
    if (!expect(greaterExpr != nullptr, "boolean_case left operand is not comparison") ||
        !expect(greaterExpr->op == trx::ast::BinaryOperator::Greater, "boolean_case left operand is not Greater")) {
        return false;
    }

    const auto *leftVar = std::get_if<trx::ast::VariableExpression>(&greaterExpr->lhs->node);
    if (!expect(leftVar != nullptr, "boolean_case comparison lhs is not variable") ||
        !expect(leftVar->path.size() == 2, "boolean_case lhs variable path not two segments") ||
        !expect(leftVar->path[0].identifier == "input", "boolean_case lhs variable missing input segment") ||
        !expect(leftVar->path[1].identifier == "VALUE", "boolean_case lhs variable missing VALUE segment")) {
        return false;
    }

    const auto *tenLiteral = std::get_if<trx::ast::LiteralExpression>(&greaterExpr->rhs->node);
    const auto *tenValue = tenLiteral ? std::get_if<double>(&tenLiteral->value) : nullptr;
    if (!expect(tenLiteral != nullptr && tenValue != nullptr && *tenValue == 10.0, "boolean_case comparison rhs not 10")) {
        return false;
    }

    const auto *notExpr = std::get_if<trx::ast::UnaryExpression>(&andExpr->rhs->node);
    if (!expect(notExpr != nullptr, "boolean_case right operand is not unary") ||
        !expect(notExpr->op == trx::ast::UnaryOperator::Not, "boolean_case unary operator is not Not")) {
        return false;
    }

    const auto *equalExpr = std::get_if<trx::ast::BinaryExpression>(&notExpr->operand->node);
    if (!expect(equalExpr != nullptr, "boolean_case NOT operand is not comparison") ||
        !expect(equalExpr->op == trx::ast::BinaryOperator::Equal, "boolean_case NOT operand is not Equal")) {
        return false;
    }

    const auto *equalVar = std::get_if<trx::ast::VariableExpression>(&equalExpr->lhs->node);
    if (!expect(equalVar != nullptr, "boolean_case equality lhs is not variable") ||
        !expect(equalVar->path.size() == 2, "boolean_case equality variable path not two segments") ||
        !expect(equalVar->path[0].identifier == "input", "boolean_case equality variable missing input segment") ||
        !expect(equalVar->path[1].identifier == "VALUE", "boolean_case equality variable missing VALUE segment")) {
        return false;
    }

    const auto *zeroLiteral = std::get_if<trx::ast::LiteralExpression>(&equalExpr->rhs->node);
    const auto *zeroValue = zeroLiteral ? std::get_if<double>(&zeroLiteral->value) : nullptr;
    if (!expect(zeroLiteral != nullptr && zeroValue != nullptr && *zeroValue == 0.0, "boolean_case equality rhs not 0")) {
        return false;
    }

    return true;
}

bool validateBooleanLiteralExpression(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(!procedure.body.empty(), "bool_literal_case missing body")) {
        return false;
    }

    const auto *assignment = std::get_if<trx::ast::AssignmentStatement>(&procedure.body.front().node);
    if (!expect(assignment != nullptr, "bool_literal_case first statement is not assignment")) {
        return false;
    }

    const auto *orExpr = std::get_if<trx::ast::BinaryExpression>(&assignment->value->node);
    if (!expect(orExpr != nullptr, "bool_literal_case expression is not binary") ||
        !expect(orExpr->op == trx::ast::BinaryOperator::Or, "bool_literal_case root operator is not Or")) {
        return false;
    }

    const auto *leftLiteral = std::get_if<trx::ast::LiteralExpression>(&orExpr->lhs->node);
    const auto *leftValue = leftLiteral ? std::get_if<bool>(&leftLiteral->value) : nullptr;
    if (!expect(leftLiteral != nullptr && leftValue != nullptr && *leftValue, "bool_literal_case left literal not TRUE")) {
        return false;
    }

    const auto *rightLiteral = std::get_if<trx::ast::LiteralExpression>(&orExpr->rhs->node);
    const auto *rightValue = rightLiteral ? std::get_if<bool>(&rightLiteral->value) : nullptr;
    if (!expect(rightLiteral != nullptr && rightValue != nullptr && !*rightValue, "bool_literal_case right literal not FALSE")) {
        return false;
    }

    return true;
}

bool validateStringLiteralExpression(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(!procedure.body.empty(), "text_case missing body")) {
        return false;
    }

    const auto *assignment = std::get_if<trx::ast::AssignmentStatement>(&procedure.body.front().node);
    if (!expect(assignment != nullptr, "text_case first statement is not assignment")) {
        return false;
    }

    const auto *literal = std::get_if<trx::ast::LiteralExpression>(&assignment->value->node);
    if (!expect(literal != nullptr, "text_case expression is not literal")) {
        return false;
    }

    const auto *value = std::get_if<std::string>(&literal->value);
    if (!expect(value != nullptr && *value == "constant", "text_case literal value mismatch")) {
        return false;
    }

    return true;
}

bool runExpressionAstTests() {
    constexpr const char *source = R"TRX(
        RECORD SAMPLE {
            VALUE INTEGER;
            RESULT INTEGER;
            FLAG BOOLEAN;
            TEXT CHAR(32);
        }

        PROCEDURE numeric_case(SAMPLE, SAMPLE) {
            output.RESULT := input.VALUE * 2 + 5;
        }

        PROCEDURE boolean_case(SAMPLE, SAMPLE) {
            output.FLAG := input.VALUE > 10 AND NOT (input.VALUE = 0);
        }

        PROCEDURE bool_literal_case(SAMPLE, SAMPLE) {
            output.FLAG := TRUE OR FALSE;
        }

        PROCEDURE text_case(SAMPLE, SAMPLE) {
            output.TEXT := "constant";
        }
    )TRX";

    trx::parsing::ParserDriver driver;
    if (!driver.parseString(source, "expression_cases.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    const auto *numeric = findProcedure(driver.context().module(), "numeric_case");
    if (!expect(numeric != nullptr, "numeric_case procedure not found") ||
        !validateNumericExpression(*numeric)) {
        return false;
    }

    const auto *booleanProc = findProcedure(driver.context().module(), "boolean_case");
    if (!expect(booleanProc != nullptr, "boolean_case procedure not found") ||
        !validateBooleanExpression(*booleanProc)) {
        return false;
    }

    const auto *booleanLiteralProc = findProcedure(driver.context().module(), "bool_literal_case");
    if (!expect(booleanLiteralProc != nullptr, "bool_literal_case procedure not found") ||
        !validateBooleanLiteralExpression(*booleanLiteralProc)) {
        return false;
    }

    const auto *textProc = findProcedure(driver.context().module(), "text_case");
    if (!expect(textProc != nullptr, "text_case procedure not found") ||
        !validateStringLiteralExpression(*textProc)) {
        return false;
    }

    return true;
}

bool validateIfProcedure(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(!procedure.body.empty(), "branching procedure has no statements")) {
        return false;
    }

    const auto *ifStmt = std::get_if<trx::ast::IfStatement>(&procedure.body.front().node);
    if (!expect(ifStmt != nullptr, "branching first statement is not IF")) {
        return false;
    }

    const auto *condition = std::get_if<trx::ast::BinaryExpression>(&ifStmt->condition->node);
    if (!expect(condition != nullptr, "branching condition is not binary") ||
        !expect(condition->op == trx::ast::BinaryOperator::Greater, "branching condition operator not Greater")) {
        return false;
    }

    if (!expect(condition->lhs != nullptr, "branching condition lhs missing")) {
        return false;
    }

    const auto *lhsVar = std::get_if<trx::ast::VariableExpression>(&condition->lhs->node);
    if (!expect(lhsVar != nullptr, "branching lhs not variable") ||
        !expect(lhsVar->path.size() == 2, "branching lhs variable path length")) {
        return false;
    }

    const auto *rhsLiteral = std::get_if<trx::ast::LiteralExpression>(&condition->rhs->node);
    const auto *rhsValue = rhsLiteral ? std::get_if<double>(&rhsLiteral->value) : nullptr;
    if (!expect(rhsLiteral != nullptr && rhsValue != nullptr && *rhsValue == 0.0, "branching rhs literal not 0")) {
        return false;
    }

    if (!expect(ifStmt->thenBranch.size() == 1, "branching then branch size incorrect") ||
        !expect(ifStmt->elseBranch.size() == 1, "branching else branch size incorrect")) {
        return false;
    }

    const auto *thenAssignment = std::get_if<trx::ast::AssignmentStatement>(&ifStmt->thenBranch.front().node);
    if (!expect(thenAssignment != nullptr, "branching then statement not assignment")) {
        return false;
    }

    const auto *elseAssignment = std::get_if<trx::ast::AssignmentStatement>(&ifStmt->elseBranch.front().node);
    if (!expect(elseAssignment != nullptr, "branching else statement not assignment")) {
        return false;
    }

    return true;
}

bool validateWhileProcedure(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(!procedure.body.empty(), "looping procedure has no statements")) {
        return false;
    }

    const auto *whileStmt = std::get_if<trx::ast::WhileStatement>(&procedure.body.front().node);
    if (!expect(whileStmt != nullptr, "looping first statement is not WHILE")) {
        return false;
    }

    const auto *condition = std::get_if<trx::ast::BinaryExpression>(&whileStmt->condition->node);
    if (!expect(condition != nullptr, "looping condition is not binary")) {
        return false;
    }

    if (!expect(whileStmt->body.size() == 1, "looping body size incorrect")) {
        return false;
    }

    const auto *assignment = std::get_if<trx::ast::AssignmentStatement>(&whileStmt->body.front().node);
    if (!expect(assignment != nullptr, "looping body statement not assignment")) {
        return false;
    }

    return true;
}

bool validateSwitchProcedure(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(!procedure.body.empty(), "switching procedure has no statements")) {
        return false;
    }

    const auto *switchStmt = std::get_if<trx::ast::SwitchStatement>(&procedure.body.front().node);
    if (!expect(switchStmt != nullptr, "switching first statement is not SWITCH")) {
        return false;
    }

    if (!expect(switchStmt->cases.size() == 2, "switching cases size incorrect")) {
        return false;
    }

    for (std::size_t index = 0; index < switchStmt->cases.size(); ++index) {
        const auto &caseClause = switchStmt->cases[index];
        if (!expect(caseClause.match != nullptr, "switching case match missing")) {
            return false;
        }
        const auto *literal = std::get_if<trx::ast::LiteralExpression>(&caseClause.match->node);
        const auto *value = literal ? std::get_if<double>(&literal->value) : nullptr;
        if (!expect(literal != nullptr && value != nullptr && *value == static_cast<double>(index), "switching case literal mismatch")) {
            return false;
        }
        if (!expect(caseClause.body.size() == 1, "switching case body size incorrect")) {
            return false;
        }
        const auto *assignment = std::get_if<trx::ast::AssignmentStatement>(&caseClause.body.front().node);
        if (!expect(assignment != nullptr, "switching case body not assignment")) {
            return false;
        }
    }

    if (!expect(switchStmt->defaultBranch.has_value(), "switching default branch missing")) {
        return false;
    }

    if (!expect(switchStmt->defaultBranch->size() == 1, "switching default body size incorrect")) {
        return false;
    }

    const auto *defaultAssignment = std::get_if<trx::ast::AssignmentStatement>(&switchStmt->defaultBranch->front().node);
    if (!expect(defaultAssignment != nullptr, "switching default statement not assignment")) {
        return false;
    }

    return true;
}

bool runControlStatementTests() {
    constexpr const char *source = R"TRX(
        RECORD SAMPLE {
            VALUE INTEGER;
            RESULT INTEGER;
        }

        PROCEDURE branching(SAMPLE, SAMPLE) {
            IF input.VALUE > 0 THEN {
                output.RESULT := input.VALUE;
            } ELSE {
                output.RESULT := 0;
            }
        }

        PROCEDURE looping(SAMPLE, SAMPLE) {
            WHILE input.VALUE > 0 {
                output.RESULT := output.RESULT + 1;
            }
        }

        PROCEDURE switching(SAMPLE, SAMPLE) {
            SWITCH input.VALUE {
                CASE 0 {
                    output.RESULT := 0;
                }
                CASE 1 {
                    output.RESULT := 1;
                }
                DEFAULT {
                    output.RESULT := -1;
                }
            }
        }
    )TRX";

    trx::parsing::ParserDriver driver;
    if (!driver.parseString(source, "control_cases.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    const auto *branching = findProcedure(driver.context().module(), "branching");
    if (!expect(branching != nullptr, "branching procedure not found") ||
        !validateIfProcedure(*branching)) {
        return false;
    }

    const auto *looping = findProcedure(driver.context().module(), "looping");
    if (!expect(looping != nullptr, "looping procedure not found") ||
        !validateWhileProcedure(*looping)) {
        return false;
    }

    const auto *switching = findProcedure(driver.context().module(), "switching");
    if (!expect(switching != nullptr, "switching procedure not found") ||
        !validateSwitchProcedure(*switching)) {
        return false;
    }

    return true;
}

bool validateSqlProcedure(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(procedure.body.size() == 3, "sql procedure does not contain three statements")) {
        return false;
    }

    const auto getSql = [&](std::size_t index) -> const trx::ast::SqlStatement * {
        const auto *sql = std::get_if<trx::ast::SqlStatement>(&procedure.body[index].node);
        if (!expect(sql != nullptr, "statement is not SQL")) {
            return nullptr;
        }
        return sql;
    };

    const auto *selectStmt = getSql(0);
    if (!selectStmt) {
        return false;
    }
    if (!expect(selectStmt->sql == "SELECT NAME FROM CUSTOMERS WHERE ID = ?", "unexpected SELECT SQL text") ||
        !expect(selectStmt->hostVariables.size() == 1, "SELECT host variable count mismatch") ||
        !expectVariablePath(selectStmt->hostVariables.front(), {"input", "VALUE"})) {
        return false;
    }

    const auto *deleteStmt = getSql(1);
    if (!deleteStmt) {
        return false;
    }
    if (!expect(deleteStmt->sql == "DELETE FROM CUSTOMERS WHERE ID = ?", "unexpected DELETE SQL text") ||
        !expect(deleteStmt->hostVariables.size() == 1, "DELETE host variable count mismatch") ||
        !expectVariablePath(deleteStmt->hostVariables.front(), {"input", "VALUE"})) {
        return false;
    }

    const auto *updateStmt = getSql(2);
    if (!updateStmt) {
        return false;
    }
    if (!expect(updateStmt->sql == "UPDATE CUSTOMERS SET NAME = ? WHERE ID = ?", "unexpected UPDATE SQL text") ||
        !expect(updateStmt->hostVariables.size() == 2, "UPDATE host variable count mismatch")) {
        return false;
    }
    if (!expectVariablePath(updateStmt->hostVariables[0], {"input", "NAME"}) ||
        !expectVariablePath(updateStmt->hostVariables[1], {"input", "VALUE"})) {
        return false;
    }

    return true;
}

bool validateCursorProcedure(const trx::ast::ProcedureDecl &procedure) {
    if (!expect(procedure.body.size() == 4, "cursor procedure does not contain four statements")) {
        return false;
    }

    const auto getSql = [&](std::size_t index) -> const trx::ast::SqlStatement * {
        const auto *sql = std::get_if<trx::ast::SqlStatement>(&procedure.body[index].node);
        if (!expect(sql != nullptr, "statement is not SQL")) {
            return nullptr;
        }
        return sql;
    };

    const auto *declareStmt = getSql(0);
    if (!declareStmt) {
        return false;
    }
    if (!expect(declareStmt->kind == trx::ast::SqlStatementKind::DeclareCursor, "DECLARE statement kind mismatch") ||
        !expect(declareStmt->identifier == "mycursor", "DECLARE cursor name mismatch") ||
        !expect(declareStmt->sql == "DECLARE mycursor CURSOR FOR SELECT NAME, VALUE FROM CUSTOMERS WHERE ID = ?", "DECLARE SQL text mismatch") ||
        !expect(declareStmt->hostVariables.size() == 1, "DECLARE host variable count mismatch") ||
        !expectVariablePath(declareStmt->hostVariables.front(), {"input", "VALUE"})) {
        return false;
    }

    const auto *openStmt = getSql(1);
    if (!openStmt) {
        return false;
    }
    if (!expect(openStmt->kind == trx::ast::SqlStatementKind::OpenCursor, "OPEN statement kind mismatch") ||
        !expect(openStmt->identifier == "mycursor", "OPEN cursor name mismatch") ||
        !expect(openStmt->sql == "OPEN mycursor", "OPEN SQL text mismatch") ||
        !expect(openStmt->hostVariables.empty(), "OPEN should not have host variables")) {
        return false;
    }

    const auto *fetchStmt = getSql(2);
    if (!fetchStmt) {
        return false;
    }
    if (!expect(fetchStmt->kind == trx::ast::SqlStatementKind::FetchCursor, "FETCH statement kind mismatch") ||
        !expect(fetchStmt->identifier == "mycursor", "FETCH cursor name mismatch") ||
        !expect(fetchStmt->sql == "FETCH mycursor INTO ?, ?", "FETCH SQL text mismatch") ||
        !expect(fetchStmt->hostVariables.size() == 2, "FETCH host variable count mismatch") ||
        !expectVariablePath(fetchStmt->hostVariables[0], {"output", "NAME"}) ||
        !expectVariablePath(fetchStmt->hostVariables[1], {"output", "RESULT"})) {
        return false;
    }

    const auto *closeStmt = getSql(3);
    if (!closeStmt) {
        return false;
    }
    if (!expect(closeStmt->kind == trx::ast::SqlStatementKind::CloseCursor, "CLOSE statement kind mismatch") ||
        !expect(closeStmt->identifier == "mycursor", "CLOSE cursor name mismatch") ||
        !expect(closeStmt->sql == "CLOSE mycursor", "CLOSE SQL text mismatch") ||
        !expect(closeStmt->hostVariables.empty(), "CLOSE should not have host variables")) {
        return false;
    }

    return true;
}

bool runSqlStatementTests() {
    constexpr const char *source = R"TRX(
        RECORD SAMPLE {
            VALUE INTEGER;
            NAME CHAR(64);
            RESULT INTEGER;
        }

        PROCEDURE sql_examples(SAMPLE, SAMPLE) {
            EXEC SQL SELECT NAME FROM CUSTOMERS WHERE ID = :input.VALUE;
            EXEC SQL DELETE FROM CUSTOMERS WHERE ID = :input.VALUE;
            EXEC SQL UPDATE CUSTOMERS SET NAME = :input.NAME WHERE ID = :input.VALUE;
        }

        PROCEDURE cursor_examples(SAMPLE, SAMPLE) {
            EXEC SQL DECLARE mycursor CURSOR FOR SELECT NAME, VALUE FROM CUSTOMERS WHERE ID = :input.VALUE;
            EXEC SQL OPEN mycursor;
            EXEC SQL FETCH mycursor INTO :output.NAME, :output.RESULT;
            EXEC SQL CLOSE mycursor;
        }
    )TRX";

    trx::parsing::ParserDriver driver;
    if (!driver.parseString(source, "sql_examples.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    const auto *procedure = findProcedure(driver.context().module(), "sql_examples");
    if (!expect(procedure != nullptr, "sql_examples procedure not found")) {
        return false;
    }

    if (!validateSqlProcedure(*procedure)) {
        return false;
    }

    const auto *cursorProcedure = findProcedure(driver.context().module(), "cursor_examples");
    if (!expect(cursorProcedure != nullptr, "cursor_examples procedure not found")) {
        return false;
    }

    return validateCursorProcedure(*cursorProcedure);
}

bool runCallStatementTests() {
    constexpr const char *source = R"TRX(
        RECORD SAMPLE {
            VALUE INTEGER;
            RESULT INTEGER;
        }

        PROCEDURE inner(SAMPLE, SAMPLE) {
        }

        PROCEDURE outer(SAMPLE, SAMPLE) {
            output := CALL inner(input);
        }

        PROCEDURE outer_no_arg(SAMPLE, SAMPLE) {
            output := CALL inner(NULL);
        }
    )TRX";

    trx::parsing::ParserDriver driver;
    if (!driver.parseString(source, "call_examples.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    const auto *outer = findProcedure(driver.context().module(), "outer");
    if (!expect(outer != nullptr, "outer procedure not found")) {
        return false;
    }

    if (!expect(outer->body.size() == 1, "outer procedure should contain one statement")) {
        return false;
    }

    const auto *callStmt = std::get_if<trx::ast::CallStatement>(&outer->body.front().node);
    if (!expect(callStmt != nullptr, "outer body statement is not CALL")) {
        return false;
    }

    if (!expect(callStmt->name == "inner", "CALL name mismatch")) {
        return false;
    }

    if (!expect(callStmt->output.has_value(), "CALL output missing")) {
        return false;
    }
    if (!expectVariablePath(*callStmt->output, {"output"})) {
        return false;
    }

    if (!expect(callStmt->input.has_value(), "CALL input missing")) {
        return false;
    }
    if (!expectVariablePath(*callStmt->input, {"input"})) {
        return false;
    }

    const auto *outerNoArg = findProcedure(driver.context().module(), "outer_no_arg");
    if (!expect(outerNoArg != nullptr, "outer_no_arg procedure not found")) {
        return false;
    }
    if (!expect(outerNoArg->body.size() == 1, "outer_no_arg procedure should contain one statement")) {
        return false;
    }

    const auto *callStmtNull = std::get_if<trx::ast::CallStatement>(&outerNoArg->body.front().node);
    if (!expect(callStmtNull != nullptr, "outer_no_arg statement is not CALL")) {
        return false;
    }

    if (!expect(!callStmtNull->input.has_value(), "CALL with NULL input should not have value")) {
        return false;
    }
    if (!expect(callStmtNull->output.has_value(), "CALL output missing for NULL case")) {
        return false;
    }
    if (!expectVariablePath(*callStmtNull->output, {"output"})) {
        return false;
    }

    return true;
}

bool runRecordFormatTests() {
    constexpr const char *source = R"TRX(
        RECORD FORMATTEST {
            CUSTOMER_ID INTEGER;
            FULL_NAME CHAR(64) json:"fullName,omitempty";
            STATUS_FLAG CHAR(16) json:"status_flag";
        }
    )TRX";

    trx::parsing::ParserDriver driver;
    if (!driver.parseString(source, "record_format.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    const auto *record = findRecord(driver.context().module(), "FORMATTEST");
    if (!expect(record != nullptr, "FORMATTEST record not found")) {
        return false;
    }

    if (!expect(record->fields.size() == 3, "FORMATTEST field count mismatch")) {
        return false;
    }

    const auto &idField = record->fields[0];
    if (!expect(idField.jsonName == "customer_id", "CUSTOMER_ID default json name mismatch") ||
        !expect(!idField.jsonOmitEmpty, "CUSTOMER_ID default omitEmpty should be false") ||
        !expect(!idField.hasExplicitJsonName, "CUSTOMER_ID should not be marked explicit")) {
        return false;
    }

    const auto &nameField = record->fields[1];
    if (!expect(nameField.jsonName == "fullName", "FULL_NAME json name mismatch") ||
        !expect(nameField.jsonOmitEmpty, "FULL_NAME omitempty not detected") ||
        !expect(nameField.hasExplicitJsonName, "FULL_NAME should be marked explicit")) {
        return false;
    }

    const auto &statusField = record->fields[2];
    if (!expect(statusField.jsonName == "status_flag", "STATUS_FLAG json name mismatch") ||
        !expect(!statusField.jsonOmitEmpty, "STATUS_FLAG omitempty should be false") ||
        !expect(statusField.hasExplicitJsonName, "STATUS_FLAG should be marked explicit")) {
        return false;
    }

    return true;
}

} // namespace

int main() {
    if (!runCopyProcedureTest()) {
        return 1;
    }

    if (!runExpressionAstTests()) {
        return 1;
    }

    if (!runControlStatementTests()) {
        return 1;
    }

    if (!runSqlStatementTests()) {
        return 1;
    }

    if (!runCallStatementTests()) {
        return 1;
    }

    if (!runRecordFormatTests()) {
        return 1;
    }

    return 0;
}
