#include "TestUtils.h"

#include <iostream>

namespace trx::test {

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
        TYPE SAMPLE {
            VALUE INTEGER;
            NAME CHAR(64);
            RESULT INTEGER;
        }

        FUNCTION sql_examples(SAMPLE): SAMPLE {
            EXEC SQL SELECT NAME FROM CUSTOMERS WHERE ID = :input.VALUE;
            EXEC SQL DELETE FROM CUSTOMERS WHERE ID = :input.VALUE;
            EXEC SQL UPDATE CUSTOMERS SET NAME = :input.NAME WHERE ID = :input.VALUE;
        }

        FUNCTION cursor_examples(SAMPLE): SAMPLE {
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

} // namespace trx::test

int main() {
    if (!trx::test::runSqlStatementTests()) {
        std::cerr << "SQL statement tests failed.\n";
        return 1;
    }

    std::cout << "All tests passed!\n";
    return 0;
}