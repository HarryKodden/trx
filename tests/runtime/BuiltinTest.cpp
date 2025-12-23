#include "TestUtils.h"

#include <iostream>

namespace trx::test {

bool runBuiltinTest() {
    std::cout << "Running builtin test...\n";
    constexpr const char *source = R"TRX(
        PROCEDURE test_builtins() {
            // Test SqlCode - should be 0 initially
            var sqlcode_val DECIMAL := sqlcode;
            trace('sqlcode=' + sqlcode_val);

            // Test Date - should return current date as string
            var date_val CHAR(20) := date;
            trace('date=' + date_val);

            // Test Time - should return current time as string
            var time_val CHAR(20) := time;
            trace('time=' + time_val);

            // Test Stamp - should return timestamp
            var stamp_val DECIMAL := timestamp;
            trace('stamp=' + stamp_val);

            // Test Week - should return week number
            var week_val INTEGER := week;
            trace('week=' + week_val);

            // Test WeekDay - should return day of week
            var weekday_val INTEGER := weekday;
            trace('weekday=' + weekday_val);

            output := 1; // Success indicator
        }
    )TRX";

    std::cout << "Source code to parse:\n" << source << "\n";

    trx::parsing::ParserDriver driver;
    std::cout << "Parsing source...\n";
    if (!driver.parseString(source, "builtin_test.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    std::cout << "Parsing successful. Executing procedure...\n";
    trx::runtime::Interpreter interpreter(driver.context().module(), nullptr); // Use default SQLite driver

    trx::runtime::JsonValue input{trx::runtime::JsonValue::Object{}};

    std::cout << "Executing procedure...\n";
    const auto outputOpt = interpreter.execute("test_builtins", input);
    if (!outputOpt) {
        std::cerr << "Procedure did not return a value\n";
        return false;
    }
    const auto &output = *outputOpt;

    // Check that we got success indicator
    if (const auto* success = std::get_if<double>(&output.data)) {
        if (*success != 1.0) {
            std::cerr << "Expected success (1), got " << *success << "\n";
            return false;
        }
    } else {
        std::cerr << "Output is not a number\n";
        return false;
    }

    std::cout << "Builtin test passed.\n";
    return true;
}

} // namespace trx::test

int main() {
    if (!trx::test::runBuiltinTest()) {
        std::cerr << "Builtin test failed.\n";
        return 1;
    }

    std::cout << "All tests passed!\n";
    return 0;
}