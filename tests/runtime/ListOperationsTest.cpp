#include "TestUtils.h"

#include <iostream>

namespace trx::test {

bool runListOperationsTest() {
    std::cout << "Running list operations test...\n";
    constexpr const char *source = R"TRX(
        ROUTINE test_lists() : INTEGER {
            var numbers list(INTEGER);

            append(numbers, 10);
            append(numbers, 20);
            append(numbers, 30);

            var length := len(numbers);
            RETURN length;
        }
    )TRX";

    std::cout << "Source code to parse:\n" << source << "\n";

    trx::parsing::ParserDriver driver;
    std::cout << "Parsing source...\n";
    if (!driver.parseString(source, "list_operations.trx")) {
        reportDiagnostics(driver);
        return false;
    }

    std::cout << "Parsing successful. Executing function...\n";
    trx::runtime::Interpreter interpreter(driver.context().module(), nullptr); // Use default SQLite driver

    trx::runtime::JsonValue input = trx::runtime::JsonValue::object(); // Empty input

    const auto outputOpt = interpreter.execute("test_lists", input);
    if (!outputOpt) {
        std::cerr << "Function did not return a value\n";
        return false;
    }
    const auto &output = *outputOpt;

    if (!std::holds_alternative<double>(output.data) || std::get<double>(output.data) != 3.0) {
        std::cerr << "Expected output 3, got: " << output << "\n";
        return false;
    }

    std::cout << "List operations test passed.\n";
    return true;
}

} // namespace trx::test

int main() {
    if (!trx::test::runListOperationsTest()) {
        std::cerr << "List operations test failed.\n";
        return 1;
    }

    std::cout << "All tests passed!\n";
    return 0;
}