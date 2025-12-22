#include "TestUtils.h"

#include <iostream>

namespace trx::test {

bool runExceptionHandlingTests() {
    std::cout << "Running exception handling tests...\n";

    // Test THROW statement parsing
    constexpr const char *throwSource = R"TRX(
        PROCEDURE test_throw() {
            THROW "error message";
        }
    )TRX";

    trx::parsing::ParserDriver throwDriver;
    if (!throwDriver.parseString(throwSource, "throw_test.trx")) {
        reportDiagnostics(throwDriver);
        return false;
    }

    const auto *throwProc = findProcedure(throwDriver.context().module(), "test_throw");
    if (!expect(throwProc != nullptr, "test_throw procedure not found")) {
        return false;
    }

    if (!expect(throwProc->body.size() == 1, "test_throw should have one statement")) {
        return false;
    }

    const auto *throwStmt = std::get_if<trx::ast::ThrowStatement>(&throwProc->body.front().node);
    if (!expect(throwStmt != nullptr, "test_throw statement is not THROW")) {
        return false;
    }

    // Test TRY/CATCH statement parsing
    constexpr const char *tryCatchSource = R"TRX(
        PROCEDURE test_try_catch() {
            TRY {
                THROW "test error";
            } CATCH (ex) {
                trace("caught");
            }
        }
    )TRX";

    trx::parsing::ParserDriver tryCatchDriver;
    if (!tryCatchDriver.parseString(tryCatchSource, "try_catch_test.trx")) {
        reportDiagnostics(tryCatchDriver);
        return false;
    }

    const auto *tryCatchProc = findProcedure(tryCatchDriver.context().module(), "test_try_catch");
    if (!expect(tryCatchProc != nullptr, "test_try_catch procedure not found")) {
        return false;
    }

    if (!expect(tryCatchProc->body.size() == 1, "test_try_catch should have one statement")) {
        return false;
    }

    const auto *tryCatchStmt = std::get_if<trx::ast::TryCatchStatement>(&tryCatchProc->body.front().node);
    if (!expect(tryCatchStmt != nullptr, "test_try_catch statement is not TRY/CATCH")) {
        return false;
    }

    if (!expect(tryCatchStmt->tryBlock.size() == 1, "try block should have one statement")) {
        return false;
    }

    if (!expect(tryCatchStmt->catchBlock.size() == 1, "catch block should have one statement")) {
        return false;
    }

    if (!expect(tryCatchStmt->exceptionVar.has_value(), "exception variable should be present")) {
        return false;
    }

    if (!expect(tryCatchStmt->exceptionVar->path.back().identifier == "ex", "exception variable name mismatch")) {
        return false;
    }

    // Test exception execution
    std::cout << "Testing exception execution...\n";
    trx::runtime::Interpreter interpreter(throwDriver.context().module(), nullptr);

    try {
        trx::runtime::JsonValue input = trx::runtime::JsonValue::object();
        interpreter.execute("test_throw", input);
        return expect(false, "THROW should have thrown an exception");
    } catch (const trx::runtime::TrxException &e) {
        if (!expect(e.getErrorType() == "ThrowException", "Exception type should be ThrowException")) {
            return false;
        }
        if (!expect(std::string(e.what()) == "Exception thrown by THROW statement", "Exception message mismatch")) {
            return false;
        }
    }

    // Test TRY/CATCH execution
    trx::runtime::Interpreter tryCatchInterpreter(tryCatchDriver.context().module(), nullptr);

    try {
        trx::runtime::JsonValue input = trx::runtime::JsonValue::object();
        auto resultOpt = tryCatchInterpreter.execute("test_try_catch", input);
        if (resultOpt) {
            return expect(false, "PROCEDURE should not return a value");
        }
        // Should succeed - exception was caught and procedure completed without returning
    } catch (const std::exception &e) {
        return expect(false, std::string("TRY/CATCH should not throw: ") + e.what());
    }

    // Test runtime exception (division by zero)
    constexpr const char *divisionSource = R"TRX(
        PROCEDURE test_division() {
            result := 10 / 0;
        }
    )TRX";

    trx::parsing::ParserDriver divisionDriver;
    if (!divisionDriver.parseString(divisionSource, "division_test.trx")) {
        reportDiagnostics(divisionDriver);
        return false;
    }

    trx::runtime::Interpreter divisionInterpreter(divisionDriver.context().module(), nullptr);

    try {
        trx::runtime::JsonValue input = trx::runtime::JsonValue::object();
        divisionInterpreter.execute("test_division", input);
        return expect(false, "Division by zero should throw an exception");
    } catch (const trx::runtime::TrxArithmeticException &e) {
        if (!expect(e.getErrorType() == "ArithmeticError", "Exception type should be ArithmeticError")) {
            return false;
        }
        if (!expect(std::string(e.what()) == "Division by zero", "Exception message mismatch")) {
            return false;
        }
    } catch (const std::exception &e) {
        return expect(false, std::string("Wrong exception type thrown: ") + typeid(e).name());
    }

    std::cout << "Exception handling tests passed.\n";
    return true;
}

} // namespace trx::test

int main() {
    if (!trx::test::runExceptionHandlingTests()) {
        std::cerr << "Exception handling tests failed.\n";
        return 1;
    }

    std::cout << "All tests passed!\n";
    return 0;
}