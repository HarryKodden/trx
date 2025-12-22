#include "TestUtils.h"

#include <iostream>

namespace trx::test {

bool runCallStatementTests() {
    constexpr const char *source = R"TRX(
        TYPE SAMPLE {
            VALUE INTEGER;
            RESULT INTEGER;
        }

        FUNCTION inner(SAMPLE): SAMPLE {
        }

        FUNCTION outer(SAMPLE): SAMPLE {
            output := CALL inner(input);
        }

        FUNCTION outer_no_arg(SAMPLE): SAMPLE {
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

} // namespace trx::test

int main() {
    if (!trx::test::runCallStatementTests()) {
        std::cerr << "Call statement tests failed.\n";
        return 1;
    }

    std::cout << "All tests passed!\n";
    return 0;
}